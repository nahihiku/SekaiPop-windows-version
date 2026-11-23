import QtQuick
import QtQuick.Window
import Common
import Sekai.background

/**
 * Per-monitor Windows desktop wallpaper window pinned at HWND_BOTTOM by WallpaperHost.
 * Supports SekaiPop's compiled shader transitions (Ring, Iris, Star, Blocks, Flash, Fade).
 */
Window {
    id: win

    required property var modelData
    required property var contextMenu

    screen: modelData
    visible: true
    color: "#1a1926"
    flags: Qt.FramelessWindowHint | Qt.WindowDoesNotAcceptFocus | Qt.Tool

    x: modelData ? modelData.virtualX : 0
    y: modelData ? modelData.virtualY : 0
    width: modelData ? modelData.width : 1920
    height: modelData ? modelData.height : 1080

    readonly property var mt: Theme.of(win)
    readonly property string screenName: modelData ? modelData.name : ""
    readonly property string wantedPath: WallpaperService.pathFor(win.screenName)
    readonly property string wantedImage: WallpaperService.imageFor(win.wantedPath)

    property string shownImage: ""
    property real progress: 1.0
    property string activeStyle: "sekaiRing"
    readonly property bool transitioning: win.previousSlot !== null
    property Item previousSlot: null
    property Item currentSlot: null

    readonly property size sourceSize: Qt.size(
        Math.max(1, Math.ceil(width * (modelData ? (modelData.devicePixelRatio || 1) : 1))),
        Math.max(1, Math.ceil(height * (modelData ? (modelData.devicePixelRatio || 1) : 1)))
    )

    Component {
        id: textureSlotComponent
        WallpaperTextureSlot {}
    }

    function createSlot(imagePath) {
        const obj = textureSlotComponent.createObject(pic, {
            sourceSize: win.sourceSize,
            source: imagePath ? Directories.fileUrl(imagePath) : ""
        })
        obj.sourceSize = Qt.binding(() => win.sourceSize)
        obj.statusChanged.connect(() => {
            const done = obj.status === Image.Ready || obj.status === Image.Error
            if (done)
                obj.sourceSize = Qt.size(obj.sourceSize.width, obj.sourceSize.height)
            if (obj === win.currentSlot && done)
                win.slotReady()
        })
        bindRole(obj)
        return obj
    }

    function bindRole(obj) {
        obj.visible = Qt.binding(() => obj === win.currentSlot && win.previousSlot === null)
        obj.layer.enabled = Qt.binding(() => obj === win.previousSlot
            || (obj === win.currentSlot && win.previousSlot !== null))
    }

    function swapTo(imagePath) {
        if (win.currentSlot && win.currentSlot.status !== Image.Ready) {
            win.currentSlot.destroy()
            win.currentSlot = win.createSlot(imagePath)
            return
        }
        if (win.previousSlot)
            win.previousSlot.destroy()
        win.previousSlot = win.currentSlot
        win.currentSlot = win.createSlot(imagePath)
    }

    function finishLocalTransition() {
        if (win.previousSlot) {
            win.previousSlot.destroy()
            win.previousSlot = null
        }
        win.progress = 1.0
    }

    function showWallpaper(imagePath) {
        if (imagePath === "" || imagePath === win.shownImage)
            return
        win.shownImage = imagePath
        win.activeStyle = WallpaperService.styleForSwitch()
        blendAnim.stop()
        warmup.stop()
        win.progress = 0.0
        win.swapTo(imagePath)
        readyTimeout.restart()
    }

    function slotReady() {
        readyTimeout.stop()
        warmup.restart()
    }

    onWantedImageChanged: win.showWallpaper(win.wantedImage)

    Component.onCompleted: {
        ShellController.registerWallpaperWindow(win)
        if (win.wantedImage !== "") {
            win.shownImage = win.wantedImage
            win.currentSlot = win.createSlot(win.wantedImage)
        }
    }

    Timer {
        id: readyTimeout
        interval: 4000
        onTriggered: win.slotReady()
    }

    Timer {
        id: warmup
        interval: 40
        onTriggered: {
            if (win.previousSlot)
                blendAnim.restart()
        }
    }

    NumberAnimation {
        id: blendAnim
        target: win
        property: "progress"
        from: 0.0
        to: 1.0
        duration: win.activeStyle === "sekaiIris" ? 2000
            : (win.activeStyle === "sekaiRing"
               || win.activeStyle === "star"
               || win.activeStyle === "diagonalBlocks") ? WallpaperService.ringDuration
            : WallpaperService.transitionDuration
        easing.type: (win.activeStyle === "sekaiIris"
            || win.activeStyle === "sekaiRing"
            || win.activeStyle === "star"
            || win.activeStyle === "diagonalBlocks") ? Easing.Linear : Easing.OutCubic
        onFinished: win.finishLocalTransition()
    }

    Item {
        id: pic
        anchors.fill: parent

        ShaderEffect {
            id: blend
            anchors.fill: parent
            visible: win.previousSlot !== null

            readonly property string shaderBase: {
                switch (win.activeStyle) {
                case "sekaiIris": return "sekai_iris"
                case "sekaiRing": return "sekai_ring"
                case "star": return "sekai_star"
                case "diagonalBlocks": return "sekai_blocks"
                case "sekaiFlash": return "sekai_flash"
                default: return "fade"
                }
            }

            property var source1: win.previousSlot
            property var source2: win.currentSlot
            property real progress: win.progress
            property real aspect: height > 0 ? width / height : 1.0
            property color accentColor: mt.accentCyan

            readonly property var tc: mt.transitionColors(ThemeService.palette)
            property color colorWhite: tc.white
            property color colorPink: tc.pink
            property color colorGreen: tc.green
            property color colorCyan: tc.cyan

            property int transitionStyle: WallpaperService.ringVariant
            property real distortion: WallpaperService.ringDistortion
            property real speed: WallpaperService.ringSpeed
            property real ringWidth: WallpaperService.ringWidth

            readonly property bool isStar: win.activeStyle === "star"
            property real colorIntensity: isStar ? WallpaperService.starColorIntensity : WallpaperService.ringColorIntensity
            property vector2d center: isStar ? WallpaperService.starCenter : WallpaperService.ringCenter

            property real thickness: WallpaperService.starThickness
            property real pointLength: WallpaperService.starPointLength
            property real curvature: WallpaperService.starCurvature
            property real elastic: WallpaperService.starElastic
            property real spin: WallpaperService.starSpin

            property vector2d direction: WallpaperService.blocksDirection
            property real bandThickness: WallpaperService.blocksBandThickness
            property real blockSize: WallpaperService.blocksBlockSize
            property real stagger: WallpaperService.blocksStagger

            fragmentShader: Directories.shader(blend.shaderBase + ".frag.qsb")
            vertexShader: Directories.shader(blend.shaderBase + ".vert.qsb")
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.RightButton
        onClicked: mouse => {
            if (win.contextMenu) {
                const gp = mapToGlobal(mouse.x, mouse.y)
                win.contextMenu.openAt(gp.x, gp.y, win.screenName, win.modelData)
            }
        }
    }
}
