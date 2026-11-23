#include "HotkeyManager.h"

#include <QCoreApplication>
#include <QMetaObject>

#ifndef MOD_NOREPEAT
#define MOD_NOREPEAT 0x4000
#endif

namespace SekaiPop::Win32 {

HotkeyManager* HotkeyManager::s_instance = nullptr;

LRESULT CALLBACK HotkeyManager::lowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && s_instance && lParam) {
        const auto* kb = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
        const bool injected = (kb->flags & LLKHF_INJECTED) != 0;
        if (!injected) {
            const bool isWinKey = (kb->vkCode == VK_LWIN || kb->vkCode == VK_RWIN);
            const bool isDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
            const bool isUp = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);

            if (isWinKey) {
                if (isDown) {
                    if (!s_instance->m_winDown) {
                        s_instance->m_winDown = true;
                        s_instance->m_winChordUsed = false;
                    }
                } else if (isUp) {
                    const bool soloTap = s_instance->m_winDown && !s_instance->m_winChordUsed;
                    s_instance->m_winDown = false;
                    s_instance->m_winChordUsed = false;
                    if (soloTap) {
                        // Swallow the original Win keyup (return 1) and atomically inject:
                        //   0xE8 down -> 0xE8 up -> Win up
                        // so win32k sees a non-solo Win chord (suppressing Windows Start Menu 100%)
                        // while cleanly releasing the Win key in OS keyboard state.
                        INPUT seq[3] = {};
                        seq[0].type = INPUT_KEYBOARD;
                        seq[0].ki.wVk = 0xE8;
                        seq[1].type = INPUT_KEYBOARD;
                        seq[1].ki.wVk = 0xE8;
                        seq[1].ki.dwFlags = KEYEVENTF_KEYUP;
                        seq[2].type = INPUT_KEYBOARD;
                        seq[2].ki.wVk = static_cast<WORD>(kb->vkCode);
                        seq[2].ki.dwFlags = KEYEVENTF_KEYUP;
                        SendInput(3, seq, sizeof(INPUT));

                        QMetaObject::invokeMethod(
                            s_instance,
                            &HotkeyManager::winKeyTapped,
                            Qt::QueuedConnection
                        );
                        return 1;
                    }
                }
            } else if (isDown && kb->vkCode == VK_SNAPSHOT) {
                // PrintScreen / Win+PrtSc / Alt+PrtSc / Ctrl+PrtSc:
                // When running without explorer.exe desktop shell (Winlogon Native Shell),
                // Windows's built-in UWP Snipping Tool overlay cannot activate. Intercept
                // PrintScreen directly and route to SekaiPop's native multi-monitor screenshot tool!
                const bool nativeNoExplorer = s_instance->m_nativeShellMode || (FindWindowW(L"Shell_TrayWnd", nullptr) == nullptr);
                if (nativeNoExplorer) {
                    const bool altHeld = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
                    const bool ctrlHeld = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
                    int mode = 0; // Region snip (matches Windows 11 PrtSc default)
                    if (s_instance->m_winDown) {
                        s_instance->m_winChordUsed = true;
                        mode = 1; // Active monitor instant capture
                    } else if (altHeld) {
                        mode = 2; // Active window instant capture
                    } else if (ctrlHeld) {
                        mode = 3; // All monitors instant capture
                    }
                    QMetaObject::invokeMethod(
                        s_instance,
                        [mode]() {
                            if (HotkeyManager::s_instance) {
                                Q_EMIT HotkeyManager::s_instance->screenshotRequested(mode);
                            }
                        },
                        Qt::QueuedConnection
                    );
                    return 1;
                }
            } else if (isDown && (kb->vkCode == VK_VOLUME_UP || kb->vkCode == VK_VOLUME_DOWN || kb->vkCode == VK_VOLUME_MUTE ||
                                  kb->vkCode == VK_MEDIA_PLAY_PAUSE || kb->vkCode == VK_MEDIA_NEXT_TRACK || kb->vkCode == VK_MEDIA_PREV_TRACK)) {
                const bool nativeNoExplorer = s_instance->m_nativeShellMode || (FindWindowW(L"Shell_TrayWnd", nullptr) == nullptr);
                if (nativeNoExplorer) {
                    const int vk = static_cast<int>(kb->vkCode);
                    QMetaObject::invokeMethod(
                        s_instance,
                        [vk]() {
                            if (HotkeyManager::s_instance) {
                                Q_EMIT HotkeyManager::s_instance->nativeMediaKeyRequested(vk);
                            }
                        },
                        Qt::QueuedConnection
                    );
                    return 1;
                }
            } else if (isDown && s_instance->m_winDown) {
                // User pressed Win + <another key> (e.g. Win+E, Win+R, Win+D, Win+L, Win+Shift+S).
                s_instance->m_winChordUsed = true;
                // When explorer.exe is NOT hosting the desktop shell (Full Native Shell mode via
                // Winlogon\Shell), intercept essential Win+key chords natively so every standard
                // Windows shortcut keeps working seamlessly.
                const bool nativeNoExplorer = s_instance->m_nativeShellMode || (FindWindowW(L"Shell_TrayWnd", nullptr) == nullptr);
                if (nativeNoExplorer) {
                    const bool shiftHeld = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
                    const bool ctrlHeld = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
                    const bool altHeld = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
                    const int vk = static_cast<int>(kb->vkCode);
                    const bool handledChord = (!ctrlHeld && !altHeld) && (
                        (!shiftHeld && (vk == 'E' || vk == 'R' || vk == 'I' || vk == 'D')) ||
                        (shiftHeld && vk == 'S')
                    );
                    if (handledChord) {
                        QMetaObject::invokeMethod(
                            s_instance,
                            [vk, shiftHeld]() {
                                if (HotkeyManager::s_instance) {
                                    Q_EMIT HotkeyManager::s_instance->nativeWinChordRequested(vk, shiftHeld);
                                }
                            },
                            Qt::QueuedConnection
                        );
                        return 1;
                    }
                }
            }
        }
    }
    return CallNextHookEx(s_instance ? s_instance->m_kbHook : nullptr, nCode, wParam, lParam);
}

