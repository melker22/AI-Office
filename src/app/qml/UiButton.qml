import QtQuick

Item {
    id: root
    property string text: ""
    property string kind: "secondary" // primary | secondary | ghost | destructive
    property int h: 40
    signal clicked()

    implicitHeight: h
    implicitWidth: Math.max(88, lab.implicitWidth + 32)
    height: implicitHeight

    Rectangle {
        anchors.fill: parent
        radius: 8
        color: {
            if (!root.enabled)
                return Theme.surfaceAlt
            if (root.kind === "primary")
                return ma.containsMouse ? Theme.accentHover : Theme.accent
            if (root.kind === "destructive")
                return ma.containsMouse ? "#9C361F" : Theme.destructive
            return ma.containsMouse ? Theme.hover : "transparent"
        }
        border.width: (root.kind === "secondary" || root.kind === "ghost") && root.enabled ? 1 : 0
        border.color: root.kind === "secondary" ? Theme.hairlineStrong : "transparent"
    }

    Text {
        id: lab
        anchors.centerIn: parent
        text: root.text
        color: {
            if (!root.enabled)
                return Theme.inkMuted
            if (root.kind === "primary" || root.kind === "destructive")
                return Theme.accentInk
            return Theme.ink
        }
        font.family: Theme.uiFont
        font.pixelSize: 13
        font.weight: Font.DemiBold
    }

    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: true
        enabled: root.enabled
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
