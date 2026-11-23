import QtQuick
import QtQuick.Window
import Common
import Common.widgets
import Sekai.phone
import Sekai.launcher

/**
 * SekaiPop 2.5D App Launcher Phone overlay window for Windows.
 * Opens via Alt+Space, Ctrl+Space, or the Bar's Apps button.
 */
Window {
    id: win

    property var targetScreen: Qt.application.screens.length > 0 ? Qt.application.screens[0] : null
    readonly property var mt: Theme.of(win)
    readonly property bool classicPanel: true
    readonly property real uiScale: ShellScale.forScreen(win.targetScreen)

    property bool shown: false
    property bool focusArmed: false
    readonly property bool isOpen: ShellController.launcherOpen

    screen: targetScreen
    visible: shown
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool

    readonly property real screenW: targetScreen ? targetScreen.width : 1920
    readonly property real screenH: targetScreen ? targetScreen.height : 1080
    readonly property real screenX: targetScreen ? targetScreen.virtualX : 0
    readonly property real screenY: targetScreen ? targetScreen.virtualY : 0

    readonly property real topBarOffset: (BarService.position === "top" && !BarService.autoHide)
        ? BarService.stripSizeFor("top", uiScale) : 0
    readonly property real barOffset: (BarService.position === "bottom" && !BarService.autoHide)
        ? BarService.stripSizeFor("bottom", uiScale) : 0
    readonly property real leftBarOffset: (BarService.position === "left" && !BarService.autoHide)
        ? BarService.stripSizeFor("left", uiScale) : 0
    readonly property real availH: Math.max(360, screenH - topBarOffset - barOffset)

    readonly property real peekFrac: (SettingsService.get("panel.peek") || 76) / 100
    readonly property real maxScale: Math.min(0.84, (availH - 48) / (1214 * peekFrac)) * uiScale
    readonly property real phoneW: Math.ceil(phone.implicitWidth * maxScale)
    readonly property real phoneH: Math.ceil(phone.implicitHeight * maxScale)

    width: phoneW + Math.round(180 * maxScale)
    height: Math.min(availH, Math.ceil(phoneH * peekFrac) + 70)
    x: Math.round(screenX + (screenW - width) / 2)
    y: Math.round(screenY + screenH - height)

    property real lift: 400
    property real tilt: 1
    property string clockText: Qt.formatTime(new Date(), SettingsService.flag("time.use24h") ? "HH:mm" : "hh:mm AP")
    property string dateText: Qt.formatDate(new Date(), "ddd, d MMM")

    Timer {
        interval: 1000
        running: win.visible
        repeat: true
        onTriggered: {
            const now = new Date()
            win.clockText = Qt.formatTime(now, SettingsService.flag("time.use24h") ? "HH:mm" : "hh:mm AP")
            win.dateText = Qt.formatDate(now, "ddd, d MMM")
        }
    }

    Timer {
        id: armFocusTimer
        interval: 350
        onTriggered: if (win.active) win.focusArmed = true
    }

    function openPhone() {
        console.info("[LauncherWindow] openPhone() active=" + win.active + " geom=" + win.x + "," + win.y + " " + win.width + "x" + win.height)
        if (Qt.application.screens.length > 0 && !win.targetScreen)
            win.targetScreen = Qt.application.screens[0]
        exitAnim.stop()
        win.focusArmed = false
        win.shown = true
        win.lift = win.height + 60
        win.tilt = 1
        face.reset()
        ShellController.activateLauncherWindow(win)
        enterAnim.restart()
        armFocusTimer.restart()
        Qt.callLater(() => face.focusSearch())
    }

    function closePhone() {
        console.info("[LauncherWindow] closePhone() shown=" + win.shown + " active=" + win.active + " focusArmed=" + win.focusArmed)
        if (!win.shown)
            return
        armFocusTimer.stop()
        win.focusArmed = false
        enterAnim.stop()
        exitAnim.restart()
    }

    Connections {
        target: ShellController
        function onLauncherOpenChanged() {
            if (ShellController.launcherOpen)
                win.openPhone()
            else
                win.closePhone()
        }
    }

    onActiveChanged: {
        if (active) {
            if (!armFocusTimer.running && win.shown)
                win.focusArmed = true
        } else if (win.focusArmed && ShellController.launcherOpen) {
            ShellController.closeLauncher()
        }
    }

    Component.onCompleted: {
        ShellController.configureLauncherWindow(win)
        if (ShellController.launcherOpen)
            Qt.callLater(win.openPhone)
    }

    ParallelAnimation {
        id: enterAnim
        NumberAnimation {
            target: win
            property: "lift"
            to: 0
            duration: 360
            easing.type: Easing.OutBack
            easing.overshoot: 1.15
        }
        NumberAnimation {
            target: win
            property: "tilt"
            to: 0
            duration: 380
            easing.type: Easing.OutCubic
        }
    }

    SequentialAnimation {
        id: exitAnim
        ParallelAnimation {
            NumberAnimation {
                target: win
                property: "lift"
                to: win.height + 80
                duration: 210
                easing.type: Easing.InCubic
            }
            NumberAnimation {
                target: win
                property: "tilt"
                to: 1
                duration: 210
                easing.type: Easing.InQuad
            }
        }
        ScriptAction {
            script: {
                win.shown = false
                face.reset()
            }
        }
    }

    Item {
        anchors.fill: parent

        PhoneBody {
            id: phone
            x: Math.round(28 * win.maxScale)
            y: win.height - Math.ceil(phone.implicitHeight * win.maxScale * win.peekFrac) + win.lift - 18
            scale: win.maxScale
            transformOrigin: Item.TopLeft

            renderScale: win.maxScale * (win.targetScreen ? (win.targetScreen.devicePixelRatio || 1) : 1)
            windowShown: win.shown
            moving: enterAnim.running || exitAnim.running
            flat: SettingsService.flag("quick.flat")

            rollOffset: win.tilt * 10
            pitchOffset: win.tilt * 8
            yawOffset: win.tilt * -5
            swingRoll: 11
            swingPitch: 9
            swingYaw: 6

            LauncherFace {
                id: face
                anchors.fill: parent
                source: AppSource
                clockText: win.clockText
                dateText: win.dateText
                downloadBps: QuickSource.downloadBps
                uploadBps: QuickSource.uploadBps
                renderScale: phone.renderScale
                visibleBottom: 1214 * win.peekFrac + 60
                screenName: win.targetScreen ? win.targetScreen.name : ""

                onAppActivated: app => {
                    AppSource.launch(app)
                    ShellController.closeLauncher()
                }
                onCloseRequested: ShellController.closeLauncher()
                onDoneRequested: ShellController.closeLauncher()
            }
        }
    }
}
