#pragma once

#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <tlhelp32.h>

#include <cwctype>
#include <string>
#include <vector>

namespace SekaiPop::Installer {

enum class ShellMode {
    ExplorerDefault = 0,
    SeamlessAutoStart = 1,
    NativeWinlogonShell = 2
};

inline int querySystemDpi() {
    using GetDpiForSysFn = UINT(WINAPI*)();
    if (HMODULE u32 = GetModuleHandleW(L"user32.dll")) {
        if (auto fn = reinterpret_cast<GetDpiForSysFn>(GetProcAddress(u32, "GetDpiForSystem"))) {
            const UINT d = fn();
            if (d > 0) return static_cast<int>(d);
        }
    }
    HDC hdc = GetDC(nullptr);
    int d = hdc ? GetDeviceCaps(hdc, LOGPIXELSY) : 96;
    if (hdc) ReleaseDC(nullptr, hdc);
    return d > 0 ? d : 96;
}

inline int queryWindowDpi(HWND hwnd) {
    using GetDpiForWndFn = UINT(WINAPI*)(HWND);
    if (hwnd) {
        if (HMODULE u32 = GetModuleHandleW(L"user32.dll")) {
            if (auto fn = reinterpret_cast<GetDpiForWndFn>(GetProcAddress(u32, "GetDpiForWindow"))) {
                const UINT d = fn(hwnd);
                if (d > 0) return static_cast<int>(d);
            }
        }
    }
    return querySystemDpi();
}

inline std::wstring toLowerW(std::wstring s) {
    for (wchar_t& c : s) {
        c = static_cast<wchar_t>(std::towlower(c));
    }
    return s;
}

inline std::wstring stripTrailingSlash(std::wstring p) {
    while (p.size() > 3 && (p.back() == L'\\' || p.back() == L'/')) {
        p.pop_back();
    }
    return p;
}

inline bool readRegString(HKEY root, const wchar_t* subKey, const wchar_t* valueName, std::wstring& out) {
    out.clear();
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(root, subKey, 0, KEY_READ | KEY_WOW64_64KEY, &hKey) != ERROR_SUCCESS) {
        return false;
    }
    DWORD type = 0;
    DWORD cb = 0;
    LONG rc = RegQueryValueExW(hKey, valueName, nullptr, &type, nullptr, &cb);
    if (rc != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || cb == 0) {
        RegCloseKey(hKey);
        return false;
    }
    std::wstring buf(cb / sizeof(wchar_t) + 1, L'\0');
    rc = RegQueryValueExW(hKey, valueName, nullptr, &type, reinterpret_cast<BYTE*>(buf.data()), &cb);
    RegCloseKey(hKey);
    if (rc != ERROR_SUCCESS) {
        return false;
    }
    size_t nul = buf.find(L'\0');
    if (nul != std::wstring::npos) {
        buf.resize(nul);
    }
    out = std::move(buf);
    return true;
}

