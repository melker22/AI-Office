import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: side
    color: Theme.surface
    property int docPane: 0 // 0 estrutura, 1 página, 2 estilos

    Rectangle {
        anchors.left: parent.left
        width: 1
        height: parent.height
        color: Theme.hairline
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            height: 48
            color: "transparent"
            Row {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 4
                Repeater {
                    model: ["Documento", "Integração"]
                    delegate: Rectangle {
                        required property int index
                        required property string modelData
                        width: (parent.width - 4) / 2
                        height: 32
                        radius: 6
                        color: app.inspectorTab === index ? Theme.paper : "transparent"
                        border.width: app.inspectorTab === index ? 1 : 0
                        border.color: Theme.hairline
                        Text {
                            anchors.centerIn: parent
                            text: modelData
                            color: app.inspectorTab === index ? Theme.ink : Theme.inkMuted
                            font.family: Theme.uiFont
                            font.pixelSize: 12
                            font.weight: app.inspectorTab === index ? Font.DemiBold : Font.Medium
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: app.inspectorTab = index
                        }
                    }
                }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.hairline }

        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentHeight: docCol.implicitHeight + 24
            visible: app.inspectorTab === 0

            Column {
                id: docCol
                width: side.width
                padding: 16
                spacing: 16

                Row {
                    spacing: 4
                    Repeater {
                        model: ["Estrutura", "Página", "Estilos"]
                        delegate: Rectangle {
                            required property int index
                            required property string modelData
                            width: 80
                            height: 28
                            radius: 6
                            color: side.docPane === index ? Theme.paper : Theme.surfaceAlt
                            Text {
                                anchors.centerIn: parent
                                text: modelData
                                color: side.docPane === index ? Theme.ink : Theme.inkMuted
                                font.family: Theme.uiFont
                                font.pixelSize: 11
                                font.weight: Font.DemiBold
                            }
                            MouseArea {
                                anchors.fill: parent
                                onClicked: side.docPane = index
                            }
                        }
                    }
                }

                Column {
                    visible: side.docPane === 0
                    width: parent.width - 32
                    spacing: 4
                    Text {
                        text: "ESTRUTURA"
                        color: Theme.inkMuted
                        font.family: Theme.uiFont
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                        font.letterSpacing: 1
                    }
                    Repeater {
                        id: outlineRep
                        model: app.outline()
                        delegate: Rectangle {
                            required property var modelData
                            required property int index
                            width: docCol.width - 32
                            height: 32
                            radius: 6
                            color: index === 0 ? Theme.accent50 : "transparent"
                            Rectangle {
                                visible: index === 0
                                width: 2
                                height: parent.height
                                color: Theme.accent
                            }
                            MouseArea {
                                anchors.fill: parent
                                onClicked: app.jumpToBlock(modelData.id)
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.left: parent.left
                                anchors.leftMargin: 8 + Math.max(0, (modelData.level - 1) * 12)
                                text: (modelData.text && modelData.text.length) ? modelData.text : modelData.id
                                color: Theme.ink
                                font.family: Theme.uiFont
                                font.pixelSize: modelData.level <= 1 ? 13 : 12
                                font.weight: modelData.level <= 1 ? Font.Medium : Font.Normal
                                elide: Text.ElideRight
                                width: parent.width - 16
                            }
                        }
                    }
                    Text {
                        visible: outlineRep.count === 0
                        text: "Sem títulos ainda"
                        color: Theme.inkMuted
                        font.family: Theme.uiFont
                        font.pixelSize: 12
                    }
                    Connections {
                        target: app
                        function onDocumentChanged() { outlineRep.model = app.outline() }
                    }
                }

                Column {
                    visible: side.docPane === 1
                    width: parent.width - 32
                    spacing: 12

                    Text {
                        text: "TAMANHO DO PAPEL"
                        color: Theme.inkMuted
                        font.family: Theme.uiFont
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                        font.letterSpacing: 1
                    }
                    Row {
                        spacing: 8
                        Repeater {
                            model: [{ id: "A4", label: "A4" }, { id: "Letter", label: "Carta" }]
                            delegate: Rectangle {
                                required property var modelData
                                width: 110
                                height: 44
                                radius: 8
                                color: app.pageSize === modelData.id ? Theme.accent50 : "transparent"
                                border.width: app.pageSize === modelData.id ? 2 : 1
                                border.color: app.pageSize === modelData.id ? Theme.accent : Theme.hairline
                                Text {
                                    anchors.centerIn: parent
                                    text: modelData.label
                                    color: Theme.ink
                                    font.family: Theme.uiFont
                                    font.pixelSize: 13
                                    font.weight: Font.DemiBold
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: app.pageSize = modelData.id
                                }
                            }
                        }
                    }

                    Text {
                        text: "ORIENTAÇÃO"
                        color: Theme.inkMuted
                        font.family: Theme.uiFont
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                        font.letterSpacing: 1
                    }
                    Row {
                        spacing: 8
                        Repeater {
                            model: [{ id: "portrait", label: "Retrato" }, { id: "landscape", label: "Paisagem" }]
                            delegate: Rectangle {
                                required property var modelData
                                width: 110
                                height: 44
                                radius: 8
                                color: app.orientation === modelData.id ? Theme.accent50 : "transparent"
                                border.width: app.orientation === modelData.id ? 2 : 1
                                border.color: app.orientation === modelData.id ? Theme.accent : Theme.hairline
                                Text {
                                    anchors.centerIn: parent
                                    text: modelData.label
                                    color: Theme.ink
                                    font.family: Theme.uiFont
                                    font.pixelSize: 13
                                    font.weight: Font.DemiBold
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: app.orientation = modelData.id
                                }
                            }
                        }
                    }

                    Text {
                        text: "MARGENS (MM)"
                        color: Theme.inkMuted
                        font.family: Theme.uiFont
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                        font.letterSpacing: 1
                    }
                    Grid {
                        columns: 2
                        columnSpacing: 12
                        rowSpacing: 10
                        Repeater {
                            model: [
                                { edge: 0, label: "Superior" },
                                { edge: 2, label: "Inferior" },
                                { edge: 3, label: "Esquerda" },
                                { edge: 1, label: "Direita" }
                            ]
                            delegate: Column {
                                required property var modelData
                                spacing: 4
                                Text {
                                    text: modelData.label
                                    color: Theme.inkMuted
                                    font.family: Theme.uiFont
                                    font.pixelSize: 11
                                }
                                UiSpin {
                                    width: 110
                                    from: 5
                                    to: 80
                                    value: Math.round(app.marginsMm[modelData.edge] || 20)
                                    suffix: "mm"
                                    onValueModified: app.setMarginMm(modelData.edge, value)
                                }
                            }
                        }
                    }

                    Text {
                        text: "COLUNAS"
                        color: Theme.inkMuted
                        font.family: Theme.uiFont
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                        font.letterSpacing: 1
                    }
                    UiSpin {
                        width: 110
                        from: 1
                        to: 3
                        value: app.pageColumns
                        onValueModified: app.pageColumns = value
                    }
                    Row {
                        spacing: 8
                        Rectangle {
                            width: 18
                            height: 18
                            radius: 4
                            color: app.hyphenate ? Theme.accent : Theme.paper
                            border.color: Theme.hairline
                            border.width: 1
                            MouseArea { anchors.fill: parent; onClicked: app.hyphenate = !app.hyphenate }
                        }
                        Text {
                            text: "Hifenizar"
                            color: Theme.ink
                            font.family: Theme.uiFont
                            font.pixelSize: 13
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    Text {
                        text: "CABEÇALHO E RODAPÉ"
                        color: Theme.inkMuted
                        font.family: Theme.uiFont
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                        font.letterSpacing: 1
                    }
                    Rectangle {
                        width: parent.width
                        height: 36
                        radius: 8
                        color: Theme.paper
                        border.color: Theme.hairlineStrong
                        border.width: 1
                        UiField {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            placeholder: "Cabeçalho"
                            text: app.headerText
                            onTextChanged: if (text !== app.headerText) app.headerText = text
                        }
                    }
                    Rectangle {
                        width: parent.width
                        height: 36
                        radius: 8
                        color: Theme.paper
                        border.color: Theme.hairlineStrong
                        border.width: 1
                        UiField {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            placeholder: "Rodapé"
                            text: app.footerText
                            onTextChanged: if (text !== app.footerText) app.footerText = text
                        }
                    }
                    Row {
                        spacing: 8
                        Rectangle {
                            width: 36
                            height: 20
                            radius: 10
                            color: app.pageNumbers ? Theme.accent : Theme.hairlineStrong
                            Rectangle {
                                width: 12
                                height: 12
                                radius: 6
                                color: Theme.paper100
                                anchors.verticalCenter: parent.verticalCenter
                                x: app.pageNumbers ? 20 : 4
                            }
                            MouseArea {
                                anchors.fill: parent
                                onClicked: app.pageNumbers = !app.pageNumbers
                            }
                        }
                        Text {
                            text: "Página n de N"
                            color: Theme.inkSoft
                            font.family: Theme.uiFont
                            font.pixelSize: 13
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                }

                Column {
                    visible: side.docPane === 2
                    width: parent.width - 32
                    spacing: 4
                    Text {
                        text: "ESTILOS"
                        color: Theme.inkMuted
                        font.family: Theme.uiFont
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                        font.letterSpacing: 1
                    }
                    Repeater {
                        model: app.styleNames
                        delegate: Rectangle {
                            required property var modelData
                            width: docCol.width - 32
                            height: 36
                            radius: 6
                            color: app.headingLabel === modelData.label ? Theme.accent50 : "transparent"
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 10
                                Text {
                                    text: modelData.label
                                    color: Theme.ink
                                    font.family: Theme.uiFont
                                    font.pixelSize: 13
                                    font.weight: app.headingLabel === modelData.label ? Font.DemiBold : Font.Normal
                                    Layout.fillWidth: true
                                }
                                Text {
                                    text: "Aa"
                                    color: app.headingLabel === modelData.label ? Theme.accent : Theme.inkMuted
                                    font.family: Theme.serifFont
                                    font.pixelSize: 16
                                    font.bold: modelData.bold
                                    font.italic: modelData.italic
                                }
                            }
                            MouseArea {
                                anchors.fill: parent
                                onClicked: app.applyNamedStyle(modelData.label)
                            }
                        }
                    }
                }

                Column {
                    visible: app.tableSelected
                    width: parent.width - 32
                    spacing: 10
                    Text {
                        text: "TABELA"
                        color: Theme.inkMuted
                        font.family: Theme.uiFont
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                        font.letterSpacing: 1
                    }
                    Rectangle {
                        width: parent.width
                        height: tblCol.implicitHeight + 24
                        radius: 12
                        color: Theme.paper
                        border.color: Theme.hairline
                        border.width: 1
                        Column {
                            id: tblCol
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 12
                            spacing: 10
                            RowLayout {
                                width: parent.width
                                Text { text: "Linhas"; color: Theme.inkSoft; font.family: Theme.uiFont; font.pixelSize: 13; Layout.fillWidth: true }
                                Text { text: app.tableRows; color: Theme.ink; font.family: Theme.uiFont; font.pixelSize: 13; font.weight: Font.DemiBold }
                            }
                            RowLayout {
                                width: parent.width
                                Text { text: "Colunas"; color: Theme.inkSoft; font.family: Theme.uiFont; font.pixelSize: 13; Layout.fillWidth: true }
                                Text { text: app.tableCols; color: Theme.ink; font.family: Theme.uiFont; font.pixelSize: 13; font.weight: Font.DemiBold }
                            }
                        }
                    }
                }

                Column {
                    visible: app.comments.length > 0 && side.docPane === 0
                    width: parent.width - 32
                    spacing: 8
                    Text {
                        text: "COMENTÁRIOS"
                        color: Theme.inkMuted
                        font.family: Theme.uiFont
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                        font.letterSpacing: 1
                    }
                    Repeater {
                        model: app.comments
                        delegate: Rectangle {
                            required property var modelData
                            width: parent.width
                            implicitHeight: ccol.implicitHeight + 20
                            radius: 12
                            color: Theme.paper
                            border.color: modelData.resolved ? Theme.hairline : Theme.accent
                            border.width: 1
                            Column {
                                id: ccol
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.margins: 10
                                spacing: 6
                                Text {
                                    text: (modelData.author && modelData.author.length) ? modelData.author : "IA"
                                    color: Theme.ink
                                    font.family: Theme.uiFont
                                    font.pixelSize: 12
                                    font.weight: Font.DemiBold
                                }
                                Text {
                                    width: parent.width
                                    wrapMode: Text.WordWrap
                                    text: modelData.text
                                    color: Theme.inkSoft
                                    font.family: Theme.uiFont
                                    font.pixelSize: 12
                                }
                                UiButton {
                                    visible: !modelData.resolved
                                    text: "Resolver"
                                    kind: "primary"
                                    h: 28
                                    onClicked: app.resolveComment(modelData.id)
                                }
                                Text {
                                    visible: modelData.resolved
                                    text: "Resolvido"
                                    color: Theme.inkMuted
                                    font.pixelSize: 11
                                }
                            }
                        }
                    }
                }

                Column {
                    visible: side.docPane === 0
                    width: parent.width - 32
                    spacing: 8
                    Text {
                        text: "LIGAÇÃO E CAMPOS"
                        color: Theme.inkMuted
                        font.family: Theme.uiFont
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                        font.letterSpacing: 1
                    }
                    Rectangle {
                        width: parent.width
                        height: 36
                        radius: 8
                        color: Theme.paper
                        border.color: Theme.hairlineStrong
                        border.width: 1
                        UiField {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            placeholder: "https://…"
                            text: app.linkUrl
                            onTextChanged: if (text !== app.linkUrl) app.linkUrl = text
                            onAccepted: app.applyLink()
                        }
                    }
                    UiButton { text: "Aplicar hiperlink"; kind: "secondary"; h: 32; onClicked: app.applyLink() }
                    Row {
                        spacing: 6
                        UiButton { text: "Pág."; kind: "ghost"; h: 28; onClicked: app.insertField("page_number") }
                        UiButton { text: "Data"; kind: "ghost"; h: 28; onClicked: app.insertField("date") }
                        UiButton { text: "Título"; kind: "ghost"; h: 28; onClicked: app.insertField("title") }
                    }
                    UiButton { text: "Atualizar sumário"; kind: "secondary"; h: 32; onClicked: app.updateToc() }
                    UiButton { text: "Ortografia"; kind: "secondary"; h: 32; onClicked: app.runSpellcheck() }
                    Row {
                        spacing: 8
                        Rectangle {
                            width: 18
                            height: 18
                            radius: 4
                            color: app.trackChanges ? Theme.accent : Theme.paper
                            border.color: Theme.hairline
                            border.width: 1
                            MouseArea { anchors.fill: parent; onClicked: app.trackChanges = !app.trackChanges }
                        }
                        Text {
                            text: "Registar alterações"
                            color: Theme.ink
                            font.family: Theme.uiFont
                            font.pixelSize: 13
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                    Repeater {
                        model: app.revisions
                        delegate: Rectangle {
                            required property var modelData
                            width: parent.width
                            implicitHeight: 56
                            radius: 8
                            color: Theme.paper
                            border.color: Theme.hairline
                            border.width: 1
                            Row {
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 8
                                Text {
                                    text: (modelData.kind || "") + " · " + (modelData.author || "")
                                    color: Theme.ink
                                    font.pixelSize: 12
                                    width: parent.width - 100
                                    elide: Text.ElideRight
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                UiButton { text: "Ok"; kind: "ghost"; h: 24; onClicked: app.acceptRevision(modelData.id) }
                                UiButton { text: "×"; kind: "ghost"; h: 24; onClicked: app.rejectRevision(modelData.id) }
                            }
                        }
                    }
                }
            }
        }

        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            visible: app.inspectorTab === 1
            contentHeight: integ.implicitHeight + 32
            AiIntegratePanel {
                id: integ
                width: side.width - 32
                anchors.horizontalCenter: parent.horizontalCenter
                y: 16
            }
        }
    }
}
