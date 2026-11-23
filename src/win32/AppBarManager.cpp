#include "AppBarManager.h"
#include "Win32Utils.h"

#include <QCoreApplication>
#include <QScreen>
#include <QTimer>
#include <shellapi.h>

namespace SekaiPop::Win32 {

namespace {

UINT toAppBarEdge(AppBarManager::Edge edge) {
    switch (edge) {
    case AppBarManager::Edge::Top: return ABE_TOP;
    case AppBarManager::Edge::Right: return ABE_RIGHT;
    case AppBarManager::Edge::Bottom: return ABE_BOTTOM;
    case AppBarManager::Edge::Left: return ABE_LEFT;
    }
    return ABE_TOP;
}

void setWorkAreaAsync(RECT rc, bool broadcast) {
    SystemParametersInfoW(SPI_SETWORKAREA, 0, &rc, 0);
    if (broadcast) {
        SendNotifyMessageW(HWND_BROADCAST, WM_SETTINGCHANGE, SPI_SETWORKAREA, 0);
    }
}

} // namespace

AppBarManager::AppBarManager(QObject* parent)
    : QObject(parent) {
    if (auto* app = QCoreApplication::instance()) {
        app->installNativeEventFilter(this);
    }
}

AppBarManager::~AppBarManager() {
    restoreAllWorkAreas();
    if (auto* app = QCoreApplication::instance()) {
        app->removeNativeEventFilter(this);
    }
}

AppBarManager::Edge AppBarManager::parseEdge(const QString& edgeName) {
    const QString e = edgeName.trimmed().toLower();
    if (e == QStringLiteral("right")) return Edge::Right;
    if (e == QStringLiteral("bottom")) return Edge::Bottom;
    if (e == QStringLiteral("left")) return Edge::Left;
    return Edge::Top;
}

void AppBarManager::applyBarStyle(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) {
        return;
    }
    LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    exStyle |= WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
    exStyle &= ~WS_EX_APPWINDOW;
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle);
}

RECT AppBarManager::computeReservedRect(const RECT& monitorRect, Edge edge, int thicknessPx) const {
    RECT r = monitorRect;
    switch (edge) {
    case Edge::Top:
        r.bottom = r.top + thicknessPx;
        break;
    case Edge::Bottom:
        r.top = r.bottom - thicknessPx;
        break;
    case Edge::Left:
        r.right = r.left + thicknessPx;
        break;
    case Edge::Right:
        r.left = r.right - thicknessPx;
        break;
    }
    return r;
}

    void AppBarManager::configureBarWindow(QWindow* window, const QString& edgeName, int thicknessPx, bool autoHide) {
    if (!window) {
        return;
    }

    const WId wid = window->winId();
    if (!wid) {
        return;
    }
    HWND hwnd = reinterpret_cast<HWND>(wid);
    applyBarStyle(hwnd);

    auto it = m_bars.find(hwnd);
    if (it == m_bars.end()) {
        BarEntry entry;
        entry.hwnd = hwnd;
        entry.window = window;
        entry.callbackMsg = m_nextCallbackMsg++;
        it = m_bars.insert(hwnd, entry);

        connect(window, &QObject::destroyed, this, [this, hwnd]() {
            auto f = m_bars.find(hwnd);
            if (f != m_bars.end()) {
                removeAppBar(*f);
                m_bars.erase(f);
            }
        });

        QTimer::singleShot(700, this, [this, hwnd]() {
            auto f = m_bars.constFind(hwnd);
            if (f != m_bars.constEnd() && IsWindow(hwnd) && !f->hiddenForFullscreen &&
                !isMonitorFullscreen(f->monitor)) {
                ShowWindow(hwnd, SW_SHOWNOACTIVATE);
                SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
            }
        });
    }

    it->edge = parseEdge(edgeName);
    it->thicknessPx = qMax(16, thicknessPx);
    it->autoHide = autoHide;
    applyReservation(*it);
}

void AppBarManager::unregisterBarWindow(QWindow* window) {
    if (!window) {
        return;
    }
    HWND hwnd = reinterpret_cast<HWND>(window->winId());
    auto it = m_bars.find(hwnd);
    if (it != m_bars.end()) {
        removeAppBar(*it);
        m_bars.erase(it);
    }
}

void AppBarManager::removeAppBar(BarEntry& entry) {
    if (entry.registeredAppBar && entry.hwnd) {
        APPBARDATA abd = {};
        abd.cbSize = sizeof(APPBARDATA);
        abd.hWnd = entry.hwnd;
        SHAppBarMessage(ABM_REMOVE, &abd);
        entry.registeredAppBar = false;
    }
}

