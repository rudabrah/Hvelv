import QtQuick
import QtQuick.Controls
import Hvelv

ApplicationWindow {
    id: window
    width: 1180
    height: 760
    minimumWidth: 860
    minimumHeight: 560
    visible: true
    title: Vault.unlocked ? "Hvelv – " + fileName(Vault.vaultPath) : "Hvelv"

    function fileName(path) {
        return path.split(/[\\/]/).pop()
    }

    Loader {
        anchors.fill: parent
        focus: true
        sourceComponent: Vault.unlocked ? vaultPage : Vault.firstRun ? welcomePage : unlockPage
        // Passordfeltet deaktiveres under opplåsing og mister fokus; gi den nye siden fokus eksplisitt.
        onLoaded: (item as Item).forceActiveFocus()
    }

    QuickSearchWindow {
        id: quickSearch
    }

    Component {
        id: welcomePage
        WelcomePage { objectName: "welcomePage" }
    }

    Component {
        id: unlockPage
        UnlockPage {}
    }

    Component {
        id: vaultPage
        VaultPage {}
    }

    // Kort beskjed nederst i vinduet
    Rectangle {
        id: toast
        property alias text: toastLabel.text

        function show(message) {
            text = message
            opacity = 1
            toastTimer.restart()
        }

        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 24
        width: toastLabel.implicitWidth + 40
        height: toastLabel.implicitHeight + 20
        radius: 8
        color: palette.text
        opacity: 0
        visible: opacity > 0
        z: 100

        Behavior on opacity { NumberAnimation { duration: 180 } }

        Label {
            id: toastLabel
            anchors.centerIn: parent
            color: palette.base
        }

        Timer {
            id: toastTimer
            interval: 3200
            onTriggered: toast.opacity = 0
        }
    }

    Connections {
        target: Vault
        function onToast(message) { toast.show(message) }
    }
}
