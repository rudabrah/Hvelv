import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Hvelv

FocusScope {
    id: page
    focus: true

    readonly property bool creating: !Vault.vaultExists
    readonly property int minLength: 10
    readonly property bool canSubmit: !Vault.busy && (creating
        ? password.text.length >= minLength && password.text === confirm.text
        : password.text.length > 0)

    function submit() {
        if (!canSubmit)
            return
        if (creating)
            Vault.createVault(password.text)
        else
            Vault.unlock(password.text)
    }

    Connections {
        target: Vault
        function onUnlockedChanged() { password.text = ""; confirm.text = "" }
        function onErrorMessageChanged() {
            if (Vault.errorMessage !== "") {
                password.selectAll()
                password.forceActiveFocus()
            }
        }
        function onVaultPathChanged() { password.text = ""; confirm.text = ""; password.forceActiveFocus() }
    }


    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(440, page.width - 48)
        spacing: 14

        Label {
            text: "Hvelv"
            font.pixelSize: 34
            font.weight: Font.DemiBold
            Layout.alignment: Qt.AlignHCenter
        }

        Label {
            text: Vault.vaultIsSql
                  ? (page.creating ? "Opprett en ny, kryptert database i SQL Server" : "Lås opp databasen")
                  : (page.creating ? "Opprett et nytt, kryptert hvelv" : "Lås opp hvelvet")
            font.pixelSize: 16
            Layout.alignment: Qt.AlignHCenter
        }

        Label {
            text: Vault.vaultDisplayName
            elide: Text.ElideMiddle
            color: palette.placeholderText
            font.pixelSize: 12
            horizontalAlignment: Text.AlignHCenter
            Layout.fillWidth: true
            ToolTip.text: Vault.vaultDisplayName
            ToolTip.visible: pathHover.hovered
            HoverHandler { id: pathHover }
        }

        TextField {
            id: password
            focus: true
            placeholderText: "Hovedpassord"
            echoMode: TextInput.Password
            enabled: !Vault.busy
            Layout.fillWidth: true
            Layout.topMargin: 8
            onAccepted: page.creating ? confirm.forceActiveFocus() : page.submit()
            onTextEdited: Vault.clearError()
        }

        TextField {
            id: confirm
            visible: page.creating
            placeholderText: "Gjenta hovedpassord"
            echoMode: TextInput.Password
            enabled: !Vault.busy
            Layout.fillWidth: true
            onAccepted: page.submit()
        }

        Label {
            visible: page.creating
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font.pixelSize: 12
            color: password.text.length > 0 && password.text.length < page.minLength ? "#c42b1c" : palette.placeholderText
            text: confirm.text.length > 0 && confirm.text !== password.text
                  ? "Passordene er ikke like."
                  : "Minst " + page.minLength + " tegn. En setning med flere ord er både sterk og lett å huske. "
                    + "Hovedpassordet kan ikke gjenopprettes hvis du glemmer det."
        }

        Label {
            visible: Vault.errorMessage !== ""
            text: Vault.errorMessage
            color: "#c42b1c"
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        Button {
            text: Vault.busy ? (page.creating ? "Oppretter…" : "Låser opp…")
                             : (page.creating ? (Vault.vaultIsSql ? "Opprett database" : "Opprett hvelv") : "Lås opp")
            highlighted: true
            enabled: page.canSubmit
            Layout.fillWidth: true
            onClicked: page.submit()
        }

        BusyIndicator {
            running: Vault.busy
            visible: running
            Layout.alignment: Qt.AlignHCenter
        }

        Button {
            visible: Vault.setupActive
            text: "← Velg et annet sted"
            flat: true
            enabled: !Vault.busy
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 8
            onClicked: Vault.restartSetup()
        }

        RowLayout {
            visible: !Vault.setupActive
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 8
            spacing: 4

            Button {
                text: "Åpne annen fil…"
                flat: true
                enabled: !Vault.busy
                onClicked: openDialog.open()
            }
            Button {
                text: "Nytt hvelv…"
                flat: true
                enabled: !Vault.busy
                onClicked: saveDialog.open()
            }
            Button {
                text: "SQL Server…"
                flat: true
                enabled: !Vault.busy
                onClicked: sqlDialog.open()
            }
        }
    }

    SqlDatabaseDialog {
        id: sqlDialog
        objectName: "sqlDatabaseDialog"
        anchors.centerIn: parent
    }

    FileDialog {
        id: openDialog
        title: "Åpne hvelv"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Hvelv-filer (*.hvelv)", "Alle filer (*)"]
        onAccepted: Vault.selectVaultFile(selectedFile)
    }

    FileDialog {
        id: saveDialog
        title: "Velg plassering for nytt hvelv"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "hvelv"
        nameFilters: ["Hvelv-filer (*.hvelv)"]
        onAccepted: Vault.selectVaultFile(selectedFile)
    }
}