void AppBarManager::applyReservation(BarEntry& entry) {
    if (!entry.hwnd || !IsWindow(entry.hwnd) || m_inApplyReservation) {
        return;
    }

    m_inApplyReservation = true;
    struct Guard {
        bool& flag;
        ~Guard() { flag = false; }
    } guard{ m_inApplyReservation };

    // Determine target monitor from Qt screen if assigned, else HWND.
    // Under Per-Monitor-DPI-V2 PassThrough, QScreen::geometry() origin (x,y) is in
    // virtual desktop coordinates while width/height are logical DIPs.
    HMONITOR hmon = MonitorFromWindow(entry.hwnd, MONITOR_DEFAULTTONEAREST);
    if (entry.window && entry.window->screen()) {
        const QRect g = entry.window->screen()->geometry();
        const qreal dpr = entry.window->screen()->devicePixelRatio();
        const POINT pt{
            qRound(g.x() + (g.width() * 0.5) * dpr),
            qRound(g.y() + (g.height() * 0.5) * dpr)
        };
        HMONITOR byScreen = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
        if (byScreen) {
            hmon = byScreen;
        }
    }
    entry.monitor = hmon;

    MONITORINFO mi = {};
    mi.cbSize = sizeof(MONITORINFO);
    if (!GetMonitorInfoW(hmon, &mi)) {
        return;
    }

    if (!m_savedWorkAreas.contains(hmon)) {
        m_savedWorkAreas.insert(hmon, mi.rcWork);
    }

    const int physicalThickness = entry.window && entry.window->screen()
        ? qRound(entry.thicknessPx * entry.window->screen()->devicePixelRatio())
        : entry.thicknessPx;

    if (entry.autoHide) {
        removeAppBar(entry);
        const bool taskbarShown = !m_taskbarHiddenMode && isExplorerShellPresent() && isWindowsTaskbarVisible();
        if (taskbarShown && m_savedWorkAreas.contains(hmon)) {
            RECT orig = m_savedWorkAreas.value(hmon);
            if (!EqualRect(&mi.rcWork, &orig)) {
                setWorkAreaAsync(orig, true);
            }
        } else if (!taskbarShown && !EqualRect(&mi.rcWork, &mi.rcMonitor)) {
            setWorkAreaAsync(mi.rcMonitor, false);
        }
        const RECT barRect = computeReservedRect(mi.rcMonitor, entry.edge, physicalThickness);
        if (!entry.positioned || !EqualRect(&entry.lastBounds, &barRect)) {
            entry.lastBounds = barRect;
            entry.positioned = true;
            positionBarWindow(entry, barRect);
        }
        return;
    }

    const bool explorerPresent = !m_taskbarHiddenMode && isExplorerShellPresent() && isWindowsTaskbarVisible();
    if (explorerPresent) {
        if (!entry.registeredAppBar) {
            APPBARDATA abd = {};
            abd.cbSize = sizeof(APPBARDATA);
            abd.hWnd = entry.hwnd;
            abd.uCallbackMessage = entry.callbackMsg;
            if (SHAppBarMessage(ABM_NEW, &abd)) {
                entry.registeredAppBar = true;
                entry.positioned = false;
            }
        }

        if (entry.registeredAppBar) {
            APPBARDATA abd = {};
            abd.cbSize = sizeof(APPBARDATA);
            abd.hWnd = entry.hwnd;
            abd.uEdge = toAppBarEdge(entry.edge);
            abd.rc = mi.rcMonitor;

            SHAppBarMessage(ABM_QUERYPOS, &abd);
            if ((abd.rc.right - abd.rc.left) < 64 || (abd.rc.bottom - abd.rc.top) < physicalThickness) {
                abd.rc = mi.rcMonitor;
            }
            abd.rc = computeReservedRect(abd.rc, entry.edge, physicalThickness);

            if (!entry.positioned || !EqualRect(&entry.lastBounds, &abd.rc)) {
                SHAppBarMessage(ABM_SETPOS, &abd);
                entry.lastBounds = abd.rc;
                entry.positioned = true;
                positionBarWindow(entry, abd.rc);
            }
            qInfo("[AppBarManager] explorer bar hwnd=%p screen=%s rect=(%ld,%ld,%ld,%ld) vis=%d",
                  entry.hwnd,
                  entry.window && entry.window->screen() ? qPrintable(entry.window->screen()->name()) : "null",
                  abd.rc.left, abd.rc.top, abd.rc.right, abd.rc.bottom,
                  IsWindowVisible(entry.hwnd) ? 1 : 0);
            return;
        }
    } else {
        removeAppBar(entry);
    }

    // Reserve work area directly on the monitor when Explorer taskbar is absent or hidden.
    // Avoid broadcasting WM_SETTINGCHANGE here so Explorer does not un-hide Shell_TrayWnd / Shell_SecondaryTrayWnd.
    RECT newWork = mi.rcMonitor;
    switch (entry.edge) {
    case Edge::Top:
        newWork.top += physicalThickness;
        break;
    case Edge::Bottom:
        newWork.bottom -= physicalThickness;
        break;
    case Edge::Left:
        newWork.left += physicalThickness;
        break;
    case Edge::Right:
        newWork.right -= physicalThickness;
        break;
    }
    if (!EqualRect(&mi.rcWork, &newWork)) {
        setWorkAreaAsync(newWork, false);
    }

    const RECT barRect = computeReservedRect(mi.rcMonitor, entry.edge, physicalThickness);
    if (!entry.positioned || !EqualRect(&entry.lastBounds, &barRect)) {
        entry.lastBounds = barRect;
        entry.positioned = true;
        positionBarWindow(entry, barRect);
    }
    qInfo("[AppBarManager] fallback bar hwnd=%p screen=%s rect=(%ld,%ld,%ld,%ld) vis=%d",
          entry.hwnd,
          entry.window && entry.window->screen() ? qPrintable(entry.window->screen()->name()) : "null",
          barRect.left, barRect.top, barRect.right, barRect.bottom,
          IsWindowVisible(entry.hwnd) ? 1 : 0);
}

