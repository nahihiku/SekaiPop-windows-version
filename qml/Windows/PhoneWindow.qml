import QtQuick
import QtQuick.Window
import Common
import Common.widgets
import "../Common/functions/PhoneMath.js" as PM
import Sekai.phone
import Sekai.quickPanel
import Sekai.settings
import Sekai.wallpaperSelector

Window {
    id: win

    property string monitorName: ""
    property var targetScreen: {
        for (let i = 0; i < Qt.application.screens.length; i++) {
            if (Qt.application.screens[i].name === win.monitorName)
                return Qt.application.screens[i]
        }
        return Qt.application.screens[0]
    }
    screen: targetScreen

    readonly property var mt: Theme.of(win)
    readonly property bool classicPanel: true
    readonly property string screenName: targetScreen ? targetScreen.name : ""
    readonly property real uiScale: ShellScale.forScreen(targetScreen)
    readonly property real dpr: win.Screen.devicePixelRatio || 1

    readonly property bool isOpen: ShellController.phoneOpen
    readonly property string screenKind: ShellController.phoneScreen
    readonly property bool isLandscape: screenKind === "wallpapers"

    readonly property real peekFrac: Math.max(0.55, Math.min(1.0, (Number(SettingsService.get("panel.peek")) || 76) / 100))
    readonly property real availH: targetScreen ? targetScreen.height : 1080
    readonly property real availW: targetScreen ? targetScreen.width : 1920

    // Portrait fit (~0.80 on 1080p, matching original QuickPanel.qml)
    readonly property real portraitFit: Math.max(
        0.55,
        Math.min(0.84, (availH - 72) / Math.max(1, 1214 * peekFrac)) * uiScale
    )

    // Landscape fit (~0.82 on 1080p for the 1440x760 Wallpapers phone)
    readonly property real landscapeFit: Math.max(
        0.50,
        Math.min(0.86, Math.min((availW - 96) / 1520, (availH - 110) / 820)) * uiScale
    )

    readonly property real railRawX: body.faceWidth + 14
    readonly property real railRawY: 276

    flags: Qt.FramelessWindowHint | Qt.Tool | Qt.WindowStaysOnTopHint
    color: "transparent"

    width: isLandscape
        ? Math.round(landscapeBody.implicitWidth * landscapeFit + 48)
        : Math.round((body.implicitWidth + 240) * portraitFit)
    height: isLandscape
        ? Math.round(landscapeBody.implicitHeight * landscapeFit + 48)
        : Math.round(body.implicitHeight * portraitFit * peekFrac + 36)

    property real slideOffset: isOpen ? 0 : (height + 40)
    Behavior on slideOffset {
        NumberAnimation { duration: 280; easing.type: Easing.OutBack; easing.overshoot: 0.85 }
    }

    visible: isOpen || slideOffset < height

    function updatePosition() {
        if (!targetScreen)
            return
        const sx = targetScreen.virtualX
        const sy = targetScreen.virtualY
        const sw = targetScreen.width
        const sh = targetScreen.height
        if (win.isLandscape) {
            win.x = sx + Math.round((sw - win.width) / 2)
            win.y = sy + Math.round((sh - win.height) / 2)
        } else {
            // Centered horizontally on the phone screen center, flush with the very bottom of the monitor
            const phoneCenterOffset = Math.round((body.screenCenterX * win.portraitFit) + 28)
            win.x = sx + Math.round(sw / 2 - phoneCenterOffset)
            win.y = sy + sh - win.height
        }
    }

    Component.onCompleted: updatePosition()
    onTargetScreenChanged: updatePosition()
    onWidthChanged: updatePosition()
    onHeightChanged: updatePosition()
    onIsLandscapeChanged: updatePosition()

    property bool focusArmed: false

    Timer {
        id: armFocusTimer
        interval: 260
        onTriggered: {
            if (win.isOpen && win.active)
                win.focusArmed = true
        }
    }

    onIsOpenChanged: {
        if (isOpen) {
            focusArmed = false
            updatePosition()
            win.raise()
            win.requestActivate()
            armFocusTimer.restart()
        } else {
            focusArmed = false
            armFocusTimer.stop()
            quickFace.powerMenuOpen = false
        }
    }

    onActiveChanged: {
        if (active && isOpen) {
            focusArmed = true
        } else if (!active && isOpen && focusArmed) {
            ShellController.closePhone()
        }
    }

    MouseArea {
        anchors.fill: parent
        onClicked: ShellController.closePhone()
    }

    // ===== PORTRAIT 2.5D PHONE (Quick Panel & Settings) =====
    Item {
        id: portraitStage
        anchors.fill: parent
        visible: !win.isLandscape
        y: win.slideOffset

        PhoneBody {
            id: body
            x: 28
            y: 28
            scale: win.portraitFit
            transformOrigin: Item.TopLeft
            renderScale: win.portraitFit * win.dpr
            windowShown: win.visible && !win.isLandscape
            powerButton: true
            powerOn: quickFace.powerMenuOpen
            flat: false
            swingRoll: -6
            swingPitch: 6
            swingYaw: -10

            onPowerClicked: quickFace.togglePowerMenu()

            MouseArea {
                anchors.fill: parent
                z: -1
            }

            QuickPanelFace {
                id: quickFace
                anchors.fill: parent
                renderScale: body.renderScale
                visible: win.screenKind === "quick"
            }

            SettingsFace {
                id: settingsFace
                anchors.fill: parent
                visible: win.screenKind === "settings"
            }
        }

        // Curved 8-button PowerMenu rail beside the tilted right bezel
        Item {
            id: railTilt
            visible: win.screenKind === "quick"
            x: body.x
            y: body.y
            transformOrigin: Item.TopLeft
            scale: win.portraitFit

            PowerMenu {
                id: powerRail
                transform: Matrix4x4 {
                    matrix: body.matrix(PM.trans(win.railRawX, win.railRawY, 0))
                }
                renderScale: body.renderScale
                open: quickFace.powerMenuOpen
                onDismissRequested: quickFace.powerMenuOpen = false
                onPowerOffRequested: {
                    ShellController.closePhone()
                    QuickSource.powerOff()
                }
                onRebootRequested: {
                    ShellController.closePhone()
                    QuickSource.reboot()
                }
                onLogoutRequested: {
                    ShellController.closePhone()
                    QuickSource.logout()
                }
                onLockRequested: {
                    ShellController.closePhone()
                    QuickSource.lockScreen()
                }
                onSleepRequested: {
                    ShellController.closePhone()
                    QuickSource.sleep()
                }
                onHibernateRequested: {
                    ShellController.closePhone()
                    QuickSource.hibernate()
                }
                onFirmwareRequested: {
                    ShellController.closePhone()
                    ShellController.restoreExplorer()
                }
                onSystemMonitorRequested: {
                    ShellController.closePhone()
                    QuickSource.openTaskManager()
                }
            }
        }
    }

    // ===== LANDSCAPE 1440x760 PHONE (Wallpapers Ferris-Wheel Selector) =====
    Item {
        id: landscapeStage
        anchors.fill: parent
        visible: win.isLandscape
        y: win.slideOffset * 0.4

        PhoneBody {
            id: landscapeBody
            x: Math.round((parent.width - implicitWidth * win.landscapeFit) / 2)
            y: Math.round((parent.height - implicitHeight * win.landscapeFit) / 2)
            landscape: true
            faceWidth: 1440
            faceHeight: 760
            flat: true
            yaw: 0
            pitch: 0
            roll: 0
            powerButton: false
            scale: win.landscapeFit
            transformOrigin: Item.TopLeft
            renderScale: win.landscapeFit * win.dpr
            windowShown: win.visible && win.isLandscape

            MouseArea {
                anchors.fill: parent
                z: -1
            }

            WallpaperFace {
                id: wallpaperFace
                anchors.fill: parent
                renderScale: landscapeBody.renderScale
            }
        }
    }
}
