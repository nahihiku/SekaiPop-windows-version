import QtQuick
import Common
import Common.widgets
import Sekai.quickPanel

Item {
    id: face

    readonly property var mt: Theme.of(Window.window)

    signal backRequested()

    width: 654
    height: 1214

    Flickable {
        id: flick
        anchors.fill: parent
        anchors.leftMargin: 44
        anchors.rightMargin: 44
        anchors.topMargin: 72
        anchors.bottomMargin: 44
        contentWidth: width
        contentHeight: mainCol.implicitHeight + 24
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: mainCol
            width: flick.width
            spacing: 16

            // Header
            Item {
                width: parent.width
                height: 54

                Row {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 12

                    SettingRoundButton {
                        icon: "arrow_back"
                        size: 46
                        onClicked: face.backRequested()
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "SekaiPop Settings"
                        color: mt.pInk
                        font.family: mt.fontFamily
                        font.pixelSize: 24
                        font.weight: Font.ExtraBold
                        font.styleName: mt.fontStyleBold
                    }
                }

                SettingRoundButton {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    icon: "restart_alt"
                    size: 44
                    onClicked: SettingsService.reset()
                }
            }

            // Section: Unit Theme & Mode
            Rectangle {
                width: parent.width
                implicitHeight: themeCol.implicitHeight + 28
                radius: 22
                color: mt.pSurfaceHover
                border.width: 3
                border.color: mt.pLine
                antialiasing: true

                Column {
                    id: themeCol
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 10

                    Text {
                        text: "THEME & COLOR MODE"
                        color: mt.pInkSub
                        font.family: mt.fontFamily
                        font.pixelSize: 11
                        font.weight: Font.ExtraBold
                        font.styleName: mt.fontStyleBold
                        font.letterSpacing: 1.0
                    }

                    Flow {
                        width: parent.width
                        spacing: 8

                        Repeater {
                            model: ThemeService.groups

                            delegate: Pill {
                                required property var modelData
                                readonly property bool selected: ThemeService.group === modelData.id
                                width: Math.max(88, gLabel.implicitWidth + 24)
                                height: 34
                                cornerRadius: 11
                                ringWidth: 2
                                shadowDy: 2
                                pressed: gArea.pressed
                                fillColor: selected ? Qt.color(modelData.c.A) : mt.pSurface

                                Text {
                                    id: gLabel
                                    anchors.centerIn: parent
                                    text: modelData.name
                                    color: selected ? mt.colBorder : mt.pInk
                                    font.family: mt.fontFamily
                                    font.pixelSize: 12
                                    font.weight: Font.ExtraBold
                                    font.styleName: mt.fontStyleBold
                                }

                                MouseArea {
                                    id: gArea
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: ThemeService.setTheme(modelData.id)
                                }
                            }
                        }
                    }

                    SegmentSwitch {
                        width: parent.width
                        height: 34
                        options: [
                            { id: "light", label: "Light" },
                            { id: "dark", label: "Dark" },
                            { id: "darker", label: "Darker" }
                        ]
                        current: ThemeService.mode
                        onPicked: id => ThemeService.setMode(id)
                    }
                }
            }

            // Section: Status Bar
            Rectangle {
                width: parent.width
                implicitHeight: barCol.implicitHeight + 28
                radius: 22
                color: mt.pSurfaceHover
                border.width: 3
                border.color: mt.pLine
                antialiasing: true

                Column {
                    id: barCol
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 10

                    Text {
                        text: "STATUS BAR"
                        color: mt.pInkSub
                        font.family: mt.fontFamily
                        font.pixelSize: 11
                        font.weight: Font.ExtraBold
                        font.styleName: mt.fontStyleBold
                        font.letterSpacing: 1.0
                    }

                    SegmentSwitch {
                        width: parent.width
                        height: 34
                        options: [
                            { id: "top", label: "Top" },
                            { id: "right", label: "Right" },
                            { id: "bottom", label: "Bottom" },
                            { id: "left", label: "Left" }
                        ]
                        current: BarService.position
                        onPicked: id => BarService.setPosition(id)
                    }

                    SegmentSwitch {
                        width: parent.width
                        height: 34
                        options: [
                            { id: "float", label: "Float" },
                            { id: "straight", label: "Straight" },
                            { id: "transparent", label: "Clear" },
                            { id: "capsule", label: "Capsule" }
                        ]
                        current: BarService.style
                        onPicked: id => BarService.setStyle(id)
                    }
                }
            }

            // Section: Bottom Dock
            Rectangle {
                width: parent.width
                implicitHeight: dockCol.implicitHeight + 28
                radius: 22
                color: mt.pSurfaceHover
                border.width: 3
                border.color: mt.pLine
                antialiasing: true

                Column {
                    id: dockCol
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 10

                    Text {
                        text: "DOCK & LAUNCHER"
                        color: mt.pInkSub
                        font.family: mt.fontFamily
                        font.pixelSize: 11
                        font.weight: Font.ExtraBold
                        font.styleName: mt.fontStyleBold
                        font.letterSpacing: 1.0
                    }

                    Row {
                        width: parent.width
                        spacing: 10

                        QuickToggle {
                            kind: "wide"
                            wideWidth: (parent.width - 10) / 2 - 4
                            icon: "dock_to_bottom"
                            label: "Dock"
                            subtitle: SettingsService.flag("dock.visible") ? "Shown" : "Hidden"
                            on: SettingsService.flag("dock.visible")
                            onClicked: SettingsService.set("dock.visible", !SettingsService.flag("dock.visible"))
                        }

                        QuickToggle {
                            kind: "wide"
                            wideWidth: (parent.width - 10) / 2 - 4
                            icon: "music_note"
                            label: "Dock Media"
                            subtitle: SettingsService.flag("dock.media") ? "Shown" : "Hidden"
                            on: SettingsService.flag("dock.media")
                            onClicked: SettingsService.set("dock.media", !SettingsService.flag("dock.media"))
                        }
                    }

                    SegmentSwitch {
                        width: parent.width
                        height: 34
                        options: [
                            { id: "float", label: "Float" },
                            { id: "transparent", label: "Clear" },
                            { id: "capsule", label: "Capsule" }
                        ]
                        current: String(SettingsService.get("dock.style") || "float")
                        onPicked: id => SettingsService.set("dock.style", id)
                    }

                    QuickToggle {
                        kind: "wide"
                        wideWidth: parent.width - 4
                        icon: SettingsService.flag("dock.hideWindowsTaskbar") ? "visibility_off" : "visibility"
                        label: "Hide Windows Taskbar"
                        subtitle: SettingsService.flag("dock.hideWindowsTaskbar")
                            ? "Hidden (SekaiPop Dock active)"
                            : "Shown (coexist mode)"
                        on: SettingsService.flag("dock.hideWindowsTaskbar")
                        onClicked: SettingsService.set("dock.hideWindowsTaskbar", !SettingsService.flag("dock.hideWindowsTaskbar"))
                    }
                }
            }

            // Section: Desktop Widgets & Visualizer
            Rectangle {
                width: parent.width
                implicitHeight: widCol.implicitHeight + 28
                radius: 22
                color: mt.pSurfaceHover
                border.width: 3
                border.color: mt.pLine
                antialiasing: true

                Column {
                    id: widCol
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 10

                    Text {
                        text: "DESKTOP WIDGETS & VISUALIZER"
                        color: mt.pInkSub
                        font.family: mt.fontFamily
                        font.pixelSize: 11
                        font.weight: Font.ExtraBold
                        font.styleName: mt.fontStyleBold
                        font.letterSpacing: 1.0
                    }

                    Row {
                        width: parent.width
                        spacing: 10

                        QuickToggle {
                            kind: "wide"
                            wideWidth: (parent.width - 10) / 2 - 4
                            icon: "checklist"
                            label: "To-Do Card"
                            subtitle: SettingsService.flag("widgets.todo") ? "Visible" : "Hidden"
                            on: SettingsService.flag("widgets.todo")
                            onClicked: SettingsService.set("widgets.todo", !SettingsService.flag("widgets.todo"))
                        }

                        QuickToggle {
                            kind: "wide"
                            wideWidth: (parent.width - 10) / 2 - 4
                            icon: "schedule"
                            label: "Clock"
                            subtitle: SettingsService.flag("widgets.clock") ? "Visible" : "Hidden"
                            on: SettingsService.flag("widgets.clock")
                            onClicked: SettingsService.set("widgets.clock", !SettingsService.flag("widgets.clock"))
                        }
                    }

                    Row {
                        width: parent.width
                        spacing: 10

                        QuickToggle {
                            kind: "wide"
                            wideWidth: (parent.width - 10) / 2 - 4
                            icon: "person"
                            label: "User Card"
                            subtitle: SettingsService.flag("widgets.userCard") ? "Visible" : "Hidden"
                            on: SettingsService.flag("widgets.userCard")
                            onClicked: SettingsService.set("widgets.userCard", !SettingsService.flag("widgets.userCard"))
                        }

                        QuickToggle {
                            kind: "wide"
                            wideWidth: (parent.width - 10) / 2 - 4
                            icon: "partly_cloudy_day"
                            label: "Weather Card"
                            subtitle: SettingsService.flag("widgets.weatherCard") ? "Visible" : "Hidden"
                            on: SettingsService.flag("widgets.weatherCard")
                            onClicked: SettingsService.set("widgets.weatherCard", !SettingsService.flag("widgets.weatherCard"))
                        }
                    }

                    Row {
                        width: parent.width
                        spacing: 10

                        QuickToggle {
                            kind: "wide"
                            wideWidth: (parent.width - 10) / 2 - 4
                            icon: "graphic_eq"
                            label: "Wave Visualizer"
                            subtitle: SettingsService.flag("widgets.visualizer") ? "Active" : "Off"
                            on: SettingsService.flag("widgets.visualizer")
                            onClicked: SettingsService.set("widgets.visualizer", !SettingsService.flag("widgets.visualizer"))
                        }

                        QuickToggle {
                            kind: "wide"
                            wideWidth: (parent.width - 10) / 2 - 4
                            icon: "widgets"
                            label: "Edit Desktop"
                            subtitle: "Open Shelf Drawer"
                            hoverLights: true
                            onClicked: ShellController.openEditMode()
                        }
                    }

                    SegmentSwitch {
                        width: parent.width
                        height: 34
                        options: [
                            { id: "sticker", label: "Sticker" },
                            { id: "keys", label: "Keys" },
                            { id: "tower", label: "Tower" },
                            { id: "stack", label: "Stack" }
                        ]
                        current: String(SettingsService.get("widgets.clockStyle") || "sticker")
                        onPicked: id => SettingsService.set("widgets.clockStyle", id)
                    }
                }
            }

            // Section: Windows Safety & Explorer Recovery
            Rectangle {
                width: parent.width
                implicitHeight: winCol.implicitHeight + 28
                radius: 22
                color: mt.pSurfaceHover
                border.width: 3
                border.color: mt.pLine
                antialiasing: true

                Column {
                    id: winCol
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 10

                    Text {
                        text: "WINDOWS DESKTOP & RECOVERY"
                        color: mt.pInkSub
                        font.family: mt.fontFamily
                        font.pixelSize: 11
                        font.weight: Font.ExtraBold
                        font.styleName: mt.fontStyleBold
                        font.letterSpacing: 1.0
                    }

                    Row {
                        width: parent.width
                        spacing: 10

                        QuickToggle {
                            kind: "wide"
                            wideWidth: (parent.width - 10) / 2 - 4
                            icon: "restart_alt"
                            label: "Explorer.exe"
                            subtitle: ShellController.explorerRunning ? "Running" : "Stopped - Restore"
                            on: !ShellController.explorerRunning
                            onClicked: ShellController.restoreExplorer()
                        }

                        QuickToggle {
                            kind: "wide"
                            wideWidth: (parent.width - 10) / 2 - 4
                            icon: "power_settings_new"
                            label: "Exit Shell"
                            subtitle: "Ctrl+Alt+Shift+Q"
                            onClicked: ShellController.exitShell()
                        }
                    }
                }
            }
        }
    }
}
