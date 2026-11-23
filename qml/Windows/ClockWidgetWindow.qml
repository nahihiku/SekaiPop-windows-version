import QtQuick
import QtQuick.Window
import Common
import Common.widgets

Window {
    id: win

    property string monitorName: ""
    readonly property var mt: Theme.of(win)
    readonly property real uiScale: ShellScale.forScreen(monitorName)
    readonly property bool shown: SettingsService.flag("widgets.clock")
    readonly property bool is24h: SettingsService.flag("widgets.clock24h")
    readonly property string styleName: String(SettingsService.get("widgets.clockStyle") || "sticker")
    readonly property bool locked: SettingsService.flag("widgets.locked")

    property date nowTime: new Date()

    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.Tool | Qt.WindowDoesNotAcceptFocus
    visible: shown

    width: Math.round(320 * uiScale)
    height: Math.round(130 * uiScale)

    Timer {
        interval: 1000
        running: win.shown
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

    function placeWindow() {
        const scr = targetScreen()
        if (!scr) return
        const sx = scr.virtualX !== undefined ? scr.virtualX : 0
        const sy = scr.virtualY !== undefined ? scr.virtualY : 0
        const sw = scr.width || 1920
        const sh = scr.height || 1080
        const px = Number(SettingsService.get("widgets.clockX"))
        const py = Number(SettingsService.get("widgets.clockY"))
        let tx = (isNaN(px) || px < 0) ? sx + sw - win.width - Math.round(52 * win.uiScale) : sx + Math.round(px)
        let ty = (isNaN(py) || py < 0) ? sy + Math.round((sh - win.height) / 2) : sy + Math.round(py)
        win.x = Math.max(sx + 8, Math.min(sx + sw - win.width - 8, tx))
        win.y = Math.max(sy + 8, Math.min(sy + sh - win.height - 8, ty))
        ShellController.registerWidgetWindow(win, false, false)
    }

    readonly property string hhStr: {
        let h = nowTime.getHours()
        if (!is24h)
            h = (h % 12) || 12
        return (h < 10 ? "0" : "") + h
    }
    readonly property string mmStr: {
        const m = nowTime.getMinutes()
        return (m < 10 ? "0" : "") + m
    }
    readonly property string apStr: nowTime.getHours() >= 12 ? "PM" : "AM"

    onShownChanged: if (shown) Qt.callLater(placeWindow)
    Component.onCompleted: Qt.callLater(placeWindow)

    ScaleHost {
        anchors.fill: parent
        factor: win.uiScale

        Item {
            anchors.fill: parent

            // Sticker Style (Image 2 signature clock: chunky outlined numerals + PM pill)
            Row {
                visible: win.styleName === "sticker" || win.styleName === "stack"
                anchors.centerIn: parent
                spacing: 4

                StickerText {
                    text: win.hhStr
                    pixelSize: 72
                    fillColor: "#ffffff"
                    ringColor: mt.colBorder
                }

                StickerText {
                    text: ":"
                    pixelSize: 66
                    fillColor: mt.accentMint
                    ringColor: mt.colBorder
                }

                StickerText {
                    text: win.mmStr
                    pixelSize: 72
                    fillColor: mt.accentCyan
                    ringColor: mt.colBorder
                }

                Pill {
                    visible: !win.is24h
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 8
                    width: apLabel.implicitWidth + 16
                    height: 24
                    cornerRadius: 8
                    ringWidth: 2
                    shadowDy: 2
                    fillColor: mt.chipFill

                    Text {
                        id: apLabel
                        anchors.centerIn: parent
                        text: win.apStr
                        color: mt.chipInk
                        font.family: mt.fontFamily
                        font.pixelSize: 10
                        font.weight: Font.ExtraBold
                        font.styleName: mt.fontStyleBold
                    }
                }
            }

            // Keys / Tower Style (mechanical keycaps)
            Row {
                visible: win.styleName === "keys" || win.styleName === "tower"
                anchors.centerIn: parent
                spacing: 6

                Pill {
                    width: 68; height: 76; cornerRadius: 16; ringWidth: 4; shadowDy: 4
                    fillColor: mt.chipFill
                    Text {
                        anchors.centerIn: parent
                        text: win.hhStr
                        color: mt.chipInk
                        font.family: mt.fontFamily
                        font.pixelSize: 34
                        font.weight: Font.ExtraBold
                        font.styleName: mt.fontStyleBold
                    }
                }

                StickerText {
                    anchors.verticalCenter: parent.verticalCenter
                    text: ":"
                    pixelSize: 42
                    fillColor: mt.accentMint
                    ringColor: mt.colBorder
                }

                Pill {
                    width: 68; height: 76; cornerRadius: 16; ringWidth: 4; shadowDy: 4
                    fillGradient: Gradient {
                        GradientStop { position: 0.0; color: mt.accentMint }
                        GradientStop { position: 1.0; color: mt.accentCyan }
                    }
                    Text {
                        anchors.centerIn: parent
                        text: win.mmStr
                        color: mt.colBorder
                        font.family: mt.fontFamily
                        font.pixelSize: 34
                        font.weight: Font.ExtraBold
                        font.styleName: mt.fontStyleBold
                    }
                }
            }

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                cursorShape: win.locked ? Qt.PointingHandCursor : Qt.SizeAllCursor
                property point startGlobal
                property int startX
                property int startY
                property bool moved: false

                onPressed: mouse => {
                    moved = false
                    startGlobal = mapToGlobal(mouse.x, mouse.y)
                    startX = win.x
                    startY = win.y
                }
                onPositionChanged: mouse => {
                    if (!pressed || win.locked || mouse.buttons !== Qt.LeftButton) return
                    const cur = mapToGlobal(mouse.x, mouse.y)
                    const dx = cur.x - startGlobal.x
                    const dy = cur.y - startGlobal.y
                    if (Math.abs(dx) > 4 || Math.abs(dy) > 4) moved = true
                    if (moved) {
                        win.x = startX + Math.round(dx)
                        win.y = startY + Math.round(dy)
                    }
                }
                onReleased: {
                    if (moved) {
                        const scr = win.targetScreen()
                        if (scr) {
                            SettingsService.set("widgets.clockX", win.x - (scr.virtualX || 0))
                            SettingsService.set("widgets.clockY", win.y - (scr.virtualY || 0))
                        }
                    }
                }
                onDoubleClicked: SettingsService.set("widgets.clock24h", !win.is24h)
                onClicked: mouse => {
                    if (mouse.button === Qt.RightButton) {
                        const styles = ["sticker", "keys", "tower", "stack"]
                        const i = (styles.indexOf(win.styleName) + 1) % styles.length
                        SettingsService.set("widgets.clockStyle", styles[i])
                    }
                }
            }
        }
    }
}
