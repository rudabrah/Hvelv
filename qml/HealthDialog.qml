pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hvelv

// Passordhelse: svake, gjenbrukte, gamle, lekkede og utløpte + TLS-sertifikater.
AppDialog {
    id: dialog

    signal entrySelected(string id)

    property var report: ({})
    property string breachMessage: ""
    property bool breachOk: true
    property var tlsResults: ({}) // vert -> resultat
    property string kindFilter: ""

    readonly property var kindLabels: ({
        weak: "Svakt", reused: "Gjenbrukt", old: "Over 1 år gammelt", breached: "Lekket", expired: "Utløpt"
    })
    readonly property var kindColors: ({
        weak: "#b45309", reused: "#b45309", old: "#6b7280", breached: "#c42b1c", expired: "#c42b1c"
    })

    function refresh() { report = Vault.healthReport() }

    title: "Passordhelse"
    modal: true
    width: Math.min(820, parent ? parent.width - 60 : 820)
    height: Math.min(680, parent ? parent.height - 60 : 680)
    standardButtons: Dialog.Close

    onOpened: { refresh(); breachMessage = ""; kindFilter = "" }

    Connections {
        target: Vault
        function onHealthChanged() { if (dialog.visible) dialog.refresh() }
        function onBreachCheckFinished(ok, message) { dialog.breachOk = ok; dialog.breachMessage = message }
        function onTlsChecked(target, result) {
            const copy = Object.assign({}, dialog.tlsResults)
            copy[target] = result
            dialog.tlsResults = copy
        }
    }

    ScrollView {
        id: scroll
        anchors.fill: parent
        contentWidth: availableWidth

        ColumnLayout {
            width: scroll.availableWidth
            spacing: 10

            // --- Sammendrag ---
            Flow {
                Layout.fillWidth: true
                spacing: 8
                Repeater {
                    model: [
                        { kind: "", label: "Sjekket", value: dialog.report.checked },
                        { kind: "weak", label: "Svake", value: dialog.report.weak },
                        { kind: "reused", label: "Gjenbrukte", value: dialog.report.reused },
                        { kind: "old", label: "Eldre enn 1 år", value: dialog.report.old },
                        { kind: "breached", label: "Lekket", value: dialog.report.breachesChecked ? dialog.report.breached : "–" },
                        { kind: "expired", label: "Utløpt", value: dialog.report.expired }
                    ]
                    delegate: Button {
                        required property var modelData
                        checkable: true
                        checked: dialog.kindFilter === modelData.kind
                        flat: !checked
                        implicitWidth: 120
                        onClicked: dialog.kindFilter = modelData.kind
                        contentItem: ColumnLayout {
                            spacing: 0
                            Label {
                                text: modelData.value === undefined ? "0" : modelData.value
                                font.pixelSize: 22
                                font.weight: Font.DemiBold
                                color: modelData.kind && modelData.value > 0 ? dialog.kindColors[modelData.kind] : palette.text
                                Layout.alignment: Qt.AlignHCenter
                            }
                            Label {
                                text: modelData.label
                                font.pixelSize: 12
                                color: palette.placeholderText
                                Layout.alignment: Qt.AlignHCenter
                            }
                        }
                    }
                }
            }

            // --- Lekkasjesjekk ---
            SectionHeader { text: "Sjekk mot kjente datalekkasjer" }
            Label {
                text: "Sjekker passordene mot Have I Been Pwned. Bare de 5 første tegnene av en SHA-1-hash "
                      + "sendes (k-anonymitet), aldri selve passordet eller hele hashen. Krever internett."
                wrapMode: Text.WordWrap
                color: palette.placeholderText
                font.pixelSize: 12
                Layout.fillWidth: true
            }
            RowLayout {
                Button {
                    text: dialog.report.breachesChecked ? "Sjekk på nytt" : "Sjekk nå"
                    enabled: !Vault.networkBusy
                    onClicked: { dialog.breachMessage = ""; Vault.checkBreaches() }
                }
                BusyIndicator { running: Vault.networkBusy; visible: running; implicitWidth: 28; implicitHeight: 28 }
                Label {
                    text: dialog.breachMessage
                    visible: text !== ""
                    color: dialog.breachOk ? (dialog.report.breached > 0 ? "#c42b1c" : "#107c10") : "#c42b1c"
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }

            // --- Funn ---
            SectionHeader { text: "Oppføringer som bør ses på" }
            Repeater {
                model: (dialog.report.items || []).filter(i => dialog.kindFilter === "" || i.kinds.includes(dialog.kindFilter))
                delegate: ItemDelegate {
                    id: issueRow
                    required property var modelData
                    Layout.fillWidth: true
                    onClicked: {
                        dialog.entrySelected(modelData.id)
                        dialog.close()
                    }
                    contentItem: RowLayout {
                        spacing: 8
                        ColumnLayout {
                            spacing: 0
                            Layout.fillWidth: true
                            Label { text: issueRow.modelData.title; font.weight: Font.DemiBold; elide: Text.ElideRight; Layout.fillWidth: true }
                            Label {
                                text: [issueRow.modelData.customer,
                                       issueRow.modelData.kinds.includes("reused") ? "brukt i " + issueRow.modelData.reuseCount + " oppføringer" : "",
                                       issueRow.modelData.kinds.includes("breached") ? "sett " + issueRow.modelData.breachCount.toLocaleString(Qt.locale("nb_NO"), "f", 0) + " ganger i lekkasjer" : "",
                                       issueRow.modelData.kinds.includes("weak") ? "≈ " + issueRow.modelData.bits + " bit" : ""]
                                      .filter(s => s).join("  ·  ")
                                color: palette.placeholderText
                                font.pixelSize: 12
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }
                        Repeater {
                            model: issueRow.modelData.kinds
                            delegate: ExpiryBadge {
                                required property string modelData
                                status: "expired"
                                tone: dialog.kindColors[modelData]
                                textOverride: dialog.kindLabels[modelData]
                            }
                        }
                    }
                }
            }
            Label {
                visible: (dialog.report.items || []).length === 0
                text: "Ingen funn – bra!"
                color: "#107c10"
            }

            // --- TLS ---
            SectionHeader { text: "TLS-sertifikater for adressene i hvelvet" }
            RowLayout {
                Label {
                    text: Vault.tlsTargets().length + " unike https-adresser."
                    color: palette.placeholderText
                    Layout.fillWidth: true
                }
                Button {
                    text: "Sjekk sertifikater"
                    enabled: !Vault.networkBusy && Vault.tlsTargets().length > 0
                    onClicked: {
                        dialog.tlsResults = {}
                        for (const t of Vault.tlsTargets())
                            Vault.checkTls(t)
                    }
                }
            }
            Repeater {
                model: Object.keys(dialog.tlsResults).sort()
                delegate: RowLayout {
                    id: tlsRow
                    required property string modelData
                    readonly property var r: dialog.tlsResults[modelData]
                    Layout.fillWidth: true
                    spacing: 8
                    Label { text: tlsRow.modelData; Layout.preferredWidth: 260; elide: Text.ElideRight }
                    Label {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        color: !tlsRow.r.ok ? "#c42b1c" : tlsRow.r.daysLeft < 0 ? "#c42b1c" : tlsRow.r.daysLeft <= 30 ? "#b45309" : palette.text
                        text: !tlsRow.r.ok ? "Feil: " + tlsRow.r.error
                              : tlsRow.r.expiresAt + (tlsRow.r.daysLeft < 0 ? "  ·  utløpt" : "  ·  om " + tlsRow.r.daysLeft + " dager")
                                + "  ·  " + tlsRow.r.issuer + (tlsRow.r.selfSigned ? " (selvsignert)" : "")
                    }
                }
            }
        }
    }
}
