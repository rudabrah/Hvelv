pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Hvelv

ScrollView {
    id: root

    property string entryId
    property int revision: 0
    property var e: ({})
    readonly property var typeInfo: Vault.entryType(e.type || "login")
    property var otp: ({ valid: false })
    property bool showHistory: false

    signal editRequested()
    signal deleteRequested()

    function channelText(c) {
        return c === "email" ? "E-post" : c === "both" ? "I appen og på e-post" : "I appen"
    }

    function expiryText() {
        if (!e.expiresAt)
            return ""
        const d = e.daysLeft
        if (d < 0) return e.expiresAt + "  ·  utløpt for " + (-d) + (d === -1 ? " dag" : " dager") + " siden"
        if (d === 0) return e.expiresAt + "  ·  utløper i dag"
        return e.expiresAt + "  ·  om " + d + (d === 1 ? " dag" : " dager")
    }

    function formatDateTime(iso) {
        return iso ? new Date(iso).toLocaleString(Qt.locale("nb_NO"), Locale.ShortFormat) : ""
    }

    function reload() {
        e = entryId !== "" ? Vault.entry(entryId) : ({})
        showHistory = false
    }

    function refreshOtp() {
        otp = entryId !== "" && e.totpSecret ? Vault.totp(entryId) : ({ valid: false })
    }

    onEntryIdChanged: reload()
    onRevisionChanged: reload()
    onEChanged: refreshOtp()
    Component.onCompleted: reload()

    Connections {
        target: Vault
        function onEntryChanged(id) { if (id === root.entryId) root.reload() }
        function onDataChanged() { if (root.entryId !== "") root.reload() }
    }

    Timer {
        interval: 1000
        repeat: true
        running: root.visible && root.otp.valid
        onTriggered: root.refreshOtp()
    }

    contentWidth: availableWidth

    ColumnLayout {
        width: root.availableWidth
        spacing: 2

        // --- Topp ---
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            Layout.topMargin: 20

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Label {
                    text: root.e.title || ""
                    font.pixelSize: 22
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Label {
                    text: [root.e.typeLabel, root.e.customer, root.e.system].filter(s => s).join("  ·  ")
                    visible: text !== ""
                    color: palette.placeholderText
                }
            }
            Button {
                text: "Rediger"
                highlighted: true
                onClicked: root.editRequested()
            }
            Button {
                text: "Slett"
                onClicked: root.deleteRequested()
            }
        }

        // --- Miljø og tagger ---
        Flow {
            Layout.fillWidth: true
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            Layout.topMargin: 6
            spacing: 6
            visible: (root.e.tags || []).length > 0 || !!root.e.environment
            Repeater {
                model: (root.e.environment ? ["env:" + root.e.environment] : []).concat(root.e.tags || [])
                delegate: Rectangle {
                    id: chip
                    required property string modelData
                    readonly property bool isEnv: modelData.startsWith("env:")
                    readonly property color tone: !isEnv ? palette.highlight
                                                  : modelData === "env:prod" ? "#c42b1c" : "#107c10"
                    radius: 10
                    color: Qt.alpha(tone, 0.12)
                    implicitWidth: tagLabel.implicitWidth + 16
                    implicitHeight: tagLabel.implicitHeight + 6
                    Label {
                        id: tagLabel
                        anchors.centerIn: parent
                        text: chip.isEnv ? chip.modelData.substring(4).toUpperCase() : chip.modelData
                        font.pixelSize: 12
                        font.weight: chip.isEnv ? Font.DemiBold : Font.Normal
                        color: chip.tone
                    }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            Layout.bottomMargin: 24
            spacing: 2

            // --- Pålogging ---
            SectionHeader {
                text: "Pålogging"
                visible: (root.e.url || root.e.username || root.e.password || "") !== ""
            }
            CopyField {
                label: "Adresse"
                value: root.e.url || ""
                link: /^https?:\/\//i.test(value) ? value : (value !== "" ? "https://" + value : "")
                sensitiveCopy: false
            }
            CopyField {
                label: root.typeInfo.usernameLabel || "Brukernavn"
                value: root.e.username || ""
                sensitiveCopy: false
            }
            CopyField {
                label: root.typeInfo.passwordLabel || "Passord"
                value: root.e.password || ""
                secret: true
                monospace: true
                customCopy: true
                onCopyRequested: Vault.copyField(root.entryId, "password")
            }
            RowLayout {
                visible: (root.e.passwordHistory || []).length > 0 || !!root.e.passwordChangedAt
                Layout.fillWidth: true
                spacing: 8
                Label {
                    text: root.e.passwordChangedAt ? "Sist byttet " + root.formatDateTime(root.e.passwordChangedAt) : ""
                    color: palette.placeholderText
                    font.pixelSize: 12
                    Layout.leftMargin: 158
                }
                Button {
                    flat: true
                    visible: (root.e.passwordHistory || []).length > 0
                    text: (root.showHistory ? "Skjul" : "Vis") + " tidligere passord (" + (root.e.passwordHistory || []).length + ")"
                    font.pixelSize: 12
                    onClicked: root.showHistory = !root.showHistory
                }
                Item { Layout.fillWidth: true }
            }
            Repeater {
                model: root.showHistory ? (root.e.passwordHistory || []) : []
                delegate: CopyField {
                    required property var modelData
                    required property int index
                    label: "Byttet " + root.formatDateTime(modelData.changedAt)
                    value: modelData.password
                    secret: true
                    monospace: true
                    customCopy: true
                    onCopyRequested: Vault.copyField(root.entryId, "history." + index)
                }
            }

            // --- Typespesifikke felt ---
            SectionHeader {
                text: root.typeInfo.label || ""
                visible: (root.typeInfo.fields || []).some(f => (root.e.fields || {})[f.key])
            }
            Repeater {
                model: root.typeInfo.fields || []
                delegate: CopyField {
                    required property var modelData
                    label: modelData.label
                    value: (root.e.fields || {})[modelData.key] || ""
                    secret: modelData.secret
                    monospace: modelData.secret || modelData.multiline
                    customCopy: true
                    onCopyRequested: Vault.copyField(root.entryId, "fields." + modelData.key)
                }
            }

            // --- Egne felt ---
            SectionHeader {
                text: "Egne felt"
                visible: (root.e.customFields || []).length > 0
            }
            Repeater {
                model: root.e.customFields || []
                delegate: CopyField {
                    required property var modelData
                    required property int index
                    label: modelData.name
                    value: modelData.value
                    secret: modelData.secret
                    customCopy: true
                    onCopyRequested: Vault.copyField(root.entryId, "custom." + index)
                }
            }

            // --- 2FA ---
            SectionHeader {
                text: "Tofaktor (2FA)"
                visible: (root.e.twoFactorPlatform || "") !== "" || root.otp.valid
            }
            CopyField {
                label: "Plattform"
                value: root.e.twoFactorPlatform || ""
                copyable: false
            }
            RowLayout {
                visible: root.otp.valid
                spacing: 8
                Layout.fillWidth: true
                Label {
                    text: "Engangskode"
                    color: palette.placeholderText
                    Layout.preferredWidth: 150
                }
                Label {
                    text: root.otp.valid ? root.otp.code.replace(/(\d{3})(?=\d)/g, "$1 ") : ""
                    font.family: "Consolas"
                    font.pixelSize: 22
                    font.weight: Font.DemiBold
                }
                Item {
                    implicitWidth: 28
                    implicitHeight: 28
                    Canvas {
                        anchors.fill: parent
                        property real frac: root.otp.valid ? root.otp.remaining / root.otp.period : 0
                        onFracChanged: requestPaint()
                        onPaint: {
                            const ctx = getContext("2d")
                            ctx.reset()
                            ctx.lineWidth = 3
                            ctx.strokeStyle = frac < 0.2 ? "#c42b1c" : palette.highlight
                            ctx.beginPath()
                            ctx.arc(width / 2, height / 2, width / 2 - 3, -Math.PI / 2, -Math.PI / 2 + 2 * Math.PI * frac)
                            ctx.stroke()
                        }
                    }
                    Label {
                        anchors.centerIn: parent
                        text: root.otp.valid ? root.otp.remaining : ""
                        font.pixelSize: 10
                    }
                }
                Item { Layout.fillWidth: true }
                ToolButton {
                    text: "Kopier"
                    onClicked: Vault.copyField(root.entryId, "totp")
                }
            }

            // --- Kontaktpersoner ---
            SectionHeader {
                text: (root.e.contacts || []).length === 1 ? "Kontaktperson" : "Kontaktpersoner"
                visible: (root.e.contacts || []).length > 0
            }
            Repeater {
                model: root.e.contacts || []
                delegate: ColumnLayout {
                    id: contactBlock
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    spacing: 0
                    Label {
                        text: contactBlock.modelData.name || contactBlock.modelData.email
                        font.weight: Font.DemiBold
                        topPadding: 4
                    }
                    Label {
                        text: [contactBlock.modelData.role, contactBlock.modelData.company].filter(s => s).join(", ")
                        visible: text !== ""
                        color: palette.placeholderText
                        font.pixelSize: 12
                    }
                    CopyField {
                        label: "E-post"
                        value: contactBlock.modelData.email || ""
                        link: value !== "" ? "mailto:" + value + "?subject=" + encodeURIComponent(root.e.title || "") : ""
                        sensitiveCopy: false
                    }
                    CopyField {
                        label: "Telefon"
                        value: contactBlock.modelData.phone || ""
                        sensitiveCopy: false
                    }
                }
            }

            // --- Internt ---
            SectionHeader {
                text: "Internt"
                visible: (root.e.owner || "") !== ""
            }
            CopyField { label: "Ansvarlig"; value: root.e.owner || ""; copyable: false }

            // --- Utløp ---
            SectionHeader {
                text: "Utløp og fornyelse"
                visible: (root.e.expiresAt || root.e.renewalProcedure || "") !== ""
            }
            RowLayout {
                visible: (root.e.expiresAt || "") !== ""
                spacing: 8
                Layout.fillWidth: true
                Label {
                    text: root.typeInfo.expiryLabel || "Utløper"
                    color: palette.placeholderText
                    Layout.preferredWidth: 150
                }
                Label { text: root.expiryText() }
                ExpiryBadge { status: root.e.expiryStatus || "none" }
                Item { Layout.fillWidth: true }
                ToolButton {
                    text: "Legg i kalender"
                    onClicked: Vault.openInCalendar(root.entryId)
                }
            }
            CopyField {
                label: "Påminnelse"
                value: (root.e.expiresAt || "") === "" ? ""
                       : root.e.reminderDays + " dager før, " + root.channelText(root.e.reminderChannel).toLowerCase()
                         + (root.e.reminderChannel !== "app" ? " (e-post sendes når Hvelv-serveren er satt opp)" : "")
                copyable: false
            }
            RowLayout {
                visible: (root.e.renewalProcedure || "") !== ""
                Layout.fillWidth: true
                spacing: 8
                Label {
                    text: "Slik fornyer du"
                    color: palette.placeholderText
                    Layout.preferredWidth: 150
                    Layout.alignment: Qt.AlignTop
                    topPadding: 6
                }
                Label {
                    text: root.e.renewalProcedure || ""
                    wrapMode: Text.WordWrap
                    textFormat: Text.PlainText
                    Layout.fillWidth: true
                    topPadding: 6
                }
            }

            // --- Vedlegg ---
            SectionHeader {
                text: "Vedlegg"
                visible: (root.e.attachments || []).length > 0
            }
            Repeater {
                model: root.e.attachments || []
                delegate: RowLayout {
                    id: fileRow
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 8
                    Label { text: fileRow.modelData.name; elide: Text.ElideMiddle; Layout.fillWidth: true }
                    Label {
                        text: Math.max(1, Math.round(fileRow.modelData.size / 1024)) + " kB"
                        color: palette.placeholderText
                    }
                    ToolButton {
                        text: "Lagre som…"
                        onClicked: {
                            saveDialog.attachmentId = fileRow.modelData.id
                            saveDialog.open()
                        }
                    }
                }
            }

            // --- Notater ---
            SectionHeader {
                text: "Notater"
                visible: (root.e.notes || "") !== ""
            }
            Label {
                text: root.e.notes || ""
                visible: text !== ""
                wrapMode: Text.WordWrap
                textFormat: Text.PlainText
                Layout.fillWidth: true
                Layout.topMargin: 6
            }

            Label {
                Layout.topMargin: 24
                font.pixelSize: 11
                color: palette.placeholderText
                text: root.e.updatedAt ? "Sist endret " + root.formatDateTime(root.e.updatedAt) : ""
            }
        }
    }

    FileDialog {
        id: saveDialog
        property string attachmentId
        title: "Lagre vedlegg (ukryptert)"
        fileMode: FileDialog.SaveFile
        onAccepted: Vault.saveAttachment(root.entryId, attachmentId, selectedFile)
    }
}