inline bool writeRegString(HKEY root, const wchar_t* subKey, const wchar_t* valueName, const std::wstring& val) {
    HKEY hKey = nullptr;
    if (RegCreateKeyExW(root, subKey, 0, nullptr, 0, KEY_SET_VALUE | KEY_WOW64_64KEY, nullptr, &hKey, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    const DWORD cb = static_cast<DWORD>((val.size() + 1) * sizeof(wchar_t));
    LONG rc = RegSetValueExW(hKey, valueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(val.c_str()), cb);
    RegCloseKey(hKey);
    return rc == ERROR_SUCCESS;
}

inline bool writeRegDword(HKEY root, const wchar_t* subKey, const wchar_t* valueName, DWORD val) {
    HKEY hKey = nullptr;
    if (RegCreateKeyExW(root, subKey, 0, nullptr, 0, KEY_SET_VALUE | KEY_WOW64_64KEY, nullptr, &hKey, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    LONG rc = RegSetValueExW(hKey, valueName, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&val), sizeof(val));
    RegCloseKey(hKey);
    return rc == ERROR_SUCCESS;
}

inline void deleteRegValue(HKEY root, const wchar_t* subKey, const wchar_t* valueName) {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(root, subKey, 0, KEY_SET_VALUE | KEY_WOW64_64KEY, &hKey) == ERROR_SUCCESS) {
        RegDeleteValueW(hKey, valueName);
        RegCloseKey(hKey);
    }
}

inline void deleteRegTree(HKEY root, const wchar_t* subKey) {
    RegDeleteKeyExW(root, subKey, KEY_WOW64_64KEY, 0);
}

inline void terminateProcessesByName(const wchar_t* exeName) {
    const std::wstring targetLow = toLowerW(exeName);
    const DWORD selfPid = GetCurrentProcessId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        return;
    }
    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof(pe);
    std::vector<HANDLE> handles;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (pe.th32ProcessID == selfPid) {
                continue;
            }
            if (toLowerW(pe.szExeFile) == targetLow) {
                HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pe.th32ProcessID);
                if (h) {
                    TerminateProcess(h, 0);
                    handles.push_back(h);
                }
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    if (!handles.empty()) {
        WaitForMultipleObjects(static_cast<DWORD>(handles.size()), handles.data(), TRUE, 3000);
        for (HANDLE h : handles) {
            CloseHandle(h);
        }
    }
}

inline void stopSekaiPopProcesses() {
    // Stop watchdog first so it does not respawn SekaiPopShell while stopping.
    terminateProcessesByName(L"SekaiPopBoot.exe");
    terminateProcessesByName(L"SekaiPopShell.exe");
    Sleep(200);
}

inline void restoreWindowsTaskbarAndExplorer(bool ensureExplorerRunning = true) {
    HWND primary = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (primary) {
        ShowWindow(primary, SW_SHOW);
    }
    HWND sec = nullptr;
    while ((sec = FindWindowExW(nullptr, sec, L"Shell_SecondaryTrayWnd", nullptr)) != nullptr) {
        ShowWindow(sec, SW_SHOW);
    }
    // Reset primary work area cleanly so maximized windows aren't stuck with old reserved margins.
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    HMONITOR hMon = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    if (hMon && GetMonitorInfoW(hMon, &mi)) {
        RECT full = mi.rcMonitor;
        SystemParametersInfoW(SPI_SETWORKAREA, 0, &full, SPIF_SENDCHANGE);
    }
    if (ensureExplorerRunning && !primary) {
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
        }
    }
}

