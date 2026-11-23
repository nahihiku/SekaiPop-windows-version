#include "WindowTracker.h"
#include "Win32Utils.h"
#include "WinIconProvider.h"

#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QScreen>
#include <QSet>
#include <algorithm>
#include <dwmapi.h>
#include <iterator>
#include <shobjidl.h>
#include <vector>

#ifndef EVENT_OBJECT_SHOW
#define EVENT_OBJECT_SHOW 0x8002
#endif
#ifndef EVENT_OBJECT_HIDE
#define EVENT_OBJECT_HIDE 0x8003
#endif
#ifndef EVENT_OBJECT_CLOAKED
#define EVENT_OBJECT_CLOAKED 0x8017
#endif
#ifndef EVENT_OBJECT_UNCLOAKED
#define EVENT_OBJECT_UNCLOAKED 0x8018
#endif
#ifndef DWM_CLOAKED_APP
#define DWM_CLOAKED_APP 0x00000001
#endif
#ifndef DWM_CLOAKED_SHELL
#define DWM_CLOAKED_SHELL 0x00000002
#endif

namespace SekaiPop::Win32 {

WindowTracker* WindowTracker::s_instance = nullptr;

namespace {

DWORD windowCloakReason(HWND hwnd) {
    DWORD cloaked = 0;
    const HRESULT hr = DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    return SUCCEEDED(hr) ? cloaked : 0;
}

QString getWindowTitleStr(HWND hwnd) {
    const int len = GetWindowTextLengthW(hwnd);
    if (len <= 0) {
        return {};
    }
    std::vector<wchar_t> buf(static_cast<size_t>(len) + 1);
    const int got = GetWindowTextW(hwnd, buf.data(), len + 1);
    if (got <= 0) {
        return {};
    }
    return QString::fromWCharArray(buf.data(), got).trimmed();
}

QString getWindowClassStr(HWND hwnd) {
    wchar_t cls[256] = {};
    const int len = GetClassNameW(hwnd, cls, 256);
    if (len <= 0) {
        return {};
    }
    return QString::fromWCharArray(cls, len);
}

QHash<DWORD, QString>& pidExeCache() {
    static QHash<DWORD, QString> cache;
    return cache;
}

QString queryExePathForPid(DWORD pid) {
    if (pid == 0) {
        return {};
    }
    auto& cache = pidExeCache();
    auto it = cache.constFind(pid);
    if (it != cache.constEnd()) {
        return *it;
    }
    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!proc) {
        return {};
    }
    wchar_t buf[MAX_PATH * 2] = {};
    DWORD len = static_cast<DWORD>(std::size(buf));
    QString exePath;
    if (QueryFullProcessImageNameW(proc, 0, buf, &len) && len > 0) {
        exePath = QString::fromWCharArray(buf, static_cast<int>(len));
    }
    CloseHandle(proc);
    if (!exePath.isEmpty()) {
        if (cache.size() > 256) {
            cache.clear();
        }
        cache.insert(pid, exePath);
    }
    return exePath;
}

DWORD resolveEffectiveAppPid(HWND hwnd) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) {
        return 0;
    }
    if (getWindowClassStr(hwnd) == QStringLiteral("ApplicationFrameWindow")) {
        struct ChildCtx {
            DWORD framePid;
            DWORD appPid;
        } c{ pid, 0 };
        EnumChildWindows(hwnd, [](HWND child, LPARAM lp) -> BOOL {
            auto* ctx = reinterpret_cast<ChildCtx*>(lp);
            DWORD cpid = 0;
            GetWindowThreadProcessId(child, &cpid);
            if (cpid != 0 && cpid != ctx->framePid) {
                ctx->appPid = cpid;
                return FALSE;
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&c));
        if (c.appPid != 0) {
            return c.appPid;
        }
    }
    return pid;
}

