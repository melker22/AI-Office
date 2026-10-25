#include "cli/Cli.h"

#include "core/Session.h"
#include "mcp/Ipc.h"
#include "mcp/ToolDispatch.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>

#ifndef Q_OS_WIN
#include <unistd.h>
#else
#include <io.h>
#define isatty _isatty
#define fileno _fileno
#endif

namespace aioffice {
namespace {

void printJson(const QJsonObject &o)
{
    const QByteArray b = QJsonDocument(o).toJson(QJsonDocument::Indented);
    fwrite(b.constData(), 1, static_cast<size_t>(b.size()), stdout);
}

// Dot-path lookup into a JSON value: "document.body.0.runs.1.bold" walks
// objects by key and arrays by numeric index. Returns QJsonValue::Undefined
// if any segment doesn't resolve.
QJsonValue valueAtPath(const QJsonValue &root, const QString &path)
{
    QJsonValue cur = root;
    for (const QString &seg : path.split(QLatin1Char('.'), Qt::SkipEmptyParts)) {
        bool isIdx = false;
        const int idx = seg.toInt(&isIdx);
        if (isIdx && cur.isArray()) {
            const QJsonArray arr = cur.toArray();
            if (idx < 0 || idx >= arr.size())
                return QJsonValue(QJsonValue::Undefined);
            cur = arr.at(idx);
        } else if (cur.isObject()) {
            const QJsonObject obj = cur.toObject();
            if (!obj.contains(seg))
                return QJsonValue(QJsonValue::Undefined);
            cur = obj.value(seg);
        } else {
            return QJsonValue(QJsonValue::Undefined);
        }
    }
    return cur;
}

static bool jsonNumber(const QJsonValue &v, double *out)
{
    if (v.isDouble()) {
        *out = v.toDouble();
        return true;
    }
    if (v.isBool()) {
        *out = v.toBool() ? 1.0 : 0.0;
        return true;
    }
    return false;
}

static bool expectValueMatches(const QJsonValue &actual, const QJsonValue &expected)
{
    if (expected.isObject()) {
        const QJsonObject eo = expected.toObject();
        if (eo.size() == 1) {
            const auto it = eo.begin();
            const QString op = it.key();
            double a = 0;
            double e = 0;
            if ((op == QLatin1String("$gt") || op == QLatin1String("$lt") || op == QLatin1String("$gte")
                 || op == QLatin1String("$lte") || op == QLatin1String("$ne"))
                && jsonNumber(actual, &a) && jsonNumber(it.value(), &e)) {
                if (op == QLatin1String("$gt"))
                    return a > e;
                if (op == QLatin1String("$lt"))
                    return a < e;
                if (op == QLatin1String("$gte"))
                    return a >= e;
                if (op == QLatin1String("$lte"))
                    return a <= e;
                if (op == QLatin1String("$ne"))
                    return a != e;
            }
        }
    }
    return actual == expected;
}

// A tool result always has "ok", including {ok:false}. ipcCall sets ipcError for
// a dead GUI, a timeout, or a payload that is not a tool result. That is a
// transport failure: play aborts even when the step's expect is {"ok":false}.
enum class PlayReplyKind { Tool, Transport };

PlayReplyKind classifyPlayReply(const QString &ipcError, bool responseHasOk)
{
    if (!ipcError.isEmpty() || !responseHasOk)
        return PlayReplyKind::Transport;
    return PlayReplyKind::Tool;
}

int playClassifySelfCheck()
{
    struct Case {
        const char *err;
        bool hasOk;
        PlayReplyKind want;
    };
    const Case cases[] = {
        {"IPC timeout", true, PlayReplyKind::Transport},
        {"GUI not running", true, PlayReplyKind::Transport},
        {"Bad IPC response", false, PlayReplyKind::Transport},
        {"IPC failure: timeout", true, PlayReplyKind::Transport},
        {"IPC failure: partial frame", true, PlayReplyKind::Transport},
        {"IPC failure: invalid response", false, PlayReplyKind::Transport},
        {"", false, PlayReplyKind::Transport},
        {"", true, PlayReplyKind::Tool},
    };
    for (const Case &c : cases) {
        if (classifyPlayReply(QString::fromUtf8(c.err), c.hasOk) != c.want) {
            fprintf(stderr, "play classify mismatch for ipcError='%s' hasOk=%d\n", c.err, c.hasOk ? 1 : 0);
            return 1;
        }
    }
    return 0;
}

bool parseArgsJson(const QString &s, QJsonObject *out, QString *error)
{
    const QString trimmed = s.trimmed();
    if (trimmed.isEmpty()) {
        *out = {};
        return true;
    }
    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(trimmed.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error)
            *error = QStringLiteral("invalid JSON arguments");
        return false;
    }
    *out = doc.object();
    return true;
}

QJsonObject dispatch(const QString &tool, const QJsonObject &args)
{
    if (!qEnvironmentVariableIsEmpty("AIOFFICE_HEADLESS")) {
        Session session;
        return session.call(tool, args);
    }
    QString detail;
    const IpcProbe probe = ipcProbe(&detail);
    if (probe == IpcProbe::NoGui) {
        Session session;
        return session.call(tool, args);
    }
    if (probe == IpcProbe::Failed)
        return toolResultErr(detail.isEmpty() ? QStringLiteral("IPC failure: connect failed") : detail);
    QString err;
    const QJsonObject r = ipcCall(tool, args, &err);
    if (!err.isEmpty())
        return r;
    if (!r.contains(QStringLiteral("ok")))
        return toolResultErr(QStringLiteral("IPC failure: invalid response"));
    return r;
}

void usage()
{
    fprintf(stderr, R"(AI Office Writer — aioffice %s

Usage:
  aioffice                 Launch the GUI
  aioffice mcp             MCP server on stdio (Cursor / Claude Code / Grok Build)
  aioffice call TOOL JSON  Call a semantic tool (live GUI if open, else headless)
  aioffice play FILE.json  Run steps in order and stop at the first failing expect (not StarBasic)
  aioffice tools           List tool names
  aioffice new [file]      Blank document (optional save path)
  aioffice get [file]      Print semantic JSON
  aioffice open FILE       Open a file in the live GUI or print it
  aioffice save FILE       Save live/headless session
  aioffice convert IN OUT  Open IN and save as OUT (odt/docx/json/pdf)
  aioffice outline [file]

Docs: docs/mcp-tools.md
Skill: skills/SKILL.md
)",
            AIOFFICE_VERSION);
}

} // namespace

