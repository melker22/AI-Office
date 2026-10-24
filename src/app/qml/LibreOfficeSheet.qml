import QtQuick
import QtQuick.Layouts

Item {
    id: hint
    visible: app.loHintVisible
    z: 45
    anchors.left: parent.left
    anchors.right: parent.right
    anchors.top: parent.top
    anchors.topMargin: 64
    height: card.height

    Rectangle {
        id: card
        width: Math.min(600, parent.width - 48)
        anchors.horizontalCenter: parent.horizontalCenter
        radius: 16
        color: Theme.toastBg
        border.color: "#4B4239"
        border.width: 1
        height: inner.implicitHeight + 40

        Row {
            id: inner
            anchors.fill: parent
            anchors.margins: 20
            spacing: 16

            Rectangle {
                width: 40
                height: 40
                radius: 12
                color: "#B4402A33"
                Text {
                    anchors.centerIn: parent
                    text: "!"
                    color: Theme.destructive
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                }
            }

            Column {
                width: card.width - 96
                spacing: 8
                Text {
                    text: "LibreOffice não encontrado"
                    color: Theme.paper100
                    font.family: Theme.uiFont
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                }
                Text {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: "Arquivos .doc precisam do LibreOffice. ODT, DOCX, AIO JSON e PDF nativo não dependem dele."
                    color: "#AFA391"
                    font.family: Theme.uiFont
                    font.pixelSize: 13
                }
                Row {
                    spacing: 8
                    UiButton {
                        text: "Entendi"
                        kind: "primary"
                        h: 32
                        onClicked: app.hideLoHint()
                    }
                    UiButton {
                        text: "Ignorar"
                        kind: "ghost"
                        h: 32
                        onClicked: app.hideLoHint()
                    }
                }
            }
        }

        IconBtn {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 8
            glyph: "×"
            onDark: true
            onClicked: app.hideLoHint()
        }
    }
}
