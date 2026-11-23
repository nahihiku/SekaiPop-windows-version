import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import Common
import Common.widgets

/**
 * SekaiPop Bar layout for Windows MVP: preserves Float, Straight, Transparent,
 * and Capsule bar visuals across top/bottom (horizontal) and left/right (vertical)
 * screen edges with live Win32 data bindings.
 */
Item {
    id: status_Bar

    readonly property var mt: Theme.of(Window.window)

    property var modules: ({})
    property var occupiedWorkspaces: []
    property bool vertical: false
    property string barPosition: "top"
    property int barStyle: 2 // 0 float, 1 straight, 2 transparent, 3 capsule
    property int activeWorkspace: 1
    property int workspaceCount: 5

    property string clockText: "00:00 AM"
    property string dateText: ""
    property string windowApp: ""
    property string windowTitle: ""
    property string windowIcon: ""

    property bool mediaActive: false
    property bool mediaPlaying: false
    property string mediaTitle: ""
    property string mediaArtist: ""
    property string mediaArtUrl: ""
    property bool mediaVolumeAvailable: false

    property real cpuPercent: 0
    property real memPercent: 0
    property string memUsedLabel: "--"
    property string memTotalLabel: "--"

    property bool batteryAvailable: false
    property int batteryPercentage: 0
    property bool batteryCharging: false
    property bool batteryLow: false

    property bool statusNetworkShown: false
    property string statusNetworkIcon: "wifi_off"
    property string statusNetworkState: "off"
    property bool statusBluetoothShown: false
    property string statusBluetoothIcon: "bluetooth_disabled"
    property string statusBluetoothState: "off"
    property bool statusSpeakerShown: false
    property string statusSpeakerIcon: "volume_off"
    property string statusSpeakerState: "off"
    property string statusDetail: ""

    property bool launcherOpen: false

    property var layout: ({
        left: ["workspaces", "activeWindow", "weather", "media"],
        center: ["status", "date", "clock"],
        right: ["resources", "battery", "utilButtons"]
    })

    function shows(name) {
        return status_Bar.modules[name] !== false
    }

    readonly property int crossAlign: status_Bar.vertical ? Qt.AlignHCenter : Qt.AlignVCenter
    readonly property real otherWidgetsLift: status_Bar.capsule ? 0 : 3
    readonly property real liftY: status_Bar.vertical ? 0 : -status_Bar.otherWidgetsLift
    readonly property real vGap: status_Bar.capsule ? mt.capGap - mt.shadowOffset : 8
    property string screenName: ""
    readonly property bool tighterV: status_Bar.vertical && content.height < 960
    implicitHeight: status_Bar.vertical
        ? (leftGroup.implicitHeight + centerGroup.implicitHeight + rightGroup.implicitHeight + 40)
        : content.height

    readonly property bool capsule: status_Bar.barStyle === 3
    readonly property bool capsuleBar: status_Bar.capsule
    readonly property bool capsuleVertical: status_Bar.capsule && status_Bar.vertical

    readonly property var slotMap: {
        const m = ({})
        for (const z of ["left", "center", "right"]) {
            let i = 0
            for (const id of (status_Bar.layout[z] || [])) {
                m[id] = i++
            }
        }
        return m
    }

    readonly property var zoneMap: {
        const m = ({})
        for (const z of ["left", "center", "right"]) {
            for (const id of (status_Bar.layout[z] || [])) {
                m[id] = z
            }
        }
        return m
    }

    function slotOf(key) {
        const v = status_Bar.slotMap[key]
        return v === undefined ? 0 : v
    }

    function zoneItem(key) {
        const z = status_Bar.zoneMap[key]
        return z === "left" ? leftGroup : (z === "center" ? centerGroup : rightGroup)
    }

    readonly property real zoneGap: status_Bar.capsule ? mt.capGap : 10 - badges.neighborShift
    readonly property real activeWindowCap: 260
    readonly property real maxActiveWindowWidth: Math.max(140, Math.min(status_Bar.activeWindowCap, status_Bar.width * 0.16))
    readonly property real mediaCap: 280
    readonly property real maxMediaWidth: {
        const z = status_Bar.zoneMap["media"]
        const half = content.width / 2
        let room = status_Bar.mediaCap
        if (z === "left") {
            room = content.width - rightGroup.width - 12 - status_Bar.othersWidth("left")
            if (centerGroup.width > 0)
                room = Math.min(room, half - centerGroup.width / 2 - 12 - status_Bar.othersWidth("left"))
        } else if (z === "right") {
            room = content.width - leftGroup.width - 12 - status_Bar.othersWidth("right")
            if (centerGroup.width > 0)
                room = Math.min(room, half - centerGroup.width / 2 - 12 - status_Bar.othersWidth("right"))
        }
        return Math.min(status_Bar.mediaCap, Math.max(120, room))
    }

    function itemsOf(id) {
        switch (id) {
        case "workspaces": return [badges]
        case "activeWindow": return [activeWindow]
        case "weather": return [weatherIndicator]
        case "media": return [mediaSlot]
        case "utilButtons": return [utilKeys]
        case "resources": return [resourceIndicator]
        case "status": return [statusPill]
        case "battery": return [batteryIndicator]
        case "date": return [dateIndicator]
        case "clock": return [clockButton]
        }
        return []
    }

    function slotWidth(it) {
        return it ? it.implicitWidth : 0
    }

    function othersWidth(zone) {
        let w = 0
        for (const id of (status_Bar.layout[zone] || [])) {
            if (id === "media")
                continue
            for (const it of status_Bar.itemsOf(id)) {
                if (it && it.visible)
                    w += status_Bar.slotWidth(it) + status_Bar.zoneGap
            }
        }
        return w
    }

    function othersHeight(zone) {
        let h = 0
        for (const id of (status_Bar.layout[zone] || [])) {
            if (id === "workspaces" || !status_Bar.shows(id))
                continue
            for (const it of status_Bar.itemsOf(id)) {
                if (it)
                    h += Math.max(it.height, it.implicitHeight) + status_Bar.vGap
            }
        }
        return h
    }

    readonly property real badgesRoom: {
        if (!status_Bar.vertical)
            return -1
        const H = content.height
        const z = status_Bar.zoneMap["workspaces"]
        if (z === "left")
            return H - rightGroup.height - 28 - status_Bar.othersHeight("left")
        if (z === "right")
            return H - leftGroup.height - 28 - status_Bar.othersHeight("right")
        return H - leftGroup.height - rightGroup.height - 28 - status_Bar.othersHeight("center")
    }

    signal workspaceRequested(int workspace)
    signal mediaToggleRequested()
    signal mediaNextRequested()
    signal mediaPreviousRequested()
    signal launcherRequested()
    signal wallpapersRequested()
    signal themeCycleRequested()
    signal positionCycleRequested()
    signal restoreExplorerRequested()
    signal statusClicked()
    signal resourcesClicked()
    signal batteryClicked()
    signal dateClicked()
    signal weatherClicked()
    signal mediaPopupClicked()

    readonly property color bgRingColor: mt.surfaceRing
    readonly property real bgRingWidth: mt.outerRingWidth
    property real bgRadius: status_Bar.barStyle === 0 ? mt.radiusOuter : 0
    property real bgBleed: status_Bar.barStyle === 1 ? mt.borderWidth + status_Bar.bgRingWidth + 1 : 0

    Behavior on bgRadius { NumberAnimation { duration: 180; easing.type: Easing.OutQuad } }
    Behavior on bgBleed { NumberAnimation { duration: 180; easing.type: Easing.OutQuad } }

    Item {
        id: bgLayer
        anchors.fill: parent
        opacity: (status_Bar.barStyle === 2 || status_Bar.capsule) ? 0 : 1
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 180; easing.type: Easing.OutQuad } }

        Rectangle {
            anchors.fill: parent
            anchors.topMargin: mt.shadowOffset - (status_Bar.vertical ? status_Bar.bgBleed : 0)
            anchors.bottomMargin: status_Bar.vertical ? -status_Bar.bgBleed : 0
            anchors.leftMargin: status_Bar.vertical ? 0 : -status_Bar.bgBleed
            anchors.rightMargin: status_Bar.vertical ? 0 : -status_Bar.bgBleed
            radius: status_Bar.bgRadius
            color: mt.lineShadow
        }

        Rectangle {
            id: bgFace
            anchors.fill: parent
            anchors.topMargin: status_Bar.vertical ? -status_Bar.bgBleed : 0
            anchors.bottomMargin: mt.shadowOffset - (status_Bar.vertical ? status_Bar.bgBleed : 0)
            anchors.leftMargin: status_Bar.vertical ? 0 : -status_Bar.bgBleed
            anchors.rightMargin: status_Bar.vertical ? 0 : -status_Bar.bgBleed
            radius: status_Bar.bgRadius
            color: status_Bar.bgRingColor
            border.width: mt.borderWidth
            border.color: mt.line
        }

        Rectangle {
            anchors.fill: bgFace
            anchors.margins: mt.borderWidth + status_Bar.bgRingWidth
            radius: Math.max(0, status_Bar.bgRadius - mt.borderWidth - status_Bar.bgRingWidth)
            color: mt.surface
        }
    }

    Item {
        id: capsuleLayer
        anchors.fill: parent
        opacity: status_Bar.capsule ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 180; easing.type: Easing.OutQuad } }

        Rectangle {
            x: 0
            y: mt.capShadowDy
            width: parent.width
            height: parent.height - mt.capShadowDy
            radius: Math.min(width, height) / 2
            color: mt.capShade
            antialiasing: true
        }

        Rectangle {
            id: capFace
            width: parent.width
            height: parent.height - mt.capShadowDy
            radius: Math.min(width, height) / 2
            color: mt.capRim
            antialiasing: true
        }

        Rectangle {
            anchors.fill: capFace
            anchors.margins: mt.capRimWidth
            radius: Math.min(width, height) / 2
            color: mt.capBody
            antialiasing: true
        }
    }

    Item {
        id: content
        anchors.fill: parent
        anchors.leftMargin: status_Bar.vertical ? 0 : (status_Bar.capsule ? mt.capRimWidth + mt.capPadX : mt.barFloatInset)
        anchors.rightMargin: status_Bar.vertical ? 0 : (status_Bar.capsule ? mt.capRimWidth + mt.capPadX : mt.barFloatInset)
        anchors.topMargin: status_Bar.vertical ? (status_Bar.capsule ? mt.capRimWidth + mt.capVPadY : mt.barFloatInset) : 0
        anchors.bottomMargin: status_Bar.vertical
            ? (status_Bar.capsule ? mt.capRimWidth + mt.capVPadY - 1 : mt.barFloatInset)
            : (status_Bar.capsule ? mt.capShadowDy : 0)

        GridLayout {
            id: leftGroup
            width: implicitWidth
            height: implicitHeight
            anchors.left: status_Bar.vertical ? undefined : parent.left
            anchors.top: status_Bar.vertical ? parent.top : undefined
            anchors.verticalCenter: status_Bar.vertical ? undefined : parent.verticalCenter
            anchors.horizontalCenter: status_Bar.vertical ? parent.horizontalCenter : undefined
            columns: status_Bar.vertical ? 1 : 100
            columnSpacing: status_Bar.zoneGap
            rowSpacing: status_Bar.vertical ? status_Bar.vGap : status_Bar.zoneGap
        }

        GridLayout {
            id: centerGroup
            width: implicitWidth
            height: implicitHeight
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.verticalCenter: status_Bar.vertical ? undefined : parent.verticalCenter
            y: status_Bar.vertical
                ? Math.max(leftGroup.implicitHeight + status_Bar.vGap,
                           Math.min(content.height - rightGroup.implicitHeight - implicitHeight - status_Bar.vGap,
                                    Math.round((content.height - implicitHeight) / 2)))
                : 0
            visible: !status_Bar.vertical || content.height >= leftGroup.implicitHeight + rightGroup.implicitHeight + centerGroup.implicitHeight + status_Bar.vGap
            columns: status_Bar.vertical ? 1 : 100
            columnSpacing: status_Bar.zoneGap
            rowSpacing: status_Bar.vertical ? status_Bar.vGap : status_Bar.zoneGap
        }

        GridLayout {
            id: rightGroup
            width: implicitWidth
            height: implicitHeight
            anchors.right: status_Bar.vertical ? undefined : parent.right
            anchors.bottom: status_Bar.vertical ? parent.bottom : undefined
            anchors.verticalCenter: status_Bar.vertical ? undefined : parent.verticalCenter
            anchors.horizontalCenter: status_Bar.vertical ? parent.horizontalCenter : undefined
            columns: status_Bar.vertical ? 1 : 100
            columnSpacing: status_Bar.zoneGap
            rowSpacing: status_Bar.vertical ? status_Bar.vGap : status_Bar.zoneGap
        }

        WorkspaceBadges {
            id: badges
            parent: status_Bar.zoneItem("workspaces")
            visible: status_Bar.shows("workspaces")
            Layout.row: status_Bar.vertical ? status_Bar.slotOf("workspaces") : 0
            Layout.column: status_Bar.vertical ? 0 : status_Bar.slotOf("workspaces")
            Layout.alignment: status_Bar.crossAlign
            Layout.rightMargin: status_Bar.vertical ? 0 : badges.neighborShift
            vertical: status_Bar.vertical
            active: status_Bar.activeWorkspace
            count: status_Bar.workspaceCount
            maxLength: status_Bar.badgesRoom
            occupied: status_Bar.occupiedWorkspaces
            onRequested: workspace => status_Bar.workspaceRequested(workspace)
        }

        Pill {
            id: activeWindow
            parent: status_Bar.zoneItem("activeWindow")
            Layout.row: status_Bar.vertical ? status_Bar.slotOf("activeWindow") : 0
            Layout.column: status_Bar.vertical ? 0 : status_Bar.slotOf("activeWindow")
            visible: !status_Bar.vertical && status_Bar.shows("activeWindow")
            Layout.alignment: status_Bar.crossAlign
            cornerRadius: mt.radiusBadge
            ringWidth: 5
            transform: Translate { y: status_Bar.liftY }

            readonly property bool hasWindow: status_Bar.windowApp !== "" || status_Bar.windowTitle !== ""
            readonly property string line1: !hasWindow
                ? "Desktop " + status_Bar.activeWorkspace
                : (status_Bar.windowApp !== "" ? status_Bar.windowApp : status_Bar.windowTitle)
            readonly property string line2: (hasWindow
                && status_Bar.windowApp !== ""
                && status_Bar.windowTitle !== ""
                && status_Bar.windowTitle.toLowerCase() !== status_Bar.windowApp.toLowerCase())
                ? status_Bar.windowTitle : ""

            implicitWidth: Math.min(status_Bar.maxActiveWindowWidth,
                Math.max(status_Bar.capsule ? 80 : 140,
                    Math.max(awLine1.implicitWidth, awLine2.visible ? awLine2.implicitWidth : 0) + (status_Bar.capsule ? 20 : 30)))

            fillGradient: Gradient {
                GradientStop { position: 0.0; color: mt.accentMint }
                GradientStop { position: 1.0; color: mt.accentCyan }
            }

            Behavior on implicitWidth { NumberAnimation { duration: 150; easing.type: Easing.OutQuad } }

            Column {
                anchors.centerIn: parent
                width: parent.width - 20
                spacing: 1

                Text {
                    id: awLine1
                    width: parent.width
                    height: lineHeight
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                    maximumLineCount: 1
                    wrapMode: Text.NoWrap
                    textFormat: Text.PlainText
                    lineHeightMode: Text.FixedHeight
                    lineHeight: Math.round(font.pixelSize * 1.25)
                    text: activeWindow.line1.replace(/\s+/g, " ")
                    color: status_Bar.capsule ? mt.capInk : mt.colBorder
                    font.family: mt.fontFamily
                    font.pixelSize: status_Bar.capsule ? mt.fontSizeInfo : (activeWindow.hasWindow ? mt.fontSizeTitle : mt.fontSizeIdle)
                    font.weight: Font.ExtraBold
                    font.styleName: mt.fontStyleBold
                }

                Text {
                    id: awLine2
                    width: parent.width
                    height: lineHeight
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                    maximumLineCount: 1
                    wrapMode: Text.NoWrap
                    textFormat: Text.PlainText
                    lineHeightMode: Text.FixedHeight
                    lineHeight: Math.round(font.pixelSize * 1.25)
                    visible: activeWindow.line2 !== "" && !status_Bar.capsule
                    text: activeWindow.line2.replace(/\s+/g, " ")
                    color: mt.colSubText
                    font.family: mt.fontFamily
                    font.pixelSize: mt.fontSizeSub
                    font.weight: Font.Medium
                }
            }
        }

        ActiveWindowKey {
            id: activeWindowKey
            parent: status_Bar.zoneItem("activeWindow")
            Layout.row: status_Bar.vertical ? status_Bar.slotOf("activeWindow") : 0
            Layout.column: status_Bar.vertical ? 0 : status_Bar.slotOf("activeWindow")
            Layout.alignment: status_Bar.crossAlign
            visible: status_Bar.vertical && status_Bar.shows("activeWindow")
            hasWindow: activeWindow.hasWindow
            app: status_Bar.windowApp !== "" ? status_Bar.windowApp : status_Bar.windowTitle
            title: status_Bar.windowApp !== "" ? status_Bar.windowTitle : ""
            icon: status_Bar.windowIcon
            workspace: status_Bar.activeWorkspace
            barPosition: status_Bar.barPosition
        }

        WeatherIndicator {
            id: weatherIndicator
            parent: status_Bar.zoneItem("weather")
            Layout.row: status_Bar.vertical ? status_Bar.slotOf("weather") : 0
            Layout.column: status_Bar.vertical ? 0 : status_Bar.slotOf("weather")
            visible: !status_Bar.tighterV && status_Bar.shows("weather")
            vertical: status_Bar.vertical
            Layout.alignment: status_Bar.crossAlign
            transform: Translate { y: status_Bar.liftY }
            barPosition: status_Bar.barPosition
            onClicked: status_Bar.weatherClicked()
        }

        Item {
            id: mediaSlot
            parent: status_Bar.zoneItem("media")
            Layout.row: status_Bar.vertical ? status_Bar.slotOf("media") : 0
            Layout.column: status_Bar.vertical ? 0 : status_Bar.slotOf("media")
            Layout.alignment: status_Bar.crossAlign
            visible: status_Bar.shows("media")
            implicitWidth: media.width
            implicitHeight: media.implicitHeight
            transform: Translate { y: status_Bar.liftY }

            MediaPill {
                id: media
                vertical: status_Bar.vertical
                active: status_Bar.mediaActive
                playing: status_Bar.mediaPlaying
                title: status_Bar.mediaTitle
                artist: status_Bar.mediaArtist
                artUrl: status_Bar.mediaArtUrl
                visualizerStyle: "wave"
                points: AudioSpectrumSource.levels
                maxWidth: status_Bar.maxMediaWidth
                minWidth: status_Bar.mediaActive ? Math.min(200, status_Bar.maxMediaWidth) : 48
                leftOpensPanel: true
                volumeAvailable: status_Bar.mediaVolumeAvailable
                onToggleRequested: status_Bar.mediaToggleRequested()
                onNextRequested: status_Bar.mediaNextRequested()
                onPreviousRequested: status_Bar.mediaPreviousRequested()
                onPanelRequested: status_Bar.mediaPopupClicked()
                onScrolled: delta => QuickSource.setSpeakerVolume(Math.max(0, Math.min(100, QuickSource.speakerVolume + (delta > 0 ? 5 : -5))))
            }
        }

        StatusPill {
            id: statusPill
            parent: status_Bar.zoneItem("status")
            Layout.row: status_Bar.vertical ? status_Bar.slotOf("status") : 0
            Layout.column: status_Bar.vertical ? 0 : status_Bar.slotOf("status")
            Layout.alignment: status_Bar.crossAlign
            transform: Translate { y: status_Bar.liftY }
            vertical: status_Bar.vertical
            barPosition: status_Bar.barPosition
            networkShown: status_Bar.statusNetworkShown && status_Bar.shows("status")
            networkIcon: status_Bar.statusNetworkIcon
            networkState: status_Bar.statusNetworkState
            bluetoothShown: status_Bar.statusBluetoothShown && status_Bar.shows("status")
            bluetoothIcon: status_Bar.statusBluetoothIcon
            bluetoothState: status_Bar.statusBluetoothState
            speakerShown: status_Bar.statusSpeakerShown && status_Bar.shows("status")
            speakerIcon: status_Bar.statusSpeakerIcon
            speakerState: status_Bar.statusSpeakerState
            detailText: status_Bar.statusDetail
            onClicked: status_Bar.statusClicked()
        }

        DateIndicator {
            id: dateIndicator
            parent: status_Bar.zoneItem("date")
            Layout.row: status_Bar.vertical ? status_Bar.slotOf("date") : 0
            Layout.column: status_Bar.vertical ? 0 : status_Bar.slotOf("date")
            visible: status_Bar.shows("date")
            vertical: status_Bar.vertical
            Layout.alignment: status_Bar.crossAlign
            transform: Translate { y: status_Bar.liftY }
            text: status_Bar.dateText
            barPosition: status_Bar.barPosition
            onClicked: status_Bar.dateClicked()
        }

        Item {
            id: clockButton
            parent: status_Bar.zoneItem("clock")
            Layout.row: status_Bar.vertical ? status_Bar.slotOf("clock") : 0
            Layout.column: status_Bar.vertical ? 0 : status_Bar.slotOf("clock")
            visible: status_Bar.shows("clock")
            Layout.alignment: status_Bar.crossAlign
            implicitWidth: clockPill.implicitWidth
            implicitHeight: clockPill.implicitHeight
            transform: Translate { y: status_Bar.liftY }

            Pill {
                id: clockPill
                anchors.fill: parent
                cornerRadius: mt.radiusBadge
                pressed: clockArea.pressed
                fillColor: mt.chipFill
                ringWidth: 6
                implicitWidth: status_Bar.vertical ? clockPill.barVWidth : cLabel.implicitWidth + (status_Bar.capsule ? 20 : 28)
                implicitHeight: status_Bar.vertical ? clockColumn.implicitHeight + (clockPill.capsule ? 16 : 24) : mt.pillHeight + mt.shadowOffset

                Column {
                    id: clockColumn
                    visible: status_Bar.vertical
                    anchors.centerIn: parent

                    Repeater {
                        model: status_Bar.clockText.split(" ")
                        Text {
                            required property string modelData
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: modelData
                            color: mt.chipInk
                            font.family: mt.fontFamily
                            font.pixelSize: mt.fontSizeInfo
                            font.weight: Font.ExtraBold
                            font.styleName: mt.fontStyleBold
                        }
                    }
                }

                Text {
                    id: cLabel
                    visible: !status_Bar.vertical
                    anchors.centerIn: parent
                    text: status_Bar.clockText
                    color: mt.chipInk
                    font.family: mt.fontFamily
                    font.pixelSize: status_Bar.capsule ? mt.capClockSize : mt.fontSizeInfo
                    font.weight: Font.ExtraBold
                    font.styleName: mt.fontStyleBold
                }
            }

            MouseArea {
                id: clockArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: status_Bar.dateClicked()
            }
        }

        ResourceIndicator {
            id: resourceIndicator
            parent: status_Bar.zoneItem("resources")
            Layout.row: status_Bar.vertical ? status_Bar.slotOf("resources") : 0
            Layout.column: status_Bar.vertical ? 0 : status_Bar.slotOf("resources")
            visible: !status_Bar.tighterV && status_Bar.shows("resources")
            vertical: status_Bar.vertical
            Layout.alignment: status_Bar.crossAlign
            transform: Translate { y: status_Bar.liftY }
            cpuPercent: status_Bar.cpuPercent
            memPercent: status_Bar.memPercent
            memUsedLabel: status_Bar.memUsedLabel
            memTotalLabel: status_Bar.memTotalLabel
            barPosition: status_Bar.barPosition
            onClicked: status_Bar.resourcesClicked()
        }

        BatteryIndicator {
            id: batteryIndicator
            parent: status_Bar.zoneItem("battery")
            Layout.row: status_Bar.vertical ? status_Bar.slotOf("battery") : 0
            Layout.column: status_Bar.vertical ? 0 : status_Bar.slotOf("battery")
            visible: available && status_Bar.shows("battery")
            vertical: status_Bar.vertical
            Layout.alignment: status_Bar.crossAlign
            transform: Translate { y: status_Bar.liftY }
            available: status_Bar.batteryAvailable
            percentage: status_Bar.batteryPercentage
            isCharging: status_Bar.batteryCharging
            isLow: status_Bar.batteryLow
            barPosition: status_Bar.barPosition
            onClicked: status_Bar.batteryClicked()
        }

        UtilButtons {
            id: utilKeys
            parent: status_Bar.zoneItem("utilButtons")
            Layout.row: status_Bar.vertical ? status_Bar.slotOf("utilButtons") : 0
            Layout.column: status_Bar.vertical ? 0 : status_Bar.slotOf("utilButtons")
            visible: status_Bar.shows("utilButtons")
            vertical: status_Bar.vertical
            compactV: status_Bar.tighterV
            barPosition: status_Bar.barPosition
            Layout.alignment: status_Bar.crossAlign
            transform: Translate { y: status_Bar.liftY }
        }
    }
}
