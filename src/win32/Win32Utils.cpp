#include "Win32Utils.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <chrono>
#include <cwchar>
#include <dwmapi.h>
#include <mutex>
#include <propvarutil.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <thread>
#include <tlhelp32.h>
#include <vector>

#ifndef DWM_CLOAKED_APP
#define DWM_CLOAKED_APP 0x00000001
#endif

namespace SekaiPop::Win32 {

ComInitGuard::ComInitGuard(DWORD coInit) {
    const HRESULT hr = CoInitializeEx(nullptr, coInit);
    if (SUCCEEDED(hr)) {
        m_shouldUninit = true;
    }
}

ComInitGuard::~ComInitGuard() {
    if (m_shouldUninit) {
        CoUninitialize();
    }
}

std::wstring toStdWString(const QString& s) {
    return std::wstring(reinterpret_cast<const wchar_t*>(s.utf16()), static_cast<size_t>(s.size()));
}

bool isExplorerShellPresent() {
    return FindWindowW(L"Shell_TrayWnd", nullptr) != nullptr;
}

bool isWindowsTaskbarVisible() {
    struct Ctx {
        bool anyVisible = false;
    } ctx;

    EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
        auto* c = reinterpret_cast<Ctx*>(lp);
        wchar_t cls[64] = {};
        const int len = GetClassNameW(hwnd, cls, 64);
        if (len > 0) {
            if (wcscmp(cls, L"Shell_TrayWnd") == 0 ||
                wcscmp(cls, L"Shell_SecondaryTrayWnd") == 0) {
                if (IsWindowVisible(hwnd) != FALSE) {
                    c->anyVisible = true;
                    return FALSE;
                }
            }
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));
    return ctx.anyVisible;
}

void setWindowsTaskbarsVisible(bool visible) {
    struct Ctx {
        bool show;
        int count = 0;
    } ctx{ visible, 0 };

    EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
        auto* c = reinterpret_cast<Ctx*>(lp);
        wchar_t cls[64] = {};
        const int len = GetClassNameW(hwnd, cls, 64);
        if (len > 0) {
            if (wcscmp(cls, L"Shell_TrayWnd") == 0 ||
                wcscmp(cls, L"Shell_SecondaryTrayWnd") == 0) {
                ShowWindow(hwnd, c->show ? SW_SHOWNA : SW_HIDE);
                ++c->count;
            }
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));

    if (ctx.count > 0) {
        qInfo("[Win32Utils] setWindowsTaskbarsVisible(%d) applied to %d taskbar window(s)",
              visible ? 1 : 0, ctx.count);
    }
}

bool launchExplorer() {
    wchar_t winDir[MAX_PATH] = {};
    const UINT len = GetWindowsDirectoryW(winDir, MAX_PATH);
    QString explorerPath = QStringLiteral("C:\\Windows\\explorer.exe");
    if (len > 0 && len < MAX_PATH) {
        explorerPath = QDir(QString::fromWCharArray(winDir, static_cast<int>(len))).filePath(QStringLiteral("explorer.exe"));
    }
    const std::wstring wExe = toStdWString(QDir::toNativeSeparators(explorerPath));
    std::vector<wchar_t> cmdBuf(wExe.begin(), wExe.end());
    cmdBuf.push_back(L'\0');

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    if (CreateProcessW(wExe.c_str(), cmdBuf.data(), nullptr, nullptr, FALSE,
                       DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return true;
    }
    const HINSTANCE res = ShellExecuteW(nullptr, L"open", wExe.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<intptr_t>(res) > 32;
}

bool focusWindowForce(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) {
        return false;
    }
    AllowSetForegroundWindow(ASFW_ANY);

    HWND targetHwnd = hwnd;
    HWND popup = GetLastActivePopup(hwnd);
    if (popup && IsWindow(popup) && IsWindowVisible(popup)) {
        targetHwnd = popup;
    }

    HWND fg = GetForegroundWindow();
    const DWORD curTid = GetCurrentThreadId();
    const DWORD fgTid = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
    const DWORD targetTid = GetWindowThreadProcessId(hwnd, nullptr);

    bool attachedFg = false;
    bool attachedTarget = false;
    if (fgTid != 0 && fgTid != curTid) {
        attachedFg = AttachThreadInput(curTid, fgTid, TRUE) != FALSE;
    }
    if (targetTid != 0 && targetTid != curTid && targetTid != fgTid) {
        attachedTarget = AttachThreadInput(curTid, targetTid, TRUE) != FALSE;
    }

    // Unlock Windows foreground lock when called from a WS_EX_NOACTIVATE panel (like the Dock)
    keybd_event(VK_MENU, 0, KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP, 0);

    if (IsIconic(hwnd)) {
        ShowWindow(hwnd, SW_RESTORE);
        if (IsIconic(hwnd)) {
            ShowWindowAsync(hwnd, SW_RESTORE);
        }
        if (IsIconic(hwnd)) {
            OpenIcon(hwnd);
        }
    }
    if (targetHwnd != hwnd && IsIconic(targetHwnd)) {
        ShowWindow(targetHwnd, SW_RESTORE);
    }

    BringWindowToTop(hwnd);
    if (targetHwnd != hwnd) {
        BringWindowToTop(targetHwnd);
    }
    SetForegroundWindow(targetHwnd);
    SetActiveWindow(targetHwnd);

    if (attachedTarget) {
        AttachThreadInput(curTid, targetTid, FALSE);
    }
    if (attachedFg) {
        AttachThreadInput(curTid, fgTid, FALSE);
    }

    if (IsIconic(hwnd) || (GetForegroundWindow() != hwnd && GetForegroundWindow() != targetHwnd)) {
        SwitchToThisWindow(targetHwnd, TRUE);
    }

    // Windows UIPI blocks unelevated processes from restoring/focusing elevated High-Integrity
    // system windows such as Task Manager (Taskmgr.exe). Re-invoking Taskmgr.exe via ShellExecute
    // silently auto-elevates (no UAC prompt) and tells the existing Taskmgr instance to restore
    // and activate its own window at High Integrity.
    if (IsIconic(hwnd) || (GetForegroundWindow() != hwnd && GetForegroundWindow() != targetHwnd)) {
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid != 0) {
            HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (hProc) {
                wchar_t exeBuf[MAX_PATH * 2] = {};
                DWORD exeLen = static_cast<DWORD>(std::size(exeBuf));
                QString exePath;
                if (QueryFullProcessImageNameW(hProc, 0, exeBuf, &exeLen) && exeLen > 0) {
                    exePath = QString::fromWCharArray(exeBuf, static_cast<int>(exeLen));
                }
                CloseHandle(hProc);

                const QString exeName = QFileInfo(exePath).fileName().toLower();
                if (exeName == QStringLiteral("taskmgr.exe")) {
                    static HWND s_lastElevHwnd = nullptr;
                    static ULONGLONG s_lastElevTick = 0;
                    const ULONGLONG now = GetTickCount64();
                    if (s_lastElevHwnd != hwnd || (now - s_lastElevTick) > 450) {
                        s_lastElevHwnd = hwnd;
                        s_lastElevTick = now;
                        launchShellTarget(exePath, QString(), QString(), SW_SHOWNORMAL);
                    }
                    return true;
                }
            }
        }
    }

    return GetForegroundWindow() == hwnd || GetForegroundWindow() == targetHwnd;
}