bool isIgnoredSystemProcessExe(const QString& exeLower) {
    static const QSet<QString> ignored = {
        QStringLiteral("searchhost.exe"),
        QStringLiteral("searchapp.exe"),
        QStringLiteral("startmenuexperiencehost.exe"),
        QStringLiteral("shellexperiencehost.exe"),
        QStringLiteral("textinputhost.exe"),
        QStringLiteral("lockapp.exe"),
        QStringLiteral("widgetboard.exe"),
        QStringLiteral("widgets.exe"),
        QStringLiteral("crossdeviceresume.exe"),
        QStringLiteral("appactions.exe"),
        QStringLiteral("shellhost.exe"),
        QStringLiteral("nvidia overlay.exe"),
        QStringLiteral("nvcontainer.exe"),
        QStringLiteral("gamebar.exe"),
        QStringLiteral("wallpaper64.exe"),
        QStringLiteral("wallpaper32.exe"),
        QStringLiteral("translucenttb.exe"),
        QStringLiteral("useroobebroker.exe")
    };
    return ignored.contains(exeLower);
}

QString screenNameForHwnd(HWND hwnd) {
    HMONITOR hMon = nullptr;
    if (IsIconic(hwnd)) {
        WINDOWPLACEMENT wp = {};
        wp.length = sizeof(wp);
        if (GetWindowPlacement(hwnd, &wp)) {
            hMon = MonitorFromRect(&wp.rcNormalPosition, MONITOR_DEFAULTTONEAREST);
        }
    }
    if (!hMon) {
        hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    }
    MONITORINFOEXW mi = {};
    mi.cbSize = sizeof(mi);
    if (hMon && GetMonitorInfoW(hMon, &mi)) {
        const QString dev = QString::fromWCharArray(mi.szDevice);
        const QPoint monCenter(
            (mi.rcMonitor.left + mi.rcMonitor.right) / 2,
            (mi.rcMonitor.top + mi.rcMonitor.bottom) / 2
        );
        for (QScreen* s : QGuiApplication::screens()) {
            if (!s) continue;
            if (s->name().compare(dev, Qt::CaseInsensitive) == 0) {
                return s->name();
            }
        }
        // Fallback: match by physical monitor rect center scaled by DPR
        for (QScreen* s : QGuiApplication::screens()) {
            if (!s) continue;
            const QRect g = s->geometry();
            const qreal dpr = qMax<qreal>(0.5, s->devicePixelRatio());
            const QRect phys(
                g.x(), g.y(),
                qRound(g.width() * dpr),
                qRound(g.height() * dpr)
            );
            if (phys.contains(monCenter) || g.contains(monCenter)) {
                return s->name();
            }
        }
    }
    if (QScreen* p = QGuiApplication::primaryScreen()) {
        return p->name();
    }
    return QString();
}

bool isWindowMaximizedOrFullscreen(HWND hwnd) {
    if (!hwnd || IsIconic(hwnd)) {
        return false;
    }
    if (IsZoomed(hwnd)) {
        return true;
    }
    RECT rc = {};
    if (!GetWindowRect(hwnd, &rc)) {
        return false;
    }
    HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (!hMon || !GetMonitorInfoW(hMon, &mi)) {
        return false;
    }
    const int monW = qMax(1, static_cast<int>(mi.rcMonitor.right - mi.rcMonitor.left));
    const int monH = qMax(1, static_cast<int>(mi.rcMonitor.bottom - mi.rcMonitor.top));
    const int winW = static_cast<int>(rc.right - rc.left);
    const int winH = static_cast<int>(rc.bottom - rc.top);
    return (winW >= monW * 94 / 100) && (winH >= monH * 94 / 100);
}

