import QtQuick
import Common
import Common.widgets

Item {
    id: root

    readonly property var mt: Theme.of(Window.window)

    readonly property int pad: 16
    readonly property int innerW: 312

    implicitWidth: innerW + 2 * pad
    implicitHeight: stack.implicitHeight + 2 * pad

    function playOpen() {}

    Column {
        id: stack
        x: root.pad
        y: root.pad
        width: root.innerW
        spacing: 12

        SekaiPlate {
            width: root.innerW
            height: 102
            colorTop: mt.accentMint
            colorBottom: mt.accentCyan

            Pill {
                id: artBox
                x: 14
                y: 15
                width: 68
                height: 68
                cornerRadius: 16
                ringWidth: 4
                shadowDy: 4
                pressed: artArea.pressed
                rotation: MediaSource.playing ? -4 : 0
                fillColor: mt.chipFill

                Behavior on rotation { NumberAnimation { duration: 220; easing.type: Easing.OutBack } }

                MaterialIcon {
                    anchors.centerIn: parent
                    name: MediaSource.present ? (MediaSource.playing ? "graphic_eq" : "music_note") : "album"
                    size: 32
                    color: mt.chipInk
                }

                MouseArea {
                    id: artArea
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: MediaSource.togglePlayPause()
                }
            }

            Column {
                x: 94
                y: 16
                width: root.innerW - 108
                spacing: 4

                Pill {
                    width: stateTxt.implicitWidth + 18
                    height: 20
                    cornerRadius: 8
                    ringWidth: 2
                    shadowDy: 2
                    fillColor: mt.chipFill

                    Text {
                        id: stateTxt
                        anchors.centerIn: parent
                        text: MediaSource.present ? (MediaSource.playing ? "PLAYING" : "PAUSED") : "NO MEDIA"
                        color: mt.chipInk
                        font.family: mt.fontFamily
                        font.pixelSize: 9
                        font.weight: Font.ExtraBold
                        font.styleName: mt.fontStyleBold
                        font.letterSpacing: 0.8
                    }
                }

                Text {
                    width: parent.width
                    text: MediaSource.title.length > 0 ? MediaSource.title : "No active Windows media session"
                    color: mt.colBorder
                    font.family: mt.fontFamily
                    font.pixelSize: 15
                    font.weight: Font.ExtraBold
                    font.styleName: mt.fontStyleBold
                    elide: Text.ElideRight
                }

                Text {
                    width: parent.width
                    text: MediaSource.artist.length > 0 ? MediaSource.artist : "Start Spotify, Chrome, or player"
                    color: Qt.rgba(mt.colBorder.r, mt.colBorder.g, mt.colBorder.b, 0.78)
                    font.family: mt.fontFamily
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
            }
        }

        Rectangle {
            width: root.innerW
            height: 44
            radius: 14
            color: mt.surfaceHover
            border.width: 2
            border.color: mt.line
            antialiasing: true

            Row {
                anchors.centerIn: parent
                spacing: 3

                Repeater {
                    model: 24

                    delegate: Rectangle {
                        required property int index
                        readonly property real rawLvl: {
                            const lvls = AudioSpectrumSource.levels
                            const i = Math.min(lvls.length - 1, Math.floor(index * lvls.length / 24))
                            return lvls && i >= 0 ? Number(lvls[i]) : 0.1
                        }
                        width: 7
                        height: Math.max(6, Math.round(rawLvl * 30))
                        anchors.verticalCenter: parent.verticalCenter
                        radius: 3.5
                        color: index % 2 === 0 ? mt.accentMint : mt.accentCyan
                    }
                }
            }
        }

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 10

            Pill {
                width: 54
                height: 42
                cornerRadius: 13
                ringWidth: 4
                shadowDy: 4
                pressed: prevArea.pressed
                fillColor: prevArea.containsMouse ? mt.surfaceHover : mt.chipFill

                MaterialIcon { anchors.centerIn: parent; name: "skip_previous"; size: 22; color: mt.chipInk }
                MouseArea {
                    id: prevArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: MediaSource.previous()
                }
            }

            Pill {
                width: 116
                height: 42
                cornerRadius: 13
                ringWidth: 4
                shadowDy: 4
                pressed: playArea.pressed
                fillGradient: Gradient {
                    GradientStop { position: 0.0; color: mt.accentMint }
                    GradientStop { position: 1.0; color: mt.accentCyan }
                }

                Row {
                    anchors.centerIn: parent
                    spacing: 4

                    MaterialIcon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: MediaSource.playing ? "pause" : "play_arrow"
                        size: 22
                        color: mt.colBorder
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: MediaSource.playing ? "Pause" : "Play"
                        color: mt.colBorder
                        font.family: mt.fontFamily
                        font.pixelSize: 13
                        font.weight: Font.ExtraBold
                        font.styleName: mt.fontStyleBold
                    }
                }

                MouseArea {
                    id: playArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: MediaSource.togglePlayPause()
                }
            }

            Pill {
                width: 54
                height: 42
                cornerRadius: 13
                ringWidth: 4
                shadowDy: 4
                pressed: nextArea.pressed
                fillColor: nextArea.containsMouse ? mt.surfaceHover : mt.chipFill

                MaterialIcon { anchors.centerIn: parent; name: "skip_next"; size: 22; color: mt.chipInk }
                MouseArea {
                    id: nextArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: MediaSource.next()
                }
            }
        }
    }
}
