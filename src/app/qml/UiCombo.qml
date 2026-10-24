import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    property var model: []
    property string text: ""
    signal activated(string value)

    implicitWidth: 140
    implicitHeight: 32

    Rectangle {
        id: shell
        anchors.fill: parent
        radius: 8
        color: Theme.accent50
        border.color: Theme.accent
        border.width: 1

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 8
            spacing: 4
            Text {
                Layout.fillWidth: true
                text: root.text
                color: Theme.ink
                font.family: Theme.uiFont
                font.pixelSize: 13
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            Text {
                text: popup.visible ? "▴" : "▾"
                color: Theme.inkMuted
                font.pixelSize: 9
            }
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: popup.visible ? popup.close() : popup.open()
        }
    }

    Popup {
        id: popup
        y: root.height + 4
        width: Math.max(root.width, 160)
        padding: 6
        modal: false
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
        background: Rectangle {
            radius: 10
            color: Theme.paper
            border.color: Theme.hairline
            border.width: 1
        }
        contentItem: Column {
            spacing: 2
            Repeater {
                model: root.model
                delegate: Rectangle {
                    required property string modelData
                    width: popup.width - 12
                    height: 32
                    radius: 6
                    color: itemMa.containsMouse ? Theme.accent50 : "transparent"
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.leftMargin: 10
                        text: modelData
                        color: Theme.ink
                        font.family: Theme.uiFont
                        font.pixelSize: 13
                        font.weight: modelData === root.text ? Font.DemiBold : Font.Normal
                    }
                    MouseArea {
                        id: itemMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            root.activated(modelData)
                            popup.close()
                        }
                    }
                }
            }
        }
    }
}
