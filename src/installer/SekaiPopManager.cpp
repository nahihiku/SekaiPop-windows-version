#include "ShellIntegration.h"

#include <commctrl.h>
#include <shellapi.h>
#include <shlwapi.h>

#include <string>
#include <vector>

using namespace SekaiPop::Installer;

namespace {

constexpr int IDC_STATUS_LABEL = 101;
constexpr int IDC_RADIO_NATIVE = 102;
constexpr int IDC_RADIO_SEAMLESS = 103;
constexpr int IDC_RADIO_EXPLORER = 104;
constexpr int IDC_BTN_APPLY = 105;
constexpr int IDC_BTN_LAUNCH = 106;
constexpr int IDC_BTN_RESTORE = 107;
constexpr int IDC_BTN_UNINSTALL = 108;
constexpr int IDC_BTN_CLOSE = 109;

struct ManagerUi {
    HWND hwnd = nullptr;
    HWND lblStatus = nullptr;
    HWND radNative = nullptr;
    HWND radSeamless = nullptr;
    HWND radExplorer = nullptr;
    HFONT fontTitle = nullptr;
    HFONT fontSub = nullptr;
    HFONT fontBold = nullptr;
    HFONT fontBody = nullptr;
    HBRUSH bgBrush = nullptr;
    HICON appIcon = nullptr;
    std::wstring installDir;
    int dpi = 96;

