import QtQuick
import QtQuick.Effects

/**
 * Pure Qt 6 rounded-corner clipping container replacing Quickshell.Widgets.ClippingRectangle.
 */
Item {
    id: root

    property real radius: 0
    property real topLeftRadius: radius
    property real topRightRadius: radius
    property real bottomLeftRadius: radius
    property real bottomRightRadius: radius
    property color color: "transparent"

    default property alias content: contentHost.data

    readonly property real effectiveRadius: Math.max(
        topLeftRadius,
        topRightRadius,
        bottomLeftRadius,
        bottomRightRadius
    )
    readonly property bool roundedClip: effectiveRadius > 0

    Rectangle {
        anchors.fill: parent
        radius: root.effectiveRadius
        color: root.color
        antialiasing: true
    }

    Item {
        id: contentHost
        anchors.fill: parent
        clip: !root.roundedClip
    }

    Rectangle {
        id: maskRect
        anchors.fill: parent
        radius: root.effectiveRadius
        color: "#ffffff"
        antialiasing: true
        visible: false
        layer.enabled: root.roundedClip
    }

    ShaderEffectSource {
        id: contentCapture
        anchors.fill: parent
        sourceItem: root.roundedClip ? contentHost : null
        hideSource: root.roundedClip
        live: true
        visible: false
    }

    MultiEffect {
        anchors.fill: parent
        visible: root.roundedClip
        source: contentCapture
        maskEnabled: true
        maskSource: maskRect
    }
}