bool isWindowTrueFullscreenOnMonitor(HWND hwnd, DWORD ownPid, HMONITOR& outMon, bool& outIsSubstantialApp) {
    outMon = nullptr;
    outIsSubstantialApp = false;
    if (!hwnd || !IsWindow(hwnd) || !IsWindowVisible(hwnd) || IsIconic(hwnd)) {
        return false;
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0 || pid == ownPid) {
        return false;
    }
    if (windowCloakReason(hwnd) != 0) {
        return false;
    }
    const LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if ((exStyle & WS_EX_TOOLWINDOW) != 0) {
        return false;
    }
    const QString cls = getWindowClassStr(hwnd);
    if (cls == QStringLiteral("Progman") ||
        cls == QStringLiteral("WorkerW") ||
        cls == QStringLiteral("Shell_TrayWnd") ||
        cls == QStringLiteral("Shell_SecondaryTrayWnd") ||
        cls == QStringLiteral("Windows.UI.Core.CoreWindow") ||
        cls == QStringLiteral("ForegroundStaging") ||
        cls == QStringLiteral("ApplicationManager_DesktopShellWindow")) {
        return false;
    }
    RECT rc = {};
    if (!GetWindowRect(hwnd, &rc)) {
        return false;
    }
    const int winW = static_cast<int>(rc.right - rc.left);
    const int winH = static_cast<int>(rc.bottom - rc.top);
    if (winW < 200 || winH < 150) {
        return false;
    }

    const QString exeFile = QFileInfo(queryExePathForPid(pid)).fileName().toLower();
    if (!exeFile.isEmpty() && isIgnoredSystemProcessExe(exeFile)) {
        return false;
    }

    HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    if (!hMon) {
        return false;
    }
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(hMon, &mi)) {
        return false;
    }
    outMon = hMon;

    const int monW = qMax(1, static_cast<int>(mi.rcMonitor.right - mi.rcMonitor.left));
    const int monH = qMax(1, static_cast<int>(mi.rcMonitor.bottom - mi.rcMonitor.top));
    const qint64 monArea = static_cast<qint64>(monW) * static_cast<qint64>(monH);
    const qint64 winArea = static_cast<qint64>(winW) * static_cast<qint64>(winH);
    if (winArea >= (monArea * 45) / 100) {
        outIsSubstantialApp = true;
    }

    const bool coversMonitor =
        (rc.left <= mi.rcMonitor.left + 2) &&
        (rc.top <= mi.rcMonitor.top + 2) &&
        (rc.right >= mi.rcMonitor.right - 2) &&
        (rc.bottom >= mi.rcMonitor.bottom - 2);
    if (!coversMonitor) {
        return false;
    }

    // If a bar reserves workarea (rcWork != rcMonitor), covering the full rcMonitor
    // (including the bar strip!) guarantees this is true fullscreen / borderless fullscreen,
    // because normal maximized windows stop at rcWork.
    if (!EqualRect(&mi.rcWork, &mi.rcMonitor)) {
        return true;
    }

    // If rcWork == rcMonitor (e.g., auto-hide bar), distinguish true fullscreen from normal
    // maximized windows by checking for borderless/captionless or non-zoomed fullscreen frame.
    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    const bool hasCaptionAndFrame = ((style & WS_CAPTION) == WS_CAPTION) && ((style & WS_THICKFRAME) != 0);
    return !IsZoomed(hwnd) || !hasCaptionAndFrame;
}

} // namespace

WindowTracker::WindowTracker(QObject* parent)
    : QObject(parent)
    , m_ownPid(GetCurrentProcessId()) {
    s_instance = this;

    // CoCreateInstance for IVirtualDesktopManager is optional; fails cleanly if unavailable.
    CoCreateInstance(CLSID_VirtualDesktopManager, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&m_vdm));

    m_debounce.setSingleShot(true);
    m_debounce.setInterval(45);
    connect(&m_debounce, &QTimer::timeout, this, &WindowTracker::refresh);

    m_nameDebounce.setSingleShot(true);
    m_nameDebounce.setInterval(180);
    connect(&m_nameDebounce, &QTimer::timeout, this, &WindowTracker::refresh);

    m_reconcileTimer.setInterval(3000);
    connect(&m_reconcileTimer, &QTimer::timeout, this, [this]() {
        if (++m_reconcileTicks >= 10) {
            m_reconcileTicks = 0;
            pidExeCache().clear();
        }
        refresh();
    });
    m_reconcileTimer.start();

    // Lightweight 500ms fullscreen check so pressing F/F11 in YouTube/Netflix/VLC
    // or entering/exiting a borderless game hides/restores the Bar almost instantaneously.
    m_fsPollTimer.setInterval(500);
    connect(&m_fsPollTimer, &QTimer::timeout, this, &WindowTracker::pollFullscreenMonitors);
    m_fsPollTimer.start();

    auto installHook = [this](DWORD evMin, DWORD evMax) {
        HWINEVENTHOOK h = SetWinEventHook(
            evMin, evMax,
            nullptr,
            &WindowTracker::winEventProc,
            0, 0,
            WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS
        );
        if (h) {
            m_hooks.push_back(h);
        }
    };

    installHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND);
    installHook(EVENT_SYSTEM_MOVESIZEEND, EVENT_SYSTEM_MOVESIZEEND);
    installHook(EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND);
    installHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_HIDE);
    installHook(EVENT_OBJECT_NAMECHANGE, EVENT_OBJECT_NAMECHANGE);
    installHook(EVENT_OBJECT_CLOAKED, EVENT_OBJECT_UNCLOAKED);

    refresh();
}