    int s(int px) const { return MulDiv(px, dpi, 96); }
};

ManagerUi g_ui;

std::wstring selfDirectory() {
    wchar_t buf[MAX_PATH * 4] = {};
    DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    if (n == 0 || n >= std::size(buf)) {
        return queryInstalledDir();
    }
    std::wstring p(buf, n);
    size_t pos = p.find_last_of(L"\\/");
    return (pos == std::wstring::npos) ? queryInstalledDir() : p.substr(0, pos);
}

std::wstring modeLabel(ShellMode m) {
    switch (m) {
    case ShellMode::NativeWinlogonShell:
        return L"Mode Aktif: Pengganti UI Penuh (Winlogon Native Shell)";
    case ShellMode::SeamlessAutoStart:
        return L"Mode Aktif: Seamless Overlay (Auto-Start + Taskbar Disembunyikan)";
    default:
        return L"Mode Aktif: Windows Explorer Standar";
    }
}

void refreshStatusUi() {
    const ShellMode m = queryCurrentShellMode();
    SetWindowTextW(g_ui.lblStatus, modeLabel(m).c_str());
    SendMessageW(g_ui.radNative, BM_SETCHECK, m == ShellMode::NativeWinlogonShell ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g_ui.radSeamless, BM_SETCHECK, m == ShellMode::SeamlessAutoStart ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g_ui.radExplorer, BM_SETCHECK, m == ShellMode::ExplorerDefault ? BST_CHECKED : BST_UNCHECKED, 0);
}

bool performUninstall(HWND parent, bool silent) {
    bool wipeUserData = false;
    if (!silent) {
        int res = MessageBoxW(
            parent,
            L"Anda yakin ingin menghapus instalasi SekaiPop Desktop Shell?\n\n"
            L"Proses ini akan:\n"
            L"- Mengembalikan Windows Explorer sebagai shell default Windows\n"
            L"- Menghapus auto-start dan pintasan Start Menu / Desktop\n"
            L"- Menghapus seluruh file aplikasi dari folder instalasi",
            L"Uninstall SekaiPop Desktop Shell",
            MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2
        );
        if (res != IDYES) {
            return false;
        }
        int wipeRes = MessageBoxW(
            parent,
            L"Hapus juga pengaturan pribadi, tema, dan cache SekaiPop di AppData?",
            L"Hapus Data Pengguna?",
            MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2
        );
        wipeUserData = (wipeRes == IDYES);
    }

    stopSekaiPopProcesses();

    std::wstring err;
    applyShellMode(ShellMode::ExplorerDefault, g_ui.installDir, err);
    restoreWindowsTaskbarAndExplorer(true);
    removeSekaiPopShortcuts();
    unregisterUninstallEntry();

    if (wipeUserData) {
        const std::wstring localApp = getKnownFolderPathStr(FOLDERID_LocalAppData);
        if (!localApp.empty()) {
            removeDirectoryRecursive(localApp + L"\\SekaiPop");
        }
    }

    // Schedule clean self-deletion of the install directory after SekaiPopManager.exe exits.
    const std::wstring dir = stripTrailingSlash(g_ui.installDir);
    if (dir.size() >= 6 && toLowerW(dir).find(L"sekaipop") != std::wstring::npos) {
        std::wstring cmd = L"cmd.exe /c \"ping 127.0.0.1 -n 3 > nul & rd /s /q \"" + dir + L"\"\"";
        STARTUPINFOW si = {};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi = {};
        if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                           CREATE_NO_WINDOW, nullptr, L"C:\\", &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
    }

    if (!silent) {
        MessageBoxW(
            parent,
            L"SekaiPop Desktop Shell berhasil dihapus dan Windows Explorer telah dipulihkan sepenuhnya.",
            L"Uninstall Selesai",
            MB_OK | MB_ICONINFORMATION
        );
    }
    return true;
}

void handleApplyMode() {
    ShellMode chosen = ShellMode::ExplorerDefault;
    if (SendMessageW(g_ui.radNative, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        chosen = ShellMode::NativeWinlogonShell;
    } else if (SendMessageW(g_ui.radSeamless, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        chosen = ShellMode::SeamlessAutoStart;
    }

    std::wstring err;
    if (!applyShellMode(chosen, g_ui.installDir, err)) {
        MessageBoxW(g_ui.hwnd, err.c_str(), L"Gagal Menerapkan Mode Shell", MB_OK | MB_ICONERROR);
        return;
    }
    refreshStatusUi();

    if (chosen == ShellMode::NativeWinlogonShell) {
        int ans = MessageBoxW(
            g_ui.hwnd,
            L"Mode Pengganti UI Penuh (Winlogon Native Shell) berhasil diterapkan di HKLM & HKCU!\n\n"
            L"Windows akan langsung memuat SekaiPop sejak login tanpa menjalankan explorer.exe.\n\n"
            L"Pengaman darurat:\n"
            L"- Tahan tombol Left Shift saat login Windows untuk masuk ke Explorer standar.\n"
            L"- Tekan Ctrl + Alt + Shift + E kapan saja untuk memulihkan Windows Explorer.\n\n"
            L"Ingin langsung menghentikan explorer.exe dan beralih penuh ke SekaiPop sekarang?",
            L"Mode Winlogon Native Aktif",
            MB_YESNO | MB_ICONINFORMATION
        );
        if (ans == IDYES) {
            switchLiveToNativeShell(g_ui.installDir);
        }
    } else if (chosen == ShellMode::SeamlessAutoStart) {
        restoreWindowsTaskbarAndExplorer(true);
        int ans = MessageBoxW(
            g_ui.hwnd,
            L"Mode Seamless (Auto-Start + Sembunyikan Taskbar) berhasil diterapkan.\n\n"
            L"Ingin menjalankan / memuat ulang SekaiPop sekarang?",
            L"Mode Seamless Aktif",
            MB_YESNO | MB_ICONINFORMATION
        );
        if (ans == IDYES) {
            stopSekaiPopProcesses();
            launchAsDesktopUser(g_ui.installDir + L"\\SekaiPopShell.exe", L"", g_ui.installDir);
        }
    } else {
        stopSekaiPopProcesses();
        restoreWindowsTaskbarAndExplorer(true);
        MessageBoxW(
            g_ui.hwnd,
            L"Windows Explorer standar telah dikembalikan sebagai shell default dan Taskbar Windows dipulihkan.",
            L"Windows Explorer Dipulihkan",
            MB_OK | MB_ICONINFORMATION
        );
    }
}

LRESULT CALLBACK managerWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_ui.hwnd = hwnd;
        g_ui.dpi = queryWindowDpi(hwnd);
        if (g_ui.dpi <= 0) g_ui.dpi = 96;
        g_ui.bgBrush = CreateSolidBrush(RGB(246, 248, 252));

        g_ui.fontTitle = CreateFontW(-g_ui.s(20), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                     DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        g_ui.fontSub = CreateFontW(-g_ui.s(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                   DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        g_ui.fontBold = CreateFontW(-g_ui.s(13), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                    DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        g_ui.fontBody = CreateFontW(-g_ui.s(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                    DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

        const int pad = g_ui.s(22);
        const int w = g_ui.s(536);
        int y = g_ui.s(92);

        g_ui.lblStatus = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                                         pad, y, w, g_ui.s(22), hwnd, reinterpret_cast<HMENU>(IDC_STATUS_LABEL), nullptr, nullptr);
        SendMessageW(g_ui.lblStatus, WM_SETFONT, reinterpret_cast<WPARAM>(g_ui.fontBold), TRUE);
        y += g_ui.s(30);

        HWND grp = CreateWindowExW(0, L"BUTTON", L"Pilih Mode Integrasi UI Windows",
                                   WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                   pad, y, w, g_ui.s(232), hwnd, nullptr, nullptr, nullptr);
        SendMessageW(grp, WM_SETFONT, reinterpret_cast<WPARAM>(g_ui.fontBold), TRUE);

        int gy = y + g_ui.s(26);
        g_ui.radNative = CreateWindowExW(0, L"BUTTON",
                                         L"Pengganti UI Windows Penuh (Winlogon Native Shell)",
                                         WS_CHILD | WS_VISIBLE | WS_GROUP | BS_AUTORADIOBUTTON,
                                         pad + g_ui.s(16), gy, w - g_ui.s(32), g_ui.s(22),
                                         hwnd, reinterpret_cast<HMENU>(IDC_RADIO_NATIVE), nullptr, nullptr);
        SendMessageW(g_ui.radNative, WM_SETFONT, reinterpret_cast<WPARAM>(g_ui.fontBold), TRUE);
        gy += g_ui.s(22);
        HWND d1 = CreateWindowExW(0, L"STATIC",
                                  L"Langsung boot ke SekaiPop sejak login Windows (menggantikan explorer.exe sepenuhnya)\n"
                                  L"dengan watchdog anti-black-screen. Tahan Left Shift saat login atau Ctrl+Alt+Shift+E untuk pulihkan.",
                                  WS_CHILD | WS_VISIBLE,
                                  pad + g_ui.s(34), gy, w - g_ui.s(48), g_ui.s(38), hwnd, nullptr, nullptr, nullptr);
        SendMessageW(d1, WM_SETFONT, reinterpret_cast<WPARAM>(g_ui.fontBody), TRUE);
        gy += g_ui.s(46);

        g_ui.radSeamless = CreateWindowExW(0, L"BUTTON",
                                           L"Mode Seamless (Auto-Start + Sembunyikan Taskbar Windows)",
                                           WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                                           pad + g_ui.s(16), gy, w - g_ui.s(32), g_ui.s(22),
                                           hwnd, reinterpret_cast<HMENU>(IDC_RADIO_SEAMLESS), nullptr, nullptr);
        SendMessageW(g_ui.radSeamless, WM_SETFONT, reinterpret_cast<WPARAM>(g_ui.fontBold), TRUE);
        gy += g_ui.s(22);
        HWND d2 = CreateWindowExW(0, L"STATIC",
                                  L"Berjalan otomatis saat login dan menyembunyikan penuh Taskbar Windows, sambil tetap\n"
                                  L"menjaga layanan background explorer.exe untuk kompatibilitas maksimal semua aplikasi.",
                                  WS_CHILD | WS_VISIBLE,
                                  pad + g_ui.s(34), gy, w - g_ui.s(48), g_ui.s(38), hwnd, nullptr, nullptr, nullptr);
        SendMessageW(d2, WM_SETFONT, reinterpret_cast<WPARAM>(g_ui.fontBody), TRUE);
        gy += g_ui.s(46);

        g_ui.radExplorer = CreateWindowExW(0, L"BUTTON",
                                           L"Windows Explorer Standar (Nonaktifkan Penggantian Shell)",
                                           WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                                           pad + g_ui.s(16), gy, w - g_ui.s(32), g_ui.s(22),
                                           hwnd, reinterpret_cast<HMENU>(IDC_RADIO_EXPLORER), nullptr, nullptr);
        SendMessageW(g_ui.radExplorer, WM_SETFONT, reinterpret_cast<WPARAM>(g_ui.fontBold), TRUE);
        gy += g_ui.s(22);
        HWND d3 = CreateWindowExW(0, L"STATIC",
                                  L"Mengembalikan shell dan Taskbar bawaan Windows sepenuhnya.",
                                  WS_CHILD | WS_VISIBLE,
                                  pad + g_ui.s(34), gy, w - g_ui.s(48), g_ui.s(20), hwnd, nullptr, nullptr, nullptr);
        SendMessageW(d3, WM_SETFONT, reinterpret_cast<WPARAM>(g_ui.fontBody), TRUE);

        y += g_ui.s(246);
        const int btnH = g_ui.s(34);
        HWND bApply = CreateWindowExW(0, L"BUTTON", L"Terapkan Mode",
                                      WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                      pad, y, g_ui.s(138), btnH, hwnd, reinterpret_cast<HMENU>(IDC_BTN_APPLY), nullptr, nullptr);
        HWND bLaunch = CreateWindowExW(0, L"BUTTON", L"Jalankan / Restart",
                                       WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                       pad + g_ui.s(146), y, g_ui.s(138), btnH, hwnd, reinterpret_cast<HMENU>(IDC_BTN_LAUNCH), nullptr, nullptr);
        HWND bRestore = CreateWindowExW(0, L"BUTTON", L"Pulihkan Explorer",
                                        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                        pad + g_ui.s(292), y, g_ui.s(128), btnH, hwnd, reinterpret_cast<HMENU>(IDC_BTN_RESTORE), nullptr, nullptr);
        HWND bUninst = CreateWindowExW(0, L"BUTTON", L"Uninstall",
                                       WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                       pad + g_ui.s(428), y, g_ui.s(108), btnH, hwnd, reinterpret_cast<HMENU>(IDC_BTN_UNINSTALL), nullptr, nullptr);

        for (HWND b : {bApply, bLaunch, bRestore, bUninst}) {
            SendMessageW(b, WM_SETFONT, reinterpret_cast<WPARAM>(g_ui.fontBold), TRUE);
        }

        refreshStatusUi();
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps = {};
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc = {};
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, g_ui.bgBrush);

        // Dark navy header banner with mint bottom accent line
        const int hdrH = g_ui.s(74);
        RECT hdr = {0, 0, rc.right, hdrH};
        HBRUSH navy = CreateSolidBrush(RGB(22, 34, 58));
        FillRect(hdc, &hdr, navy);
        DeleteObject(navy);

        RECT accent = {0, hdrH - g_ui.s(3), rc.right, hdrH};
        HBRUSH mint = CreateSolidBrush(RGB(94, 234, 212));
        FillRect(hdc, &accent, mint);
        DeleteObject(mint);

        if (g_ui.appIcon) {
            DrawIconEx(hdc, g_ui.s(20), g_ui.s(17), g_ui.appIcon, g_ui.s(40), g_ui.s(40), 0, nullptr, DI_NORMAL);
        }

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(255, 255, 255));
        HGDIOBJ oldF = SelectObject(hdc, g_ui.fontTitle);
        RECT tRect = {g_ui.s(72), g_ui.s(14), rc.right - g_ui.s(20), g_ui.s(42)};
        DrawTextW(hdc, L"SekaiPop Control Center", -1, &tRect, DT_LEFT | DT_SINGLELINE);

        SetTextColor(hdc, RGB(180, 212, 230));
        SelectObject(hdc, g_ui.fontSub);
        RECT sRect = {g_ui.s(72), g_ui.s(42), rc.right - g_ui.s(20), g_ui.s(64)};
        DrawTextW(hdc, L"Pengaturan Mode Shell Windows, Pemulihan Explorer & Uninstall (v1.0.0)", -1, &sRect, DT_LEFT | DT_SINGLELINE);
        SelectObject(hdc, oldF);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(30, 41, 59));
        return reinterpret_cast<LRESULT>(g_ui.bgBrush);
    }

    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        if (id == IDC_BTN_APPLY) {
            handleApplyMode();
            return 0;
        }
        if (id == IDC_BTN_LAUNCH) {
            if (queryCurrentShellMode() == ShellMode::NativeWinlogonShell) {
                switchLiveToNativeShell(g_ui.installDir);
            } else {
                stopSekaiPopProcesses();
                launchAsDesktopUser(g_ui.installDir + L"\\SekaiPopShell.exe", L"", g_ui.installDir);
            }
            return 0;
        }
        if (id == IDC_BTN_RESTORE) {
            stopSekaiPopProcesses();
            std::wstring err;
            applyShellMode(ShellMode::ExplorerDefault, g_ui.installDir, err);
            restoreWindowsTaskbarAndExplorer(true);
            refreshStatusUi();
            MessageBoxW(hwnd, L"Windows Explorer dan Taskbar standar telah dipulihkan.",
                        L"Pemulihan Berhasil", MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        if (id == IDC_BTN_UNINSTALL) {
            if (performUninstall(hwnd, false)) {
                DestroyWindow(hwnd);
            }
            return 0;
        }
        if (id == IDC_BTN_CLOSE || id == IDCANCEL) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }

    case WM_DESTROY:
        if (g_ui.fontTitle) DeleteObject(g_ui.fontTitle);
        if (g_ui.fontSub) DeleteObject(g_ui.fontSub);
        if (g_ui.fontBold) DeleteObject(g_ui.fontBold);
        if (g_ui.fontBody) DeleteObject(g_ui.fontBody);
        if (g_ui.bgBrush) DeleteObject(g_ui.bgBrush);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR cmdLine, int showCmd) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&icc);

    g_ui.installDir = selfDirectory();
    if (GetFileAttributesW((g_ui.installDir + L"\\SekaiPopShell.exe").c_str()) == INVALID_FILE_ATTRIBUTES) {
        g_ui.installDir = queryInstalledDir();
    }

    const std::wstring args = toLowerW(cmdLine ? cmdLine : L"");
    if (args.find(L"--restore-explorer") != std::wstring::npos) {
        stopSekaiPopProcesses();
        std::wstring err;
        applyShellMode(ShellMode::ExplorerDefault, g_ui.installDir, err);
        restoreWindowsTaskbarAndExplorer(true);
        MessageBoxW(nullptr,
                    L"Windows Explorer dan Taskbar standar telah dipulihkan sepenuhnya.\n"
                    L"Pengaturan Winlogon Shell juga telah dikembalikan ke default Windows.",
                    L"SekaiPop Restore Explorer",
                    MB_OK | MB_ICONINFORMATION);
        CoUninitialize();
        return 0;
    }

    if (args.find(L"--uninstall-silent") != std::wstring::npos) {
        performUninstall(nullptr, true);
        CoUninitialize();
        return 0;
    }

    if (args.find(L"--uninstall") != std::wstring::npos) {
        performUninstall(nullptr, false);
        CoUninitialize();
        return 0;
    }

    g_ui.appIcon = LoadIconW(hInst, MAKEINTRESOURCEW(1));

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &managerWndProc;
    wc.hInstance = hInst;
    wc.hIcon = g_ui.appIcon;
    wc.hIconSm = g_ui.appIcon;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"SekaiPopManagerWnd";
    RegisterClassExW(&wc);

    const int sysDpi = querySystemDpi();
    const int winW = MulDiv(596, sysDpi, 96);
    const int winH = MulDiv(456, sysDpi, 96);
    const int scrW = GetSystemMetrics(SM_CXSCREEN);
    const int scrH = GetSystemMetrics(SM_CYSCREEN);

    HWND hwnd = CreateWindowExW(
        0,
        wc.lpszClassName,
        L"SekaiPop Control Center",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        (scrW - winW) / 2,
        (scrH - winH) / 2,
        winW,
        winH,
        nullptr,
        nullptr,
        hInst,
        nullptr
    );
    if (!hwnd) {
        CoUninitialize();
        return 1;
    }

    ShowWindow(hwnd, showCmd);
    UpdateWindow(hwnd);

    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    CoUninitialize();
    return 0;
}
