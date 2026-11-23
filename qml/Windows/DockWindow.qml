import QtQuick
import QtQuick.Window
import Common
import Common.widgets

Window {
    id: win

    property string monitorName: ""
    readonly property var mt: Theme.of(win)
    readonly property real uiScale: ShellScale.forScreen(monitorName)
    readonly property string dockStyle: String(SettingsService.get("dock.style") || "float")
    readonly property bool dockVisible: SettingsService.flag("dock.visible")
    readonly property bool showMedia: SettingsService.flag("dock.media")
    readonly property bool magnify: SettingsService.flag("dock.magnify")
    readonly property bool isCapsule: dockStyle === "capsule"
    readonly property bool isClear: dockStyle === "transparent"

    property bool _ready: false
    property bool shown: dockVisible
    property real dockOffsetY: 0
    property real dockOpacity: 1.0

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

    width: Math.max(260, Math.round((dockRoot.implicitWidth + 16) * uiScale))
    height: Math.max(66, Math.round((dockRoot.implicitHeight + 12) * uiScale))

    function positionDock() {
        const scr = win.targetScreen || (Qt.application.screens.length > 0 ? Qt.application.screens[0] : win.screen)
        if (!scr) return
        const sx = scr.virtualX !== undefined ? scr.virtualX : 0
        const sy = scr.virtualY !== undefined ? scr.virtualY : 0
        const sw = scr.width || 1920
        const sh = scr.height || 1080
        const taskbarReserve = ShellController.windowsTaskbarVisible ? 54 : 0
        const bottomReserve = (BarService.position === "bottom" ? BarService.stripSizeFor("bottom", win.uiScale) + 6 : 8) + taskbarReserve
        win.x = sx + Math.round((sw - win.width) / 2)
        win.y = sy + sh - win.height - bottomReserve
    }

    function openWinForPinned(pinItem) {
        const tgt = String(pinItem.target || "").toLowerCase()
        const nm = String(pinItem.name || "").toLowerCase()
        const list = WindowTracker.openWindows
        for (let i = 0; i < list.length; i++) {
            const w = list[i]
            const wApp = String(w.app || w.appName || w.name || "").toLowerCase()
            const wTitle = String(w.title || "").toLowerCase()
            const wExe = String(w.exe || w.exePath || w.process || "").toLowerCase()
            if ((tgt && wExe && wExe.indexOf(tgt.replace(".exe", "")) >= 0) ||
                (nm && (wApp.indexOf(nm) >= 0 || wTitle.indexOf(nm) >= 0))) {
                return w
            }
        }
        return null
    }

    readonly property var unpinnedRunning: {
        const list = WindowTracker.openWindows
        const pins = PinnedAppsService.pinned
        const out = []
        for (let i = 0; i < list.length; i++) {
            const w = list[i]
            const wApp = String(w.app || w.appName || w.name || w.title || "").toLowerCase()
            const wExe = String(w.exe || w.exePath || w.process || "").toLowerCase()
            let covered = false
            for (let j = 0; j < pins.length; j++) {
                const pName = String(pins[j].name || "").toLowerCase()
                const pTgt = String(pins[j].target || "").toLowerCase()
                if ((pName && wApp.indexOf(pName) >= 0) || (pTgt && wExe.indexOf(pTgt.replace(".exe", "")) >= 0)) {
                    covered = true
                    break
                }
            }
            if (!covered)
                out.push(w)
        }
        return out
    }

    onDockVisibleChanged: {
        if (!win._ready) {
            win.shown = dockVisible
            win.dockOffsetY = dockVisible ? 0 : 56
            win.dockOpacity = dockVisible ? 1.0 : 0.0
            return
        }
        if (dockVisible) {
            hideAnim.stop()
            win.shown = true
            win.dockOffsetY = 56
            win.dockOpacity = 0.0
            Qt.callLater(positionDock)
            Qt.callLater(() => ShellController.configureDockWindow(win))
            showAnim.restart()
        } else {
            showAnim.stop()
            hideAnim.restart()
        }
    }
    onWidthChanged: Qt.callLater(positionDock)
    onHeightChanged: Qt.callLater(positionDock)

    Component.onCompleted: {
        win.shown = win.dockVisible
        win.dockOffsetY = win.dockVisible ? 0 : 56
        win.dockOpacity = win.dockVisible ? 1.0 : 0.0
        win._ready = true
        win.positionDock()
        Qt.callLater(() => {
            win.positionDock()
            ShellController.configureDockWindow(win)
        })
    }

    Connections {
        target: BarService
        function onChanged() { Qt.callLater(win.positionDock) }
    }

    Connections {
        target: ShellController
        function onExplorerRunningChanged() { Qt.callLater(win.positionDock) }
    }

    ParallelAnimation {
        id: showAnim
        NumberAnimation { target: win; property: "dockOffsetY"; to: 0; duration: 280; easing.type: Easing.OutBack; easing.overshoot: 1.2 }
        NumberAnimation { target: win; property: "dockOpacity"; to: 1.0; duration: 180; easing.type: Easing.OutCubic }
    }

    SequentialAnimation {
        id: hideAnim
        ParallelAnimation {
            NumberAnimation { target: win; property: "dockOffsetY"; to: 56; duration: 160; easing.type: Easing.InCubic }
            NumberAnimation { target: win; property: "dockOpacity"; to: 0.0; duration: 140; easing.type: Easing.InQuad }
        }
        ScriptAction { script: win.shown = false }
    }

    ScaleHost {
        id: scaleHost
        anchors.fill: parent
        factor: win.uiScale

        Item {
            id: dockRoot
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.verticalCenter: parent.verticalCenter
            anchors.verticalCenterOffset: win.dockOffsetY
            opacity: win.dockOpacity
            width: implicitWidth
            height: implicitHeight
            implicitWidth: outerRow.implicitWidth + 24
            implicitHeight: 58

            // Hard offset shadow (float/capsule)
            Rectangle {
                visible: !win.isClear
                x: 2
                y: mt.shadowOffset + 2
                width: dockFace.width
                height: dockFace.height
                radius: win.isCapsule ? 26 : 18
                color: win.isCapsule ? mt.capShade : mt.lineShadow
                antialiasing: true
            }

            // Outer ring
            Rectangle {
                id: dockFace
                width: parent.width
                height: parent.height - mt.shadowOffset
                radius: win.isCapsule ? 26 : 18
                color: win.isClear ? "transparent" : (win.isCapsule ? mt.capBody : mt.surfaceRing)
                border.width: win.isClear ? 0 : mt.borderWidth
                border.color: win.isClear ? "transparent" : (win.isCapsule ? mt.capRim : mt.line)
                antialiasing: true

                // Inner surface
                Rectangle {
                    visible: !win.isClear && !win.isCapsule
                    anchors.fill: parent
                    anchors.margins: mt.borderWidth + 4
                    radius: 11
                    color: mt.surface
                    antialiasing: true
                }

                Row {
                    id: outerRow
                    anchors.centerIn: parent
                    spacing: 6

                    // Quick tool keys: Search, Quick Panel, Settings
                    Repeater {
                        model: [
                            { icon: "search", tip: "Apps (Alt+Space)", active: ShellController.launcherOpen, kind: "apps" },
                            { icon: "tune", tip: "Quick Panel (Alt+N)", active: ShellController.phoneOpen && ShellController.phoneScreen === "quick", kind: "quick" },
                            { icon: "settings", tip: "Settings", active: ShellController.phoneOpen && ShellController.phoneScreen === "settings", kind: "settings" }
                        ]

                        delegate: Item {
                            required property var modelData
                            width: 34
                            height: 34
                            scale: win.magnify && kArea.containsMouse ? 1.14 : 1.0
                            Behavior on scale { NumberAnimation { duration: 130; easing.type: Easing.OutBack } }

                            Rectangle {
                                anchors.fill: parent
                                radius: 11
                                color: modelData.active ? mt.accentMint : (kArea.containsMouse ? mt.surfaceHover : mt.chipFill)
                                border.width: 2
                                border.color: mt.line
                                antialiasing: true
                            }

                            MaterialIcon {
                                anchors.centerIn: parent
                                name: modelData.icon
                                size: 18
                                color: modelData.active ? mt.colBorder : mt.chipInk
                            }

                            MouseArea {
                                id: kArea
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    if (modelData.kind === "apps") {
                                        ShellController.closePhone()
                                        ShellController.toggleLauncher()
                                    } else {
                                        ShellController.togglePhone(modelData.kind)
                                    }
                                }
                            }
                        }
                    }

                    // Divider
                    Rectangle {
                        width: 2
                        height: 26
                        radius: 1
                        anchors.verticalCenter: parent.verticalCenter
                        color: mt.line
                        opacity: 0.35
                    }

                    // Pinned Windows apps
                    Repeater {
                        model: PinnedAppsService.pinned

                        delegate: Item {
                            id: pinSlot
                            required property var modelData
                            readonly property var runningWin: win.openWinForPinned(modelData)
                            readonly property bool isRunning: runningWin !== null
                            readonly property bool isActive: isRunning && runningWin.hwnd === WindowTracker.activeHwnd

                            width: 38
                            height: 38
                            scale: win.magnify && pArea.containsMouse ? 1.15 : 1.0
                            Behavior on scale { NumberAnimation { duration: 140; easing.type: Easing.OutBack } }

                            Rectangle {
                                anchors.centerIn: parent
                                width: 34
                                height: 34
                                radius: 10
                                color: pinSlot.isActive
                                    ? Qt.rgba(mt.accentMint.r, mt.accentMint.g, mt.accentMint.b, 0.38)
                                    : (pArea.containsMouse ? mt.surfaceHover : "transparent")
                                border.width: pinSlot.isActive ? 2 : 0
                                border.color: mt.line
                                antialiasing: true
                            }

                            IconImage {
                                anchors.centerIn: parent
                                width: 26
                                height: 26
                                asynchronous: true
                                source: pinSlot.modelData.icon || ""
                            }

                            // Running indicator dot
                            Rectangle {
                                visible: pinSlot.isRunning
                                anchors.bottom: parent.bottom
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: pinSlot.isActive ? 14 : 6
                                height: 4
                                radius: 2
                                color: pinSlot.isActive ? mt.accentMint : mt.line
                                Behavior on width { NumberAnimation { duration: 160 } }
                            }

                            MouseArea {
                                id: pArea
                                anchors.fill: parent
                                hoverEnabled: true
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                cursorShape: Qt.PointingHandCursor
                                onClicked: mouse => {
                                    if (mouse.button === Qt.RightButton) {
                                        PinnedAppsService.unpin(pinSlot.modelData.key || pinSlot.modelData.target)
                                        return
                                    }
                                    if (pinSlot.isRunning) {
                                        if (pinSlot.isActive)
                                            WindowTracker.minimizeWindow(pinSlot.runningWin.hwnd)
                                        else
                                            WindowTracker.focusWindow(pinSlot.runningWin.hwnd)
                                    } else {
                                        PinnedAppsService.launchItem(pinSlot.modelData)
                                    }
                                }
                            }
                        }
                    }

                    // Unpinned running windows
                    Repeater {
                        model: win.unpinnedRunning

                        delegate: Item {
                            id: runSlot
                            required property var modelData
                            readonly property bool isActive: modelData.hwnd === WindowTracker.activeHwnd

                            width: 38
                            height: 38
                            scale: win.magnify && rArea.containsMouse ? 1.15 : 1.0
                            Behavior on scale { NumberAnimation { duration: 140; easing.type: Easing.OutBack } }

                            Rectangle {
                                anchors.centerIn: parent
                                width: 34
                                height: 34
                                radius: 10
                                color: runSlot.isActive
                                    ? Qt.rgba(mt.accentCyan.r, mt.accentCyan.g, mt.accentCyan.b, 0.35)
                                    : (rArea.containsMouse ? mt.surfaceHover : mt.chipFill)
                                border.width: 2
                                border.color: runSlot.isActive ? mt.accentCyan : mt.line
                                antialiasing: true
                            }

                            IconImage {
                                anchors.centerIn: parent
                                width: 24
                                height: 24
                                asynchronous: true
                                opacity: (runSlot.modelData.workspace === undefined || runSlot.modelData.workspace === WindowTracker.activeWorkspace) ? 1.0 : 0.62
                                source: runSlot.modelData.icon || ""
                            }

                            Rectangle {
                                anchors.bottom: parent.bottom
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: runSlot.isActive ? 14 : 6
                                height: 4
                                radius: 2
                                color: runSlot.isActive ? mt.accentCyan : mt.line
                                Behavior on width { NumberAnimation { duration: 160 } }
                            }

                            MouseArea {
                                id: rArea
                                anchors.fill: parent
                                hoverEnabled: true
                                acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
                                cursorShape: Qt.PointingHandCursor
                                onClicked: mouse => {
                                    if (mouse.button === Qt.MiddleButton || mouse.button === Qt.RightButton) {
                                        WindowTracker.closeWindow(runSlot.modelData.hwnd)
                                    } else if (runSlot.isActive) {
                                        WindowTracker.minimizeWindow(runSlot.modelData.hwnd)
                                    } else {
                                        WindowTracker.focusWindow(runSlot.modelData.hwnd)
                                    }
                                }
                            }
                        }
                    }

                    // Media Divider + Inline Mini Media Pill
                    Rectangle {
                        visible: win.showMedia
                        width: 2
                        height: 26
                        radius: 1
                        anchors.verticalCenter: parent.verticalCenter
                        color: mt.line
                        opacity: 0.35
                    }

                    Pill {
                        visible: win.showMedia
                        width: 196
                        height: 36
                        cornerRadius: 12
                        ringWidth: 2
                        shadowDy: 2
                        fillColor: mt.chipFill

                        Item {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8

                            MaterialIcon {
                                id: medGlyph
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                name: MediaSource.present ? (MediaSource.playing ? "graphic_eq" : "music_note") : "music_off"
                                size: 17
                                color: MediaSource.playing ? mt.accentMint : mt.chipInk
                            }

                            Text {
                                anchors.left: medGlyph.right
                                anchors.leftMargin: 6
                                anchors.right: ctrlRow.left
                                anchors.rightMargin: 6
                                anchors.verticalCenter: parent.verticalCenter
                                text: MediaSource.present && MediaSource.title.length > 0 ? MediaSource.title : "No Media"
                                color: mt.chipInk
                                font.family: mt.fontFamily
                                font.pixelSize: 11
                                font.weight: Font.ExtraBold
                                font.styleName: mt.fontStyleBold
                                elide: Text.ElideRight
                            }

                            Row {
                                id: ctrlRow
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 2

                                Item {
                                    width: 22; height: 22
                                    MaterialIcon { anchors.centerIn: parent; name: "skip_previous"; size: 16; color: mt.chipInk }
                                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: MediaSource.previous() }
                                }
                                Item {
                                    width: 24; height: 24
                                    Rectangle { anchors.fill: parent; radius: 12; color: MediaSource.playing ? mt.accentMint : mt.surfaceHover; border.width: 1.5; border.color: mt.line }
                                    MaterialIcon { anchors.centerIn: parent; name: MediaSource.playing ? "pause" : "play_arrow"; size: 16; color: MediaSource.playing ? mt.colBorder : mt.chipInk }
                                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: MediaSource.togglePlayPause() }
                                }
                                Item {
                                    width: 22; height: 22
                                    MaterialIcon { anchors.centerIn: parent; name: "skip_next"; size: 16; color: mt.chipInk }
                                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: MediaSource.next() }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
