pragma Singleton
import QtQuick

/**
 * Provides phone LCD wallpaper lookups on Windows (replaces Linux PanelImages.qml).
 */
QtObject {
    id: root

    readonly property string phoneMode: SettingsService.get("phone.wallpaper") || "desktop"

    function framingFor(kind) {
        return null
    }

    function phoneWallpaperFor(screenName) {
        if (root.phoneMode === "off")
            return ""
        return WallpaperService.imageFor(WallpaperService.pathFor(screenName))
    }
}
