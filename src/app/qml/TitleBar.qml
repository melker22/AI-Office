import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

Rectangle {
    id: bar
    property Window window
    height: 44
    color: Theme.titleBar

    DragHandler {
        target: null
        onActiveChanged: if (active && bar.window)
                             bar.window.startSystemMove()
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 8
        spacing: 8

        Rectangle {
            width: 24
            height: 24
            radius: 6
            color: Theme.accent
            Text {
                anchors.centerIn: parent
                text: "✒"
                color: Theme.paper
                font.pixelSize: 11
            }
        }

        Label {
            text: "AI Office"
            color: Theme.titleText
            font.family: Theme.uiFont
            font.pixelSize: 13
            font.weight: Font.Medium
        }

        Label {
            text: "·"
            color: Theme.titleMuted
            font.pixelSize: 13
        }

        Label {
            Layout.fillWidth: true
            text: app.title.length ? app.title : "Documento sem título"
            color: app.dirty ? Theme.titleText : Theme.titleMuted
            font.family: Theme.uiFont
            font.pixelSize: 13
            elide: Text.ElideMiddle
        }

        Rectangle {
            visible: app.dirty
            width: 6
            height: 6
            radius: 3
            color: Theme.accent
        }

        IconBtn {
            glyph: "—"
            tip: "Minimizar"
            onDark: true
            onClicked: if (bar.window) bar.window.showMinimized()
        }
        IconBtn {
            glyph: bar.window && bar.window.visibility === Window.Maximized ? "❐" : "□"
            tip: "Maximizar"
            onDark: true
            onClicked: {
                if (!bar.window)
                    return
                bar.window.visibility = bar.window.visibility === Window.Maximized
                        ? Window.Windowed : Window.Maximized
            }
        }
        IconBtn {
            glyph: "×"
            tip: "Fechar"
            onDark: true
            danger: true
            onClicked: if (bar.window) bar.window.close()
        }
    }
}