namespace {

struct PackageAppInfo {
    QString installDir;
    QString fullExePath;
};

QString queryExePathForPidLocal(DWORD pid) {
    if (pid == 0) {
        return {};
    }
    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc) {
        return {};
    }
    wchar_t buf[MAX_PATH * 2] = {};
    DWORD len = static_cast<DWORD>(std::size(buf));
    QString out;
    if (QueryFullProcessImageNameW(hProc, 0, buf, &len) && len > 0) {
        out = QDir::toNativeSeparators(QString::fromWCharArray(buf, static_cast<int>(len)));
    }
    CloseHandle(hProc);
    return out;
}

bool isProcessAliveLocal(DWORD pid) {
    if (pid == 0) {
        return false;
    }
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!h) {
        return false;
    }
    const DWORD waitRes = WaitForSingleObject(h, 0);
    CloseHandle(h);
    return waitRes == WAIT_TIMEOUT;
}

QSet<DWORD> findPidsForExePath(const QString& fullExePath) {
    QSet<DWORD> pids;
    if (fullExePath.isEmpty()) {
        return pids;
    }
    const QString targetLow = QDir::toNativeSeparators(fullExePath).toLower();
    const QString targetFileLow = QFileInfo(targetLow).fileName();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        return pids;
    }
    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            const QString exeNameLow = QString::fromWCharArray(pe.szExeFile).toLower();
            if (exeNameLow == targetFileLow) {
                const QString full = queryExePathForPidLocal(pe.th32ProcessID).toLower();
                if (full == targetLow) {
                    pids.insert(pe.th32ProcessID);
                }
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pids;
}

