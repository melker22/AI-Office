#include "mcp/Ipc.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QJsonArray>
#include <QStandardPaths>

namespace aioffice {

QString ipcSocketPath()
{
#ifdef Q_OS_WIN
    return QStringLiteral("aioffice");
#else
    QString dir = QString::fromUtf8(qgetenv("XDG_RUNTIME_DIR"));
    if (dir.isEmpty())
        dir = QDir::tempPath();
    return dir + QStringLiteral("/aioffice.sock");
#endif
}

bool ipcAvailable()
{
    QLocalSocket s;
    s.connectToServer(ipcSocketPath());
    if (!s.waitForConnected(150))
        return false;
    s.disconnectFromServer();
    return true;
}

QJsonObject ipcCall(const QString &tool, const QJsonObject &args, QString *error)
{
    QLocalSocket s;
    s.connectToServer(ipcSocketPath());
    if (!s.waitForConnected(500)) {
        if (error)
            *error = QStringLiteral("GUI not running");
        return toolResultErr(QStringLiteral("GUI not running"));
    }
    QJsonObject req;
    req[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    req[QStringLiteral("id")] = 1;
    req[QStringLiteral("method")] = QStringLiteral("tools/call");
    req[QStringLiteral("params")] = QJsonObject{
        {QStringLiteral("name"), tool},
        {QStringLiteral("arguments"), args},
    };
    const QByteArray line = QJsonDocument(req).toJson(QJsonDocument::Compact) + '\n';
    s.write(line);
    s.flush();
    if (!s.waitForReadyRead(30000)) {
        if (error)
            *error = QStringLiteral("IPC timeout");
        return toolResultErr(QStringLiteral("IPC timeout"));
    }
    QByteArray resp = s.readAll();
    while (!resp.contains('\n') && s.waitForReadyRead(5000))
        resp += s.readAll();
    const auto doc = QJsonDocument::fromJson(resp.trimmed());
    if (!doc.isObject()) {
        if (error)
            *error = QStringLiteral("Bad IPC response");
        return toolResultErr(QStringLiteral("Bad IPC response"));
    }
    const auto obj = doc.object();
    if (obj.contains(QStringLiteral("result"))) {
        const auto result = obj.value(QStringLiteral("result"));
        if (result.isObject())
            return result.toObject();
        // MCP-style content wrapper
        const auto content = result.toObject().value(QStringLiteral("content")).toArray();
        if (!content.isEmpty()) {
            const QString text = content.at(0).toObject().value(QStringLiteral("text")).toString();
            const auto inner = QJsonDocument::fromJson(text.toUtf8());
            if (inner.isObject())
                return inner.object();
        }
    }
    if (obj.contains(QStringLiteral("ok")))
        return obj;
    if (error)
        *error = obj.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString();
    return obj;
}

IpcServer::IpcServer(Session *session, QObject *parent)
    : QObject(parent)
    , session_(session)
{
}

bool IpcServer::start()
{
    auto *server = new QLocalServer(this);
    QLocalServer::removeServer(ipcSocketPath());
    if (!server->listen(ipcSocketPath()))
        return false;
    QObject::connect(server, &QLocalServer::newConnection, this, [this, server]() {
        while (auto *sock = server->nextPendingConnection()) {
            QObject::connect(sock, &QLocalSocket::readyRead, this, [this, sock]() {
                while (sock->canReadLine() || sock->bytesAvailable() > 0) {
                    QByteArray line = sock->canReadLine() ? sock->readLine() : sock->readAll();
                    line = line.trimmed();
                    if (line.isEmpty())
                        continue;
                    const auto doc = QJsonDocument::fromJson(line);
                    if (!doc.isObject())
                        continue;
                    const auto msg = doc.object();
                    const QString method = msg.value(QStringLiteral("method")).toString();
                    QJsonObject out;
                    QString toolName = method;
                    if (method == QLatin1String("tools/call") || method == QLatin1String("call")) {
                        const auto params = msg.value(QStringLiteral("params")).toObject();
                        toolName = params.value(QStringLiteral("name")).toString();
                        out = session_->call(toolName, params.value(QStringLiteral("arguments")).toObject());
                    } else {
                        out = session_->call(method, msg.value(QStringLiteral("params")).toObject());
                    }
                    emit toolUsed(toolName);
                    QJsonObject resp;
                    resp[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
                    resp[QStringLiteral("id")] = msg.value(QStringLiteral("id"));
                    resp[QStringLiteral("result")] = out;
                    sock->write(QJsonDocument(resp).toJson(QJsonDocument::Compact) + '\n');
                    sock->flush();
                }
            });
            QObject::connect(sock, &QLocalSocket::disconnected, sock, &QObject::deleteLater);
        }
    });
    return true;
}

} // namespace aioffice
