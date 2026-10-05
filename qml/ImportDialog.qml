import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Hvelv

// Import fra KeePass, Bitwarden, 1Password, CSV eller et annet Hvelv.
AppDialog {
    id: dialog

    property url file
    property var preview: ({})
    readonly property bool needsPassword: !!preview.needsPassword

    title: "Importer"
    modal: true
    width: 520
    standardButtons: Dialog.Cancel

    function start() {
        file = ""
        preview = {}
        password.text = ""
        tag.text = "Importert " + Qt.formatDate(new Date(), "yyyy-MM-dd")
        customer.editText = ""
        open()
        fileDialog.open()
    }

    onRejected: Vault.cancelImport()

    Connections {
        target: Vault
        function onImportPreviewReady(p) { dialog.preview = p }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        Label {
            text: "Støttet: KeePass/KeePassXC (XML eller CSV), Bitwarden (JSON eller CSV), 1Password (CSV), "
                  + "Chrome/Edge (CSV), generell CSV med kolonnenavn, og andre Hvelv-filer."
            wrapMode: Text.WordWrap
            color: palette.placeholderText
            font.pixelSize: 12
            Layout.fillWidth: true
        }

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: dialog.file.toString() !== "" ? decodeURIComponent(dialog.file.toString().split("/").pop()) : "Ingen fil valgt"
                elide: Text.ElideMiddle
                Layout.fillWidth: true
            }
            Button { text: "Velg fil…"; onClicked: fileDialog.open() }
        }

        // Hvelv-fil: passord
        RowLayout {
            visible: dialog.needsPassword
            Layout.fillWidth: true
            TextField {
                id: password
                placeholderText: "Passordet til hvelvet som skal importeres"
                echoMode: TextInput.Password
                Layout.fillWidth: true
                onAccepted: openButton.clicked()
            }
            Button {
                id: openButton
                text: "Åpne"
                enabled: password.text.length > 0 && !Vault.busy
                onClicked: Vault.previewVaultImport(dialog.file, password.text)
            }
            BusyIndicator { running: Vault.busy; visible: running; implicitWidth: 28; implicitHeight: 28 }
        }

        Label {
            visible: !!dialog.preview.error
            text: dialog.preview.error || ""
            color: "#c42b1c"
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        // Forhåndsvisning og valg
        ColumnLayout {
            visible: !!dialog.preview.ok
            Layout.fillWidth: true
            spacing: 8

            Label {
                text: "Fant <b>" + (dialog.preview.count || 0) + "</b> oppføringer"
                      + (dialog.preview.contactCount ? " og " + dialog.preview.contactCount + " kontakter" : "")
                      + " (" + (dialog.preview.format || "") + ")."
                textFormat: Text.StyledText
            }
            Repeater {
                model: dialog.preview.warnings || []
                delegate: Label {
                    required property string modelData
                    text: "⚠ " + modelData
                    color: "#b45309"
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
            GridLayout {
                columns: 2
                columnSpacing: 12
                Layout.fillWidth: true
                Label { text: "Sett kunde på alle" }
                ComboBox {
                    id: customer
                    editable: true
                    currentIndex: -1
                    model: Vault.customers
                    Layout.fillWidth: true
                }
                Label { text: "Legg til tagg" }
                TextField { id: tag; Layout.fillWidth: true }
            }
            CheckBox {
                id: skipDuplicates
                text: "Hopp over duplikater (samme navn, brukernavn, URL og passord)"
                checked: true
            }
            Label {
                text: "Husk å slette eksportfilen etterpå – den inneholder passord i klartekst."
                visible: dialog.preview.format !== "Hvelv"
                color: palette.placeholderText
                font.pixelSize: 12
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Button {
                text: "Importer " + (dialog.preview.count || 0) + " oppføringer"
                highlighted: true
                enabled: (dialog.preview.count || 0) > 0
                Layout.alignment: Qt.AlignRight
                onClicked: {
                    const r = Vault.commitImport(skipDuplicates.checked, customer.editText.trim(), tag.text.trim())
                    if (r.ok)
                        dialog.accept()
                }
            }
        }
    }

    FileDialog {
        id: fileDialog
        title: "Velg fil å importere"
        nameFilters: ["Eksportfiler (*.csv *.xml *.json *.hvelv)", "Alle filer (*)"]
        onAccepted: {
            dialog.file = selectedFile
            dialog.preview = Vault.previewImport(selectedFile)
            if (dialog.needsPassword)
                password.forceActiveFocus()
        }
    }
}
