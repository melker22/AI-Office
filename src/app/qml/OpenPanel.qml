import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: overlay
    visible: false
    color: "#2A242050"
    z: 20

    signal browse()
    signal closed()

    MouseArea {
        anchors.fill: parent
        onClicked: overlay.close()
    }

    function open() { visible = true }
    function close() { visible = false; closed() }

    Rectangle {
        id: panel
        width: 620
        anchors.centerIn: parent
        radius: 16
        color: Theme.paper
        border.color: Theme.hairline
        border.width: 1
        height: col.implicitHeight + 56

        MouseArea { anchors.fill: parent }

        Column {
            id: col
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 28
            spacing: 16

            RowLayout {
                width: parent.width
                Label {
                    text: "Abrir"
                    color: Theme.ink
                    font.family: Theme.uiFont
                    font.pixelSize: 19
                    font.weight: Font.DemiBold
                    Layout.fillWidth: true
                }
                IconBtn { glyph: "×"; onClicked: overlay.close() }
            }

            Rectangle {
                id: drop
                width: parent.width
                height: 132
                radius: 12
                color: dropArea.containsDrag ? Theme.accent50 : "transparent"
                border.width: 2
                border.color: dropArea.containsDrag ? Theme.accent : Theme.hairlineStrong

                DropArea {
                    id: dropArea
                    anchors.fill: parent
                    onDropped: {
                        if (drop.hasUrls && drop.urls.length > 0) {
                            app.openPath(drop.urls[0])
                            overlay.close()
                        }
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: overlay.browse()
                    cursorShape: Qt.PointingHandCursor
                }
                Column {
                    anchors.centerIn: parent
                    spacing: 6
                    Text { text: "↑"; color: Theme.inkMuted; font.pixelSize: 22; anchors.horizontalCenter: parent.horizontalCenter }
                    Text {
                        text: "Solte o arquivo aqui"
                        color: Theme.inkSoft
                        font.family: Theme.uiFont
                        font.pixelSize: 13
                        font.weight: Font.Medium
                        anchors.horizontalCenter: parent.horizontalCenter
                    }
                    Text {
                        text: "ou clique para procurar"
                        color: Theme.inkMuted
                        font.family: Theme.uiFont
                        font.pixelSize: 11
                        anchors.horizontalCenter: parent.horizontalCenter
                    }
                }
            }

            Text {
                text: "RECENTES"
                color: Theme.inkMuted
                font.family: Theme.uiFont
                font.pixelSize: 11
                font.weight: Font.DemiBold
                font.letterSpacing: 1
            }

            Repeater {
                model: app.recentFiles
                delegate: Rectangle {
                    required property var modelData
                    width: col.width
                    height: 52
                    radius: 8
                    color: recMa.containsMouse ? Theme.hover : "transparent"
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 12
                        Rectangle {
                            width: 36
                            height: 36
                            radius: 8
                            color: Theme.accent50
                            Text {
                                anchors.centerIn: parent
                                text: "▢"
                                color: Theme.accent
                                font.pixelSize: 14
                            }
                        }
                        Column {
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                text: modelData.name || modelData.path
                                color: Theme.ink
                                font.family: Theme.uiFont
                                font.pixelSize: 13
                                font.weight: Font.Medium
                                elide: Text.ElideMiddle
                                width: parent.width
                            }
                            Text {
                                text: modelData.path
                                color: Theme.inkMuted
                                font.family: Theme.uiFont
                                font.pixelSize: 11
                                elide: Text.ElideMiddle
                                width: parent.parent.width - 80
                            }
                        }
                        Rectangle {
                            radius: 4
                            color: Theme.surfaceAlt
                            width: extLab.implicitWidth + 16
                            height: 22
                            Text {
                                id: extLab
                                anchors.centerIn: parent
                                text: (modelData.ext || "").toString().toUpperCase()
                                color: Theme.inkMuted
                                font.family: Theme.uiFont
                                font.pixelSize: 10
                                font.weight: Font.DemiBold
                            }
                        }
                    }
                    MouseArea {
                        id: recMa
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            app.openPath(modelData.path)
                            overlay.close()
                        }
                    }
                }
            }

            Text {
                visible: app.recentFiles.length === 0
                text: "Nenhum arquivo recente"
                color: Theme.inkMuted
                font.family: Theme.uiFont
                font.pixelSize: 13
            }

            Rectangle { width: parent.width; height: 1; color: Theme.hairline }

            RowLayout {
                width: parent.width
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: "Formatos suportados: ODT · DOCX · DOC · RTF · AIO (JSON nativo)"
                    color: Theme.inkMuted
                    font.family: Theme.uiFont
                    font.pixelSize: 11
                }
                UiButton {
                    text: "Procurar arquivo"
                    kind: "secondary"
                    h: 36
                    onClicked: overlay.browse()
                }
            }
        }
    }
}
