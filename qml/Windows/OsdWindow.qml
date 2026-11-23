import QtQuick
import QtQuick.Window
import Common
import Common.widgets

Window {
    id: win

    readonly property var mt: Theme.of(win)
    readonly property real uiScale: ShellScale.forScreen("")

    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.Tool | Qt.WindowStaysOnTopHint | Qt.WindowTransparentForInput | Qt.WindowDoesNotAcceptFocus
    visible: OsdSource.open

    width: Math.round(280 * uiScale)
    height: Math.round(74 * uiScale)

    function placeOsd() {
        const scr = Qt.application.screens.length > 0 ? Qt.application.screens[0] : win.screen
        if (!scr) return
        const sx = scr.virtualX !== undefined ? scr.virtualX : 0
        const sy = scr.virtualY !== undefined ? scr.virtualY : 0
        const sw = scr.width || 1920
        const sh = scr.height || 1080
        win.x = sx + Math.round((sw - win.width) / 2)
        win.y = sy + sh - win.height - Math.round(94 * win.uiScale)
        ShellController.configureOsdWindow(win)
    }

    Connections {
        target: OsdSource
        function onChanged() {
            if (OsdSource.open)
                win.placeOsd()
        }
    }

    Component.onCompleted: placeOsd()

    ScaleHost {
        anchors.fill: parent
        factor: win.uiScale

        Item {
            anchors.fill: parent

            Pill {
                anchors.centerIn: parent
                width: 262
                height: 58
                cornerRadius: 18
                ringWidth: 4
                shadowDy: 4
                fillColor: mt.surface

                Item {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 14

                    MaterialIcon {
                        id: osdIcon
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        name: OsdSource.icon
                        size: 22
                        color: OsdSource.muted ? mt.inkSub : mt.ink
                    }

                    Column {
                        anchors.left: osdIcon.right
                        anchors.leftMargin: 10
                        anchors.right: pctTxt.left
                        anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 4

                        Text {
                            text: OsdSource.label
                            color: mt.ink
                            font.family: mt.fontFamily
                            font.pixelSize: 11
                            font.weight: Font.ExtraBold
                            font.styleName: mt.fontStyleBold
                        }

                        Rectangle {
                            width: parent.width
                            height: 8
                            radius: 4
                            color: mt.surfaceHover
                            border.width: 1.5
                            border.color: mt.line
                            antialiasing: true

                            Rectangle {
                                height: parent.height
                                width: Math.max(8, parent.width * Math.max(0, Math.min(1, OsdSource.value / 100)))
                                radius: 4
                                gradient: Gradient {
                                    orientation: Gradient.Horizontal
                                    GradientStop { position: 0.0; color: mt.accentMint }
                                    GradientStop { position: 1.0; color: mt.accentCyan }
                                }
                                opacity: OsdSource.muted ? 0.45 : 1.0
                            }
                        }
                    }

                    Text {
                        id: pctTxt
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: OsdSource.muted ? "MUTE" : Math.round(OsdSource.value) + "%"
                        color: mt.ink
                        font.family: mt.fontFamily
                        font.pixelSize: 12
                        font.weight: Font.ExtraBold
                        font.styleName: mt.fontStyleBold
                    }
                }
            }
        }
    }
}
