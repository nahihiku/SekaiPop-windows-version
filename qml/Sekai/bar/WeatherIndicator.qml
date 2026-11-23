import QtQuick
import Common
import Common.widgets

Item {
    id: root

    readonly property var mt: Theme.of(Window.window)

    property string text: WeatherSource.temperatureText.length > 0 ? WeatherSource.temperatureText : "--"
    property string iconName: WeatherSource.iconName.length > 0 ? WeatherSource.iconName : "partly_cloudy_day"
    property bool vertical: false
    property string barPosition: "top"

    readonly property string tipText: "Weather: " + root.text

    signal clicked()

    implicitWidth: pill.implicitWidth
    implicitHeight: pill.implicitHeight

    Pill {
        id: pill
        anchors.fill: parent
        cornerRadius: mt.radiusBadge
        fillColor: mt.chipFill
        pressed: area.pressed
        ringWidth: 6
        implicitWidth: root.vertical ? pill.barVWidth : weatherIcon.width + label.implicitWidth + (pill.capsule ? 26 : 34)
        implicitHeight: root.vertical
            ? vcol.implicitHeight + (pill.capsule ? 4 : 2 * ringWidth) + 8 + mt.shadowOffset
            : mt.pillHeight + mt.shadowOffset

        Column {
            id: vcol
            visible: root.vertical
            anchors.centerIn: parent
            spacing: 1

            MaterialIcon {
                anchors.horizontalCenter: parent.horizontalCenter
                name: root.iconName
                size: 22
                color: mt.chipInk
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: root.text
                color: mt.chipInk
                font.family: mt.fontFamily
                font.pixelSize: mt.fontSizeInfo
                font.weight: Font.ExtraBold
                font.styleName: mt.fontStyleBold
            }
        }

        MaterialIcon {
            id: weatherIcon
            visible: !root.vertical
            name: root.iconName
            size: 22
            color: mt.chipInk
            x: 10
            anchors.verticalCenter: parent.verticalCenter
        }

        Text {
            id: label
            visible: !root.vertical
            anchors.left: weatherIcon.right
            anchors.leftMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            color: mt.chipInk
            font.family: mt.fontFamily
            font.pixelSize: mt.fontSizeInfo
            font.weight: Font.ExtraBold
            font.styleName: mt.fontStyleBold
        }
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }

    BarTip {
        shown: area.containsMouse
        pressed: area.pressed
        text: root.tipText
        barPosition: root.barPosition
    }
}