WindowTracker::~WindowTracker() {
    restoreAllWorkspaces();
    for (HWINEVENTHOOK h : m_hooks) {
        UnhookWinEvent(h);
    }
    m_hooks.clear();
    if (m_vdm) {
        m_vdm->Release();
        m_vdm = nullptr;
    }
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

void CALLBACK WindowTracker::winEventProc(
    HWINEVENTHOOK,
    DWORD event,
    HWND hwnd,
    LONG idObject,
    LONG idChild,
    DWORD,
    DWORD
) {
    if (!s_instance) {
        return;
    }
    if (idObject != OBJID_WINDOW || idChild != CHILDID_SELF || !hwnd) {
        if (event != EVENT_SYSTEM_FOREGROUND && event != EVENT_SYSTEM_MOVESIZEEND) {
            return;
        }
    }
    if (event == EVENT_OBJECT_NAMECHANGE) {
        if (!hwnd || !IsWindowVisible(hwnd) || GetAncestor(hwnd, GA_ROOT) != hwnd) {
            return;
        }
        s_instance->scheduleNameRefresh();
        return;
    }
    if (event == EVENT_OBJECT_CREATE || event == EVENT_OBJECT_SHOW || event == EVENT_OBJECT_HIDE) {
        if (!hwnd || GetAncestor(hwnd, GA_ROOT) != hwnd) {
            return;
        }
        if (event == EVENT_OBJECT_CREATE && !IsWindowVisible(hwnd)) {
            return;
        }
    }
    s_instance->scheduleRefresh();
}

void WindowTracker::scheduleRefresh() {
    if (!m_debounce.isActive()) {
        m_debounce.start();
    }
}

void WindowTracker::scheduleNameRefresh() {
    if (!m_debounce.isActive() && !m_nameDebounce.isActive()) {
        m_nameDebounce.start();
    }
}

bool WindowTracker::isTrackableAppWindow(HWND hwnd) const {
    if (!hwnd || !IsWindow(hwnd) || !IsWindowVisible(hwnd)) {
        return false;
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0 || pid == m_ownPid) {
        return false;
    }
    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    const LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if ((style & WS_CHILD) != 0) {
        return false;
    }
    const bool hasAppWindow = (exStyle & WS_EX_APPWINDOW) != 0;
    if ((exStyle & WS_EX_TOOLWINDOW) != 0 && !hasAppWindow) {
        return false;
    }
    if ((exStyle & WS_EX_NOACTIVATE) != 0 && !hasAppWindow) {
        return false;
    }
    // Windows Taskbar rule: only reject an owned window if its owner is actually visible
    // (many real apps like Steam, Voicemeeter, Qt, Delphi, and Electron use an invisible dummy owner).
    if (!hasAppWindow) {
        HWND owner = GetWindow(hwnd, GW_OWNER);
        if (owner && owner != hwnd && IsWindowVisible(owner)) {
            return false;
        }
    }
    const bool iconic = IsIconic(hwnd) != FALSE;
    if (!iconic) {
        RECT rc = {};
        if (!GetWindowRect(hwnd, &rc) || (rc.right - rc.left) <= 0 || (rc.bottom - rc.top) <= 0) {
            return false;
        }
    }
    const QString cls = getWindowClassStr(hwnd);
    if (cls == QStringLiteral("Progman") ||
        cls == QStringLiteral("WorkerW") ||
        cls == QStringLiteral("Shell_TrayWnd") ||
        cls == QStringLiteral("Shell_SecondaryTrayWnd") ||
        cls == QStringLiteral("Shell_InputSwitchTopLevelWindow") ||
        cls == QStringLiteral("Windows.UI.Core.CoreWindow") ||
        cls == QStringLiteral("EdgeUiInputTopWndClass") ||
        cls == QStringLiteral("DummyDWMListenerWindow") ||
        cls == QStringLiteral("ForegroundStaging")) {
        return false;
    }

    const QString title = getWindowTitleStr(hwnd);
    if (title.isEmpty()) {
        return false;
    }

    const DWORD effPid = resolveEffectiveAppPid(hwnd);
    const QString exeFile = QFileInfo(queryExePathForPid(effPid)).fileName().toLower();
    if (!exeFile.isEmpty() && isIgnoredSystemProcessExe(exeFile)) {
        return false;
    }

    const DWORD cloaked = windowCloakReason(hwnd);
    if (cloaked != 0) {
        // App explicitly cloaked itself (hidden background helper/pre-launch) -> skip
        if ((cloaked & DWM_CLOAKED_APP) != 0) {
            return false;
        }
        // Shell-cloaked (DWM_CLOAKED_SHELL):
        // - Always keep minimized windows (Windows 11 cloaks minimized UWP/WinUI3 apps like WhatsApp & Notepad)
        // - Keep windows living on another Windows virtual desktop
        // - Skip suspended/pre-launched background UWP frames on the current desktop that aren't iconic
        if ((cloaked & DWM_CLOAKED_SHELL) != 0) {
            if (!iconic) {
                BOOL onCurrentDesktop = TRUE;
                if (m_vdm && SUCCEEDED(m_vdm->IsWindowOnCurrentVirtualDesktop(hwnd, &onCurrentDesktop)) && !onCurrentDesktop) {
                    return true;
                }
                if (cls == QStringLiteral("ApplicationFrameWindow") || exeFile == QStringLiteral("systemsettings.exe")) {
                    return false;
                }
            }
        }
    }
    return true;
}

QString WindowTracker::resolveAppNameForHwnd(HWND hwnd) const {
    static QHash<QString, QString> exeNameCache;
    const DWORD effPid = resolveEffectiveAppPid(hwnd);
    const QString exePath = queryExePathForPid(effPid);
    if (exePath.isEmpty()) {
        return {};
    }
    auto it = exeNameCache.constFind(exePath);
    if (it != exeNameCache.constEnd()) {
        return *it;
    }
    const QString stemLower = QFileInfo(exePath).completeBaseName().toLower();
    QString friendly;
    if (stemLower == QStringLiteral("steamwebhelper") || stemLower == QStringLiteral("steam")) {
        friendly = QStringLiteral("Steam");
    } else if (stemLower == QStringLiteral("whatsapp.root") || stemLower == QStringLiteral("whatsapp")) {
        friendly = QStringLiteral("WhatsApp");
    } else if (stemLower == QStringLiteral("explorer")) {
        friendly = QStringLiteral("File Explorer");
    } else if (stemLower == QStringLiteral("robloxplayerbeta")) {
        friendly = QStringLiteral("Roblox");
    } else if (stemLower == QStringLiteral("windowsterminal")) {
        friendly = QStringLiteral("Windows Terminal");
    } else if (stemLower.startsWith(QStringLiteral("voicemeeter"))) {
        friendly = QStringLiteral("Voicemeeter");
    } else {
        friendly = friendlyAppNameFromExe(exePath);
    }
    // Avoid generic host names like ApplicationFrameHost for UWP frames.
    if (friendly.compare(QStringLiteral("Application Frame Host"), Qt::CaseInsensitive) == 0) {
        friendly.clear();
    }
    if (exeNameCache.size() > 256) {
        exeNameCache.clear();
    }
    exeNameCache.insert(exePath, friendly);
    return friendly;
}

void WindowTracker::refresh() {
    HWND fg = GetForegroundWindow();
    quintptr nextHwnd = 0;
    QString nextApp;
    QString nextTitle;
    QString nextIcon;

    if (fg && IsWindow(fg)) {
        DWORD fgPid = 0;
        GetWindowThreadProcessId(fg, &fgPid);
        const QString cls = getWindowClassStr(fg);
        if (fgPid != m_ownPid &&
            cls != QStringLiteral("Progman") &&
            cls != QStringLiteral("WorkerW") &&
            cls != QStringLiteral("Shell_TrayWnd")) {
            const QString t = getWindowTitleStr(fg);
            if (!t.isEmpty()) {
                nextHwnd = reinterpret_cast<quintptr>(fg);
                nextTitle = t;
                nextApp = resolveAppNameForHwnd(fg);
                if (nextApp.isEmpty()) {
                    nextApp = nextTitle;
                }
                nextIcon = WinIconProvider::hwndIconUrl(nextHwnd);
            }
        }
    }

    if (nextHwnd != m_activeHwnd || nextApp != m_activeApp || nextTitle != m_activeTitle || nextIcon != m_activeIcon) {
        m_activeHwnd = nextHwnd;
        m_activeApp = nextApp;
        m_activeTitle = nextTitle;
        m_activeIcon = nextIcon;
        Q_EMIT activeWindowChanged();
    }

    struct EnumCtx {
        const WindowTracker* self;
        quintptr fgHwnd;
        int activeWorkspace;
        QString primaryScreenName;
        QHash<quintptr, int>* windowWorkspace;
        QSet<quintptr> seen;
        QVariantList list;
        bool primaryOccluded = false;
    } ctx{
        this,
        m_activeHwnd,
        m_activeWorkspace,
        QGuiApplication::primaryScreen() ? QGuiApplication::primaryScreen()->name() : QString(),
        &m_windowWorkspace,
        {},
        {},
        false
    };

    EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
        auto* c = reinterpret_cast<EnumCtx*>(lp);
        if (!c->self->isTrackableAppWindow(hwnd)) {
            return TRUE;
        }
        const QString title = getWindowTitleStr(hwnd);
        QString app = c->self->resolveAppNameForHwnd(hwnd);
        if (app.isEmpty()) {
            app = title;
        }
        const quintptr val = reinterpret_cast<quintptr>(hwnd);
        c->seen.insert(val);
        int ws = c->windowWorkspace->value(val, 0);
        if (ws <= 0) {
            ws = c->activeWorkspace;
            c->windowWorkspace->insert(val, ws);
        }
        const DWORD effPid = resolveEffectiveAppPid(hwnd);
        const QString exePath = queryExePathForPid(effPid);
        const bool isMin = IsIconic(hwnd) != FALSE;
        const bool isMax = isWindowMaximizedOrFullscreen(hwnd);
        const QString winScreen = screenNameForHwnd(hwnd);
        if (!isMin && isMax && windowCloakReason(hwnd) == 0 &&
            (c->primaryScreenName.isEmpty() || winScreen == c->primaryScreenName)) {
            c->primaryOccluded = true;
        }
        QVariantMap row;
        row.insert(QStringLiteral("hwnd"), static_cast<qulonglong>(val));
        row.insert(QStringLiteral("app"), app);
        row.insert(QStringLiteral("title"), title);
        row.insert(QStringLiteral("exe"), exePath);
        row.insert(QStringLiteral("screen"), winScreen);
        row.insert(QStringLiteral("icon"), WinIconProvider::hwndIconUrl(val));
        row.insert(QStringLiteral("minimized"), isMin);
        row.insert(QStringLiteral("maximized"), isMax);
        row.insert(QStringLiteral("active"), val == c->fgHwnd);
        row.insert(QStringLiteral("workspace"), ws);
        c->list.push_back(row);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));

    if (ctx.primaryOccluded != m_primaryOccluded) {
        m_primaryOccluded = ctx.primaryOccluded;
        qInfo("[WindowTracker] primaryOccluded=%d", m_primaryOccluded ? 1 : 0);
        Q_EMIT primaryOccludedChanged();
    }

    for (auto it = m_windowWorkspace.begin(); it != m_windowWorkspace.end(); ) {
        HWND hwnd = reinterpret_cast<HWND>(it.key());
        if (!ctx.seen.contains(it.key()) && (!hwnd || !IsWindow(hwnd))) {
            m_minimizedByWs.remove(it.key());
            it = m_windowWorkspace.erase(it);
        } else {
            ++it;
        }
    }

    if (ctx.list != m_openWindows) {
        m_openWindows = ctx.list;
        if (m_lastLoggedCount != m_openWindows.size()) {
            m_lastLoggedCount = static_cast<int>(m_openWindows.size());
            QStringList names;
            for (const QVariant& v : m_openWindows) {
                names.append(v.toMap().value(QStringLiteral("app")).toString());
            }
            names.removeDuplicates();
            qInfo("[WindowTracker] Tracked %d window(s): %s", m_lastLoggedCount, qPrintable(names.join(QStringLiteral(", "))));
        }
        Q_EMIT openWindowsChanged();
    }

    pollFullscreenMonitors();
}

