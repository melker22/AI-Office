import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: sheet
    visible: false
    color: "#2A242033"
    z: 21

    signal browse()
    signal closed()

    function open() { visible = true }
    function close() { visible = false; closed() }

    property var formats: [
        { id: "docx", label: "DOCX" },
        { id: "odt", label: "ODT" },
        { id: "doc", label: "DOC" },
        { id: "pdf", label: "PDF" },
        { id: "json", label: "AIO JSON" }
    ]

    MouseArea {
        anchors.fill: parent
        onClicked: sheet.close()
    }

    Rectangle {
        id: card
        width: 400
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        color: Theme.paper
        border.color: Theme.hairline
        border.width: 1

        MouseArea { anchors.fill: parent }

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                Layout.margins: 24
                Label {
                    text: "Salvar como"
                    color: Theme.ink
                    font.family: Theme.uiFont
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                    Layout.fillWidth: true
                }
                IconBtn { glyph: "×"; onClicked: sheet.close() }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.hairline }

            Flickable {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentHeight: inner.implicitHeight
                Column {
                    id: inner
                    width: card.width - 48
                    x: 24
                    topPadding: 24
                    bottomPadding: 24
                    spacing: 12

                    Text {
                        text: "DESTINO"
                        color: Theme.inkMuted
                        font.family: Theme.uiFont
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                        font.letterSpacing: 1
                    }
                    Rectangle {
                        width: parent.width
                        height: 44
                        radius: 8
                        color: destMa.containsMouse ? Theme.hover : Theme.paper
                        border.color: Theme.hairlineStrong
                        border.width: 1
                        Row {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            spacing: 8
                            Text {
                                text: "▣"
                                color: Theme.inkMuted
                                font.pixelSize: 14
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Text {
                                text: "Escolher pasta e nome…"
                                color: Theme.ink
                                font.family: Theme.uiFont
                                font.pixelSize: 13
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                        MouseArea {
                            id: destMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: sheet.browse()
                        }
                    }

                    Text {
                        topPadding: 12
                        text: "FORMATO"
                        color: Theme.inkMuted
                        font.family: Theme.uiFont
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                        font.letterSpacing: 1
                    }

                    Grid {
                        width: parent.width
                        columns: 3
                        columnSpacing: 8
                        rowSpacing: 8
                        Repeater {
                            model: sheet.formats
                            delegate: Rectangle {
                                required property var modelData
                                width: (inner.width - 16) / 3
                                height: 64
                                radius: 12
                                color: app.saveFormat === modelData.id ? Theme.accent50 : "transparent"
                                border.width: app.saveFormat === modelData.id ? 2 : 1
                                border.color: app.saveFormat === modelData.id ? Theme.accent : Theme.hairline
                                Column {
                                    anchors.centerIn: parent
                                    spacing: 6
                                    Text {
                                        text: "▢"
                                        color: app.saveFormat === modelData.id ? Theme.accent : Theme.inkMuted
                                        font.pixelSize: 16
                                        anchors.horizontalCenter: parent.horizontalCenter
                                    }
                                    Text {
                                        text: modelData.label
                                        color: Theme.ink
                                        font.family: Theme.uiFont
                                        font.pixelSize: 12
                                        font.weight: app.saveFormat === modelData.id ? Font.DemiBold : Font.Medium
                                        anchors.horizontalCenter: parent.horizontalCenter
                                    }
                                    Text {
                                        visible: modelData.id === "pdf"
                                        text: "nativo"
                                        color: Theme.success
                                        font.family: Theme.uiFont
                                        font.pixelSize: 9
                                        font.weight: Font.DemiBold
                                        font.letterSpacing: 0.4
                                        anchors.horizontalCenter: parent.horizontalCenter
                                    }
                                    Text {
                                        visible: modelData.id === "doc"
                                        text: "LibreOffice"
                                        color: Theme.warning
                                        font.family: Theme.uiFont
                                        font.pixelSize: 9
                                        font.weight: Font.DemiBold
                                        anchors.horizontalCenter: parent.horizontalCenter
                                    }
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: app.saveFormat = modelData.id
                                }
                            }
                        }
                    }

                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: "PDF é nativo (sem LibreOffice). ODT, DOCX e AIO JSON também. Só .doc pede o LibreOffice."
                        color: Theme.inkMuted
                        font.family: Theme.uiFont
                        font.pixelSize: 11
                    }

                    Text {
                        topPadding: 8
                        text: "NOME DO ARQUIVO"
                        color: Theme.inkMuted
                        font.family: Theme.uiFont
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                        font.letterSpacing: 1
                    }
                    Text {
                        text: (app.title.length ? app.title : "Sem título") + "." + (app.saveFormat === "json" ? "aio.json" : app.saveFormat)
                        color: Theme.ink
                        font.family: Theme.uiFont
                        font.pixelSize: 13
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.hairline }

            RowLayout {
                Layout.fillWidth: true
                Layout.margins: 20
                spacing: 12
                UiButton {
                    Layout.fillWidth: true
                    text: "Cancelar"
                    kind: "secondary"
                    h: 44
                    onClicked: sheet.close()
                }
                UiButton {
                    Layout.fillWidth: true
                    text: app.saveFormat === "pdf" ? "Exportar PDF" : "Salvar"
                    kind: "primary"
                    h: 44
                    onClicked: sheet.browse()
                }
            }
        }
    }
}
