pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hvelv

// Hurtigsøk som åpnes med global hurtigtast. Enter = Auto-Type i vinduet du kom fra.
Window {
    id: win

    property string targetTitle: ""
    property bool forAutoType: false

    function present(title, autoTypeMode) {
        targetTitle = title
        forAutoType = autoTypeMode
        remember.checked = false
        search.text = ""
        list.currentIndex = Vault.quickEntries.count > 0 ? 0 : -1
        const s = screen
        x = s.virtualX + (s.width - width) / 2
        y = s.virtualY + s.height / 4
        show()
        raise()
        requestActivate()
        search.forceActiveFocus()
    }

    function currentId() {
        return list.currentIndex >= 0 ? Vault.quickEntries.idAt(list.currentIndex) : ""
    }

    function typeSelected() {
        const id = currentId()
        if (id === "")
            return
        hide()
        Vault.autoType(id, remember.checked)
    }

    function copy(field) {
        const id = currentId()
        if (id === "")
            return
        Vault.copyField(id, field)
        hide()
    }

    width: 620
    height: 420
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: "transparent"
    title: "Hvelv hurtigsøk"

    onActiveChanged: if (!active) hide()

    Connections {
        target: Vault
        function onQuickSearchRequested(windowTitle, forAutoType) { win.present(windowTitle, forAutoType) }
        function onUnlockedChanged() { if (!Vault.unlocked) win.hide() }
    }

    Rectangle {
        anchors.fill: parent
        radius: 10
        color: Application.styleHints.colorScheme === Qt.Light ? "white" : "#2b2b2b"
        border.color: "#66757575"

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 8

            Label {
                text: win.targetTitle !== "" ? "Mål: " + win.targetTitle : "Hurtigsøk"
                elide: Text.ElideRight
                color: palette.placeholderText
                font.pixelSize: 12
                Layout.fillWidth: true
            }

            TextField {
                id: search
                Layout.fillWidth: true
                placeholderText: win.forAutoType && Vault.quickEntries.onlyIds.length === 0
                                 ? "Ingen oppføring passet vinduet – søk og velg"
                                 : "Søk …"
                onTextChanged: {
                    if (text !== "")
                        Vault.quickEntries.onlyIds = [] // fritt søk når brukeren skriver
                    Vault.quickEntries.searchText = text
                    list.currentIndex = Vault.quickEntries.count > 0 ? 0 : -1
                }
                Keys.onDownPressed: list.incrementCurrentIndex()
                Keys.onUpPressed: list.decrementCurrentIndex()
                Keys.onReturnPressed: win.typeSelected()
                Keys.onEnterPressed: win.typeSelected()
                Keys.onEscapePressed: win.hide()
                Keys.onPressed: (event) => {
                    if (event.modifiers & Qt.ControlModifier) {
                        if (event.key === Qt.Key_C) { win.copy("password"); event.accepted = true }
                        else if (event.key === Qt.Key_B) { win.copy("username"); event.accepted = true }
                        else if (event.key === Qt.Key_T) { win.copy("totp"); event.accepted = true }
                    }
                }
            }

            ListView {
                id: list
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: Vault.quickEntries
                highlightMoveDuration: 0
                ScrollBar.vertical: ScrollBar {}

                delegate: ItemDelegate {
                    id: row
                    required property int index
                    required property string title
                    required property string customer
                    required property string username
                    required property string environment
                    width: ListView.view.width
                    highlighted: ListView.isCurrentItem
                    onClicked: list.currentIndex = index
                    onDoubleClicked: win.typeSelected()
                    contentItem: RowLayout {
                        Label { text: row.title; font.weight: Font.DemiBold; elide: Text.ElideRight; Layout.fillWidth: true }
                        Label {
                            text: [row.customer, row.environment.toUpperCase(), row.username].filter(s => s).join("  ·  ")
                            color: palette.placeholderText
                            font.pixelSize: 12
                        }
                    }
                }

                Label {
                    anchors.centerIn: parent
                    visible: list.count === 0
                    text: "Ingen treff."
                    color: palette.placeholderText
                }
            }

            CheckBox {
                id: remember
                visible: win.targetTitle !== "" && Vault.quickEntries.onlyIds.length === 0
                text: "Husk dette vinduet for oppføringen jeg velger"
            }

            Label {
                text: (win.targetTitle !== "" ? "Enter: Auto-Type   ·   " : "")
                      + "Ctrl+C: passord   ·   Ctrl+B: brukernavn   ·   Ctrl+T: engangskode   ·   Esc: lukk"
                color: palette.placeholderText
                font.pixelSize: 11
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
        }
    }
}
