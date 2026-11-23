import QtQuick
import QtQuick.Controls
import Common
import Common.widgets

Item {
    id: root

    readonly property var mt: Theme.of(Window.window)
    readonly property bool classicPanel: true

    property real renderScale: 1
    width: 1440
    height: 760

    property bool lockMode: false
    property string searchQuery: ""
    property string sortMode: "Newest"
    property bool gridView: false
    property string justSet: ""

    readonly property var allEntries: WallpaperService ? WallpaperService.entries : []
    readonly property var filteredEntries: {
        const q = root.searchQuery.trim().toLowerCase()
        let arr = []
        for (let i = 0; i < allEntries.length; i++) {
            const item = allEntries[i]
            if (q === "" || String(item.title || "").toLowerCase().indexOf(q) >= 0
                    || String(item.fileName || "").toLowerCase().indexOf(q) >= 0) {
                arr.push(item)
            }
        }
        if (root.sortMode === "A-Z") {
            arr.sort((a, b) => String(a.title).localeCompare(String(b.title)))
        } else if (root.sortMode === "Z-A") {
            arr.sort((a, b) => String(b.title).localeCompare(String(a.title)))
        }
        return arr
    }

    property int currentIndex: 0
    property real wheelPos: 0
    Behavior on wheelPos {
        NumberAnimation {
            duration: 320
            easing.type: Easing.OutBack
            easing.overshoot: 1.15
        }
    }

    onFilteredEntriesChanged: {
        if (currentIndex >= filteredEntries.length) {
            currentIndex = Math.max(0, filteredEntries.length - 1)
            wheelPos = currentIndex
        }
    }

    function stepWheel(delta) {
        const n = filteredEntries.length
        if (n <= 0)
            return
        currentIndex = (currentIndex + delta + n) % n
        wheelPos = currentIndex
    }

    function selectIndex(idx) {
        const n = filteredEntries.length
        if (idx < 0 || idx >= n)
            return
        currentIndex = idx
        wheelPos = idx
    }

    function applyCurrentDesktop() {
        if (currentIndex < 0 || currentIndex >= filteredEntries.length)
            return
        const entry = filteredEntries[currentIndex]
        if (entry && entry.filePath) {
            WallpaperService.setWallpaper(entry.filePath, "")
            root.justSet = "desktop"
            justSetTimer.restart()
        }
    }

    function applyCurrentLock() {
        if (currentIndex < 0 || currentIndex >= filteredEntries.length)
            return
        const entry = filteredEntries[currentIndex]
        if (entry && entry.filePath) {
            SettingsService.set("wallpaper.lockPath", entry.filePath)
            root.justSet = "lock"
            justSetTimer.restart()
        }
    }

    Timer {
        id: justSetTimer
        interval: 1500
        onTriggered: root.justSet = ""
    }

    // ===== Dotted Scrapbook Board Background inside Phone Screen =====
    Rectangle {
        x: mt.phoneScreenInset
        y: mt.phoneScreenInset
        width: root.width - 2 * mt.phoneScreenInset
        height: root.height - 2 * mt.phoneScreenInset
        radius: mt.phoneCornerRadius - mt.phoneScreenInset
        color: mt.pSurface
        clip: true

        // Subtle scrapbook dot grid background
        Canvas {
            anchors.fill: parent
            anchors.topMargin: 108
            opacity: 0.45
            onPaint: {
                const ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)
                ctx.fillStyle = mt.pSurfaceDivider
                const step = 24
                for (let y = 12; y < height; y += step) {
                    for (let x = 12; x < width; x += step) {
                        ctx.beginPath()
                        ctx.arc(x, y, 2.2, 0, Math.PI * 2)
                        ctx.fill()
                    }
                }
            }
        }
    }

    // ===== TOP HEADER BAR (x: 48..1392, y: 42) =====
    Item {
        id: topBar
        x: 48
        y: 42
        width: root.width - 96
        height: 58

        // Back button
        Pill {
            id: backBtn
            x: 0
            anchors.verticalCenter: parent.verticalCenter
            width: 52
            height: 50
            cornerRadius: 25
            borderWidth: 3
            ringWidth: 3
            shadowDx: 0
            shadowDy: 3
            fillColor: backArea.containsMouse ? mt.accentMint : mt.pSurface
            pressed: backArea.pressed

            MaterialIcon {
                anchors.centerIn: parent
                name: "arrow_back"
                size: 24
                color: mt.pInk
            }

            MouseArea {
                id: backArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: ShellController.openQuickPanel()
            }
        }

        // Title "Wallpapers"
        Text {
            id: titleText
            anchors.left: backBtn.right
            anchors.leftMargin: 18
            anchors.verticalCenter: parent.verticalCenter
            text: "Wallpapers"
            color: mt.pInk
            font.family: mt.fontFamily
            font.pixelSize: 28
            font.weight: Font.ExtraBold
            font.styleName: mt.fontStyleBold
        }

        // Segmented "Desktop | Lockscreen" Pill Switch
        Rectangle {
            id: modeSwitch
            anchors.left: titleText.right
            anchors.leftMargin: 22
            anchors.verticalCenter: parent.verticalCenter
            width: 244
            height: 48
            radius: 24
            color: mt.pSurfaceHover
            border.width: 3
            border.color: mt.pLine

            Row {
                anchors.centerIn: parent
                spacing: 4

                Repeater {
                    model: [
                        { id: false, label: "Desktop", icon: "desktop_windows" },
                        { id: true,  label: "Lockscreen", icon: "lock" }
                    ]

                    delegate: Rectangle {
                        required property var modelData
                        readonly property bool active: root.lockMode === modelData.id
                        width: 114
                        height: 38
                        radius: 19
                        color: active ? mt.accentCyan : "transparent"
                        border.width: active ? 2 : 0
                        border.color: mt.pLine

                        Row {
                            anchors.centerIn: parent
                            spacing: 6

                            MaterialIcon {
                                anchors.verticalCenter: parent.verticalCenter
                                name: modelData.icon
                                size: 16
                                color: active ? mt.colBorder : mt.pInk
                            }

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
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
                            onClicked: root.lockMode = modelData.id
                        }
                    }
                }
            }
        }

        // Wallpaper Counter
        Text {
            anchors.left: modeSwitch.right
            anchors.leftMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            text: root.filteredEntries.length + " wallpapers"
            color: mt.pInkSub
            font.family: mt.fontFamily
            font.pixelSize: 16
            font.weight: Font.ExtraBold
        }

        // Right controls: Search box, Transition chip, Sort chip, Grid toggle, Browse folder
        Row {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: 10

            // Search box
            Rectangle {
                width: 230
                height: 46
                radius: 23
                color: mt.pSurface
                border.width: 3
                border.color: searchInput.activeFocus ? mt.colBorder : mt.pLine

                Row {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 12
                    spacing: 8

                    MaterialIcon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "search"
                        size: 20
                        color: mt.pInkSub
                    }

                    TextInput {
                        id: searchInput
                        width: parent.width - 42
                        anchors.verticalCenter: parent.verticalCenter
                        color: mt.pInk
                        font.family: mt.fontFamily
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                        clip: true
                        onTextChanged: root.searchQuery = text

                        Text {
                            visible: searchInput.text.length === 0
                            text: "Search wallpapers..."
                            color: mt.pInkSub
                            font: parent.font
                        }
                    }
                }
            }

            // Transition Style Chip
            Pill {
                width: 132
                height: 46
                cornerRadius: 23
                borderWidth: 3
                ringWidth: 3
                shadowDx: 0
                shadowDy: 3
                fillColor: mt.pSurface

                Row {
                    anchors.centerIn: parent
                    spacing: 6

                    MaterialIcon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "auto_awesome"
                        size: 18
                        color: mt.pInk
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: WallpaperService ? WallpaperService.transitionStyle : "sekaiRing"
                        color: mt.pInk
                        font.family: mt.fontFamily
                        font.pixelSize: 14
                        font.weight: Font.ExtraBold
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (!WallpaperService)
                            return
                        const styles = WallpaperService.availableStyles
                        const idx = styles.indexOf(WallpaperService.transitionStyle)
                        WallpaperService.setTransitionStyle(styles[(idx + 1) % styles.length])
                    }
                }
            }

            // Sort Chip ("Newest" / "A-Z" / "Z-A")
            Pill {
                width: 112
                height: 46
                cornerRadius: 23
                borderWidth: 3
                ringWidth: 3
                shadowDx: 0
                shadowDy: 3
                fillColor: mt.accentMint

                Row {
                    anchors.centerIn: parent
                    spacing: 6

                    MaterialIcon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "swap_vert"
                        size: 18
                        color: mt.colBorder
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.sortMode
                        color: mt.colBorder
                        font.family: mt.fontFamily
                        font.pixelSize: 14
                        font.weight: Font.ExtraBold
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (root.sortMode === "Newest") root.sortMode = "A-Z"
                        else if (root.sortMode === "A-Z") root.sortMode = "Z-A"
                        else root.sortMode = "Newest"
                    }
                }
            }

            // Wheel / Grid toggle
            Pill {
                width: 48
                height: 46
                cornerRadius: 23
                borderWidth: 3
                ringWidth: 3
                shadowDx: 0
                shadowDy: 3
                fillColor: root.gridView ? mt.accentCyan : mt.pSurface

                MaterialIcon {
                    anchors.centerIn: parent
                    name: root.gridView ? "grid_view" : "view_carousel"
                    size: 20
                    color: root.gridView ? mt.colBorder : mt.pInk
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.gridView = !root.gridView
                }
            }

            // Browse file button
            Pill {
                width: 48
                height: 46
                cornerRadius: 23
                borderWidth: 3
                ringWidth: 3
                shadowDx: 0
                shadowDy: 3
                fillColor: mt.pSurface

                MaterialIcon {
                    anchors.centerIn: parent
                    name: "folder_open"
                    size: 20
                    color: mt.pInk
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: WallpaperService.pickWallpaperDialog("")
                }
            }
        }
    }

    // ===== ARCHED FERRIS-WHEEL CAROUSEL (y: 112..624) =====
    Item {
        id: wheelStage
        visible: !root.gridView
        x: 36
        y: 112
        width: root.width - 72
        height: 512
        clip: true

        readonly property real wheelR: 920
        readonly property real pivotY: 26
        readonly property real cx: width / 2
        readonly property real hubY: pivotY + wheelR
        readonly property real stepDeg: 21
        readonly property real cardW: 440
        readonly property real cardH: 275
        readonly property real hang: 42

        // Arched Ferris-Wheel Track + Beat Diamonds
        Canvas {
            id: trackCanvas
            anchors.fill: parent
            onPaint: {
                const ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)
                const cx = wheelStage.cx
                const cy = wheelStage.hubY
                const r = wheelStage.wheelR

                // Outer track shadow
                ctx.beginPath()
                ctx.arc(cx, cy + 4, r, -Math.PI * 0.85, -Math.PI * 0.15)
                ctx.lineWidth = 22
                ctx.strokeStyle = mt.pLine
                ctx.stroke()

                // White track band
                ctx.beginPath()
                ctx.arc(cx, cy, r, -Math.PI * 0.85, -Math.PI * 0.15)
                ctx.lineWidth = 16
                ctx.strokeStyle = mt.pSurface
                ctx.stroke()

                // Inner accent stripe
                ctx.beginPath()
                ctx.arc(cx, cy, r, -Math.PI * 0.85, -Math.PI * 0.15)
                ctx.lineWidth = 6
                ctx.strokeStyle = mt.pSurfaceDivider
                ctx.stroke()
            }
        }

        // Diamond studs along the arched track
        Repeater {
            model: [-3.5, -2.5, -1.5, -0.5, 0.5, 1.5, 2.5, 3.5]

            delegate: Item {
                required property real modelData
                readonly property real deg: modelData * wheelStage.stepDeg
                readonly property real rad: deg * Math.PI / 180
                readonly property bool isCenterFlank: Math.abs(modelData) < 1.0

                x: wheelStage.cx + Math.sin(rad) * wheelStage.wheelR - width / 2
                y: wheelStage.hubY - Math.cos(rad) * wheelStage.wheelR - height / 2
                width: isCenterFlank ? 22 : 16
                height: isCenterFlank ? 22 : 16
                rotation: 45

                Rectangle {
                    anchors.fill: parent
                    radius: 3
                    color: isCenterFlank ? mt.accentCyan : mt.pSurface
                    border.width: 3
                    border.color: mt.colBorder
                }
            }
        }

        // Gondola Cards along the Ferris Wheel
        Repeater {
            model: [-3, -2, -1, 0, 1, 2, 3]

            delegate: Item {
                id: gondola
                required property int modelData

                readonly property int total: root.filteredEntries.length
                readonly property int itemIdx: {
                    if (total <= 0)
                        return -1
                    if (total >= 5) {
                        return ((root.currentIndex + modelData) % total + total) % total
                    }
                    const raw = root.currentIndex + modelData
                    return (raw >= 0 && raw < total) ? raw : -1
                }
                readonly property var entry: itemIdx >= 0 ? root.filteredEntries[itemIdx] : null
                visible: entry !== null

                readonly property real rel: modelData
                readonly property real deg: rel * wheelStage.stepDeg
                readonly property real rad: deg * Math.PI / 180
                readonly property real boltX: wheelStage.cx + Math.sin(rad) * wheelStage.wheelR
                readonly property real boltY: wheelStage.hubY - Math.cos(rad) * wheelStage.wheelR
                readonly property real dist: Math.abs(rel)
                readonly property real cardScale: Math.max(0.56, 1.0 - dist * 0.14)

                x: boltX - width / 2
                y: boltY
                z: 10 - dist
                width: wheelStage.cardW
                height: wheelStage.cardH + wheelStage.hang + 30
                scale: cardScale
                transformOrigin: Item.Top
                opacity: Math.max(0.35, 1.0 - dist * 0.18)

                Behavior on x { NumberAnimation { duration: 280; easing.type: Easing.OutCubic } }
                Behavior on y { NumberAnimation { duration: 280; easing.type: Easing.OutCubic } }
                Behavior on scale { NumberAnimation { duration: 280; easing.type: Easing.OutCubic } }

                // Two angled hanger wires from track bolt to picture top
                Canvas {
                    width: parent.width
                    height: wheelStage.hang + 12
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.clearRect(0, 0, width, height)
                        const midX = width / 2
                        ctx.strokeStyle = mt.colBorder
                        ctx.lineWidth = 3
                        ctx.beginPath()
                        ctx.moveTo(midX, 4)
                        ctx.lineTo(midX - 92, wheelStage.hang + 4)
                        ctx.moveTo(midX, 4)
                        ctx.lineTo(midX + 92, wheelStage.hang + 4)
                        ctx.stroke()
                    }
                }

                // Track bolt circle
                Rectangle {
                    x: parent.width / 2 - 8
                    y: -6
                    width: 16
                    height: 16
                    radius: 8
                    color: rel === 0 ? mt.accentMint : mt.pSurface
                    border.width: 3
                    border.color: mt.colBorder
                }

                // Framed Picture Card
                Rectangle {
                    id: frameShadow
                    x: 5
                    y: wheelStage.hang + 5
                    width: wheelStage.cardW
                    height: wheelStage.cardH
                    radius: 26
                    color: mt.colBorder
                }

                Rectangle {
                    id: frameCard
                    x: 0
                    y: wheelStage.hang
                    width: wheelStage.cardW
                    height: wheelStage.cardH
                    radius: 26
                    color: mt.pSurface
                    border.width: rel === 0 ? 4 : 3
                    border.color: mt.colBorder
                    clip: true

                    Rectangle {
                        id: innerFrame
                        anchors.fill: parent
                        anchors.margins: 10
                        radius: 18
                        color: mt.pSurfaceHover
                        border.width: 2
                        border.color: mt.pLine
                        clip: true

                        Image {
                            anchors.fill: parent
                            source: gondola.entry ? ("file:///" + gondola.entry.filePath) : ""
                            fillMode: Image.PreserveAspectCrop
                            sourceSize.width: 640
                            sourceSize.height: 400
                            asynchronous: true
                            smooth: true
                        }
                    }

                    // Active Desktop / Lock badge on card
                    Rectangle {
                        readonly property bool isDesktop: gondola.entry && WallpaperService
                            && gondola.entry.filePath === WallpaperService.defaultPath
                        visible: isDesktop
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: 18
                        width: 108
                        height: 30
                        radius: 15
                        color: mt.accentMint
                        border.width: 3
                        border.color: mt.colBorder

                        Row {
                            anchors.centerIn: parent
                            spacing: 4

                            MaterialIcon {
                                anchors.verticalCenter: parent.verticalCenter
                                name: "check"
                                size: 15
                                weight: 700
                                color: mt.colBorder
                            }

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: "APPLIED"
                                color: mt.colBorder
                                font.family: mt.fontFamily
                                font.pixelSize: 12
                                font.weight: Font.ExtraBold
                            }
                        }
                    }
                }

                // Pastel Washi Tape Strips over wire attachment corners
                Rectangle {
                    x: wheelStage.cardW / 2 - 122
                    y: wheelStage.hang - 7
                    width: 58
                    height: 18
                    radius: 4
                    rotation: -14
                    color: mt.accentMint
                    border.width: 2
                    border.color: mt.colBorder
                    opacity: 0.92
                }

                Rectangle {
                    x: wheelStage.cardW / 2 + 64
                    y: wheelStage.hang - 7
                    width: 58
                    height: 18
                    radius: 4
                    rotation: 14
                    color: mt.accentCyan
                    border.width: 2
                    border.color: mt.colBorder
                    opacity: 0.92
                }

                MouseArea {
                    anchors.fill: frameCard
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (gondola.rel === 0) {
                            if (root.lockMode)
                                root.applyCurrentLock()
                            else
                                root.applyCurrentDesktop()
                        } else {
                            root.stepWheel(gondola.rel)
                        }
                    }
                }
            }
        }

        // Center Wallpaper Title Sticker & Tags (below center gondola)
        Column {
            anchors.horizontalCenter: parent.horizontalCenter
            y: 356
            spacing: 8
            visible: root.filteredEntries.length > 0

            readonly property var cur: (root.currentIndex >= 0 && root.currentIndex < root.filteredEntries.length)
                ? root.filteredEntries[root.currentIndex]
                : null

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: parent.cur ? parent.cur.title : ""
                color: mt.pInk
                font.family: mt.fontFamily
                font.pixelSize: 28
                font.weight: Font.Black
                font.styleName: mt.fontStyleBold
            }

            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 8

                Rectangle {
                    width: resText.implicitWidth + 20
                    height: 28
                    radius: 14
                    color: mt.pSurface
                    border.width: 2
                    border.color: mt.pLine

                    Text {
                        id: resText
                        anchors.centerIn: parent
                        text: parent.parent.parent.cur ? parent.parent.parent.cur.resolution : "HD"
                        color: mt.pInk
                        font.family: mt.fontFamily
                        font.pixelSize: 12
                        font.weight: Font.ExtraBold
                    }
                }

                Rectangle {
                    width: folderText.implicitWidth + 20
                    height: 28
                    radius: 14
                    color: mt.accentMint
                    border.width: 2
                    border.color: mt.colBorder

                    Text {
                        id: folderText
                        anchors.centerIn: parent
                        text: parent.parent.parent.cur ? parent.parent.parent.cur.folder : "Wallpapers"
                        color: mt.colBorder
                        font.family: mt.fontFamily
                        font.pixelSize: 12
                        font.weight: Font.ExtraBold
                    }
                }
            }
        }

        MouseArea {
            anchors.fill: parent
            z: -1
            onWheel: wheel => {
                if (wheel.angleDelta.y > 0)
                    root.stepWheel(-1)
                else if (wheel.angleDelta.y < 0)
                    root.stepWheel(1)
            }
        }
    }

    // ===== OPTIONAL GRID VIEW (when grid toggle is active) =====
    Flickable {
        id: gridStage
        visible: root.gridView
        x: 56
        y: 118
        width: root.width - 112
        height: 500
        contentWidth: width
        contentHeight: gridFlow.implicitHeight
        clip: true

        Flow {
            id: gridFlow
            width: gridStage.width
            spacing: 18

            Repeater {
                model: root.filteredEntries

                delegate: Rectangle {
                    required property var modelData
                    required property int index
                    readonly property bool selected: index === root.currentIndex
                    width: 248
                    height: 162
                    radius: 20
                    color: mt.pSurface
                    border.width: selected ? 4 : 3
                    border.color: selected ? mt.accentCyan : mt.colBorder
                    clip: true

                    Image {
                        anchors.fill: parent
                        anchors.margins: 6
                        source: "file:///" + modelData.filePath
                        fillMode: Image.PreserveAspectCrop
                        sourceSize.width: 360
                        sourceSize.height: 220
                        asynchronous: true
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: 10
                        height: 28
                        radius: 14
                        color: Qt.rgba(1, 1, 1, 0.9)
                        border.width: 2
                        border.color: mt.pLine

                        Text {
                            anchors.centerIn: parent
                            width: parent.width - 16
                            elide: Text.ElideRight
                            horizontalAlignment: Text.AlignHCenter
                            text: modelData.title
                            color: mt.pInk
                            font.family: mt.fontFamily
                            font.pixelSize: 12
                            font.weight: Font.ExtraBold
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            root.selectIndex(index)
                            root.applyCurrentDesktop()
                        }
                    }
                }
            }
        }
    }

    // ===== BOTTOM ACTION PILLS: < Previous | Set as Desktop | Set as Lockscreen | Next > =====
    Row {
        anchors.horizontalCenter: parent.horizontalCenter
        y: 652
        spacing: 14

        // < Previous
        Pill {
            width: 148
            height: 54
            cornerRadius: 27
            borderWidth: 3
            ringWidth: 3
            shadowDx: 0
            shadowDy: 4
            fillColor: prevArea.containsMouse ? mt.pSurfaceHover : mt.pSurface
            pressed: prevArea.pressed

            Row {
                anchors.centerIn: parent
                spacing: 6

                MaterialIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: "chevron_left"
                    size: 22
                    color: mt.pInk
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Previous"
                    color: mt.pInk
                    font.family: mt.fontFamily
                    font.pixelSize: 16
                    font.weight: Font.ExtraBold
                }
            }

            MouseArea {
                id: prevArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.stepWheel(-1)
            }
        }

        // Set as Desktop (lit gradient pill)
        Pill {
            width: 210
            height: 54
            cornerRadius: 27
            borderWidth: 4
            ringWidth: 6
            shadowDx: 0
            shadowDy: 4
            fillColor: mt.accentMint
            pressed: setDeskArea.pressed

            Rectangle {
                anchors.fill: parent
                radius: parent.radius
                gradient: Gradient {
                    GradientStop { position: 0.0; color: mt.accentMint }
                    GradientStop { position: 1.0; color: mt.accentCyan }
                }
            }

            Row {
                anchors.centerIn: parent
                spacing: 8

                MaterialIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: root.justSet === "desktop" ? "check" : "desktop_windows"
                    size: 20
                    color: mt.colBorder
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.justSet === "desktop" ? "Applied!" : "Set as Desktop"
                    color: mt.colBorder
                    font.family: mt.fontFamily
                    font.pixelSize: 16
                    font.weight: Font.ExtraBold
                }
            }

            MouseArea {
                id: setDeskArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.applyCurrentDesktop()
            }
        }

        // Set as Lockscreen
        Pill {
            width: 220
            height: 54
            cornerRadius: 27
            borderWidth: 3
            ringWidth: 3
            shadowDx: 0
            shadowDy: 4
            fillColor: setLockArea.containsMouse ? mt.accentMint : mt.pSurface
            pressed: setLockArea.pressed

            Row {
                anchors.centerIn: parent
                spacing: 8

                MaterialIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: root.justSet === "lock" ? "check" : "lock"
                    size: 20
                    color: mt.pInk
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.justSet === "lock" ? "Applied!" : "Set as Lockscreen"
                    color: mt.pInk
                    font.family: mt.fontFamily
                    font.pixelSize: 16
                    font.weight: Font.ExtraBold
                }
            }

            MouseArea {
                id: setLockArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.applyCurrentLock()
            }
        }

        // Next >
        Pill {
            width: 132
            height: 54
            cornerRadius: 27
            borderWidth: 3
            ringWidth: 3
            shadowDx: 0
            shadowDy: 4
            fillColor: nextArea.containsMouse ? mt.pSurfaceHover : mt.pSurface
            pressed: nextArea.pressed

            Row {
                anchors.centerIn: parent
                spacing: 6

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Next"
                    color: mt.pInk
                    font.family: mt.fontFamily
                    font.pixelSize: 16
                    font.weight: Font.ExtraBold
                }

                MaterialIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: "chevron_right"
                    size: 22
                    color: mt.pInk
                }
            }

            MouseArea {
                id: nextArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.stepWheel(1)
            }
        }
    }
}
