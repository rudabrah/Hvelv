import QtQuick
import QtQuick.Controls
import Hvelv

// Velg én av de åpne databasene. currentKey er databasens nøkkel ("main" = hoveddatabasen).
ComboBox {
    id: box

    property string currentKey: "main"
    readonly property var openSources: Vault.sources.filter(s => s.open)

    model: openSources
    textRole: "name"
    currentIndex: Math.max(0, openSources.findIndex(s => s.key === currentKey))
    onActivated: (i) => {
        currentKey = openSources[i].key
        currentIndex = Qt.binding(() => Math.max(0, openSources.findIndex(s => s.key === currentKey)))
    }
}
