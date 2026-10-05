import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    property alias text: label.text

    spacing: 4
    Layout.fillWidth: true
    Layout.topMargin: 14

    Label {
        id: label
        font.pixelSize: 13
        font.weight: Font.DemiBold
        color: palette.highlight
    }
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: 1
        color: palette.mid
        opacity: 0.5
    }
}
