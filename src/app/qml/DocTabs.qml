import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: tabs
    height: 36
    color: Theme.surfaceAlt

    signal requestNew()

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Theme.hairline
    }

    Row {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.leftMargin: 12
        spacing: 4
        height: 32

        Repeater {
            model: app.tabs
            delegate: Rectangle {
                required property var modelData
                required property int index
                width: tabInner.implicitWidth + 28
                height: 32
                radius: 8
                color: modelData.active ? Theme.paper : "transparent"
                border.color: modelData.active ? Theme.hairline : "transparent"
                border.width: modelData.active ? 1 : 0

                Rectangle {
                    visible: modelData.active
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 2
                    color: Theme.paper
                }

                Row {
                    id: tabInner
                    anchors.centerIn: parent
                    spacing: 8
                    Text {
                        text: "☰"
                        color: modelData.active ? Theme.accent : Theme.inkMuted
                        font.pixelSize: 11
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: modelData.title
                        color: modelData.active ? Theme.ink : Theme.inkMuted
                        font.family: Theme.uiFont
                        font.pixelSize: 12
                        font.weight: modelData.active ? Font.DemiBold : Font.Normal
                        elide: Text.ElideRight
                        width: Math.min(implicitWidth, 160)
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Rectangle {
                        visible: modelData.dirty
                        width: 6
                        height: 6
                        radius: 3
                        color: modelData.active ? Theme.accent : "#B5651D66"
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: "×"
                        color: Theme.inkMuted
                        font.pixelSize: 11
                        anchors.verticalCenter: parent.verticalCenter
                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -6
                            onClicked: app.closeTab(index)
                        }
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    z: -1
                    onClicked: app.switchTab(index)
                }
            }
        }

        IconBtn {
            glyph: "+"
            tip: "Novo documento"
            width: 28
            height: 28
            anchors.verticalCenter: parent.verticalCenter
            onClicked: tabs.requestNew()
        }
    }
}
