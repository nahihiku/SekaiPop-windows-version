import QtQuick
import QtQuick.Window
import Common
import Common.widgets
import Sekai.bar

Window {
    id: win

    property string monitorName: ""
    readonly property string barPos: BarService.position
    readonly property bool vertical: BarService.vertical
    readonly property real uiScale: ShellScale.forScreen(monitorName)
    readonly property int stripPx: BarService.stripSizeFor(barPos, uiScale)

    property date nowTime: new Date()

    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.Tool | Qt.WindowStaysOnTopHint
    visible: true

    Timer {
        interval: 1000
        running: true
        repeat: true
        onTriggered: win.nowTime = new Date()
    }

    function targetScreen() {
        for (let i = 0; i < Qt.application.screens.length; i++) {
            if (Qt.application.screens[i].name === win.monitorName)
                return Qt.application.screens[i]
        }
        return Qt.application.screens.length > 0 ? Qt.application.screens[0] : win.screen
    }

    function applyPlacement() {
        const scr = targetScreen()
        if (!scr) return
        const sx = scr.virtualX !== undefined ? scr.virtualX : 0
        const sy = scr.virtualY !== undefined ? scr.virtualY : 0
        const sw = scr.width || 1920
        const sh = scr.height || 1080
        const s = win.stripPx
        if (win.barPos === "bottom") {
            win.x = sx
            win.y = sy + sh - s
            win.width = sw
            win.height = s
        } else if (win.barPos === "left") {
            win.x = sx
            win.y = sy
            win.width = s
            win.height = sh
        } else if (win.barPos === "right") {
            win.x = sx + sw - s
            win.y = sy
            win.width = s
            win.height = sh
        } else {
            win.x = sx
            win.y = sy
            win.width = sw
            win.height = s
        }
        ShellController.registerBarWindow(win, win.barPos, win.stripPx, BarService.autoHide)
    }

    onMonitorNameChanged: Qt.callLater(applyPlacement)
    onBarPosChanged: Qt.callLater(applyPlacement)
    onStripPxChanged: Qt.callLater(applyPlacement)
    onVerticalChanged: {
        barLoader.active = false
        Qt.callLater(() => {
            barLoader.active = true
            applyPlacement()
        })
    }

    Connections {
        target: BarService
        function onChanged() {
            Qt.callLater(win.applyPlacement)
        }
    }

    Connections {
        target: ShellScale
        function onChanged() {
            Qt.callLater(win.applyPlacement)
        }
    }

    Component.onCompleted: Qt.callLater(applyPlacement)

    Loader {
        id: barLoader
        anchors.fill: parent
        active: true
        sourceComponent: Component {
            ScaleHost {
                id: scaleHost
                anchors.fill: parent
                factor: win.uiScale

                readonly property real roomH: scaleHost.height / scaleHost.f
                readonly property real fitRatio: {
                    if (!win.vertical || bar.implicitHeight <= 0 || roomH <= 0)
                        return 1.0
                    return Math.min(1.0, (roomH - 4) / bar.implicitHeight)
                }

                Status_Bar {
                    id: bar
                    width: parent.width
                    height: win.vertical ? Math.round(scaleHost.roomH / scaleHost.fitRatio) : parent.height
                    transformOrigin: Item.Top
                    scale: scaleHost.fitRatio
                    screenName: win.monitorName

                    vertical: win.vertical
                    barPosition: win.barPos
                    barStyle: BarService.styleIndex
                    workspaceCount: BarService.workspaceCount
                    activeWorkspace: WindowTracker.activeWorkspace
                    occupiedWorkspaces: WindowTracker.occupiedWorkspaces
                    layout: BarService.layout

                    clockText: Qt.formatDateTime(win.nowTime, "hh:mm AP")
                    dateText: Qt.formatDateTime(win.nowTime, "ddd, MMM d")

                    windowApp: WindowTracker.activeApp
                    windowTitle: WindowTracker.activeTitle
                    windowIcon: WindowTracker.activeIcon

                    mediaActive: MediaSource.present
                    mediaPlaying: MediaSource.playing
                    mediaTitle: MediaSource.title
                    mediaArtist: MediaSource.artist
                    mediaArtUrl: MediaSource.artUrl
                    mediaVolumeAvailable: true

                    cpuPercent: ResourceSource.cpuPercent
                    memPercent: ResourceSource.memPercent
                    memUsedLabel: ResourceSource.memUsedLabel
                    memTotalLabel: ResourceSource.memTotalLabel

                    batteryAvailable: BatterySource.available
                    batteryPercentage: BatterySource.percentage
                    batteryCharging: BatterySource.isCharging
                    batteryLow: BatterySource.isLow

                    statusNetworkShown: StatusIconsService.networkShown
                    statusNetworkIcon: StatusIconsService.networkIcon
                    statusNetworkState: StatusIconsService.networkState
                    statusBluetoothShown: StatusIconsService.bluetoothShown
                    statusBluetoothIcon: StatusIconsService.bluetoothIcon
                    statusBluetoothState: StatusIconsService.bluetoothState
                    statusSpeakerShown: StatusIconsService.speakerShown
                    statusSpeakerIcon: StatusIconsService.speakerIcon
                    statusSpeakerState: StatusIconsService.speakerState
                    statusDetail: StatusIconsService.detailText

                    onWorkspaceRequested: ws => WindowTracker.switchWorkspace(ws)
                    onMediaToggleRequested: MediaSource.togglePlayPause()
                    onMediaNextRequested: MediaSource.next()
                    onMediaPreviousRequested: MediaSource.previous()
                    onStatusClicked: ShellController.togglePhone("quick")
                    onResourcesClicked: ShellController.setActivePopup(ShellController.activePopup === "resource" ? "" : "resource")
                    onBatteryClicked: ShellController.setActivePopup(ShellController.activePopup === "battery" ? "" : "battery")
                    onDateClicked: ShellController.setActivePopup(ShellController.activePopup === "calendar" ? "" : "calendar")
                    onWeatherClicked: ShellController.setActivePopup(ShellController.activePopup === "weather" ? "" : "weather")
                    onMediaPopupClicked: ShellController.setActivePopup(ShellController.activePopup === "media" ? "" : "media")
                }
            }
        }
    }
}
