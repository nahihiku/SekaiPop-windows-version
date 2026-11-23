import QtQuick
import QtQuick.Effects
import QtQuick.Window
import Common
import Common.widgets

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
    readonly property real uiScale: ShellScale.forScreen(targetScreen)
    readonly property bool shown: SettingsService.flag("widgets.userCard")
    readonly property bool locked: SettingsService.flag("widgets.locked") && !ShellController.editModeOpen

    flags: Qt.FramelessWindowHint | Qt.Tool
    color: "transparent"
    visible: shown

    readonly property real designW: 320
    readonly property real designH: 276
    width: Math.round((designW + 24) * uiScale)
    height: Math.round((designH + 36) * uiScale)

    property bool posInitialized: false

    function initPosition() {
        if (!targetScreen || posInitialized)
            return
        const savedX = Number(SettingsService.get("usercard.posX"))
        const savedY = Number(SettingsService.get("usercard.posY"))
        const sx = targetScreen.virtualX
        const sy = targetScreen.virtualY
        if (!isNaN(savedX) && savedX > 0 && !isNaN(savedY) && savedY > 0) {
            win.x = sx + Math.round(savedX)
            win.y = sy + Math.round(savedY)
        } else {
            win.x = sx + Math.round(28 * uiScale)
            win.y = sy + Math.round(70 * uiScale)
        }
        posInitialized = true
    }

    Component.onCompleted: {
        initPosition()
        ShellController.registerWidgetWindow(win, false, false)
    }

    onVisibleChanged: {
        if (visible) {
            initPosition()
            ShellController.registerWidgetWindow(win, false, false)
        }
    }

    property string greetingText: "GOOD EVENING!"
    property real dayProgress: 0.75
    property string timeNowText: "08:00 PM"

    function updateClockInfo() {
        const now = new Date()
        const h = now.getHours()
        const m = now.getMinutes()
        if (h < 5) win.greetingText = "LATE NIGHT!"
        else if (h < 12) win.greetingText = "GOOD MORNING!"
        else if (h < 17) win.greetingText = "GOOD AFTERNOON!"
        else win.greetingText = "GOOD EVENING!"
        win.dayProgress = Math.max(0.02, Math.min(1.0, (h * 60 + m) / 1440.0))
        win.timeNowText = Qt.formatTime(now, "hh:mm AP")
    }

    Timer {
        interval: 30000
        running: win.visible
        repeat: true
        triggeredOnStart: true
        onTriggered: win.updateClockInfo()
    }

    ScaleHost {
        anchors.fill: parent
        factor: win.uiScale

        Item {
            width: win.designW + 24
            height: win.designH + 36

            // Edit Desktop Mode Floating Tag
            Rectangle {
                visible: ShellController.editModeOpen
                anchors.horizontalCenter: cardBody.horizontalCenter
                y: 0
                width: 112
                height: 26
                radius: 13
                color: mt.accentCyan
                border.width: 3
                border.color: mt.colBorder
                z: 20

                Row {
                    anchors.centerIn: parent
                    spacing: 6

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "USER CARD"
                        color: mt.colBorder
                        font.family: mt.fontFamily
                        font.pixelSize: 11
                        font.weight: Font.ExtraBold
                    }

                    MaterialIcon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "close"
                        size: 14
                        color: mt.colBorder

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: SettingsService.set("widgets.userCard", false)
                        }
                    }
                }
            }

            // Hard drop shadow
            Rectangle {
                x: cardBody.x + 5
                y: cardBody.y + 5
                width: cardBody.width
                height: cardBody.height
                radius: 28
                color: mt.colBorder
            }

            // Main Card (320 x 264)
            Rectangle {
                id: cardBody
                x: 10
                y: 22
                width: 312
                height: 256
                radius: 28
                color: mt.pSurface
                border.width: 4
                border.color: mt.colBorder
                clip: true

                // Drag area
                MouseArea {
                    id: dragArea
                    anchors.fill: parent
                    enabled: !win.locked
                    cursorShape: win.locked ? Qt.ArrowCursor : Qt.OpenHandCursor
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    property point pressPos
                    onPressed: mouse => {
                        if (mouse.button === Qt.RightButton && ShellController.editModeOpen) {
                            SettingsService.set("widgets.userCard", false)
                            return
                        }
                        pressPos = Qt.point(mouse.x, mouse.y)
                    }
                    onPositionChanged: mouse => {
                        if (pressed && (mouse.buttons & Qt.LeftButton)) {
                            win.x += Math.round((mouse.x - pressPos.x) * win.uiScale)
                            win.y += Math.round((mouse.y - pressPos.y) * win.uiScale)
                        }
                    }
                    onReleased: {
                        if (targetScreen) {
                            SettingsService.set("usercard.posX", win.x - targetScreen.virtualX)
                            SettingsService.set("usercard.posY", win.y - targetScreen.virtualY)
                        }
                    }
                }

                // ===== Top Banner Strip =====
                Rectangle {
                    id: bannerStrip
                    x: 12
                    y: 12
                    width: parent.width - 24
                    height: 68
                    radius: 18
                    border.width: 3
                    border.color: mt.colBorder
                    clip: true
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: mt.accentMint }
                        GradientStop { position: 1.0; color: mt.accentCyan }
                    }

                    // Decorative diamonds on banner
                    Repeater {
                        model: [
                            { dx: 204, dy: 16, sz: 12 },
                            { dx: 236, dy: 38, sz: 16 },
                            { dx: 262, dy: 18, sz: 10 }
                        ]
                        delegate: Rectangle {
                            required property var modelData
                            x: modelData.dx
                            y: modelData.dy
                            width: modelData.sz
                            height: modelData.sz
                            radius: 2
                            rotation: 45
                            color: mt.pSurface
                            border.width: 2
                            border.color: mt.colBorder
                        }
                    }

                    // Dark Navy Greeting Pill ("GOOD EVENING!")
                    Rectangle {
                        x: 84
                        y: 14
                        width: greetLabel.implicitWidth + 24
                        height: 28
                        radius: 14
                        color: mt.colBorder
                        border.width: 2
                        border.color: mt.pRingWhite

                        Text {
                            id: greetLabel
                            anchors.centerIn: parent
                            text: win.greetingText
                            color: "#ffffff"
                            font.family: mt.fontFamily
                            font.pixelSize: 12
                            font.weight: Font.Black
                            font.letterSpacing: 0.6
                        }
                    }
                }

                // ===== Tilted Sticker Avatar + Username/@Host =====
                Item {
                    x: 20
                    y: 44
                    width: 64
                    height: 64
                    rotation: -5

                    Rectangle {
                        x: 3
                        y: 3
                        width: parent.width
                        height: parent.height
                        radius: 18
                        color: mt.colBorder
                    }

                    Rectangle {
                        anchors.fill: parent
                        radius: 18
                        color: mt.pSurface
                        border.width: 3
                        border.color: mt.colBorder

                        Image {
                            id: userAvatarImg
                            anchors.fill: parent
                            anchors.margins: 4
                            source: WallpaperService.defaultPath ? ("file:///" + WallpaperService.defaultPath) : ""
                            fillMode: Image.PreserveAspectCrop
                            visible: false
                        }

                        Item {
                            id: userAvatarMask
                            anchors.fill: parent
                            anchors.margins: 4
                            visible: false
                            layer.enabled: true
                            layer.smooth: true
                            Rectangle {
                                anchors.fill: parent
                                radius: 14
                                color: "black"
                            }
                        }

                        MultiEffect {
                            anchors.fill: parent
                            anchors.margins: 4
                            source: userAvatarImg
                            maskEnabled: true
                            maskSource: userAvatarMask
                            visible: userAvatarImg.status === Image.Ready
                        }
                    }
                }

                Column {
                    x: 96
                    y: 84
                    width: 196
                    spacing: 0

                    Text {
                        width: parent.width
                        elide: Text.ElideRight
                        text: QuickSource ? QuickSource.userName : "user"
                        color: mt.pInk
                        font.family: mt.fontFamily
                        font.pixelSize: 18
                        font.weight: Font.Black
                        font.styleName: mt.fontStyleBold
                    }

                    Text {
                        width: parent.width
                        elide: Text.ElideRight
                        text: "@" + (QuickSource ? QuickSource.hostName : "windows")
                        color: mt.pInkSub
                        font.family: mt.fontFamily
                        font.pixelSize: 12
                        font.weight: Font.ExtraBold
                    }
                }

                // ===== Three Keycap Pills (UPTIME, SHELL, WM) =====
                Row {
                    x: 16
                    y: 136
                    spacing: 8

                    Repeater {
                        model: [
                            { title: "UPTIME", val: QuickSource ? QuickSource.uptimeText : "1h 20m", icon: "schedule" },
                            { title: "SHELL",  val: "SekaiPop", icon: "auto_awesome" },
                            { title: "WM",     val: "Win32 11", icon: "desktop_windows" }
                        ]

                        delegate: Pill {
                            required property var modelData
                            width: 88
                            height: 58
                            cornerRadius: 16
                            borderWidth: 3
                            ringWidth: 3
                            shadowDx: 0
                            shadowDy: 3
                            fillColor: mt.pSurfaceHover

                            Column {
                                anchors.centerIn: parent
                                spacing: 2

                                Row {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    spacing: 4

                                    MaterialIcon {
                                        anchors.verticalCenter: parent.verticalCenter
                                        name: modelData.icon
                                        size: 13
                                        color: mt.pInkSub
                                    }

                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: modelData.title
                                        color: mt.pInkSub
                                        font.family: mt.fontFamily
                                        font.pixelSize: 10
                                        font.weight: Font.ExtraBold
                                    }
                                }

                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: modelData.val
                                    color: mt.pInk
                                    font.family: mt.fontFamily
                                    font.pixelSize: 13
                                    font.weight: Font.Black
                                }
                            }
                        }
                    }
                }

                // ===== Day Progress Lane at Bottom =====
                Rectangle {
                    x: 16
                    y: 206
                    width: parent.width - 32
                    height: 36
                    radius: 14
                    color: mt.pSurfaceHover
                    border.width: 2
                    border.color: mt.pLine

                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 10
                        spacing: 10

                        MaterialIcon {
                            anchors.verticalCenter: parent.verticalCenter
                            name: "sunny"
                            size: 16
                            color: mt.pInk
                        }

                        Rectangle {
                            width: 168
                            height: 12
                            radius: 6
                            anchors.verticalCenter: parent.verticalCenter
                            color: mt.pSurfaceDivider
                            border.width: 2
                            border.color: mt.pLine

                            Rectangle {
                                width: Math.max(12, parent.width * win.dayProgress)
                                height: parent.height
                                radius: 6
                                border.width: 2
                                border.color: mt.pLine
                                gradient: Gradient {
                                    orientation: Gradient.Horizontal
                                    GradientStop { position: 0.0; color: mt.accentMint }
                                    GradientStop { position: 1.0; color: mt.accentCyan }
                                }
                            }
                        }

                        Rectangle {
                            width: 48
                            height: 22
                            radius: 11
                            anchors.verticalCenter: parent.verticalCenter
                            color: mt.colBorder

                            Text {
                                anchors.centerIn: parent
                                text: Math.round(win.dayProgress * 100) + "%"
                                color: "#ffffff"
                                font.family: mt.fontFamily
                                font.pixelSize: 11
                                font.weight: Font.ExtraBold
                            }
                        }
                    }
                }
            }
        }
    }
}
