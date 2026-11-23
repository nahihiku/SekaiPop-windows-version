import QtQuick
import Common
import Common.widgets

Item {
    id: root

    readonly property var mt: Theme.of(Window.window)

    property string viewMode: "hourly"
    property int hoverHour: -1
    property int hoverDay: -1

    readonly property int pad: 16
    readonly property int innerW: 320

    implicitWidth: innerW + 2 * pad
    implicitHeight: stack.implicitHeight + 2 * pad

    function playOpen() {
        WeatherSource.refreshIfStale()
    }

    Column {
        id: stack
        x: root.pad
        y: root.pad
        width: root.innerW
        spacing: 12

        SekaiPlate {
            id: plate
            width: root.innerW
            height: 112
            colorTop: mt.accentCyan
            colorBottom: mt.accentMint

            WeatherGlyph {
                x: 16
                y: 18
                size: 56
                name: WeatherSource.iconName
                color: mt.colBorder
            }

            StickerText {
                x: 84
                y: 8
                text: WeatherSource.temperatureText.length > 0 ? WeatherSource.temperatureText : "--"
                pixelSize: 40
                fillColor: plate.numberFill
                ringColor: plate.numberRing
            }

            Text {
                x: 86
                y: 74
                width: root.innerW - 134
                text: WeatherSource.cityName + "  -  " + WeatherSource.conditionText
                color: mt.colBorder
                font.family: mt.fontFamily
                font.pixelSize: 12
                font.weight: Font.ExtraBold
                font.styleName: mt.fontStyleBold
                elide: Text.ElideRight
            }

            Pill {
                anchors.right: parent.right
                anchors.rightMargin: 12
                y: 66
                width: 30
                height: 30
                cornerRadius: 10
                ringWidth: 3
                shadowDy: 2
                pressed: refArea.pressed
                fillColor: refArea.containsMouse ? mt.surfaceHover : mt.chipFill

                MaterialIcon {
                    anchors.centerIn: parent
                    name: "refresh"
                    size: 16
                    color: mt.chipInk
                }

                MouseArea {
                    id: refArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: WeatherSource.refresh()
                }
            }
        }

        Row {
            width: root.innerW
            spacing: 8

            Repeater {
                model: [
                    { label: "FEELS LIKE", val: WeatherSource.feelsLikeText, icon: "thermostat" },
                    { label: "HUMIDITY", val: WeatherSource.humidity + "%", icon: "water_drop" },
                    { label: "WIND", val: WeatherSource.windSpeedText, icon: "air" }
                ]

                delegate: Pill {
                    required property var modelData
                    width: (root.innerW - 16) / 3
                    height: 50
                    cornerRadius: 12
                    ringWidth: 3
                    shadowDy: 3
                    fillColor: mt.surfaceHover

                    Column {
                        anchors.centerIn: parent
                        spacing: 2

                        Row {
                            anchors.horizontalCenter: parent.horizontalCenter
                            spacing: 3

                            MaterialIcon {
                                anchors.verticalCenter: parent.verticalCenter
                                name: modelData.icon
                                size: 13
                                color: mt.inkSub
                            }

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.label
                                color: mt.inkSub
                                font.family: mt.fontFamily
                                font.pixelSize: 8
                                font.weight: Font.ExtraBold
                                font.styleName: mt.fontStyleBold
                            }
                        }

                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: modelData.val
                            color: mt.ink
                            font.family: mt.fontFamily
                            font.pixelSize: 12
                            font.weight: Font.ExtraBold
                            font.styleName: mt.fontStyleBold
                        }
                    }
                }
            }
        }

        Item {
            width: root.innerW
            height: 28

            SectionTab {
                anchors.verticalCenter: parent.verticalCenter
                text: root.viewMode === "hourly" ? "Hourly" : "5-Day"
            }

            SegmentSwitch {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: 146
                height: 28
                options: [
                    { id: "hourly", label: "Hourly" },
                    { id: "days", label: "Days" }
                ]
                current: root.viewMode
                onPicked: id => {
                    root.viewMode = id
                    root.hoverHour = -1
                    root.hoverDay = -1
                }
            }
        }

        Row {
            visible: root.viewMode === "hourly"
            width: root.innerW
            spacing: 6

            Repeater {
                model: WeatherSource.hourly.slice(0, 6)

                delegate: Pill {
                    required property var modelData
                    required property int index
                    width: (root.innerW - 30) / 6
                    height: 76
                    cornerRadius: 12
                    ringWidth: 3
                    shadowDy: 3
                    fillColor: hArea.containsMouse ? mt.accentMint : mt.chipFill

                    Column {
                        anchors.centerIn: parent
                        spacing: 3

                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: modelData.label || ""
                            color: hArea.containsMouse ? mt.colBorder : mt.inkSub
                            font.family: mt.fontFamily
                            font.pixelSize: 9
                            font.weight: Font.ExtraBold
                            font.styleName: mt.fontStyleBold
                        }

                        WeatherGlyph {
                            anchors.horizontalCenter: parent.horizontalCenter
                            size: 22
                            name: modelData.icon || "cloud"
                            color: hArea.containsMouse ? mt.colBorder : mt.chipInk
                        }

                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: modelData.temp || "--"
                            color: hArea.containsMouse ? mt.colBorder : mt.chipInk
                            font.family: mt.fontFamily
                            font.pixelSize: 11
                            font.weight: Font.ExtraBold
                            font.styleName: mt.fontStyleBold
                        }
                    }

                    MouseArea {
                        id: hArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onEntered: root.hoverHour = index
                        onExited: if (root.hoverHour === index) root.hoverHour = -1
                    }
                }
            }
        }

        Column {
            visible: root.viewMode === "days"
            width: root.innerW
            spacing: 6

            Repeater {
                model: WeatherSource.forecast.slice(0, 5)

                delegate: Pill {
                    required property var modelData
                    required property int index
                    width: root.innerW
                    height: 36
                    cornerRadius: 10
                    ringWidth: 3
                    shadowDy: 2
                    fillColor: dArea.containsMouse ? mt.accentMint : mt.chipFill

                    Item {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12

                        Text {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.label || ""
                            color: dArea.containsMouse ? mt.colBorder : mt.chipInk
                            font.family: mt.fontFamily
                            font.pixelSize: 11
                            font.weight: Font.ExtraBold
                            font.styleName: mt.fontStyleBold
                        }

                        WeatherGlyph {
                            x: 116
                            anchors.verticalCenter: parent.verticalCenter
                            size: 20
                            name: modelData.icon || "cloud"
                            color: dArea.containsMouse ? mt.colBorder : mt.chipInk
                        }

                        Text {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            text: (modelData.low || "--") + " / " + (modelData.high || "--")
                            color: dArea.containsMouse ? mt.colBorder : mt.chipInk
                            font.family: mt.fontFamily
                            font.pixelSize: 11
                            font.weight: Font.ExtraBold
                            font.styleName: mt.fontStyleBold
                        }
                    }

                    MouseArea {
                        id: dArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onEntered: root.hoverDay = index
                        onExited: if (root.hoverDay === index) root.hoverDay = -1
                    }
                }
            }
        }

        Text {
            width: root.innerW
            text: {
                if (WeatherSource.loading)
                    return "Refreshing Open-Meteo telemetry..."
                if (root.viewMode === "hourly" && root.hoverHour >= 0 && root.hoverHour < WeatherSource.hourly.length) {
                    const h = WeatherSource.hourly[root.hoverHour]
                    return h.label + "  -  " + (h.condition || "") + "  (feels " + (h.feels || "--") + ")"
                }
                if (root.viewMode === "days" && root.hoverDay >= 0 && root.hoverDay < WeatherSource.forecast.length) {
                    const d = WeatherSource.forecast[root.hoverDay]
                    return d.label + "  -  " + (d.condition || "")
                }
                return "Pressure " + WeatherSource.pressureText + "  -  UV " + WeatherSource.uvIndex.toFixed(1)
            }
            color: mt.inkSub
            font.family: mt.fontFamily
            font.pixelSize: 10
            elide: Text.ElideRight
        }
    }
}
