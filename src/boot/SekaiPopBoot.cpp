#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

std::atomic<bool> g_shuttingDown{false};
HANDLE g_childProcess = nullptr;
LARGE_INTEGER g_qpcFreq = {};
LARGE_INTEGER g_bootStartQpc = {};

double elapsedMsSinceBoot() {
    LARGE_INTEGER now = {};
    QueryPerformanceCounter(&now);
    if (g_qpcFreq.QuadPart <= 0) {
        return 0.0;
    }
    return static_cast<double>(now.QuadPart - g_bootStartQpc.QuadPart) * 1000.0 /
           static_cast<double>(g_qpcFreq.QuadPart);
}

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

std::wstring localAppDataDir() {
    wchar_t envBuf[MAX_PATH * 2] = {};
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", envBuf, static_cast<DWORD>(std::size(envBuf)));
    if (n > 0 && n < std::size(envBuf)) {
        return std::wstring(envBuf, n);
    }
    PWSTR known = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &known)) && known) {
        std::wstring out(known);
        CoTaskMemFree(known);
        return out;
    }
    return L".";
}

// ============================================================================
// Instant Pure-Win32 Boot Curtain (< 10ms First Visible Pixel on Windows Logon)
// ============================================================================

struct CurtainManifestEntry {
    int left = 0;
    int top = 0;
    int width = 0;
    int height = 0;
    std::wstring fileName;
};

struct CurtainMonitorWindow {
    HWND hwnd = nullptr;
    RECT rect = {};
    HBITMAP hbmp = nullptr;
    HDC memDc = nullptr;
    int bmpW = 0;
    int bmpH = 0;
    COLORREF bgColor = RGB(24, 24, 37);
    bool isPrimary = false;
};

struct CurtainState {
    std::vector<CurtainMonitorWindow> windows;
    bool active = false;
    bool userDismissed = false;
    ULONGLONG shownTick = 0;
    double primaryPaintMs = 0.0;
    double allPaintMs = 0.0;
};

CurtainState g_curtain;

struct EnumMonItem {
    RECT rect = {};
    bool isPrimary = false;
};

BOOL CALLBACK enumCurtainMonitorsProc(HMONITOR hmon, HDC, LPRECT, LPARAM lParam) {
    auto* list = reinterpret_cast<std::vector<EnumMonItem>*>(lParam);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(hmon, &mi)) {
        list->push_back(EnumMonItem{mi.rcMonitor, (mi.dwFlags & MONITORINFOF_PRIMARY) != 0});
    }
    return TRUE;
}

// Single-syscall sequential ReadFile straight into a top-down 32bpp DIB section (zero page-fault storm,
// zero decoding, native DWM BGRA layout). Safe to execute in parallel across monitors.
bool loadFastBmp32ToDib(const std::wstring& path, HBITMAP& outBmp, int& outW, int& outH) {
    outBmp = nullptr;
    outW = 0;
    outH = 0;

    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        return false;
    }

    BITMAPFILEHEADER bfh = {};
    BITMAPINFOHEADER bih = {};
    DWORD rd = 0;
    if (!ReadFile(hFile, &bfh, sizeof(bfh), &rd, nullptr) || rd != sizeof(bfh) ||
        bfh.bfType != 0x4D42 ||
        !ReadFile(hFile, &bih, sizeof(bih), &rd, nullptr) || rd != sizeof(bih) ||
        bih.biBitCount != 32 || bih.biCompression != BI_RGB ||
        bih.biWidth <= 0 || bih.biWidth > 8192 ||
        bih.biHeight == 0 || std::abs(bih.biHeight) > 8192) {
        CloseHandle(hFile);
        return false;
    }

    const int w = bih.biWidth;
    const int h = std::abs(bih.biHeight);
    const DWORD pixelBytes = static_cast<DWORD>(w) * static_cast<DWORD>(h) * 4U;

    if (bfh.bfOffBits > sizeof(bfh) + sizeof(bih)) {
        SetFilePointer(hFile, static_cast<LONG>(bfh.bfOffBits), nullptr, FILE_BEGIN);
    }

    BITMAPINFO bmi = {};
    bmi.bmiHeader = bih;

    void* dibBits = nullptr;
    HBITMAP hBmp = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &dibBits, nullptr, 0);
    if (!hBmp || !dibBits) {
        if (hBmp) DeleteObject(hBmp);
        CloseHandle(hFile);
        return false;
    }

    DWORD totalRead = 0;
    BYTE* dst = static_cast<BYTE*>(dibBits);
    while (totalRead < pixelBytes) {
        DWORD chunk = 0;
        if (!ReadFile(hFile, dst + totalRead, pixelBytes - totalRead, &chunk, nullptr) || chunk == 0) {
            break;
        }
        totalRead += chunk;
    }
    CloseHandle(hFile);

    if (totalRead != pixelBytes) {
        DeleteObject(hBmp);
        return false;
    }

    outBmp = hBmp;
    outW = w;
    outH = h;
    return true;
}

