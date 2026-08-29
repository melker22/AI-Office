import QtQuick
import QtQuick.Layouts

Rectangle {
    id: mini
    visible: app.tableSelected && !app.welcomeVisible
    width: row.implicitWidth + 16
    height: 44
    radius: 12
    color: "#2A2420"

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 2
        IconBtn { glyph: "＋"; tip: "Inserir linha"; onDark: true; onClicked: app.insertRow() }
        IconBtn { glyph: "⊞"; tip: "Inserir coluna"; onDark: true; onClicked: app.insertCol() }
        Rectangle { width: 1; height: 20; color: "#4B4239"; anchors.verticalCenter: parent.verticalCenter }
        IconBtn { glyph: "H"; tip: "Linha de cabeçalho"; onDark: true; active: app.tableHeader; boldGlyph: true; onClicked: app.toggleTableHeader() }
        Rectangle { width: 1; height: 20; color: "#4B4239"; anchors.verticalCenter: parent.verticalCenter }
        IconBtn { glyph: "⇤"; tip: "Esquerda"; onDark: true; onClicked: app.setAlign("left") }
        IconBtn { glyph: "⇔"; tip: "Centro"; onDark: true; onClicked: app.setAlign("center") }
        IconBtn { glyph: "⇥"; tip: "Direita"; onDark: true; onClicked: app.setAlign("right") }
        Rectangle { width: 1; height: 20; color: "#4B4239"; anchors.verticalCenter: parent.verticalCenter }
        IconBtn { glyph: "⌫"; tip: "Excluir linha"; onDark: true; onClicked: app.deleteRow() }
        IconBtn { glyph: "⊟"; tip: "Excluir tabela"; onDark: true; danger: true; onClicked: app.deleteTable() }
    }
}
