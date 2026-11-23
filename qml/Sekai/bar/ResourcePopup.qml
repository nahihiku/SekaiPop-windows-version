import QtQuick
import Common
import Common.widgets

Item {
    id: root

    readonly property var mt: Theme.of(Window.window)

    readonly property real cpuPercent: ResourceSource.cpuPercent
    readonly property real memPercent: ResourceSource.memPercent
    readonly property string memUsedLabel: ResourceSource.memUsedLabel
    readonly property string memTotalLabel: ResourceSource.memTotalLabel
    readonly property real diskPercent: ResourceSource.diskPercent
    readonly property string diskUsedLabel: ResourceSource.diskUsedLabel
    readonly property string diskTotalLabel: ResourceSource.diskTotalLabel
    readonly property string uptimeLabel: ResourceSource.uptimeLabel

    readonly property int pad: 16
    readonly property int innerW: 320
    readonly property int halfW: (innerW - 10) / 2

    implicitWidth: innerW + 2 * pad
    implicitHeight: stack.implicitHeight + 2 * pad

    readonly property var words: ["CALM", "STEADY", "WARM", "BUSY", "PEAK"]
    readonly property var tierFill: [mt.accentMint, mt.accentCyan, "#FFE066", "#FF9ECB", "#e0575b"]

    function tierOf(pct) {
        if (pct >= 88) return 4
        if (pct >= 68) return 3
        if (pct >= 45) return 2
        if (pct >= 20) return 1
        return 0
    }

    function plateTop(t) {
        if (t === 4) return "#e0575b"
        if (t === 3) return "#FF9ECB"
        if (t === 2) return "#FFE066"
        return mt.accentMint
    }

    function plateBottom(t) {
        if (t === 4) return "#a8242b"
        if (t === 3) return "#f26ba9"
        if (t === 2) return "#f5b83d"
        return mt.accentCyan
    }

    readonly property int cpuTier: tierOf(cpuPercent)
    readonly property int memTier: tierOf(memPercent)
    readonly property bool cpuHot: cpuTier >= 3
    readonly property bool memHot: memTier >= 3

    property real fillWave: 1
    property real shakeX: 0

    function playOpen() {
        ResourceSource.sampleNow()
        fillAnim.restart()
        if (cpuHot || memHot)
            shakeAnim.restart()
    }

    NumberAnimation {
        id: fillAnim
        target: root
        property: "fillWave"
        from: 0
        to: 1
        duration: 620
    }

    SequentialAnimation {
        id: shakeAnim
        NumberAnimation { target: root; property: "shakeX"; to: -5; duration: 50 }
        NumberAnimation { target: root; property: "shakeX"; to: 5; duration: 70 }
        NumberAnimation { target: root; property: "shakeX"; to: -3; duration: 55 }
        NumberAnimation { target: root; property: "shakeX"; to: 0; duration: 55 }
    }

    Column {
        id: stack
        x: root.pad
        y: root.pad
        width: root.innerW
        spacing: 10

        Row {
            width: root.innerW
            spacing: 10

            LoadPlate {
                width: root.halfW
                label: "CPU"
                glyph: "memory"
                pct: root.cpuPercent
                colorTop: root.plateTop(root.cpuTier)
                colorBottom: root.plateBottom(root.cpuTier)
                ink: root.cpuTier === 4 ? "#ffffff" : mt.colBorder
            }

            LoadPlate {
                width: root.halfW
                label: "RAM"
                glyph: "storage"
                pct: root.memPercent
                colorTop: root.plateTop(root.memTier)
                colorBottom: root.plateBottom(root.memTier)
                ink: root.memTier === 4 ? "#ffffff" : mt.colBorder
            }
        }

        LoadGauge {
            width: root.innerW
            title: "Processor"
            pct: root.cpuPercent
            word: root.words[root.cpuTier]
            tint: root.tierFill[root.cpuTier]
            plain: root.cpuTier >= 2
            hot: root.cpuHot
            wave: root.fillWave
            shake: root.cpuHot ? root.shakeX : 0
        }

        LoadGauge {
            width: root.innerW
            title: "Memory"
            pct: root.memPercent
            word: root.words[root.memTier]
            tint: root.tierFill[root.memTier]
            plain: root.memTier >= 2
            hot: root.memHot
            wave: root.fillWave
            shake: root.memHot ? root.shakeX : 0
        }

        Item {
            width: root.innerW
            height: 44

            Row {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6

                Pill {
                    width: Math.max(54, memUsedText.implicitWidth + 22)
                    height: 38
                    shadowDy: 3
                    cornerRadius: 12
                    ringWidth: 4
                    fillGradient: Gradient {
                        GradientStop { position: 0.0; color: mt.accentMint }
                        GradientStop { position: 1.0; color: mt.accentCyan }
                    }

                    Text {
                        id: memUsedText
                        anchors.centerIn: parent
                        text: root.memUsedLabel + " / " + root.memTotalLabel
                        color: mt.colBorder
                        font.family: mt.fontFamily
                        font.pixelSize: 11
                        font.weight: Font.ExtraBold
                        font.styleName: mt.fontStyleBold
                    }
                }

                Pill {
                    width: Math.max(54, diskText.implicitWidth + 20)
                    height: 38
                    shadowDy: 3
                    cornerRadius: 12
                    ringWidth: 4
                    fillColor: mt.chipFill

                    Text {
                        id: diskText
                        anchors.centerIn: parent
                        text: "C: " + root.diskUsedLabel + " / " + root.diskTotalLabel
                        color: mt.chipInk
                        font.family: mt.fontFamily
                        font.pixelSize: 10
                        font.weight: Font.ExtraBold
                        font.styleName: mt.fontStyleBold
                    }
                }
            }

            Pill {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: tmRow.implicitWidth + 22
                height: 38
                shadowDy: 3
                cornerRadius: 12
                ringWidth: 4
                pressed: tmArea.pressed
                fillColor: tmArea.containsMouse ? mt.surfaceHover : mt.chipFill

                Row {
                    id: tmRow
                    anchors.centerIn: parent
                    spacing: 4

                    MaterialIcon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "monitor_heart"
                        size: 15
                        color: mt.chipInk
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.uptimeLabel.length > 0 ? root.uptimeLabel : "Tasks"
                        color: mt.chipInk
                        font.family: mt.fontFamily
                        font.pixelSize: 10
                        font.weight: Font.ExtraBold
                        font.styleName: mt.fontStyleBold
                    }
                }

                MouseArea {
                    id: tmArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        QuickSource.openTaskManager()
                        ShellController.setActivePopup("")
                    }
                }
            }
        }
    }
}
