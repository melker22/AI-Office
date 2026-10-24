import QtQuick
import QtQuick.Controls

Item {
    id: root
    property string glyph: ""
    property string tip: ""
    property bool active: false
    property bool danger: false
    property bool onDark: false
    property bool boldGlyph: false
    property bool italicGlyph: false
    property bool underlineGlyph: false
    signal clicked()

    width: 32
    height: 32

    Rectangle {
        anchors.fill: parent
        radius: 8
        color: root.active ? (root.onDark ? "#332C26" : Theme.accent50)
                           : (ma.containsMouse ? (root.onDark ? "#332C26" : Theme.hover) : "transparent")
        border.width: root.active && !root.onDark ? 1 : 0
        border.color: Theme.accent

        Text {
            anchors.centerIn: parent
            text: root.glyph
            color: root.danger ? Theme.destructive
                               : (root.active && !root.onDark ? Theme.accent
                                                              : (root.onDark ? Theme.iconOnDark : Theme.icon))
            font.family: Theme.uiFont
            font.pixelSize: 13
            font.bold: root.boldGlyph
            font.italic: root.italicGlyph
            font.underline: root.underlineGlyph
        }
    }

    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }

    UiTip {
        id: bubble
        parent: Overlay.overlay
        text: root.tip
        active: ma.containsMouse && Overlay.overlay
        x: {
            if (!Overlay.overlay)
                return 0
            const p = root.mapToItem(Overlay.overlay, 0, 0)
            return p.x + Math.round((root.width - bubble.width) / 2)
        }
        y: {
            if (!Overlay.overlay)
                return 0
            const p = root.mapToItem(Overlay.overlay, 0, 0)
            return p.y + root.height + 6
        }
    }
}
