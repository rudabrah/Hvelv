import QtQuick
import QtQuick.Controls

Rectangle {
    id: badge

    property string status: "none" // none | ok | soon | expired
    property string textOverride: ""

    property color tone: status === "expired" ? "#c42b1c"
                                : status === "soon" ? "#b45309"
                                : "#107c10"

    visible: status !== "none"
    radius: 9
    color: Qt.alpha(tone, 0.13)
    implicitWidth: badgeLabel.implicitWidth + 14
    implicitHeight: badgeLabel.implicitHeight + 4

    Label {
        id: badgeLabel
        anchors.centerIn: parent
        font.pixelSize: 11
        font.weight: Font.DemiBold
        color: badge.tone
        text: badge.textOverride !== "" ? badge.textOverride
              : badge.status === "expired" ? "Utløpt"
              : badge.status === "soon" ? "Utløper snart"
              : "Gyldig"
    }
}
