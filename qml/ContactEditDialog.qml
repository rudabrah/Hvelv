import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hvelv

// Opprett eller rediger én kontaktperson.
AppDialog {
    id: dialog

    property var contact: ({})
    signal saved(string id)

    function edit(c) {
        contact = c || {}
        Vault.clearError()
        name.text = contact.name || ""
        company.text = contact.company || ""
        role.text = contact.role || ""
        email.text = contact.email || ""
        phone.text = contact.phone || ""
        notes.text = contact.notes || ""
        open()
        name.forceActiveFocus()
    }

    title: contact.id ? "Rediger kontakt" : "Ny kontakt"
    modal: true
    width: 460
    standardButtons: Dialog.Save | Dialog.Cancel

    onAccepted: {
        const c = Object.assign({}, contact, {
            name: name.text, company: company.text, role: role.text,
            email: email.text, phone: phone.text, notes: notes.text
        })
        const id = Vault.saveContact(c)
        if (id !== "")
            saved(id)
        else
            open() // vis feilen og la brukeren rette
    }

    GridLayout {
        anchors.fill: parent
        columns: 2
        columnSpacing: 12
        rowSpacing: 8

        Label { text: "Navn" }
        TextField { id: name; Layout.fillWidth: true }
        Label { text: "Firma" }
        TextField { id: company; Layout.fillWidth: true }
        Label { text: "Rolle" }
        TextField { id: role; placeholderText: "kundeansvarlig, support, teknisk kontakt …"; Layout.fillWidth: true }
        Label { text: "E-post" }
        TextField { id: email; Layout.fillWidth: true; inputMethodHints: Qt.ImhEmailCharactersOnly }
        Label { text: "Telefon" }
        TextField { id: phone; Layout.fillWidth: true; inputMethodHints: Qt.ImhDialableCharactersOnly }
        Label { text: "Notater"; Layout.alignment: Qt.AlignTop; topPadding: 6 }
        TextArea { id: notes; Layout.fillWidth: true; Layout.preferredHeight: 70; wrapMode: TextEdit.Wrap }
        Label {
            Layout.columnSpan: 2
            Layout.fillWidth: true
            visible: Vault.errorMessage !== ""
            text: Vault.errorMessage
            color: "#c42b1c"
            wrapMode: Text.WordWrap
        }
    }
}
