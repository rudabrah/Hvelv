import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hvelv

// Én linje i detaljvisningen: etikett, verdi og handlinger.
RowLayout {
    id: root

    property string label
    property string value
    property bool secret: false
    property bool monospace: false
    property string link: ""          // åpnes ved klikk på verdien (http/mailto)
    property bool copyable: true
    property bool sensitiveCopy: secret
    property bool revealed: false
    signal copyRequested()            // overstyr kopiering (f.eks. passord via controller)
    property bool customCopy: false

    visible: value !== ""
    spacing: 8
    Layout.fillWidth: true

    Label {
        text: root.label
        color: palette.placeholderText
        Layout.preferredWidth: 150
        Layout.alignment: Qt.AlignTop
        topPadding: 6
    }

    Label {
        text: root.secret && !root.revealed ? "••••••••••••" : root.value
        font.family: root.monospace || (root.secret && root.revealed) ? "Consolas" : Qt.application.font.family // qmllint disable missing-property
        color: root.link !== "" ? palette.highlight : palette.text
        font.underline: root.link !== "" && linkHover.hovered
        wrapMode: Text.WrapAnywhere
        textFormat: Text.PlainText
        Layout.fillWidth: true
        topPadding: 6
        bottomPadding: 6

        HoverHandler {
            id: linkHover
            enabled: root.link !== ""
            cursorShape: Qt.PointingHandCursor
        }
        TapHandler {
            enabled: root.link !== ""
            onTapped: Qt.openUrlExternally(root.link)
        }
    }

    ToolButton {
        visible: root.secret
        text: root.revealed ? "Skjul" : "Vis"
        onClicked: root.revealed = !root.revealed
    }

    ToolButton {
        visible: root.copyable
        text: "Kopier"
        onClicked: root.customCopy ? root.copyRequested() : Vault.copyToClipboard(root.value, root.sensitiveCopy)
    }
}
