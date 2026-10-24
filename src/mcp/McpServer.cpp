#include "mcp/McpServer.h"

#include "mcp/Ipc.h"
#include "mcp/ToolDispatch.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSocketNotifier>
#include <QTimer>
#include <cstdio>

#ifndef Q_OS_WIN
#include <unistd.h>
#endif

namespace aioffice {

McpServer::McpServer(Session *session, QObject *parent)
    : QObject(parent)
    , session_(session)
{
}

bool McpServer::startStdio()
{
#ifdef Q_OS_WIN
    // Blocking read on a worker would be better; poll via QSocketNotifier is POSIX.
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this]() {
        char buf[4096];
        if (std::fgets(buf, sizeof(buf), stdin)) {
            buffer_.append(buf);
            handleBuffer();
        }
    });
    timer->start(20);
    return true;
#else
    auto *n = new QSocketNotifier(STDIN_FILENO, QSocketNotifier::Read, this);
    connect(n, &QSocketNotifier::activated, this, [this]() { onStdin(); });
    return true;
#endif
}

void McpServer::onStdin()
{
#ifndef Q_OS_WIN
    char buf[8192];
    const ssize_t n = ::read(STDIN_FILENO, buf, sizeof(buf));
    if (n <= 0) {
        QCoreApplication::quit();
        return;
    }
    buffer_.append(buf, int(n));
    handleBuffer();
#endif
}

void McpServer::handleBuffer()
{
    for (;;) {
        // Content-Length framing
        const int headerEnd = buffer_.indexOf("\r\n\r\n");
        const int headerEndN = buffer_.indexOf("\n\n");
        int sep = -1;
        int sepLen = 0;
        if (headerEnd >= 0) {
            sep = headerEnd;
            sepLen = 4;
        } else if (headerEndN >= 0 && (buffer_.startsWith("Content-Length:") || buffer_.startsWith("content-length:"))) {
            sep = headerEndN;
            sepLen = 2;
        }
        if (sep >= 0) {
            const QByteArray header = buffer_.left(sep);
            int length = -1;
            for (const auto &line : header.split('\n')) {
                const QByteArray l = line.trimmed();
                if (l.toLower().startsWith("content-length:")) {
                    length = l.mid(l.indexOf(':') + 1).trimmed().toInt();
                }
            }
            if (length < 0) {
                buffer_.remove(0, sep + sepLen);
                continue;
            }
            if (buffer_.size() < sep + sepLen + length)
                return;
            const QByteArray body = buffer_.mid(sep + sepLen, length);
            buffer_.remove(0, sep + sepLen + length);
            const auto doc = QJsonDocument::fromJson(body);
            if (doc.isObject())
                handleMessage(doc.object());
            continue;
        }
        // NDJSON fallback
        const int nl = buffer_.indexOf('\n');
        if (nl < 0)
            return;
        const QByteArray line = buffer_.left(nl).trimmed();
        buffer_.remove(0, nl + 1);
        if (line.isEmpty() || line.startsWith("Content-Length"))
            continue;
        if (!line.startsWith('{'))
            continue;
        const auto doc = QJsonDocument::fromJson(line);
        if (doc.isObject())
            handleMessage(doc.object());
    }
}

void McpServer::writeMessage(const QJsonObject &msg)
{
    const QByteArray body = QJsonDocument(msg).toJson(QJsonDocument::Compact);
    const QByteArray frame = "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body;
    fwrite(frame.constData(), 1, static_cast<size_t>(frame.size()), stdout);
    fflush(stdout);
}

void McpServer::handleMessage(const QJsonObject &msg)
{
    const QString method = msg.value(QStringLiteral("method")).toString();
    const QJsonValue id = msg.value(QStringLiteral("id"));
    const bool isNotif = !msg.contains(QStringLiteral("id"));

    auto reply = [&](const QJsonValue &result) {
        if (isNotif)
            return;
        QJsonObject r;
        r[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
        r[QStringLiteral("id")] = id;
        r[QStringLiteral("result")] = result;
        writeMessage(r);
    };
    auto replyErr = [&](int code, const QString &message) {
        if (isNotif)
            return;
        QJsonObject err;
        err[QStringLiteral("code")] = code;
        err[QStringLiteral("message")] = message;
        QJsonObject r;
        r[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
        r[QStringLiteral("id")] = id;
        r[QStringLiteral("error")] = err;
        writeMessage(r);
    };

    if (method == QLatin1String("initialize")) {
        QJsonObject caps;
        caps[QStringLiteral("tools")] = QJsonObject{};
        QJsonObject info;
        info[QStringLiteral("name")] = QStringLiteral("ai-office");
        info[QStringLiteral("version")] = QStringLiteral(AIOFFICE_VERSION);
        QJsonObject result;
        result[QStringLiteral("protocolVersion")] = QStringLiteral("2024-11-05");
        result[QStringLiteral("capabilities")] = caps;
        result[QStringLiteral("serverInfo")] = info;
        result[QStringLiteral("instructions")] = QStringLiteral(
            "AI Office Writer. Prefer document_get then mutate by block id. "
            "Use page_preview to verify appearance. No API keys. integrate_help explains attachment.");
        reply(result);
        return;
    }
    if (method == QLatin1String("notifications/initialized") || method == QLatin1String("initialized"))
        return;
    if (method == QLatin1String("tools/list")) {
        QJsonObject result;
        result[QStringLiteral("tools")] = toolDefinitions();
        reply(result);
        return;
    }
    if (method == QLatin1String("tools/call") || method == QLatin1String("call")) {
        const auto params = msg.value(QStringLiteral("params")).toObject();
        const QString name = params.value(QStringLiteral("name")).toString();
        QJsonObject args = params.value(QStringLiteral("arguments")).toObject();
        if (args.isEmpty() && params.value(QStringLiteral("arguments")).isString()) {
            args = QJsonDocument::fromJson(params.value(QStringLiteral("arguments")).toString().toUtf8()).object();
        }
        QJsonObject out;
        QString ipcErr;
        if (!session_->liveConnected() && ipcAvailable()) {
            out = ipcCall(name, args, &ipcErr);
            if (!ipcErr.isEmpty() && !out.value(QStringLiteral("ok")).toBool()) {
                // fall through to local
                out = session_->call(name, args);
            }
        } else {
            out = session_->call(name, args);
        }
        QJsonObject result;
        result[QStringLiteral("content")] = QJsonArray{QJsonObject{
            {QStringLiteral("type"), QStringLiteral("text")},
            {QStringLiteral("text"), QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Indented))},
        }};
        if (!out.value(QStringLiteral("ok")).toBool())
            result[QStringLiteral("isError")] = true;
        reply(result);
        return;
    }
    if (method == QLatin1String("ping")) {
        reply(QJsonObject{});
        return;
    }
    replyErr(-32601, QStringLiteral("Method not found: ") + method);
}

} // namespace aioffice
