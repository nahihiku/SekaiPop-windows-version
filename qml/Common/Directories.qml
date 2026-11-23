pragma Singleton
import QtQuick

/**
 * Resolves Windows-safe file:/// URLs and bundled asset/shader paths under ShellBaseDir.
 */
QtObject {
    id: root

    readonly property string shell: ShellBaseDir
    readonly property string shaders: shell + "/shaders"
    readonly property string assets: shell + "/assets"

    function asset(rel) {
        return assets + "/" + rel
    }

    function fileUrl(path) {
        if (!path || path === "")
            return ""
        if (path.indexOf("file://") === 0 || path.indexOf("image://") === 0 || path.indexOf("qrc:") === 0)
            return path
        const normalized = path.replace(/\\/g, "/")
        if (/^[A-Za-z]:\//.test(normalized))
            return "file:///" + normalized
        if (normalized.charAt(0) === "/")
            return "file://" + normalized
        return Qt.resolvedUrl(normalized)
    }

    function shader(rel) {
        return fileUrl(shaders + "/" + rel)
    }
}
