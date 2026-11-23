#include "ShellController.h"
#include "BarService.h"
#include "SettingsService.h"
#include "../win32/AppBarManager.h"
#include "../win32/HotkeyManager.h"
#include "../win32/WallpaperHost.h"
#include "../win32/Win32Utils.h"
#include "../win32/WindowTracker.h"

#include <QCoreApplication>
#include <QTimer>
#include <windows.h>

namespace SekaiPop::Services {

ShellController::ShellController(
    Win32::WallpaperHost* wallpaperHost,
    Win32::AppBarManager* appBarManager,
    Win32::WindowTracker* windowTracker,
    Win32::HotkeyManager* hotkeyManager,
    BarService* barService,
    SettingsService* settingsService,
    QObject* parent
)
    : QObject(parent)
    , m_wallpaperHost(wallpaperHost)
    , m_appBarManager(appBarManager)
    , m_windowTracker(windowTracker)
    , m_hotkeyManager(hotkeyManager)
    , m_barService(barService)
    , m_settings(settingsService) {
    if (m_hotkeyManager) {
        connect(m_hotkeyManager, &Win32::HotkeyManager::launcherToggleRequested, this, &ShellController::toggleLauncher);
        connect(m_hotkeyManager, &Win32::HotkeyManager::quickPanelToggleRequested, this, &ShellController::toggleQuickPanel);
        connect(m_hotkeyManager, &Win32::HotkeyManager::restoreExplorerRequested, this, &ShellController::restoreExplorer);
        connect(m_hotkeyManager, &Win32::HotkeyManager::exitShellRequested, this, &ShellController::exitShell);
    }

    if (m_settings) {
        connect(m_settings, &SettingsService::keyChanged, this, [this](const QString& key) {
            if (key == QStringLiteral("dock.hideWindowsTaskbar")) {
                applyWindowsTaskbarSetting(true);
            }
        });
    }

    applyWindowsTaskbarSetting(false);

    auto* explorerPoll = new QTimer(this);
    explorerPoll->setInterval(1200);
    connect(explorerPoll, &QTimer::timeout, this, [this]() {
        if (!m_restoring && m_settings && m_settings->flag(QStringLiteral("dock.hideWindowsTaskbar"))) {
            if (Win32::isWindowsTaskbarVisible()) {
                Win32::setWindowsTaskbarsVisible(false);
            }
        }
        Q_EMIT explorerRunningChanged();
    });
    explorerPoll->start();
}

ShellController::~ShellController() {
    m_restoring = true;
    Win32::setWindowsTaskbarsVisible(true);
    if (m_windowTracker) {
        m_windowTracker->restoreAllWorkspaces();
    }
    if (m_appBarManager) {
        m_appBarManager->restoreAllWorkAreas();
    }
}

void ShellController::applyWindowsTaskbarSetting(bool reapplyBars) {
    if (m_restoring) {
        return;
    }
    const bool hide = m_settings ? m_settings->flag(QStringLiteral("dock.hideWindowsTaskbar")) : true;
    if (m_appBarManager) {
        m_appBarManager->setTaskbarHiddenMode(hide);
    }
    if (hide) {
        Win32::setWindowsTaskbarsVisible(false);
        if (reapplyBars && m_appBarManager) {
            m_appBarManager->reapplyAllReservations();
        }
    } else {
        Win32::setWindowsTaskbarsVisible(true);
        if (reapplyBars && m_appBarManager) {
            m_appBarManager->restoreAllWorkAreas();
            QTimer::singleShot(300, this, [this]() {
                if (m_appBarManager && !m_restoring && (!m_settings || !m_settings->flag(QStringLiteral("dock.hideWindowsTaskbar")))) {
                    m_appBarManager->reapplyAllReservations();
                }
            });
        }
    }
    Q_EMIT explorerRunningChanged();
}

bool ShellController::explorerRunning() const {
    return Win32::isExplorerShellPresent();
}

bool ShellController::windowsTaskbarVisible() const {
    return Win32::isWindowsTaskbarVisible();
}

void ShellController::setLauncherOpen(bool open) {
    if (open) {
        openLauncher();
    } else {
        closeLauncher();
    }
}

void ShellController::toggleLauncher() {
    if (m_launcherOpen) {
        closeLauncher();
    } else {
        openLauncher();
    }
}

void ShellController::openLauncher() {
    qInfo("[ShellController] openLauncher() current=%d", m_launcherOpen ? 1 : 0);
    if (!m_activePopup.isEmpty()) {
        m_activePopup.clear();
        Q_EMIT activePopupChanged();
    }
    if (m_phoneOpen) {
        m_phoneOpen = false;
        Q_EMIT phoneChanged();
    }
    if (!m_launcherOpen) {
        m_launcherOpen = true;
        Q_EMIT launcherOpenChanged();
    }
}

void ShellController::closeLauncher() {
    qInfo("[ShellController] closeLauncher() current=%d", m_launcherOpen ? 1 : 0);
    if (!m_launcherOpen) {
        return;
    }
    m_launcherOpen = false;
    Q_EMIT launcherOpenChanged();
}

void ShellController::toggleQuickPanel() {
    togglePhone(QStringLiteral("quick"));
}

void ShellController::openQuickPanel() {
    openPhone(QStringLiteral("quick"));
}

void ShellController::openSettings() {
    openPhone(QStringLiteral("settings"));
}

void ShellController::openWallpaperSelector() {
    openPhone(QStringLiteral("wallpapers"));
}

void ShellController::toggleEditMode() {
    if (m_editModeOpen) {
        closeEditMode();
    } else {
        openEditMode();
    }
}

void ShellController::openEditMode() {
    if (m_launcherOpen) {
        m_launcherOpen = false;
        Q_EMIT launcherOpenChanged();
    }
    if (m_phoneOpen) {
        m_phoneOpen = false;
        Q_EMIT phoneChanged();
    }
    if (!m_activePopup.isEmpty()) {
        m_activePopup.clear();
        Q_EMIT activePopupChanged();
    }
    if (!m_editModeOpen) {
        m_editModeOpen = true;
        Q_EMIT editModeChanged();
    }
}

void ShellController::closeEditMode() {
    if (!m_editModeOpen) {
        return;
    }
    m_editModeOpen = false;
    Q_EMIT editModeChanged();
}

void ShellController::setWindowsTaskbarVisible(bool visible) {
    if (m_settings) {
        m_settings->set(QStringLiteral("dock.hideWindowsTaskbar"), !visible);
    } else {
        Win32::setWindowsTaskbarsVisible(visible);
        Q_EMIT explorerRunningChanged();
    }
}

void ShellController::setQuickPowerOpen(bool open) {
    if (m_quickPowerOpen == open) {
        return;
    }
    m_quickPowerOpen = open;
    Q_EMIT phoneChanged();
}

void ShellController::setQuickEditOpen(bool open) {
    if (m_quickEditOpen == open) {
        return;
    }
    m_quickEditOpen = open;
    Q_EMIT phoneChanged();
}

void ShellController::togglePhone(const QString& screen) {
    const QString target = screen.isEmpty() ? QStringLiteral("quick") : screen;
    if (target == QStringLiteral("apps")) {
        toggleLauncher();
        return;
    }
    if (target == QStringLiteral("edit_desktop")) {
        toggleEditMode();
        return;
    }
    if (m_phoneOpen && m_phoneScreen == target) {
        closePhone();
    } else {
        openPhone(target);
    }
}

void ShellController::openPhone(const QString& screen) {
    QString target = screen.isEmpty() ? QStringLiteral("quick") : screen;
    if (target == QStringLiteral("apps")) {
        openLauncher();
        return;
    }
    if (target == QStringLiteral("edit_desktop")) {
        openEditMode();
        return;
    }
    if (target == QStringLiteral("quick_power")) {
        target = QStringLiteral("quick");
        m_quickPowerOpen = true;
        m_quickEditOpen = false;
    } else if (target == QStringLiteral("quick_edit")) {
        target = QStringLiteral("quick");
        m_quickEditOpen = true;
        m_quickPowerOpen = false;
    }
    if (!m_activePopup.isEmpty()) {
        m_activePopup.clear();
        Q_EMIT activePopupChanged();
    }
    if (m_launcherOpen) {
        m_launcherOpen = false;
        Q_EMIT launcherOpenChanged();
    }
    if (m_editModeOpen) {
        m_editModeOpen = false;
        Q_EMIT editModeChanged();
    }
    m_phoneScreen = target;
    m_phoneOpen = true;
    Q_EMIT phoneChanged();
}

void ShellController::closePhone() {
    if (!m_phoneOpen) {
        return;
    }
    m_phoneOpen = false;
    Q_EMIT phoneChanged();
}

void ShellController::setActivePopup(const QString& name) {
    if (!name.isEmpty()) {
        if (m_launcherOpen) {
            m_launcherOpen = false;
            Q_EMIT launcherOpenChanged();
        }
        if (m_phoneOpen) {
            m_phoneOpen = false;
            Q_EMIT phoneChanged();
        }
    }
    if (m_activePopup == name) {
        return;
    }
    m_activePopup = name;
    Q_EMIT activePopupChanged();
}

void ShellController::registerWallpaperWindow(QWindow* window) {
    if (m_wallpaperHost) {
        m_wallpaperHost->registerWallpaperWindow(window);
    }
}

void ShellController::registerWidgetWindow(QWindow* window, bool clickThrough, bool focusable) {
    if (m_wallpaperHost) {
        m_wallpaperHost->registerWidgetWindow(window, clickThrough, focusable);
    }
}

void ShellController::repinDesktopLayer() {
    if (m_wallpaperHost) {
        m_wallpaperHost->repinAll();
    }
}

void ShellController::registerBarWindow(QWindow* window, const QString& position, int stripSizePx, bool autoHide) {
    if (m_appBarManager) {
        m_appBarManager->configureBarWindow(window, position, stripSizePx, autoHide);
        if (!m_restoring && m_settings && m_settings->flag(QStringLiteral("dock.hideWindowsTaskbar"))) {
            QTimer::singleShot(250, this, [this]() {
                if (!m_restoring && m_settings && m_settings->flag(QStringLiteral("dock.hideWindowsTaskbar")) &&
                    Win32::isWindowsTaskbarVisible()) {
                    Win32::setWindowsTaskbarsVisible(false);
                }
            });
        }
    }
}

void ShellController::configureDockWindow(QWindow* window) {
    if (!window) {
        return;
    }
    HWND hwnd = reinterpret_cast<HWND>(window->winId());
    if (hwnd) {
        LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        exStyle |= WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
        exStyle &= ~WS_EX_APPWINDOW;
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle);
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_FRAMECHANGED);
    }
}

