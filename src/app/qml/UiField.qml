import QtQuick

Item {
    id: root
    property alias text: input.text
    property alias input: input
    property string placeholder: ""
    signal accepted()

    implicitHeight: 20

    function forceActiveFocus() { input.forceActiveFocus() }

    TextInput {
        id: input
        anchors.fill: parent
        color: Theme.ink
        font.family: Theme.uiFont
        font.pixelSize: 13
        selectionColor: Theme.accent
        selectedTextColor: Theme.accentInk
        clip: true
        verticalAlignment: TextInput.AlignVCenter
        onAccepted: root.accepted()
    }

    Text {
        visible: input.text.length === 0
        text: root.placeholder
        color: Theme.inkMuted
        font.family: Theme.uiFont
        font.pixelSize: 13
        anchors.fill: parent
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
