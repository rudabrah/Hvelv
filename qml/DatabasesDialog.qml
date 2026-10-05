pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Hvelv

// Databasene som er åpne, og tillegg av nye. Passordet til en tilleggsdatabase lagres kryptert
// i den personlige databasen, så den låses opp automatisk sammen med den.
AppDialog {
    id: dialog

    title: "Databaser"
    modal: true
    width: 620
    standardButtons: Dialog.Close

    property string message: ""
    property bool messageOk: true
    property string unlockKey: ""   // databasen det skrives passord for

    readonly property bool useSql: sqlKind.checked
    readonly property bool creating: createNew.checked
    readonly property bool canAdd: !Vault.busy && pw.text.length > 0
        && (!creating || (pw.text.length >= 10 && pw.text === pw2.text))
        && (useSql ? server.text.trim() !== "" && Vault.isValidDatabaseName(database.text.trim())
                   : filePath.text !== "")

    onOpened: {
        message = ""; unlockKey = ""
        name.text = ""; pw.text = ""; pw2.text = ""; filePath.text = ""
        server.text = "localhost"; database.text = ""
        openExisting.checked = true; fileKind.checked = true
    }

    Connections {
        target: Vault
        function onConnectionFinished(ok, msg) {
            dialog.message = msg
            dialog.messageOk = ok
            if (ok) {
                name.text = ""; pw.text = ""; pw2.text = ""; filePath.text = ""; database.text = ""
                dialog.unlockKey = ""
            }
        }
    }

    function add() {
        message = ""
        if (useSql)
            Vault.addSqlConnection(name.text, server.text, database.text, pw.text, creating)
        else
            Vault.addFileConnection(name.text, fileUrl, pw.text, creating)
    }

    property url fileUrl: ""

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        SectionHeader { text: "Åpne databaser" }
        Repeater {
            model: Vault.sources
            delegate: RowLayout {
                id: srcRow
                required property var modelData
                Layout.fillWidth: true
                spacing: 10

                ColumnLayout {
                    spacing: 0
                    Layout.fillWidth: true
                    Label {
                        text: srcRow.modelData.name + (srcRow.modelData.isMain ? "  (hoveddatabase)" : "")
                        font.weight: Font.DemiBold
                    }
                    Label {
                        text: srcRow.modelData.displayLocation
                        color: palette.placeholderText
                        font.pixelSize: 12
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }
                    Label {
                        text: srcRow.modelData.open ? srcRow.modelData.count + (srcRow.modelData.count === 1 ? " oppføring" : " oppføringer")
                              : srcRow.modelData.opening ? "Åpner …"
                              : srcRow.modelData.error
                        color: srcRow.modelData.open || srcRow.modelData.opening ? palette.placeholderText : "#c42b1c"
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    RowLayout {
                        visible: dialog.unlockKey === srcRow.modelData.key
                        Layout.fillWidth: true
                        TextField {
                            id: unlockPw
                            placeholderText: "Passord til " + srcRow.modelData.name
                            echoMode: TextInput.Password
                            Layout.fillWidth: true
                            onAccepted: Vault.unlockConnection(srcRow.modelData.key, text)
                        }
                        Button {
                            text: "Lås opp"
                            highlighted: true
                            enabled: unlockPw.text.length > 0
                            onClicked: Vault.unlockConnection(srcRow.modelData.key, unlockPw.text)
                        }
                    }
                }
                Button {
                    text: "Passord…"
                    visible: !srcRow.modelData.isMain && !srcRow.modelData.open && !srcRow.modelData.opening
                    onClicked: dialog.unlockKey = srcRow.modelData.key
                }
                Button {
                    text: "Fjern"
                    flat: true
                    visible: !srcRow.modelData.isMain
                    onClicked: Vault.removeConnection(srcRow.modelData.key)
                }
            }
        }

        SectionHeader { text: "Legg til database" }
        Label {
            text: "Passordet lagres kryptert i den personlige databasen, så databasen åpnes automatisk "
                  + "neste gang du låser opp."
            wrapMode: Text.WordWrap
            color: palette.placeholderText
            font.pixelSize: 12
            Layout.fillWidth: true
        }
        GridLayout {
            columns: 2
            columnSpacing: 12
            rowSpacing: 6
            Layout.fillWidth: true

            Label { text: "Navn" }
            TextField { id: name; placeholderText: "f.eks. Felles"; Layout.fillWidth: true }

            Label { text: "Type" }
            RowLayout {
                RadioButton { id: fileKind; text: "Fil"; checked: true }
                RadioButton { id: sqlKind; text: "SQL Server" }
                Item { Layout.fillWidth: true }
                RadioButton { id: openExisting; text: "Eksisterende"; checked: true }
                RadioButton { id: createNew; text: "Ny" }
            }

            Label { text: "Fil"; visible: !dialog.useSql }
            RowLayout {
                visible: !dialog.useSql
                Layout.fillWidth: true
                TextField { id: filePath; readOnly: true; placeholderText: "Ingen fil valgt"; Layout.fillWidth: true }
                Button { text: "Velg…"; onClicked: dialog.creating ? saveFile.open() : openFile.open() }
            }

            Label { text: "Server"; visible: dialog.useSql }
            TextField { id: server; visible: dialog.useSql; Layout.fillWidth: true }
            Label { text: "Database"; visible: dialog.useSql }
            TextField { id: database; visible: dialog.useSql; placeholderText: "f.eks. HvelvFelles"; Layout.fillWidth: true }

            Label { text: "Passord" }
            TextField {
                id: pw
                echoMode: TextInput.Password
                placeholderText: dialog.creating ? "Nytt passord (minst 10 tegn)" : "Passordet til databasen"
                Layout.fillWidth: true
            }
            Label { text: "Gjenta"; visible: dialog.creating }
            TextField { id: pw2; visible: dialog.creating; echoMode: TextInput.Password; Layout.fillWidth: true }
        }
        RowLayout {
            Layout.fillWidth: true
            Label {
                visible: dialog.message !== ""
                text: dialog.message
                color: dialog.messageOk ? "#107c10" : "#c42b1c"
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Item { Layout.fillWidth: dialog.message === "" }
            Button {
                text: dialog.creating ? "Opprett og legg til" : "Legg til"
                highlighted: true
                enabled: dialog.canAdd
                onClicked: dialog.add()
            }
        }
    }

    FileDialog {
        id: openFile
        title: "Velg hvelvfil"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Hvelv-filer (*.hvelv)", "Alle filer (*)"]
        onAccepted: { dialog.fileUrl = selectedFile; filePath.text = Vault.displayPath(selectedFile) }
    }
    FileDialog {
        id: saveFile
        title: "Ny hvelvfil"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "hvelv"
        nameFilters: ["Hvelv-filer (*.hvelv)"]
        onAccepted: { dialog.fileUrl = selectedFile; filePath.text = Vault.displayPath(selectedFile) }
    }
}
