import QtQuick
import Common

Item {
    id: root

    readonly property var mt: Theme.of(Window.window)

    property bool open: false
    property real renderScale: 1

    property int currentIndex: -1
    property int hoveredIndex: -1
    property bool keyNav: false
    readonly property int labelIndex: hoveredIndex >= 0 ? hoveredIndex
                                      : (keyNav ? currentIndex : -1)

    function move(delta) {
        const n = rows.length
        if (n === 0)
            return
        keyNav = true
        currentIndex = currentIndex < 0
            ? (delta > 0 ? 0 : n - 1)
            : (currentIndex + delta + n) % n
    }

    function trigger(i) {
        switch (i) {
            case 0: root.powerOffRequested(); break
            case 1: root.rebootRequested(); break
            case 2: root.logoutRequested(); break
            case 3: root.lockRequested(); break
            case 4: root.sleepRequested(); break
            case 5: root.hibernateRequested(); break
            case 6: root.firmwareRequested(); break
            case 7: root.systemMonitorRequested(); break
        }
        root.dismissRequested()
    }

    onOpenChanged: {
        if (root.open) {
            root.currentIndex = -1
            root.hoveredIndex = -1
            root.keyNav = false
        }
    }

    signal powerOffRequested()
    signal rebootRequested()
    signal logoutRequested()
    signal lockRequested()
    signal sleepRequested()
    signal hibernateRequested()
    signal firmwareRequested()
    signal systemMonitorRequested()
    signal dismissRequested()

    readonly property var rows: [
        "power_settings_circle",
        "restart_alt",
        "exit_to_app",
        "lock",
        "moon_stars",
        "hotel",
        "settings_b_roll",
        "monitor_heart"
    ]

    readonly property var labels: [
        "Shut down",
        "Reboot",
        "Sign out",
        "Lock",
        "Sleep",
        "Hibernate",
        "Restore Explorer",
        "Task Manager"
    ]

    readonly property real pillWidth: 90
    readonly property real pillHeight: 82
    readonly property real rowSpacing: mt.quickFlat ? 89 : 83
    readonly property real pillFixedRight: 79

    implicitWidth: pillWidth
    implicitHeight: rows.length > 0 ? (rows.length - 1) * rowSpacing + pillHeight : 0

    visible: opacity > 0
    opacity: root.open ? 1 : 0
    Behavior on opacity {
        NumberAnimation { duration: root.open ? 120 : 260 }
    }

    readonly property real canvasMargin: 20
    readonly property real canvasLeft: pillFixedRight - pillWidth - canvasMargin
    readonly property real canvasTop: -canvasMargin
    readonly property real canvasWidth: pillWidth + canvasMargin * 2
    readonly property real canvasHeight: root.implicitHeight + canvasMargin * 2

    Item {
        id: canvas
        x: root.canvasLeft
        y: root.canvasTop
        width: root.canvasWidth
        height: root.canvasHeight

        layer.enabled: root.visible
        layer.smooth: true
        layer.samples: 8
        layer.mipmap: true
        layer.textureSize: Qt.size(Math.ceil(width * root.renderScale * 2),
                                    Math.ceil(height * root.renderScale * 2))

        Item {
            id: content
            x: -root.canvasLeft
            y: -root.canvasTop

            Repeater {
                model: root.rows

                delegate: QuickToggle {
                    id: item
                    flatShadow: mt.quickFlat

                    required property string modelData
                    required property int index

                    kind: "round"
                    icon: modelData
                    elevated: true
                    hoverLights: true
                    hoverElevates: true
                    anchorRight: true
                    keyFocused: root.currentIndex === item.index
                    onHoveredChanged: {
                        if (item.hovered) {
                            root.currentIndex = item.index
                            root.hoveredIndex = item.index
                            root.keyNav = false
                        } else if (root.hoveredIndex === item.index) {
                            root.hoveredIndex = -1
                        }
                    }
                    x: 0
                    y: index * root.rowSpacing

                    property real popT: root.open ? 1 : 0

                    visible: opacity > 0.02
                    opacity: popT
                    transform: [
                        Scale {
                            origin.x: item.width / 2
                            origin.y: item.height / 2
                            xScale: 0.35 + 0.65 * item.popT
                            yScale: 0.35 + 0.65 * item.popT
                        },
                        Translate {
                            x: (1 - item.popT) * -56
                            y: Math.sin(item.popT * Math.PI) * (1 - item.popT)
                               * (item.index % 2 === 0 ? 18 : -18)
                        }
                    ]

                    SequentialAnimation {
                        id: popAnim
                        PauseAnimation { duration: item.index * 38 }
                        NumberAnimation {
                            target: item
                            property: "popT"
                            to: 1
                            duration: 320
                            easing.type: Easing.OutBack
                            easing.overshoot: 1.7
                        }
                    }

                    SequentialAnimation {
                        id: hideAnim
                        PauseAnimation { duration: (root.rows.length - 1 - item.index) * 26 }
                        NumberAnimation {
                            target: item
                            property: "popT"
                            to: 0
                            duration: 150
                            easing.type: Easing.InCubic
                        }
                    }

                    Connections {
                        target: root
                        function onOpenChanged() {
                            if (root.open) {
                                hideAnim.stop()
                                item.popT = 0
                                popAnim.restart()
                            } else {
                                popAnim.stop()
                                hideAnim.restart()
                            }
                        }
                    }

                    onClicked: root.trigger(item.index)
                }
            }
        }
    }

    Rectangle {
        id: label
        readonly property int idx: root.labelIndex
        property int shownIdx: 0
        onIdxChanged: if (idx >= 0) shownIdx = idx

        x: root.pillFixedRight + 14
        y: shownIdx * root.rowSpacing + 75 / 2 - height / 2
        z: 10
        width: labelText.width + 28
        height: labelText.implicitHeight + 16
        radius: 12
        color: mt.chipFill
        border.width: 3
        border.color: mt.pLine
        antialiasing: true

        opacity: root.open && idx >= 0 ? 1 : 0
        visible: opacity > 0
        Behavior on opacity {
            NumberAnimation { duration: 90 }
        }

        layer.enabled: visible
        layer.smooth: true
        layer.samples: 4
        layer.mipmap: true
        layer.textureSize: Qt.size(Math.ceil(width * root.renderScale * 2),
                                    Math.ceil(height * root.renderScale * 2))

        Text {
            id: labelText
            anchors.centerIn: parent
            width: Math.min(implicitWidth, 300)
            wrapMode: Text.WordWrap
            text: root.labels[label.shownIdx] || ""
            color: mt.chipInk
            font.family: mt.fontFamily
            font.pixelSize: 14
            font.weight: Font.Black
        }
    }
}