// Dynamically resolved Windows AppModel APIs (avoid hard link dependency on kernel32 AppModel exports).
PackageAppInfo resolvePackageAppInfo(const QString& aumid) {
    PackageAppInfo info;
    const int bangIdx = aumid.indexOf(QLatin1Char('!'));
    const QString familyName = (bangIdx > 0) ? aumid.left(bangIdx).trimmed() : QString();
    const QString appId = (bangIdx > 0) ? aumid.mid(bangIdx + 1).trimmed() : QString();
    if (familyName.isEmpty()) {
        return info;
    }

    HMODULE hKernel = GetModuleHandleW(L"kernel32.dll");
    if (!hKernel) {
        return info;
    }
    using GetPackagesByFamilyFn = LONG(WINAPI*)(PCWSTR, UINT32*, PWSTR*, UINT32*, WCHAR*);
    using GetPackagePathFn = LONG(WINAPI*)(PCWSTR, UINT32*, PWSTR);
    auto pGetPackages = reinterpret_cast<GetPackagesByFamilyFn>(GetProcAddress(hKernel, "GetPackagesByPackageFamily"));
    auto pGetPath = reinterpret_cast<GetPackagePathFn>(GetProcAddress(hKernel, "GetPackagePathByFullName"));
    if (!pGetPackages || !pGetPath) {
        return info;
    }

    const std::wstring wFamily = toStdWString(familyName);
    UINT32 count = 0;
    UINT32 bufLen = 0;
    LONG rc = pGetPackages(wFamily.c_str(), &count, nullptr, &bufLen, nullptr);
    if (rc != ERROR_INSUFFICIENT_BUFFER || count == 0 || bufLen == 0) {
        return info;
    }
    std::vector<PWSTR> pkgNames(count);
    std::vector<WCHAR> nameBuf(bufLen);
    rc = pGetPackages(wFamily.c_str(), &count, pkgNames.data(), &bufLen, nameBuf.data());
    if (rc != ERROR_SUCCESS || count == 0 || !pkgNames[0]) {
        return info;
    }

    UINT32 pathLen = 0;
    rc = pGetPath(pkgNames[0], &pathLen, nullptr);
    if (rc != ERROR_INSUFFICIENT_BUFFER || pathLen == 0) {
        return info;
    }
    std::vector<WCHAR> pathBuf(pathLen);
    rc = pGetPath(pkgNames[0], &pathLen, pathBuf.data());
    if (rc != ERROR_SUCCESS) {
        return info;
    }

    info.installDir = QDir::toNativeSeparators(QString::fromWCharArray(pathBuf.data()));
    QFile manifestFile(QDir(info.installDir).filePath(QStringLiteral("AppxManifest.xml")));
    if (!manifestFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return info;
    }
    const QString xml = QString::fromUtf8(manifestFile.readAll());
    static const QRegularExpression appTagRe(QStringLiteral("<Application\\b([^>]*)>"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression idAttrRe(QStringLiteral("\\bId\\s*=\\s*\"([^\"]+)\""), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression exeAttrRe(QStringLiteral("\\bExecutable\\s*=\\s*\"([^\"]+)\""), QRegularExpression::CaseInsensitiveOption);

    QString chosenExe;
    auto it = appTagRe.globalMatch(xml);
    while (it.hasNext()) {
        const QString attrs = it.next().captured(1);
        const QString idVal = idAttrRe.match(attrs).captured(1);
        const QString exeVal = exeAttrRe.match(attrs).captured(1);
        if (exeVal.isEmpty()) {
            continue;
        }
        if (chosenExe.isEmpty()) {
            chosenExe = exeVal;
        }
        if (!appId.isEmpty() && idVal.compare(appId, Qt::CaseInsensitive) == 0) {
            chosenExe = exeVal;
            break;
        }
    }
    if (!chosenExe.isEmpty()) {
        const QString fullExe = QDir::toNativeSeparators(QDir(info.installDir).filePath(chosenExe));
        if (QFileInfo::exists(fullExe)) {
            info.fullExePath = fullExe;
        }
    }
    return info;
}

HWND findPackageTopWindow(
    DWORD activatedPid,
    const QString& installDirLow,
    const QString& exePathLow,
    bool findHiddenOnly
) {
    struct Ctx {
        DWORD ownPid;
        DWORD activatedPid;
        QString installDirPrefix;
        QString exePathLow;
        bool findHiddenOnly;
        HWND bestHwnd = nullptr;
        int bestScore = 0;
    } ctx{
        GetCurrentProcessId(),
        activatedPid,
        installDirLow.isEmpty() ? QString() : (installDirLow.endsWith(QLatin1Char('\\')) ? installDirLow : (installDirLow + QLatin1Char('\\'))),
        exePathLow,
        findHiddenOnly
    };

    EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
        auto* c = reinterpret_cast<Ctx*>(lp);
        if (!hwnd || !IsWindow(hwnd)) {
            return TRUE;
        }
        const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
        const LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        if ((style & WS_CHILD) != 0) {
            return TRUE;
        }
        const bool hasAppWindow = (exStyle & WS_EX_APPWINDOW) != 0;
        if ((exStyle & WS_EX_TOOLWINDOW) != 0 && !hasAppWindow) {
            return TRUE;
        }

        const bool vis = IsWindowVisible(hwnd) != FALSE;
        if (c->findHiddenOnly) {
            if (vis) {
                return TRUE;
            }
            if ((exStyle & WS_EX_NOACTIVATE) != 0 && !hasAppWindow) {
                return TRUE;
            }
        } else {
            if (!vis) {
                return TRUE;
            }
        }

        wchar_t clsBuf[128] = {};
        const int clsLen = GetClassNameW(hwnd, clsBuf, static_cast<int>(std::size(clsBuf)));
        const QString cls = (clsLen > 0) ? QString::fromWCharArray(clsBuf, clsLen) : QString();
        if (cls == QStringLiteral("IME") ||
            cls == QStringLiteral("MSCTFIME UI") ||
            cls == QStringLiteral("Progman") ||
            cls == QStringLiteral("WorkerW") ||
            cls == QStringLiteral("Shell_TrayWnd") ||
            cls == QStringLiteral("Shell_SecondaryTrayWnd") ||
            cls == QStringLiteral("Windows.UI.Core.CoreWindow") ||
            cls == QStringLiteral("DummyDWMListenerWindow") ||
            cls == QStringLiteral("ForegroundStaging")) {
            return TRUE;
        }

        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid == 0 || pid == c->ownPid) {
            return TRUE;
        }

        DWORD effPid = pid;
        if (cls == QStringLiteral("ApplicationFrameWindow")) {
            struct ChildCtx {
                DWORD framePid;
                DWORD appPid;
            } childCtx{ pid, 0 };
            EnumChildWindows(hwnd, [](HWND ch, LPARAM clp) -> BOOL {
                auto* cc = reinterpret_cast<ChildCtx*>(clp);
                DWORD cpid = 0;
                GetWindowThreadProcessId(ch, &cpid);
                if (cpid != 0 && cpid != cc->framePid) {
                    cc->appPid = cpid;
                    return FALSE;
                }
                return TRUE;
            }, reinterpret_cast<LPARAM>(&childCtx));
            if (childCtx.appPid != 0) {
                effPid = childCtx.appPid;
            }
        }

        bool matched = (c->activatedPid != 0 && effPid == c->activatedPid);
        if (!matched && (!c->exePathLow.isEmpty() || !c->installDirPrefix.isEmpty())) {
            const QString exeLow = queryExePathForPidLocal(effPid).toLower();
            if (!exeLow.isEmpty()) {
                if (!c->exePathLow.isEmpty() && exeLow == c->exePathLow) {
                    matched = true;
                } else if (!c->installDirPrefix.isEmpty() && exeLow.startsWith(c->installDirPrefix)) {
                    matched = true;
                }
            }
        }
        if (!matched) {
            return TRUE;
        }

        const int titleLen = GetWindowTextLengthW(hwnd);
        const bool isKnownUiHost =
            cls == QStringLiteral("WinUIDesktopWin32WindowClass") ||
            cls == QStringLiteral("ApplicationFrameWindow") ||
            cls == QStringLiteral("CASCADIA_HOSTING_WINDOW_CLASS");

        if (c->findHiddenOnly) {
            // Only match genuine hidden main app windows (e.g. tray-minimized WhatsApp WinUI3 window),
            // never hidden COM/tray helper message windows.
            const bool hasCaption = ((style & WS_CAPTION) == WS_CAPTION) || ((style & WS_THICKFRAME) != 0);
            if (titleLen <= 0 || (!isKnownUiHost && !hasCaption)) {
                return TRUE;
            }
        } else {
            const bool iconic = IsIconic(hwnd) != FALSE;
            if (!iconic) {
                DWORD cloaked = 0;
                if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) &&
                    (cloaked & DWM_CLOAKED_APP) != 0) {
                    return TRUE;
                }
                RECT rc = {};
                if (!GetWindowRect(hwnd, &rc) || (rc.right - rc.left) <= 0 || (rc.bottom - rc.top) <= 0) {
                    return TRUE;
                }
            }
            if (titleLen <= 0 && !isKnownUiHost) {
                return TRUE;
            }
        }

        int score = 10;
        if (titleLen > 0) score += 20;
        if (isKnownUiHost) score += 30;
        if (c->activatedPid != 0 && effPid == c->activatedPid) score += 10;
        if (score > c->bestScore) {
            c->bestScore = score;
            c->bestHwnd = hwnd;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));

    return ctx.bestHwnd;
}

