#pragma once

#include "core/Session.h"

#include <QJsonObject>
#include <QObject>
#include <QString>

namespace aioffice {

QString ipcSocketPath();
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
    Session *session_ = nullptr;
};

} // namespace aioffice
