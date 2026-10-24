import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: bar
    height: 56
    color: Theme.paper

    signal requestOpen()
    signal requestSaveAs()
    signal requestImage()

    property string alignKind: "left"

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Theme.hairline
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 2

        IconBtn { glyph: "▢"; tip: "Novo"; onClicked: app.newDocument() }
        IconBtn { glyph: "▣"; tip: "Abrir"; onClicked: bar.requestOpen() }
        IconBtn { glyph: "▤"; tip: "Salvar"; onClicked: app.save() }

        Rectangle { width: 1; height: 24; color: Theme.hairline; Layout.leftMargin: 6; Layout.rightMargin: 6 }

        IconBtn { glyph: "↺"; tip: "Desfazer"; onClicked: app.undo() }
        IconBtn { glyph: "↻"; tip: "Refazer"; onClicked: app.redo() }

        Rectangle { width: 1; height: 24; color: Theme.hairline; Layout.leftMargin: 6; Layout.rightMargin: 6 }

        UiCombo {
            Layout.preferredWidth: 140
            Layout.preferredHeight: 32
            text: app.headingLabel
            model: app.styleLabels
            onActivated: (value) => app.applyNamedStyle(value)
        }

        Rectangle { width: 1; height: 24; color: Theme.hairline; Layout.leftMargin: 6; Layout.rightMargin: 6 }

        Rectangle {
            Layout.preferredHeight: 32
            Layout.preferredWidth: 92
            radius: 8
            color: "transparent"
            border.color: Theme.hairlineStrong
            border.width: 1
            Row {
                anchors.centerIn: parent
                spacing: 2
                IconBtn {
                    width: 28; height: 28; glyph: "−"; tip: "Diminuir corpo"
                    onClicked: app.fontSizePt = app.fontSizePt - 1
                }
                Text {
                    width: 28
                    height: 28
                    text: app.fontSizePt
                    color: Theme.ink
                    font.family: Theme.uiFont
                    font.pixelSize: 12
                    font.weight: Font.Medium
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                IconBtn {
                    width: 28; height: 28; glyph: "+"; tip: "Aumentar corpo"
                    onClicked: app.fontSizePt = app.fontSizePt + 1
                }
            }
        }

        Rectangle { width: 1; height: 24; color: Theme.hairline; Layout.leftMargin: 6; Layout.rightMargin: 6 }

        IconBtn { glyph: "B"; tip: "Negrito"; boldGlyph: true; onClicked: app.applyBold() }
        IconBtn { glyph: "I"; tip: "Itálico"; italicGlyph: true; onClicked: app.applyItalic() }
        IconBtn { glyph: "S"; tip: "Sublinhado"; underlineGlyph: true; onClicked: app.applyUnderline() }
        IconBtn { glyph: "T̶"; tip: "Tachado"; onClicked: app.applyStrike() }
        IconBtn { glyph: "x²"; tip: "Superescrito"; onClicked: app.applySuper() }
        IconBtn { glyph: "x₂"; tip: "Subscrito"; onClicked: app.applySub() }

        Rectangle { width: 1; height: 24; color: Theme.hairline; Layout.leftMargin: 6; Layout.rightMargin: 6 }

        IconBtn { glyph: "•≡"; tip: "Lista com marcadores"; onClicked: app.insertList("bullet") }
        IconBtn { glyph: "1."; tip: "Lista numerada"; onClicked: app.insertList("number") }

        Rectangle { width: 1; height: 24; color: Theme.hairline; Layout.leftMargin: 6; Layout.rightMargin: 6 }

        IconBtn { glyph: "⇤"; tip: "Alinhar à esquerda"; active: bar.alignKind === "left"; onClicked: { bar.alignKind = "left"; app.setAlign("left") } }
        IconBtn { glyph: "⇔"; tip: "Centralizar"; active: bar.alignKind === "center"; onClicked: { bar.alignKind = "center"; app.setAlign("center") } }
        IconBtn { glyph: "⇥"; tip: "Alinhar à direita"; active: bar.alignKind === "right"; onClicked: { bar.alignKind = "right"; app.setAlign("right") } }
        IconBtn { glyph: "☰"; tip: "Justificar"; active: bar.alignKind === "justify"; onClicked: { bar.alignKind = "justify"; app.setAlign("justify") } }

        Rectangle { width: 1; height: 24; color: Theme.hairline; Layout.leftMargin: 6; Layout.rightMargin: 6 }

        IconBtn { glyph: "⊞"; tip: "Inserir tabela"; active: app.tableSelected; onClicked: app.insertTable() }
        IconBtn { glyph: "▭"; tip: "Inserir imagem"; onClicked: bar.requestImage() }
        IconBtn { glyph: "⏎"; tip: "Inserir quebra"; onClicked: app.insertBreak() }
        IconBtn { glyph: "¹"; tip: "Nota de rodapé"; onClicked: app.insertFootnote() }
        IconBtn { glyph: "Σ"; tip: "Sumário"; onClicked: app.updateToc() }

        Item { Layout.fillWidth: true }

        IconBtn {
            glyph: "⌕"
            tip: "Localizar (Ctrl+F)"
            active: app.findVisible
            onClicked: app.findVisible = !app.findVisible
        }
    }
}
