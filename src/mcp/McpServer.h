#pragma once

#include "core/Session.h"

#include <QByteArray>
#include <QObject>

namespace aioffice {

class McpServer : public QObject {
    Q_OBJECT
public:
    explicit McpServer(Session *session, QObject *parent = nullptr);
    bool startStdio();

private:
    void onStdin();
    void handleBuffer();
    void handleMessage(const QJsonObject &msg);
    void writeMessage(const QJsonObject &msg);

    Session *session_ = nullptr;
    QByteArray buffer_;
};

} // namespace aioffice
