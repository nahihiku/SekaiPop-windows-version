import QtQuick
import Common

Item {
    id: root

    readonly property var mt: Theme.of(Window.window)

    property bool lite: false
    readonly property color safeInk: (mt && mt.colBorder !== undefined) ? mt.colBorder : Qt.color("#1c2a4a")
    readonly property color safeMint: (mt && mt.accentMint !== undefined) ? mt.accentMint : Qt.color("#00ffcb")
    readonly property color safeCyan: (mt && mt.accentCyan !== undefined) ? mt.accentCyan : Qt.color("#22e6d3")
    readonly property color safePink: (mt && mt.accentPink !== undefined) ? mt.accentPink : Qt.color("#ff8fd6")
    property color ink: safeInk
    property color shadowColor: Qt.rgba(safeInk.r, safeInk.g, safeInk.b, 0.25)
    property var fills: [
        Qt.rgba(safeMint.r, safeMint.g, safeMint.b, 0.42),
        Qt.rgba(safeCyan.r, safeCyan.g, safeCyan.b, 0.42),
        Qt.rgba(safePink.r, safePink.g, safePink.b, 0.48)
    ]

    readonly property int cols: 80
    readonly property real pad: 28
    readonly property real lineW: 3
    readonly property real shadowOff: 6

    readonly property var spec: [
        { base: 0.28, amp: 0.62, rate: 5.5, rad: 3, pass: 2, len: 6.2, speed: 0.9 },
        { base: 0.20, amp: 0.56, rate: 8.5, rad: 2, pass: 2, len: 8.4, speed: 1.3 },
        { base: 0.13, amp: 0.50, rate: 12.0, rad: 2, pass: 1, len: 10.5, speed: 1.8 }
    ]

    readonly property var laneX: [0.12, 0.21, 0.31, 0.41, 0.50, 0.59, 0.69, 0.79, 0.88]

    property var st: [new Array(cols + 1).fill(0), new Array(cols + 1).fill(0), new Array(cols + 1).fill(0)]
    property real lastTime: 0
    property real bassSmooth: 0

    function blur(arr) {
        const n = arr.length
        const out = new Array(n)
        for (let i = 0; i < n; i++) {
            const l = arr[Math.max(0, i - 1)]
            const m = arr[i]
            const r = arr[Math.min(n - 1, i + 1)]
            out[i] = 0.25 * l + 0.5 * m + 0.25 * r
        }
        return out
    }

    function rebuild() {
        const W = width
        const H = height
        if (W <= 20 || H <= 20) return

        const lv = AudioSpectrumSource.levels
        const pk = AudioSpectrumSource.peaks
        const nb = Math.max(1, lv.length)
        const n = root.cols
        const span = W + 2 * root.pad

        const tNow = AudioSpectrumSource.time
        const dt = Math.min(0.06, Math.max(0.004, tNow - root.lastTime))
        root.lastTime = tNow

        root.bassSmooth += (AudioSpectrumSource.bass - root.bassSmooth) * Math.min(1, dt * 8)
        const ampMul = 1 + 0.35 * root.bassSmooth
        const fadeVal = Math.max(0.45, AudioSpectrumSource.fade)

        const raw = new Array(n + 1)
        let mean = 0
        for (let c = 0; c <= n; c++) {
            const d = Math.abs(c / n - 0.5) * 2
            const f = d * (nb - 1) * 0.8
            const i0 = Math.floor(f)
            const i1 = Math.min(nb - 1, i0 + 1)
            const t = f - i0
            const val = lv && lv.length > 0 ? (Number(lv[i0]) * (1 - t) + Number(lv[i1]) * t) : 0.08
            raw[c] = Math.pow(Math.min(1, val * (1 + d * 1.1)), 0.8)
            mean += raw[c] / (n + 1)
        }
        const swellAmp = (0.045 + 0.06 * Math.min(1, mean * 2.5)) * H * fadeVal

        const xs = new Array(n + 1)
        for (let c = 0; c <= n; c++) xs[c] = -root.pad + c / n * span

        let tops = null
        for (let k = 0; k < 3; k++) {
            const L = root.spec[k]
            const s = root.st[k]
            const a = Math.min(1, dt * L.rate)
            const ys = new Array(n + 1)
            for (let c = 0; c <= n; c++) {
                let sum = 0, cnt = 0
                for (let j = -L.rad; j <= L.rad; j++) {
                    const cc = c + j
                    if (cc < 0 || cc > n) continue
                    sum += raw[cc]
                    cnt++
                }
                s[c] += (sum / cnt - s[c]) * a
            }
            let v = s
            for (let q = 0; q < L.pass; q++) v = root.blur(v)
            for (let c = 0; c <= n; c++) {
                const u = xs[c] / W
                const swell = swellAmp * (Math.sin(u * L.len - tNow * L.speed + k * 2.1)
                    + 0.5 * Math.sin(u * L.len * 1.9 + tNow * L.speed * 0.6 + k))
                ys[c] = Math.max(8, H - (L.base * H + v[c] * L.amp * H * 0.68 * ampMul + swell))
            }
            shaderLayer.setSheet(k, ys)
            if (k === 2) tops = ys
        }

        const marks = []
        for (let m = 0; m < root.laneX.length; m++) {
            const xf = root.laneX[m]
            const d = Math.abs(xf - 0.5) * 2
            const f = d * (nb - 1) * 0.8
            const pVal = pk && pk.length > 0
                ? Math.min(1, Math.max(Number(pk[Math.floor(f)]), Number(pk[Math.min(nb - 1, Math.ceil(f))])) * (1 + d * 1.1))
                : 0.1
            const col = Math.max(0, Math.min(n, Math.round((xf * W + root.pad) / span * n)))
            const crest = tops[col]
            const lift = 14 + Math.min(30, pVal * H * 0.16)
            marks.push({ x: xf * W, y: Math.max(10, crest - lift), cy: crest, a: Math.min(1, pVal * 4) * AudioSpectrumSource.fade, s: pVal })
        }
        shaderLayer.setPins(marks)
    }

    Connections {
        target: AudioSpectrumSource
        function onUpdated() { root.rebuild() }
    }

    onWidthChanged: Qt.callLater(rebuild)
    onHeightChanged: Qt.callLater(rebuild)
    Component.onCompleted: Qt.callLater(rebuild)

    TideShaderLayer {
        id: shaderLayer
        anchors.fill: parent
        ink: root.ink
        shadowColor: root.lite ? "transparent" : root.shadowColor
        fillBack: root.fills[0]
        fillMid: root.fills[1]
        fillFront: root.fills[2]
        columns: root.cols
        pad: root.pad
        lineW: root.lineW
        shadowDrop: root.shadowOff
    }
}
