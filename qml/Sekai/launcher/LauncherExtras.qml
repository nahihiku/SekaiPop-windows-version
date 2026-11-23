import QtQuick
import QtQuick.Window
import Common
import Common.widgets
import "../../Common/functions/Extras.js" as X
import "Calc.js" as Calc

/**
 * Windows-native Launcher Extras: CalculatorCard, ColorCard, WebCard, > / $ Windows
 * command runner, and quick Windows/Emergency system actions.
 */
Item {
    id: root

    readonly property var mt: Theme.of(Window.window)

    property string query: ""
    property bool active: false
    property bool present: false
    property int appCount: 0
    property real pageWidth: 550
    property real pageHeight: 700

    signal doneRequested()
    signal setTextRequested(string text)

    readonly property var routed: X.route(root.query)
    readonly property string mode: routed.mode
    readonly property string arg: routed.arg
    readonly property string prefix: routed.prefix
    readonly property bool terminalCommand: routed.terminal

    property int engineOverride: -1
    readonly property int engine: engineOverride >= 0 ? engineOverride : routed.engine
    onModeChanged: engineOverride = -1

    property var calcHistory: []

    readonly property string cardMode: ["calc", "color", "web"].indexOf(root.mode) >= 0 ? root.mode : ""
    readonly property bool cardShown: cardMode !== ""
    property string shownMode: ""

    onCardModeChanged: {
        if (root.cardMode !== "") {
            holdTimer.stop()
            root.shownMode = root.cardMode
        } else if (root.shownMode !== "") {
            holdTimer.restart()
        }
    }
    Component.onCompleted: root.shownMode = root.cardMode
    onPresentChanged: {
        if (!root.present && !root.active && root.cardMode === "") {
            holdTimer.stop()
            root.shownMode = ""
        }
    }
    Timer {
        id: holdTimer
        interval: 150
        onTriggered: {
            if (root.cardMode === "")
                root.shownMode = ""
        }
    }

    property real cardK: root.cardMode !== "" ? 1 : 0
    Behavior on cardK {
        enabled: root.present && root.active
        NumberAnimation { duration: 120; easing.type: Easing.OutCubic }
    }

    function actionEntry(a) {
        return {
            isAction: true,
            id: a.id,
            kind: "action",
            title: a.title,
            sub: a.sub,
            glyph: a.glyph,
            confirm: a.confirm === true,
            def: a
        }
    }

    readonly property var rowsAll: {
        if (root.cardMode !== "")
            return { rows: [], strong: 0 }
        if (root.mode === "shell") {
            const cmd = root.arg.trim()
            return {
                rows: [{
                    isAction: true,
                    id: "shell",
                    kind: "shell",
                    title: cmd === "" ? (root.terminalCommand ? "Run in Windows Command Prompt" : "Run a Windows command") : cmd,
                    sub: cmd === "" ? "> runs hidden, $ opens a Command Prompt window" : (root.terminalCommand ? "Runs in cmd.exe /k" : "Runs hidden in background"),
                    glyph: root.terminalCommand ? "terminal" : "play_arrow",
                    command: cmd,
                    terminal: root.terminalCommand,
                    confirm: false
                }],
                strong: 1
            }
        }
        const above = []
        const below = []
        const matches = root.mode === "" ? X.matchActions(root.query) : []
        for (const m of matches)
            (m.score >= 80 ? above : below).push(root.actionEntry(m.action))
        const q = root.query.trim()
        if (above.length + below.length === 0 && root.appCount === 0 && q.length > 0) {
            return {
                rows: [{
                    isAction: true,
                    id: "websearch",
                    kind: "web",
                    title: "Search the web for \"" + q + "\"",
                    sub: "Opens in your default Windows browser",
                    glyph: "travel_explore",
                    confirm: false
                }],
                strong: 1
            }
        }
        return { rows: above.concat(below), strong: above.length }
    }
    readonly property var rows: rowsAll.rows
    readonly property int strongRows: rowsAll.strong

    property int rowPick: -1
    property string pendingConfirm: ""
    property var rowsHold: []

    onRowsChanged: {
        pendingConfirm = ""
        rowPick = strongRows > 0 ? 0 : -1
        if (rows.length > 0) {
            rowsHoldTimer.stop()
            rowsHold = rows.slice(0, maxRows)
        } else if (rowsHold.length > 0) {
            rowsHoldTimer.restart()
        }
    }
    Timer {
        id: rowsHoldTimer
        interval: 120
        onTriggered: {
            if (root.rows.length === 0)
                root.rowsHold = []
        }
    }
    onRowPickChanged: pendingConfirm = ""

    readonly property int rowH: 64
    readonly property int rowGap: 8
    readonly property int maxRows: 3
    readonly property int shownRows: Math.min(rows.length, maxRows)
    readonly property real rowsTarget: shownRows > 0 ? shownRows * rowH + (shownRows - 1) * rowGap + 14 : 0
    property real rowsHeight: root.rowsTarget
    Behavior on rowsHeight {
        enabled: root.present && root.active
        NumberAnimation { duration: 110; easing.type: Easing.OutCubic }
    }

    function navigate(dx, dy) {
        if (root.cardMode !== "") {
            const it = cardLoader.item
            if (it && it.nav)
                it.nav(dx, dy)
            return true
        }
        return false
    }

    function cycleCardTab(step) {
        return false
    }

    function moveRows(dy, gridHasPick, gridOnFirstRow) {
        const n = root.rows.length
        if (n === 0)
            return ""
        if (root.rowPick >= 0) {
            if (dy > 0) {
                if (root.rowPick < n - 1) {
                    root.rowPick++
                    return "rows"
                }
                if (root.appCount > 0) {
                    root.rowPick = -1
                    return "grid"
                }
                return "rows"
            }
            if (root.rowPick > 0)
                root.rowPick--
            return "rows"
        }
        if (dy < 0 && gridHasPick && gridOnFirstRow) {
            root.rowPick = Math.min(n, root.maxRows) - 1
            return "rows"
        }
        return ""
    }

    function enter(shift) {
        if (root.cardMode !== "") {
            const it = cardLoader.item
            if (it && it.activate)
                it.activate(shift)
            return true
        }
        if (root.rowPick >= 0 && root.rowPick < root.rows.length) {
            root.activateRow(root.rows[root.rowPick])
            return true
        }
        return false
    }

    function tab(step) {
        if (root.cardMode !== "web")
            return false
        engineOverride = (engine + step + X.ENGINES.length) % X.ENGINES.length
        return true
    }

    function runActionId(id) {
        switch (id) {
        case "explorer":
            ShellController.restoreExplorer()
            return
        case "taskmgr":
            QuickSource.openTaskManager()
            return
        case "winsettings":
            QuickSource.openWindowsSettings("")
            return
        case "wallpaper":
            WallpaperService.pickWallpaperDialog("")
            return
        case "theme":
            ThemeService.toggleDark()
            return
        case "palette":
            ThemeService.cycleTheme()
            return
        case "mutespk":
            QuickSource.toggleSpeakerMute()
            return
        case "mutemic":
            QuickSource.toggleMicrophoneMute()
            return
        case "exitshell":
            ShellController.exitShell()
            return
        }
    }

    function activateRow(entry) {
        if (!entry)
            return
        if (entry.confirm && root.pendingConfirm !== entry.id) {
            root.pendingConfirm = entry.id
            return
        }
        root.pendingConfirm = ""
        if (entry.kind === "shell") {
            if (!entry.command || entry.command.trim() === "")
                return
            QuickSource.runCommand(entry.command, entry.terminal === true)
            root.doneRequested()
            return
        }
        if (entry.kind === "web") {
            const q = root.query.trim()
            if (q !== "") {
                const url = X.looksLikeUrl(q) ? X.urlFor(q) : X.searchUrl(0, q)
                QuickSource.openUrl(url)
                root.doneRequested()
            }
            return
        }
        if (entry.kind === "action") {
            root.runActionId(entry.id)
            root.doneRequested()
        }
    }

    Column {
        id: rowsCol
        width: root.pageWidth
        spacing: root.rowGap
        visible: root.rowsHold.length > 0 && root.cardK < 0.99
        opacity: 1 - root.cardK

        Repeater {
            model: root.rowsHold
            delegate: AppResultRow {
                required property var modelData
                required property int index
                width: root.pageWidth
                height: root.rowH
                app: modelData
                selected: index === root.rowPick
                confirming: root.pendingConfirm === modelData.id
                onActivated: root.activateRow(modelData)
            }
        }
    }

    Loader {
        id: cardLoader
        width: root.pageWidth
        height: root.pageHeight
        active: root.shownMode !== "" && root.present
        opacity: root.cardK
        scale: 0.96 + 0.04 * root.cardK
        transformOrigin: Item.Top
        visible: active && opacity > 0.01

        sourceComponent: {
            if (root.shownMode === "calc") return calcComp
            if (root.shownMode === "color") return colorComp
            if (root.shownMode === "web") return webComp
            return null
        }
    }

    Component {
        id: calcComp
        CalculatorCard {
            pageW: root.pageWidth
            pageH: root.pageHeight
            expr: root.arg
            history: root.calcHistory
            onInsert: t => root.setTextRequested((root.prefix || "= ") + root.arg + t)
            onBackspace: root.setTextRequested((root.prefix || "= ") + root.arg.slice(0, -1))
            onClearAll: root.setTextRequested(root.prefix || "= ")
            onEquals: {
                const r = Calc.evaluate(root.arg)
                if (r.ok) {
                    const next = root.calcHistory.concat([{ expr: root.arg, result: r.formatted }])
                    root.calcHistory = next.slice(-6)
                    root.setTextRequested((root.prefix || "= ") + r.formatted)
                }
            }
            onCopyResult: {
                const r = Calc.evaluate(root.arg)
                if (r.ok) {
                    QuickSource.copyText(r.formatted)
                    root.doneRequested()
                }
            }
            onUseHistory: idx => {
                if (idx >= 0 && idx < root.calcHistory.length)
                    root.setTextRequested((root.prefix || "= ") + root.calcHistory[idx].expr)
            }
        }
    }

    Component {
        id: colorComp
        ColorCard {
            width: root.pageWidth
            arg: root.arg
            onCopyRequested: text => {
                QuickSource.copyText(text)
                root.doneRequested()
            }
            onActivated: hex => {
                QuickSource.copyText(hex)
                root.doneRequested()
            }
            onShadePicked: hex => root.setTextRequested(hex)
            onPickRequested: {}
        }
    }

    Component {
        id: webComp
        WebCard {
            pageW: root.pageWidth
            pageH: root.pageHeight
            arg: root.arg
            engine: root.engine
            engines: X.ENGINES
            suggestions: []
            onOpenRequested: url => {
                QuickSource.openUrl(url)
                root.doneRequested()
            }
            onEngineChosen: idx => {
                root.engineOverride = idx
            }
        }
    }
}