bool tryLaunchPackageExecutable(const PackageAppInfo& pkg, const QString& args, int showCmd) {
    if (pkg.fullExePath.isEmpty() || !QFileInfo::exists(pkg.fullExePath)) {
        return false;
    }

    std::wstring wExe = toStdWString(pkg.fullExePath);
    std::wstring wCmd = L"\"" + wExe + L"\"";
    if (!args.trimmed().isEmpty()) {
        wCmd += L" " + toStdWString(args.trimmed());
    }
    std::wstring wWorkDir = toStdWString(pkg.installDir);
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = static_cast<WORD>(showCmd);
    PROCESS_INFORMATION pi = {};
    if (CreateProcessW(wExe.c_str(), wCmd.data(), nullptr, nullptr, FALSE,
                       CREATE_DEFAULT_ERROR_MODE | NORMAL_PRIORITY_CLASS,
                       nullptr, wWorkDir.empty() ? nullptr : wWorkDir.c_str(), &si, &pi)) {
        AllowSetForegroundWindow(pi.dwProcessId);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        qInfo("[Win32Utils] Launched packaged executable fallback: %s", qPrintable(pkg.fullExePath));
        return true;
    }
    return false;
}

bool tryLaunchAppsFolderDirect(const QString& appsFolderId, const QString& extraArgs, int showCmd) {
    const QString id = appsFolderId.trimmed();
    if (id.isEmpty()) {
        return false;
    }

    // Special-case Windows File Explorer AUMID: ActivateApplication fails with E_INVALIDARG (0x80070057)
    // when explorer.exe shell is not running, so open a standalone File Explorer window via /separate.
    if (id.compare(QStringLiteral("Microsoft.Windows.Explorer"), Qt::CaseInsensitive) == 0) {
        AllowSetForegroundWindow(ASFW_ANY);
        const HINSTANCE res = ShellExecuteW(
            nullptr, L"open", L"explorer.exe",
            L"/separate,::{20D04FE0-3AEA-1069-A2D8-08002B30309D}",
            nullptr, showCmd
        );
        if (reinterpret_cast<intptr_t>(res) > 32) {
            qInfo("[Win32Utils] Opened standalone File Explorer for %s", qPrintable(id));
            return true;
        }
        return false;
    }

    // Query AppsFolder item's PKEY_Link_TargetParsingPath so Win32 apps registered in shell:AppsFolder
    // (Brave, Discord, NVIDIA App, local .exe/.lnk/.chm, CLSID folders, etc.) launch directly without
    // depending on explorer.exe's shell broker.
    IShellItem2* item2 = nullptr;
    const std::wstring wParsing = L"shell:AppsFolder\\" + toStdWString(id);
    if (FAILED(SHCreateItemFromParsingName(wParsing.c_str(), nullptr, IID_PPV_ARGS(&item2))) || !item2) {
        return false;
    }

    // PKEY_Link_TargetParsingPath = {B9B4B3FC-2B51-4A42-B5D8-324146AFCF25}, 2
    PROPERTYKEY pkTarget = {};
    pkTarget.fmtid = { 0xB9B4B3FC, 0x2B51, 0x4A42, { 0xB5, 0xD8, 0x32, 0x41, 0x46, 0xAF, 0xCF, 0x25 } };
    pkTarget.pid = 2;

    // PKEY_Link_Arguments = {436F2667-14E2-4FEB-B30A-146C53B5B674}, 100
    PROPERTYKEY pkArgs = {};
    pkArgs.fmtid = { 0x436F2667, 0x14E2, 0x4FEB, { 0xB3, 0x0A, 0x14, 0x6C, 0x53, 0xB5, 0xB6, 0x74 } };
    pkArgs.pid = 100;

    QString targetPath;
    PWSTR rawTarget = nullptr;
    if (SUCCEEDED(item2->GetString(pkTarget, &rawTarget)) && rawTarget) {
        targetPath = QString::fromWCharArray(rawTarget).trimmed();
        CoTaskMemFree(rawTarget);
    }

    QString linkArgs;
    PWSTR rawArgs = nullptr;
    if (SUCCEEDED(item2->GetString(pkArgs, &rawArgs)) && rawArgs) {
        linkArgs = QString::fromWCharArray(rawArgs).trimmed();
        CoTaskMemFree(rawArgs);
    }
    item2->Release();

    if (targetPath.isEmpty()) {
        return false; // Packaged MSIX/UWP app (handled by IApplicationActivationManager)
    }

    AllowSetForegroundWindow(ASFW_ANY);
    if (targetPath.startsWith(QStringLiteral("::"))) {
        const std::wstring expArg = L"/separate," + toStdWString(targetPath);
        const HINSTANCE res = ShellExecuteW(nullptr, L"open", L"explorer.exe", expArg.c_str(), nullptr, showCmd);
        if (reinterpret_cast<intptr_t>(res) > 32) {
            qInfo("[Win32Utils] Opened shell folder %s via explorer /separate for %s",
                  qPrintable(targetPath), qPrintable(id));
            return true;
        }
        return false;
    }

    QString combinedArgs = linkArgs;
    if (!extraArgs.trimmed().isEmpty()) {
        if (!combinedArgs.isEmpty()) combinedArgs += QLatin1Char(' ');
        combinedArgs += extraArgs.trimmed();
    }

    const std::wstring wTarget = toStdWString(QDir::toNativeSeparators(targetPath));
    const std::wstring wArgs = toStdWString(combinedArgs);
    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_FLAG_NO_UI;
    sei.lpFile = wTarget.c_str();
    sei.lpParameters = wArgs.empty() ? nullptr : wArgs.c_str();
    sei.nShow = showCmd;
    if (ShellExecuteExW(&sei)) {
        qInfo("[Win32Utils] Launched AppsFolder Win32 target %s for %s",
              qPrintable(targetPath), qPrintable(id));
        return true;
    }
    if (GetLastError() == ERROR_ELEVATION_REQUIRED) {
        sei.fMask = 0;
        sei.lpVerb = L"runas";
        if (ShellExecuteExW(&sei)) {
            return true;
        }
    }
    return false;
}

