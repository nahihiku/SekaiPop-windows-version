#pragma once

#include <QString>
#include <windows.h>
#include <objbase.h>

namespace SekaiPop::Win32 {

/**
 * RAII guard for COM apartment initialization on the current thread.
 * Safe when COM is already initialized in the same or compatible mode.
 */
class ComInitGuard final {
public:
    explicit ComInitGuard(DWORD coInit = COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    ~ComInitGuard();

    ComInitGuard(const ComInitGuard&) = delete;
    ComInitGuard& operator=(const ComInitGuard&) = delete;

    bool isInitialized() const noexcept { return m_shouldUninit; }

private:
    bool m_shouldUninit = false;
};

/** Converts QString to std::wstring cleanly without STL/Qt macro conflicts. */
std::wstring toStdWString(const QString& s);

/** Returns true if explorer.exe currently hosts a Windows taskbar shell window. */
bool isExplorerShellPresent();

/** Returns true if the primary Windows taskbar (Shell_TrayWnd) is currently visible. */
bool isWindowsTaskbarVisible();

/** Shows or hides Windows taskbar windows (Shell_TrayWnd and Shell_SecondaryTrayWnd) without altering the Registry. */
void setWindowsTaskbarsVisible(bool visible);

/** Launches or restores C:\Windows\explorer.exe safely via Win32 CreateProcessW. */
bool launchExplorer();

/** Brings a top-level HWND to the foreground reliably across processes (SW_RESTORE + AllowSetForegroundWindow + AttachThreadInput). */
bool focusWindowForce(HWND hwnd);

/** Launches a file, executable, shortcut (.lnk), AUMID (shell:AppsFolder\...), or URL. */
bool launchShellTarget(const QString& target, const QString& args = {}, const QString& workDir = {}, int showCmd = SW_SHOWNORMAL);

/** Executes a Windows command via cmd.exe (hidden background or interactive terminal). */
bool runWindowsCommand(const QString& command, bool inTerminal);

/** Reads friendly product/file description from executable version info, falling back to base filename. */
QString friendlyAppNameFromExe(const QString& exePath);

/**
 * Registers a hidden top-level Taskman/ShellHook window and ensures ARW_HIDE is enabled
 * whenever SekaiPop runs without explorer.exe's desktop shell (Shell_TrayWnd absent).
 * Without a registered TaskmanWindow, Windows win32k parks minimized apps as classic
 * floating titlebar pills at the bottom-left of the desktop instead of hiding them off-screen.
 */
class NativeTaskmanHost final {
public:
    NativeTaskmanHost() = default;
    ~NativeTaskmanHost();

    NativeTaskmanHost(const NativeTaskmanHost&) = delete;
    NativeTaskmanHost& operator=(const NativeTaskmanHost&) = delete;

    /** Claims TaskmanWindow + ShellHook + ARW_HIDE if explorer.exe shell is not present, and tucks any stray iconic pills off-screen. */
    void ensureActive();

    /** Moves any currently minimized top-level windows sitting on-screen off to (-32000, -32000) without restoring or flashing them. */
    void tuckOnScreenMinimizedWindows() const;

    /** Releases TaskmanWindow, ShellHook, hidden HWND, and restores original MINIMIZEDMETRICS if modified. */
    void release();

    bool isActive() const noexcept { return m_hwnd != nullptr && m_taskmanClaimed; }

private:
    HWND m_hwnd = nullptr;
    bool m_taskmanClaimed = false;
    bool m_shellHookRegistered = false;
    bool m_modifiedMinMetrics = false;
    MINIMIZEDMETRICS m_origMinMetrics = {};
};

} // namespace SekaiPop::Win32
