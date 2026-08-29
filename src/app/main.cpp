#include "app/AppController.h"
#include "app/DocumentCanvas.h"
#include "cli/Cli.h"
#include "core/Session.h"
#include "mcp/McpServer.h"

#include <QColor>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QPalette>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QUrl>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("AI Office"));
    app.setOrganizationName(QStringLiteral("AIOffice"));
    app.setApplicationVersion(QStringLiteral(AIOFFICE_VERSION));

    const QStringList args = app.arguments();
    if (args.size() >= 2 && args[1] == QLatin1String("mcp")) {
        aioffice::Session session;
        aioffice::McpServer server(&session);
        server.startStdio();
        return app.exec();
    }
    if (args.size() >= 2 && args[1] != QLatin1String("gui") && args[1] != QLatin1String("--gui")) {
        return aioffice::runCli(args);
    }

    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QPalette pal = app.palette();
    pal.setColor(QPalette::Window, QColor(QStringLiteral("#FBF7F0")));
    pal.setColor(QPalette::WindowText, QColor(QStringLiteral("#2A2420")));
    pal.setColor(QPalette::Base, QColor(QStringLiteral("#FDFBF7")));
    pal.setColor(QPalette::Text, QColor(QStringLiteral("#2A2420")));
    pal.setColor(QPalette::Button, QColor(QStringLiteral("#FBF7F0")));
    pal.setColor(QPalette::ButtonText, QColor(QStringLiteral("#2A2420")));
    pal.setColor(QPalette::Highlight, QColor(QStringLiteral("#B5651D")));
    pal.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#FFFFFF")));
    pal.setColor(QPalette::ToolTipBase, QColor(QStringLiteral("#2A2420")));
    pal.setColor(QPalette::ToolTipText, QColor(QStringLiteral("#FDFBF7")));
    pal.setColor(QPalette::PlaceholderText, QColor(QStringLiteral("#8A7D6C")));
    pal.setColor(QPalette::Mid, QColor(QStringLiteral("#E4DDD2")));
    pal.setColor(QPalette::Dark, QColor(QStringLiteral("#362F28")));
    pal.setColor(QPalette::Light, QColor(QStringLiteral("#FDFBF7")));
    app.setPalette(pal);
    qmlRegisterType<aioffice::DocumentCanvas>("AIOffice", 1, 0, "DocumentCanvas");

    aioffice::AppController controller;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("app"), &controller);
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app, []() { QCoreApplication::exit(1); },
        Qt::QueuedConnection);
    engine.loadFromModule("AIOffice", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;
    return app.exec();
}