LRESULT CALLBACK curtainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        if (cs && cs->lpCreateParams) {
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        }
        return TRUE;
    }
    if (msg == WM_ERASEBKGND) {
        return 1;
    }
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps = {};
        HDC hdc = BeginPaint(hwnd, &ps);
        auto* mw = reinterpret_cast<CurtainMonitorWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (mw) {
            const int dstW = mw->rect.right - mw->rect.left;
            const int dstH = mw->rect.bottom - mw->rect.top;
            if (mw->memDc && mw->hbmp && mw->bmpW > 0 && mw->bmpH > 0) {
                if (mw->bmpW == dstW && mw->bmpH == dstH) {
                    BitBlt(hdc, 0, 0, dstW, dstH, mw->memDc, 0, 0, SRCCOPY);
                } else {
                    SetStretchBltMode(hdc, HALFTONE);
                    StretchBlt(hdc, 0, 0, dstW, dstH, mw->memDc, 0, 0, mw->bmpW, mw->bmpH, SRCCOPY);
                }
            } else {
                HBRUSH br = CreateSolidBrush(mw->bgColor);
                RECT rc = {0, 0, dstW, dstH};
                FillRect(hdc, &rc, br);
                DeleteObject(br);
            }
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN || msg == WM_KEYDOWN) {
        g_curtain.userDismissed = true;
        return 0;
    }
    if (msg == WM_MOUSEACTIVATE) {
        return MA_NOACTIVATE;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void destroyBootCurtain() {
    if (!g_curtain.active && g_curtain.windows.empty()) {
        return;
    }
    for (CurtainMonitorWindow& mw : g_curtain.windows) {
        if (mw.hwnd) {
            SetWindowLongPtrW(mw.hwnd, GWLP_USERDATA, 0);
            DestroyWindow(mw.hwnd);
            mw.hwnd = nullptr;
        }
        if (mw.memDc) {
            DeleteDC(mw.memDc);
            mw.memDc = nullptr;
        }
        if (mw.hbmp) {
            DeleteObject(mw.hbmp);
            mw.hbmp = nullptr;
        }
    }
    g_curtain.windows.clear();
    g_curtain.active = false;
    g_curtain.userDismissed = false;
}

void writeBootTimingLog(double primaryPaintMs, double allPaintMs, int monitorCount, int loadedSnapshots) {
    const std::wstring dir = localAppDataDir() + L"\\SekaiPop\\cache\\boot_curtain";
    SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    const std::wstring logPath = dir + L"\\last_boot_ms.txt";
    HANDLE hFile = CreateFileW(logPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        return;
    }
    char buf[256] = {};
    int len = std::snprintf(buf, sizeof(buf),
                            "primary_first_paint_ms=%.2f\nall_monitors_paint_ms=%.2f\nmonitors=%d\nsnapshots=%d\n",
                            primaryPaintMs, allPaintMs, monitorCount, loadedSnapshots);
    if (len > 0) {
        DWORD wr = 0;
        WriteFile(hFile, buf, static_cast<DWORD>(len), &wr, nullptr);
    }
    CloseHandle(hFile);
}

bool showInstantBootCurtain(HINSTANCE hInst) {
    destroyBootCurtain();

    std::vector<EnumMonItem> monitors;
    EnumDisplayMonitors(nullptr, nullptr, &enumCurtainMonitorsProc, reinterpret_cast<LPARAM>(&monitors));
    if (monitors.empty()) {
        return false;
    }
    // Primary monitor FIRST so main display paints in the fewest possible milliseconds
    std::sort(monitors.begin(), monitors.end(), [](const EnumMonItem& a, const EnumMonItem& b) {
        if (a.isPrimary != b.isPrimary) return a.isPrimary > b.isPrimary;
        if (a.rect.left != b.rect.left) return a.rect.left < b.rect.left;
        return a.rect.top < b.rect.top;
    });

    const std::wstring cacheDir = localAppDataDir() + L"\\SekaiPop\\cache\\boot_curtain";
    const std::wstring manifestPath = cacheDir + L"\\manifest.txt";

    COLORREF bgColor = RGB(24, 24, 37);
    std::vector<CurtainManifestEntry> entries;
    {
        HANDLE hMf = CreateFileW(manifestPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (hMf != INVALID_HANDLE_VALUE) {
            char buf[8192] = {};
            DWORD rd = 0;
            if (ReadFile(hMf, buf, sizeof(buf) - 1, &rd, nullptr) && rd > 0) {
                buf[rd] = '\0';
                std::istringstream mf(buf);
                std::string header;
                if (std::getline(mf, header) && header.find("SKPP_CURTAIN_V1") == 0) {
                    int r = 24, g = 24, b = 37;
                    int count = 0;
                    if ((mf >> r >> g >> b) && (mf >> count) && count > 0 && count <= 16) {
                        bgColor = RGB(std::clamp(r, 0, 255), std::clamp(g, 0, 255), std::clamp(b, 0, 255));
                        for (int i = 0; i < count; ++i) {
                            CurtainManifestEntry e;
                            std::string fnameUtf8;
                            if (mf >> e.left >> e.top >> e.width >> e.height >> fnameUtf8) {
                                e.fileName.assign(fnameUtf8.begin(), fnameUtf8.end());
                                entries.push_back(std::move(e));
                            }
                        }
                    }
                }
            }
            CloseHandle(hMf);
        }
    }

    static bool classRegistered = false;
    constexpr const wchar_t* kCurtainClass = L"SekaiPopInstantBootCurtain";
    if (!classRegistered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &curtainWndProc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kCurtainClass;
        if (RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS) {
            classRegistered = true;
        }
    }

    g_curtain.windows.resize(monitors.size());
    std::vector<std::wstring> bmpPaths(monitors.size());
    for (size_t i = 0; i < monitors.size(); ++i) {
        CurtainMonitorWindow& mw = g_curtain.windows[i];
        mw.rect = monitors[i].rect;
        mw.isPrimary = monitors[i].isPrimary;
        mw.bgColor = bgColor;
        const int monW = mw.rect.right - mw.rect.left;
        const int monH = mw.rect.bottom - mw.rect.top;
        const CurtainManifestEntry* match = nullptr;
        for (const CurtainManifestEntry& e : entries) {
            if (e.left == mw.rect.left && e.top == mw.rect.top &&
                e.width == monW && e.height == monH) {
                match = &e;
                break;
            }
        }
        if (!match && i < entries.size()) {
            match = &entries[i];
        }
        if (match && !match->fileName.empty()) {
            bmpPaths[i] = cacheDir + L"\\" + match->fileName;
        }
    }

    // Load secondary monitor BMPs on parallel worker thread(s) while Main Thread loads & paints Primary first!
    std::vector<std::thread> bgLoaders;
    for (size_t i = 1; i < monitors.size(); ++i) {
        if (!bmpPaths[i].empty()) {
            CurtainMonitorWindow* mwPtr = &g_curtain.windows[i];
            std::wstring p = bmpPaths[i];
            bgLoaders.emplace_back([mwPtr, p = std::move(p)]() {
                loadFastBmp32ToDib(p, mwPtr->hbmp, mwPtr->bmpW, mwPtr->bmpH);
            });
        }
    }

    // Load Primary monitor DIB on Main Thread and paint it IMMEDIATELY
    int loadedCount = 0;
    if (!bmpPaths.empty() && !bmpPaths[0].empty()) {
        CurtainMonitorWindow& mw0 = g_curtain.windows[0];
        if (loadFastBmp32ToDib(bmpPaths[0], mw0.hbmp, mw0.bmpW, mw0.bmpH)) {
            ++loadedCount;
        }
    }

    HDC screenDc = GetDC(nullptr);
    auto createAndPaintMonitor = [&](CurtainMonitorWindow& mw) {
        if (mw.hbmp && !mw.memDc) {
            mw.memDc = CreateCompatibleDC(screenDc);
            if (mw.memDc) {
                SelectObject(mw.memDc, mw.hbmp);
            }
        }
        const int monW = mw.rect.right - mw.rect.left;
        const int monH = mw.rect.bottom - mw.rect.top;
        mw.hwnd = CreateWindowExW(
            WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST,
            kCurtainClass,
            L"SekaiPopBootCurtain",
            WS_POPUP,
            mw.rect.left, mw.rect.top, monW, monH,
            nullptr, nullptr, hInst, &mw
        );
        if (mw.hwnd) {
            ShowWindow(mw.hwnd, SW_SHOWNOACTIVATE);
            InvalidateRect(mw.hwnd, nullptr, FALSE);
            UpdateWindow(mw.hwnd);
        }
    };

    createAndPaintMonitor(g_curtain.windows[0]);
    g_curtain.primaryPaintMs = elapsedMsSinceBoot();

    for (std::thread& t : bgLoaders) {
        if (t.joinable()) t.join();
    }
    for (size_t i = 1; i < g_curtain.windows.size(); ++i) {
        if (g_curtain.windows[i].hbmp) {
            ++loadedCount;
        }
        createAndPaintMonitor(g_curtain.windows[i]);
    }
    ReleaseDC(nullptr, screenDc);

    g_curtain.allPaintMs = elapsedMsSinceBoot();
    g_curtain.active = !g_curtain.windows.empty();
    g_curtain.userDismissed = false;
    g_curtain.shownTick = GetTickCount64();

    const double pMs = g_curtain.primaryPaintMs;
    const double aMs = g_curtain.allPaintMs;
    const int monCount = static_cast<int>(monitors.size());
    std::thread([pMs, aMs, monCount, loadedCount]() {
        writeBootTimingLog(pMs, aMs, monCount, loadedCount);
    }).detach();

    return g_curtain.active;
}

// ============================================================================
// Parallel Cold-Boot File Cache Prefetcher (Warms Qt6 DLLs + QMLC Bytecode)
// ============================================================================

void prefetchSingleFile(const std::wstring& path, HANDLE hStopEvent) {
    if (hStopEvent && WaitForSingleObject(hStopEvent, 0) == WAIT_OBJECT_0) {
        return;
    }
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        return;
    }
    // 512 KB sequential chunks pull pages cleanly into Windows Standby/File Cache
    static thread_local std::vector<BYTE> buf(512 * 1024);
    DWORD rd = 0;
    while (ReadFile(hFile, buf.data(), static_cast<DWORD>(buf.size()), &rd, nullptr) && rd > 0) {
        if (hStopEvent && WaitForSingleObject(hStopEvent, 0) == WAIT_OBJECT_0) {
            break;
        }
    }
    CloseHandle(hFile);
}

void startColdBootPrefetch(const std::wstring& exeDir, HANDLE hStopEvent) {
    // Thread 1: Core binaries & Qt6 DLLs
    HANDLE hStop1 = nullptr;
    if (hStopEvent) {
        DuplicateHandle(GetCurrentProcess(), hStopEvent, GetCurrentProcess(), &hStop1, SYNCHRONIZE, FALSE, 0);
    }
    std::thread([exeDir, hStop1]() {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
        static const wchar_t* kFiles[] = {
            L"\\SekaiPopShell.exe",
            L"\\Qt6Core.dll",
            L"\\Qt6Gui.dll",
            L"\\Qt6Qml.dll",
            L"\\Qt6Quick.dll",
            L"\\Qt6Widgets.dll",
            L"\\Qt6Network.dll",
            L"\\Qt6QuickShapes.dll",
            L"\\Qt6QuickEffects.dll",
            L"\\Qt6LabsFolderListModel.dll",
            L"\\libc++.dll",
            L"\\libunwind.dll",
            L"\\platforms\\qwindows.dll"
        };
        for (const wchar_t* rel : kFiles) {
            if (g_shuttingDown.load() || (hStop1 && WaitForSingleObject(hStop1, 0) == WAIT_OBJECT_0)) {
                break;
            }
            prefetchSingleFile(exeDir + rel, hStop1);
        }
        if (hStop1) CloseHandle(hStop1);
    }).detach();

    // Thread 2: Precompiled QML bytecode cache (.qmlc) + staged core QML files
    HANDLE hStop2 = nullptr;
    if (hStopEvent) {
        DuplicateHandle(GetCurrentProcess(), hStopEvent, GetCurrentProcess(), &hStop2, SYNCHRONIZE, FALSE, 0);
    }
    std::thread([hStop2]() {
        const std::wstring qmlCacheDir = localAppDataDir() + L"\\SekaiPop\\SekaiPopShell\\cache\\qmlcache";
        WIN32_FIND_DATAW fd = {};
        HANDLE hFind = FindFirstFileW((qmlCacheDir + L"\\*.qmlc").c_str(), &fd);
        if (hFind != INVALID_HANDLE_VALUE) {
            int count = 0;
            do {
                if (g_shuttingDown.load() || (hStop2 && WaitForSingleObject(hStop2, 0) == WAIT_OBJECT_0)) {
                    break;
                }
                if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
                    prefetchSingleFile(qmlCacheDir + L"\\" + fd.cFileName, hStop2);
                    if (++count >= 250) {
                        break;
                    }
                }
            } while (FindNextFileW(hFind, &fd));
            FindClose(hFind);
        }
        if (hStop2) CloseHandle(hStop2);
    }).detach();
}

