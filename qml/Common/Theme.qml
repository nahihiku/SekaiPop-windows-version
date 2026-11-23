pragma Singleton
import QtQuick

/**
 * Active ThemeSet singleton for SekaiPop Windows Shell.
 */
ThemeSet {
    id: theme

    readonly property bool perMonitor: false

    function of(w) {
        return theme
    }
}
