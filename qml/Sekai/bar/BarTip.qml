import QtQuick
import QtQuick.Window
import Common

/**
 * Lightweight SekaiPop hover tooltip for bar pills, using a separate click-through
 * borderless Window positioned beside the hovered module so it is never clipped
 * by the AppBar's strip bounds.
 */
Item {
    id: root

    readonly property var mt: Theme.of(Window.window)

    property bool shown: false
    property bool pressed: false
    property string text: ""
    property string barPosition: "top"

    readonly property Item host: parent
    readonly property bool wantShown: shown && !pressed && text.length > 0 && Window.window !== null

    Loader {
        active: root.wantShown
        sourceComponent: Window {
            id: tipWin

            visible: true
            color: "transparent"
            flags: Qt.ToolTip
                | Qt.FramelessWindowHint
                | Qt.WindowStaysOnTopHint
                | Qt.WindowDoesNotAcceptFocus
                | Qt.WindowTransparentForInput

            width: Math.ceil(tipBox.implicitWidth + 8)
            height: Math.ceil(tipBox.implicitHeight + 8)

            function reposition() {
                if (!root.host || !root.host.Window.window)
                    return
                const globalTopLeft = root.host.mapToGlobal(0, 0)
                const hostW = root.host.width
                const hostH = root.host.height
                const gap = 8

                if (root.barPosition === "bottom") {
                    x = Math.round(globalTopLeft.x + (hostW - width) / 2)
                    y = Math.round(globalTopLeft.y - height - gap)
                } else if (root.barPosition === "left") {
                    x = Math.round(globalTopLeft.x + hostW + gap)
                    y = Math.round(globalTopLeft.y + (hostH - height) / 2)
                } else if (root.barPosition === "right") {
                    x = Math.round(globalTopLeft.x - width - gap)
                    y = Math.round(globalTopLeft.y + (hostH - height) / 2)
                } else {
                    x = Math.round(globalTopLeft.x + (hostW - width) / 2)
                    y = Math.round(globalTopLeft.y + hostH + gap)
                }
            }

            Component.onCompleted: reposition()

            Rectangle {
                id: tipBox
                anchors.centerIn: parent
                implicitWidth: tipLabel.implicitWidth + 20
                implicitHeight: tipLabel.implicitHeight + 12
                radius: 10
                color: mt.darker ? mt.surface : mt.colDeep
                border.width: 2
                border.color: mt.line
                antialiasing: true

                Text {
                    id: tipLabel
                    anchors.centerIn: parent
                    text: root.text
                    color: "#ffffff"
                    font.family: mt.fontFamily
                    font.pixelSize: 12
                    font.weight: Font.Bold
                }
            }
        }
    }
}
