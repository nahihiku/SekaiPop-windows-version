import QtQuick
import Common

Item {
    id: root

    readonly property var mt: Theme.of(Window.window)

    property bool open: false
    signal dismissRequested()

    anchors.fill: parent
    visible: opacity > 0
    opacity: root.open ? 1 : 0
    Behavior on opacity {
        NumberAnimation { duration: root.open ? 120 : 260 }
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: mt.phoneScreenInset
        radius: mt.phoneCornerRadius - mt.phoneScreenInset
        color: "transparent"
        opacity: 0

        MouseArea {
            anchors.fill: parent
            enabled: root.open
            onClicked: root.dismissRequested()
        }
    }
}
