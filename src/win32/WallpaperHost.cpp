#include "WallpaperHost.h"

#include <QCoreApplication>
#include <QScreen>
#include <dwmapi.h>
#include <shobjidl.h>

#ifndef DWMWA_EXCLUDED_FROM_PEEK
#define DWMWA_EXCLUDED_FROM_PEEK 12
#endif

namespace SekaiPop::Win32 {

namespace {

BOOL CALLBACK enumWorkerWProc(HWND hwnd, LPARAM lParam) {
    HWND defView = FindWindowExW(hwnd, nullptr, L"SHELLDLL_DefView", nullptr);
    if (defView) {
        HWND workerW = FindWindowExW(nullptr, hwnd, L"WorkerW", nullptr);
        if (workerW) {
            *reinterpret_cast<HWND*>(lParam) = workerW;
            return FALSE;
        }
    }
    return TRUE;
}

} // namespace

WallpaperHost::WallpaperHost(QObject* parent)
    : QObject(parent) {
    if (auto* app = QCoreApplication::instance()) {
        app->installNativeEventFilter(this);
    }
}

WallpaperHost::~WallpaperHost() {
    if (auto* app = QCoreApplication::instance()) {
        app->removeNativeEventFilter(this);
    }
}

HWND WallpaperHost::findExplorerWorkerW() {
    HWND progman = FindWindowW(L"Progman", nullptr);
    if (!progman) {
        return nullptr;
    }
    DWORD_PTR result = 0;
    SendMessageTimeoutW(progman, 0x052C, 0xD, 0x1, SMTO_NORMAL, 500, &result);
    HWND workerW = nullptr;
    EnumWindows(enumWorkerWProc, reinterpret_cast<LPARAM>(&workerW));
    return workerW;
}

void WallpaperHost::registerWallpaperWindow(QWindow* window) {
    if (!window) {
        return;
    }
    const WId wid = window->winId();
    if (!wid) {
        return;
    }
    HWND hwnd = reinterpret_cast<HWND>(wid);
    if (m_wallpaperHwnds.contains(hwnd)) {
        return;
    }

    const Qt::WindowFlags desired = Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus | Qt::Tool;
    if ((window->flags() & desired) != desired) {
        window->setFlags(desired);
    }

    m_wallpaperHwnds.insert(hwnd);
    applyBottomStyle(hwnd);

    connect(window, &QObject::destroyed, this, [this, hwnd]() {
        m_wallpaperHwnds.remove(hwnd);
    });
}

void WallpaperHost::unregisterWallpaperWindow(QWindow* window) {
    if (!window) {
        return;
    }
    HWND hwnd = reinterpret_cast<HWND>(window->winId());
    if (hwnd) {
        m_wallpaperHwnds.remove(hwnd);
        m_visualizerHwnds.remove(hwnd);
        m_widgetHwnds.remove(hwnd);
        m_bottomDockHwnds.remove(hwnd);
        m_clickThroughWidgets.remove(hwnd);
        m_focusableWidgets.remove(hwnd);
    }
}

void WallpaperHost::registerVisualizerWindow(QWindow* window, bool focusable) {
    if (!window) {
        return;
    }
    const WId wid = window->winId();
    if (!wid) {
        return;
    }
    HWND hwnd = reinterpret_cast<HWND>(wid);
    const bool alreadyRegistered = m_visualizerHwnds.contains(hwnd);
    m_widgetHwnds.remove(hwnd);
    m_bottomDockHwnds.remove(hwnd);
    m_visualizerHwnds.insert(hwnd);
    m_clickThroughWidgets.remove(hwnd);
    if (focusable) {
        m_focusableWidgets.insert(hwnd);
    } else {
        m_focusableWidgets.remove(hwnd);
    }
    applyWidgetStyle(hwnd, false, focusable);
    repinAll();

    if (!alreadyRegistered) {
        connect(window, &QObject::destroyed, this, [this, hwnd]() {
            m_visualizerHwnds.remove(hwnd);
            m_clickThroughWidgets.remove(hwnd);
            m_focusableWidgets.remove(hwnd);
        });
    }
}

void WallpaperHost::registerBottomDockWindow(QWindow* window, bool focusable) {
    if (!window) {
        return;
    }
    const WId wid = window->winId();
    if (!wid) {
        return;
    }
    HWND hwnd = reinterpret_cast<HWND>(wid);
    const bool alreadyRegistered = m_bottomDockHwnds.contains(hwnd);
    m_widgetHwnds.remove(hwnd);
    m_bottomDockHwnds.insert(hwnd);
    if (focusable) {
        m_focusableWidgets.insert(hwnd);
    } else {
        m_focusableWidgets.remove(hwnd);
    }
    // Keep normal/maximized windows above an unpinned Dock, while keeping the Dock
    // strictly above Desktop Widgets and Wallpaper.
    SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    repinAll();

    if (!alreadyRegistered) {
        connect(window, &QObject::destroyed, this, [this, hwnd]() {
            m_bottomDockHwnds.remove(hwnd);
            m_focusableWidgets.remove(hwnd);
        });
    }
}

void WallpaperHost::registerWidgetWindow(QWindow* window, bool clickThrough, bool focusable) {
    if (!window) {
        return;
    }
    const WId wid = window->winId();
    if (!wid) {
        return;
    }
    HWND hwnd = reinterpret_cast<HWND>(wid);
    const bool alreadyRegistered = m_widgetHwnds.contains(hwnd);
    m_widgetHwnds.insert(hwnd);
    if (clickThrough) {
        m_clickThroughWidgets.insert(hwnd);
    } else {
        m_clickThroughWidgets.remove(hwnd);
    }
    if (focusable) {
        m_focusableWidgets.insert(hwnd);
    } else {
        m_focusableWidgets.remove(hwnd);
    }
    applyWidgetStyle(hwnd, clickThrough, focusable);
    repinWallpapersBottom();

    if (!alreadyRegistered) {
        connect(window, &QObject::destroyed, this, [this, hwnd]() {
            m_widgetHwnds.remove(hwnd);
            m_clickThroughWidgets.remove(hwnd);
            m_focusableWidgets.remove(hwnd);
        });
    }
}

void WallpaperHost::applyWidgetStyle(HWND hwnd, bool clickThrough, bool focusable) {
    if (!hwnd || !IsWindow(hwnd)) {
        return;
    }
    const LONG_PTR curExStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    LONG_PTR exStyle = curExStyle;
    exStyle |= WS_EX_TOOLWINDOW;
    exStyle &= ~WS_EX_APPWINDOW;
    if (clickThrough) {
        exStyle |= WS_EX_LAYERED | WS_EX_TRANSPARENT;
    }
    if (!focusable) {
        exStyle |= WS_EX_NOACTIVATE;
    } else {
        exStyle &= ~WS_EX_NOACTIVATE;
    }
    const bool styleChanged = (exStyle != curExStyle);
    if (styleChanged) {
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle);
    }

