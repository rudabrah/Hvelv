pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Hvelv

FocusScope {
    id: page
    objectName: "vaultPage"
    focus: true

    property string selectedId: ""
    property bool editing: false
    property var editSource: ({})
    property int revision: 0

    function startNew(templateIndex) {
        editSource = Vault.newEntry(templateIndex === undefined ? -1 : templateIndex, Vault.entries.sourceFilter)
        editing = true
    }

    function startEdit() {
        editSource = Vault.entry(selectedId)
        editing = true
    }

    // Brukes av testene: åpne/lukk dialoger uten mus. name: "health" | "none"
    function openDialog(name) {
        healthDialog.close()
        settings.close()
        databases.close()
        if (name === "health")
            healthDialog.open()
        else if (name === "settings")
            settings.open()
        else if (name === "databases")
            databases.open()
    }

    function focusList() {
        list.forceActiveFocus()
        if (list.currentIndex < 0 && list.count > 0)
            list.currentIndex = 0
    }

    function select(id) {
        selectedId = id
        const row = Vault.entries.rowOfId(id)
        if (row >= 0)
            list.currentIndex = row
    }

    Shortcut { sequence: StandardKey.Find; onActivated: search.forceActiveFocus() }
    Shortcut { sequence: StandardKey.New; onActivated: templates.open() }
    Shortcut { sequence: "Ctrl+L"; onActivated: Vault.lock() }

    Connections {
        target: Vault
        function onShowAttentionRequested() { Vault.entries.filter = "attention" }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // --- Verktøylinje ---
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 12
            spacing: 8

            TextField {
                id: search
                focus: true
                placeholderText: "Søk i system, kunde, brukernavn, kontaktperson, tagger …  (Ctrl+F)"
                Layout.fillWidth: true
                text: Vault.entries.searchText
                onTextChanged: Vault.entries.searchText = text
                Keys.onEscapePressed: text = ""
                Keys.onDownPressed: page.focusList()
                Keys.onReturnPressed: page.focusList()
            }
            Button {
                text: "Ny oppføring"
                highlighted: true
                onClicked: templates.open()
            }
            Button {
                text: "Lås"
                onClicked: Vault.lock()
            }
            ToolButton {
                text: "⋯"
                font.pixelSize: 18
                onClicked: menu.popup()
                Menu {
                    id: menu
                    MenuItem { text: "Kontaktpersoner…"; onTriggered: contacts.open() }
                    MenuItem { text: "Databaser…"; onTriggered: databases.open() }
                    MenuItem { text: "Passordhelse…"; onTriggered: healthDialog.open() }
                    MenuSeparator {}
                    MenuItem { text: "Importer…"; onTriggered: importer.start() }
                    MenuItem { text: "Eksporter…"; onTriggered: exporter.open() }
                    MenuItem { text: "Utløpsdatoer til kalender (.ics)…"; onTriggered: icsDialog.open() }
                    MenuSeparator {}
                    MenuItem { text: "Bytt hovedpassord…"; onTriggered: changePw.open() }
                    MenuItem { text: "Innstillinger…"; onTriggered: settings.open() }
                    MenuSeparator {}
                    MenuItem { text: "Lås (Ctrl+L)"; onTriggered: Vault.lock() }
                }
            }
        }

        // --- Filtre ---
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.bottomMargin: 8
            spacing: 6

            ButtonGroup { id: filterGroup }
            Repeater {
                model: [
                    { key: "all", label: "Alle", count: Vault.totalCount },
                    { key: "attention", label: "Krever oppmerksomhet", count: Vault.expiringCount + Vault.expiredCount },
                    { key: "soon", label: "Utløper snart", count: Vault.expiringCount },
                    { key: "expired", label: "Utløpt", count: Vault.expiredCount }
                ]
                delegate: Button {
                    required property var modelData
                    text: modelData.label + "  " + modelData.count
                    checkable: true
                    flat: !checked
                    checked: Vault.entries.filter === modelData.key
                    ButtonGroup.group: filterGroup
                    onClicked: Vault.entries.filter = modelData.key
                }
            }
            Item { Layout.fillWidth: true }
            ComboBox {
                id: sourceFilter
                objectName: "sourceFilter"
                visible: Vault.sources.length > 1
                Layout.preferredWidth: 170
                readonly property var keys: [""].concat(Vault.sources.map(s => s.key))
                model: ["Alle databaser"].concat(Vault.sources.map(s => s.name))
                currentIndex: Math.max(0, keys.indexOf(Vault.entries.sourceFilter))
                onActivated: (i) => Vault.entries.sourceFilter = keys[i]
            }
            ComboBox {
                id: customerFilter
                Layout.preferredWidth: 190
                model: ["Alle kunder"].concat(Vault.customers)
                currentIndex: Math.max(0, model.indexOf(Vault.entries.customerFilter))
                onActivated: (i) => Vault.entries.customerFilter = i === 0 ? "" : model[i]
            }
            ComboBox {
                id: ownerFilter
                Layout.preferredWidth: 170
                model: ["Alle ansvarlige"].concat(Vault.owners)
                currentIndex: Math.max(0, model.indexOf(Vault.entries.ownerFilter))
                onActivated: (i) => Vault.entries.ownerFilter = i === 0 ? "" : model[i]
            }
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: palette.mid; opacity: 0.4 }

        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true

            // --- Liste ---
            ListView {
                id: list
                SplitView.preferredWidth: 380
                SplitView.minimumWidth: 260
                clip: true
                model: Vault.entries
                currentIndex: -1
                keyNavigationEnabled: true
                ScrollBar.vertical: ScrollBar {}

                section.property: "customer"
                section.criteria: ViewSection.FullString
                section.delegate: Label {
                    required property string section
                    width: ListView.view.width
                    text: section !== "" ? section : "Uten kunde"
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    color: palette.highlight
                    leftPadding: 12
                    topPadding: 10
                    bottomPadding: 4
                }

                onCurrentIndexChanged: {
                    if (currentIndex >= 0 && !page.editing)
                        page.selectedId = Vault.entries.idAt(currentIndex)
                }

                delegate: ItemDelegate {
                    id: row
                    required property int index
                    required property string entryId
                    required property string title
                    required property string customer
                    required property string system
                    required property string environment
                    required property string username
                    required property string expiryStatus
                    required property var daysLeft
                    required property bool hasTotp
                    required property string sourceName

                    width: ListView.view.width
                    highlighted: ListView.isCurrentItem
                    onClicked: {
                        if (page.editing)
                            return
                        list.currentIndex = index
                        page.selectedId = entryId
                    }

                    contentItem: RowLayout {
                        spacing: 10
                        Rectangle {
                            implicitWidth: 34
                            implicitHeight: 34
                            radius: 8
                            color: Qt.alpha(palette.highlight, 0.14)
                            Label {
                                anchors.centerIn: parent
                                text: row.title.length > 0 ? row.title.charAt(0).toUpperCase() : "?"
                                font.weight: Font.DemiBold
                                color: palette.highlight
                            }
                        }
                        ColumnLayout {
                            spacing: 0
                            Layout.fillWidth: true
                            Label {
                                text: row.title
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Label {
                                text: [row.system, row.environment ? row.environment.toUpperCase() : "", row.username]
                                      .filter(s => s !== "").join("  ·  ")
                                visible: text !== ""
                                color: palette.placeholderText
                                font.pixelSize: 12
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }
                        Label {
                            visible: Vault.openSourceCount > 1
                            text: row.sourceName
                            font.pixelSize: 11
                            color: palette.placeholderText
                            leftPadding: 6
                            rightPadding: 6
                            topPadding: 1
                            bottomPadding: 1
                            background: Rectangle { radius: 4; color: "transparent"; border.color: palette.mid }
                        }
                        ExpiryBadge {
                            status: row.expiryStatus === "ok" ? "none" : row.expiryStatus
                            textOverride: row.expiryStatus === "expired" ? "Utløpt"
                                          : row.daysLeft === 0 ? "I dag"
                                          : row.daysLeft + " d"
                        }
                    }
                }

                Label {
                    anchors.centerIn: parent
                    width: parent.width - 40
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    visible: list.count === 0
                    color: palette.placeholderText
                    text: Vault.totalCount === 0 ? "Hvelvet er tomt.\nTrykk «Ny oppføring» for å legge til den første kontoen."
                                                 : "Ingen treff."
                }
            }

            // --- Detaljer / redigering ---
            Item {
                SplitView.fillWidth: true
                SplitView.minimumWidth: 420

                EntryEditor {
                    anchors.fill: parent
                    visible: page.editing
                    source: page.editSource
                    onSaved: (id) => {
                        page.editing = false
                        page.revision++
                        page.select(id)
                    }
                    onCancelled: page.editing = false
                }

                EntryDetails {
                    objectName: "entryDetails"
                    anchors.fill: parent
                    visible: !page.editing && page.selectedId !== "" && Vault.entry(page.selectedId).id !== undefined
                    entryId: page.selectedId
                    revision: page.revision
                    onEditRequested: page.startEdit()
                    onDeleteRequested: confirmDelete.open()
                    onEntryMoved: (id) => {
                        page.revision++
                        page.select(id)
                    }
                }

                Label {
                    anchors.centerIn: parent
                    visible: !page.editing && page.selectedId === ""
                    text: "Velg en oppføring i listen."
                    color: palette.placeholderText
                }
            }
        }
    }

    // --- Dialoger ---
    TemplateDialog {
        id: templates
        anchors.centerIn: parent
        onTemplateChosen: (i) => page.startNew(i)
    }

    FileDialog {
        id: icsDialog
        title: "Lagre utløpsdatoer som kalenderfil"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "ics"
        nameFilters: ["Kalender (*.ics)"]
        onAccepted: Vault.exportCalendar(selectedFile, false)
    }

    HealthDialog {
        id: healthDialog
        anchors.centerIn: parent
        onEntrySelected: (id) => {
            Vault.entries.filter = "all"
            Vault.entries.ownerFilter = ""
            Vault.entries.customerFilter = ""
            search.text = ""
            page.editing = false
            page.select(id)
        }
    }

    ImportDialog {
        id: importer
        anchors.centerIn: parent
    }

    DatabasesDialog {
        id: databases
        objectName: "databasesDialog"
        anchors.centerIn: parent
    }

    ExportDialog {
        id: exporter
        anchors.centerIn: parent
    }

    ContactsDialog {
        id: contacts
        anchors.centerIn: parent
        onSearchRequested: (text) => {
            Vault.entries.filter = "all"
            search.text = text
        }
    }

    AppDialog {
        id: confirmDelete
        anchors.centerIn: parent
        modal: true
        title: "Slette oppføring?"
        width: 420
        standardButtons: Dialog.Yes | Dialog.No
        Label {
            width: confirmDelete.availableWidth
            text: "«" + (Vault.entry(page.selectedId).title || "") + "» slettes permanent fra «"
                  + (Vault.entry(page.selectedId).sourceName || "hvelvet") + "»."
            wrapMode: Text.WordWrap
        }
        onAccepted: {
            if (Vault.deleteEntry(page.selectedId)) {
                page.selectedId = ""
                list.currentIndex = -1
            }
        }
    }

    AppDialog {
        id: changePw
        anchors.centerIn: parent
        modal: true
        width: 420
        title: "Bytt hovedpassord"
        standardButtons: Dialog.Cancel

        onOpened: { Vault.clearError(); current.text = ""; next.text = ""; next2.text = ""; current.forceActiveFocus() }

        Connections {
            target: Vault
            function onMasterPasswordChanged() { changePw.close() }
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 8
            TextField { id: current; placeholderText: "Nåværende hovedpassord"; echoMode: TextInput.Password; Layout.fillWidth: true }
            TextField { id: next; placeholderText: "Nytt hovedpassord (minst 10 tegn)"; echoMode: TextInput.Password; Layout.fillWidth: true }
            TextField { id: next2; placeholderText: "Gjenta nytt hovedpassord"; echoMode: TextInput.Password; Layout.fillWidth: true }
            Label {
                text: Vault.errorMessage !== "" ? Vault.errorMessage
                      : next2.text !== "" && next.text !== next2.text ? "Passordene er ikke like." : ""
                visible: text !== ""
                color: "#c42b1c"
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            RowLayout {
                Layout.fillWidth: true
                BusyIndicator { running: Vault.busy; visible: running; implicitHeight: 28; implicitWidth: 28 }
                Item { Layout.fillWidth: true }
                Button {
                    text: "Bytt passord"
                    highlighted: true
                    enabled: !Vault.busy && current.text.length > 0 && next.text.length >= 10 && next.text === next2.text
                    onClicked: Vault.changeMasterPassword(current.text, next.text)
                }
            }
        }
    }

    AppDialog {
        id: settings
        anchors.centerIn: parent
        modal: true
        title: "Innstillinger"
        width: 560
        standardButtons: Dialog.Close

        GridLayout {
            columns: 2
            columnSpacing: 12
            rowSpacing: 8
            anchors.fill: parent
            Label { text: "Lås automatisk etter (minutter)" }
            SpinBox {
                from: 1; to: 240; editable: true
                value: Vault.autoLockMinutes
                onValueModified: Vault.autoLockMinutes = value
            }
            Label { text: "Tøm utklippstavlen etter (sekunder)" }
            SpinBox {
                from: 5; to: 600; editable: true
                value: Vault.clipboardSeconds
                onValueModified: Vault.clipboardSeconds = value
            }
            SectionHeader { text: "Hurtigtaster (globale)"; Layout.columnSpan: 2 }
            Label { text: "Hurtigsøk" }
            TextField {
                text: Vault.hotkeySearch
                placeholderText: "f.eks. Ctrl+Alt+H (tom = av)"
                Layout.fillWidth: true
                onEditingFinished: if (text !== Vault.hotkeySearch) Vault.hotkeySearch = text
            }
            Label { text: "Auto-Type" }
            TextField {
                text: Vault.hotkeyAutoType
                placeholderText: "f.eks. Ctrl+Alt+A (tom = av)"
                Layout.fillWidth: true
                onEditingFinished: if (text !== Vault.hotkeyAutoType) Vault.hotkeyAutoType = text
            }
            Label {
                visible: Vault.hotkeyStatus !== ""
                text: Vault.hotkeyStatus
                color: "#c42b1c"
                wrapMode: Text.WordWrap
                Layout.columnSpan: 2
                Layout.fillWidth: true
            }

            SectionHeader { text: "Auto-Type"; Layout.columnSpan: 2 }
            Label { text: "Standard sekvens" }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                TextField {
                    id: sequenceField
                    text: Vault.autoTypeSequence
                    font.family: "Consolas"
                    Layout.fillWidth: true
                    onEditingFinished: if (Vault.validateSequence(text) === "") Vault.autoTypeSequence = text
                }
                Label {
                    readonly property string err: Vault.validateSequence(sequenceField.text)
                    text: err !== "" ? err
                          : "{USERNAME} {PASSWORD} {TOTP} {TAB} {ENTER} {DELAY 500} …"
                    color: err !== "" ? "#c42b1c" : palette.placeholderText
                    font.pixelSize: 11
                }
            }
            Label {
                text: "Per oppføring: legg til egendefinert felt «Auto-Type» for egen sekvens, "
                      + "eller «Auto-Type-vindu» (f.eks. *Visma*) for hvilket vindu den hører til. "
                      + "Auto-Type virker ikke mot programmer som kjører som administrator."
                color: palette.placeholderText
                font.pixelSize: 11
                wrapMode: Text.WordWrap
                Layout.columnSpan: 2
                Layout.fillWidth: true
            }

            SectionHeader { text: Vault.vaultIsSql ? "Database" : "Fil"; Layout.columnSpan: 2 }
            Label { text: Vault.vaultIsSql ? "SQL Server" : "Hvelvfil"; Layout.topMargin: 8 }
            Label {
                text: Vault.vaultDisplayName
                color: palette.placeholderText
                elide: Text.ElideMiddle
                Layout.maximumWidth: 320
                Layout.topMargin: 8
            }
        }
    }
}
