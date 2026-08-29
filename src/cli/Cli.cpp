#include "cli/Cli.h"

#include "core/Session.h"
#include "mcp/Ipc.h"
#include "mcp/ToolDispatch.h"

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

QJsonObject parseArgsJson(const QString &s)
{
    if (s.isEmpty())
        return {};
    const auto d = QJsonDocument::fromJson(s.toUtf8());
    return d.object();
}

QJsonObject dispatch(const QString &tool, const QJsonObject &args)
{
    QString err;
    if (ipcAvailable() && qEnvironmentVariableIsEmpty("AIOFFICE_HEADLESS")) {
        auto r = ipcCall(tool, args, &err);
        if (err.isEmpty() || r.value(QStringLiteral("ok")).toBool())
            return r;
    }
    Session session;
    return session.call(tool, args);
}

void usage()
{
    fprintf(stderr, R"(AI Office Writer — aioffice %s

Usage:
  aioffice                 Launch the GUI
  aioffice mcp             MCP server on stdio (Cursor / Claude Code / Grok Build)
  aioffice call TOOL JSON  Call a semantic tool (live GUI if open, else headless)
  aioffice play FILE.json  Run a JSON array of {tool, args} (atomic batch; not StarBasic)
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
        if (args.size() >= 4)
            a = parseArgsJson(args[3]);
        else if (!isatty(fileno(stdin))) {
            QByteArray raw;
            char buf[4096];
            while (fgets(buf, sizeof(buf), stdin))
                raw += buf;
            a = QJsonDocument::fromJson(raw).object();
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
        const bool live = ipcAvailable() && qEnvironmentVariableIsEmpty("AIOFFICE_HEADLESS");
        Session session;
        QJsonArray results;
        for (const auto &v : calls) {
            const QJsonObject c = v.toObject();
            const QString t = c.value(QStringLiteral("tool")).toString();
            const QJsonObject a = c.value(QStringLiteral("args")).toObject();
            QJsonObject r;
            if (live) {
                QString err;
                r = ipcCall(t, a, &err);
                if (!err.isEmpty() && !r.value(QStringLiteral("ok")).toBool())
                    r = toolResultErr(err);
            } else {
                r = session.call(t, a);
            }
            results.append(r);
            if (!r.value(QStringLiteral("ok")).toBool()) {
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
        if (ipcAvailable()) {
            auto r = ipcCall(QStringLiteral("document_new"), a);
            if (args.size() >= 3 && r.value(QStringLiteral("ok")).toBool())
                r = ipcCall(QStringLiteral("document_save"),
                            QJsonObject{{QStringLiteral("path"), args[2]}});
            printJson(r);
            return r.value(QStringLiteral("ok")).toBool() ? 0 : 1;
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
