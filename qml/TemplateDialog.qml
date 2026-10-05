pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hvelv

// Velg mal for ny oppføring.
AppDialog {
    id: dialog

    signal templateChosen(int index)

    title: "Ny oppføring"
    modal: true
    width: 520
    height: Math.min(560, parent ? parent.height - 80 : 560)
    standardButtons: Dialog.Cancel

    function choose(i) {
        templateChosen(i)
        close()
    }

    onOpened: list.forceActiveFocus()

    ListView {
        id: list
        anchors.fill: parent
        clip: true
        model: Vault.templates
        currentIndex: 0
        keyNavigationEnabled: true
        ScrollBar.vertical: ScrollBar {}
        Keys.onReturnPressed: dialog.choose(currentIndex)

        delegate: ItemDelegate {
            id: row
            required property int index
            required property var modelData
            width: ListView.view.width
            highlighted: ListView.isCurrentItem
            onClicked: dialog.choose(index)
            contentItem: ColumnLayout {
                spacing: 1
                Label {
                    text: row.modelData.name
                    font.weight: Font.DemiBold
                }
                Label {
                    text: row.modelData.description
                    color: palette.placeholderText
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
        }
    }
}
