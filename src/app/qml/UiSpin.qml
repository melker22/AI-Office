import QtQuick

Item {
    id: root
    property int value: 20
    property int from: 5
    property int to: 80
    property string suffix: "mm"
    signal valueModified()

    implicitWidth: 140
    implicitHeight: 40

    function bump(delta) {
        const next = Math.max(root.from, Math.min(root.to, root.value + delta))
        if (next === root.value)
            return
        root.value = next
        root.valueModified()
    }

    Rectangle {
        anchors.fill: parent
        radius: 8
        color: ma.containsMouse ? Theme.accent50 : Theme.paper
        border.color: ma.containsMouse ? Theme.accent : Theme.hairlineStrong
        border.width: ma.containsMouse ? 2 : 1

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            text: root.value
            color: Theme.ink
            font.family: Theme.uiFont
            font.pixelSize: 13
            font.weight: Font.Medium
        }

        Text {
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            text: root.suffix
            color: Theme.inkMuted
            font.family: Theme.uiFont
            font.pixelSize: 11
        }

        MouseArea {
            id: ma
            anchors.fill: parent
            hoverEnabled: true
            onWheel: function (w) {
                root.bump(w.angleDelta.y > 0 ? 1 : -1)
            }
            onClicked: root.bump(1)
        }
    }
}
