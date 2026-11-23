#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>

#include <algorithm>
#include <atomic>
#include <cwctype>
#include <string>
#include <thread>
#include <vector>

namespace {

std::atomic<bool> g_shuttingDown{false};
HANDLE g_childProcess = nullptr;

std::wstring toLowerW(std::wstring s) {
    for (wchar_t& ch : s) {
        ch = static_cast<wchar_t>(std::towlower(ch));
    }
    return s;
}

std::wstring exeDirectory() {
    wchar_t buf[MAX_PATH * 4] = {};
    DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    if (n == 0 || n >= std::size(buf)) {
        return L".";
    }
    std::wstring p(buf, n);
    size_t pos = p.find_last_of(L"\\/");
    return (pos == std::wstring::npos) ? L"." : p.substr(0, pos);
}

bool launchWindowsExplorer() {
    wchar_t winDir[MAX_PATH] = {};
    UINT len = GetWindowsDirectoryW(winDir, MAX_PATH);
    std::wstring expPath = (len > 0 && len < MAX_PATH)
        ? (std::wstring(winDir, len) + L"\\explorer.exe")
        : L"C:\\Windows\\explorer.exe";

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    std::wstring cmd = L"\"" + expPath + L"\"";
    if (CreateProcessW(expPath.c_str(), cmd.data(), nullptr, nullptr, FALSE,
                       CREATE_DEFAULT_ERROR_MODE | NORMAL_PRIORITY_CLASS,
                       nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return true;
    }
    HINSTANCE res = ShellExecuteW(nullptr, L"open", expPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(res) > 32;
}

bool isSekaiPopCommand(const std::wstring& cmd) {
    const std::wstring low = toLowerW(cmd);
    return low.find(L"sekaipopshell") != std::wstring::npos ||
           low.find(L"sekaipopboot") != std::wstring::npos;
}

void runRegistryRunKey(HKEY root, const wchar_t* subKey) {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(root, subKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return;
    }

    DWORD index = 0;
    wchar_t valName[512] = {};
    BYTE data[4096] = {};
    for (;;) {
        DWORD valNameLen = static_cast<DWORD>(std::size(valName));
        DWORD dataLen = sizeof(data) - sizeof(wchar_t);
        DWORD type = 0;
        LONG rc = RegEnumValueW(hKey, index++, valName, &valNameLen, nullptr, &type, data, &dataLen);
        if (rc == ERROR_NO_MORE_ITEMS) {
            break;
        }
        if (rc != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || dataLen < sizeof(wchar_t)) {
            continue;
        }
        data[dataLen] = 0;
        data[dataLen + 1] = 0;
        std::wstring rawCmd(reinterpret_cast<const wchar_t*>(data));
        if (rawCmd.empty() || isSekaiPopCommand(rawCmd)) {
            continue;
        }

        std::wstring expanded = rawCmd;
        if (type == REG_EXPAND_SZ) {
            wchar_t expBuf[4096] = {};
            DWORD expN = ExpandEnvironmentStringsW(rawCmd.c_str(), expBuf, static_cast<DWORD>(std::size(expBuf)));
            if (expN > 0 && expN < std::size(expBuf)) {
                expanded.assign(expBuf, expN - 1);
            }
        }

        if (g_shuttingDown.load()) {
            break;
        }
        // Launch standard startup entry asynchronously at below-normal priority with light stagger
        // so heavy background apps (Docker Desktop, Discord, WARP) never starve the shell UI.
        STARTUPINFOW si = {};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi = {};
        std::wstring mutableCmd = expanded;
        if (CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE,
                           CREATE_DEFAULT_ERROR_MODE | BELOW_NORMAL_PRIORITY_CLASS, nullptr, nullptr, &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            Sleep(200);
        }
    }
    RegCloseKey(hKey);
}

void runStartupFolder(REFKNOWNFOLDERID folderId) {
    PWSTR pathPtr = nullptr;
    if (FAILED(SHGetKnownFolderPath(folderId, 0, nullptr, &pathPtr)) || !pathPtr) {
        return;
    }
    std::wstring folder(pathPtr);
    CoTaskMemFree(pathPtr);

    WIN32_FIND_DATAW fd = {};
    HANDLE hFind = FindFirstFileW((folder + L"\\*").c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        return;
    }
    do {
        if (g_shuttingDown.load()) {
            break;
        }
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            continue;
        }
        std::wstring name(fd.cFileName);
        std::wstring low = toLowerW(name);
        if (low == L"desktop.ini" || low.find(L"sekaipop") != std::wstring::npos) {
            continue;
        }
        std::wstring full = folder + L"\\" + name;
        ShellExecuteW(nullptr, L"open", full.c_str(), nullptr, folder.c_str(), SW_SHOWNORMAL);
        Sleep(200);
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
}

void launchWindowsStartupAppsOnce(HANDLE hUiReady) {
    // Only run Windows startup apps once per logon session so restarting SekaiPop does not re-launch them.
    HANDLE hEvt = CreateEventW(nullptr, TRUE, FALSE, L"Local\\SekaiPopBoot_StartupRan_v1");
    if (!hEvt) {
        return;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(hEvt);
        return;
    }
    HANDLE hThreadEvt = nullptr;
    if (hUiReady) {
        DuplicateHandle(GetCurrentProcess(), hUiReady, GetCurrentProcess(), &hThreadEvt,
                        SYNCHRONIZE, FALSE, 0);
    }
    // Keep handle open for process lifetime so the marker stays alive during this logon session.
    std::thread([hThreadEvt]() {
        // Wait until SekaiPopShell signals that Wallpaper + Bar are visible on screen
        // (with a 15s safety cap), so Docker Desktop / Discord / VPN never starve cold boot.
        if (hThreadEvt) {
            WaitForSingleObject(hThreadEvt, 15000);
            CloseHandle(hThreadEvt);
        } else {
            Sleep(2500);
        }
        if (g_shuttingDown.load()) {
            return;
        }
        Sleep(600);
        if (g_shuttingDown.load()) {
            return;
        }
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        runRegistryRunKey(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run");
        runRegistryRunKey(HKEY_LOCAL_MACHINE, L"Software\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Run");
        runRegistryRunKey(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run");
        runStartupFolder(FOLDERID_CommonStartup);
        runStartupFolder(FOLDERID_Startup);
        CoUninitialize();
    }).detach();
}

LRESULT CALLBACK sessionWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_QUERYENDSESSION) {
        g_shuttingDown.store(true);
        return TRUE;
    }
    if (msg == WM_ENDSESSION) {
        if (wParam) {
            g_shuttingDown.store(true);
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

HWND createSessionMonitorWindow(HINSTANCE hInst) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &sessionWndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"SekaiPopBootSessionWindow";
    RegisterClassExW(&wc);
    return CreateWindowExW(0, wc.lpszClassName, L"SekaiPopBoot", WS_OVERLAPPED,
                           0, 0, 0, 0, nullptr, nullptr, hInst, nullptr);
}

bool waitProcessPumpingMessages(HANDLE hProc, DWORD& outExitCode) {
    outExitCode = 0;
    for (;;) {
        DWORD res = MsgWaitForMultipleObjects(1, &hProc, FALSE, INFINITE, QS_ALLINPUT);
        if (res == WAIT_OBJECT_0) {
            GetExitCodeProcess(hProc, &outExitCode);
            return true;
        }
        if (res == WAIT_OBJECT_0 + 1) {
            MSG msg = {};
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            if (g_shuttingDown.load()) {
                // Allow SekaiPopShell up to 2.5 seconds to exit cleanly on Windows shutdown/sign-out.
                if (WaitForSingleObject(hProc, 2500) == WAIT_OBJECT_0) {
                    GetExitCodeProcess(hProc, &outExitCode);
                }
                return true;
            }
        } else {
            return false;
        }
    }
}

void signalWindowsShellReady() {
    // Tell winlogon.exe immediately that the desktop shell is ready so Windows 11
    // dismisses the logon spinner instantly instead of waiting for ShellStartupTimeout.
    for (const wchar_t* evtName : {L"ShellReadyEvent", L"Global\\ShellReadyEvent", L"Local\\ShellStartupEvent"}) {
        HANDLE h = OpenEventW(EVENT_MODIFY_STATE, FALSE, evtName);
        if (h) {
            SetEvent(h);
            CloseHandle(h);
        }
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR cmdLine, int) {
    // Failsafe bypass: holding Left Shift during Windows logon immediately boots standard explorer.exe.
    if ((GetAsyncKeyState(VK_LSHIFT) & 0x8000) != 0) {
        signalWindowsShellReady();
        launchWindowsExplorer();
        return 0;
    }

    const std::wstring args = toLowerW(cmdLine ? cmdLine : L"");
    const bool skipStartupApps = (args.find(L"--no-startup-apps") != std::wstring::npos);

    // Single-instance watchdog guard per logon session.
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"Local\\SekaiPopBoot_Watchdog_v1");
    if (hMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(hMutex);
        return 0;
    }

    HWND sessionHwnd = createSessionMonitorWindow(hInst);

    const std::wstring dir = exeDirectory();
    const std::wstring shellExe = dir + L"\\SekaiPopShell.exe";
    if (GetFileAttributesW(shellExe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        signalWindowsShellReady();
        launchWindowsExplorer();
        MessageBoxW(
            nullptr,
            L"SekaiPopShell.exe tidak ditemukan. Windows Explorer telah dijalankan secara otomatis untuk pemulihan.",
            L"SekaiPop Native Shell Recovery",
            MB_OK | MB_ICONWARNING | MB_TOPMOST
        );
        if (sessionHwnd) DestroyWindow(sessionHwnd);
        if (hMutex) CloseHandle(hMutex);
        return 1;
    }

    // Create named UI-ready event signaled by SekaiPopShell once Wallpaper + Bar are visible.
    HANDLE hUiReady = CreateEventW(nullptr, TRUE, FALSE, L"Local\\SekaiPopShell_UiReady_v1");

    // Dismiss Windows 11 logon welcome spinner immediately as we hand off to SekaiPopShell.
    signalWindowsShellReady();

    // Because explorer.exe is replaced by Winlogon, launch standard Windows Startup apps once per logon
    // AFTER SekaiPopShell signals UI ready.
    if (!skipStartupApps && FindWindowW(L"Shell_TrayWnd", nullptr) == nullptr) {
        launchWindowsStartupAppsOnce(hUiReady);
    }

    std::vector<ULONGLONG> recentCrashTicks;

    while (!g_shuttingDown.load() && GetSystemMetrics(SM_SHUTTINGDOWN) == 0) {
        if (hUiReady) {
            ResetEvent(hUiReady);
        }
        std::wstring cmd = L"\"" + shellExe + L"\" --native-shell";
        STARTUPINFOW si = {};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi = {};
        if (!CreateProcessW(shellExe.c_str(), cmd.data(), nullptr, nullptr, FALSE,
                            CREATE_DEFAULT_ERROR_MODE | ABOVE_NORMAL_PRIORITY_CLASS,
                            nullptr, dir.c_str(), &si, &pi)) {
            launchWindowsExplorer();
            MessageBoxW(
                nullptr,
                L"Gagal menjalankan SekaiPopShell.exe. Windows Explorer telah dijalankan otomatis sebagai cadangan.",
                L"SekaiPop Native Shell Recovery",
                MB_OK | MB_ICONERROR | MB_TOPMOST
            );
            break;
        }

        CloseHandle(pi.hThread);
        g_childProcess = pi.hProcess;
        DWORD exitCode = 0;
        waitProcessPumpingMessages(pi.hProcess, exitCode);
        CloseHandle(pi.hProcess);
        g_childProcess = nullptr;

        if (g_shuttingDown.load() || GetSystemMetrics(SM_SHUTTINGDOWN) != 0) {
            break;
        }

        // Exit code 42 = clean user-requested Restart SekaiPop (Ctrl+Shift+Alt+R or tray/menu)
        if (exitCode == 42) {
            Sleep(150);
            continue;
        }

        // Exit code 0 = clean user exit or "Restore Windows Explorer"
        if (exitCode == 0) {
            if (FindWindowW(L"Shell_TrayWnd", nullptr) == nullptr) {
                launchWindowsExplorer();
            }
            break;
        }

        // Unexpected crash: auto-restart, with failsafe fallback to explorer.exe after 3 crashes in 25s
        const ULONGLONG now = GetTickCount64();
        recentCrashTicks.push_back(now);
        recentCrashTicks.erase(
            std::remove_if(recentCrashTicks.begin(), recentCrashTicks.end(),
                           [now](ULONGLONG t) { return (now - t) > 25000ULL; }),
            recentCrashTicks.end()
        );

        if (recentCrashTicks.size() >= 3) {
            launchWindowsExplorer();
            MessageBoxW(
                nullptr,
                L"SekaiPopShell mengalami gangguan berulang saat berjalan.\n\n"
                L"Windows Explorer telah dipulihkan otomatis agar desktop tetap dapat digunakan.\n"
                L"Anda dapat membuka SekaiPop Manager dari Start Menu untuk memperbaiki atau mengganti mode shell.",
                L"SekaiPop Failsafe Recovery",
                MB_OK | MB_ICONWARNING | MB_TOPMOST
            );
            break;
        }

        Sleep(400);
    }

    if (hUiReady) CloseHandle(hUiReady);
    if (sessionHwnd) DestroyWindow(sessionHwnd);
    if (hMutex) {
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
    }
    return 0;
}