// ============================================================================
// Standard Windows Explorer Fallback & Deferred Startup Apps
// ============================================================================

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

enum class StartupApprovalState {
    NotFound,
    Enabled,
    Disabled
};

StartupApprovalState queryStartupApproval(HKEY root, const wchar_t* approvedSubKey, const wchar_t* itemName) {
    if (!approvedSubKey || !itemName || !itemName[0]) {
        return StartupApprovalState::NotFound;
    }
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(root, approvedSubKey, 0, KEY_READ | KEY_WOW64_64KEY, &hKey) != ERROR_SUCCESS) {
        return StartupApprovalState::NotFound;
    }
    BYTE buf[64] = {};
    DWORD cb = sizeof(buf);
    DWORD type = 0;
    LONG rc = RegQueryValueExW(hKey, itemName, nullptr, &type, buf, &cb);
    RegCloseKey(hKey);
    if (rc != ERROR_SUCCESS || type != REG_BINARY || cb < 1) {
        return StartupApprovalState::NotFound;
    }
    return ((buf[0] & 0x01) != 0) ? StartupApprovalState::Disabled : StartupApprovalState::Enabled;
}

bool isStartupItemDisabled(HKEY primaryRoot, const wchar_t* approvedSubKey, const wchar_t* itemName) {
    if (queryStartupApproval(primaryRoot, approvedSubKey, itemName) == StartupApprovalState::Disabled) {
        return true;
    }
    if (primaryRoot != HKEY_CURRENT_USER &&
        queryStartupApproval(HKEY_CURRENT_USER, approvedSubKey, itemName) == StartupApprovalState::Disabled) {
        return true;
    }
    return false;
}

