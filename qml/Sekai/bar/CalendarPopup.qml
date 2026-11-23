import QtQuick
import Common
import Common.widgets
import "../../Common/functions/CalendarMath.js" as CM

Item {
    id: root

    readonly property var mt: Theme.of(Window.window)

    property date today: new Date()
    property int monthShift: 0
    property string pickedKey: CM.dayKey(today.getFullYear(), today.getMonth(), today.getDate())
    property var marks: CM.parseMarks(SettingsService.get("calendar.marks"))

    readonly property date viewDate: CM.monthStart(today, monthShift)
    readonly property var gridCells: CM.monthGrid(viewDate, today, 1)
    readonly property var markPalette: [
        "transparent",
        mt.accentMint,
        mt.accentCyan,
        "#FFE066",
        "#FF9ECB",
        "#e0575b"
    ]

    readonly property int pad: 16
    readonly property int innerW: 320
    readonly property int cellW: Math.floor((innerW - 24) / 7)

    implicitWidth: innerW + 2 * pad
    implicitHeight: stack.implicitHeight + 2 * pad

    function playOpen() {
        today = new Date()
        marks = CM.parseMarks(SettingsService.get("calendar.marks"))
    }

    function cycleMark(key) {
        const nextMap = Object.assign({}, marks)
        const cur = nextMap[key] || 0
        if (cur >= 5)
            delete nextMap[key]
        else
            nextMap[key] = cur + 1
        marks = nextMap
        SettingsService.set("calendar.marks", CM.serializeMarks(nextMap, today))
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
            height: 92
            colorTop: mt.accentMint
            colorBottom: mt.accentCyan

            StickerText {
                id: dayNum
                x: 12
                y: 8
                text: String(root.today.getDate())
                pixelSize: 46
                fillColor: plate.numberFill
                ringColor: plate.numberRing
            }

            Column {
                x: dayNum.x + dayNum.width + 6
                y: 20
                spacing: 2

                Text {
                    text: CM.weekdayLong(root.today.getDay()).toUpperCase()
                    color: mt.colBorder
                    font.family: mt.fontFamily
                    font.pixelSize: 11
                    font.weight: Font.ExtraBold
                    font.styleName: mt.fontStyleBold
                    font.letterSpacing: 1.0
                }

                Text {
                    text: CM.monthTitle(root.viewDate)
                    color: mt.colBorder
                    font.family: mt.fontFamily
                    font.pixelSize: 16
                    font.weight: Font.ExtraBold
                    font.styleName: mt.fontStyleBold
                }
            }

            Row {
                anchors.right: parent.right
                anchors.rightMargin: 10
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 12
                spacing: 4

                Pill {
                    width: 28; height: 28; cornerRadius: 9; ringWidth: 2; shadowDy: 2
                    pressed: prevM.pressed
                    fillColor: mt.chipFill
                    MaterialIcon { anchors.centerIn: parent; name: "chevron_left"; size: 16; color: mt.chipInk }
                    MouseArea { id: prevM; anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.monthShift -= 1 }
                }

                Pill {
                    width: 28; height: 28; cornerRadius: 9; ringWidth: 2; shadowDy: 2
                    pressed: nowM.pressed
                    fillColor: mt.chipFill
                    MaterialIcon { anchors.centerIn: parent; name: "today"; size: 15; color: mt.chipInk }
                    MouseArea {
                        id: nowM
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            root.monthShift = 0
                            root.pickedKey = CM.dayKey(root.today.getFullYear(), root.today.getMonth(), root.today.getDate())
                        }
                    }
                }

                Pill {
                    width: 28; height: 28; cornerRadius: 9; ringWidth: 2; shadowDy: 2
                    pressed: nextM.pressed
                    fillColor: mt.chipFill
                    MaterialIcon { anchors.centerIn: parent; name: "chevron_right"; size: 16; color: mt.chipInk }
                    MouseArea { id: nextM; anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.monthShift += 1 }
                }
            }
        }

        Grid {
            width: root.innerW
            columns: 7
            columnSpacing: 4
            rowSpacing: 4

            Repeater {
                model: CM.weekdayLabels(1)

                delegate: Item {
                    required property string modelData
                    width: root.cellW
                    height: 20

                    Text {
                        anchors.centerIn: parent
                        text: modelData
                        color: mt.inkSub
                        font.family: mt.fontFamily
                        font.pixelSize: 10
                        font.weight: Font.ExtraBold
                        font.styleName: mt.fontStyleBold
                    }
                }
            }

            Repeater {
                model: root.gridCells

                delegate: Item {
                    id: cell
                    required property var modelData
                    readonly property int markIdx: root.marks[modelData.key] || 0
                    readonly property bool isPicked: root.pickedKey === modelData.key
                    width: root.cellW
                    height: 32

                    Rectangle {
                        anchors.fill: parent
                        radius: 9
                        color: cell.modelData.isToday
                            ? mt.accentMint
                            : (cell.markIdx > 0
                                ? Qt.rgba(Qt.color(root.markPalette[cell.markIdx]).r,
                                          Qt.color(root.markPalette[cell.markIdx]).g,
                                          Qt.color(root.markPalette[cell.markIdx]).b, 0.35)
                                : (cArea.containsMouse ? mt.surfaceHover : "transparent"))
                        border.width: cell.isPicked || cell.modelData.isToday ? 2 : 0
                        border.color: cell.modelData.isToday ? mt.colBorder : mt.line
                        antialiasing: true
                    }

                    Text {
                        anchors.centerIn: parent
                        text: String(cell.modelData.day)
                        color: cell.modelData.isToday
                            ? mt.colBorder
                            : (cell.modelData.inMonth ? mt.ink : mt.inkSub)
                        opacity: cell.modelData.inMonth ? 1.0 : 0.42
                        font.family: mt.fontFamily
                        font.pixelSize: 12
                        font.weight: cell.modelData.isToday ? Font.ExtraBold : Font.DemiBold
                    }

                    Rectangle {
                        visible: cell.markIdx > 0
                        width: 5
                        height: 5
                        radius: 2.5
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 3
                        color: root.markPalette[cell.markIdx]
                    }

                    MouseArea {
                        id: cArea
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        cursorShape: Qt.PointingHandCursor
                        onClicked: mouse => {
                            root.pickedKey = cell.modelData.key
                            if (mouse.button === Qt.RightButton || !cell.modelData.isToday)
                                root.cycleMark(cell.modelData.key)
                        }
                    }
                }
            }
        }

        Text {
            width: root.innerW
            text: {
                const parts = root.pickedKey.split("-")
                const y = parseInt(parts[0], 10)
                const m = parseInt(parts[1], 10) - 1
                const d = parseInt(parts[2], 10)
                const diff = CM.dayDiff(root.today, y, m, d)
                return CM.longDay(y, m, d) + "  (" + CM.relative(diff) + ")  -  Click date to tag"
            }
            color: mt.inkSub
            font.family: mt.fontFamily
            font.pixelSize: 10
            elide: Text.ElideRight
        }
    }
}
