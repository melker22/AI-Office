import QtQuick

Rectangle {
    id: ruler
    height: 24
    color: Theme.surface

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Theme.hairline
    }

    Item {
        id: track
        width: Math.min(720, parent.width - 80)
        height: parent.height
        anchors.horizontalCenter: parent.horizontalCenter

        Repeater {
            model: 20
            Rectangle {
                x: index * (track.width / 20)
                y: 8
                width: 1
                height: index % 2 === 0 ? 10 : 6
                color: Theme.hairlineStrong
            }
        }

        Rectangle {
            width: Math.max(8, track.width * 0.08)
            height: parent.height
            color: Theme.dark ? "#3A2818" : "#E4DDD2"
            opacity: 0.7
        }
        Rectangle {
            anchors.right: parent.right
            width: Math.max(8, track.width * 0.08)
            height: parent.height
            color: Theme.dark ? "#3A2818" : "#E4DDD2"
            opacity: 0.7
        }

        Rectangle {
            x: track.width * 0.08 - 5
            anchors.verticalCenter: parent.verticalCenter
            width: 10
            height: 10
            rotation: 45
            color: Theme.accent
        }
        Rectangle {
            x: track.width * 0.92 - 5
            anchors.verticalCenter: parent.verticalCenter
            width: 10
            height: 10
            rotation: 45
            color: Theme.accent
        }
    }
}
