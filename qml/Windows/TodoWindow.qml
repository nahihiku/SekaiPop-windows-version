import QtQuick
import QtQuick.Window
import Common
import Common.widgets

Window {
    id: win

    property string monitorName: ""
    readonly property var mt: Theme.of(win)
    readonly property real uiScale: ShellScale.forScreen(monitorName)
    readonly property bool shown: SettingsService.flag("widgets.todo")
    readonly property bool locked: SettingsService.flag("widgets.locked")

    property string editingId: ""

    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.Tool
    visible: shown

    width: Math.round(308 * uiScale)
    height: Math.round(430 * uiScale)

    function targetScreen() {
        for (let i = 0; i < Qt.application.screens.length; i++) {
            if (Qt.application.screens[i].name === win.monitorName)
                return Qt.application.screens[i]
        }
        return Qt.application.screens.length > 0 ? Qt.application.screens[0] : win.screen
    }

    function placeWindow() {
        const scr = targetScreen()
        if (!scr) return
        const sx = scr.virtualX !== undefined ? scr.virtualX : 0
        const sy = scr.virtualY !== undefined ? scr.virtualY : 0
        const sw = scr.width || 1920
        const sh = scr.height || 1080
        let px = TodoService.posX
        let py = TodoService.posY
        if (px < 0 || py < 0) {
            px = sx + Math.round(28 * win.uiScale)
            py = sy + Math.round(358 * win.uiScale)
        } else {
            px = sx + Math.round(px)
            py = sy + Math.round(py)
        }
        win.x = Math.max(sx + 8, Math.min(sx + sw - win.width - 8, px))
        win.y = Math.max(sy + 8, Math.min(sy + sh - win.height - 8, py))
        ShellController.registerWidgetWindow(win, false, true)
    }

    onShownChanged: if (shown) Qt.callLater(placeWindow)
    Component.onCompleted: Qt.callLater(placeWindow)

    ScaleHost {
        anchors.fill: parent
        factor: win.uiScale

        Item {
            anchors.fill: parent

            Rectangle {
                x: mt.shadowOffset
                y: mt.shadowOffset + 2
                width: card.width
                height: card.height
                radius: 22
                color: mt.lineShadow
                antialiasing: true
            }

            Rectangle {
                id: card
                anchors.fill: parent
                anchors.rightMargin: mt.shadowOffset
                anchors.bottomMargin: mt.shadowOffset + 2
                radius: 22
                color: mt.surface
                border.width: mt.borderWidth
                border.color: mt.line
                antialiasing: true

                Column {
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 10

                    // Draggable header
                    Item {
                        width: parent.width
                        height: 36

                        Row {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 8

                            Pill {
                                width: 32
                                height: 32
                                cornerRadius: 10
                                ringWidth: 2
                                shadowDy: 2
                                fillColor: mt.accentMint

                                MaterialIcon {
                                    anchors.centerIn: parent
                                    name: "checklist"
                                    size: 18
                                    color: mt.colBorder
                                }
                            }

                            Column {
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 0

                                Text {
                                    text: "To-Do"
                                    color: mt.ink
                                    font.family: mt.fontFamily
                                    font.pixelSize: 16
                                    font.weight: Font.ExtraBold
                                    font.styleName: mt.fontStyleBold
                                }

                                Text {
                                    text: TodoService.totalCount + " left"
                                    color: mt.inkSub
                                    font.family: mt.fontFamily
                                    font.pixelSize: 10
                                    font.weight: Font.DemiBold
                                }
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            enabled: !win.locked
                            cursorShape: win.locked ? Qt.ArrowCursor : Qt.SizeAllCursor
                            property point startGlobal
                            property int startWinX
                            property int startWinY
                            onPressed: mouse => {
                                startGlobal = mapToGlobal(mouse.x, mouse.y)
                                startWinX = win.x
                                startWinY = win.y
                            }
                            onPositionChanged: mouse => {
                                if (!pressed) return
                                const cur = mapToGlobal(mouse.x, mouse.y)
                                win.x = startWinX + Math.round(cur.x - startGlobal.x)
                                win.y = startWinY + Math.round(cur.y - startGlobal.y)
                            }
                            onReleased: {
                                const scr = win.targetScreen()
                                if (scr)
                                    TodoService.setPosition(win.x - (scr.virtualX || 0), win.y - (scr.virtualY || 0))
                            }
                        }
                    }

                    // Tasks List
                    ListView {
                        id: listView
                        width: parent.width
                        height: parent.height - 130
                        clip: true
                        spacing: 6
                        model: TodoService.items

                        delegate: Item {
                            id: taskRow
                            required property var modelData
                            readonly property bool done: !!modelData.done
                            readonly property bool isEditing: win.editingId === modelData.id

                            width: listView.width
                            height: Math.max(32, labelText.implicitHeight + 10)

                            Rectangle {
                                anchors.fill: parent
                                radius: 10
                                color: rowHover.containsMouse ? mt.surfaceHover : "transparent"
                                antialiasing: true
                            }

                            Pill {
                                id: checkBox
                                x: 2
                                anchors.verticalCenter: parent.verticalCenter
                                width: 22
                                height: 22
                                cornerRadius: 7
                                ringWidth: 2
                                shadowDy: 1
                                fillColor: taskRow.done ? mt.accentMint : mt.chipFill

                                MaterialIcon {
                                    visible: taskRow.done
                                    anchors.centerIn: parent
                                    name: "check"
                                    size: 14
                                    color: mt.colBorder
                                }
                            }

                            Text {
                                id: labelText
                                visible: !taskRow.isEditing
                                anchors.left: checkBox.right
                                anchors.leftMargin: 10
                                anchors.right: delBtn.left
                                anchors.rightMargin: 6
                                anchors.verticalCenter: parent.verticalCenter
                                text: taskRow.modelData.text || ""
                                color: taskRow.done ? mt.inkSub : mt.ink
                                opacity: taskRow.done ? 0.65 : 1.0
                                font.family: mt.fontFamily
                                font.pixelSize: 12
                                font.weight: Font.Medium
                                font.strikeout: taskRow.done
                                wrapMode: Text.WordWrap
                            }

                            TextInput {
                                visible: taskRow.isEditing
                                anchors.left: checkBox.right
                                anchors.leftMargin: 10
                                anchors.right: delBtn.left
                                anchors.rightMargin: 6
                                anchors.verticalCenter: parent.verticalCenter
                                text: taskRow.modelData.text || ""
                                color: mt.ink
                                font.family: mt.fontFamily
                                font.pixelSize: 12
                                clip: true
                                onAccepted: {
                                    TodoService.editTask(taskRow.modelData.id, text)
                                    win.editingId = ""
                                }
                            }

                            MouseArea {
                                id: rowHover
                                anchors.left: parent.left
                                anchors.right: delBtn.left
                                anchors.top: parent.top
                                anchors.bottom: parent.bottom
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: TodoService.toggleDone(taskRow.modelData.id)
                                onDoubleClicked: win.editingId = taskRow.modelData.id
                            }

                            Item {
                                id: delBtn
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                width: 22
                                height: 22
                                opacity: rowHover.containsMouse || delArea.containsMouse ? 0.9 : 0.25

                                MaterialIcon {
                                    anchors.centerIn: parent
                                    name: "close"
                                    size: 14
                                    color: mt.inkSub
                                }

                                MouseArea {
                                    id: delArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: TodoService.removeTask(taskRow.modelData.id)
                                }
                            }
                        }
                    }

                    // Add Input Box + (+) button
                    Row {
                        width: parent.width
                        spacing: 6

                        Rectangle {
                            width: parent.width - 38
                            height: 36
                            radius: 11
                            color: mt.surfaceHover
                            border.width: 2
                            border.color: inputBox.activeFocus ? mt.accentMint : mt.line
                            antialiasing: true

                            Text {
                                visible: inputBox.text.length === 0
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                verticalAlignment: Text.AlignVCenter
                                text: "add a task..."
                                color: mt.inkSub
                                font.family: mt.fontFamily
                                font.pixelSize: 12
                            }

                            TextInput {
                                id: inputBox
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 10
                                verticalAlignment: TextInput.AlignVCenter
                                color: mt.ink
                                font.family: mt.fontFamily
                                font.pixelSize: 12
                                clip: true
                                onAccepted: {
                                    if (text.trim().length > 0) {
                                        TodoService.addTask(text.trim())
                                        text = ""
                                    }
                                }
                            }
                        }

                        Pill {
                            width: 32
                            height: 36
                            cornerRadius: 11
                            ringWidth: 2
                            shadowDy: 2
                            pressed: addArea.pressed
                            fillColor: mt.accentMint

                            MaterialIcon {
                                anchors.centerIn: parent
                                name: "add"
                                size: 18
                                color: mt.colBorder
                            }

                            MouseArea {
                                id: addArea
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    if (inputBox.text.trim().length > 0) {
                                        TodoService.addTask(inputBox.text.trim())
                                        inputBox.text = ""
                                    } else {
                                        inputBox.forceActiveFocus()
                                    }
                                }
                            }
                        }
                    }

                    // Bottom Clear Done chip
                    Item {
                        width: parent.width
                        height: 24

                        Pill {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            width: clrTxt.implicitWidth + 20
                            height: 24
                            cornerRadius: 9
                            ringWidth: 2
                            shadowDy: 2
                            pressed: clrDoneArea.pressed
                            fillColor: clrDoneArea.containsMouse ? mt.accentMint : mt.chipFill

                            Text {
                                id: clrTxt
                                anchors.centerIn: parent
                                text: "clear done (" + TodoService.doneCount + "/" + TodoService.totalCount + ")"
                                color: clrDoneArea.containsMouse ? mt.colBorder : mt.chipInk
                                font.family: mt.fontFamily
                                font.pixelSize: 10
                                font.weight: Font.ExtraBold
                                font.styleName: mt.fontStyleBold
                            }

                            MouseArea {
                                id: clrDoneArea
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: TodoService.clearDone()
                            }
                        }
                    }
                }
            }
        }
    }
}
