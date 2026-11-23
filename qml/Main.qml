import QtQuick
import QtQuick.Window
import Common
import Windows

QtObject {
    id: root

    readonly property string primaryScreenName: Qt.application.screens.length > 0 ? Qt.application.screens[0].name : ""

    property DesktopContextMenu desktopMenu: DesktopContextMenu {}

    property Instantiator screenInstantiator: Instantiator {
        model: Qt.application.screens
        delegate: QtObject {
            id: screenBundle
            required property var modelData
            property WallpaperWindow wallpaper: WallpaperWindow {
                modelData: screenBundle.modelData
                contextMenu: root.desktopMenu
            }
            property VisualizerWindow visualizer: VisualizerWindow {
                monitorName: screenBundle.modelData.name
            }
            property BarWindow bar: BarWindow {
                monitorName: screenBundle.modelData.name
            }
        }
    }

    property TodoWindow todoWindow: TodoWindow {
        monitorName: root.primaryScreenName
    }

    property UserCardWindow userCardWindow: UserCardWindow {
        monitorName: root.primaryScreenName
    }

    property WeatherCardWindow weatherCardWindow: WeatherCardWindow {
        monitorName: root.primaryScreenName
    }

    property ClockWidgetWindow clockWindow: ClockWidgetWindow {
        monitorName: root.primaryScreenName
    }

    property EditDesktopWindow editDesktopWindow: EditDesktopWindow {
        monitorName: root.primaryScreenName
    }

    property DockWindow dockWindow: DockWindow {
        monitorName: root.primaryScreenName
    }

    property BarPopupWindow barPopupWindow: BarPopupWindow {
        monitorName: root.primaryScreenName
    }

    property LauncherWindow launcherWindow: LauncherWindow {}

    property PhoneWindow phoneWindow: PhoneWindow {
        monitorName: root.primaryScreenName
    }

    property OsdWindow osdWindow: OsdWindow {}
}
