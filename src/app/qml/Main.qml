import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Window
import AIOffice

ApplicationWindow {
    id: root
    width: 1440
    height: 900
    minimumWidth: 1024
    minimumHeight: 700
    visible: true
    flags: Qt.Window | Qt.FramelessWindowHint
    title: (app.dirty ? "• " : "") + (app.title.length ? app.title : "Sem título") + " — AI Office"
    color: Theme.paper
    font.family: Theme.uiFont

    palette.window: Theme.paper
    palette.windowText: Theme.ink
    palette.base: Theme.paper100
    palette.text: Theme.ink
    palette.button: Theme.surface
    palette.buttonText: Theme.ink
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.accentInk
    palette.placeholderText: Theme.inkMuted
    palette.toolTipBase: Theme.toastBg
    palette.toolTipText: Theme.paper100
    palette.mid: Theme.hairline
    palette.dark: Theme.titleBar
    palette.light: Theme.paper
    palette.shadow: Theme.panelShadow

    Binding {
        target: Theme
        property: "dark"
        value: app.darkMode
    }

    FileDialog {
        id: openDlg
        title: "Abrir documento"
        fileMode: FileDialog.OpenFile
        nameFilters: [
            "Documentos (*.odt *.docx *.doc *.rtf *.aio.json *.json)",
            "Todos (*)"
        ]
        onAccepted: app.openPath(selectedFile)
    }

    FileDialog {
        id: saveDlg
        title: app.saveFormat === "pdf" ? "Exportar PDF" : "Salvar documento"
        fileMode: FileDialog.SaveFile
        nameFilters: saveFilters()
        defaultSuffix: app.saveFormat === "json" ? "aio.json" : app.saveFormat
        onAccepted: {
            if (app.saveFormat === "pdf")
                app.exportPdf(selectedFile)
            else
                app.savePath(selectedFile)
            saveSheet.close()
        }
    }

    FileDialog {
        id: imageDlg
        title: "Inserir imagem"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Imagens (*.png *.jpg *.jpeg *.webp *.svg)", "Todos (*)"]
        onAccepted: app.insertImage(selectedFile)
    }

    function saveFilters() {
        switch (app.saveFormat) {
        case "docx": return ["DOCX (*.docx)"]
        case "odt": return ["ODT (*.odt)"]
        case "doc": return ["DOC (*.doc)"]
        case "pdf": return ["PDF (*.pdf)"]
        case "json": return ["AIO JSON (*.aio.json *.json)"]
        default: return ["Todos (*)"]
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        TitleBar {
            Layout.fillWidth: true
            window: root
        }

        DocTabs {
            Layout.fillWidth: true
            onRequestNew: app.newTab()
        }

        CommandBar {
            Layout.fillWidth: true
            onRequestOpen: openPanel.open()
            onRequestSaveAs: saveSheet.open()
            onRequestImage: imageDlg.open()
        }

        FindBar { Layout.fillWidth: true }

        Ruler { Layout.fillWidth: true }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            OutlineRail { Layout.fillHeight: true }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                DocumentCanvas {
                    anchors.fill: parent
                    controller: app
                    dark: app.darkMode
                }

                EmptyWelcome {
                    anchors.fill: parent
                    onRequestOpen: openPanel.open()
                }

                TableMiniBar {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    anchors.topMargin: 24
                }
            }

            Sidebar {
                Layout.preferredWidth: 300
                Layout.minimumWidth: 260
                Layout.maximumWidth: 380
                Layout.fillHeight: true
            }
        }

        StatusBarBar { Layout.fillWidth: true }
    }

    OpenPanel {
        id: openPanel
        anchors.fill: parent
        onBrowse: openDlg.open()
    }

    SaveSheet {
        id: saveSheet
        anchors.fill: parent
        onBrowse: saveDlg.open()
    }

    UnsavedSheet { anchors.fill: parent }

    LibreOfficeSheet { anchors.fill: parent }

    Shortcut { sequences: [StandardKey.Find]; onActivated: app.findVisible = true }
    Shortcut { sequences: [StandardKey.New]; onActivated: app.newDocument() }
    Shortcut { sequences: [StandardKey.Open]; onActivated: openPanel.open() }
    Shortcut { sequences: [StandardKey.Save]; onActivated: app.save() }
    Shortcut { sequences: [StandardKey.SaveAs]; onActivated: saveSheet.open() }
    Shortcut { sequences: [StandardKey.Undo]; onActivated: app.undo() }
    Shortcut { sequences: [StandardKey.Redo]; onActivated: app.redo() }
    Shortcut { sequence: "Ctrl+B"; onActivated: app.applyBold() }
    Shortcut { sequence: "Ctrl+I"; onActivated: app.applyItalic() }
    Shortcut { sequence: "Ctrl+U"; onActivated: app.applyUnderline() }
    Shortcut {
        sequence: "Escape"
        onActivated: {
            if (openPanel.visible) openPanel.close()
            else if (saveSheet.visible) saveSheet.close()
            else app.findVisible = false
        }
    }

    Connections {
        target: app
        function onErrorMessage(msg) { toast.show(msg, true) }
        function onStatusMessage(msg) { toast.show(msg, false) }
        function onNeedSaveAs() { saveSheet.open() }
    }

    Rectangle {
        id: toast
        property alias text: toastLabel.text
        property bool isError: false
        function show(msg, err) {
            text = msg
            isError = err
            opacity = 1
            toastVisible.restart()
        }
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 48
        width: toastRow.implicitWidth + 28
        height: 40
        radius: 8
        color: Theme.toastBg
        visible: opacity > 0.05
        opacity: 0
        z: 50
        Row {
            id: toastRow
            anchors.centerIn: parent
            spacing: 10
            Rectangle {
                width: 8
                height: 8
                radius: 4
                color: toast.isError ? Theme.destructive : Theme.success
                anchors.verticalCenter: parent.verticalCenter
            }
            Label {
                id: toastLabel
                color: "#FDFBF7"
                font.family: Theme.uiFont
                font.pixelSize: 13
            }
        }
        Timer {
            id: toastVisible
            interval: 2600
            onTriggered: toast.opacity = 0
        }
        Behavior on opacity { NumberAnimation { duration: 160 } }
    }
}
