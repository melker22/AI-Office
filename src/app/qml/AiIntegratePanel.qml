import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    radius: 12
    color: app.aiConnected ? Theme.accent50 : Theme.paper
    border.color: app.aiConnected ? Theme.accent : Theme.hairline
    border.width: 1
    implicitHeight: col.implicitHeight + 32

    Column {
        id: col
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 16
        spacing: 10

        Row {
            spacing: 8
            Rectangle {
                width: 8
                height: 8
                radius: 4
                color: app.aiConnected ? Theme.accent : Theme.inkMuted
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: app.aiConnected ? "IA conectada" : "Integração com IA"
                color: Theme.ink
                font.family: Theme.uiFont
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }
            Text {
                visible: app.aiConnected
                text: "★"
                color: Theme.accent
                font.pixelSize: 11
            }
        }

        Text {
            width: parent.width
            wrapMode: Text.WordWrap
            color: Theme.inkSoft
            font.family: Theme.uiFont
            font.pixelSize: 13
            lineHeight: 1.45
            text: app.aiConnected
                  ? "A IA do usuário pode ler e editar este documento com IDs, estilos e layout — sem créditos deste aplicativo."
                  : app.integrateBlurb
        }

        Flow {
            visible: !app.aiConnected
            width: parent.width
            spacing: 6
            Repeater {
                model: ["Cursor", "Claude Code", "Grok Build", "Copilot", "Gemini", "ChatGPT", "Kimi"]
                delegate: Rectangle {
                    required property string modelData
                    radius: 6
                    color: Theme.surfaceAlt
                    width: chip.implicitWidth + 16
                    height: 24
                    Text {
                        id: chip
                        anchors.centerIn: parent
                        text: modelData
                        color: Theme.inkSoft
                        font.family: Theme.uiFont
                        font.pixelSize: 11
                    }
                }
            }
        }

        Text {
            visible: app.aiConnected
            text: "MCP · cli aioffice"
            color: Theme.inkMuted
            font.family: Theme.uiFont
            font.pixelSize: 11
            font.letterSpacing: 0.4
        }

        Column {
            visible: app.aiConnected && app.lastTool.length > 0
            width: parent.width
            spacing: 2
            topPadding: 8
            Rectangle { width: parent.width; height: 1; color: Theme.accent100 }
            Text {
                topPadding: 8
                text: "Última ação"
                color: Theme.inkMuted
                font.family: Theme.uiFont
                font.pixelSize: 11
            }
            Text {
                text: app.lastTool + " · agora"
                color: Theme.ink
                font.family: "JetBrains Mono"
                font.pixelSize: 12
            }
        }

        UiButton {
            visible: !app.aiConnected
            width: parent.width
            text: "Copiar instrução"
            kind: "primary"
            h: 40
            onClicked: app.copyIntegratePrompt()
        }
    }
}