int runCli(const QStringList &args)
{
    if (qEnvironmentVariableIntValue("AIOFFICE_PLAY_CLASSIFY_CHECK") == 1)
        return playClassifySelfCheck();
    // Test hooks. They do not change the GUI-IPC policy; they only let a
    // checker print the socket path or run the real IpcServer in-process.
    if (qEnvironmentVariableIntValue("AIOFFICE_PRINT_IPC_PATH") == 1) {
        printf("%s\n", qPrintable(ipcSocketPath()));
        return 0;
    }
    if (qEnvironmentVariableIntValue("AIOFFICE_IPC_SERVE") == 1) {
        Session session;
        IpcServer server(&session);
        const bool up = server.start();
        fprintf(stderr, "ipc serving %s %s\n", qPrintable(ipcSocketPath()), up ? "up" : "inactive");
        fflush(stderr);
        QCoreApplication *app = QCoreApplication::instance();
        if (!app)
            return 1;
        return app->exec();
    }
    if (args.size() <= 1 || args[1] == QLatin1String("-h") || args[1] == QLatin1String("--help")) {
        usage();
        return args.size() <= 1 ? 2 : 0;
    }
    const QString cmd = args[1];
    if (cmd == QLatin1String("tools")) {
        for (const auto &n : toolNames())
            printf("%s\n", qPrintable(n));
        return 0;
    }
    if (cmd == QLatin1String("call")) {
        if (args.size() < 3) {
            fprintf(stderr, "aioffice call TOOL [json-args]\n");
            return 2;
        }
        QJsonObject a;
        QString jsonErr;
        bool parsed = true;
        if (args.size() >= 4)
            parsed = parseArgsJson(args[3], &a, &jsonErr);
        else if (!isatty(fileno(stdin))) {
            QByteArray raw;
            char buf[4096];
            while (fgets(buf, sizeof(buf), stdin))
                raw += buf;
            parsed = parseArgsJson(QString::fromUtf8(raw), &a, &jsonErr);
        }
        if (!parsed) {
            printJson(toolResultErr(jsonErr));
            return 1;
        }
        const auto r = dispatch(args[2], a);
        printJson(r);
        return r.value(QStringLiteral("ok")).toBool() ? 0 : 1;
    }
    if (cmd == QLatin1String("play")) {
        if (args.size() < 3) {
            fprintf(stderr, "aioffice play FILE.json\n");
            return 2;
        }
        QFile f(args[2]);
        if (!f.open(QIODevice::ReadOnly)) {
            fprintf(stderr, "cannot read %s\n", qPrintable(args[2]));
            return 1;
        }
        const auto docj = QJsonDocument::fromJson(f.readAll());
        QJsonArray calls;
        if (docj.isArray())
            calls = docj.array();
        else if (docj.isObject())
            calls = docj.object().value(QStringLiteral("calls")).toArray();
        if (calls.isEmpty()) {
            fprintf(stderr, "playbook needs a JSON array of {tool, args} or {calls:[...]}\n");
            return 2;
        }
        bool live = false;
        if (qEnvironmentVariableIsEmpty("AIOFFICE_HEADLESS")) {
            QString detail;
            const IpcProbe probe = ipcProbe(&detail);
            if (probe == IpcProbe::Failed) {
                const QString why = detail.isEmpty() ? QStringLiteral("IPC failure: connect failed") : detail;
                QJsonObject out;
                out[QStringLiteral("ok")] = false;
                out[QStringLiteral("error")] = QStringLiteral("playbook transport failure: %1").arg(why);
                out[QStringLiteral("results")] = QJsonArray{};
                printJson(out);
                return 1;
            }
            live = probe == IpcProbe::Connected;
        }
        Session session;
        QJsonArray results;
        for (const auto &v : calls) {
            const QJsonObject c = v.toObject();
            const QString t = c.value(QStringLiteral("tool")).toString();
            const QJsonObject a = c.value(QStringLiteral("args")).toObject();
            if (c.contains(QStringLiteral("expect"))) {
                const QJsonValue expectV = c.value(QStringLiteral("expect"));
                if (!expectV.isObject() || expectV.toObject().isEmpty()) {
                    QJsonObject out;
                    out[QStringLiteral("ok")] = false;
                    out[QStringLiteral("error")] = QStringLiteral("expect must be a non-empty object");
                    out[QStringLiteral("results")] = results;
                    printJson(out);
                    return 1;
                }
            }
            QJsonObject r;
            if (live) {
                QString ipcError;
                r = ipcCall(t, a, &ipcError);
                if (classifyPlayReply(ipcError, r.contains(QStringLiteral("ok"))) == PlayReplyKind::Transport) {
                    const QString why = ipcError.isEmpty() ? QStringLiteral("IPC failed") : ipcError;
                    results.append(toolResultErr(why));
                    QJsonObject out;
                    out[QStringLiteral("ok")] = false;
                    out[QStringLiteral("error")] = QStringLiteral("playbook transport failure: %1").arg(why);
                    out[QStringLiteral("results")] = results;
                    printJson(out);
                    return 1;
                }
            } else {
                r = session.call(t, a);
            }
            results.append(r);
            const bool hasExpect = c.contains(QStringLiteral("expect"));
            if (hasExpect) {
                const QJsonObject expect = c.value(QStringLiteral("expect")).toObject();
                for (auto it = expect.begin(); it != expect.end(); ++it) {
                    const QJsonValue actual = valueAtPath(r, it.key());
                    if (!expectValueMatches(actual, it.value())) {
                        QJsonObject out;
                        out[QStringLiteral("ok")] = false;
                        out[QStringLiteral("error")] = QStringLiteral("expect failed for %1 at %2: got %3, want %4")
                            .arg(t, it.key(),
                                 QString::fromUtf8(QJsonDocument(QJsonArray{actual}).toJson(QJsonDocument::Compact)),
                                 QString::fromUtf8(QJsonDocument(QJsonArray{it.value()}).toJson(QJsonDocument::Compact)));
                        out[QStringLiteral("results")] = results;
                        printJson(out);
                        return 1;
                    }
                }
            } else if (!r.value(QStringLiteral("ok")).toBool()) {
                QJsonObject out;
                out[QStringLiteral("ok")] = false;
                out[QStringLiteral("error")] = r.value(QStringLiteral("error"));
                out[QStringLiteral("results")] = results;
                printJson(out);
                return 1;
            }
        }
        QJsonObject out;
        out[QStringLiteral("ok")] = true;
        out[QStringLiteral("results")] = results;
        printJson(out);
        return 0;
    }
    if (cmd == QLatin1String("new")) {
        QJsonObject a;
        if (args.size() >= 3)
            a[QStringLiteral("title")] = QFileInfo(args[2]).completeBaseName();
        if (qEnvironmentVariableIsEmpty("AIOFFICE_HEADLESS")) {
            QString detail;
            const IpcProbe probe = ipcProbe(&detail);
            if (probe == IpcProbe::Failed) {
                printJson(toolResultErr(detail.isEmpty() ? QStringLiteral("IPC failure: connect failed") : detail));
                return 1;
            }
            if (probe == IpcProbe::Connected) {
                QString err;
                auto r = ipcCall(QStringLiteral("document_new"), a, &err);
                if (!err.isEmpty()) {
                    printJson(r);
                    return 1;
                }
                if (args.size() >= 3 && r.value(QStringLiteral("ok")).toBool()) {
                    r = ipcCall(QStringLiteral("document_save"),
                                QJsonObject{{QStringLiteral("path"), args[2]}}, &err);
                    if (!err.isEmpty()) {
                        printJson(r);
                        return 1;
                    }
                }
                printJson(r);
                return r.value(QStringLiteral("ok")).toBool() ? 0 : 1;
            }
        }
        Session s;
        auto r = s.call(QStringLiteral("document_new"), a);
        if (args.size() >= 3 && r.value(QStringLiteral("ok")).toBool())
            r = s.call(QStringLiteral("document_save"), QJsonObject{{QStringLiteral("path"), args[2]}});
        printJson(r);
        return r.value(QStringLiteral("ok")).toBool() ? 0 : 1;
    }
    if (cmd == QLatin1String("get")) {
        if (args.size() >= 3) {
            Session s;
            auto r = s.call(QStringLiteral("document_open"), QJsonObject{{QStringLiteral("path"), args[2]}});
            if (!r.value(QStringLiteral("ok")).toBool()) {
                printJson(r);
                return 1;
            }
            r = s.call(QStringLiteral("document_get"), {});
            printJson(r);
            return 0;
        }
        printJson(dispatch(QStringLiteral("document_get"), {}));
        return 0;
    }
    if (cmd == QLatin1String("open")) {
        if (args.size() < 3) {
            fprintf(stderr, "aioffice open FILE\n");
            return 2;
        }
        const auto r = dispatch(QStringLiteral("document_open"), QJsonObject{{QStringLiteral("path"), args[2]}});
        printJson(r);
        return r.value(QStringLiteral("ok")).toBool() ? 0 : 1;
    }
    if (cmd == QLatin1String("save")) {
        if (args.size() < 3) {
            fprintf(stderr, "aioffice save FILE\n");
            return 2;
        }
        const auto r = dispatch(QStringLiteral("document_save"), QJsonObject{{QStringLiteral("path"), args[2]}});
        printJson(r);
        return r.value(QStringLiteral("ok")).toBool() ? 0 : 1;
    }
    if (cmd == QLatin1String("convert")) {
        if (args.size() < 4) {
            fprintf(stderr, "aioffice convert IN OUT\n");
            return 2;
        }
        Session s;
        auto r = s.call(QStringLiteral("document_open"), QJsonObject{{QStringLiteral("path"), args[2]}});
        if (!r.value(QStringLiteral("ok")).toBool()) {
            printJson(r);
            return 1;
        }
        r = s.call(QStringLiteral("document_save"), QJsonObject{{QStringLiteral("path"), args[3]}});
        printJson(r);
        return r.value(QStringLiteral("ok")).toBool() ? 0 : 1;
    }
    if (cmd == QLatin1String("outline")) {
        if (args.size() >= 3) {
            Session s;
            auto r = s.call(QStringLiteral("document_open"), QJsonObject{{QStringLiteral("path"), args[2]}});
            if (!r.value(QStringLiteral("ok")).toBool()) {
                printJson(r);
                return 1;
            }
            printJson(s.call(QStringLiteral("document_get_outline"), {}));
            return 0;
        }
        printJson(dispatch(QStringLiteral("document_get_outline"), {}));
        return 0;
    }
    fprintf(stderr, "Unknown command: %s\n", qPrintable(cmd));
    usage();
    return 2;
}

} // namespace aioffice
