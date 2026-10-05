pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hvelv

// Oversikt over alle kontaktpersoner.
AppDialog {
    id: dialog

    signal searchRequested(string text) // vis oppføringene til en kontakt

    title: "Kontaktpersoner"
    modal: true
    width: Math.min(760, parent ? parent.width - 80 : 760)
    height: Math.min(600, parent ? parent.height - 80 : 600)
    standardButtons: Dialog.Close

    property string filterText: ""
    readonly property var filtered: Vault.contacts.filter(c => {
        const f = filterText.toLowerCase()
        return f === "" || [c.name, c.company, c.email, c.role, c.phone].join(" ").toLowerCase().includes(f)
    })

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            TextField {
                placeholderText: "Søk i kontakter"
                Layout.fillWidth: true
                onTextChanged: dialog.filterText = text
            }
            SourceComboBox { id: newContactSource; visible: Vault.openSourceCount > 1 }
            Button {
                text: "Ny kontakt"
                highlighted: true
                onClicked: editor.edit({ source: newContactSource.currentKey })
            }
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: dialog.filtered
            ScrollBar.vertical: ScrollBar {}

            delegate: ItemDelegate {
                id: row
                required property var modelData
                width: ListView.view.width
                onDoubleClicked: editor.edit(modelData)
                contentItem: RowLayout {
                    spacing: 12
                    ColumnLayout {
                        spacing: 1
                        Layout.fillWidth: true
                        Label {
                            text: row.modelData.name || row.modelData.email
                            font.weight: Font.DemiBold
                        }
                        Label {
                            text: [row.modelData.role, row.modelData.company, row.modelData.email, row.modelData.phone,
                                   Vault.openSourceCount > 1 ? "i «" + row.modelData.sourceName + "»" : ""]
                                  .filter(s => s).join("  ·  ")
                            color: palette.placeholderText
                            font.pixelSize: 12
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }
                    Button {
                        flat: true
                        text: row.modelData.usage + (row.modelData.usage === 1 ? " oppføring" : " oppføringer")
                        enabled: row.modelData.usage > 0
                        onClicked: {
                            dialog.searchRequested(row.modelData.email || row.modelData.name)
                            dialog.close()
                        }
                    }
                    ToolButton { text: "Rediger"; onClicked: editor.edit(row.modelData) }
                    ToolButton {
                        text: "Slett"
                        onClicked: {
                            confirm.contact = row.modelData
                            confirm.open()
                        }
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: list.count === 0
                color: palette.placeholderText
                text: Vault.contacts.length === 0 ? "Ingen kontakter ennå." : "Ingen treff."
            }
        }
    }

    ContactEditDialog {
        id: editor
        anchors.centerIn: parent
    }

    AppDialog {
        id: confirm
        property var contact: ({})
        anchors.centerIn: parent
        modal: true
        width: 400
        title: "Slette kontakt?"
        standardButtons: Dialog.Yes | Dialog.No
        Label {
            width: confirm.availableWidth
            wrapMode: Text.WordWrap
            text: "«" + (confirm.contact.name || confirm.contact.email || "") + "» slettes"
                  + (confirm.contact.usage > 0 ? " og fjernes fra " + confirm.contact.usage + " oppføring(er)." : ".")
        }
        onAccepted: Vault.deleteContact(contact.id)
    }
}
