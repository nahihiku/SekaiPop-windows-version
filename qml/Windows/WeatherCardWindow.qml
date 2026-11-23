import QtQuick
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
    readonly property bool shown: SettingsService.flag("widgets.weatherCard")
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
        const savedX = Number(SettingsService.get("weathercard.posX"))
        const savedY = Number(SettingsService.get("weathercard.posY"))
        const sx = targetScreen.virtualX
        const sy = targetScreen.virtualY
        if (!isNaN(savedX) && savedX > 0 && !isNaN(savedY) && savedY > 0) {
            win.x = sx + Math.round(savedX)
            win.y = sy + Math.round(savedY)
        } else {
            win.x = sx + Math.round(362 * uiScale)
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
                width: 124
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
                        text: "WEATHER CARD"
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
                            onClicked: SettingsService.set("widgets.weatherCard", false)
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

            // Main Card (312 x 256)
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

                MouseArea {
                    anchors.fill: parent
                    enabled: !win.locked
                    cursorShape: win.locked ? Qt.ArrowCursor : Qt.OpenHandCursor
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    property point pressPos
                    onPressed: mouse => {
                        if (mouse.button === Qt.RightButton && ShellController.editModeOpen) {
                            SettingsService.set("widgets.weatherCard", false)
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
                            SettingsService.set("weathercard.posX", win.x - targetScreen.virtualX)
                            SettingsService.set("weathercard.posY", win.y - targetScreen.virtualY)
                        }
                    }
                }

                // Top Gradient Weather Plate
                Rectangle {
                    x: 12
                    y: 12
                    width: parent.width - 24
                    height: 92
                    radius: 20
                    border.width: 3
                    border.color: mt.colBorder
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: mt.accentMint }
                        GradientStop { position: 1.0; color: mt.accentCyan }
                    }

                    Column {
                        x: 16
                        y: 10
                        spacing: 0

                        Text {
                            text: WeatherSource && WeatherSource.conditionText !== ""
                                ? WeatherSource.conditionText
                                : "Partly Cloudy"
                            color: mt.colBorder
                            font.family: mt.fontFamily
                            font.pixelSize: 12
                            font.weight: Font.ExtraBold
                        }

                        Text {
                            text: WeatherSource && WeatherSource.temperature !== ""
                                ? WeatherSource.temperature
                                : "28°C"
                            color: mt.colBorder
                            font.family: mt.fontFamily
                            font.pixelSize: 34
                            font.weight: Font.Black
                        }

                        Text {
                            text: WeatherSource && WeatherSource.city !== ""
                                ? WeatherSource.city
                                : "Local Weather"
                            color: mt.colBorder
                            font.family: mt.fontFamily
                            font.pixelSize: 12
                            font.weight: Font.ExtraBold
                        }
                    }

                    // Tilted Dark Disc with WeatherGlyph
                    Rectangle {
                        anchors.right: parent.right
                        anchors.rightMargin: 14
                        anchors.verticalCenter: parent.verticalCenter
                        width: 62
                        height: 62
                        radius: 18
                        rotation: 6
                        color: mt.colBorder
                        border.width: 3
                        border.color: mt.pRingWhite

                        WeatherGlyph {
                            anchors.centerIn: parent
                            name: WeatherSource && WeatherSource.glyph ? WeatherSource.glyph : "partly_cloudy_day"
                            size: 36
                        }
                    }
                }

                // Middle 3 Stat Gauges
                Row {
                    x: 16
                    y: 114
                    spacing: 8

                    Repeater {
                        model: [
                            { title: "FEELS", val: WeatherSource && WeatherSource.feelsLike !== "" ? WeatherSource.feelsLike : "30°C", icon: "thermostat" },
                            { title: "HUMID", val: WeatherSource ? (WeatherSource.humidity + "%") : "65%", icon: "water_drop" },
                            { title: "WIND",  val: WeatherSource && WeatherSource.windSpeed !== "" ? WeatherSource.windSpeed : "12 km/h", icon: "air" }
                        ]

                        delegate: Rectangle {
                            required property var modelData
                            width: 88
                            height: 54
                            radius: 14
                            color: mt.pSurfaceHover
                            border.width: 2
                            border.color: mt.pLine

                            Column {
                                anchors.centerIn: parent
                                spacing: 2

                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: modelData.title
                                    color: mt.pInkSub
                                    font.family: mt.fontFamily
                                    font.pixelSize: 10
                                    font.weight: Font.ExtraBold
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

                // Bottom Hourly Forecast Strip
                Row {
                    x: 16
                    y: 178
                    spacing: 6

                    Repeater {
                        model: WeatherSource && WeatherSource.hourlyModel && WeatherSource.hourlyModel.length > 0
                            ? WeatherSource.hourlyModel.slice(0, 5)
                            : [
                                { time: "Now", temp: "28°", glyph: "partly_cloudy_day" },
                                { time: "+3h", temp: "29°", glyph: "clear_day" },
                                { time: "+6h", temp: "27°", glyph: "partly_cloudy_day" },
                                { time: "+9h", temp: "25°", glyph: "cloud" },
                                { time: "+12h", temp: "24°", glyph: "rainy_light" }
                            ]

                        delegate: Rectangle {
                            required property var modelData
                            required property int index
                            width: 51
                            height: 62
                            radius: 13
                            color: index === 0 ? mt.accentMint : mt.pSurfaceHover
                            border.width: 2
                            border.color: index === 0 ? mt.colBorder : mt.pLine

                            Column {
                                anchors.centerIn: parent
                                spacing: 2

                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: modelData.time || ""
                                    color: mt.pInk
                                    font.family: mt.fontFamily
                                    font.pixelSize: 10
                                    font.weight: Font.ExtraBold
                                }

                                WeatherGlyph {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    name: modelData.glyph || modelData.icon || "partly_cloudy_day"
                                    size: 18
                                }

                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: modelData.temp || ""
                                    color: mt.pInk
                                    font.family: mt.fontFamily
                                    font.pixelSize: 11
                                    font.weight: Font.Black
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
