import QtQuick
import QtQuick.Window
import Common
import Common.widgets

/**
 * SekaiPop-styled desktop context menu opened on right-clicking empty desktop wallpaper.
 * Provides quick access to MVP testing controls and emergency Explorer recovery.
 */
Window {
    id: menuWin

    property string targetScreenName: ""
    property bool open: false

    readonly property var mt: Theme.of(menuWin)

    visible: open
    color: "transparent"
    flags: Qt.Popup | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    width: card.implicitWidth + 12
    height: card.implicitHeight + 12

    onActiveChanged: {
        if (!active && open)
            open = false
    }

    function openAt(globalX, globalY, screenName, screenObj) {
        targetScreenName = screenName || ""
        if (screenObj)
            menuWin.screen = screenObj
        const sx = screenObj ? screenObj.virtualX : 0
        const sy = screenObj ? screenObj.virtualY : 0
        const sw = screenObj ? screenObj.width : 1920
        const sh = screenObj ? screenObj.height : 1080
        menuWin.x = Math.max(sx + 8, Math.min(globalX, sx + sw - menuWin.width - 8))
        menuWin.y = Math.max(sy + 8, Math.min(globalY, sy + sh - menuWin.height - 8))
        menuWin.open = true
        menuWin.requestActivate()
    }

    readonly property var actionsList: [
        { label: "Edit Desktop (Widgets & Theme)", icon: "widgets", fn: () => ShellController.toggleEditMode() },
        { label: "Quick Panel (Alt+N)", icon: "tune", fn: () => ShellController.togglePhone("quick") },
        { label: "Apps Launcher (Alt+Space)", icon: "apps", fn: () => ShellController.openLauncher() },
        { label: "Wallpapers Wheel (1440x760)", icon: "wallpaper", fn: () => ShellController.togglePhone("wallpapers") },
        { label: "Settings Hub", icon: "settings", fn: () => ShellController.togglePhone("settings") },
        { label: "Next Wallpaper", icon: "shuffle", fn: () => WallpaperService.nextInFolder(menuWin.targetScreenName) },
        { label: "Theme: " + ThemeService.group + " (" + ThemeService.mode + ")", icon: "palette", fn: () => ThemeService.cycleTheme() },
        { label: "Toggle Dock (" + (SettingsService.flag("dock.visible") ? "On" : "Off") + ")", icon: "dock_to_bottom", fn: () => SettingsService.set("dock.visible", !SettingsService.flag("dock.visible")) },
        { label: SettingsService.flag("widgets.locked") ? "Unlock Widgets" : "Lock Widgets", icon: SettingsService.flag("widgets.locked") ? "lock_open" : "lock", fn: () => SettingsService.set("widgets.locked", !SettingsService.flag("widgets.locked")) },
        { label: "Bar Edge: " + BarService.position, icon: "view_sidebar", fn: () => BarService.cyclePosition() },
        { label: "Bar Style: " + BarService.style, icon: "style", fn: () => BarService.cycleStyle() },
        { label: SettingsService.flag("dock.hideWindowsTaskbar") ? "Show Windows Taskbar" : "Hide Windows Taskbar", icon: SettingsService.flag("dock.hideWindowsTaskbar") ? "visibility" : "visibility_off", fn: () => SettingsService.set("dock.hideWindowsTaskbar", !SettingsService.flag("dock.hideWindowsTaskbar")) },
        { label: "Restore Windows Explorer", icon: "restart_alt", warn: true, fn: () => ShellController.restoreExplorer() },
        { label: "Exit SekaiPop Shell", icon: "logout", warn: true, fn: () => ShellController.exitShell() }
    ]

    Pill {
        id: card
        anchors.centerIn: parent
        cornerRadius: 16
        ringWidth: 6
        fillColor: mt.surface
        implicitWidth: 280
        implicitHeight: col.implicitHeight + 24 + mt.shadowOffset

        Column {
            id: col
            anchors.centerIn: parent
            width: parent.width - 18
            spacing: 4

            Repeater {
                model: menuWin.actionsList

                delegate: Item {
                    id: row
                    required property var modelData
                    width: col.width
                    height: 34

                    Rectangle {
                        anchors.fill: parent
                        radius: 10
                        color: rowArea.containsMouse
                            ? (modelData.warn ? Qt.rgba(0.92, 0.42, 0.36, 0.18) : mt.surfaceTint)
                            : "transparent"
                        antialiasing: true
                    }

                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 10

                        MaterialIcon {
                            anchors.verticalCenter: parent.verticalCenter
                            name: modelData.icon
                            size: 18
                            color: modelData.warn ? "#d9534f" : mt.ink
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.label
                            color: modelData.warn ? "#d9534f" : mt.ink
                            font.family: mt.fontFamily
                            font.pixelSize: 13
                            font.weight: Font.Bold
                            elide: Text.ElideRight
                            width: row.width - 48
                        }
                    }

                    MouseArea {
                        id: rowArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            menuWin.open = false
                            modelData.fn()
                        }
                    }
                }
            }
        }
    }
}
