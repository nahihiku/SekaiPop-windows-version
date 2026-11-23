import QtQuick
import QtQuick.Window
import Common
import Sekai.visualizer

Window {
    id: win

    property string monitorName: ""
    readonly property bool shown: SettingsService.flag("widgets.visualizer")
    readonly property real uiScale: ShellScale.forScreen(monitorName)

    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.Tool | Qt.WindowTransparentForInput | Qt.WindowDoesNotAcceptFocus
    visible: shown

    function targetScreen() {
        for (let i = 0; i < Qt.application.screens.length; i++) {
            if (Qt.application.screens[i].name === win.monitorName)
                return Qt.application.screens[i]
        }
        return Qt.application.screens.length > 0 ? Qt.application.screens[0] : win.screen
    }

    function placeWindow() {
        const scr = targetScreen()
        if (!scr) return
        const sx = scr.virtualX !== undefined ? scr.virtualX : 0
        const sy = scr.virtualY !== undefined ? scr.virtualY : 0
        const sw = scr.width || 1920
        const sh = scr.height || 1080
        const h = Math.round(230 * win.uiScale)
        win.x = sx
        win.y = sy + sh - h
        win.width = sw
        win.height = h
        ShellController.registerWidgetWindow(win, true, false)
    }

    onShownChanged: if (shown) Qt.callLater(placeWindow)
    Component.onCompleted: Qt.callLater(placeWindow)

    TideVisualizer {
        anchors.fill: parent
    }
}