inline ShellMode queryCurrentShellMode() {
    constexpr wchar_t kIniBoot[] = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\IniFileMapping\\system.ini\\boot";
    constexpr wchar_t kWinlogon[] = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon";
    constexpr wchar_t kRun[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

    std::wstring mapping;
    std::wstring userShell;
    if (readRegString(HKEY_LOCAL_MACHINE, kIniBoot, L"Shell", mapping) &&
        toLowerW(mapping).find(L"usr:") == 0 &&
        readRegString(HKEY_CURRENT_USER, kWinlogon, L"Shell", userShell) &&
        toLowerW(userShell).find(L"sekaipop") != std::wstring::npos) {
        return ShellMode::NativeWinlogonShell;
    }

    std::wstring sysShell;
    if (readRegString(HKEY_LOCAL_MACHINE, kWinlogon, L"Shell", sysShell) &&
        toLowerW(sysShell).find(L"sekaipop") != std::wstring::npos) {
        return ShellMode::NativeWinlogonShell;
    }

    std::wstring runVal;
    if (readRegString(HKEY_CURRENT_USER, kRun, L"SekaiPopShell", runVal) &&
        toLowerW(runVal).find(L"sekaipop") != std::wstring::npos) {
        return ShellMode::SeamlessAutoStart;
    }

    return ShellMode::ExplorerDefault;
}

inline std::wstring queryInstalledDir() {
    std::wstring dir;
    if (readRegString(HKEY_LOCAL_MACHINE,
                      L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\SekaiPopShell",
                      L"InstallLocation", dir) && !dir.empty()) {
        return stripTrailingSlash(dir);
    }
    return L"C:\\Program Files\\SekaiPop";
}

inline bool applyShellMode(ShellMode mode, const std::wstring& installDirRaw, std::wstring& outErr) {
    outErr.clear();
    const std::wstring dir = stripTrailingSlash(installDirRaw);
    constexpr wchar_t kIniBoot[] = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\IniFileMapping\\system.ini\\boot";
    constexpr wchar_t kWinlogon[] = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon";
    constexpr wchar_t kRun[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

    if (mode == ShellMode::NativeWinlogonShell) {
        const std::wstring bootExe = dir + L"\\SekaiPopBoot.exe";
        if (GetFileAttributesW(bootExe.c_str()) == INVALID_FILE_ATTRIBUTES) {
            outErr = L"SekaiPopBoot.exe tidak ditemukan di: " + bootExe;
            return false;
        }
        const std::wstring quotedBoot = L"\"" + bootExe + L"\"";

        // Windows 11 winlogon.exe and userinit.exe read HKLM\...\Winlogon\Shell directly (and
        // AutoRestartShell controls whether winlogon respawns explorer.exe when explorer exits).
        // Set both HKLM and HKCU Winlogon\Shell to SekaiPopBoot.exe and disable AutoRestartShell
        // so explorer.exe never launches at logon and never respawns behind SekaiPop.
        // SekaiPopBoot.exe provides built-in failsafe recovery (hold Left Shift at logon or 3-crash fallback).
        if (!writeRegString(HKEY_LOCAL_MACHINE, kWinlogon, L"Shell", quotedBoot)) {
            outErr = L"Gagal menulis HKLM Winlogon\\Shell (butuh izin Administrator).";
            return false;
        }
        writeRegDword(HKEY_LOCAL_MACHINE, kWinlogon, L"AutoRestartShell", 0);
        writeRegString(HKEY_LOCAL_MACHINE, kIniBoot, L"Shell",
                       L"USR:Software\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon");
        writeRegString(HKEY_CURRENT_USER, kWinlogon, L"Shell", quotedBoot);

        // Remove Run auto-start so SekaiPop does not launch twice.
        deleteRegValue(HKEY_CURRENT_USER, kRun, L"SekaiPopShell");
        return true;
    }

    // Both SeamlessAutoStart and ExplorerDefault revert Winlogon completely back to standard explorer.exe.
    deleteRegValue(HKEY_CURRENT_USER, kWinlogon, L"Shell");
    writeRegString(HKEY_LOCAL_MACHINE, kIniBoot, L"Shell",
                   L"SYS:Microsoft\\Windows NT\\CurrentVersion\\Winlogon");
    writeRegString(HKEY_LOCAL_MACHINE, kWinlogon, L"Shell", L"explorer.exe");
    writeRegDword(HKEY_LOCAL_MACHINE, kWinlogon, L"AutoRestartShell", 1);

    if (mode == ShellMode::SeamlessAutoStart) {
        const std::wstring shellExe = dir + L"\\SekaiPopShell.exe";
        if (!writeRegString(HKEY_CURRENT_USER, kRun, L"SekaiPopShell", L"\"" + shellExe + L"\"")) {
            outErr = L"Gagal menulis entri Auto-Start di Registry.";
            return false;
        }
        return true;
    }

    // ExplorerDefault
    deleteRegValue(HKEY_CURRENT_USER, kRun, L"SekaiPopShell");
    return true;
}

inline bool createShortcutLnk(
    const std::wstring& lnkPath,
    const std::wstring& targetPath,
    const std::wstring& arguments,
    const std::wstring& workDir,
    const std::wstring& description,
    const std::wstring& iconPath
) {
    IShellLinkW* psl = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&psl));
    if (FAILED(hr) || !psl) {
        return false;
    }
    psl->SetPath(targetPath.c_str());
    if (!arguments.empty()) {
        psl->SetArguments(arguments.c_str());
    }
    if (!workDir.empty()) {
        psl->SetWorkingDirectory(workDir.c_str());
    }
    if (!description.empty()) {
        psl->SetDescription(description.c_str());
    }
    if (!iconPath.empty()) {
        psl->SetIconLocation(iconPath.c_str(), 0);
    }

    IPersistFile* ppf = nullptr;
    hr = psl->QueryInterface(IID_PPV_ARGS(&ppf));
    bool ok = false;
    if (SUCCEEDED(hr) && ppf) {
        ok = SUCCEEDED(ppf->Save(lnkPath.c_str(), TRUE));
        ppf->Release();
    }
    psl->Release();
    return ok;
}

