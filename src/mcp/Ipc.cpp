#include "mcp/Ipc.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>

#include <cstdio>

#ifndef Q_OS_WIN
#include <unistd.h>
#endif

namespace aioffice {
namespace {

QString ipcUserToken()
{
#ifdef Q_OS_WIN
    QString user = QString::fromUtf8(qgetenv("USERNAME"));
    if (user.isEmpty())
        user = QString::fromUtf8(qgetenv("USER"));
    if (user.isEmpty())
        user = QStringLiteral("user");
    return user;
#else
    return QString::number(static_cast<qulonglong>(::getuid()));
#endif
}

int ipcReadTimeoutMs()
{
    bool ok = false;
    const int v = qEnvironmentVariableIntValue("AIOFFICE_IPC_READ_MS", &ok);
    if (!ok || v <= 0)
        return 30000;
    return qBound(50, v, 120000);
}

QString ipcFailure(const QString &detail)
{
    if (detail.startsWith(QLatin1String("IPC failure:")))
        return detail;
    return QStringLiteral("IPC failure: ") + detail;
}

bool isNoGuiError(QLocalSocket::LocalSocketError err)
{
    return err == QLocalSocket::ServerNotFoundError || err == QLocalSocket::ConnectionRefusedError;
}

void warnWithoutIpc(const QString &message)
{
    qWarning("%s", qPrintable(message));
    fprintf(stderr, "%s\n", qPrintable(message));
    fflush(stderr);
}

QJsonObject unwrapToolResult(const QJsonObject &obj, bool *ok)
{
    *ok = false;
    if (obj.contains(QStringLiteral("result"))) {
        const QJsonValue result = obj.value(QStringLiteral("result"));
        if (result.isObject()) {
            const QJsonObject ro = result.toObject();
            if (ro.contains(QStringLiteral("ok"))) {
                *ok = true;
                return ro;
            }
            const QJsonArray content = ro.value(QStringLiteral("content")).toArray();
            if (!content.isEmpty()) {
                const QString text = content.at(0).toObject().value(QStringLiteral("text")).toString();
                const auto inner = QJsonDocument::fromJson(text.toUtf8());
                if (inner.isObject() && inner.object().contains(QStringLiteral("ok"))) {
                    *ok = true;
                    return inner.object();
                }
            }
        }
    }
    if (obj.contains(QStringLiteral("ok"))) {
        *ok = true;
        return obj;
    }
    return {};
}

} // namespace

QString ipcSocketPath()
{
#ifdef Q_OS_WIN
    return QStringLiteral("aioffice-") + ipcUserToken();
#else
    QString dir = QString::fromUtf8(qgetenv("XDG_RUNTIME_DIR"));
    if (dir.isEmpty())
        dir = QDir::tempPath();
    return dir + QStringLiteral("/aioffice-") + ipcUserToken() + QStringLiteral(".sock");
#endif
}

IpcProbe ipcProbe(QString *detail)
{
    QLocalSocket s;
    s.connectToServer(ipcSocketPath());
    if (s.waitForConnected(500)) {
        s.disconnectFromServer();
        return IpcProbe::Connected;
    }
    if (isNoGuiError(s.error()))
        return IpcProbe::NoGui;
    if (detail) {
        const QString why = s.errorString().isEmpty() ? QStringLiteral("connect failed") : s.errorString();
        *detail = ipcFailure(why);
    }
    return IpcProbe::Failed;
}

bool ipcAvailable()
{
    return ipcProbe(nullptr) == IpcProbe::Connected;
}

QJsonObject ipcCall(const QString &tool, const QJsonObject &args, QString *error)
{
    auto fail = [&](const QString &why) {
        const QString msg = ipcFailure(why);
        if (error)
            *error = msg;
        return toolResultErr(msg);
    };

    QLocalSocket s;
    s.connectToServer(ipcSocketPath());
    if (!s.waitForConnected(500)) {
        const QString why = s.errorString().isEmpty() ? QStringLiteral("connect failed") : s.errorString();
        return fail(why);
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
    if (s.write(line) != line.size() || !s.waitForBytesWritten(5000))
        return fail(QStringLiteral("connect failed"));

    QByteArray resp;
    QElapsedTimer timer;
    timer.start();
    const int budget = ipcReadTimeoutMs();
    bool peerClosed = false;
    while (!resp.contains('\n') && timer.elapsed() < budget) {
        const int remain = budget - int(timer.elapsed());
        if (remain <= 0)
            break;
        if (s.bytesAvailable() == 0 && !s.waitForReadyRead(remain)) {
            if (s.state() == QLocalSocket::UnconnectedState || s.error() == QLocalSocket::PeerClosedError) {
                peerClosed = true;
                resp += s.readAll();
            }
            break;
        }
        resp += s.readAll();
        if (resp.size() > 8 * 1024 * 1024)
            return fail(QStringLiteral("invalid response"));
    }
    if (!resp.contains('\n')) {
        if (peerClosed || !resp.isEmpty())
            return fail(QStringLiteral("partial frame"));
        return fail(QStringLiteral("timeout"));
    }

    const QByteArray payload = resp.left(resp.indexOf('\n')).trimmed();
    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return fail(QStringLiteral("invalid response"));
    bool have = false;
    const QJsonObject toolResult = unwrapToolResult(doc.object(), &have);
    if (!have || !toolResult.contains(QStringLiteral("ok")))
        return fail(QStringLiteral("invalid response"));
    if (error)
        *error = QString();
    return toolResult;
}

IpcServer::IpcServer(Session *session, QObject *parent)
    : QObject(parent)
    , session_(session)
{
}

bool IpcServer::start()
{
    auto *server = new QLocalServer(this);
    server->setSocketOptions(QLocalServer::UserAccessOption);

    const QString path = ipcSocketPath();
    QLocalSocket probe;
    probe.connectToServer(path);
    if (probe.waitForConnected(300)) {
        probe.disconnectFromServer();
        warnWithoutIpc(QStringLiteral("AI Office IPC: socket %1 is already served; this process will run without IPC")
                           .arg(path));
        return false;
    }
    if (!isNoGuiError(probe.error())) {
        warnWithoutIpc(QStringLiteral("AI Office IPC: socket %1 did not accept a probe (%2); this process will run without IPC")
                           .arg(path, probe.errorString()));
        return false;
    }

    // Only an orphan (nobody accepted the probe) may be removed.
    QLocalServer::removeServer(path);
    if (!server->listen(path)) {
        warnWithoutIpc(QStringLiteral("AI Office IPC: listen failed on %1 (%2); this process will run without IPC")
                           .arg(path, server->errorString()));
        return false;
    }

    QObject::connect(server, &QLocalServer::newConnection, this, [this, server]() {
        while (auto *sock = server->nextPendingConnection()) {
            QObject::connect(sock, &QLocalSocket::readyRead, this, [this, sock]() { onSocketReady(sock); });
            QObject::connect(sock, &QLocalSocket::disconnected, this, [this, sock]() {
                pending_.remove(sock);
                sock->deleteLater();
            });
        }
    });
    return true;
}

void IpcServer::onSocketReady(QLocalSocket *sock)
{
    QByteArray &buf = pending_[sock];
    buf.append(sock->readAll());
    if (buf.size() > 8 * 1024 * 1024) {
        pending_.remove(sock);
        sock->disconnectFromServer();
        return;
    }
    while (buf.indexOf('\n') >= 0) {
        const int nl = buf.indexOf('\n');
        QByteArray line = buf.left(nl).trimmed();
        buf.remove(0, nl + 1);
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
}

} // namespace aioffice
