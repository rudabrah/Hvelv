pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Hvelv

Item {
    id: root

    property var source: ({})          // map fra Vault.entry() eller Vault.newEntry()
    readonly property bool isNew: !Vault.entry(source.id || "").id

    // Redigeringstilstand som ikke er enkle tekstfelt
    property string typeKey: "login"
    readonly property var typeInfo: Vault.entryType(typeKey)
    property var fieldValues: ({})
    property var customFields: []
    property var contactIds: []
    property var attachments: []
    property url pendingCertificate: ""  // legges ved etter første lagring
    property string tlsTarget: ""        // venter på svar fra Vault.checkTls

    signal saved(string id)
    signal cancelled()

    function load() {
        typeKey = source.type || "login"
        fieldValues = Object.assign({}, source.fields || {})
        customFields = (source.customFields || []).map(f => Object.assign({}, f))
        contactIds = (source.contactIds || []).slice()
        attachments = source.attachments || []
        pendingCertificate = ""
        title.text = source.title || ""
        url.text = source.url || ""
        customer.editText = source.customer || ""
        system.editText = source.system || ""
        environment.editText = source.environment || ""
        tags.text = (source.tags || []).join(", ")
        username.text = source.username || ""
        password.text = source.password || ""
        platform.editText = source.twoFactorPlatform || ""
        totpSecret.text = source.totpSecret || ""
        owner.editText = source.owner || ""
        expiresAt.text = source.expiresAt || ""
        reminderDays.value = source.reminderDays !== undefined ? source.reminderDays : 14
        channel.currentIndex = Math.max(0, ["app", "email", "both"].indexOf(source.reminderChannel || "app"))
        renewal.text = source.renewalProcedure || ""
        notes.text = source.notes || ""
        title.forceActiveFocus()
    }

    function collect() {
        const m = Object.assign({}, source)
        m.type = typeKey
        m.title = title.text
        m.url = url.text.trim()
        m.customer = customer.editText.trim()
        m.system = system.editText.trim()
        m.environment = environment.editText.trim()
        m.tags = tags.text.split(",").map(t => t.trim()).filter(t => t !== "")
        m.username = username.text
        m.password = password.text
        // Bare felt som hører til valgt type lagres (ikke gjenglemte hemmeligheter fra en annen type).
        m.fields = {}
        for (const f of (typeInfo.fields || []))
            if (fieldValues[f.key])
                m.fields[f.key] = fieldValues[f.key]
        m.customFields = customFields
        m.twoFactorPlatform = platform.editText.trim()
        m.totpSecret = totpSecret.text.trim()
        m.contactIds = contactIds
        m.owner = owner.editText.trim()
        m.expiresAt = expiresAt.text.trim()
        m.reminderDays = reminderDays.value
        m.reminderChannel = ["app", "email", "both"][channel.currentIndex]
        m.renewalProcedure = renewal.text
        m.notes = notes.text
        return m
    }

    function save() {
        const id = Vault.saveEntry(collect())
        if (id === "")
            return
        if (pendingCertificate.toString() !== "")
            Vault.addAttachment(id, pendingCertificate)
        saved(id)
    }

    function addMonths(months) {
        const base = /^\d{4}-\d{2}-\d{2}$/.test(expiresAt.text) ? new Date(expiresAt.text) : new Date()
        base.setMonth(base.getMonth() + months)
        expiresAt.text = Qt.formatDate(base, "yyyy-MM-dd")
    }

    function applyCertificate(fileUrl) {
        const info = Vault.readCertificate(fileUrl, password.text)
        if (!info.ok) {
            certError.text = info.error
            return
        }
        certError.text = ""
        const f = Object.assign({}, fieldValues)
        for (const k of ["subject", "issuer", "thumbprint", "serialNumber", "validFrom"])
            f[k] = info[k]
        fieldValues = f
        expiresAt.text = info.expiresAt
        if (title.text.trim() === "") {
            const cn = /CN=([^,]+)/.exec(info.subject)
            title.text = cn ? cn[1] : info.subject
        }
        if (attachFile.checked)
            pendingCertificate = fileUrl
    }

    function applyQr(r) {
        if (!r.ok) {
            qrStatus.text = r.error
            return
        }
        qrStatus.text = ""
        totpSecret.text = r.otpauth
        if (platform.editText === "")
            platform.editText = "TOTP i Hvelv"
    }

    function setField(key, value) {
        fieldValues[key] = value // ingen re-binding; leses i collect()
    }

    // Scroll skjemaet slik at feltet som får fokus (f.eks. via Tab) er synlig.
    function ensureVisible(item) {
        if (!item || !root.visible)
            return
        let p = item
        while (p && p !== form)
            p = p.parent
        if (!p)
            return
        const flick = scroll.contentItem
        const y = item.mapToItem(form, 0, 0).y
        const margin = 48
        if (y - margin < flick.contentY)
            flick.contentY = Math.max(0, y - margin)
        else if (y + item.height + margin > flick.contentY + flick.height)
            flick.contentY = Math.min(flick.contentHeight - flick.height, y + item.height + margin - flick.height)
    }

    Connections {
        target: root.Window.window
        function onActiveFocusItemChanged() { root.ensureVisible(root.Window.activeFocusItem) }
    }

    Connections {
        target: Vault
        function onTlsChecked(target, r) {
            if (target !== root.tlsTarget || !root.visible)
                return
            root.tlsTarget = ""
            if (!r.ok) {
                certError.text = "Kunne ikke hente sertifikat: " + r.error
                return
            }
            const f = Object.assign({}, root.fieldValues)
            for (const k of ["subject", "issuer", "thumbprint", "serialNumber", "validFrom"])
                f[k] = r[k]
            f.host = f.host || r.host
            root.fieldValues = f
            expiresAt.text = r.expiresAt
            if (title.text.trim() === "")
                title.text = r.host
        }
        function onQrScanned(r) { if (root.visible) root.applyQr(r) }
        function onEntryChanged(id) {
            if (id === root.source.id)
                root.attachments = Vault.entry(id).attachments || []
        }
    }

    onSourceChanged: load()
    Component.onCompleted: { Vault.clearError(); load() }

    Shortcut { sequence: StandardKey.Save; enabled: root.visible; onActivated: root.save() }
    Shortcut { sequence: "Esc"; enabled: root.visible; onActivated: root.cancelled() }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 20
            Layout.bottomMargin: 8
            Label {
                text: root.isNew ? "Ny oppføring" : "Rediger oppføring"
                font.pixelSize: 20
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }
            Button { text: "Avbryt"; onClicked: root.cancelled() }
            Button { text: "Lagre"; highlighted: true; onClicked: root.save() }
        }

        Label {
            visible: Vault.errorMessage !== ""
            text: Vault.errorMessage
            color: "#c42b1c"
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
        }

        ScrollView {
            id: scroll
            Layout.fillWidth: true
            Layout.maximumWidth: root.width // ellers kan innholdets bredde dra ScrollView-en ut av panelet
            Layout.fillHeight: true
            contentWidth: availableWidth

            GridLayout {
                id: form
                width: scroll.availableWidth - 40
                x: 20
                columns: 2
                columnSpacing: 12
                rowSpacing: 8

                // --- Type og system ---
                SectionHeader { text: "System"; Layout.columnSpan: 2 }
                Label { text: "Type" }
                ComboBox {
                    id: typeBox
                    Layout.fillWidth: true
                    model: Vault.entryTypes
                    textRole: "label"
                    valueRole: "key"
                    currentIndex: Math.max(0, Vault.entryTypes.findIndex(t => t.key === root.typeKey))
                    onActivated: root.typeKey = currentValue
                }
                Item { implicitWidth: 1; implicitHeight: 1 }
                Label {
                    text: root.typeInfo.description || ""
                    color: palette.placeholderText
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                Label { text: "Navn *" }
                TextField { id: title; placeholderText: "f.eks. Visma.net – Kunde AS (prod)"; Layout.fillWidth: true }
                Label { text: "Kunde / tredjepart" }
                ComboBox { id: customer; editable: true; currentIndex: -1; model: Vault.customers; Layout.fillWidth: true }
                Label { text: "System" }
                ComboBox { id: system; editable: true; currentIndex: -1; model: Vault.systems; Layout.fillWidth: true }
                Label { text: "Miljø" }
                ComboBox {
                    id: environment
                    editable: true
                    currentIndex: -1
                    model: ["prod", "test", "qa", "dev"]
                    Layout.preferredWidth: 200
                }
                Label { text: "Adresse (URL)" }
                TextField { id: url; placeholderText: "https://…"; Layout.fillWidth: true }
                Label { text: "Tagger" }
                TextField { id: tags; placeholderText: "kommaseparert, f.eks. ERP, integrasjon"; Layout.fillWidth: true }

                // --- Pålogging / hemmelighet ---
                SectionHeader { text: "Pålogging"; Layout.columnSpan: 2 }
                Label { text: root.typeInfo.usernameLabel || "Brukernavn" }
                TextField { id: username; Layout.fillWidth: true }
                Label { text: root.typeInfo.passwordLabel || "Passord" }
                RowLayout {
                    Layout.fillWidth: true
                    TextField {
                        id: password
                        echoMode: showPw.checked ? TextInput.Normal : TextInput.Password
                        font.family: showPw.checked ? "Consolas" : Qt.application.font.family // qmllint disable missing-property
                        Layout.fillWidth: true
                    }
                    ToolButton { id: showPw; text: checked ? "Skjul" : "Vis"; checkable: true }
                    Button { text: "Generer…"; onClicked: generator.open() }
                }
                Item { implicitWidth: 1; implicitHeight: 1; visible: password.text !== "" }
                RowLayout {
                    visible: password.text !== ""
                    Layout.fillWidth: true
                    readonly property int bits: Vault.passwordBits(password.text)
                    readonly property color tone: bits < 28 ? "#c42b1c" : bits < 50 ? "#b45309" : bits < 80 ? "#2563eb" : "#107c10"
                    id: strength
                    Rectangle {
                        Layout.preferredWidth: 160
                        implicitHeight: 6
                        radius: 3
                        color: Qt.alpha(palette.mid, 0.4)
                        Rectangle {
                            width: parent.width * Math.min(1, strength.bits / 100)
                            height: parent.height
                            radius: 3
                            color: strength.tone
                        }
                    }
                    Label {
                        text: (strength.bits < 28 ? "Svært svakt" : strength.bits < 50 ? "Svakt" : strength.bits < 80 ? "Greit" : "Sterkt")
                              + "  (≈ " + strength.bits + " bit)"
                        color: strength.tone
                        font.pixelSize: 12
                    }
                    Item { Layout.fillWidth: true }
                }

                // --- Typespesifikke felt ---
                SectionHeader {
                    text: root.typeInfo.label || ""
                    visible: (root.typeInfo.fields || []).length > 0
                    Layout.columnSpan: 2
                }
                RowLayout {
                    visible: root.typeKey === "certificate"
                    Layout.columnSpan: 2
                    Layout.fillWidth: true
                    Button {
                        text: "Les fra sertifikatfil…"
                        onClicked: certDialog.open()
                    }
                    Button {
                        text: "Hent fra nettsted"
                        enabled: !Vault.networkBusy && (url.text.trim() !== "" || !!root.fieldValues.host)
                        onClicked: {
                            certError.text = ""
                            root.tlsTarget = url.text.trim() !== "" ? url.text.trim() : root.fieldValues.host
                            Vault.checkTls(root.tlsTarget)
                        }
                    }
                    CheckBox { id: attachFile; text: "Legg ved filen"; checked: true }
                    Label {
                        id: certError
                        color: "#c42b1c"
                        visible: text !== ""
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                    Item { Layout.fillWidth: true; visible: certError.text === "" }
                }
                Label {
                    visible: root.typeKey === "certificate"
                    Layout.columnSpan: 2
                    text: "For PFX/P12: skriv PFX-passordet i feltet over først."
                    color: palette.placeholderText
                    font.pixelSize: 12
                }
                Repeater {
                    model: root.typeInfo.fields || []
                    delegate: RowLayout {
                        id: fieldRow
                        required property var modelData
                        Layout.columnSpan: 2
                        Layout.fillWidth: true
                        spacing: 12
                        Label {
                            text: fieldRow.modelData.label
                            Layout.preferredWidth: title.x - form.columnSpacing
                            Layout.alignment: fieldRow.modelData.multiline ? Qt.AlignTop : Qt.AlignVCenter
                            topPadding: fieldRow.modelData.multiline ? 6 : 0
                        }
                        TextField {
                            visible: !fieldRow.modelData.multiline
                            text: root.fieldValues[fieldRow.modelData.key] || ""
                            placeholderText: fieldRow.modelData.placeholder
                            echoMode: fieldRow.modelData.secret ? TextInput.PasswordEchoOnEdit : TextInput.Normal
                            Layout.fillWidth: true
                            onTextEdited: root.setField(fieldRow.modelData.key, text)
                        }
                        TextArea {
                            visible: fieldRow.modelData.multiline
                            text: root.fieldValues[fieldRow.modelData.key] || ""
                            placeholderText: fieldRow.modelData.placeholder
                            font.family: "Consolas"
                            wrapMode: TextEdit.WrapAnywhere
                            Layout.fillWidth: true
                            Layout.preferredHeight: 90
                            onTextChanged: if (activeFocus) root.setField(fieldRow.modelData.key, text)
                        }
                    }
                }

                // --- Egne felt ---
                SectionHeader { text: "Egne felt"; Layout.columnSpan: 2 }
                Repeater {
                    model: root.customFields
                    delegate: RowLayout {
                        id: customRow
                        required property var modelData
                        required property int index
                        Layout.columnSpan: 2
                        Layout.fillWidth: true
                        TextField {
                            text: customRow.modelData.name
                            placeholderText: "Navn"
                            Layout.preferredWidth: 180
                            onTextEdited: root.customFields[customRow.index].name = text
                        }
                        TextField {
                            text: customRow.modelData.value
                            placeholderText: "Verdi"
                            echoMode: secretBox.checked ? TextInput.PasswordEchoOnEdit : TextInput.Normal
                            Layout.fillWidth: true
                            onTextEdited: root.customFields[customRow.index].value = text
                        }
                        CheckBox {
                            id: secretBox
                            text: "Hemmelig"
                            checked: customRow.modelData.secret
                            onToggled: root.customFields[customRow.index].secret = checked
                        }
                        ToolButton {
                            text: "Fjern"
                            onClicked: root.customFields = root.customFields.filter((_, i) => i !== customRow.index)
                        }
                    }
                }
                Button {
                    text: "Legg til felt"
                    flat: true
                    Layout.columnSpan: 2
                    onClicked: root.customFields = root.customFields.concat([{ name: "", value: "", secret: false }])
                }

                // --- 2FA ---
                SectionHeader { text: "Tofaktor (2FA)"; Layout.columnSpan: 2 }
                Label { text: "Plattform" }
                ComboBox {
                    id: platform
                    editable: true
                    Layout.fillWidth: true
                    model: ["", "TOTP i Hvelv", "Microsoft Authenticator", "Google Authenticator",
                            "Authy", "SMS", "E-post", "YubiKey / FIDO2", "BankID", "Duo", "Okta Verify"]
                }
                Label { text: "TOTP-hemmelighet" }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    RowLayout {
                        Layout.fillWidth: true
                        TextField {
                            id: totpSecret
                            placeholderText: "base32-nøkkel eller otpauth://-lenke"
                            echoMode: TextInput.PasswordEchoOnEdit
                            Layout.fillWidth: true
                        }
                        Button {
                            text: "Skann QR"
                            onClicked: qrMenu.popup()
                            Menu {
                                id: qrMenu
                                MenuItem {
                                    text: "Fra skjermen"
                                    onTriggered: { qrStatus.text = ""; Vault.scanQrFromScreen() }
                                }
                                MenuItem {
                                    text: "Fra utklippstavlen (skjermbilde)"
                                    onTriggered: root.applyQr(Vault.scanQrFromClipboard())
                                }
                                MenuItem {
                                    text: "Fra bildefil…"
                                    onTriggered: qrFileDialog.open()
                                }
                            }
                        }
                    }
                    Label {
                        id: qrStatus
                        visible: text !== ""
                        color: "#c42b1c"
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    Label {
                        readonly property bool ok: Vault.isValidTotp(totpSecret.text)
                        visible: totpSecret.text !== ""
                        text: ok ? "Gyldig – koden vises i oppføringen." : "Ugyldig nøkkel."
                        color: ok ? "#107c10" : "#c42b1c"
                        font.pixelSize: 12
                    }
                }

                // --- Kontaktpersoner ---
                SectionHeader { text: "Kontaktpersoner"; Layout.columnSpan: 2 }
                Flow {
                    Layout.columnSpan: 2
                    Layout.fillWidth: true
                    spacing: 6
                    visible: root.contactIds.length > 0
                    Repeater {
                        model: root.contactIds
                        delegate: Rectangle {
                            id: chip
                            required property string modelData
                            readonly property var c: Vault.contact(modelData)
                            radius: 12
                            color: Qt.alpha(palette.highlight, 0.12)
                            implicitHeight: chipRow.implicitHeight + 6
                            implicitWidth: chipRow.implicitWidth + 16
                            RowLayout {
                                id: chipRow
                                anchors.centerIn: parent
                                spacing: 4
                                Label {
                                    text: (chip.c.name || chip.c.email || "?") + (chip.c.company ? " (" + chip.c.company + ")" : "")
                                }
                                ToolButton {
                                    text: "✕"
                                    implicitWidth: 24
                                    implicitHeight: 24
                                    onClicked: root.contactIds = root.contactIds.filter(id => id !== chip.modelData)
                                }
                            }
                        }
                    }
                }
                RowLayout {
                    Layout.columnSpan: 2
                    Layout.fillWidth: true
                    ComboBox {
                        id: contactPicker
                        Layout.fillWidth: true
                        readonly property var available: Vault.contacts.filter(c => !root.contactIds.includes(c.id))
                        model: available
                        textRole: "name"
                        displayText: available.length > 0 ? "Koble til eksisterende kontakt …" : "Ingen andre kontakter"
                        enabled: available.length > 0
                        currentIndex: -1
                        onActivated: (i) => {
                            root.contactIds = root.contactIds.concat([available[i].id])
                            currentIndex = -1
                        }
                    }
                    Button { text: "Ny kontakt…"; onClicked: contactEditor.edit({ company: customer.editText }) }
                }

                // --- Internt ---
                SectionHeader { text: "Internt"; Layout.columnSpan: 2 }
                Label { text: "Ansvarlig hos oss" }
                ComboBox { id: owner; editable: true; currentIndex: -1; model: Vault.owners; Layout.fillWidth: true }

                // --- Utløp ---
                SectionHeader { text: "Utløp og fornyelse"; Layout.columnSpan: 2 }
                Label { text: root.typeInfo.expiryLabel || "Utløper" }
                // Flow, så hurtigknappene brytes til ny linje i smale vinduer.
                Flow {
                    Layout.fillWidth: true
                    spacing: 4
                    TextField {
                        id: expiresAt
                        placeholderText: "ÅÅÅÅ-MM-DD"
                        inputMethodHints: Qt.ImhDate
                        validator: RegularExpressionValidator { regularExpression: /^$|^\d{0,4}(-\d{0,2}(-\d{0,2})?)?$/ }
                        width: 140
                    }
                    Button { text: "+3 mnd"; flat: true; onClicked: root.addMonths(3) }
                    Button { text: "+6 mnd"; flat: true; onClicked: root.addMonths(6) }
                    Button { text: "+1 år"; flat: true; onClicked: root.addMonths(12) }
                    Button { text: "+2 år"; flat: true; onClicked: root.addMonths(24) }
                    Button { text: "Fjern"; flat: true; enabled: expiresAt.text !== ""; onClicked: expiresAt.text = "" }
                }
                Label { text: "Varsle" }
                RowLayout {
                    Layout.fillWidth: true
                    enabled: expiresAt.text !== ""
                    SpinBox { id: reminderDays; from: 0; to: 365; editable: true }
                    Label { text: "dager før, via" }
                    ComboBox {
                        id: channel
                        model: ["I appen", "E-post", "Begge"]
                        Layout.preferredWidth: 150
                    }
                    Item { Layout.fillWidth: true }
                }
                Item { implicitWidth: 1; visible: channel.currentIndex > 0 }
                Label {
                    visible: channel.currentIndex > 0
                    text: "E-postvarsler sendes av Hvelv-serveren (fase 2). Til da vises varsler i appen for «Begge»."
                    color: palette.placeholderText
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                Label { text: "Slik fornyer du"; Layout.alignment: Qt.AlignTop; topPadding: 6 }
                TextArea {
                    id: renewal
                    Layout.fillWidth: true
                    Layout.preferredHeight: 90
                    wrapMode: TextEdit.Wrap
                    placeholderText: "Hvem bestiller, hvor fornyes det, hva må oppdateres etterpå …"
                }

                // --- Vedlegg ---
                SectionHeader { text: "Vedlegg"; Layout.columnSpan: 2 }
                Label {
                    visible: root.isNew
                    Layout.columnSpan: 2
                    text: "Lagre oppføringen først for å legge til vedlegg."
                    color: palette.placeholderText
                }
                Repeater {
                    model: root.isNew ? [] : root.attachments
                    delegate: RowLayout {
                        id: fileRow
                        required property var modelData
                        Layout.columnSpan: 2
                        Layout.fillWidth: true
                        Label { text: fileRow.modelData.name; Layout.fillWidth: true; elide: Text.ElideMiddle }
                        Label { text: Math.max(1, Math.round(fileRow.modelData.size / 1024)) + " kB"; color: palette.placeholderText }
                        ToolButton { text: "Fjern"; onClicked: Vault.removeAttachment(root.source.id, fileRow.modelData.id) }
                    }
                }
                Button {
                    visible: !root.isNew
                    text: "Legg til fil…"
                    flat: true
                    Layout.columnSpan: 2
                    onClicked: attachDialog.open()
                }

                // --- Notater ---
                SectionHeader { text: "Notater"; Layout.columnSpan: 2 }
                TextArea {
                    id: notes
                    Layout.columnSpan: 2
                    Layout.fillWidth: true
                    Layout.preferredHeight: 120
                    Layout.bottomMargin: 20
                    wrapMode: TextEdit.Wrap
                    placeholderText: "Avtalenummer, tilgangsnivå, annet …"
                }
            }
        }
    }

    PasswordGeneratorDialog {
        id: generator
        anchors.centerIn: parent
        onUsePassword: (pw) => { password.text = pw; showPw.checked = true }
    }

    ContactEditDialog {
        id: contactEditor
        anchors.centerIn: parent
        onSaved: (id) => root.contactIds = root.contactIds.concat([id])
    }

    FileDialog {
        id: certDialog
        title: "Velg sertifikatfil"
        nameFilters: ["Sertifikater (*.pfx *.p12 *.cer *.crt *.pem *.der)", "Alle filer (*)"]
        onAccepted: root.applyCertificate(selectedFile)
    }

    FileDialog {
        id: qrFileDialog
        title: "Velg bilde med QR-kode"
        nameFilters: ["Bilder (*.png *.jpg *.jpeg *.bmp *.gif)", "Alle filer (*)"]
        onAccepted: root.applyQr(Vault.scanQrFromFile(selectedFile))
    }

    FileDialog {
        id: attachDialog
        title: "Legg til vedlegg"
        onAccepted: Vault.addAttachment(root.source.id, selectedFile)
    }
}