bool launchUwpByAumid(const QString& aumid, const QString& args, int showCmd) {
    const QString trimmed = aumid.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }

    // Coalesce rapid double-clicks on the same UWP/MSIX app so two concurrent threads
    // don't race ActivateApplication or headless-recovery against each other.
    {
        static std::mutex s_launchMu;
        static QHash<QString, ULONGLONG> s_lastLaunchTick;
        const QString key = trimmed.toLower();
        const ULONGLONG now = GetTickCount64();
        std::lock_guard<std::mutex> lock(s_launchMu);
        const ULONGLONG prev = s_lastLaunchTick.value(key, 0);
        if (prev != 0 && (now - prev) < 700) {
            return true;
        }
        s_lastLaunchTick.insert(key, now);
    }

    const std::wstring wAumid = toStdWString(trimmed);
    const std::wstring wArgs = toStdWString(args);
    const PackageAppInfo pkg = resolvePackageAppInfo(trimmed);
    const QString installDirLow = pkg.installDir.toLower();
    const QString exePathLow = pkg.fullExePath.toLower();
    QSet<DWORD> existingPids = findPidsForExePath(pkg.fullExePath);
    const bool hadExistingInstance = !existingPids.isEmpty();

    auto activateOnce = [&wAumid, &wArgs, &trimmed](DWORD& outPid) -> bool {
        outPid = 0;
        // Transfer foreground permission BEFORE activation so both fresh launches and
        // single-instance redirects (e.g., WhatsApp Desktop restoring from tray) can take focus.
        AllowSetForegroundWindow(ASFW_ANY);

        IApplicationActivationManager* aam = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_ApplicationActivationManager, nullptr,
                                      CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(&aam));
        if (FAILED(hr) || !aam) {
            hr = CoCreateInstance(CLSID_ApplicationActivationManager, nullptr,
                                  CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&aam));
        }
        if (FAILED(hr) || !aam) {
            return false;
        }
        hr = aam->ActivateApplication(wAumid.c_str(),
                                      wArgs.empty() ? nullptr : wArgs.c_str(),
                                      AO_NOERRORUI,
                                      &outPid);
        aam->Release();
        if (SUCCEEDED(hr)) {
            if (outPid != 0) {
                AllowSetForegroundWindow(outPid);
            }
            qInfo("[Win32Utils] Activated UWP/MSIX app %s (pid=%lu)",
                  qPrintable(trimmed), static_cast<unsigned long>(outPid));
            return true;
        }
        qWarning("[Win32Utils] ActivateApplication(%s) returned hr=0x%08lx (pid=%lu); trying fallback",
                 qPrintable(trimmed), static_cast<unsigned long>(hr), static_cast<unsigned long>(outPid));
        return false;
    };

    // Primary path: IApplicationActivationManager works natively in Full Native Shell mode
    // (without explorer.exe) for MSIX/Desktop Bridge apps like WhatsApp Desktop, Terminal, Store, etc.,
    // whereas ShellExecuteW("shell:AppsFolder\\...") fails with REGDB_E_CLASSNOTREG when explorer is absent.
    DWORD pid = 0;
    if (!activateOnce(pid)) {
        // Fallback 2: directly launch the packaged FullTrust executable from AppxManifest.xml
        return tryLaunchPackageExecutable(pkg, args, showCmd);
    }

    // Post-activation surface & recovery guard (runs on launchShellTarget's detached background thread):
    // 1. Brings the app's window to the foreground (fixes silent background launch behind maximized windows
    //    when launched from a WS_EX_NOACTIVATE Dock).
    // 2. Unhides tray-minimized WinUI3/MSIX windows (e.g. WhatsApp closed to tray when Shell_TrayWnd is absent)
    //    if single-instance redirection left the window hidden — while giving cold-starting apps plenty of time
    //    (3.2s) to finish initializing their own frame cleanly before intervening.
    // 3. Auto-recovers orphaned headless single-instance processes (e.g. background push-task or crashed UI
    //    holding the single-instance lock with 0 windows on the user's desktop) by terminating ONLY the
    //    headless pre-existing instance and re-activating cleanly once.
    bool retriedHeadless = false;
    int elapsedMs = 0;
    int maxWaitMs = 5500;
    while (elapsedMs < maxWaitMs) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        elapsedMs += 100;

        HWND vis = findPackageTopWindow(pid, installDirLow, exePathLow, /*findHiddenOnly=*/false);
        if (vis) {
            std::this_thread::sleep_for(std::chrono::milliseconds(60));
            if (IsWindow(vis)) {
                if (IsIconic(vis) || GetForegroundWindow() != vis) {
                    focusWindowForce(vis);
                }
                qInfo("[Win32Utils] Surfaced UWP/MSIX window %p for %s", vis, qPrintable(trimmed));
            }
            return true;
        }

        const int hiddenCheckDelayMs = (hadExistingInstance && !retriedHeadless) ? 900 : 4200;
        if (elapsedMs >= hiddenCheckDelayMs) {
            HWND hid = findPackageTopWindow(pid, installDirLow, exePathLow, /*findHiddenOnly=*/true);
            if (hid) {
                ShowWindow(hid, SW_SHOW);
                ShowWindow(hid, SW_RESTORE);
                focusWindowForce(hid);
                qInfo("[Win32Utils] Restored hidden/tray UWP/MSIX window %p for %s", hid, qPrintable(trimmed));
                return true;
            }
        }

        if (!retriedHeadless &&
            elapsedMs >= 2000 &&
            !existingPids.isEmpty() &&
            pid != 0 &&
            !existingPids.contains(pid) &&
            !isProcessAliveLocal(pid)) {
            retriedHeadless = true;
            int terminated = 0;
            for (DWORD oldPid : existingPids) {
                if (isProcessAliveLocal(oldPid)) {
                    HANDLE hProc = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, oldPid);
                    if (hProc) {
                        if (TerminateProcess(hProc, 0)) {
                            WaitForSingleObject(hProc, 1200);
                            ++terminated;
                        }
                        CloseHandle(hProc);
                    }
                }
            }
            if (terminated > 0) {
                qWarning("[Win32Utils] Recovered %d stuck headless instance(s) of %s (redirector pid=%lu exited with no window); re-activating",
                         terminated, qPrintable(trimmed), static_cast<unsigned long>(pid));
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
                existingPids.clear();
                DWORD retryPid = 0;
                if (activateOnce(retryPid)) {
                    pid = retryPid;
                    elapsedMs = 0;
                    maxWaitMs = 5500;
                    continue;
                }
                return tryLaunchPackageExecutable(pkg, args, showCmd);
            }
        }
    }
    return true;
}

