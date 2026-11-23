import QtQuick

/**
 * Pure Qt 6 icon image helper replacing Quickshell.Widgets.IconImage.
 */
Item {
    id: root

    property real implicitSize: 32
    property alias source: img.source
    property alias asynchronous: img.asynchronous
    property alias mipmap: img.mipmap
    property alias smooth: img.smooth
    property alias fillMode: img.fillMode
    readonly property int status: img.status

    implicitWidth: implicitSize
    implicitHeight: implicitSize
    width: implicitSize
    height: implicitSize

    Image {
        id: img
        anchors.fill: parent
        sourceSize: Qt.size(
            Math.max(16, Math.round(root.implicitSize * 2)),
            Math.max(16, Math.round(root.implicitSize * 2))
        )
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
        asynchronous: true
    }
}
