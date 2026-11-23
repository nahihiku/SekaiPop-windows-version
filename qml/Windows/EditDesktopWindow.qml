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
    readonly property bool classicPanel: true
    readonly property real uiScale: ShellScale.forScreen(targetScreen)
    readonly property bool isOpen: ShellController.editModeOpen

    property string activeTab: "widgets"

    flags: Qt.FramelessWindowHint | Qt.Tool | Qt.WindowStaysOnTopHint
    color: "transparent"

    readonly property real screenW: targetScreen ? targetScreen.width : 1920
    readonly property real screenH: targetScreen ? targetScreen.height : 1080
    readonly property real designW: Math.min(1840, Math.round(screenW / uiScale) - 36)
    readonly property real designH: 224

    width: Math.round((designW + 24) * uiScale)
    height: Math.round((designH + 28) * uiScale)

    property real slideOffset: isOpen ? 0 : (height + 40)
    Behavior on slideOffset {
        NumberAnimation { duration: 280; easing.type: Easing.OutBack; easing.overshoot: 0.9 }
    }

    visible: isOpen || slideOffset < height

    function updatePosition() {
        if (!targetScreen)
            return
        win.x = targetScreen.virtualX + Math.round((targetScreen.width - win.width) / 2)
        win.y = targetScreen.virtualY + targetScreen.height - win.height - Math.round(12 * uiScale)
    }

    Component.onCompleted: updatePosition()
    onTargetScreenChanged: updatePosition()
    onWidthChanged: updatePosition()
    onHeightChanged: updatePosition()

    onIsOpenChanged: {
        if (isOpen) {
            updatePosition()
            win.raise()
            win.requestActivate()
        }
    }

    component HintRow: Item {
        id: hr
        property string keyText: ""
        property string label: ""
        width: 210
        height: 24

        Pill {
            width: 30
            height: 22
            cornerRadius: 7
            borderWidth: 2
            ringWidth: 2
            shadowDx: 0
            shadowDy: 2
            fillColor: mt.chipFill

            Text {
                anchors.centerIn: parent
                text: hr.keyText
                color: mt.chipInk
                font.family: mt.fontFamily
                font.pixelSize: 10
                font.weight: Font.ExtraBold
            }
        }

        Text {
            x: 38
            anchors.verticalCenter: parent.verticalCenter
            text: hr.label
            color: mt.pInk
            font.family: mt.fontFamily
            font.pixelSize: 11
            font.weight: Font.ExtraBold
        }
    }

    component TabKey: Item {
        id: tk
        property string icon: ""
        property string label: ""
        property bool on: false
        signal activated()

        width: 84
        height: 58

        Pill {
            anchors.fill: parent
            cornerRadius: 15
            borderWidth: 3
            ringWidth: tk.on ? 5 : 3
            shadowDx: 0
            shadowDy: 3
            pressed: tkArea.pressed
            fillColor: tkArea.containsMouse && !tk.on ? mt.pSurfaceHover : mt.pSurface

            Rectangle {
                anchors.fill: parent
                radius: parent.radius
                visible: tk.on
                gradient: Gradient {
                    GradientStop { position: 0.0; color: mt.accentMint }
                    GradientStop { position: 1.0; color: mt.accentCyan }
                }
            }

            Column {
                anchors.centerIn: parent
                spacing: 1

                MaterialIcon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    name: tk.icon
                    size: 22
                    weight: 600
                    color: tk.on ? mt.colBorder : mt.pInk
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: tk.label
                    color: tk.on ? mt.colBorder : mt.pInk
                    font.family: mt.fontFamily
                    font.pixelSize: 10
                    font.weight: Font.ExtraBold
                    font.letterSpacing: 0.6
                }
            }
        }

        MouseArea {
            id: tkArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: tk.activated()
        }
    }

    ScaleHost {
        anchors.fill: parent
        factor: win.uiScale

        Item {
            width: win.designW + 24
            height: win.designH + 28
            y: win.slideOffset / win.uiScale

            // Hard Shadow
            Rectangle {
                x: trayCard.x + 5
                y: trayCard.y + 5
                width: trayCard.width
                height: trayCard.height
                radius: 30
                color: mt.colBorder
            }

            // Main Shelf Card
            Rectangle {
                id: trayCard
                x: 10
                y: 12
                width: win.designW
                height: win.designH - 12
                radius: 30
                color: mt.pSurface
                border.width: 4
                border.color: mt.colBorder
                clip: true

                // Subtle Washi Tape at top center of shelf
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: -6
                    width: 110
                    height: 18
                    radius: 4
                    rotation: -2
                    color: mt.accentMint
                    border.width: 2
                    border.color: mt.colBorder
                }

                // ===== LEFT PANEL: Title + Instructions + 6 Category Tabs =====
                Item {
                    id: leftPanel
                    x: 20
                    y: 16
                    width: 488
                    height: parent.height - 32

                    // Title + Hint column (left half of leftPanel)
                    Column {
                        x: 0
                        y: 2
                        width: 208
                        spacing: 6

                        Row {
                            spacing: 8

                            Rectangle {
                                width: 32
                                height: 32
                                radius: 10
                                color: mt.accentCyan
                                border.width: 3
                                border.color: mt.colBorder

                                MaterialIcon {
                                    anchors.centerIn: parent
                                    name: "edit"
                                    size: 18
                                    color: mt.colBorder
                                }
                            }

                            Column {
                                spacing: -2
                                Text {
                                    text: "EDIT DESKTOP"
                                    color: mt.pInk
                                    font.family: mt.fontFamily
                                    font.pixelSize: 16
                                    font.weight: Font.Black
                                }
                                Text {
                                    text: "Pick it, place it, tweak it"
                                    color: mt.pInkSub
                                    font.family: mt.fontFamily
                                    font.pixelSize: 11
                                    font.weight: Font.ExtraBold
                                }
                            }
                        }

                        Rectangle {
                            width: 196
                            height: 2
                            color: mt.pSurfaceDivider
                        }

                        HintRow { keyText: "L"; label: "Left click: add / edit" }
                        HintRow { keyText: "R"; label: "Right click: remove" }
                        HintRow { keyText: "M"; label: "Drag widget: move" }
                    }

                    // 3x2 Category Tabs Grid (right half of leftPanel)
                    Grid {
                        x: 214
                        y: 18
                        columns: 3
                        rowSpacing: 10
                        columnSpacing: 8

                        TabKey {
                            icon: "widgets"
                            label: "WIDGETS"
                            on: win.activeTab === "widgets"
                            onActivated: win.activeTab = "widgets"
                        }
                        TabKey {
                            icon: "toolbar"
                            label: "BAR"
                            on: win.activeTab === "bar"
                            onActivated: win.activeTab = "bar"
                        }
                        TabKey {
                            icon: "dock_to_bottom"
                            label: "DOCK"
                            on: win.activeTab === "dock"
                            onActivated: win.activeTab = "dock"
                        }
                        TabKey {
                            icon: "wallpaper"
                            label: "VIEWS"
                            on: win.activeTab === "views"
                            onActivated: win.activeTab = "views"
                        }
                        TabKey {
                            icon: "palette"
                            label: "THEME"
                            on: win.activeTab === "theme"
                            onActivated: win.activeTab = "theme"
                        }
                        TabKey {
                            icon: "auto_awesome"
                            label: "PRESETS"
                            on: win.activeTab === "presets"
                            onActivated: win.activeTab = "presets"
                        }
                    }
                }

                // Vertical Divider
                Rectangle {
                    x: 516
                    y: 22
                    width: 3
                    height: parent.height - 44
                    radius: 1.5
                    color: mt.pSurfaceDivider
                }

                // ===== CENTER SHELF AREA =====
                Item {
                    id: centerShelf
                    x: 534
                    y: 16
                    width: parent.width - 534 - 248
                    height: parent.height - 32

                    // --- TAB 1: WIDGETS (5 Visual Preview Cards: CLOCK, USER, WEATHER, TO-DO, VISUALIZER) ---
                    Row {
                        visible: win.activeTab === "widgets"
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 14

                        Repeater {
                            model: [
                                { id: "clock",      key: "widgets.clock",      title: "CLOCK",      sub: "4 Designs",   icon: "schedule",     preview: "07:56" },
                                { id: "usercard",   key: "widgets.userCard",   title: "USER",       sub: "Player Card", icon: "person",       preview: "HI!" },
                                { id: "weather",    key: "widgets.weatherCard",title: "WEATHER",    sub: "Live Forecast",icon: "partly_cloudy_day", preview: WeatherSource && WeatherSource.temperature !== "" ? WeatherSource.temperature : "28°C" },
                                { id: "todo",       key: "widgets.todo",       title: "TO-DO",      sub: "Task List",   icon: "checklist",    preview: "Tasks" },
                                { id: "visualizer", key: "widgets.visualizer", title: "VISUALIZER", sub: "Audio Waves", icon: "graphic_eq",   preview: "Waves" }
                            ]

                            delegate: Item {
                                id: wSlot
                                required property var modelData
                                readonly property bool active: SettingsService.flag(modelData.key)
                                width: Math.min(200, Math.floor((centerShelf.width - 56) / 5))
                                height: 174

                                Rectangle {
                                    x: 4
                                    y: 4
                                    width: parent.width
                                    height: parent.height
                                    radius: 22
                                    color: mt.colBorder
                                }

                                Rectangle {
                                    anchors.fill: parent
                                    radius: 22
                                    color: wArea.containsMouse ? mt.pSurfaceHover : mt.pSurface
                                    border.width: wSlot.active ? 4 : 3
                                    border.color: mt.colBorder

                                    // Mini Graphical Preview Box
                                    Rectangle {
                                        x: 12
                                        y: 12
                                        width: parent.width - 24
                                        height: 98
                                        radius: 16
                                        border.width: 2
                                        border.color: mt.pLine
                                        gradient: Gradient {
                                            GradientStop { position: 0.0; color: wSlot.active ? mt.accentMint : mt.pSurfaceHover }
                                            GradientStop { position: 1.0; color: wSlot.active ? mt.accentCyan : mt.pSurfaceDivider }
                                        }

                                        Column {
                                            anchors.centerIn: parent
                                            spacing: 2

                                            MaterialIcon {
                                                anchors.horizontalCenter: parent.horizontalCenter
                                                name: modelData.icon
                                                size: 28
                                                color: mt.colBorder
                                            }

                                            Text {
                                                anchors.horizontalCenter: parent.horizontalCenter
                                                text: modelData.preview
                                                color: mt.colBorder
                                                font.family: mt.fontFamily
                                                font.pixelSize: 22
                                                font.weight: Font.Black
                                            }
                                        }
                                    }

                                    // Title + Subtitle at bottom of card
                                    Column {
                                        x: 14
                                        y: 118
                                        width: parent.width - 28
                                        spacing: 0

                                        Text {
                                            text: modelData.title
                                            color: mt.pInk
                                            font.family: mt.fontFamily
                                            font.pixelSize: 15
                                            font.weight: Font.Black
                                        }

                                        Text {
                                            text: wSlot.active ? "On Desktop" : modelData.sub
                                            color: mt.pInkSub
                                            font.family: mt.fontFamily
                                            font.pixelSize: 11
                                            font.weight: Font.ExtraBold
                                        }
                                    }

                                    // Clock Design Sub-Pill on Clock Card
                                    Rectangle {
                                        visible: modelData.id === "clock" && wSlot.active
                                        anchors.right: parent.right
                                        anchors.bottom: parent.bottom
                                        anchors.margins: 12
                                        width: 58
                                        height: 24
                                        radius: 12
                                        color: mt.accentMint
                                        border.width: 2
                                        border.color: mt.colBorder
                                        z: 5

                                        Text {
                                            anchors.centerIn: parent
                                            readonly property int d: Number(SettingsService.get("widgets.clockDesign")) || 2
                                            text: d === 1 ? "Keys" : (d === 2 ? "Sticker" : (d === 3 ? "Tower" : "Stack"))
                                            color: mt.colBorder
                                            font.family: mt.fontFamily
                                            font.pixelSize: 10
                                            font.weight: Font.ExtraBold
                                        }

                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: {
                                                const cur = Number(SettingsService.get("widgets.clockDesign")) || 2
                                                SettingsService.set("widgets.clockDesign", (cur % 4) + 1)
                                            }
                                        }
                                    }
                                }

                                // Top-Right Active Checkmark Badge
                                Rectangle {
                                    visible: wSlot.active
                                    x: parent.width - 22
                                    y: -6
                                    width: 28
                                    height: 28
                                    radius: 14
                                    color: mt.accentCyan
                                    border.width: 3
                                    border.color: mt.colBorder
                                    z: 10

                                    MaterialIcon {
                                        anchors.centerIn: parent
                                        name: "check"
                                        size: 16
                                        weight: 700
                                        color: mt.colBorder
                                    }
                                }

                                MouseArea {
                                    id: wArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                                    onClicked: mouse => {
                                        if (mouse.button === Qt.RightButton) {
                                            SettingsService.set(modelData.key, false)
                                        } else {
                                            SettingsService.set(modelData.key, !wSlot.active)
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // --- TAB 2: BAR ---
                    Row {
                        visible: win.activeTab === "bar"
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 14

                        Repeater {
                            model: [
                                { label: "Top Bar",      kind: "pos",   val: "top",      icon: "vertical_align_top" },
                                { label: "Right Bar",    kind: "pos",   val: "right",    icon: "align_horizontal_right" },
                                { label: "Bottom Bar",   kind: "pos",   val: "bottom",   icon: "vertical_align_bottom" },
                                { label: "Left Bar",     kind: "pos",   val: "left",     icon: "align_horizontal_left" },
                                { label: "Float Style",  kind: "style", val: "float",    icon: "toolbar" },
                                { label: "Straight",     kind: "style", val: "straight", icon: "horizontal_rule" },
                                { label: "Capsule",      kind: "style", val: "capsule",  icon: "pill" }
                            ]

                            delegate: Pill {
                                required property var modelData
                                readonly property bool active: modelData.kind === "pos"
                                    ? (BarService.position === modelData.val)
                                    : (BarService.barStyle === modelData.val)
                                width: 132
                                height: 146
                                cornerRadius: 22
                                borderWidth: 3
                                ringWidth: active ? 6 : 3
                                shadowDx: 0
                                shadowDy: 4
                                fillColor: active ? mt.accentMint : mt.pSurface

                                Column {
                                    anchors.centerIn: parent
                                    spacing: 8

                                    MaterialIcon {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        name: modelData.icon
                                        size: 32
                                        color: active ? mt.colBorder : mt.pInk
                                    }

                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: modelData.label
                                        color: active ? mt.colBorder : mt.pInk
                                        font.family: mt.fontFamily
                                        font.pixelSize: 14
                                        font.weight: Font.ExtraBold
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        if (modelData.kind === "pos")
                                            BarService.setPosition(modelData.val)
                                        else
                                            BarService.setBarStyle(modelData.val)
                                    }
                                }
                            }
                        }
                    }

                    // --- TAB 3: DOCK ---
                    Row {
                        visible: win.activeTab === "dock"
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 14

                        Repeater {
                            model: [
                                { label: "Show Dock",        key: "dock.visible",            icon: "dock_to_bottom" },
                                { label: "Dock Media Pill",  key: "dock.media",              icon: "music_note" },
                                { label: "Hide Win Taskbar", key: "dock.hideWindowsTaskbar", icon: "visibility_off" },
                                { label: "Magnify Hover",    key: "dock.magnify",            icon: "zoom_in" },
                                { label: "Auto-Hide Dock",   key: "dock.autoHide",           icon: "unfold_less" }
                            ]

                            delegate: Pill {
                                required property var modelData
                                readonly property bool active: SettingsService.flag(modelData.key)
                                width: 184
                                height: 146
                                cornerRadius: 22
                                borderWidth: 3
                                ringWidth: active ? 6 : 3
                                shadowDx: 0
                                shadowDy: 4
                                fillColor: active ? mt.accentCyan : mt.pSurface

                                Column {
                                    anchors.centerIn: parent
                                    spacing: 8

                                    MaterialIcon {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        name: modelData.icon
                                        size: 32
                                        color: active ? mt.colBorder : mt.pInk
                                    }

                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: modelData.label
                                        color: active ? mt.colBorder : mt.pInk
                                        font.family: mt.fontFamily
                                        font.pixelSize: 14
                                        font.weight: Font.ExtraBold
                                    }

                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: active ? "ENABLED" : "OFF"
                                        color: active ? mt.colBorder : mt.pInkSub
                                        font.family: mt.fontFamily
                                        font.pixelSize: 11
                                        font.weight: Font.Black
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: SettingsService.set(modelData.key, !active)
                                }
                            }
                        }
                    }

                    // --- TAB 4: VIEWS (Wallpapers Ferris Wheel, Quick Panel, Apps, Settings, Shader) ---
                    Row {
                        visible: win.activeTab === "views"
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 14

                        Repeater {
                            model: [
                                { id: "wallpapers", label: "Wallpapers Wheel", sub: "1440x760 Carousel", icon: "wallpaper" },
                                { id: "quick",      label: "Quick Panel",      sub: "2.5D Phone + Power", icon: "smartphone" },
                                { id: "apps",       label: "App Launcher",     sub: "Search & Extras",    icon: "apps" },
                                { id: "settings",   label: "Sekai Settings",   sub: "Full Control Hub",   icon: "settings" },
                                { id: "shader",     label: "Shader: " + (WallpaperService ? WallpaperService.transitionStyle : "sekaiRing"), sub: "Click to cycle", icon: "auto_awesome" }
                            ]

                            delegate: Pill {
                                required property var modelData
                                width: 184
                                height: 146
                                cornerRadius: 22
                                borderWidth: 3
                                ringWidth: 4
                                shadowDx: 0
                                shadowDy: 4
                                fillColor: mt.pSurface

                                Column {
                                    anchors.centerIn: parent
                                    spacing: 6

                                    MaterialIcon {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        name: modelData.icon
                                        size: 32
                                        color: mt.pInk
                                    }

                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: modelData.label
                                        color: mt.pInk
                                        font.family: mt.fontFamily
                                        font.pixelSize: 14
                                        font.weight: Font.ExtraBold
                                    }

                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: modelData.sub
                                        color: mt.pInkSub
                                        font.family: mt.fontFamily
                                        font.pixelSize: 11
                                        font.weight: Font.DemiBold
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        if (modelData.id === "wallpapers") {
                                            ShellController.closeEditMode()
                                            ShellController.openWallpaperSelector()
                                        } else if (modelData.id === "quick") {
                                            ShellController.closeEditMode()
                                            ShellController.openQuickPanel()
                                        } else if (modelData.id === "apps") {
                                            ShellController.closeEditMode()
                                            ShellController.openLauncher()
                                        } else if (modelData.id === "settings") {
                                            ShellController.closeEditMode()
                                            ShellController.openSettings()
                                        } else if (modelData.id === "shader" && WallpaperService) {
                                            const styles = WallpaperService.availableStyles
                                            const idx = styles.indexOf(WallpaperService.transitionStyle)
                                            WallpaperService.setTransitionStyle(styles[(idx + 1) % styles.length])
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // --- TAB 5: THEME (All 8 Palettes + Mode Switch) ---
                    Row {
                        visible: win.activeTab === "theme"
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 10

                        Repeater {
                            model: ThemeService ? ThemeService.groups : []

                            delegate: Rectangle {
                                required property var modelData
                                readonly property bool active: ThemeService && ThemeService.group === modelData.id
                                width: 112
                                height: 152
                                radius: 20
                                color: mt.pSurface
                                border.width: active ? 4 : 3
                                border.color: modelData.c ? modelData.c.border : mt.colBorder

                                Column {
                                    anchors.centerIn: parent
                                    spacing: 8

                                    Rectangle {
                                        width: 84
                                        height: 64
                                        radius: 14
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        border.width: 3
                                        border.color: modelData.c ? modelData.c.border : mt.colBorder
                                        gradient: Gradient {
                                            GradientStop { position: 0.0; color: modelData.c ? modelData.c.B : mt.accentMint }
                                            GradientStop { position: 1.0; color: modelData.c ? modelData.c.A : mt.accentCyan }
                                        }

                                        MaterialIcon {
                                            visible: active
                                            anchors.centerIn: parent
                                            name: "check"
                                            size: 24
                                            color: modelData.c ? modelData.c.border : mt.colBorder
                                        }
                                    }

                                    Text {
                                        width: 96
                                        horizontalAlignment: Text.AlignHCenter
                                        wrapMode: Text.WordWrap
                                        maximumLineCount: 2
                                        text: modelData.name || ""
                                        color: mt.pInk
                                        font.family: mt.fontFamily
                                        font.pixelSize: 12
                                        font.weight: Font.ExtraBold
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: ThemeService.setTheme(modelData.id)
                                }
                            }
                        }
                    }

                    // --- TAB 6: PRESETS ---
                    Row {
                        visible: win.activeTab === "presets"
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 14

                        Repeater {
                            model: [
                                { id: "full",    label: "Full Sekai Desktop", sub: "All 5 Widgets Active", icon: "auto_awesome" },
                                { id: "video",   label: "Original Video",     sub: "Clock + User + To-Do + Viz", icon: "movie" },
                                { id: "minimal", label: "Clean Minimal",      sub: "Clock + Visualizer Only", icon: "filter_vintage" },
                                { id: "reset",   label: "Reset Positions",    sub: "Snap widgets to default", icon: "restart_alt" }
                            ]

                            delegate: Pill {
                                required property var modelData
                                width: 215
                                height: 146
                                cornerRadius: 22
                                borderWidth: 3
                                ringWidth: 4
                                shadowDx: 0
                                shadowDy: 4
                                fillColor: mt.pSurface

                                Column {
                                    anchors.centerIn: parent
                                    spacing: 6

                                    MaterialIcon {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        name: modelData.icon
                                        size: 30
                                        color: mt.pInk
                                    }

                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: modelData.label
                                        color: mt.pInk
                                        font.family: mt.fontFamily
                                        font.pixelSize: 14
                                        font.weight: Font.ExtraBold
                                    }

                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: modelData.sub
                                        color: mt.pInkSub
                                        font.family: mt.fontFamily
                                        font.pixelSize: 11
                                        font.weight: Font.DemiBold
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        if (modelData.id === "full") {
                                            SettingsService.set("widgets.clock", true)
                                            SettingsService.set("widgets.userCard", true)
                                            SettingsService.set("widgets.weatherCard", true)
                                            SettingsService.set("widgets.todo", true)
                                            SettingsService.set("widgets.visualizer", true)
                                        } else if (modelData.id === "video") {
                                            SettingsService.set("widgets.clock", true)
                                            SettingsService.set("widgets.userCard", true)
                                            SettingsService.set("widgets.weatherCard", false)
                                            SettingsService.set("widgets.todo", true)
                                            SettingsService.set("widgets.visualizer", true)
                                        } else if (modelData.id === "minimal") {
                                            SettingsService.set("widgets.clock", true)
                                            SettingsService.set("widgets.userCard", false)
                                            SettingsService.set("widgets.weatherCard", false)
                                            SettingsService.set("widgets.todo", false)
                                            SettingsService.set("widgets.visualizer", true)
                                        } else if (modelData.id === "reset") {
                                            SettingsService.set("usercard.posX", 28)
                                            SettingsService.set("usercard.posY", 70)
                                            SettingsService.set("weathercard.posX", 362)
                                            SettingsService.set("weathercard.posY", 70)
                                            SettingsService.set("todo.posX", 28)
                                            SettingsService.set("todo.posY", 362)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // Vertical Divider before Right Panel
                Rectangle {
                    x: parent.width - 238
                    y: 22
                    width: 3
                    height: parent.height - 44
                    radius: 1.5
                    color: mt.pSurfaceDivider
                }

                // ===== RIGHT PANEL: LOCK ALL + DONE =====
                Row {
                    anchors.right: parent.right
                    anchors.rightMargin: 20
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 12

                    // LOCK ALL Button
                    Pill {
                        readonly property bool locked: SettingsService.flag("widgets.locked")
                        width: 98
                        height: 168
                        cornerRadius: 22
                        borderWidth: 3
                        ringWidth: locked ? 6 : 3
                        shadowDx: 0
                        shadowDy: 4
                        fillColor: locked ? mt.accentMint : (lockArea.containsMouse ? mt.pSurfaceHover : mt.pSurface)
                        pressed: lockArea.pressed

                        Column {
                            anchors.centerIn: parent
                            spacing: 8

                            MaterialIcon {
                                anchors.horizontalCenter: parent.horizontalCenter
                                name: parent.parent.locked ? "lock" : "lock_open"
                                size: 30
                                color: parent.parent.locked ? mt.colBorder : mt.pInk
                            }

                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: parent.parent.locked ? "LOCKED" : "LOCK ALL"
                                color: parent.parent.locked ? mt.colBorder : mt.pInk
                                font.family: mt.fontFamily
                                font.pixelSize: 12
                                font.weight: Font.Black
                            }
                        }

                        MouseArea {
                            id: lockArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: SettingsService.set("widgets.locked", !SettingsService.flag("widgets.locked"))
                        }
                    }

                    // DONE Button
                    Pill {
                        width: 98
                        height: 168
                        cornerRadius: 22
                        borderWidth: 4
                        ringWidth: 6
                        shadowDx: 0
                        shadowDy: 4
                        fillColor: mt.accentCyan
                        pressed: doneArea.pressed

                        Rectangle {
                            anchors.fill: parent
                            radius: parent.radius
                            gradient: Gradient {
                                GradientStop { position: 0.0; color: mt.accentMint }
                                GradientStop { position: 1.0; color: mt.accentCyan }
                            }
                        }

                        Column {
                            anchors.centerIn: parent
                            spacing: 8

                            MaterialIcon {
                                anchors.horizontalCenter: parent.horizontalCenter
                                name: "check"
                                size: 32
                                weight: 700
                                color: mt.colBorder
                            }

                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: "DONE"
                                color: mt.colBorder
                                font.family: mt.fontFamily
                                font.pixelSize: 13
                                font.weight: Font.Black
                            }
                        }

                        MouseArea {
                            id: doneArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: ShellController.closeEditMode()
                        }
                    }
                }
            }
        }
    }
}
