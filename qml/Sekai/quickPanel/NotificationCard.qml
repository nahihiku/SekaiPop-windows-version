import QtQuick
import Common
import Common.widgets

Item {
    id: card

    readonly property var mt: Theme.of(Window.window)

    property int notifId: 0
    property string app: ""
    property string title: ""
    property string body: ""
    property string icon: "notifications"
    property string timeLabel: ""
    property int urgency: 1

    signal dismissRequested(int id)

    implicitHeight: Math.max(76, contentCol.implicitHeight + 24)

    Rectangle {
        anchors.fill: parent
        radius: 16
        color: mt.pSurfaceHover
        border.width: 2
        border.color: card.urgency === 2 ? mt.accentPink : mt.pLine
        antialiasing: true
    }

    Item {
        x: 14
        width: 40
        height: 40
        anchors.verticalCenter: parent.verticalCenter

        ShellBadge {
            anchors.centerIn: parent
            size: 38
            glyph: card.icon.length > 0 ? card.icon : "notifications"
        }
    }

    Column {
        id: contentCol
        anchors.left: parent.left
        anchors.leftMargin: 66
        anchors.right: closeBtn.left
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2

        Row {
            width: parent.width
            spacing: 8

            Text {
                text: card.app.toUpperCase()
                color: mt.pInkSub
                font.family: mt.fontFamily
                font.pixelSize: 11
                font.weight: Font.ExtraBold
                font.styleName: mt.fontStyleBold
                font.letterSpacing: 0.8
            }

            Text {
                visible: card.timeLabel.length > 0
                text: card.timeLabel
                color: mt.pInkSub
                font.family: mt.fontFamily
                font.pixelSize: 11
            }
        }

        Text {
            width: parent.width
            text: card.title
            color: mt.pInk
            font.family: mt.fontFamily
            font.pixelSize: 15
            font.weight: Font.ExtraBold
            font.styleName: mt.fontStyleBold
            elide: Text.ElideRight
        }

        Text {
            width: parent.width
            visible: card.body.length > 0
            text: card.body
            color: mt.pInkSub
            font.family: mt.fontFamily
            font.pixelSize: 13
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }
    }

    Pill {
        id: closeBtn
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        width: 28
        height: 28
        cornerRadius: 10
        ringWidth: 2
        shadowDy: 2
        fillColor: cArea.containsMouse ? mt.accentMint : mt.pSurface

        MaterialIcon {
            anchors.centerIn: parent
            name: "close"
            size: 16
            color: cArea.containsMouse ? mt.colBorder : mt.pInk
        }

        MouseArea {
            id: cArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: card.dismissRequested(card.notifId)
        }
    }
}