QString extractShortcutAumid(const QString& lnkPath) {
    IShellLinkW* psl = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&psl))) || !psl) {
        return {};
    }
    QString aumid;
    IPersistFile* ppf = nullptr;
    if (SUCCEEDED(psl->QueryInterface(IID_PPV_ARGS(&ppf))) && ppf) {
        const std::wstring wLnk = toStdWString(QDir::toNativeSeparators(lnkPath));
        if (SUCCEEDED(ppf->Load(wLnk.c_str(), STGM_READ))) {
            IPropertyStore* pps = nullptr;
            if (SUCCEEDED(psl->QueryInterface(IID_PPV_ARGS(&pps))) && pps) {
                // PKEY_AppUserModel_ID = {9F4C2855-9F79-4B39-A8D0-E1D42DE1D5F3}, 5
                PROPERTYKEY pkey;
                pkey.fmtid = { 0x9F4C2855, 0x9F79, 0x4B39, { 0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3 } };
                pkey.pid = 5;
                PROPVARIANT pv;
                PropVariantInit(&pv);
                if (SUCCEEDED(pps->GetValue(pkey, &pv))) {
                    if (pv.vt == VT_LPWSTR && pv.pwszVal) {
                        aumid = QString::fromWCharArray(pv.pwszVal).trimmed();
                    }
                    PropVariantClear(&pv);
                }
                pps->Release();
            }
        }
        ppf->Release();
    }
    psl->Release();
    return aumid;
}

} // namespace