inline std::wstring getKnownFolderPathStr(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    if (FAILED(SHGetKnownFolderPath(id, 0, nullptr, &p)) || !p) {
        return {};
    }
    std::wstring res(p);
    CoTaskMemFree(p);
    return res;
}

inline void createSekaiPopShortcuts(const std::wstring& installDirRaw, bool startMenu, bool desktop) {
    const std::wstring dir = stripTrailingSlash(installDirRaw);
    const std::wstring shellExe = dir + L"\\SekaiPopShell.exe";
    const std::wstring mgrExe = dir + L"\\SekaiPopManager.exe";

    if (startMenu) {
        std::wstring progDir = getKnownFolderPathStr(FOLDERID_CommonPrograms);
        if (progDir.empty()) {
            progDir = getKnownFolderPathStr(FOLDERID_Programs);
        }
        if (!progDir.empty()) {
            const std::wstring smDir = progDir + L"\\SekaiPop";
            SHCreateDirectoryExW(nullptr, smDir.c_str(), nullptr);
            createShortcutLnk(smDir + L"\\SekaiPop Shell.lnk", shellExe, L"", dir,
                              L"Jalankan SekaiPop Desktop Shell", shellExe);
            createShortcutLnk(smDir + L"\\SekaiPop Control Center.lnk", mgrExe, L"", dir,
                              L"Atur mode Shell Windows, perbaikan, dan konfigurasi SekaiPop", mgrExe);
            createShortcutLnk(smDir + L"\\Restore Windows Explorer.lnk", mgrExe, L"--restore-explorer", dir,
                              L"Pulihkan Windows Explorer dan Taskbar standar Windows", mgrExe);
            createShortcutLnk(smDir + L"\\Uninstall SekaiPop.lnk", mgrExe, L"--uninstall", dir,
                              L"Hapus instalasi SekaiPop dan pulihkan UI Windows", mgrExe);
        }
    }

    if (desktop) {
        std::wstring deskDir = getKnownFolderPathStr(FOLDERID_PublicDesktop);
        if (deskDir.empty()) {
            deskDir = getKnownFolderPathStr(FOLDERID_Desktop);
        }
        if (!deskDir.empty()) {
            createShortcutLnk(deskDir + L"\\SekaiPop Control Center.lnk", mgrExe, L"", dir,
                              L"Atur mode Shell Windows SekaiPop atau pulihkan Windows Explorer", mgrExe);
        }
    }
}

