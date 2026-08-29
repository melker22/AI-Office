import QtQuick
import QtQuick.Layouts

Rectangle {
    id: rail
    width: 52
    color: Theme.surface

    Rectangle {
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: Theme.hairline
    }

    Column {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 12
        spacing: 4

        IconBtn {
            glyph: "☰"
            tip: "Estrutura"
            active: true
            onClicked: app.inspectorTab = 0
        }
        IconBtn {
            glyph: "◎"
            tip: "Integração"
            active: app.inspectorTab === 1
            onClicked: app.inspectorTab = 1
        }
        IconBtn {
            glyph: "↺"
            tip: "Desfazer"
            onClicked: app.undo()
        }
    }
}
