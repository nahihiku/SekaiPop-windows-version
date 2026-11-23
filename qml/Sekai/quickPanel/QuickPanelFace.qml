import QtQuick
import QtQuick.Effects
import Common
import Common.widgets
import "../../Common/functions/QuickTiles.js" as QT

Item {
    id: face

    readonly property var mt: Theme.of(Window.window)
    readonly property bool classicPanel: true

    property real renderScale: 1
    property bool powerMenuOpen: ShellController ? ShellController.quickPowerOpen : false
    property bool editing: ShellController ? ShellController.quickEditOpen : false

    Connections {
        target: ShellController
        function onPhoneChanged() {
            if (ShellController.quickPowerOpen !== face.powerMenuOpen)
                face.powerMenuOpen = ShellController.quickPowerOpen
            if (ShellController.quickEditOpen !== face.editing)
                face.editing = ShellController.quickEditOpen
        }
    }

    onPowerMenuOpenChanged: {
        if (ShellController && ShellController.quickPowerOpen !== face.powerMenuOpen)
            ShellController.quickPowerOpen = face.powerMenuOpen
    }

    onEditingChanged: {
        if (ShellController && ShellController.quickEditOpen !== face.editing)
            ShellController.quickEditOpen = face.editing
    }

    width: 654
    height: 1214

    property string clockText: Qt.formatTime(new Date(), "hh:mm AP")
    property string dateText: Qt.formatDate(new Date(), "ddd, d MMM")

    Timer {
        interval: 1000
        running: face.visible
        repeat: true
        triggeredOnStart: true
        onTriggered: {
            const now = new Date()
            face.clockText = Qt.formatTime(now, SettingsService.flag("time.use24h") ? "HH:mm" : "hh:mm AP")
            face.dateText = Qt.formatDate(now, "ddd, d MMM")
        }
    }

    readonly property string userName: QuickSource ? QuickSource.userName : "user"
    readonly property string hostName: QuickSource ? QuickSource.hostName : "windows"
    readonly property real downloadBps: QuickSource ? QuickSource.downloadBps : 0
    readonly property real uploadBps: QuickSource ? QuickSource.uploadBps : 0

    property int tileColumns: 5
    property var tiles: [
        { id: "network", size: "big" },
        { id: "bluetooth", size: "big" },
        { id: "keepAwake", size: "small" },
        { id: "microphone", size: "big" },
        { id: "speaker", size: "big" },
        { id: "notifications", size: "small" },
        { id: "warp", size: "big" },
        { id: "easyEffects", size: "big" },
        { id: "screenSnip", size: "small" }
    ]

    property bool easyEffectsOn: true
    property bool warpOn: false
    property real gammaValue: 100

    function togglePowerMenu() {
        face.powerMenuOpen = !face.powerMenuOpen
    }

    function formatRate(bps) {
        if (bps >= 1048576)
            return (bps / 1048576).toFixed(1) + " MB/s"
        if (bps >= 1024)
            return Math.round(bps / 1024) + " KB/s"
        return Math.round(bps) + " B/s"
    }

    readonly property var tileLayout: QT.pack(face.tiles, face.tileColumns, mt.quickFlat)
    property real tileExtra: tileLayout.height - QT.baseGridHeight
    Behavior on tileExtra {
        NumberAnimation { duration: 200; easing.type: Easing.OutCubic }
    }

    readonly property var unplacedTiles: QT.catalog.filter(id => !face.tiles.some(t => t.id === id))
    readonly property bool canAdd: QT.fits(face.tiles.concat([{ id: "_", size: "small" }]), face.tileColumns)

    function canResize(index) {
        if (index < 0 || index >= face.tiles.length)
            return false
        return QT.nextSize(face.tiles, index, face.tileColumns) !== face.tiles[index].size
    }

    function resizeGrows(index) {
        if (index < 0 || index >= face.tiles.length)
            return true
        const next = QT.nextSize(face.tiles, index, face.tileColumns)
        return QT.unitsOf(next) > QT.unitsOf(face.tiles[index].size)
    }

    function canColumns(columns) {
        return QT.fits(face.tiles, columns)
    }

    function addTile(id) {
        if (!face.canAdd)
            return
        const next = face.tiles.slice()
        next.push({ id: id, size: "small" })
        if (QT.fits(next, face.tileColumns))
            face.tiles = next
    }

    function removeTile(index) {
        if (index < 0 || index >= face.tiles.length)
            return
        const next = face.tiles.slice()
        next.splice(index, 1)
        face.tiles = next
    }

    function resizeTile(index) {
        if (index < 0 || index >= face.tiles.length)
            return
        const candidate = QT.nextSize(face.tiles, index, face.tileColumns)
        if (candidate === face.tiles[index].size)
            return
        const next = face.tiles.map(t => ({ id: t.id, size: t.size }))
        next[index].size = candidate
        face.tiles = next
    }

    function tileName(id) {
        switch (id) {
        case "network": return QuickSource && QuickSource.networkKind === "ethernet" ? "Ethernet" : "Wi-Fi"
        case "bluetooth": return "Bluetooth"
        case "keepAwake": return "Keep Awake"
        case "microphone": return "Microphone"
        case "speaker": return "Speaker"
        case "notifications": return "Alerts"
        case "nightLight": return "Night Light"
        case "gameMode": return "Game Mode"
        case "darkMode": return "Dark Mode"
        case "easyEffects": return "Easy Effects"
        case "warp": return "Cloudflare WARP"
        case "screenSnip": return "Screen Snip"
        case "colorPicker": return "Wallpaper"
        case "keyboard": return "On-Screen Keyboard"
        case "powerProfile": return "Power Profile"
        case "editDesktop": return "Edit Desktop"
        case "hideTaskbar": return "Win Taskbar"
        }
        return id
    }

    function tileOn(id) {
        switch (id) {
        case "network": return QuickSource ? QuickSource.networkOn : true
        case "bluetooth": return QuickSource ? QuickSource.bluetoothOn : false
        case "keepAwake": return QuickSource ? QuickSource.keepAwakeOn : false
        case "microphone": return QuickSource ? QuickSource.microphoneOn : true
        case "speaker": return QuickSource ? QuickSource.speakerOn : true
        case "notifications": return QuickSource ? QuickSource.notificationsOn : true
        case "nightLight": return QuickSource ? QuickSource.nightLightOn : false
        case "gameMode": return QuickSource ? QuickSource.gameModeOn : false
        case "darkMode": return ThemeService ? ThemeService.dark : false
        case "easyEffects": return face.easyEffectsOn
        case "warp": return face.warpOn
        case "keyboard": return false
        case "powerProfile": return QuickSource ? QuickSource.gameModeOn : false
        case "editDesktop": return ShellController ? ShellController.editModeOpen : false
        case "hideTaskbar": return !ShellController.windowsTaskbarVisible
        }
        return false
    }

    function tileSubtitle(id) {
        switch (id) {
        case "network": return QuickSource ? QuickSource.networkDetail : "Connected"
        case "bluetooth": return QuickSource ? QuickSource.bluetoothDetail : "Off"
        case "keepAwake": return tileOn(id) ? "ON" : "OFF"
        case "microphone": return tileOn(id) ? Math.round(QuickSource ? QuickSource.microphoneVolume : 100) + "%" : "Muted"
        case "speaker": return tileOn(id) ? Math.round(QuickSource ? QuickSource.speakerVolume : 72) + "%" : "Muted"
        case "notifications": return tileOn(id) ? "ON" : "Muted"
        case "nightLight": return tileOn(id) ? "Warm" : "OFF"
        case "gameMode": return tileOn(id) ? "ON" : "OFF"
        case "darkMode": return ThemeService ? (ThemeService.darker ? "Darker" : (ThemeService.dark ? "Dark" : "Light")) : "Light"
        case "easyEffects": return face.easyEffectsOn ? "ON" : "OFF"
        case "warp": return face.warpOn ? "Connected" : "Not installed"
        case "screenSnip": return "Tap"
        case "colorPicker": return "Tap"
        case "keyboard": return "Tap"
        case "powerProfile": return tileOn(id) ? "Performance" : "Balanced"
        case "editDesktop": return "Widgets"
        case "hideTaskbar": return !ShellController.windowsTaskbarVisible ? "Hidden" : "Shown"
        }
        return ""
    }

    function tileIcon(id) {
        switch (id) {
        case "network":
            return (QuickSource && QuickSource.networkKind === "ethernet")
                ? "settings_ethernet"
                : (tileOn("network") ? "wifi" : "wifi_off")
        case "bluetooth": return tileOn("bluetooth") ? "bluetooth" : "bluetooth_disabled"
        case "keepAwake": return "emoji_food_beverage"
        case "microphone": return tileOn("microphone") ? "mic" : "mic_off"
        case "speaker": return tileOn("speaker") ? "volume_up" : "volume_off"
        case "notifications": return tileOn("notifications") ? "notifications_active" : "notifications_off"
        case "nightLight": return "bedtime"
        case "gameMode": return "gamepad"
        case "darkMode": return ThemeService && ThemeService.dark ? "dark_mode" : "light_mode"
        case "easyEffects": return "graphic_eq"
        case "warp": return "vpn_lock"
        case "screenSnip": return "screenshot_region"
        case "colorPicker": return "wallpaper"
        case "keyboard": return "keyboard"
        case "powerProfile": return tileOn("powerProfile") ? "speed" : "balance"
        case "editDesktop": return "widgets"
        case "hideTaskbar": return "dock_to_bottom"
        }
        return "settings"
    }

    function tileFilled(id) {
        return (id === "nightLight" || id === "gameMode"
                || id === "easyEffects" || id === "warp") && face.tileOn(id)
    }

    function tileClicked(id) {
        switch (id) {
        case "network": if (QuickSource) QuickSource.toggleNetwork(); break
        case "bluetooth": if (QuickSource) QuickSource.toggleBluetooth(); break
        case "keepAwake": if (QuickSource) QuickSource.toggleKeepAwake(); break
        case "microphone": if (QuickSource) QuickSource.toggleMicrophone(); break
        case "speaker": if (QuickSource) QuickSource.toggleSpeaker(); break
        case "notifications": if (QuickSource) QuickSource.toggleNotifications(); break
        case "nightLight": if (QuickSource) QuickSource.toggleNightLight(); break
        case "gameMode":
        case "powerProfile":
            if (QuickSource) QuickSource.toggleGameMode()
            break
        case "darkMode": if (ThemeService) ThemeService.cycleMode(); break
        case "easyEffects": face.easyEffectsOn = !face.easyEffectsOn; break
        case "warp": face.warpOn = !face.warpOn; break
        case "keyboard": if (QuickSource) QuickSource.openOnScreenKeyboard(); break
        case "screenSnip":
            ShellController.closePhone()
            if (QuickSource) QuickSource.triggerScreenSnip()
            break
        case "colorPicker":
            ShellController.openWallpaperSelector()
            break
        case "editDesktop":
            ShellController.closePhone()
            ShellController.toggleEditMode()
            break
        case "hideTaskbar":
            ShellController.setWindowsTaskbarVisible(!ShellController.windowsTaskbarVisible)
            break
        }
    }

    function tileRightClicked(id) {
        switch (id) {
        case "network": if (QuickSource) QuickSource.openNetworkSettings(); break
        case "bluetooth": if (QuickSource) QuickSource.openBluetoothSettings(); break
        case "microphone":
        case "speaker":
            if (QuickSource) QuickSource.openSoundSettings()
            break
        case "nightLight": if (QuickSource) QuickSource.openDisplaySettings(); break
        }
    }

    // ===== Phone screen background =====
    Rectangle {
        x: mt.phoneScreenInset
        y: mt.phoneScreenInset
        width: face.width - 2 * mt.phoneScreenInset
        height: face.height - 2 * mt.phoneScreenInset
        radius: mt.phoneCornerRadius - mt.phoneScreenInset
        color: mt.pSurface
    }

    // ===== Top Banner (x:26, y:26, 602x240) =====
    Item {
        id: banner
        x: 26
        y: 26
        width: 602
        height: 240

        Rectangle {
            id: bannerBg
            anchors.fill: parent
            topLeftRadius: 74
            topRightRadius: 74
            bottomLeftRadius: 50
            bottomRightRadius: 50
            color: mt.pSurfaceHover
        }

        Image {
            id: bannerImg
            anchors.fill: parent
            source: WallpaperService.defaultPath ? ("file:///" + WallpaperService.defaultPath) : ""
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            smooth: true
            visible: false
        }

        Item {
            id: bannerMask
            anchors.fill: parent
            visible: false
            layer.enabled: true
            layer.smooth: true
            Rectangle {
                anchors.fill: parent
                color: "black"
                topLeftRadius: 74
                topRightRadius: 74
                bottomLeftRadius: 50
                bottomRightRadius: 50
            }
        }

        MultiEffect {
            anchors.fill: parent
            source: bannerImg
            maskEnabled: true
            maskSource: bannerMask
            visible: bannerImg.status === Image.Ready
        }

        Rectangle {
            anchors.fill: parent
            topLeftRadius: 74
            topRightRadius: 74
            bottomLeftRadius: 50
            bottomRightRadius: 50
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.24) }
                GradientStop { position: 0.5; color: Qt.rgba(1, 1, 1, 0.04) }
                GradientStop { position: 1.0; color: Qt.rgba(1, 1, 1, 0.35) }
            }
        }
    }

    // ===== Clock & Date on Banner (x:67, y:38) =====
    Column {
        x: 67
        y: 38
        spacing: -4

        Text {
            text: face.clockText
            color: mt.pInk
            font.family: mt.fontFamily
            font.pixelSize: 26
            font.weight: Font.ExtraBold
            font.styleName: mt.fontStyleBold
        }

        Text {
            text: face.dateText
            color: mt.pInk
            font.family: mt.fontFamily
            font.pixelSize: 20
            font.weight: Font.DemiBold
        }
    }

    // ===== Internet Speed on Banner (top-right) =====
    Column {
        id: speedBlock
        x: 654 - 67 - width
        y: 38
        width: 150
        spacing: -2

        Item {
            width: speedBlock.width
            height: downText.implicitHeight

            MaterialIcon {
                id: downIcon
                name: "arrow_downward"
                size: 20
                weight: 600
                color: mt.pInk
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                id: downText
                anchors.right: downIcon.left
                anchors.rightMargin: 2
                anchors.verticalCenter: parent.verticalCenter
                text: face.formatRate(face.downloadBps)
                color: mt.pInk
                font.family: mt.fontFamily
                font.pixelSize: 20
                font.weight: Font.ExtraBold
                font.styleName: mt.fontStyleBold
            }
        }

        Item {
            width: speedBlock.width
            height: upText.implicitHeight

            MaterialIcon {
                id: upIcon
                name: "arrow_upward"
                size: 20
                weight: 600
                color: mt.pInk
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                id: upText
                anchors.right: upIcon.left
                anchors.rightMargin: 2
                anchors.verticalCenter: parent.verticalCenter
                text: face.formatRate(face.uploadBps)
                color: mt.pInk
                font.family: mt.fontFamily
                font.pixelSize: 20
                font.weight: Font.DemiBold
            }
        }
    }

    // ===== Avatar + user@host (x:67, y:208, 120x120) =====
    Item {
        id: avatar
        x: 67
        y: 208
        width: 120
        height: 120

        Rectangle {
            anchors.fill: parent
            anchors.margins: 2
            radius: width / 2
            gradient: Gradient {
                GradientStop { position: 0.0; color: mt.accentMint }
                GradientStop { position: 1.0; color: mt.accentCyan }
            }
            antialiasing: true
        }

        Image {
            id: avatarImg
            anchors.fill: parent
            anchors.margins: 2
            source: WallpaperService.defaultPath ? ("file:///" + WallpaperService.defaultPath) : ""
            fillMode: Image.PreserveAspectCrop
            visible: false
        }

        Item {
            id: avatarMask
            anchors.fill: parent
            anchors.margins: 2
            visible: false
            layer.enabled: true
            layer.smooth: true
            Rectangle {
                anchors.fill: parent
                radius: width / 2
                color: "black"
            }
        }

        MultiEffect {
            anchors.fill: parent
            anchors.margins: 2
            source: avatarImg
            maskEnabled: true
            maskSource: avatarMask
            visible: avatarImg.status === Image.Ready
        }

        MaterialIcon {
            anchors.centerIn: parent
            name: "person"
            size: 46
            color: mt.colBorder
            visible: avatarImg.status !== Image.Ready
        }

        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: "transparent"
            border.width: 4
            border.color: mt.pRingWhite
            antialiasing: true
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: ShellController.openWallpaperSelector()
        }
    }

    Text {
        x: 67
        y: 331
        width: 254
        elide: Text.ElideRight
        text: face.userName + "@" + face.hostName
        color: mt.pInk
        font.family: mt.fontFamily
        font.pixelSize: 20
        font.weight: Font.ExtraBold
        font.styleName: mt.fontStyleBold
    }

    // ===== 4 Header Action Buttons: Settings, Refresh/Wallpapers, Edit, Power =====
    readonly property real headerX: 240
    readonly property real headerPitch: 89

    QuickToggle {
        flatShadow: mt.quickFlat
        kind: "square"
        x: face.headerX
        y: 276
        hoverLights: true
        icon: "settings"
        onClicked: ShellController.openSettings()
    }

    QuickToggle {
        flatShadow: mt.quickFlat
        kind: "square"
        x: face.headerX + face.headerPitch
        y: 276
        hoverLights: true
        icon: "refresh"
        onClicked: ShellController.openWallpaperSelector()
        onRightClicked: WallpaperService.nextInFolder("")
    }

    QuickToggle {
        flatShadow: mt.quickFlat
        kind: "square"
        x: face.headerX + face.headerPitch * 2
        y: 276
        hoverLights: true
        on: face.editing
        icon: "stylus"
        onClicked: face.editing = !face.editing
    }

    QuickToggle {
        flatShadow: mt.quickFlat
        kind: "square"
        x: face.headerX + face.headerPitch * 3
        y: 276
        hoverLights: true
        on: face.powerMenuOpen
        icon: "power_settings_new"
        onClicked: face.togglePowerMenu()
    }

    // ===== Tile Grid Card (x:40, y:362, 575 x 275+tileExtra) =====
    Rectangle {
        x: 40
        y: 362
        width: 575
        height: 275 + face.tileExtra
        radius: 40
        color: mt.pSurfaceHover
        antialiasing: true
    }

    Repeater {
        model: QT.catalog

        delegate: Item {
            id: tile

            required property string modelData

            readonly property int pos: {
                const t = face.tiles
                for (let i = 0; i < t.length; i++) {
                    if (t[i].id === tile.modelData)
                        return i
                }
                return -1
            }
            readonly property bool placed: pos >= 0
            readonly property var slot: pos >= 0 ? face.tileLayout.slots[pos] : undefined

            property string sizeName: "small"
            readonly property bool big: sizeName !== "small"
            property real slotW: 74
            property real slotX: 67
            property real slotY: 373

            Binding {
                target: tile
                property: "sizeName"
                value: tile.placed ? face.tiles[tile.pos].size : "small"
                when: tile.placed
            }
            Binding {
                target: tile
                property: "slotW"
                value: tile.slot ? tile.slot.w : 74
                when: tile.placed && tile.slot !== undefined
            }
            Binding {
                target: tile
                property: "slotX"
                value: 67 + (tile.slot ? tile.slot.x : 0)
                when: tile.placed && tile.slot !== undefined
            }
            Binding {
                target: tile
                property: "slotY"
                value: 373 + (tile.slot ? tile.slot.y + tile.slot.oy : 0)
                when: tile.placed && tile.slot !== undefined
            }

            x: slotX
            y: slotY
            readonly property bool sliderMode: QT.isSlider(modelData) && big
            width: sliderMode ? slider.implicitWidth + 5 : toggle.implicitWidth
            height: sliderMode ? slider.implicitHeight : toggle.implicitHeight

            opacity: placed ? 1 : 0
            visible: opacity > 0.01
            Behavior on opacity { NumberAnimation { duration: 150 } }
            Behavior on slotW { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
            Behavior on x { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
            Behavior on y { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }

            QuickToggle {
                id: toggle
                flatShadow: mt.quickFlat
                visible: !tile.sliderMode
                enabled: !face.editing
                kind: tile.big ? "wide" : "round"
                wideWidth: tile.slotW
                roundWidth: tile.slotW
                hoverLights: QT.isAction(tile.modelData)
                on: face.tileOn(tile.modelData)
                icon: face.tileIcon(tile.modelData)
                iconFilled: face.tileFilled(tile.modelData)
                label: face.tileName(tile.modelData)
                subtitle: face.tileSubtitle(tile.modelData)
                onClicked: face.tileClicked(tile.modelData)
                onRightClicked: face.tileRightClicked(tile.modelData)
            }

            VolumeSlider {
                id: slider
                flatShadow: mt.quickFlat
                visible: tile.sliderMode
                enabled: !face.editing
                sliderWidth: tile.slotW
                label: face.tileName(tile.modelData)
                icon: face.tileIcon(tile.modelData)
                muted: !face.tileOn(tile.modelData)
                value: tile.modelData === "microphone"
                    ? (QuickSource ? QuickSource.microphoneVolume : 100)
                    : (QuickSource ? QuickSource.speakerVolume : 72)
                onIconClicked: face.tileClicked(tile.modelData)
                onRightClicked: face.tileRightClicked(tile.modelData)
                onMoved: value => {
                    if (!QuickSource)
                        return
                    if (tile.modelData === "microphone")
                        QuickSource.setMicrophoneVolume(value)
                    else
                        QuickSource.setSpeakerVolume(value)
                }
            }

            // Edit Mode Remove Badge (top-left)
            Rectangle {
                visible: face.editing && tile.placed
                x: -6
                y: -6
                width: 28
                height: 28
                radius: 14
                color: mt.pSurface
                border.width: 3
                border.color: mt.pLine
                antialiasing: true
                z: 10

                MaterialIcon {
                    anchors.centerIn: parent
                    name: "remove"
                    size: 16
                    weight: 700
                    color: mt.pInk
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: face.removeTile(tile.pos)
                }
            }

            // Edit Mode Resize Badge (bottom-right)
            Rectangle {
                visible: face.editing && tile.placed
                x: tile.slotW - 18
                y: 73 - 20
                width: 28
                height: 28
                radius: 14
                color: mt.accentMint
                border.width: 3
                border.color: mt.pLine
                opacity: face.canResize(tile.pos) ? 1 : 0.4
                antialiasing: true
                z: 10

                MaterialIcon {
                    anchors.centerIn: parent
                    name: face.resizeGrows(tile.pos) ? "open_in_full" : "close_fullscreen"
                    size: 15
                    weight: 700
                    color: mt.colBorder
                }

                MouseArea {
                    anchors.fill: parent
                    enabled: face.canResize(tile.pos)
                    cursorShape: Qt.PointingHandCursor
                    onClicked: face.resizeTile(tile.pos)
                }
            }
        }
    }

    // ===== Combined Gamma + Brightness Slider (x:67, y:570+tileExtra) =====
    GammaBrightnessSlider {
        x: 67
        y: 570 + face.tileExtra
        sliderWidth: 514
        gamma: face.gammaValue
        brightness: QuickSource ? QuickSource.brightness : 90
        onGammaMoved: value => face.gammaValue = value
        onBrightnessMoved: value => {
            if (QuickSource)
                QuickSource.setBrightness(value)
        }
    }

    // ===== Notifications Card Shadow & Body (x:40, y:655+tileExtra, 575 x 515-tileExtra) =====
    Rectangle {
        x: 45
        y: 659 + face.tileExtra
        width: 575
        height: 515 - face.tileExtra
        radius: 41
        color: mt.pLineShadow
        antialiasing: true
    }

    Rectangle {
        id: notificationsCard
        x: 40
        y: 655 + face.tileExtra
        width: 575
        height: 515 - face.tileExtra
        radius: 41
        color: mt.pSurface
        border.width: 3
        border.color: mt.pLine
        clip: true

        readonly property var entries: (NotificationSource && NotificationSource.list) ? NotificationSource.list : []
        readonly property bool empty: entries.length === 0

        Column {
            anchors.horizontalCenter: parent.horizontalCenter
            y: 80
            spacing: 8
            visible: notificationsCard.empty && !face.editing

            MaterialIcon {
                anchors.horizontalCenter: parent.horizontalCenter
                name: "notifications_none"
                size: 36
                color: mt.pInkSub
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Nothing here. Suspiciously quiet."
                color: mt.pInkSub
                font.family: mt.fontFamily
                font.pixelSize: 17
                font.weight: Font.ExtraBold
            }
        }

        ListView {
            id: notifList
            anchors.fill: parent
            anchors.topMargin: 32
            anchors.bottomMargin: 18
            anchors.leftMargin: 18
            anchors.rightMargin: 18
            spacing: 12
            clip: true
            visible: !notificationsCard.empty && !face.editing
            model: notificationsCard.entries

            delegate: NotificationCard {
                required property var modelData
                width: notifList.width
                notifId: modelData.key !== undefined ? modelData.key : (modelData.id || 0)
                app: modelData.app || "SEKAIPOP"
                title: modelData.title || ""
                body: modelData.body || ""
                icon: modelData.icon || "notifications"
                timeLabel: modelData.timeLabel || "now"
                urgency: modelData.urgency !== undefined ? modelData.urgency : 1
                onDismissRequested: id => {
                    if (NotificationSource)
                        NotificationSource.dismiss(id)
                }
            }
        }
    }

    // ===== Edit Mode "Add buttons" + "Columns 1 2 3 4 5" Drawer =====
    Rectangle {
        id: editTray
        x: 40
        y: notificationsCard.y
        width: 575
        height: notificationsCard.height
        radius: 41
        color: mt.pSurface
        border.width: 3
        border.color: mt.pLine
        clip: true
        opacity: face.editing ? 1 : 0
        visible: opacity > 0.01
        Behavior on opacity { NumberAnimation { duration: 160 } }

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.AllButtons
            onWheel: wheel => wheel.accepted = true
        }

        Item {
            id: columnsRow
            x: 27
            y: 30
            width: 520
            height: 44

            Text {
                id: columnsLabel
                anchors.verticalCenter: parent.verticalCenter
                text: "Columns"
                color: mt.pInk
                font.family: mt.fontFamily
                font.pixelSize: 18
                font.weight: Font.ExtraBold
                font.styleName: mt.fontStyleBold
            }

            Row {
                anchors.left: columnsLabel.right
                anchors.leftMargin: 18
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8

                Repeater {
                    model: [1, 2, 3, 4, 5]

                    delegate: Rectangle {
                        id: chip
                        required property int modelData

                        readonly property bool current: face.tileColumns === modelData
                        readonly property bool allowed: face.canColumns(modelData)

                        width: 46
                        height: 40
                        radius: 20
                        color: chip.current ? mt.accentMint : mt.pSurfaceDivider
                        border.width: 3
                        border.color: mt.pLine
                        opacity: chip.allowed || chip.current ? 1 : 0.4
                        antialiasing: true

                        Text {
                            anchors.centerIn: parent
                            text: chip.modelData
                            color: chip.current ? mt.colBorder : mt.pInk
                            font.family: mt.fontFamily
                            font.pixelSize: 18
                            font.weight: Font.ExtraBold
                            font.styleName: mt.fontStyleBold
                        }

                        MouseArea {
                            anchors.fill: parent
                            enabled: chip.allowed && !chip.current
                            cursorShape: Qt.PointingHandCursor
                            onClicked: face.tileColumns = chip.modelData
                        }
                    }
                }
            }
        }

        Flickable {
            id: trayScroll
            x: 27
            y: columnsRow.y + columnsRow.height + 10
            width: 520
            height: Math.max(0, editTray.height - y - 20)
            contentWidth: width
            contentHeight: trayFlow.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            Flow {
                id: trayFlow
                width: trayScroll.width
                spacing: 4

                Repeater {
                    model: face.unplacedTiles

                    delegate: Item {
                        required property string modelData

                        width: 100
                        height: 118
                        opacity: face.canAdd ? 1 : 0.4

                        QuickToggle {
                            flatShadow: mt.quickFlat
                            kind: "round"
                            anchors.horizontalCenter: parent.horizontalCenter
                            hoverLights: face.canAdd
                            enabled: face.canAdd
                            icon: face.tileIcon(modelData)
                            onClicked: face.addTile(modelData)
                        }

                        Text {
                            y: 80
                            x: 2
                            width: parent.width - 4
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                            lineHeight: 0.9
                            text: face.tileName(modelData)
                            color: mt.pInk
                            font.family: mt.fontFamily
                            font.pixelSize: 16
                            font.weight: Font.ExtraBold
                            font.styleName: mt.fontStyleBold
                        }
                    }
                }
            }
        }
    }

    // ===== "Notifications" / "Add buttons" Top Tag (x:212, y:639+tileExtra) =====
    Rectangle {
        id: tag
        x: 212
        y: 639 + face.tileExtra
        width: 229
        height: 31
        radius: 27
        color: tagArea.containsMouse && !face.editing && !notificationsCard.empty ? mt.accentMint : mt.pSurface
        border.width: 3
        border.color: mt.pLine
        antialiasing: true

        Text {
            anchors.centerIn: parent
            text: face.editing
                ? "Add buttons"
                : (tagArea.containsMouse && !notificationsCard.empty ? "Clear all" : "Notifications")
            color: mt.pInk
            font.family: mt.fontFamily
            font.pixelSize: 20
            font.weight: Font.ExtraBold
            font.styleName: mt.fontStyleBold
        }

        MouseArea {
            id: tagArea
            anchors.fill: parent
            hoverEnabled: true
            enabled: !face.editing && !notificationsCard.empty
            cursorShape: notificationsCard.empty ? Qt.ArrowCursor : Qt.PointingHandCursor
            onClicked: {
                if (NotificationSource)
                    NotificationSource.clearAll()
            }
        }
    }

    // ===== Power Menu Scrim on Phone Glass =====
    PowerMenuScrim {
        id: powerScrim
        open: face.powerMenuOpen
        onDismissRequested: face.powerMenuOpen = false
    }
}