void runRegistryRunKey(HKEY root, const wchar_t* subKey, const wchar_t* approvedSubKey) {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(root, subKey, 0, KEY_READ | KEY_WOW64_64KEY, &hKey) != ERROR_SUCCESS) {
        return;
    }

    DWORD index = 0;
    wchar_t valName[512] = {};
    BYTE data[4096] = {};
    for (;;) {
        DWORD valNameLen = static_cast<DWORD>(std::size(valName) - 1);
        DWORD dataLen = sizeof(data) - sizeof(wchar_t);
        DWORD type = 0;
        LONG rc = RegEnumValueW(hKey, index++, valName, &valNameLen, nullptr, &type, data, &dataLen);
        if (rc == ERROR_NO_MORE_ITEMS) {
            break;
        }
        if (rc != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || dataLen < sizeof(wchar_t)) {
            continue;
        }
        valName[valNameLen] = L'\0';
        data[dataLen] = 0;
        data[dataLen + 1] = 0;

        if (isStartupItemDisabled(root, approvedSubKey, valName)) {
            continue;
        }

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

void runStartupFolder(REFKNOWNFOLDERID folderId, HKEY approvedRoot) {
    PWSTR pathPtr = nullptr;
    if (FAILED(SHGetKnownFolderPath(folderId, 0, nullptr, &pathPtr)) || !pathPtr) {
        return;
    }
    std::wstring folder(pathPtr);
    CoTaskMemFree(pathPtr);

    constexpr const wchar_t* kApprovedFolderKey =
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\StartupFolder";

    WIN32_FIND_DATAW fd = {};
    HANDLE hFind = FindFirstFileW((folder + L"\\*").c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        return;
    }
    do {
        if (g_shuttingDown.load()) {
            break;
        }
        if ((fd.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) != 0) {
            continue;
        }
        std::wstring name(fd.cFileName);
        std::wstring low = toLowerW(name);
        if (low == L"desktop.ini" || low.find(L"sekaipop") != std::wstring::npos) {
            continue;
        }
        if (isStartupItemDisabled(approvedRoot, kApprovedFolderKey, name.c_str())) {
            continue;
        }
        std::wstring full = folder + L"\\" + name;
        ShellExecuteW(nullptr, L"open", full.c_str(), nullptr, folder.c_str(), SW_SHOWNORMAL);
        Sleep(200);
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
}

void launchWindowsStartupAppsOnce(HANDLE hUiReady) {
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
    std::thread([hThreadEvt]() {
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
        runRegistryRunKey(
            HKEY_LOCAL_MACHINE,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run"
        );
        runRegistryRunKey(
            HKEY_LOCAL_MACHINE,
            L"Software\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Run",
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run32"
        );
        runRegistryRunKey(
            HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run"
        );
        runStartupFolder(FOLDERID_CommonStartup, HKEY_LOCAL_MACHINE);
        runStartupFolder(FOLDERID_Startup, HKEY_CURRENT_USER);
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

bool waitProcessPumpingMessages(HANDLE hProc, HANDLE hCurtainDrop, HANDLE hUiReady, DWORD& outExitCode) {
    outExitCode = 0;
    ULONGLONG uiReadyFallbackTick = 0;

    for (;;) {
        HANDLE waitHandles[3] = { hProc, nullptr, nullptr };
        DWORD handleCount = 1;
        DWORD timeoutMs = INFINITE;

        if (g_curtain.active) {
            if (hCurtainDrop) {
                waitHandles[handleCount++] = hCurtainDrop;
            }
            if (hUiReady && uiReadyFallbackTick == 0) {
                waitHandles[handleCount++] = hUiReady;
            }
            timeoutMs = 50; // Responsive check for curtain failsafe & user-click dismiss
        }

        const DWORD res = MsgWaitForMultipleObjects(handleCount, waitHandles, FALSE, timeoutMs, QS_ALLINPUT);

        if (res == WAIT_OBJECT_0) {
            destroyBootCurtain();
            SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);
            GetExitCodeProcess(hProc, &outExitCode);
            return true;
        }

        if (g_curtain.active) {
            const ULONGLONG now = GetTickCount64();
            bool dropNow = g_curtain.userDismissed || ((now - g_curtain.shownTick) >= 7000ULL);
            if (hCurtainDrop && WaitForSingleObject(hCurtainDrop, 0) == WAIT_OBJECT_0) {
                // Live Wallpaper + Bar have both completed D3D11 frameSwapped
                dropNow = true;
            } else if (hUiReady && WaitForSingleObject(hUiReady, 0) == WAIT_OBJECT_0) {
                if (uiReadyFallbackTick == 0) {
                    uiReadyFallbackTick = now;
                } else if ((now - uiReadyFallbackTick) >= 120ULL) {
                    dropNow = true;
                }
            }
            if (dropNow) {
                destroyBootCurtain();
                SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);
            }
        }

        if (res == WAIT_OBJECT_0 + handleCount) {
            MSG msg = {};
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            if (g_curtain.active && g_curtain.userDismissed) {
                destroyBootCurtain();
                SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);
            }
            if (g_shuttingDown.load()) {
                destroyBootCurtain();
                if (WaitForSingleObject(hProc, 2500) == WAIT_OBJECT_0) {
                    GetExitCodeProcess(hProc, &outExitCode);
                }
                return true;
            }
        } else if (res == WAIT_FAILED) {
            destroyBootCurtain();
            return false;
        }
    }
}

void signalWindowsShellReady() {
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
    QueryPerformanceFrequency(&g_qpcFreq);
    QueryPerformanceCounter(&g_bootStartQpc);

    // Ensure 1:1 physical pixel geometry matching Windows monitors on mixed-DPI setups
    if (HMODULE hUser32 = GetModuleHandleW(L"user32.dll")) {
        using SetDpiCtxFn = BOOL(WINAPI*)(HANDLE);
        if (auto pSetDpiCtx = reinterpret_cast<SetDpiCtxFn>(GetProcAddress(hUser32, "SetProcessDpiAwarenessContext"))) {
            pSetDpiCtx(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4))); // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
        }
    }

    // Failsafe bypass: holding Left Shift during Windows logon immediately boots standard explorer.exe.
    if ((GetAsyncKeyState(VK_LSHIFT) & 0x8000) != 0) {
        signalWindowsShellReady();
        launchWindowsExplorer();
        return 0;
    }

    const std::wstring args = toLowerW(cmdLine ? cmdLine : L"");

    // Standalone benchmark/test mode: paints instant curtain, logs exact first_paint_ms, dismisses cleanly.
    if (args.find(L"--test-curtain") != std::wstring::npos) {
        SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
        showInstantBootCurtain(hInst);
        const ULONGLONG start = GetTickCount64();
        while (g_curtain.active && !g_curtain.userDismissed && (GetTickCount64() - start) < 800ULL) {
            MSG msg = {};
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            Sleep(16);
        }
        destroyBootCurtain();
        return 0;
    }

    const bool skipStartupApps = (args.find(L"--no-startup-apps") != std::wstring::npos);

    // Single-instance watchdog guard per logon session.
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"Local\\SekaiPopBoot_Watchdog_v1");
    if (hMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(hMutex);
        return 0;
    }

    // Briefly elevate priority so the instant curtain + shell launch win CPU ahead of cold-boot services.
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);

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
        if (hMutex) CloseHandle(hMutex);
        return 1;
    }

    // Step 1 (< 10ms): Paint pixel-exact cached desktop UI onto all monitors BEFORE dropping logon spinner.
    showInstantBootCurtain(hInst);

    // Step 2: Tell Windows 11 Winlogon immediately that the shell is visible -> zero Welcome spinner stall.
    signalWindowsShellReady();

    HWND sessionHwnd = createSessionMonitorWindow(hInst);

    // Named events signaled by SekaiPopShell:
    // - UiReady: Wallpaper + Bar created & mapped (triggers startup apps + stops prefetch)
    // - CurtainDrop: Live D3D11 frameSwapped on Wallpaper + Bar (triggers zero-flicker curtain teardown)
    HANDLE hUiReady = CreateEventW(nullptr, TRUE, FALSE, L"Local\\SekaiPopShell_UiReady_v1");
    HANDLE hCurtainDrop = CreateEventW(nullptr, TRUE, FALSE, L"Local\\SekaiPopShell_CurtainDrop_v1");

    // Step 3: Warm OS page cache in parallel for Qt6 DLLs and .qmlc bytecode while SekaiPopShell starts.
    startColdBootPrefetch(dir, hUiReady);

    // Launch standard Windows Startup apps once per logon AFTER SekaiPopShell signals UI ready.
    if (!skipStartupApps && FindWindowW(L"Shell_TrayWnd", nullptr) == nullptr) {
        launchWindowsStartupAppsOnce(hUiReady);
    }

    std::vector<ULONGLONG> recentCrashTicks;
    bool isFirstSpawn = true;

    while (!g_shuttingDown.load() && GetSystemMetrics(SM_SHUTTINGDOWN) == 0) {
        if (hUiReady) ResetEvent(hUiReady);
        if (hCurtainDrop) ResetEvent(hCurtainDrop);

        if (!isFirstSpawn) {
            showInstantBootCurtain(hInst);
        }
        isFirstSpawn = false;

        std::wstring cmd = L"\"" + shellExe + L"\" --native-shell";
        STARTUPINFOW si = {};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi = {};
        if (!CreateProcessW(shellExe.c_str(), cmd.data(), nullptr, nullptr, FALSE,
                            CREATE_DEFAULT_ERROR_MODE | HIGH_PRIORITY_CLASS,
                            nullptr, dir.c_str(), &si, &pi)) {
            destroyBootCurtain();
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
        waitProcessPumpingMessages(pi.hProcess, hCurtainDrop, hUiReady, exitCode);
        CloseHandle(pi.hProcess);
        g_childProcess = nullptr;
        destroyBootCurtain();

        if (g_shuttingDown.load() || GetSystemMetrics(SM_SHUTTINGDOWN) != 0) {
            break;
        }

        // Exit code 42 = clean user-requested Restart SekaiPop
        if (exitCode == 42) {
            Sleep(120);
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

    destroyBootCurtain();
    if (hCurtainDrop) CloseHandle(hCurtainDrop);
    if (hUiReady) CloseHandle(hUiReady);
    if (sessionHwnd) DestroyWindow(sessionHwnd);
    if (hMutex) {
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
    }
    return 0;
}
