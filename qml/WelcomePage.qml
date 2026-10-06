import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Hvelv

// Første oppstart: velg hvor det personlige hvelvet skal ligge. Selve opprettelsen
// (hovedpassord) skjer på UnlockPage etterpå.
FocusScope {
    id: page
    focus: true

    readonly property bool hasSql: Vault.localSqlServers.length > 0
    property string choice: hasSql ? "sql" : "file"

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(520, page.width - 48)
        spacing: 14

        Label {
            text: "Velkommen til Hvelv"
            font.pixelSize: 30
            font.weight: Font.DemiBold
            Layout.alignment: Qt.AlignHCenter
        }

        Label {
            text: "Først lager vi ditt personlige hvelv. Det er bare du som har tilgang til det, "
                  + "og alt krypteres med et hovedpassord du velger i neste steg."
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
            Layout.fillWidth: true
        }

        ButtonGroup { id: group }

        component Choice: Frame {
            id: card
            required property string key
            required property string heading
            required property string details
            Layout.fillWidth: true
            Layout.topMargin: 4

            // Hele kortet er klikkbart; ButtonGroup holder bare én avkrysset.
            TapHandler { onTapped: radio.click() }

            RowLayout {
                anchors.fill: parent
                spacing: 10
                RadioButton {
                    id: radio
                    ButtonGroup.group: group
                    checked: page.choice === card.key
                    onToggled: if (checked) page.choice = card.key
                    Layout.alignment: Qt.AlignTop
                }
                ColumnLayout {
                    spacing: 2
                    Layout.fillWidth: true
                    Label { text: card.heading; font.weight: Font.DemiBold; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    Label {
                        text: card.details
                        wrapMode: Text.WordWrap
                        font.pixelSize: 12
                        color: palette.placeholderText
                        Layout.fillWidth: true
                    }
                }
            }
        }

        Choice {
            objectName: "choiceSql"
            key: "sql"
            heading: page.hasSql ? "Personlig database i SQL Server på denne PC-en (anbefalt)"
                                 : "Personlig database i SQL Server"
            details: "Databasen «HvelvPersonlig» opprettes med Windows-påloggingen din. Samme format som "
                     + "felles database, og kan åpnes i SSMS."
        }

        ComboBox {
            id: server
            objectName: "setupServerField"
            visible: page.choice === "sql"
            editable: true
            model: Vault.localSqlServers
            Layout.fillWidth: true
            Layout.leftMargin: 36
            Component.onCompleted: if (!page.hasSql) editText = "localhost"
        }

        Choice {
            objectName: "choiceFile"
            key: "file"
            heading: "Kryptert fil"
            details: "Én fil i Dokumenter\\Hvelv. Enkel å ta sikkerhetskopi av og flytte."
        }

        Label {
            visible: Vault.errorMessage !== ""
            text: Vault.errorMessage
            color: "#c42b1c"
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        Button {
            objectName: "welcomeContinue"
            text: "Fortsett"
            highlighted: true
            Layout.fillWidth: true
            Layout.topMargin: 8
            onClicked: Vault.startSetup(page.choice, server.editText)
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 4
            Button {
                text: "Åpne eksisterende database…"
                flat: true
                onClicked: sqlDialog.open()
            }
            Button {
                text: "Åpne hvelvfil…"
                flat: true
                onClicked: openDialog.open()
            }
        }
    }

    SqlDatabaseDialog {
        id: sqlDialog
        anchors.centerIn: parent
    }

    FileDialog {
        id: openDialog
        title: "Åpne hvelv"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Hvelv-filer (*.hvelv)", "Alle filer (*)"]
        onAccepted: Vault.selectVaultFile(selectedFile)
    }
}