void AppBarManager::positionBarWindow(const BarEntry& entry, const RECT& rectPx) {
    if (!entry.hwnd || !IsWindow(entry.hwnd)) {
        return;
    }
    const int w = qMax(1, static_cast<int>(rectPx.right - rectPx.left));
    const int h = qMax(1, static_cast<int>(rectPx.bottom - rectPx.top));
    if (entry.hiddenForFullscreen || isMonitorFullscreen(entry.monitor)) {
        SetWindowPos(
            entry.hwnd,
            HWND_NOTOPMOST,
            rectPx.left,
            rectPx.top,
            w,
            h,
            SWP_NOACTIVATE | SWP_HIDEWINDOW
        );
        return;
    }
    ShowWindow(entry.hwnd, SW_SHOWNOACTIVATE);
    SetWindowPos(
        entry.hwnd,
        HWND_TOPMOST,
        rectPx.left,
        rectPx.top,
        w,
        h,
        SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_FRAMECHANGED
    );
}

bool AppBarManager::isMonitorFullscreen(HMONITOR hmon) const {
    return hmon && m_fullscreenMonitors.contains(reinterpret_cast<quintptr>(hmon));
}

void AppBarManager::setFullscreenMonitors(const QSet<quintptr>& monitors) {
    if (m_fullscreenMonitors == monitors) {
        return;
    }
    m_fullscreenMonitors = monitors;
    for (auto it = m_bars.begin(); it != m_bars.end(); ++it) {
        BarEntry& entry = *it;
        if (!entry.hwnd || !IsWindow(entry.hwnd)) {
            continue;
        }
        HMONITOR hmon = entry.monitor ? entry.monitor : MonitorFromWindow(entry.hwnd, MONITOR_DEFAULTTONEAREST);
        const bool fs = isMonitorFullscreen(hmon);
        const bool isWinVis = IsWindowVisible(entry.hwnd) != FALSE;
        if (entry.hiddenForFullscreen == fs && (fs ? !isWinVis : isWinVis)) {
            continue;
        }
        entry.hiddenForFullscreen = fs;
        if (fs) {
            ShowWindow(entry.hwnd, SW_HIDE);
        } else {
            ShowWindow(entry.hwnd, SW_SHOWNOACTIVATE);
            SetWindowPos(entry.hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        }
        qInfo("[AppBarManager] fullscreen=%d monitor=%p barHwnd=%p", fs ? 1 : 0, hmon, entry.hwnd);
    }
}

void AppBarManager::setTaskbarHiddenMode(bool hidden) {
    m_taskbarHiddenMode = hidden;
}

void AppBarManager::reapplyAllReservations() {
    for (auto it = m_bars.begin(); it != m_bars.end(); ++it) {
        it->positioned = false;
        applyReservation(*it);
    }
}

void AppBarManager::ensureWorkAreasValid() {
    if (!m_taskbarHiddenMode || m_inApplyReservation) {
        return;
    }
    for (auto it = m_bars.begin(); it != m_bars.end(); ++it) {
        BarEntry& entry = *it;
        if (!entry.hwnd || !IsWindow(entry.hwnd)) {
            continue;
        }
        HMONITOR hmon = entry.monitor ? entry.monitor : MonitorFromWindow(entry.hwnd, MONITOR_DEFAULTTONEAREST);
        if (!hmon) {
            continue;
        }
        MONITORINFO mi = {};
        mi.cbSize = sizeof(MONITORINFO);
        if (!GetMonitorInfoW(hmon, &mi)) {
            continue;
        }
        RECT expectedWork = mi.rcMonitor;
        if (!entry.autoHide) {
            const int physicalThickness = entry.window && entry.window->screen()
                ? qRound(entry.thicknessPx * entry.window->screen()->devicePixelRatio())
                : entry.thicknessPx;
            switch (entry.edge) {
            case Edge::Top: expectedWork.top += physicalThickness; break;
            case Edge::Bottom: expectedWork.bottom -= physicalThickness; break;
            case Edge::Left: expectedWork.left += physicalThickness; break;
            case Edge::Right: expectedWork.right -= physicalThickness; break;
            }
        }
        if (!EqualRect(&mi.rcWork, &expectedWork)) {
            setWorkAreaAsync(expectedWork, false);
            qInfo("[AppBarManager] repaired drifted workArea hwnd=%p work=(%ld,%ld,%ld,%ld)",
                  entry.hwnd, expectedWork.left, expectedWork.top, expectedWork.right, expectedWork.bottom);
        }
    }
}

void AppBarManager::restoreAllWorkAreas() {
    for (auto it = m_bars.begin(); it != m_bars.end(); ++it) {
        removeAppBar(*it);
    }
    for (auto it = m_savedWorkAreas.constBegin(); it != m_savedWorkAreas.constEnd(); ++it) {
        setWorkAreaAsync(it.value(), true);
    }
    m_savedWorkAreas.clear();
}

bool AppBarManager::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) {
    Q_UNUSED(result);
    if (eventType != "windows_generic_MSG" && eventType != "windows_dispatcher_MSG") {
        return false;
    }
    MSG* msg = static_cast<MSG*>(message);
    if (!msg) {
        return false;
    }

    auto it = m_bars.find(msg->hwnd);
    if (it == m_bars.end()) {
        return false;
    }

    if (msg->message == it->callbackMsg && it->registeredAppBar) {
        if (msg->wParam == ABN_POSCHANGED && !m_inApplyReservation) {
            applyReservation(*it);
        } else if (msg->wParam == ABN_FULLSCREENAPP) {
            bool externalFs = (msg->lParam != 0);
            HWND fg = GetForegroundWindow();
            if (externalFs) {
                DWORD fgPid = 0;
                if (!fg || !IsWindow(fg)) {
                    externalFs = false;
                } else {
                    GetWindowThreadProcessId(fg, &fgPid);
                    if (fgPid == GetCurrentProcessId()) {
                        externalFs = false;
                    } else {
                        wchar_t cls[128] = {};
                        GetClassNameW(fg, cls, 128);
                        if (wcscmp(cls, L"Progman") == 0 || wcscmp(cls, L"WorkerW") == 0 || wcscmp(cls, L"Shell_TrayWnd") == 0) {
                            externalFs = false;
                        } else if (MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST) != it->monitor) {
                            externalFs = false;
                        }
                    }
                }
            }
            qInfo("[AppBarManager] ABN_FULLSCREENAPP raw=%d externalFs=%d fg=%p hwnd=%p",
                  msg->lParam != 0 ? 1 : 0, externalFs ? 1 : 0, fg, it->hwnd);
            // Never push below WallpaperWindow (HWND_BOTTOM); HWND_NOTOPMOST yields only to real fullscreen apps.
            SetWindowPos(
                it->hwnd,
                externalFs ? HWND_NOTOPMOST : HWND_TOPMOST,
                0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE
            );
        }
        return true;
    }

    if (msg->message == WM_ACTIVATE && it->registeredAppBar) {
        APPBARDATA abd = {};
        abd.cbSize = sizeof(APPBARDATA);
        abd.hWnd = it->hwnd;
        SHAppBarMessage(ABM_ACTIVATE, &abd);
    } else if (msg->message == WM_WINDOWPOSCHANGED && it->registeredAppBar && !m_inApplyReservation) {
        APPBARDATA abd = {};
        abd.cbSize = sizeof(APPBARDATA);
        abd.hWnd = it->hwnd;
        SHAppBarMessage(ABM_WINDOWPOSCHANGED, &abd);
    }
    return false;
}

} // namespace SekaiPop::Win32