void ShellController::configurePopupWindow(QWindow* window) {
    if (!window) {
        return;
    }
    HWND hwnd = reinterpret_cast<HWND>(window->winId());
    if (hwnd) {
        LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        exStyle |= WS_EX_TOOLWINDOW;
        exStyle &= ~WS_EX_APPWINDOW;
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle);
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
}

void ShellController::configureOsdWindow(QWindow* window) {
    if (!window) {
        return;
    }
    HWND hwnd = reinterpret_cast<HWND>(window->winId());
    if (hwnd) {
        LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        exStyle |= WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT;
        exStyle &= ~WS_EX_APPWINDOW;
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle);
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
}

void ShellController::configureLauncherWindow(QWindow* window) {
    if (!window) {
        return;
    }
    const Qt::WindowFlags desired = Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool;
    if ((window->flags() & desired) != desired) {
        window->setFlags(desired);
    }
    HWND hwnd = reinterpret_cast<HWND>(window->winId());
    if (hwnd) {
        LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        exStyle |= WS_EX_TOOLWINDOW;
        exStyle &= ~WS_EX_APPWINDOW;
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle);
    }
}

void ShellController::activateLauncherWindow(QWindow* window) {
    if (!window) {
        return;
    }
    HWND hwnd = reinterpret_cast<HWND>(window->winId());
    if (!hwnd) {
        return;
    }
    HWND fg = GetForegroundWindow();
    const DWORD fgThread = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
    const DWORD curThread = GetCurrentThreadId();
    if (fgThread != 0 && fgThread != curThread) {
        AttachThreadInput(curThread, fgThread, TRUE);
        SetForegroundWindow(hwnd);
        BringWindowToTop(hwnd);
        AttachThreadInput(curThread, fgThread, FALSE);
    } else {
        SetForegroundWindow(hwnd);
        BringWindowToTop(hwnd);
    }
    window->requestActivate();
}

void ShellController::restoreExplorer() {
    m_restoring = true;
    Win32::setWindowsTaskbarsVisible(true);
    if (m_appBarManager) {
        m_appBarManager->setTaskbarHiddenMode(false);
    }
    if (m_settings) {
        m_settings->set(QStringLiteral("dock.hideWindowsTaskbar"), false);
    }
    if (m_windowTracker) {
        m_windowTracker->restoreAllWorkspaces();
    }
    if (m_appBarManager) {
        m_appBarManager->restoreAllWorkAreas();
    }
    Win32::launchExplorer();
    QTimer::singleShot(1200, this, [this]() {
        m_restoring = false;
        Q_EMIT explorerRunningChanged();
        if (m_appBarManager) {
            m_appBarManager->reapplyAllReservations();
        }
    });
}

void ShellController::exitShell() {
    m_restoring = true;
    Win32::setWindowsTaskbarsVisible(true);
    if (m_windowTracker) {
        m_windowTracker->restoreAllWorkspaces();
    }
    if (m_appBarManager) {
        m_appBarManager->restoreAllWorkAreas();
    }
    if (!Win32::isExplorerShellPresent()) {
        Win32::launchExplorer();
    }
    QCoreApplication::quit();
}

} // namespace SekaiPop::Services
