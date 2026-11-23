#pragma once

#include <QAbstractNativeEventFilter>
#include <QObject>
#include <windows.h>

namespace SekaiPop::Win32 {

/**
 * Registers global Win32 hotkeys & standalone Windows key hook for SekaiPop:
 *   Win (standalone tap)     -> Toggle SekaiPop Phone (keeps Win+E/R/D/L/etc. working!)
 *   Alt + Space              -> Toggle SekaiPop App Launcher
 *   Ctrl + Space             -> Toggle SekaiPop App Launcher (backup)
 *   Alt + N                  -> Toggle SekaiPop Phone / Quick Panel
 *   Ctrl + Alt + Shift + E   -> Emergency Restore Windows Explorer (explorer.exe) & Exit
 *   Ctrl + Alt + Shift + R   -> Emergency Restart SekaiPop Shell
 *   Ctrl + Alt + Shift + Q   -> Emergency Exit SekaiPop Shell
 */
class HotkeyManager final : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT

public:
    explicit HotkeyManager(QObject* parent = nullptr);
    ~HotkeyManager() override;

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;
    void setNativeShellMode(bool enabled) { m_nativeShellMode = enabled; }

Q_SIGNALS:
    void winKeyTapped();
    void launcherToggleRequested();
    void quickPanelToggleRequested();
    void restoreExplorerRequested();
    void restartShellRequested();
    void exitShellRequested();
    void nativeWinChordRequested(int vkCode, bool shiftHeld);
    void screenshotRequested(int mode); // 0=region, 1=activeMonitor, 2=activeWindow, 3=allMonitors
    void nativeMediaKeyRequested(int vkCode);

private:
    static LRESULT CALLBACK lowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);

    enum HotkeyId : int {
        HotkeyLauncherAlt = 1,
        HotkeyLauncherCtrl = 2,
        HotkeyRestoreExplorer = 3,
        HotkeyExitShell = 4,
        HotkeyQuickPanel = 5,
        HotkeyRestartShell = 6
    };

    static HotkeyManager* s_instance;
    HWND m_msgWindow = nullptr;
    HHOOK m_kbHook = nullptr;
    bool m_winDown = false;
    bool m_winChordUsed = false;
    bool m_nativeShellMode = false;
};

} // namespace SekaiPop::Win32