inline void removeDirectoryRecursive(const std::wstring& dir) {
    if (dir.size() < 6) {
        return; // safety guard against accidental root delete
    }
    WIN32_FIND_DATAW fd = {};
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) {
                continue;
            }
            const std::wstring full = dir + L"\\" + fd.cFileName;
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
                RemoveDirectoryW(full.c_str());
                DeleteFileW(full.c_str());
            } else if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                removeDirectoryRecursive(full);
            } else {
                SetFileAttributesW(full.c_str(), FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(full.c_str());
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(dir.c_str());
}

inline void removeSekaiPopShortcuts() {
    for (const KNOWNFOLDERID& id : {FOLDERID_CommonPrograms, FOLDERID_Programs}) {
        const std::wstring progDir = getKnownFolderPathStr(id);
        if (!progDir.empty()) {
            removeDirectoryRecursive(progDir + L"\\SekaiPop");
        }
    }
    for (const KNOWNFOLDERID& id : {FOLDERID_PublicDesktop, FOLDERID_Desktop}) {
        const std::wstring deskDir = getKnownFolderPathStr(id);
        if (!deskDir.empty()) {
            DeleteFileW((deskDir + L"\\SekaiPop Shell.lnk").c_str());
            DeleteFileW((deskDir + L"\\SekaiPop Control Center.lnk").c_str());
        }
    }
}

inline void registerUninstallEntry(const std::wstring& installDirRaw, DWORD estimatedSizeKB) {
    const std::wstring dir = stripTrailingSlash(installDirRaw);
    const std::wstring mgrExe = dir + L"\\SekaiPopManager.exe";
    const std::wstring shellExe = dir + L"\\SekaiPopShell.exe";
    constexpr wchar_t kUninst[] = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\SekaiPopShell";

    writeRegString(HKEY_LOCAL_MACHINE, kUninst, L"DisplayName", L"SekaiPop Desktop Shell");
    writeRegString(HKEY_LOCAL_MACHINE, kUninst, L"DisplayVersion", L"1.0.0");
    writeRegString(HKEY_LOCAL_MACHINE, kUninst, L"Publisher", L"SekaiPop");
    writeRegString(HKEY_LOCAL_MACHINE, kUninst, L"DisplayIcon", shellExe + L",0");
    writeRegString(HKEY_LOCAL_MACHINE, kUninst, L"InstallLocation", dir);
    writeRegString(HKEY_LOCAL_MACHINE, kUninst, L"UninstallString", L"\"" + mgrExe + L"\" --uninstall");
    writeRegString(HKEY_LOCAL_MACHINE, kUninst, L"QuietUninstallString", L"\"" + mgrExe + L"\" --uninstall-silent");
    writeRegString(HKEY_LOCAL_MACHINE, kUninst, L"ModifyPath", L"\"" + mgrExe + L"\"");
    writeRegDword(HKEY_LOCAL_MACHINE, kUninst, L"EstimatedSize", estimatedSizeKB);
    writeRegDword(HKEY_LOCAL_MACHINE, kUninst, L"NoModify", 0);
    writeRegDword(HKEY_LOCAL_MACHINE, kUninst, L"NoRepair", 1);
}

inline void unregisterUninstallEntry() {
    deleteRegTree(HKEY_LOCAL_MACHINE,
                  L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\SekaiPopShell");
}

// Launch an executable with the standard (unelevated) desktop user token when explorer.exe is running,
// falling back to direct CreateProcessW if no shell window is active.
inline bool launchAsDesktopUser(const std::wstring& exePath, const std::wstring& args, const std::wstring& workDir) {
    bool launched = false;
    HWND shellWnd = GetShellWindow();
    if (shellWnd) {
        DWORD shellPid = 0;
        GetWindowThreadProcessId(shellWnd, &shellPid);
        if (shellPid != 0) {
            HANDLE hShellProc = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, shellPid);
            if (hShellProc) {
                HANDLE hShellTok = nullptr;
                if (OpenProcessToken(hShellProc, TOKEN_DUPLICATE, &hShellTok) && hShellTok) {
                    HANDLE hPrimaryTok = nullptr;
                    if (DuplicateTokenEx(hShellTok,
                                         TOKEN_QUERY | TOKEN_ASSIGN_PRIMARY | TOKEN_DUPLICATE | TOKEN_ADJUST_DEFAULT | TOKEN_ADJUST_SESSIONID,
                                         nullptr, SecurityImpersonation, TokenPrimary, &hPrimaryTok) && hPrimaryTok) {
                        std::wstring cmd = L"\"" + exePath + L"\"";
                        if (!args.empty()) {
                            cmd += L" " + args;
                        }
                        STARTUPINFOW si = {};
                        si.cb = sizeof(si);
                        PROCESS_INFORMATION pi = {};
                        if (CreateProcessWithTokenW(hPrimaryTok, LOGON_WITH_PROFILE, exePath.c_str(), cmd.data(),
                                                    CREATE_DEFAULT_ERROR_MODE, nullptr,
                                                    workDir.empty() ? nullptr : workDir.c_str(), &si, &pi)) {
                            CloseHandle(pi.hThread);
                            CloseHandle(pi.hProcess);
                            launched = true;
                        }
                        CloseHandle(hPrimaryTok);
                    }
                    CloseHandle(hShellTok);
                }
                CloseHandle(hShellProc);
            }
        }
    }
    if (!launched) {
        std::wstring cmd = L"\"" + exePath + L"\"";
        if (!args.empty()) {
            cmd += L" " + args;
        }
        STARTUPINFOW si = {};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi = {};
        if (CreateProcessW(exePath.c_str(), cmd.data(), nullptr, nullptr, FALSE,
                           CREATE_DEFAULT_ERROR_MODE | NORMAL_PRIORITY_CLASS,
                           nullptr, workDir.empty() ? nullptr : workDir.c_str(), &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            launched = true;
        }
    }
    return launched;
}

// Immediately transition the live session into pure Native Shell mode (zero explorer.exe):
// 1. Captures the unelevated desktop user token from explorer.exe BEFORE closing it.
// 2. Stops any old SekaiPop processes and terminates desktop explorer.exe (won't respawn since AutoRestartShell=0).
// 3. Launches SekaiPopBoot.exe --no-startup-apps with the captured normal desktop user token.
inline bool switchLiveToNativeShell(const std::wstring& installDirRaw) {
    const std::wstring dir = stripTrailingSlash(installDirRaw);
    const std::wstring bootExe = dir + L"\\SekaiPopBoot.exe";

    HANDLE hPrimaryTok = nullptr;
    HWND shellWnd = GetShellWindow();
    if (shellWnd) {
        DWORD shellPid = 0;
        GetWindowThreadProcessId(shellWnd, &shellPid);
        if (shellPid != 0) {
            HANDLE hShellProc = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, shellPid);
            if (hShellProc) {
                HANDLE hShellTok = nullptr;
                if (OpenProcessToken(hShellProc, TOKEN_DUPLICATE, &hShellTok) && hShellTok) {
                    DuplicateTokenEx(hShellTok,
                                     TOKEN_QUERY | TOKEN_ASSIGN_PRIMARY | TOKEN_DUPLICATE | TOKEN_ADJUST_DEFAULT | TOKEN_ADJUST_SESSIONID,
                                     nullptr, SecurityImpersonation, TokenPrimary, &hPrimaryTok);
                    CloseHandle(hShellTok);
                }
                CloseHandle(hShellProc);
            }
        }
    }

    stopSekaiPopProcesses();
    terminateProcessesByName(L"explorer.exe");
    Sleep(250);

    bool launched = false;
    std::wstring cmd = L"\"" + bootExe + L"\" --no-startup-apps";
    if (hPrimaryTok) {
        STARTUPINFOW si = {};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi = {};
        if (CreateProcessWithTokenW(hPrimaryTok, LOGON_WITH_PROFILE, bootExe.c_str(), cmd.data(),
                                    CREATE_DEFAULT_ERROR_MODE, nullptr, dir.c_str(), &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            launched = true;
        }
        CloseHandle(hPrimaryTok);
    }
    if (!launched) {
        STARTUPINFOW si = {};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi = {};
        if (CreateProcessW(bootExe.c_str(), cmd.data(), nullptr, nullptr, FALSE,
                           CREATE_DEFAULT_ERROR_MODE | NORMAL_PRIORITY_CLASS,
                           nullptr, dir.c_str(), &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            launched = true;
        }
    }
    return launched;
}

} // namespace SekaiPop::Installer
