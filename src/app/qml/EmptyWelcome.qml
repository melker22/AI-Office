import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: welcome
    visible: app.welcomeVisible

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(720, parent.width - 48)
        height: Math.min(parent.height - 48, 640)
        color: Theme.paper100
        opacity: 0.0
    }

    Column {
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: Math.max(48, (parent.width - 720) / 2 + 88)
        spacing: 8

        Text {
            text: "AI Office"
            color: Theme.ink
            font.family: Theme.serifFont
            font.pixelSize: 34
            font.weight: Font.DemiBold
        }
        Text {
            text: "WRITER"
            color: Theme.inkMuted
            font.family: Theme.uiFont
            font.pixelSize: 15
            font.letterSpacing: 2
            font.weight: Font.Medium
        }

        Row {
            spacing: 12
            topPadding: 24

            UiButton {
                text: "Novo documento"
                kind: "primary"
                h: 44
                width: 176
                onClicked: app.newDocument()
            }

            UiButton {
                text: "Abrir arquivo"
                kind: "secondary"
                h: 44
                width: 150
                onClicked: welcome.requestOpen()
            }
        }

        Text {
            topPadding: 8
            text: "ODT · DOCX · DOC · PDF"
            color: Theme.inkMuted
            font.family: Theme.uiFont
            font.pixelSize: 11
            font.letterSpacing: 1
        }

        Item { height: 28; width: 1 }

        Rectangle {
            width: 2
            height: 26
            color: Theme.accent

            SequentialAnimation on opacity {
                loops: Animation.Infinite
                NumberAnimation { to: 0.15; duration: 700 }
                NumberAnimation { to: 1; duration: 700 }
            }
        }
    }

    signal requestOpen()
}
