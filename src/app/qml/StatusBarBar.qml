import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    height: 32
    color: Theme.titleBar

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        spacing: 12

        Row {
            spacing: 4
            IconBtn {
                glyph: "‹"
                tip: "Página anterior"
                onDark: true
                width: 28
                height: 28
                onClicked: app.page = app.page - 1
            }
            Label {
                text: app.tableSelected
                      ? ("Tabela " + app.tableRows + "×" + app.tableCols)
                      : ("Página " + app.page + " de " + app.pages)
                color: Theme.titleMuted
                font.family: Theme.uiFont
                font.pixelSize: 11
                anchors.verticalCenter: parent.verticalCenter
            }
            IconBtn {
                glyph: "›"
                tip: "Próxima página"
                onDark: true
                width: 28
                height: 28
                onClicked: app.page = app.page + 1
            }
        }
        Label { text: "·"; color: "#4E453B" }
        Label {
            text: app.words + " palavras"
            color: Theme.titleMuted
            font.family: Theme.uiFont
            font.pixelSize: 11
        }
        Label { text: "·"; color: "#4E453B" }
        Label {
            text: "pt-BR"
            color: Theme.titleMuted
            font.family: Theme.uiFont
            font.pixelSize: 11
        }
        Label { text: "·"; color: "#4E453B" }
        Row {
            spacing: 6
            Rectangle {
                width: 6
                height: 6
                radius: 3
                color: app.aiConnected ? Theme.accent : "#6B6155"
                anchors.verticalCenter: parent.verticalCenter
            }
            Label {
                text: app.aiEditedFlash ? "IA editou" : (app.aiConnected ? "IA conectada" : "IA desconectada")
                color: app.aiConnected ? Theme.accent : Theme.titleMuted
                font.family: Theme.uiFont
                font.pixelSize: 11
                font.weight: app.aiConnected ? Font.Medium : Font.Normal
            }
        }

        Item { Layout.fillWidth: true }

        Label {
            visible: !app.libreOffice
            text: "sem LibreOffice"
            color: Theme.titleMuted
            font.family: Theme.uiFont
            font.pixelSize: 11
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: app.showLoHint()
            }
        }

        IconBtn {
            glyph: app.darkMode ? "☾" : "☀"
            tip: "Claro / escuro"
            onDark: true
            width: 28
            height: 28
            onClicked: app.toggleDark()
        }

        Label {
            text: Math.round(app.zoom * 100) + "%"
            color: Theme.titleMuted
            font.family: Theme.uiFont
            font.pixelSize: 11
        }
        MouseArea {
            width: 48
            height: 28
            onWheel: (w) => {
                app.zoom = app.zoom + (w.angleDelta.y > 0 ? 0.05 : -0.05)
            }
        }
    }
}
