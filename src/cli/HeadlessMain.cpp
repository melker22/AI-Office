#include "cli/Cli.h"
#include "core/Session.h"
#include "mcp/McpServer.h"

#include <QByteArray>
#include <QGuiApplication>

// CLI/MCP entry point with no QML. CircleCI builds this target so the smoke
// suite does not have to wait on qmlimportscanner + 23 QML files + the GUI
// link — that was the 77/108 wall that never finished on the plan's large
// runner. Same tool surface as `aioffice play` / `aioffice mcp`.

int main(int argc, char *argv[])
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    // The smoke binary stays headless so a playbook never attaches to a live
    // GUI. AIOFFICE_HEADLESS=0 is the test hook for the GUI-IPC policy the
    // full `aioffice` binary uses when the variable is unset.
    if (qgetenv("AIOFFICE_HEADLESS") == "0")
        qunsetenv("AIOFFICE_HEADLESS");
    else
        qputenv("AIOFFICE_HEADLESS", QByteArrayLiteral("1"));

    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("AI Office"));
    app.setOrganizationName(QStringLiteral("AIOffice"));
    app.setApplicationVersion(QStringLiteral(AIOFFICE_VERSION));

    const QStringList args = app.arguments();
    if (args.size() >= 2 && args[1] == QLatin1String("mcp")) {
        aioffice::Session session;
        aioffice::McpServer server(&session);
        if (!server.startStdio())
            return 1;
        return app.exec();
    }
    return aioffice::runCli(args);
}