    BOOL excludePeek = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_EXCLUDED_FROM_PEEK, &excludePeek, sizeof(excludePeek));

    ITaskbarList2* tbl = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&tbl))) && tbl) {
        if (SUCCEEDED(tbl->HrInit())) {
            tbl->MarkFullscreenWindow(hwnd, FALSE);
        }
        tbl->Release();
    }

    SetWindowPos(
        hwnd,
        HWND_BOTTOM,
        0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW | (styleChanged ? SWP_FRAMECHANGED : 0)
    );
}

void WallpaperHost::setEditModeSuspended(bool suspended) {
    m_editModeSuspended = suspended;
}

void WallpaperHost::repinWallpapersBottom() {
    if (m_repinning || m_editModeSuspended) {
        return;
    }
    m_repinning = true;
    for (HWND hwnd : m_wallpaperHwnds) {
        if (IsWindow(hwnd)) {
            SetWindowPos(hwnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
    }
    m_repinning = false;
}

void WallpaperHost::repinAll() {
    if (m_editModeSuspended) {
        return;
    }
    m_repinning = true;
    // Order top -> bottom beneath regular/maximized app windows:
    //   1. Unpinned Bottom-layer Docks (right below normal apps)
    //   2. Desktop Widgets (Clock, UserCard, Calendar, To-Do, Weather, Notes)
    //   3. Audio Visualizer (Ribbons / Tide / Aurora / etc. behind Dock & Widgets)
    //   4. Wallpapers (bottommost, behind everything)
    for (HWND hwnd : m_bottomDockHwnds) {
        if (IsWindow(hwnd) && IsWindowVisible(hwnd)) {
            SetWindowPos(hwnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
    }
    for (HWND hwnd : m_widgetHwnds) {
        if (IsWindow(hwnd) && IsWindowVisible(hwnd)) {
            SetWindowPos(hwnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
    }
    for (HWND hwnd : m_visualizerHwnds) {
        if (IsWindow(hwnd) && IsWindowVisible(hwnd)) {
            SetWindowPos(hwnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
    }
    for (HWND hwnd : m_wallpaperHwnds) {
        if (IsWindow(hwnd)) {
            SetWindowPos(hwnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
    }
    m_repinning = false;
}

void WallpaperHost::applyBottomStyle(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) {
        return;
    }
    const LONG_PTR curExStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    LONG_PTR exStyle = curExStyle;
    exStyle |= WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
    exStyle &= ~(WS_EX_APPWINDOW | WS_EX_LAYERED | WS_EX_TRANSPARENT);
    const bool styleChanged = (exStyle != curExStyle);
    if (styleChanged) {
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle);
    }

    // Exclude the wallpaper layer from Aero Peek and tell Explorer's shell tray
    // that this fullscreen-sized window is NOT a fullscreen game/app (prevents
    // Explorer from lowering the taskbar or broadcasting ABN_FULLSCREENAPP = 1).
    BOOL excludePeek = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_EXCLUDED_FROM_PEEK, &excludePeek, sizeof(excludePeek));

    ITaskbarList2* tbl = nullptr;
    HRESULT tblHr = CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&tbl));
    if (SUCCEEDED(tblHr) && tbl) {
        if (SUCCEEDED(tbl->HrInit())) {
            tblHr = tbl->MarkFullscreenWindow(hwnd, FALSE);
        }
        tbl->Release();
    }

    SetWindowPos(
        hwnd,
        HWND_BOTTOM,
        0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | (styleChanged ? SWP_FRAMECHANGED : 0)
    );

    RECT r = {};
    GetWindowRect(hwnd, &r);
    qInfo("[WallpaperHost] pinned hwnd=%p rect=(%ld,%ld,%ld,%ld) markFsHr=0x%08lX vis=%d",
          hwnd, r.left, r.top, r.right, r.bottom, static_cast<unsigned long>(tblHr), IsWindowVisible(hwnd) ? 1 : 0);
}

bool WallpaperHost::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) {
    if (eventType != "windows_generic_MSG" && eventType != "windows_dispatcher_MSG") {
        return false;
    }
    MSG* msg = static_cast<MSG*>(message);
    if (!msg) {
        return false;
    }

    const bool isWallpaper = m_wallpaperHwnds.contains(msg->hwnd);
    const bool isVisualizer = m_visualizerHwnds.contains(msg->hwnd);
    const bool isWidget = m_widgetHwnds.contains(msg->hwnd);
    const bool isBottomDock = m_bottomDockHwnds.contains(msg->hwnd);
    if (!isWallpaper && !isVisualizer && !isWidget && !isBottomDock) {
        return false;
    }

    if (m_editModeSuspended) {
        // While Edit Desktop is active, QsWindowManager owns exact topmost z-ordering
        // across WallpaperLayer -> Visualizer -> Widgets -> Bar/Dock -> EditTray.
        // Still block wallpaper clicks from activating/raising WallpaperLayer over widgets.
        if (msg->message == WM_MOUSEACTIVATE && isWallpaper) {
            if (result) {
                *result = MA_NOACTIVATE;
            }
            return true;
        }
        if (msg->message == WM_SYSCOMMAND && (msg->wParam & 0xFFF0) == SC_MINIMIZE) {
            if (result) {
                *result = 0;
            }
            return true;
        }
        return false;
    }

    switch (msg->message) {
    case WM_WINDOWPOSCHANGING: {
        if (isWallpaper && !m_repinning) {
            auto* wp = reinterpret_cast<WINDOWPOS*>(msg->lParam);
            if (wp) {
                wp->hwndInsertAfter = HWND_BOTTOM;
                wp->flags |= SWP_NOACTIVATE;
            }
        }
        return false;
    }
    case WM_MOUSEACTIVATE:
        if (isWallpaper || m_clickThroughWidgets.contains(msg->hwnd) ||
            ((isBottomDock || isVisualizer) && !m_focusableWidgets.contains(msg->hwnd))) {
            if (result) {
                *result = MA_NOACTIVATE;
            }
            return true;
        }
        return false;
    case WM_SYSCOMMAND:
        if ((msg->wParam & 0xFFF0) == SC_MINIMIZE) {
            if (result) {
                *result = 0;
            }
            return true;
        }
        return false;
    default:
        return false;
    }
}

} // namespace SekaiPop::Win32