bool launchShellTarget(const QString& target, const QString& args, const QString& workDir, int showCmd) {
    const QString cleanTarget = target.trimmed();
    if (cleanTarget.isEmpty()) {
        return false;
    }
    // Execute on a short-lived detached STA thread so it NEVER runs a
    // re-entrant Windows Shell COM message loop inside Qt's QML/GUI event stack.
    const std::wstring wArgs = toStdWString(args);
    const std::wstring wDir = toStdWString(workDir);
    std::thread([cleanTarget, args, wArgs, wDir, showCmd]() {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

        // 1. AppsFolder entries ("shell:AppsFolder\<id>"):
        //    - Win32 / CLSID AppsFolder items (e.g. Microsoft.Windows.Explorer, Brave, Discord, local .exe):
        //      resolve via PKEY_Link_TargetParsingPath and launch directly without needing explorer.exe.
        //    - Packaged MSIX / UWP apps (e.g. WhatsApp Desktop, Terminal, Calculator, Store):
        //      activate via IApplicationActivationManager with foreground surfacing and tray/headless recovery.
        static const QString kAppsPrefix = QStringLiteral("shell:appsfolder");
        if (cleanTarget.toLower().startsWith(kAppsPrefix) && cleanTarget.size() > kAppsPrefix.size() + 1) {
            const QChar sep = cleanTarget.at(kAppsPrefix.size());
            if (sep == QLatin1Char('\\') || sep == QLatin1Char('/')) {
                const QString aumid = cleanTarget.mid(kAppsPrefix.size() + 1).trimmed();
                if (tryLaunchAppsFolderDirect(aumid, args, showCmd) ||
                    launchUwpByAumid(aumid, args, showCmd)) {
                    if (SUCCEEDED(hr)) CoUninitialize();
                    return;
                }
                // Only fall through to ShellExecuteExW if explorer.exe shell is present (avoids Class not registered popup)
                if (!isExplorerShellPresent()) {
                    if (SUCCEEDED(hr)) CoUninitialize();
                    return;
                }
            }
        }

        // 2. Other shell: namespace folders (e.g. shell:RecycleBinFolder) without explorer.exe shell:
        // open in a standalone File Explorer window via /separate so it doesn't fail with Class not registered.
        if (cleanTarget.startsWith(QStringLiteral("shell:"), Qt::CaseInsensitive) && !isExplorerShellPresent()) {
            const std::wstring expArg = L"/separate," + toStdWString(cleanTarget);
            ShellExecuteW(nullptr, L"open", L"explorer.exe", expArg.c_str(), nullptr, showCmd);
            if (SUCCEEDED(hr)) CoUninitialize();
            return;
        }

        // 2b. Bare "explorer.exe" launch without explorer.exe shell (e.g. Dock Files button / default pin):
        // always open a standalone File Explorer window via /separate instead of starting the Windows taskbar.
        const QString targetFileLow = QFileInfo(cleanTarget).fileName().toLower();
        if ((cleanTarget.compare(QStringLiteral("explorer"), Qt::CaseInsensitive) == 0 ||
             targetFileLow == QStringLiteral("explorer.exe")) &&
            args.trimmed().isEmpty() &&
            !isExplorerShellPresent()) {
            AllowSetForegroundWindow(ASFW_ANY);
            ShellExecuteW(nullptr, L"open", L"explorer.exe",
                          L"/separate,::{20D04FE0-3AEA-1069-A2D8-08002B30309D}",
                          nullptr, showCmd);
            if (SUCCEEDED(hr)) CoUninitialize();
            return;
        }

        // 3. Standard executables, documents, URLs, and .lnk shortcuts (suppress modal COM error popups)
        const std::wstring wTarget = toStdWString(cleanTarget);
        SHELLEXECUTEINFOW sei = {};
        sei.cbSize = sizeof(sei);
        sei.fMask = SEE_MASK_FLAG_NO_UI;
        sei.lpVerb = nullptr;
        sei.lpFile = wTarget.c_str();
        sei.lpParameters = wArgs.empty() ? nullptr : wArgs.c_str();
        sei.lpDirectory = wDir.empty() ? nullptr : wDir.c_str();
        sei.nShow = showCmd;
        if (!ShellExecuteExW(&sei)) {
            const DWORD err = GetLastError();
            if (err == ERROR_ELEVATION_REQUIRED) {
                sei.fMask = 0;
                sei.lpVerb = L"runas";
                ShellExecuteExW(&sei);
            } else if (cleanTarget.endsWith(QStringLiteral(".lnk"), Qt::CaseInsensitive)) {
                // Packaged-app Start Menu .lnk shortcuts carry an AppUserModelId and no direct .exe target
                const QString aumid = extractShortcutAumid(cleanTarget);
                if (!aumid.isEmpty() && launchUwpByAumid(aumid, args, showCmd)) {
                    if (SUCCEEDED(hr)) CoUninitialize();
                    return;
                }
                qWarning("[Win32Utils] ShellExecuteExW failed for shortcut %s (err=%lu)",
                         qPrintable(cleanTarget), static_cast<unsigned long>(err));
            } else if (cleanTarget.startsWith(QStringLiteral("ms-settings:"), Qt::CaseInsensitive)) {
                // Fallback for Windows Settings URI when explorer shell broker is absent
                launchUwpByAumid(
                    QStringLiteral("windows.immersivecontrolpanel_cw5n1h2txyewy!microsoft.windows.immersivecontrolpanel"),
                    cleanTarget, showCmd);
            } else {
                sei.fMask = SEE_MASK_FLAG_NO_UI;
                sei.lpVerb = L"open";
                if (!ShellExecuteExW(&sei)) {
                    qWarning("[Win32Utils] ShellExecuteExW failed for %s (err=%lu)",
                             qPrintable(cleanTarget), static_cast<unsigned long>(GetLastError()));
                }
            }
        }
        if (SUCCEEDED(hr)) {
            CoUninitialize();
        }
    }).detach();
    return true;
}

bool runWindowsCommand(const QString& command, bool inTerminal) {
    const QString trimmed = command.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }
    if (inTerminal) {
        return launchShellTarget(QStringLiteral("cmd.exe"), QStringLiteral("/k ") + trimmed, {}, SW_SHOWNORMAL);
    }
    return launchShellTarget(QStringLiteral("cmd.exe"), QStringLiteral("/c ") + trimmed, {}, SW_HIDE);
}

QString friendlyAppNameFromExe(const QString& exePath) {
    if (exePath.isEmpty()) {
        return {};
    }
    const std::wstring wPath = toStdWString(exePath);
    DWORD dummy = 0;
    const DWORD size = GetFileVersionInfoSizeW(wPath.c_str(), &dummy);
    if (size > 0) {
        std::vector<BYTE> buf(size);
        if (GetFileVersionInfoW(wPath.c_str(), 0, size, buf.data())) {
            struct LangAndCodePage {
                WORD language;
                WORD codePage;
            };
            LangAndCodePage* translate = nullptr;
            UINT transLen = 0;
            if (VerQueryValueW(buf.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<LPVOID*>(&translate), &transLen) &&
                transLen >= sizeof(LangAndCodePage)) {
                wchar_t subBlock[128] = {};
                swprintf_s(subBlock, L"\\StringFileInfo\\%04x%04x\\FileDescription", translate[0].language, translate[0].codePage);
                wchar_t* desc = nullptr;
                UINT descLen = 0;
                if (VerQueryValueW(buf.data(), subBlock, reinterpret_cast<LPVOID*>(&desc), &descLen) && desc && descLen > 1) {
                    const QString res = QString::fromWCharArray(desc).trimmed();
                    if (!res.isEmpty()) {
                        return res;
                    }
                }
                swprintf_s(subBlock, L"\\StringFileInfo\\%04x%04x\\ProductName", translate[0].language, translate[0].codePage);
                if (VerQueryValueW(buf.data(), subBlock, reinterpret_cast<LPVOID*>(&desc), &descLen) && desc && descLen > 1) {
                    const QString res = QString::fromWCharArray(desc).trimmed();
                    if (!res.isEmpty()) {
                        return res;
                    }
                }
            }
        }
    }
    const QFileInfo fi(exePath);
    QString base = fi.completeBaseName();
    if (!base.isEmpty()) {
        base[0] = base[0].toUpper();
    }
    return base;
}

#ifndef ARW_HIDE
#define ARW_HIDE 0x0008L
#endif

