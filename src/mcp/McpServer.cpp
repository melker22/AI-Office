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
namespace {

constexpr int kMaxFrameBytes = 32 * 1024 * 1024;

bool startsWithContentLength(const QByteArray &buffer)
{
    return buffer.left(64).toLower().startsWith("content-length:");
}

// Legacy LSP-style input. Only when the buffer itself starts with the header.
// Returns false when the frame is incomplete (*needMore) or was skipped.
bool takeContentLengthMessage(QByteArray *buffer, QJsonObject *msg, bool *needMore)
{
    *needMore = false;
    const int headerEndR = buffer->indexOf("\r\n\r\n");
    const int headerEndN = buffer->indexOf("\n\n");
    int sep = -1;
    int sepLen = 0;
    if (headerEndR >= 0 && (headerEndN < 0 || headerEndR <= headerEndN)) {
        sep = headerEndR;
        sepLen = 4;
    } else if (headerEndN >= 0) {
        sep = headerEndN;
        sepLen = 2;
    }
    if (sep < 0) {
        if (buffer->size() > 65536)
            buffer->clear();
        *needMore = !buffer->isEmpty();
        return false;
    }
    int length = -1;
    const QList<QByteArray> lines = buffer->left(sep).split('\n');
    for (QByteArray line : lines) {
        line = line.trimmed();
        if (line.endsWith('\r'))
            line.chop(1);
        if (line.toLower().startsWith("content-length:")) {
            bool ok = false;
            length = line.mid(line.indexOf(':') + 1).trimmed().toInt(&ok);
            if (!ok)
                length = -1;
        }
    }
    if (length < 0 || length > kMaxFrameBytes) {
        buffer->remove(0, sep + sepLen);
        return false;
    }
    if (buffer->size() < sep + sepLen + length) {
        *needMore = true;
        return false;
    }
    const QByteArray body = buffer->mid(sep + sepLen, length);
    buffer->remove(0, sep + sepLen + length);
    const auto doc = QJsonDocument::fromJson(body);
    if (!doc.isObject())
        return false;
    *msg = doc.object();
    return true;
}

bool parseToolArguments(const QJsonObject &params, QJsonObject *args, QString *error)
{
    if (!params.contains(QStringLiteral("arguments"))) {
        *args = {};
        return true;
    }
    const QJsonValue value = params.value(QStringLiteral("arguments"));
    if (value.isObject()) {
        *args = value.toObject();
        return true;
    }
    if (value.isString()) {
        const QByteArray raw = value.toString().trimmed().toUtf8();
        if (raw.isEmpty()) {
            *args = {};
            return true;
        }
        QJsonParseError parseError;
        const auto doc = QJsonDocument::fromJson(raw, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            *error = QStringLiteral("invalid JSON arguments");
            return false;
        }
        *args = doc.object();
        return true;
    }
    *error = QStringLiteral("invalid JSON arguments");
    return false;
}

} // namespace

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
        if (startsWithContentLength(buffer_)) {
            const int before = buffer_.size();
            QJsonObject msg;
            bool needMore = false;
            if (takeContentLengthMessage(&buffer_, &msg, &needMore))
                handleMessage(msg);
            if (needMore)
                return;
            if (buffer_.size() == before) {
                buffer_.clear();
                return;
            }
            continue;
        }
        const int nl = buffer_.indexOf('\n');
        if (nl < 0) {
            if (buffer_.size() > kMaxFrameBytes)
                buffer_.clear();
            return;
        }
        const QByteArray line = buffer_.left(nl).trimmed();
        buffer_.remove(0, nl + 1);
        if (line.isEmpty())
            continue;
        const auto doc = QJsonDocument::fromJson(line);
        if (doc.isObject())
            handleMessage(doc.object());
    }
}

void McpServer::writeMessage(const QJsonObject &msg)
{
    // One compact JSON object per line. Never a Content-Length header.
    QByteArray body = QJsonDocument(msg).toJson(QJsonDocument::Compact);
    body.append('\n');
    fwrite(body.constData(), 1, static_cast<size_t>(body.size()), stdout);
    fflush(stdout);
}

QJsonObject McpServer::invokeTool(const QString &name, const QJsonObject &args)
{
    if (callMode_ == CallMode::Undecided) {
        if (!qEnvironmentVariableIsEmpty("AIOFFICE_HEADLESS")) {
            callMode_ = CallMode::Headless;
        } else {
            QString detail;
            const IpcProbe probe = ipcProbe(&detail);
            if (probe == IpcProbe::Connected) {
                callMode_ = CallMode::Gui;
            } else if (probe == IpcProbe::NoGui) {
                callMode_ = CallMode::Headless;
            } else {
                // Stay off the local session for the rest of the process.
                callMode_ = CallMode::Gui;
                return toolResultErr(detail.isEmpty() ? QStringLiteral("IPC failure: connect failed") : detail);
            }
        }
    }
    if (callMode_ == CallMode::Headless)
        return session_->call(name, args);

    QString ipcErr;
    const QJsonObject out = ipcCall(name, args, &ipcErr);
    if (!ipcErr.isEmpty())
        return out;
    if (!out.contains(QStringLiteral("ok")))
        return toolResultErr(QStringLiteral("IPC failure: invalid response"));
    return out;
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
        QJsonObject args;
        QString argError;
        if (!parseToolArguments(params, &args, &argError)) {
            replyErr(-32602, argError);
            return;
        }
        const QJsonObject out = invokeTool(name, args);
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