void WindowTracker::pollFullscreenMonitors() {
    struct FsCtx {
        DWORD ownPid;
        HWND fgHwnd;
        QSet<quintptr> decidedMonitors;
        QSet<quintptr> fsMonitors;
    } fsCtx{ m_ownPid, GetForegroundWindow(), {}, {} };

    EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
        auto* c = reinterpret_cast<FsCtx*>(lp);
        HMONITOR hMon = nullptr;
        bool isSubstantial = false;
        const bool isFs = isWindowTrueFullscreenOnMonitor(hwnd, c->ownPid, hMon, isSubstantial);
        if (!hMon) {
            return TRUE;
        }
        const quintptr monKey = reinterpret_cast<quintptr>(hMon);
        if (c->decidedMonitors.contains(monKey)) {
            return TRUE;
        }
        if (isFs) {
            c->decidedMonitors.insert(monKey);
            c->fsMonitors.insert(monKey);
        } else if (isSubstantial || hwnd == c->fgHwnd) {
            // A normal/foreground non-fullscreen app is sitting above any fullscreen app on this monitor
            c->decidedMonitors.insert(monKey);
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&fsCtx));

    if (fsCtx.fsMonitors != m_fullscreenMonitors) {
        m_fullscreenMonitors = fsCtx.fsMonitors;
        qInfo("[WindowTracker] fullscreenMonitors count=%lld", static_cast<long long>(m_fullscreenMonitors.size()));
        Q_EMIT fullscreenMonitorsChanged();
    }
}

