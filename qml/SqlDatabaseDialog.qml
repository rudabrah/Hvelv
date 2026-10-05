import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hvelv

// Velg en SQL Server-database for hvelvet: åpne en eksisterende eller opprett en ny.
AppDialog {
    id: dialog

    title: "SQL Server-database"
    modal: true
    width: 480
    standardButtons: Dialog.Ok | Dialog.Cancel

    readonly property bool valid: server.text.trim().length > 0 && Vault.isValidDatabaseName(database.text.trim())

    function updateOk() {
        const b = standardButton(Dialog.Ok)
        if (b)
            b.enabled = valid
    }

    onValidChanged: updateOk()
    onOpened: {
        const loc = Vault.sqlLocation()
        server.text = loc.server
        database.text = loc.database
        openExisting.checked = true
        updateOk()
        server.forceActiveFocus()
    }
    onAccepted: Vault.selectSqlDatabase(server.text, database.text, createNew.checked)

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        Label {
            text: "Hvelvet lagres i en database på SQL Server. Du kobler til med Windows-brukeren din, "
                  + "og alle hemmeligheter krypteres med hovedpassordet før de sendes til databasen."
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        Label { text: "Server"; Layout.topMargin: 6 }
        TextField {
            id: server
            objectName: "sqlServerField"
            placeholderText: "localhost eller (localdb)\\MSSQLLocalDB"
            Layout.fillWidth: true
            onAccepted: database.forceActiveFocus()
        }

        Label { text: "Database" }
        TextField {
            id: database
            objectName: "sqlDatabaseField"
            placeholderText: "HvelvPersonlig"
            Layout.fillWidth: true
            onAccepted: if (dialog.valid) dialog.accept()
        }
        Label {
            visible: database.text.length > 0 && !Vault.isValidDatabaseName(database.text.trim())
            text: "Bruk bare bokstaver (a–z), tall, _ og -."
            color: "#c42b1c"
            font.pixelSize: 12
        }

        RadioButton { id: openExisting; text: "Åpne eksisterende database"; checked: true; Layout.topMargin: 6 }
        RadioButton { id: createNew; text: "Opprett ny database" }
        Label {
            visible: createNew.checked
            text: "Databasen opprettes hvis den ikke finnes. Du trenger rettighet til å opprette databaser, "
                  + "eller en tom database som DBA-en har laget."
            wrapMode: Text.WordWrap
            font.pixelSize: 12
            color: palette.placeholderText
            Layout.fillWidth: true
        }
    }
}
