import QtQuick
import QtQuick.Layouts

Rectangle {
    id: sheet
    visible: app.unsavedVisible
    color: "#2A24201A"
    z: 40

    MouseArea {
        anchors.fill: parent
        onClicked: app.cancelUnsaved()
    }

    Rectangle {
        width: 420
        implicitHeight: col.implicitHeight
        anchors.centerIn: parent
        radius: 16
        color: Theme.paper
        border.color: Theme.hairline
        border.width: 1

        MouseArea { anchors.fill: parent }

        Column {
            id: col
            width: parent.width
            padding: 32
            spacing: 16

            Rectangle {
                width: 56
                height: 56
                radius: 28
                color: Theme.accent50
                anchors.horizontalCenter: parent.horizontalCenter
                Text {
                    anchors.centerIn: parent
                    text: "▤"
                    color: Theme.accent
                    font.pixelSize: 22
                }
            }

            Text {
                width: parent.width - 64
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Documento não salvo"
                color: Theme.ink
                font.family: Theme.uiFont
                font.pixelSize: 19
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
            }
            Text {
                width: parent.width - 64
                anchors.horizontalCenter: parent.horizontalCenter
                wrapMode: Text.WordWrap
                text: "Deseja salvar as alterações em “" + (app.title.length ? app.title : "Sem título") + "” antes de continuar?"
                color: Theme.inkMuted
                font.family: Theme.uiFont
                font.pixelSize: 14
                horizontalAlignment: Text.AlignHCenter
            }

            Item { height: 8; width: 1 }

            UiButton {
                width: parent.width - 64
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Salvar alterações"
                kind: "primary"
                h: 40
                onClicked: app.confirmSave()
            }
            UiButton {
                width: parent.width - 64
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Descartar"
                kind: "destructive"
                h: 40
                onClicked: app.confirmDiscard()
            }
            UiButton {
                width: parent.width - 64
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Cancelar"
                kind: "ghost"
                h: 40
                onClicked: app.cancelUnsaved()
            }
        }
    }
}