QVariantList WindowTracker::occupiedWorkspaces() const {
    QSet<int> used;
    for (const QVariant& v : m_openWindows) {
        const int ws = v.toMap().value(QStringLiteral("workspace"), 1).toInt();
        if (ws >= 1) {
            used.insert(ws);
        }
    }
    QList<int> sorted = used.values();
    std::sort(sorted.begin(), sorted.end());
    QVariantList out;
    for (int ws : sorted) {
        out.push_back(ws);
    }
    return out;
}

void WindowTracker::focusWindow(qulonglong hwndVal) {
    HWND hwnd = reinterpret_cast<HWND>(static_cast<quintptr>(hwndVal));
    if (!hwnd || !IsWindow(hwnd)) {
        return;
    }
    const int targetWs = m_windowWorkspace.value(static_cast<quintptr>(hwndVal), m_activeWorkspace);
    if (targetWs != m_activeWorkspace) {
        switchWorkspace(targetWs);
    }
    m_minimizedByWs.remove(static_cast<quintptr>(hwndVal));
    focusWindowForce(hwnd);
    scheduleRefresh();
}

void WindowTracker::minimizeWindow(qulonglong hwndVal) {
    HWND hwnd = reinterpret_cast<HWND>(static_cast<quintptr>(hwndVal));
    if (hwnd && IsWindow(hwnd)) {
        m_minimizedByWs.remove(static_cast<quintptr>(hwndVal));
        ShowWindow(hwnd, SW_MINIMIZE);
        scheduleRefresh();
    }
}

