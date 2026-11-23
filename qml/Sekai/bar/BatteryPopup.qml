import QtQuick
import Common
import Common.widgets
import "../../Common/functions/BatteryMath.js" as BM

Item {
    id: root

    readonly property var mt: Theme.of(Window.window)

    readonly property int percentage: BatterySource.percentage
    readonly property string statusLabel: BatterySource.statusLabel
    readonly property string timeRemaining: BatterySource.timeRemaining
    readonly property bool charging: BatterySource.isCharging
    readonly property bool plugged: BatterySource.isPluggedIn
    readonly property bool low: BatterySource.isLow
    readonly property int minutes: BM.parseMins(timeRemaining)

    readonly property bool scrubbing: scrub.containsMouse
    readonly property int scrubTarget: Math.max(0, Math.min(100,
        Math.round((scrub.mouseX - gauge.lean / 2) / Math.max(1, scrub.width - gauge.lean) * 100)))
    readonly property int shownPct: scrubbing ? scrubTarget : percentage
    readonly property int tierIdx: BM.tier(shownPct, plugged && !scrubbing)
    readonly property var tierFill: ["#FFE066", mt.accentMint, "#FF9ECB", "#A5E6A5", "#B39DDB", "#e0575b"]

    readonly property string scrubText: {
        if (Math.abs(scrubTarget - percentage) <= 1)
            return "Current charge level"
        const e = BM.eta(percentage, scrubTarget, charging, minutes)
        if (e.kind === "eta")
            return "About " + BM.fmtMins(e.mins) + (charging ? " of charging to reach " + scrubTarget + "%" : " until " + scrubTarget + "% at current pace")
        if (e.kind === "past")
            return "Already past " + scrubTarget + "%"
        if (e.kind === "above")
            return "Needs AC power to climb back to " + scrubTarget + "%"
        return "Slide along segments to estimate ETA"
    }

    readonly property int pad: 16
    readonly property int innerW: 312

    implicitWidth: innerW + 2 * pad
    implicitHeight: stack.implicitHeight + 2 * pad

    property real fillWave: 1
    property real shakeX: 0

    function playOpen() {
        BatterySource.refresh()
        fillAnim.restart()
        if (low)
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
        NumberAnimation { target: root; property: "shakeX"; to: -6; duration: 50 }
        NumberAnimation { target: root; property: "shakeX"; to: 6; duration: 70 }
        NumberAnimation { target: root; property: "shakeX"; to: -4; duration: 55 }
        NumberAnimation { target: root; property: "shakeX"; to: 0; duration: 55 }
    }

    Gradient {
        id: accentGradient
        GradientStop { position: 0.0; color: mt.accentMint }
        GradientStop { position: 1.0; color: mt.accentCyan }
    }

    Column {
        id: stack
        x: root.pad
        y: root.pad
        width: root.innerW
        spacing: 12

        SekaiPlate {
            id: plate
            width: root.innerW
            height: 108
            colorTop: root.tierIdx === 5 ? "#e0575b" : root.tierFill[root.tierIdx]
            colorBottom: root.tierIdx <= 1 ? mt.accentCyan : Qt.darker(colorTop, 1.25)

            Row {
                x: 14
                y: 12
                spacing: 6

                MaterialIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: BM.glyph(root.shownPct, root.charging)
                    size: 20
                    color: root.tierIdx === 5 ? "#ffffff" : mt.colBorder
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.statusLabel.toUpperCase()
                    color: root.tierIdx === 5 ? "#ffffff" : mt.colBorder
                    font.family: mt.fontFamily
                    font.pixelSize: 11
                    font.weight: Font.ExtraBold
                    font.styleName: mt.fontStyleBold
                    font.letterSpacing: 1.0
                }
            }

            StickerText {
                id: pctNum
                x: 10
                y: 34
                text: String(root.shownPct)
                pixelSize: 48
                fillColor: root.low && !root.charging ? "#e0575b" : plate.numberFill
                ringColor: plate.numberRing
            }

            StickerText {
                x: pctNum.x + pctNum.width - 4
                y: 54
                text: "%"
                pixelSize: 20
                fillColor: pctNum.fillColor
                ringColor: plate.numberRing
            }

            Pill {
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                width: judgeTxt.implicitWidth + 24
                height: 28
                rotation: 5
                cornerRadius: 10
                ringWidth: 3
                shadowDy: 3
                fillColor: mt.chipFill

                Text {
                    id: judgeTxt
                    anchors.centerIn: parent
                    text: BM.word(root.tierIdx)
                    color: root.tierIdx === 5 ? "#c93a40" : mt.colBorder
                    font.family: mt.fontFamily
                    font.pixelSize: 11
                    font.weight: Font.ExtraBold
                    font.styleName: mt.fontStyleBold
                    font.letterSpacing: 0.7
                }
            }
        }

        Column {
            width: root.innerW
            spacing: 8

            Item {
                width: root.innerW
                height: 24

                SectionTab {
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.scrubbing ? "Forecast (" + root.scrubTarget + "%)" : "Life Gauge"
                }

                Text {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.timeRemaining.length > 0 ? root.timeRemaining : (root.plugged ? "AC Power" : "On Battery")
                    color: mt.inkSub
                    font.family: mt.fontFamily
                    font.pixelSize: 11
                    font.weight: Font.ExtraBold
                    font.styleName: mt.fontStyleBold
                }
            }

            Rectangle {
                id: track
                width: root.innerW
                height: 68
                radius: 18
                color: Qt.rgba(mt.colBorder.r, mt.colBorder.g, mt.colBorder.b, 0.24)
                border.width: 2
                border.color: mt.line
                antialiasing: true

                Item {
                    id: gauge
                    x: 10 + root.shakeX
                    y: 12
                    width: track.width - 20
                    height: 40

                    readonly property int n: 10
                    readonly property real segH: 36
                    readonly property real skew: 0.2
                    readonly property real lean: segH * skew
                    readonly property real segW: 24 + lean
                    readonly property real pitch: (width - segW) / (n - 1)

                    Repeater {
                        model: gauge.n

                        delegate: Item {
                            id: seg
                            required property int index
                            x: index * gauge.pitch
                            y: 2
                            width: gauge.segW
                            height: gauge.segH

                            readonly property real f: BM.segmentFill(root.shownPct, index, gauge.n)
                            readonly property real t: BM.clamp01((root.fillWave * 620 - index * 42) / 240)

                            Item {
                                anchors.fill: parent
                                scale: 0.8 + 0.2 * BM.outBack(seg.t, 2.0)
                                opacity: Math.min(1, seg.t * 3)

                                Slant {
                                    anchors.fill: parent
                                    skew: gauge.skew
                                    radius: 9
                                    color: mt.surfaceHover
                                    borderColor: mt.line
                                    borderWidth: 3
                                }

                                Slant {
                                    height: parent.height
                                    width: seg.f >= 1 ? parent.width : gauge.lean + Math.max(12, (parent.width - gauge.lean) * seg.f)
                                    skew: gauge.skew
                                    radius: 9
                                    color: root.low && !root.charging ? "#e0575b" : "transparent"
                                    gradient: (root.low && !root.charging) ? null : accentGradient
                                    borderColor: mt.line
                                    borderWidth: 3
                                    opacity: seg.f > 0 ? 1 : 0
                                }
                            }
                        }
                    }

                    MouseArea {
                        id: scrub
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.SizeHorCursor
                    }
                }
            }

            Text {
                width: root.innerW
                text: root.scrubText
                color: mt.inkSub
                font.family: mt.fontFamily
                font.pixelSize: 10
                font.weight: Font.Medium
                elide: Text.ElideRight
            }
        }

        Row {
            width: root.innerW
            spacing: 8

            Pill {
                width: root.innerW - powerSetBtn.width - 8
                height: 36
                cornerRadius: 12
                ringWidth: 3
                shadowDy: 3
                fillColor: mt.surfaceHover

                Text {
                    anchors.centerIn: parent
                    text: root.charging ? "Charging via adapter"
                        : (root.plugged ? "Plugged in" : "Discharging on battery")
                    color: mt.ink
                    font.family: mt.fontFamily
                    font.pixelSize: 11
                    font.weight: Font.ExtraBold
                    font.styleName: mt.fontStyleBold
                }
            }

            Pill {
                id: powerSetBtn
                width: 118
                height: 36
                cornerRadius: 12
                ringWidth: 3
                shadowDy: 3
                pressed: pwrArea.pressed
                fillColor: pwrArea.containsMouse ? mt.surfaceHover : mt.chipFill

                Row {
                    anchors.centerIn: parent
                    spacing: 4

                    MaterialIcon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "bolt"
                        size: 16
                        color: mt.chipInk
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Power Plan"
                        color: mt.chipInk
                        font.family: mt.fontFamily
                        font.pixelSize: 11
                        font.weight: Font.ExtraBold
                        font.styleName: mt.fontStyleBold
                    }
                }

                MouseArea {
                    id: pwrArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        QuickSource.openWindowsSettings("ms-settings:powersleep")
                        ShellController.setActivePopup("")
                    }
                }
            }
        }
    }
}