HotkeyManager::HotkeyManager(QObject* parent)
    : QObject(parent) {
    s_instance = this;
    m_msgWindow = CreateWindowExW(
        0,
        L"STATIC",
        L"SekaiPopHotkeySink",
        0,
        0, 0, 0, 0,
        HWND_MESSAGE,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr
    );

    m_kbHook = SetWindowsHookExW(
        WH_KEYBOARD_LL,
        &HotkeyManager::lowLevelKeyboardProc,
        GetModuleHandleW(nullptr),
        0
    );

    if (m_msgWindow) {
        const BOOL okAlt = RegisterHotKey(m_msgWindow, HotkeyLauncherAlt, MOD_ALT | MOD_NOREPEAT, VK_SPACE);
        const BOOL okCtrl = RegisterHotKey(m_msgWindow, HotkeyLauncherCtrl, MOD_CONTROL | MOD_NOREPEAT, VK_SPACE);
        const BOOL okQuick = RegisterHotKey(m_msgWindow, HotkeyQuickPanel, MOD_ALT | MOD_NOREPEAT, 'N');
        const BOOL okRest = RegisterHotKey(m_msgWindow, HotkeyRestoreExplorer, MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, 'E');
        const BOOL okRestart = RegisterHotKey(m_msgWindow, HotkeyRestartShell, MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, 'R');
        const BOOL okExit = RegisterHotKey(m_msgWindow, HotkeyExitShell, MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, 'Q');
        qInfo("[HotkeyManager] WinHook=%d Alt+Space=%d Ctrl+Space=%d Alt+N=%d Restore=%d Restart=%d Exit=%d",
              m_kbHook ? 1 : 0, okAlt ? 1 : 0, okCtrl ? 1 : 0, okQuick ? 1 : 0, okRest ? 1 : 0, okRestart ? 1 : 0, okExit ? 1 : 0);
    }

    if (auto* app = QCoreApplication::instance()) {
        app->installNativeEventFilter(this);
    }
}

HotkeyManager::~HotkeyManager() {
    if (m_kbHook) {
        UnhookWindowsHookEx(m_kbHook);
        m_kbHook = nullptr;
    }
    if (s_instance == this) {
        s_instance = nullptr;
    }
    if (auto* app = QCoreApplication::instance()) {
        app->removeNativeEventFilter(this);
    }
    if (m_msgWindow) {
        UnregisterHotKey(m_msgWindow, HotkeyLauncherAlt);
        UnregisterHotKey(m_msgWindow, HotkeyLauncherCtrl);
        UnregisterHotKey(m_msgWindow, HotkeyQuickPanel);
        UnregisterHotKey(m_msgWindow, HotkeyRestoreExplorer);
        UnregisterHotKey(m_msgWindow, HotkeyRestartShell);
        UnregisterHotKey(m_msgWindow, HotkeyExitShell);
        DestroyWindow(m_msgWindow);
        m_msgWindow = nullptr;
    }
}

bool HotkeyManager::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) {
    Q_UNUSED(result);
    if (eventType != "windows_generic_MSG" && eventType != "windows_dispatcher_MSG") {
        return false;
    }
    MSG* msg = static_cast<MSG*>(message);
    if (!msg || msg->message != WM_HOTKEY || msg->hwnd != m_msgWindow) {
        return false;
    }

    switch (static_cast<int>(msg->wParam)) {
    case HotkeyLauncherAlt:
    case HotkeyLauncherCtrl:
        Q_EMIT launcherToggleRequested();
        return true;
    case HotkeyQuickPanel:
        Q_EMIT quickPanelToggleRequested();
        return true;
    case HotkeyRestoreExplorer:
        Q_EMIT restoreExplorerRequested();
        return true;
    case HotkeyRestartShell:
        Q_EMIT restartShellRequested();
        return true;
    case HotkeyExitShell:
        Q_EMIT exitShellRequested();
        return true;
    default:
        return false;
    }
}

} // namespace SekaiPop::Win32