void WindowTracker::closeWindow(qulonglong hwndVal) {
    HWND hwnd = reinterpret_cast<HWND>(static_cast<quintptr>(hwndVal));
    if (hwnd && IsWindow(hwnd)) {
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
    }
}

void WindowTracker::switchWorkspace(int workspace) {
    const int target = qBound(1, workspace, 5);
    if (target == m_activeWorkspace) {
        return;
    }
    const int leaving = m_activeWorkspace;
    if (m_activeHwnd != 0) {
        m_lastFocusedByWs.insert(leaving, m_activeHwnd);
    }
    m_activeWorkspace = target;

    QList<HWND> toRestore;
    HWND restoreFocus = nullptr;
    const quintptr preferredFocus = m_lastFocusedByWs.value(target, 0);

    for (auto it = m_windowWorkspace.begin(); it != m_windowWorkspace.end(); ) {
        HWND hwnd = reinterpret_cast<HWND>(it.key());
        if (!hwnd || !IsWindow(hwnd)) {
            m_minimizedByWs.remove(it.key());
            it = m_windowWorkspace.erase(it);
            continue;
        }
        const int ws = it.value();
        if (ws == leaving) {
            if (!IsIconic(hwnd)) {
                m_minimizedByWs.insert(it.key());
                ShowWindowAsync(hwnd, SW_MINIMIZE);
            }
        } else if (ws == target) {
            if (m_minimizedByWs.remove(it.key()) && IsIconic(hwnd)) {
                toRestore.push_back(hwnd);
                if (it.key() == preferredFocus) {
                    restoreFocus = hwnd;
                }
            }
        }
        ++it;
    }

    for (HWND hwnd : toRestore) {
        ShowWindowAsync(hwnd, SW_RESTORE);
    }
    if (restoreFocus && IsWindow(restoreFocus)) {
        ShowWindow(restoreFocus, SW_RESTORE);
        SetForegroundWindow(restoreFocus);
        BringWindowToTop(restoreFocus);
    } else if (!toRestore.isEmpty()) {
        HWND last = toRestore.last();
        ShowWindow(last, SW_RESTORE);
        SetForegroundWindow(last);
        BringWindowToTop(last);
    }

    Q_EMIT activeWorkspaceChanged();
    scheduleRefresh();
}

void WindowTracker::moveWindowToWorkspace(qulonglong hwndVal, int workspace) {
    const quintptr val = static_cast<quintptr>(hwndVal);
    HWND hwnd = reinterpret_cast<HWND>(val);
    if (!hwnd || !IsWindow(hwnd)) {
        return;
    }
    const int target = qBound(1, workspace, 5);
    m_windowWorkspace.insert(val, target);
    if (target != m_activeWorkspace && !IsIconic(hwnd)) {
        m_minimizedByWs.insert(val);
        ShowWindowAsync(hwnd, SW_MINIMIZE);
    } else if (target == m_activeWorkspace && m_minimizedByWs.remove(val) && IsIconic(hwnd)) {
        ShowWindowAsync(hwnd, SW_RESTORE);
    }
    scheduleRefresh();
    Q_EMIT openWindowsChanged();
}

void WindowTracker::restoreAllWorkspaces() {
    for (quintptr val : m_minimizedByWs) {
        HWND hwnd = reinterpret_cast<HWND>(val);
        if (hwnd && IsWindow(hwnd) && IsIconic(hwnd)) {
            ShowWindow(hwnd, SW_RESTORE);
        }
    }
    m_minimizedByWs.clear();
}

} // namespace SekaiPop::Win32
