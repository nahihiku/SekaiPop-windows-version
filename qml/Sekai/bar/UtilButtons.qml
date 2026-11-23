import QtQuick
import QtQuick.Layouts
import Common
import Common.widgets

Item {
    id: root

    readonly property var mt: Theme.of(Window.window)

    property bool vertical: false
    property bool compactV: false
    property string barPosition: "top"

    readonly property bool twoColV: root.vertical && root.compactV
    readonly property real keyW: root.twoColV ? 20 : (root.vertical ? 34 : 28)
    readonly property real keyH: root.twoColV ? 20 : 24
    readonly property int keyIcon: root.twoColV ? 16 : 19

    implicitWidth: pill.implicitWidth
    implicitHeight: pill.implicitHeight

    Pill {
        id: pill
        anchors.fill: parent
        cornerRadius: mt.radiusBadge
        fillColor: mt.chipFill
        ringWidth: 6
        implicitWidth: root.vertical
            ? pill.barVWidth
            : grid.implicitWidth + (pill.capsule ? 14 : 20)
        implicitHeight: root.vertical
            ? grid.implicitHeight + (pill.capsule ? 10 : 18) + mt.shadowOffset
            : mt.pillHeight + mt.shadowOffset

        GridLayout {
            id: grid
            anchors.centerIn: parent
            anchors.verticalCenterOffset: root.vertical ? -mt.shadowOffset / 2 : -1
            columns: root.twoColV ? 2 : (root.vertical ? 1 : 100)
            columnSpacing: root.twoColV ? 1 : 0
            rowSpacing: root.twoColV ? 1 : 0

            UtilKey {
                label: "Quick Panel (Alt+N)"
                icon: "tune"
                active: ShellController.phoneOpen && ShellController.phoneScreen === "quick"
                keyWidth: root.keyW; keyHeight: root.keyH; iconSize: root.keyIcon
                tip: true; barPosition: root.barPosition
                onClicked: ShellController.togglePhone("quick")
            }

            UtilKey {
                label: "Apps Launcher (Alt+Space)"
                icon: "apps"
                active: ShellController.launcherOpen
                keyWidth: root.keyW; keyHeight: root.keyH; iconSize: root.keyIcon
                tip: true; barPosition: root.barPosition
                onClicked: {
                    ShellController.closePhone()
                    ShellController.toggleLauncher()
                }
            }

            UtilKey {
                label: "Wallpapers"
                icon: "wallpaper"
                active: ShellController.phoneOpen && ShellController.phoneScreen === "wallpapers"
                keyWidth: root.keyW; keyHeight: root.keyH; iconSize: root.keyIcon
                tip: true; barPosition: root.barPosition
                onClicked: ShellController.togglePhone("wallpapers")
            }

            UtilKey {
                label: "Settings"
                icon: "settings"
                active: ShellController.phoneOpen && ShellController.phoneScreen === "settings"
                keyWidth: root.keyW; keyHeight: root.keyH; iconSize: root.keyIcon
                tip: true; barPosition: root.barPosition
                onClicked: ShellController.togglePhone("settings")
            }

            UtilKey {
                label: "Theme: " + ThemeService.group
                icon: "palette"
                keyWidth: root.keyW; keyHeight: root.keyH; iconSize: root.keyIcon
                tip: true; barPosition: root.barPosition
                onClicked: ThemeService.cycleTheme()
            }

            UtilKey {
                label: "Dark / Light (" + ThemeService.mode + ")"
                icon: ThemeService.mode === "light" ? "light_mode" : (ThemeService.darker ? "bedtime" : "dark_mode")
                keyWidth: root.keyW; keyHeight: root.keyH; iconSize: root.keyIcon
                tip: true; barPosition: root.barPosition
                onClicked: ThemeService.cycleMode()
            }

            UtilKey {
                label: "Bar Edge (" + BarService.position + ")"
                icon: "view_sidebar"
                keyWidth: root.keyW; keyHeight: root.keyH; iconSize: root.keyIcon
                tip: true; barPosition: root.barPosition
                onClicked: BarService.cyclePosition()
            }

            UtilKey {
                visible: !root.twoColV || ShellController.explorerRunning === false
                label: "Restore Explorer"
                icon: "restart_alt"
                active: !ShellController.explorerRunning
                keyWidth: root.keyW; keyHeight: root.keyH; iconSize: root.keyIcon
                tip: true; barPosition: root.barPosition
                onClicked: ShellController.restoreExplorer()
            }
        }
    }
}