namespace {

using SetTaskmanWindowFn = BOOL(WINAPI*)(HWND);
using GetTaskmanWindowFn = HWND(WINAPI*)();

constexpr wchar_t kTaskmanClassName[] = L"SekaiPopTaskmanHost";

SetTaskmanWindowFn resolveSetTaskmanWindow() {
    static const auto fn = reinterpret_cast<SetTaskmanWindowFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetTaskmanWindow"));
    return fn;
}

GetTaskmanWindowFn resolveGetTaskmanWindow() {
    static const auto fn = reinterpret_cast<GetTaskmanWindowFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetTaskmanWindow"));
    return fn;
}

} // namespace

NativeTaskmanHost::~NativeTaskmanHost() {
    release();
}

void NativeTaskmanHost::ensureActive() {
    if (isExplorerShellPresent()) {
        return;
    }

    const auto pGetTaskman = resolveGetTaskmanWindow();
    const auto pSetTaskman = resolveSetTaskmanWindow();
    if (!pSetTaskman) {
        return;
    }

    const HWND currentTaskman = pGetTaskman ? pGetTaskman() : nullptr;
    if (m_hwnd && IsWindow(m_hwnd) && m_taskmanClaimed && currentTaskman == m_hwnd) {
        // Already registered as active TaskmanWindow; ensure ARW_HIDE stays set and tuck any stray iconic pills.
        MINIMIZEDMETRICS mm = {};
        mm.cbSize = sizeof(mm);
        if (SystemParametersInfoW(SPI_GETMINIMIZEDMETRICS, sizeof(mm), &mm, 0) &&
            (mm.iArrange & ARW_HIDE) == 0) {
            mm.iArrange |= ARW_HIDE;
            SystemParametersInfoW(SPI_SETMINIMIZEDMETRICS, sizeof(mm), &mm, SPIF_SENDCHANGE);
        }
        tuckOnScreenMinimizedWindows();
        return;
    }

    // Never steal TaskmanWindow if another valid shell window already owns it.
    if (currentTaskman && IsWindow(currentTaskman) && currentTaskman != m_hwnd) {
        return;
    }

    if (!m_hwnd || !IsWindow(m_hwnd)) {
        const HINSTANCE hInst = GetModuleHandleW(nullptr);
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = hInst;
        wc.lpszClassName = kTaskmanClassName;
        RegisterClassExW(&wc);

        // Must be a real desktop top-level window (NOT HWND_MESSAGE) so win32k accepts it as spwndTaskman,
        // kept hidden (no WS_VISIBLE) and excluded from Alt+Tab/taskbar via WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE.
        m_hwnd = CreateWindowExW(
            WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            kTaskmanClassName,
            L"",
            WS_POPUP,
            -32000, -32000, 0, 0,
            nullptr, nullptr, hInst, nullptr
        );
        if (!m_hwnd) {
            qWarning("[NativeTaskmanHost] CreateWindowExW failed: err=%lu", GetLastError());
            return;
        }
    }

    m_taskmanClaimed = (pSetTaskman(m_hwnd) != FALSE);
    m_shellHookRegistered = (RegisterShellHookWindow(m_hwnd) != FALSE);

    MINIMIZEDMETRICS mm = {};
    mm.cbSize = sizeof(mm);
    if (SystemParametersInfoW(SPI_GETMINIMIZEDMETRICS, sizeof(mm), &mm, 0)) {
        if ((mm.iArrange & ARW_HIDE) == 0) {
            if (!m_modifiedMinMetrics) {
                m_origMinMetrics = mm;
                m_modifiedMinMetrics = true;
            }
            mm.iArrange |= ARW_HIDE;
            SystemParametersInfoW(SPI_SETMINIMIZEDMETRICS, sizeof(mm), &mm, SPIF_SENDCHANGE);
        }
    }

    qInfo("[NativeTaskmanHost] Registered TaskmanWindow=%p claimed=%d shellHook=%d",
          m_hwnd, m_taskmanClaimed ? 1 : 0, m_shellHookRegistered ? 1 : 0);

    tuckOnScreenMinimizedWindows();
}

void NativeTaskmanHost::tuckOnScreenMinimizedWindows() const {
    struct TuckCtx {
        DWORD ownPid;
        int tucked;
    } ctx{ GetCurrentProcessId(), 0 };

    EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
        auto* c = reinterpret_cast<TuckCtx*>(lp);
        if (!hwnd || !IsIconic(hwnd) || !IsWindowVisible(hwnd)) {
            return TRUE;
        }
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid == 0 || pid == c->ownPid) {
            return TRUE;
        }
        if ((GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CHILD) != 0) {
            return TRUE;
        }
        RECT rc = {};
        if (GetWindowRect(hwnd, &rc)) {
            if (rc.left > -10000 && rc.top > -10000) {
                if (SetWindowPos(hwnd, nullptr, -32000, -32000, 0, 0,
                                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS)) {
                    ++c->tucked;
                }
            }
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));

    if (ctx.tucked > 0) {
        qInfo("[NativeTaskmanHost] Tucked %d on-screen minimized window titlebar(s) off-screen", ctx.tucked);
    }
}

void NativeTaskmanHost::release() {
    if (m_shellHookRegistered && m_hwnd) {
        DeregisterShellHookWindow(m_hwnd);
        m_shellHookRegistered = false;
    }
    if (m_taskmanClaimed && m_hwnd) {
        const auto pGetTaskman = resolveGetTaskmanWindow();
        const auto pSetTaskman = resolveSetTaskmanWindow();
        if (pSetTaskman && (!pGetTaskman || pGetTaskman() == m_hwnd)) {
            pSetTaskman(nullptr);
        }
        m_taskmanClaimed = false;
    }
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    if (m_modifiedMinMetrics) {
        SystemParametersInfoW(SPI_SETMINIMIZEDMETRICS, sizeof(m_origMinMetrics), &m_origMinMetrics, SPIF_SENDCHANGE);
        m_modifiedMinMetrics = false;
    }
}

} // namespace SekaiPop::Win32
