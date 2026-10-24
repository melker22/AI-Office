import QtQuick

Rectangle {
    id: tip
    property string text: ""
    property bool active: false
    visible: opacity > 0.05 && text.length > 0
    opacity: 0
    z: 1000
    radius: 8
    color: Theme.toastBg
    width: lab.implicitWidth + 16
    height: 28

    Text {
        id: lab
        anchors.centerIn: parent
        text: tip.text
        color: Theme.paper100
        font.family: Theme.uiFont
        font.pixelSize: 11
    }

    Timer {
        id: delay
        interval: 350
        onTriggered: if (tip.active && tip.text.length) tip.opacity = 1
    }

    onActiveChanged: {
        if (active && text.length)
            delay.restart()
        else {
            delay.stop()
            opacity = 0
        }
    }

    Behavior on opacity { NumberAnimation { duration: 120 } }
}
