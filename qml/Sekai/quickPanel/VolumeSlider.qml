import QtQuick
import Common
import Common.widgets
import Sekai.phone

Item {
    id: root

    readonly property var mt: Theme.of(Window.window)

    property string label: ""
    property string icon: ""
    property bool muted: false
    property real value: 0
    property bool simple: false
    property real sliderWidth: 200
    property bool flatShadow: false

    signal moved(real value)
    signal iconClicked()
    signal clicked()
    signal rightClicked()

    implicitWidth: sliderWidth
    implicitHeight: root.simple ? 56 : 97

    property bool dragging: false
    property real dragValue: 0
    readonly property real shownValue: dragging ? dragValue : value

    property real litP: root.muted ? 0 : 1

    readonly property color inkNow: root.simple
        ? mt.pInk
        : Qt.tint(mt.pInk, Qt.rgba(mt.colBorder.r, mt.colBorder.g, mt.colBorder.b, litP))
    readonly property color inkSubNow: Qt.rgba(inkNow.r, inkNow.g, inkNow.b, 0.76)

    Behavior on litP {
        NumberAnimation {
            duration: 170
            easing.type: Easing.OutCubic
        }
    }

    property bool keyPulse: false
    property bool suppressPulse: false

    onMutedChanged: {
        if (suppressPulse)
            return
        keyPulse = true
        pulseRelease.restart()
    }

    Timer {
        id: pulseRelease
        interval: 90
        onTriggered: root.keyPulse = false
    }

    Timer {
        id: suppressGrace
        interval: 600
        onTriggered: root.suppressPulse = false
    }

    Pill {
        id: cap
        visible: !root.simple
        anchors.left: parent.left
        anchors.top: parent.top
        width: root.sliderWidth + shadowDx
        height: 73 + shadowDy
        cornerRadius: 40
        borderWidth: 3 + 2 * root.litP
        ringWidth: 3 + 11 * root.litP
        shadowDx: root.flatShadow ? 0 : 3 + 2 * root.litP
        shadowDy: root.flatShadow ? 3 + root.litP : 1 + root.litP
        fillColor: mt.pSurface
        pressed: buttonArea.pressed || root.keyPulse

        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            gradient: Gradient {
                GradientStop { position: 0.0; color: mt.accentMint }
                GradientStop { position: 1.0; color: mt.accentCyan }
            }
            opacity: root.litP
            antialiasing: true
        }

        Item {
            id: capBox
            x: -cap.ringWidth
            y: -cap.ringWidth
            width: cap.capWidth
            height: cap.capHeight

            MaterialIcon {
                id: glyph
                x: 20
                size: 24
                anchors.verticalCenter: parent.verticalCenter
                name: root.icon
                color: root.inkNow
                opacity: root.muted ? 0.4 : 1
            }

            Text {
                id: nameLabel
                anchors.left: glyph.right
                anchors.leftMargin: 12
                anchors.right: pctLabel.left
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                elide: Text.ElideRight
                text: root.label
                color: root.muted ? mt.pInkSub : root.inkNow
                font.family: mt.fontFamily
                font.pixelSize: 18
                font.weight: Font.ExtraBold
                font.styleName: mt.fontStyleBold
            }

            Text {
                id: pctLabel
                anchors.right: parent.right
                anchors.rightMargin: 18
                anchors.verticalCenter: parent.verticalCenter
                text: root.muted ? "MUTE" : Math.round(root.shownValue) + "%"
                color: root.inkSubNow
                font.family: mt.fontFamily
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }
        }
    }

    Item {
        visible: root.simple
        x: 0
        y: 0
        width: root.sliderWidth
        height: 32

        MaterialIcon {
            id: simpleGlyph
            x: 20
            size: 24
            anchors.verticalCenter: parent.verticalCenter
            name: root.icon
            color: root.inkNow
        }

        Text {
            anchors.left: simpleGlyph.right
            anchors.leftMargin: 12
            anchors.right: simplePct.left
            anchors.rightMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            elide: Text.ElideRight
            text: root.label
            color: root.inkNow
            font.family: mt.fontFamily
            font.pixelSize: 18
            font.weight: Font.ExtraBold
            font.styleName: mt.fontStyleBold
        }

        Text {
            id: simplePct
            anchors.right: parent.right
            anchors.rightMargin: 18
            anchors.verticalCenter: parent.verticalCenter
            text: Math.round(root.shownValue) + "%"
            color: root.inkSubNow
            font.family: mt.fontFamily
            font.pixelSize: 14
            font.weight: Font.DemiBold
        }
    }

    MouseArea {
        id: buttonArea
        visible: !root.simple
        enabled: !root.simple
        x: 0
        y: 0
        width: root.sliderWidth + 5
        height: 75
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onPressed: mouse => {
            if (mouse.button === Qt.LeftButton)
                root.suppressPulse = true
        }
        onReleased: mouse => {
            if (mouse.button === Qt.LeftButton)
                suppressGrace.restart()
        }
        onCanceled: suppressGrace.restart()
        onClicked: mouse => {
            if (mouse.button === Qt.RightButton) {
                root.rightClicked()
            } else {
                root.iconClicked()
                root.clicked()
            }
        }
    }

    Rectangle {
        id: track
        x: 20
        y: root.simple ? (32 + 10) : (73 + 2 + 8)
        width: parent.width - 40
        height: 14
        radius: 7
        color: mt.pSurfaceDivider

        Rectangle {
            width: parent.width * Math.max(0, Math.min(1, root.shownValue / 100))
            height: parent.height
            radius: 7
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: root.muted ? mt.pInkSub : mt.accentMint }
                GradientStop { position: 1.0; color: root.muted ? mt.pInkSub : mt.accentCyan }
            }
            opacity: root.muted ? 0.5 : 1
            Behavior on width {
                enabled: !root.dragging
                NumberAnimation { duration: 150 }
            }
            antialiasing: true
        }

        MouseArea {
            id: trackArea
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            preventStealing: true

            function setFromX(mx) {
                const p = Math.max(0, Math.min(1, mx / track.width))
                root.dragValue = p * 100
                root.moved(root.dragValue)
                PanelActivity.touch()
            }

            onPressed: mouse => {
                root.dragging = true
                setFromX(mouse.x)
            }
            onPositionChanged: mouse => {
                if (pressed)
                    setFromX(mouse.x)
            }
            onReleased: root.dragging = false
            onCanceled: root.dragging = false
        }
        antialiasing: true
    }
}
