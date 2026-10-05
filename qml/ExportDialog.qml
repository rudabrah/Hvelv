import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Hvelv

// Kryptert kopi (.hvelv), kopi i SQL Server eller ukryptert CSV.
AppDialog {
    id: dialog

    title: "Eksporter"
    modal: true
    width: 520
    standardButtons: Dialog.Close

    property string message: ""
    property bool messageOk: true

    readonly property bool passwordOk: pw.text.length >= 10 && pw.text === pw2.text

    onOpened: {
        message = ""; pw.text = ""; pw2.text = ""; csvConfirm.checked = false
        const loc = Vault.sqlLocation()
        sqlServer.text = loc.server
        sqlDatabase.text = Vault.vaultIsSql ? "" : loc.database
    }

    Connections {
        target: Vault
        function onExportFinished(ok, msg) { dialog.message = msg; dialog.messageOk = ok }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        SectionHeader { text: "Kryptert kopi (anbefalt)" }
        Label {
            text: "Lager en ny .hvelv-fil med alt innhold, også vedlegg, beskyttet med et eget passord. "
                  + "Brukes til sikkerhetskopi eller til å gi en kollega en kopi."
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        TextField { id: pw; placeholderText: "Passord for kopien (minst 10 tegn)"; echoMode: TextInput.Password; Layout.fillWidth: true }
        TextField { id: pw2; placeholderText: "Gjenta passord"; echoMode: TextInput.Password; Layout.fillWidth: true }
        RowLayout {
            Layout.fillWidth: true
            BusyIndicator { running: Vault.busy; visible: running; implicitWidth: 28; implicitHeight: 28 }
            Item { Layout.fillWidth: true }
            Button {
                text: "Lagre kryptert kopi…"
                highlighted: true
                enabled: !Vault.busy && dialog.passwordOk
                onClicked: vaultDialog.open()
            }
        }

        SectionHeader { text: "Kopi i SQL Server" }
        Label {
            text: "Lager en ny database i SQL Server med alt innhold, låst med passordet over. "
                  + "Slik flytter du hvelvet til SQL Server."
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        RowLayout {
            Layout.fillWidth: true
            TextField { id: sqlServer; placeholderText: "Server"; Layout.fillWidth: true }
            TextField { id: sqlDatabase; placeholderText: "Database"; Layout.fillWidth: true }
            Button {
                text: "Kopier"
                enabled: !Vault.busy && dialog.passwordOk && sqlServer.text.trim().length > 0
                         && Vault.isValidDatabaseName(sqlDatabase.text.trim())
                onClicked: Vault.exportToSql(sqlServer.text, sqlDatabase.text, pw.text)
            }
        }

        SectionHeader { text: "CSV (ukryptert)" }
        Label {
            text: "Til å flytte data til et annet verktøy. Filen inneholder alle passord i klartekst – "
                  + "slett den så snart den er importert. Vedlegg og passordhistorikk blir ikke med."
            wrapMode: Text.WordWrap
            color: "#b45309"
            Layout.fillWidth: true
        }
        CheckBox { id: csvConfirm; text: "Jeg forstår at filen ikke er kryptert" }
        Button {
            text: "Eksporter CSV…"
            enabled: csvConfirm.checked
            Layout.alignment: Qt.AlignRight
            onClicked: csvDialog.open()
        }

        Label {
            visible: dialog.message !== ""
            text: dialog.message
            color: dialog.messageOk ? "#107c10" : "#c42b1c"
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }

    FileDialog {
        id: vaultDialog
        title: "Lagre kryptert kopi"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "hvelv"
        nameFilters: ["Hvelv-filer (*.hvelv)"]
        onAccepted: Vault.exportVault(selectedFile, pw.text)
    }

    FileDialog {
        id: csvDialog
        title: "Eksporter CSV"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "csv"
        nameFilters: ["CSV (*.csv)"]
        onAccepted: {
            if (Vault.exportCsv(selectedFile)) {
                dialog.message = "CSV lagret. Husk å slette den etter bruk."
                dialog.messageOk = true
            }
        }
    }
}
