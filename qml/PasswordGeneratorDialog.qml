import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hvelv

AppDialog {
    id: dialog

    signal usePassword(string password)

    title: "Generer passord"
    modal: true
    width: 460
    standardButtons: Dialog.Ok | Dialog.Cancel

    function regenerate() {
        preview.text = Vault.generatePassword(length.value, lower.checked, upper.checked,
                                              digits.checked, symbols.checked, avoid.checked)
    }

    onOpened: regenerate()
    onAccepted: usePassword(preview.text)

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            TextField {
                id: preview
                readOnly: true
                font.family: "Consolas"
                font.pixelSize: 15
                Layout.fillWidth: true
            }
            Button { text: "Ny"; onClicked: dialog.regenerate() }
        }

        RowLayout {
            Label { text: "Lengde" }
            Slider {
                id: length
                from: 8; to: 64; stepSize: 1; value: 20
                Layout.fillWidth: true
                onMoved: dialog.regenerate()
            }
            Label { text: length.value; Layout.preferredWidth: 24 }
        }

        GridLayout {
            columns: 2
            CheckBox { id: lower; text: "Små bokstaver"; checked: true; onToggled: dialog.regenerate() }
            CheckBox { id: upper; text: "Store bokstaver"; checked: true; onToggled: dialog.regenerate() }
            CheckBox { id: digits; text: "Tall"; checked: true; onToggled: dialog.regenerate() }
            CheckBox { id: symbols; text: "Spesialtegn"; checked: true; onToggled: dialog.regenerate() }
            CheckBox {
                id: avoid
                text: "Unngå tegn som er lette å forveksle (l, I, 1, O, 0)"
                checked: true
                Layout.columnSpan: 2
                onToggled: dialog.regenerate()
            }
        }
    }
}
