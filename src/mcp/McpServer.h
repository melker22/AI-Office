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
    enum class CallMode { Undecided, Gui, Headless };

    void onStdin();
    void handleBuffer();
    void handleMessage(const QJsonObject &msg);
    void writeMessage(const QJsonObject &msg);
    QJsonObject invokeTool(const QString &name, const QJsonObject &args);

    Session *session_ = nullptr;
    QByteArray buffer_;
    // Chosen on the first tools/call. initialize and tools/list do not lock it.
    CallMode callMode_ = CallMode::Undecided;
};

} // namespace aioffice
