import QtQuick
import Common
import Common.widgets

Item {
    id: root

    readonly property var mt: Theme.of(Window.window)

    property string kind: "wide"
    property real wideWidth: 200
    property real roundWidth: 74
    property bool on: false
    property bool active: false
    property bool hoverLights: false
    property bool keyFocused: false
    property string label: ""
    property string subtitle: ""
    property string icon: ""
    property bool iconFilled: false

    property bool elevated: false
    property bool hoverElevates: false
    property real hoverShadowExtraX: 6
    property real hoverShadowExtraY: 4
    property bool anchorRight: false
    property bool flatShadow: false

    signal clicked()
    signal rightClicked()

    readonly property bool isOn: root.on || root.active
    readonly property bool hovered: area.containsMouse
    readonly property bool lit: isOn || (hoverLights && (hovered || keyFocused))

    readonly property color inkNow: Qt.tint(mt.classicFor(root) ? mt.pInk : mt.ink, Qt.rgba(mt.colBorder.r, mt.colBorder.g, mt.colBorder.b, litP))
    readonly property color inkSubNow: Qt.rgba(inkNow.r, inkNow.g, inkNow.b, 0.76)

    property real litP: lit ? 1 : 0
    Behavior on litP {
        NumberAnimation {
            duration: 170
            easing.type: Easing.OutCubic
        }
    }

    readonly property real capW: kind === "wide" ? wideWidth : roundWidth - 1 + litP
    readonly property real capH: 73

    property real lineHeightPx: 0
    readonly property real titleLine: 20
    readonly property real subLine: 17

    readonly property real shadowLitP: elevated ? Math.max(litP, 1) : litP

    property real hoverBoostP: (hoverElevates && (hovered || keyFocused)) ? 1 : 0
    Behavior on hoverBoostP {
        NumberAnimation {
            duration: 170
            easing.type: Easing.OutCubic
        }
    }

    implicitWidth: (kind === "wide" ? wideWidth : roundWidth) + 5
    implicitHeight: flatShadow ? 77 : 75

    property bool keyPulse: false
    property bool suppressPulse: false

    onIsOnChanged: {
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
        anchors.left: root.anchorRight ? undefined : parent.left
        anchors.right: root.anchorRight ? parent.right : undefined
        anchors.bottom: parent.bottom

        width: root.capW + shadowDx
        height: root.capH + shadowDy

        cornerRadius: root.kind === "square" ? 30 : 40
        borderWidth: 3 + 2 * root.shadowLitP
        ringWidth: 3 + 11 * root.shadowLitP
        shadowDx: root.flatShadow ? 0 : 3 + 2 * root.shadowLitP + root.hoverBoostP * root.hoverShadowExtraX
        shadowDy: root.flatShadow ? 3 + root.shadowLitP + root.hoverBoostP * root.hoverShadowExtraY
                                  : 1 + root.shadowLitP + root.hoverBoostP * root.hoverShadowExtraY
        fillColor: mt.pSurface
        pressed: area.pressed || root.keyPulse

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
                size: 24
                x: root.kind === "wide" ? 28 - 8 * root.litP : (parent.width - width) / 2
                y: (parent.height - height) / 2
                name: root.icon
                fill: root.iconFilled ? 1 : mt.iconFill
                color: root.inkNow
            }

            Column {
                visible: root.kind === "wide"
                x: 62 - 8 * root.litP
                width: root.wideWidth - 73
                anchors.verticalCenter: parent.verticalCenter

                Text {
                    width: parent.width
                    elide: Text.ElideRight
                    text: root.label
                    color: root.inkNow
                    font.family: mt.fontFamily
                    font.pixelSize: 18
                    font.weight: Font.ExtraBold
                    font.styleName: mt.fontStyleBold
                    lineHeight: root.lineHeightPx > 0 ? root.lineHeightPx : root.titleLine
                    lineHeightMode: Text.FixedHeight
                }

                Text {
                    width: parent.width
                    elide: Text.ElideRight
                    text: root.subtitle
                    color: root.inkSubNow
                    font.family: mt.fontFamily
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                    lineHeight: root.lineHeightPx > 0 ? root.lineHeightPx : root.subLine
                    lineHeightMode: Text.FixedHeight
                }
            }
        }
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onPressed: mouse => {
            if (mouse.button === Qt.RightButton)
                return
            root.suppressPulse = true
        }
        onReleased: mouse => {
            if (mouse.button === Qt.RightButton)
                return
            suppressGrace.restart()
        }
        onCanceled: suppressGrace.restart()
        onClicked: mouse => {
            if (mouse.button === Qt.RightButton) {
                root.rightClicked()
                return
            }
            root.clicked()
        }
    }
}
