#pragma once

#include "core/Session.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>

class QLocalSocket;

namespace aioffice {

QString ipcSocketPath();

// NoGui: nothing is listening (missing socket or connection refused).
// Connected: a server accepted the probe.
// Failed: the connect itself failed after the socket existed (timeout, and
// anything that is not "no server"). Callers must not edit a local document.
enum class IpcProbe { NoGui, Connected, Failed };

IpcProbe ipcProbe(QString *detail = nullptr);
bool ipcAvailable();
QJsonObject ipcCall(const QString &tool, const QJsonObject &args, QString *error = nullptr);

class IpcServer : public QObject {
    Q_OBJECT
public:
    explicit IpcServer(Session *session, QObject *parent = nullptr);
    bool start();

signals:
    void toolUsed(const QString &name);

private:
    void onSocketReady(QLocalSocket *sock);

    Session *session_ = nullptr;
    QHash<QLocalSocket *, QByteArray> pending_;
};

} // namespace aioffice
