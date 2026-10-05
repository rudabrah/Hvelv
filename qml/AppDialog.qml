import QtQuick
import QtQuick.Controls

// Felles dialog: fast, ugjennomsiktig bakgrunn (Fluent-stilen bruker en shader-effekt som ikke
// tegnes i alle renderere) og norske tekster på standardknappene.
Dialog {
    id: control

    function norwegianButtons() {
        const texts = [[Dialog.Ok, "OK"], [Dialog.Cancel, "Avbryt"], [Dialog.Close, "Lukk"],
                       [Dialog.Save, "Lagre"], [Dialog.Yes, "Ja"], [Dialog.No, "Nei"]]
        for (const [button, text] of texts) {
            const b = standardButton(button)
            if (b)
                b.text = text
        }
    }

    Component.onCompleted: norwegianButtons()
    onStandardButtonsChanged: Qt.callLater(norwegianButtons)

    background: Rectangle {
        color: Application.styleHints.colorScheme === Qt.Light
               ? "white" : Qt.tint(control.palette.window, Qt.rgba(1, 1, 1, 0.05))
        border.color: "#66757575"
        radius: 8
    }
}
