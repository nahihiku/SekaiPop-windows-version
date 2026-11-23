import QtQuick
import QtQuick.Window
import Common
import Common.widgets
import Sekai.bar

Window {
    id: win

    property string monitorName: ""
    readonly property string popupKind: ShellController.activePopup
    readonly property bool isOpen: popupKind !== "" && monitorName === (Qt.application.screens.length > 0 ? Qt.application.screens[0].name : monitorName)
    readonly property var mt: Theme.of(win)
    readonly property real uiScale: ShellScale.forScreen(monitorName)

    property bool shown: false
    property bool focusArmed: false
    property real popScale: 0.90
    property real popOpacity: 0.0

    readonly property var targetScreen: {
        const screens = Qt.application.screens
        for (let i = 0; i < screens.length; i++) {
            if (win.monitorName && screens[i].name === win.monitorName)
                return screens[i]
        }
        return screens.length > 0 ? screens[0] : win.screen
    }

    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.Tool | Qt.WindowStaysOnTopHint
    visible: win.shown

    readonly property int cardW: contentHost.implicitWidth + 12
    readonly property int cardH: contentHost.implicitHeight + 12

    width: Math.max(320, Math.round((cardW + 24) * uiScale))
    height: Math.max(220, Math.round((cardH + 24) * uiScale))

    function positionWindow() {
        const scr = win.targetScreen || (Qt.application.screens.length > 0 ? Qt.application.screens[0] : win.screen)
        if (!scr) return
        const sx = scr.virtualX !== undefined ? scr.virtualX : 0
        const sy = scr.virtualY !== undefined ? scr.virtualY : 0
        const sw = scr.width || 1920
        const sh = scr.height || 1080
        const pos = BarService.position
        const margin = Math.round(12 * win.uiScale)
        const edgePad = Math.round(60 * win.uiScale)
        const barRes = BarService.stripSizeFor(pos, win.uiScale)

        let tx = sx + Math.round((sw - win.width) / 2)
        let ty = sy + barRes + margin

        if (pos === "top") {
            ty = sy + barRes + 6
            if (win.popupKind === "calendar") tx = sx + Math.round((sw - win.width) / 2)
            else if (win.popupKind === "weather" || win.popupKind === "media") tx = sx + margin + edgePad
            else tx = sx + sw - win.width - margin - edgePad
        } else if (pos === "bottom") {
            const taskbarH = ShellController.windowsTaskbarVisible ? 50 : 0
            ty = sy + sh - barRes - win.height - 6 - taskbarH
            if (win.popupKind === "calendar") tx = sx + Math.round((sw - win.width) / 2)
            else if (win.popupKind === "weather" || win.popupKind === "media") tx = sx + margin + edgePad
            else tx = sx + sw - win.width - margin - edgePad
        } else if (pos === "left") {
            tx = sx + barRes + 8
            ty = sy + (win.popupKind === "calendar" ? Math.round((sh - win.height) / 2)
                 : ((win.popupKind === "resource" || win.popupKind === "battery") ? sh - win.height - margin : margin))
        } else {
            tx = sx + sw - barRes - win.width - 8
            ty = sy + (win.popupKind === "calendar" ? Math.round((sh - win.height) / 2)
                 : ((win.popupKind === "resource" || win.popupKind === "battery") ? sh - win.height - margin : margin))
        }

        win.x = Math.max(sx + 8, Math.min(sx + sw - win.width - 8, tx))
        win.y = Math.max(sy + 8, Math.min(sy + sh - win.height - 8, ty))
    }

    function openPopup() {
        closeAnim.stop()
        focusArmed = false
        popScale = 0.88
        popOpacity = 0.0
        win.shown = true
        positionWindow()
        if (popupLoader.item && typeof popupLoader.item.playOpen === "function")
            popupLoader.item.playOpen()
        Qt.callLater(() => {
            win.positionWindow()
            ShellController.configurePopupWindow(win)
            ShellController.activateLauncherWindow(win)
        })
        openAnim.restart()
        armFocusTimer.restart()
    }

    function closePopup() {
        if (!win.shown)
            return
        armFocusTimer.stop()
        focusArmed = false
        openAnim.stop()
        closeAnim.restart()
    }

    onIsOpenChanged: {
        if (isOpen)
            openPopup()
        else
            closePopup()
    }

    onPopupKindChanged: {
        if (win.isOpen && win.shown) {
            Qt.callLater(positionWindow)
            if (popupLoader.item && typeof popupLoader.item.playOpen === "function")
                popupLoader.item.playOpen()
            openAnim.restart()
        }
    }

    onActiveChanged: {
        if (active) {
            if (!armFocusTimer.running && win.shown)
                win.focusArmed = true
        } else if (win.focusArmed && ShellController.activePopup !== "") {
            ShellController.setActivePopup("")
        }
    }

    Component.onCompleted: {
        ShellController.configurePopupWindow(win)
        if (win.isOpen)
            Qt.callLater(win.openPopup)
    }

    Timer {
        id: armFocusTimer
        interval: 350
        onTriggered: if (win.active) win.focusArmed = true
    }

    ParallelAnimation {
        id: openAnim
        NumberAnimation {
            target: win
            property: "popScale"
            to: 1.0
            duration: 240
            easing.type: Easing.OutBack
            easing.overshoot: 1.3
        }
        NumberAnimation {
            target: win
            property: "popOpacity"
            to: 1.0
            duration: 150
            easing.type: Easing.OutCubic
        }
    }

    SequentialAnimation {
        id: closeAnim
        ParallelAnimation {
            NumberAnimation {
                target: win
                property: "popScale"
                to: 0.90
                duration: 130
                easing.type: Easing.InCubic
            }
            NumberAnimation {
                target: win
                property: "popOpacity"
                to: 0.0
                duration: 120
                easing.type: Easing.InQuad
            }
        }
        ScriptAction {
            script: win.shown = false
        }
    }

    Item {
        id: frameRoot
        anchors.fill: parent
        focus: true
        Keys.onEscapePressed: ShellController.setActivePopup("")

        ScaleHost {
            anchors.fill: parent
            factor: win.uiScale

            Item {
                id: contentHost
                anchors.centerIn: parent
                implicitWidth: popupLoader.item ? popupLoader.item.implicitWidth : 340
                implicitHeight: popupLoader.item ? popupLoader.item.implicitHeight : 280
                width: implicitWidth
                height: implicitHeight
                scale: win.popScale
                opacity: win.popOpacity
                transformOrigin: {
                    switch (BarService.position) {
                    case "top": return Item.Top
                    case "bottom": return Item.Bottom
                    case "left": return Item.Left
                    default: return Item.Right
                    }
                }

                Rectangle {
                    x: mt.shadowOffset
                    y: mt.shadowOffset + 2
                    width: cardBody.width
                    height: cardBody.height
                    radius: 22
                    color: mt.lineShadow
                    antialiasing: true
                }

                Rectangle {
                    id: cardBody
                    anchors.fill: parent
                    radius: 22
                    color: mt.surface
                    border.width: mt.borderWidth
                    border.color: mt.line
                    antialiasing: true

                    Loader {
                        id: popupLoader
                        sourceComponent: {
                            switch (win.popupKind) {
                            case "resource": return resComp
                            case "battery": return batComp
                            case "weather": return weaComp
                            case "calendar": return calComp
                            case "media": return medComp
                            default: return null
                            }
                        }
                        onLoaded: {
                            Qt.callLater(win.positionWindow)
                            if (item && typeof item.playOpen === "function")
                                item.playOpen()
                        }
                    }

                    Pill {
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 8
                        z: 20
                        width: 24
                        height: 24
                        cornerRadius: 8
                        ringWidth: 2
                        shadowDy: 2
                        pressed: closeArea.pressed
                        fillColor: closeArea.containsMouse ? mt.surfaceHover : mt.chipFill

                        MaterialIcon {
                            anchors.centerIn: parent
                            name: "close"
                            size: 14
                            color: mt.chipInk
                        }

                        MouseArea {
                            id: closeArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: ShellController.setActivePopup("")
                        }
                    }
                }
            }
        }
    }

    Component { id: resComp; ResourcePopup {} }
    Component { id: batComp; BatteryPopup {} }
    Component { id: weaComp; WeatherPopup {} }
    Component { id: calComp; CalendarPopup {} }
    Component { id: medComp; MediaPopup {} }
}
