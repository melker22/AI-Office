import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: bar
    visible: app.findVisible
    height: visible ? 48 : 0
    color: Theme.surface

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Theme.hairline
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 12
        spacing: 8

        Rectangle {
            Layout.preferredWidth: 280
            Layout.preferredHeight: 36
            radius: 8
            color: Theme.paper
            border.color: Theme.accent
            border.width: 1
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 6
                Text { text: "⌕"; color: Theme.inkMuted; font.pixelSize: 12 }
                UiField {
                    id: findField
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    text: app.findText
                    placeholder: "Localizar…"
                    onTextChanged: app.findText = text
                    onAccepted: app.findNext()
                }
            }
        }

        IconBtn { glyph: "↑"; tip: "Anterior"; onClicked: app.findPrev() }
        IconBtn { glyph: "↓"; tip: "Próximo"; onClicked: app.findNext() }
        IconBtn {
            glyph: ".*"
            tip: "Expressão regular"
            active: app.findRegex
            onClicked: app.findRegex = !app.findRegex
        }

        Rectangle { width: 1; height: 24; color: Theme.hairlineStrong }

        Rectangle {
            Layout.preferredWidth: 280
            Layout.preferredHeight: 36
            radius: 8
            color: Theme.paper
            border.color: Theme.hairlineStrong
            border.width: 1
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                Text { text: "⇄"; color: Theme.inkMuted; font.pixelSize: 12 }
                UiField {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    text: app.replaceText
                    placeholder: "Substituir por…"
                    onTextChanged: app.replaceText = text
                    onAccepted: app.findReplace(false)
                }
            }
        }

        UiButton {
            text: "Substituir"
            kind: "secondary"
            h: 36
            onClicked: app.findReplace(false)
        }

        UiButton {
            text: "Substituir tudo"
            kind: "secondary"
            h: 36
            onClicked: app.findReplace(true)
        }

        Item { Layout.fillWidth: true }

        Text {
            text: "Ctrl+F"
            color: Theme.inkMuted
            font.family: "JetBrains Mono"
            font.pixelSize: 11
        }

        IconBtn { glyph: "×"; tip: "Fechar"; onClicked: app.findVisible = false }
    }

    onVisibleChanged: if (visible) findField.forceActiveFocus()
}
