#include "ShellIntegration.h"

#include <commctrl.h>
#include <setupapi.h>
#include <shlobj.h>
#include <shlwapi.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

using namespace SekaiPop::Installer;

namespace {

constexpr int IDC_EDIT_DIR = 201;
constexpr int IDC_BTN_BROWSE = 202;
constexpr int IDC_RAD_NATIVE = 203;
constexpr int IDC_RAD_SEAMLESS = 204;
constexpr int IDC_CHK_STARTMENU = 205;
constexpr int IDC_CHK_DESKTOP = 206;
constexpr int IDC_CHK_LAUNCH = 207;
constexpr int IDC_PROGRESS = 208;
constexpr int IDC_STATUS = 209;
constexpr int IDC_BTN_INSTALL = 210;
constexpr int IDC_BTN_CANCEL = 211;

constexpr UINT WM_APP_PROGRESS = WM_APP + 1;
constexpr UINT WM_APP_DONE = WM_APP + 2;

#pragma pack(push, 1)
struct EmbeddedCabFooter {
    uint64_t cabOffset;
    uint64_t cabSize;
    uint64_t unpackedBytes;
    char magic[8];
};
#pragma pack(pop)

static_assert(sizeof(EmbeddedCabFooter) == 32, "Footer size must be 32 bytes");

struct SetupUi {
    HWND hwnd = nullptr;
    HWND editDir = nullptr;
    HWND btnBrowse = nullptr;
    HWND radNative = nullptr;
    HWND radSeamless = nullptr;
    HWND chkStartMenu = nullptr;
    HWND chkDesktop = nullptr;
    HWND chkLaunch = nullptr;
    HWND progress = nullptr;
    HWND lblStatus = nullptr;
    HWND btnInstall = nullptr;
    HWND btnCancel = nullptr;
    HFONT fontTitle = nullptr;
    HFONT fontSub = nullptr;
    HFONT fontBold = nullptr;
    HFONT fontBody = nullptr;
    HBRUSH bgBrush = nullptr;
    HICON appIcon = nullptr;
    int dpi = 96;
    std::atomic<bool> installing{false};
    bool installDone = false;
    ShellMode installedMode = ShellMode::NativeWinlogonShell;
    std::wstring installedDir;

    int s(int px) const { return MulDiv(px, dpi, 96); }
};

SetupUi g_ui;

std::wstring selfExePath() {
    wchar_t buf[MAX_PATH * 4] = {};
    DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    return (n > 0 && n < std::size(buf)) ? std::wstring(buf, n) : std::wstring();
}

std::wstring utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int needed = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (needed <= 0) return {};
    std::wstring out(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), needed);
    return out;
}

bool readEmbeddedFooter(const std::wstring& exePath, EmbeddedCabFooter& outFooter) {
    HANDLE h = CreateFileW(exePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }
    LARGE_INTEGER fsz = {};
    if (!GetFileSizeEx(h, &fsz) || fsz.QuadPart < static_cast<LONGLONG>(sizeof(EmbeddedCabFooter))) {
        CloseHandle(h);
        return false;
    }
    LARGE_INTEGER pos = {};
    pos.QuadPart = fsz.QuadPart - static_cast<LONGLONG>(sizeof(EmbeddedCabFooter));
    if (!SetFilePointerEx(h, pos, nullptr, FILE_BEGIN)) {
        CloseHandle(h);
        return false;
    }
    DWORD got = 0;
    const BOOL ok = ReadFile(h, &outFooter, sizeof(outFooter), &got, nullptr);
    CloseHandle(h);
    if (!ok || got != sizeof(outFooter)) {
        return false;
    }
    if (std::memcmp(outFooter.magic, "SKPPCAB1", 8) != 0) {
        return false;
    }
    if (outFooter.cabSize == 0 ||
        outFooter.cabOffset + outFooter.cabSize + sizeof(EmbeddedCabFooter) > static_cast<uint64_t>(fsz.QuadPart)) {
        return false;
    }
    return true;
}

bool extractEmbeddedCabToFile(const std::wstring& exePath, const EmbeddedCabFooter& footer, const std::wstring& dstCabPath) {
    HANDLE hIn = CreateFileW(exePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hIn == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER startPos = {};
    startPos.QuadPart = static_cast<LONGLONG>(footer.cabOffset);
    if (!SetFilePointerEx(hIn, startPos, nullptr, FILE_BEGIN)) {
        CloseHandle(hIn);
        return false;
    }

    HANDLE hOut = CreateFileW(dstCabPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (hOut == INVALID_HANDLE_VALUE) {
        CloseHandle(hIn);
        return false;
    }

    std::vector<BYTE> buf(256 * 1024);
    uint64_t remaining = footer.cabSize;
    bool ok = true;
    while (remaining > 0) {
        const DWORD toRead = static_cast<DWORD>(std::min<uint64_t>(buf.size(), remaining));
        DWORD got = 0;
        if (!ReadFile(hIn, buf.data(), toRead, &got, nullptr) || got == 0) {
            ok = false;
            break;
        }
        DWORD written = 0;
        if (!WriteFile(hOut, buf.data(), got, &written, nullptr) || written != got) {
            ok = false;
            break;
        }
        remaining -= got;
    }

    CloseHandle(hOut);
    CloseHandle(hIn);
    return ok;
}

struct CabExtractCtx {
    std::wstring targetPakPath;
    bool extracted = false;
};

UINT CALLBACK cabNotifyCallback(PVOID context, UINT notification, UINT_PTR param1, UINT_PTR) {
    auto* ctx = static_cast<CabExtractCtx*>(context);
    if (notification == SPFILENOTIFY_FILEINCABINET) {
        auto* info = reinterpret_cast<FILE_IN_CABINET_INFO_W*>(param1);
        if (info && ctx && ctx->targetPakPath.size() < MAX_PATH) {
            wcsncpy_s(info->FullTargetName, MAX_PATH, ctx->targetPakPath.c_str(), _TRUNCATE);
            return FILEOP_DOIT;
        }
        return FILEOP_ABORT;
    }
    if (notification == SPFILENOTIFY_FILEEXTRACTED) {
        auto* fp = reinterpret_cast<FILEPATHS_W*>(param1);
        if (ctx && fp && fp->Win32Error == NO_ERROR) {
            ctx->extracted = true;
        }
        return NO_ERROR;
    }
    return NO_ERROR;
}

bool readExact(HANDLE h, void* dst, uint64_t len) {
    auto* p = static_cast<BYTE*>(dst);
    uint64_t left = len;
    while (left > 0) {
        const DWORD chunk = static_cast<DWORD>(std::min<uint64_t>(left, 16ULL * 1024ULL * 1024ULL));
        DWORD got = 0;
        if (!ReadFile(h, p, chunk, &got, nullptr) || got == 0) {
            return false;
        }
        p += got;
        left -= got;
    }
    return true;
}

bool ensureParentDirExists(const std::wstring& fullFilePath) {
    size_t pos = fullFilePath.find_last_of(L"\\/");
    if (pos == std::wstring::npos) {
        return true;
    }
    const std::wstring dir = fullFilePath.substr(0, pos);
    const int rc = SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    return (rc == ERROR_SUCCESS || rc == ERROR_ALREADY_EXISTS || rc == ERROR_FILE_EXISTS);
}

void postProgress(HWND hwnd, int pct, const std::wstring& text) {
    if (!hwnd) return;
    auto* copy = new std::wstring(text);
    PostMessageW(hwnd, WM_APP_PROGRESS, static_cast<WPARAM>(pct), reinterpret_cast<LPARAM>(copy));
}

bool unpackPayloadToDir(const std::wstring& exePath, const std::wstring& targetDirRaw, HWND uiHwnd,
                        uint64_t& outUnpackedBytes, std::wstring& outErr) {
    outErr.clear();
    outUnpackedBytes = 0;

    EmbeddedCabFooter footer = {};
    if (!readEmbeddedFooter(exePath, footer)) {
        outErr = L"Paket instalasi (payload CAB) tidak ditemukan atau rusak di dalam file Setup.";
        return false;
    }
    outUnpackedBytes = footer.unpackedBytes;

    wchar_t tempDirBuf[MAX_PATH] = {};
    if (GetTempPathW(MAX_PATH, tempDirBuf) == 0) {
        outErr = L"Gagal mengakses direktori sementara Windows (%TEMP%).";
        return false;
    }
    const std::wstring tempBase = stripTrailingSlash(tempDirBuf) + L"\\SekaiPopSetup_" + std::to_wstring(GetCurrentProcessId());
    SHCreateDirectoryExW(nullptr, tempBase.c_str(), nullptr);
    const std::wstring tempCab = tempBase + L"\\payload.cab";
    const std::wstring tempPak = tempBase + L"\\payload.pak";

    auto cleanupTemp = [&]() {
        DeleteFileW(tempPak.c_str());
        DeleteFileW(tempCab.c_str());
        RemoveDirectoryW(tempBase.c_str());
    };

    postProgress(uiHwnd, 5, L"Menyiapkan paket instalasi terkompresi...");
    if (!extractEmbeddedCabToFile(exePath, footer, tempCab)) {
        cleanupTemp();
        outErr = L"Gagal menyalin arsip payload ke folder sementara.";
        return false;
    }

    postProgress(uiHwnd, 15, L"Mendekompresi paket SekaiPop (LZX:21)...");
    CabExtractCtx cabCtx;
    cabCtx.targetPakPath = tempPak;
    if (!SetupIterateCabinetW(tempCab.c_str(), 0, &cabNotifyCallback, &cabCtx) || !cabCtx.extracted) {
        cleanupTemp();
        outErr = L"Gagal mendekompresi arsip CAB (SetupIterateCabinetW gagal).";
        return false;
    }
    DeleteFileW(tempCab.c_str());

    const std::wstring targetDir = stripTrailingSlash(targetDirRaw);
    SHCreateDirectoryExW(nullptr, targetDir.c_str(), nullptr);

    HANDLE hPak = CreateFileW(tempPak.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (hPak == INVALID_HANDLE_VALUE) {
        cleanupTemp();
        outErr = L"Gagal membuka paket payload yang didekompresi.";
        return false;
    }

    char magic[8] = {};
    uint32_t fileCount = 0;
    uint64_t totalBytes = 0;
    if (!readExact(hPak, magic, 8) || std::memcmp(magic, "SKPPPAK1", 8) != 0 ||
        !readExact(hPak, &fileCount, sizeof(fileCount)) ||
        !readExact(hPak, &totalBytes, sizeof(totalBytes))) {
        CloseHandle(hPak);
        cleanupTemp();
        outErr = L"Header paket payload.pak tidak valid.";
        return false;
    }
    if (totalBytes > 0) {
        outUnpackedBytes = totalBytes;
    }

    std::vector<BYTE> copyBuf(256 * 1024);
    uint64_t writtenTotal = 0;
    int lastPct = 18;

    for (uint32_t i = 0; i < fileCount; ++i) {
        uint32_t pathLen = 0;
        if (!readExact(hPak, &pathLen, sizeof(pathLen)) || pathLen == 0 || pathLen > 4096) {
            CloseHandle(hPak);
            cleanupTemp();
            outErr = L"Entri path pada paket rusak.";
            return false;
        }
        std::string relUtf8(pathLen, '\0');
        if (!readExact(hPak, relUtf8.data(), pathLen)) {
            CloseHandle(hPak);
            cleanupTemp();
            outErr = L"Gagal membaca nama file dari paket.";
            return false;
        }
        uint64_t fileLen = 0;
        if (!readExact(hPak, &fileLen, sizeof(fileLen))) {
            CloseHandle(hPak);
            cleanupTemp();
            outErr = L"Gagal membaca ukuran file dari paket.";
            return false;
        }

        std::wstring relW = utf8ToWide(relUtf8);
        // Prevent any path traversal outside targetDir
        if (relW.empty() || relW.find(L"..") != std::wstring::npos || relW[0] == L'\\' || relW[0] == L'/') {
            CloseHandle(hPak);
            cleanupTemp();
            outErr = L"Entri path tidak aman dalam paket.";
            return false;
        }

        const std::wstring fullDst = targetDir + L"\\" + relW;
        if (!ensureParentDirExists(fullDst)) {
            CloseHandle(hPak);
            cleanupTemp();
            outErr = L"Gagal membuat direktori tujuan: " + fullDst;
            return false;
        }

        SetFileAttributesW(fullDst.c_str(), FILE_ATTRIBUTE_NORMAL);
        HANDLE hOut = CreateFileW(fullDst.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hOut == INVALID_HANDLE_VALUE) {
            // Retry once after brief delay in case antivirus/shell just released a lock
            Sleep(250);
            hOut = CreateFileW(fullDst.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        }
        if (hOut == INVALID_HANDLE_VALUE) {
            CloseHandle(hPak);
            cleanupTemp();
            outErr = L"Gagal menulis file (kemungkinan masih dipakai proses lain): " + fullDst;
            return false;
        }

        uint64_t rem = fileLen;
        while (rem > 0) {
            const DWORD chunk = static_cast<DWORD>(std::min<uint64_t>(copyBuf.size(), rem));
            if (!readExact(hPak, copyBuf.data(), chunk)) {
                CloseHandle(hOut);
                CloseHandle(hPak);
                cleanupTemp();
                outErr = L"Data paket terpotong saat mengekstrak: " + relW;
                return false;
            }
            DWORD wr = 0;
            if (!WriteFile(hOut, copyBuf.data(), chunk, &wr, nullptr) || wr != chunk) {
                CloseHandle(hOut);
                CloseHandle(hPak);
                cleanupTemp();
                outErr = L"Gagal menulis ke disk (periksa ruang kosong): " + fullDst;
                return false;
            }
            rem -= chunk;
            writtenTotal += chunk;
        }
        CloseHandle(hOut);

        const int pct = 20 + (totalBytes > 0 ? static_cast<int>((writtenTotal * 70ULL) / totalBytes)
                                             : static_cast<int>(((i + 1) * 70ULL) / std::max<uint32_t>(1, fileCount)));
        if (pct > lastPct || (i % 40 == 0)) {
            lastPct = pct;
            postProgress(uiHwnd, std::min(90, pct), L"Mengekstrak: " + relW);
        }
    }

    CloseHandle(hPak);
    cleanupTemp();
    return true;
}

void setControlsEnabled(bool en) {
    EnableWindow(g_ui.editDir, en);
    EnableWindow(g_ui.btnBrowse, en);
    EnableWindow(g_ui.radNative, en);
    EnableWindow(g_ui.radSeamless, en);
    EnableWindow(g_ui.chkStartMenu, en);
    EnableWindow(g_ui.chkDesktop, en);
    EnableWindow(g_ui.chkLaunch, en);
    EnableWindow(g_ui.btnInstall, en);
    EnableWindow(g_ui.btnCancel, en);
}

void browseInstallFolder() {
    IFileOpenDialog* dlg = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg))) && dlg) {
        DWORD opts = 0;
        dlg->GetOptions(&opts);
        dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        dlg->SetTitle(L"Pilih Folder Instalasi SekaiPop");
        if (SUCCEEDED(dlg->Show(g_ui.hwnd))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dlg->GetResult(&item)) && item) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                    std::wstring chosen(path);
                    CoTaskMemFree(path);
                    if (toLowerW(chosen).find(L"sekaipop") == std::wstring::npos) {
                        chosen = stripTrailingSlash(chosen) + L"\\SekaiPop";
                    }
                    SetWindowTextW(g_ui.editDir, chosen.c_str());
                }
                item->Release();
            }
        }
        dlg->Release();
    }
}

struct InstallOutcome {
    bool ok = false;
    std::wstring errorMsg;
    ShellMode mode = ShellMode::NativeWinlogonShell;
    std::wstring dir;
    bool launchAfter = true;
};

void startInstallWorker() {
    if (g_ui.installing.exchange(true)) {
        return;
    }

    wchar_t dirBuf[MAX_PATH * 2] = {};
    GetWindowTextW(g_ui.editDir, dirBuf, static_cast<int>(std::size(dirBuf)));
    std::wstring targetDir = stripTrailingSlash(dirBuf);
    if (targetDir.size() < 4) {
        g_ui.installing.store(false);
        MessageBoxW(g_ui.hwnd, L"Silakan masukkan lokasi folder instalasi yang valid.", L"Folder Tidak Valid", MB_OK | MB_ICONWARNING);
        return;
    }

    const ShellMode mode = (SendMessageW(g_ui.radNative, BM_GETCHECK, 0, 0) == BST_CHECKED)
        ? ShellMode::NativeWinlogonShell
        : ShellMode::SeamlessAutoStart;
    const bool mkStartMenu = (SendMessageW(g_ui.chkStartMenu, BM_GETCHECK, 0, 0) == BST_CHECKED);
    const bool mkDesktop = (SendMessageW(g_ui.chkDesktop, BM_GETCHECK, 0, 0) == BST_CHECKED);
    const bool launchNow = (SendMessageW(g_ui.chkLaunch, BM_GETCHECK, 0, 0) == BST_CHECKED);
    const std::wstring exePath = selfExePath();
    HWND hwnd = g_ui.hwnd;

    setControlsEnabled(false);
    SendMessageW(g_ui.progress, PBM_SETPOS, 2, 0);
    SetWindowTextW(g_ui.lblStatus, L"Menghentikan instansi SekaiPop yang sedang berjalan...");

    std::thread([hwnd, exePath, targetDir, mode, mkStartMenu, mkDesktop, launchNow]() {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        auto* res = new InstallOutcome();
        res->mode = mode;
        res->dir = targetDir;
        res->launchAfter = launchNow;

        stopSekaiPopProcesses();

        uint64_t unpackedBytes = 0;
        if (!unpackPayloadToDir(exePath, targetDir, hwnd, unpackedBytes, res->errorMsg)) {
            res->ok = false;
            CoUninitialize();
            PostMessageW(hwnd, WM_APP_DONE, 0, reinterpret_cast<LPARAM>(res));
            return;
        }

        postProgress(hwnd, 93, L"Mendaftarkan integrasi Shell Windows & Uninstall...");
        if (!applyShellMode(mode, targetDir, res->errorMsg)) {
            res->ok = false;
            CoUninitialize();
            PostMessageW(hwnd, WM_APP_DONE, 0, reinterpret_cast<LPARAM>(res));
            return;
        }

        const DWORD sizeKB = static_cast<DWORD>(std::max<uint64_t>(1024ULL, unpackedBytes / 1024ULL));
        registerUninstallEntry(targetDir, sizeKB);

        postProgress(hwnd, 97, L"Membuat pintasan Start Menu dan Desktop...");
        createSekaiPopShortcuts(targetDir, mkStartMenu, mkDesktop);

        postProgress(hwnd, 100, L"Instalasi selesai!");
        res->ok = true;
        CoUninitialize();
        PostMessageW(hwnd, WM_APP_DONE, 0, reinterpret_cast<LPARAM>(res));
    }).detach();
}

LRESULT CALLBACK setupWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_ui.hwnd = hwnd;
        g_ui.dpi = queryWindowDpi(hwnd);
        if (g_ui.dpi <= 0) g_ui.dpi = 96;
        g_ui.bgBrush = CreateSolidBrush(RGB(246, 248, 252));

        g_ui.fontTitle = CreateFontW(-g_ui.s(21), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
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
        const int w = g_ui.s(576);
        int y = g_ui.s(92);

        HWND lblDir = CreateWindowExW(0, L"STATIC", L"Folder Instalasi:", WS_CHILD | WS_VISIBLE,
                                      pad, y, w, g_ui.s(20), hwnd, nullptr, nullptr, nullptr);
        SendMessageW(lblDir, WM_SETFONT, reinterpret_cast<WPARAM>(g_ui.fontBold), TRUE);
        y += g_ui.s(22);

        g_ui.editDir = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"C:\\Program Files\\SekaiPop",
                                       WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                       pad, y, w - g_ui.s(98), g_ui.s(28), hwnd, reinterpret_cast<HMENU>(IDC_EDIT_DIR), nullptr, nullptr);
        SendMessageW(g_ui.editDir, WM_SETFONT, reinterpret_cast<WPARAM>(g_ui.fontBody), TRUE);

        g_ui.btnBrowse = CreateWindowExW(0, L"BUTTON", L"Ubah...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                         pad + w - g_ui.s(90), y, g_ui.s(90), g_ui.s(28), hwnd, reinterpret_cast<HMENU>(IDC_BTN_BROWSE), nullptr, nullptr);
        SendMessageW(g_ui.btnBrowse, WM_SETFONT, reinterpret_cast<WPARAM>(g_ui.fontBody), TRUE);
        y += g_ui.s(42);

        HWND grpMode = CreateWindowExW(0, L"BUTTON", L"Mode Penggantian UI Windows",
                                       WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                       pad, y, w, g_ui.s(176), hwnd, nullptr, nullptr, nullptr);
        SendMessageW(grpMode, WM_SETFONT, reinterpret_cast<WPARAM>(g_ui.fontBold), TRUE);

        int gy = y + g_ui.s(26);
        g_ui.radNative = CreateWindowExW(0, L"BUTTON",
                                         L"Pengganti UI Windows Penuh - Winlogon Native Shell (Direkomendasikan)",
                                         WS_CHILD | WS_VISIBLE | WS_GROUP | BS_AUTORADIOBUTTON,
                                         pad + g_ui.s(16), gy, w - g_ui.s(32), g_ui.s(22),
                                         hwnd, reinterpret_cast<HMENU>(IDC_RAD_NATIVE), nullptr, nullptr);
        SendMessageW(g_ui.radNative, WM_SETFONT, reinterpret_cast<WPARAM>(g_ui.fontBold), TRUE);
        SendMessageW(g_ui.radNative, BM_SETCHECK, BST_CHECKED, 0);
        gy += g_ui.s(22);
        HWND desc1 = CreateWindowExW(0, L"STATIC",
                                     L"Menggantikan desktop explorer.exe secara penuh sejak login Windows lewat Winlogon\\Shell\n"
                                     L"dengan watchdog anti-black-screen SekaiPopBoot.exe (tahan Left Shift saat login / Ctrl+Alt+Shift+E).",
                                     WS_CHILD | WS_VISIBLE,
                                     pad + g_ui.s(34), gy, w - g_ui.s(48), g_ui.s(38), hwnd, nullptr, nullptr, nullptr);
        SendMessageW(desc1, WM_SETFONT, reinterpret_cast<WPARAM>(g_ui.fontBody), TRUE);
        gy += g_ui.s(48);

        g_ui.radSeamless = CreateWindowExW(0, L"BUTTON",
                                           L"Mode Seamless - Auto-Start + Sembunyikan Taskbar Windows",
                                           WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                                           pad + g_ui.s(16), gy, w - g_ui.s(32), g_ui.s(22),
                                           hwnd, reinterpret_cast<HMENU>(IDC_RAD_SEAMLESS), nullptr, nullptr);
        SendMessageW(g_ui.radSeamless, WM_SETFONT, reinterpret_cast<WPARAM>(g_ui.fontBold), TRUE);
        gy += g_ui.s(22);
        HWND desc2 = CreateWindowExW(0, L"STATIC",
                                     L"Berjalan otomatis saat login & menyembunyikan penuh Taskbar Windows sambil menjaga proses\n"
                                     L"latar explorer.exe tetap aktif. Mode dapat diubah kapan saja lewat SekaiPop Control Center.",
                                     WS_CHILD | WS_VISIBLE,
                                     pad + g_ui.s(34), gy, w - g_ui.s(48), g_ui.s(38), hwnd, nullptr, nullptr, nullptr);
        SendMessageW(desc2, WM_SETFONT, reinterpret_cast<WPARAM>(g_ui.fontBody), TRUE);

        y += g_ui.s(188);
        g_ui.chkStartMenu = CreateWindowExW(0, L"BUTTON",
                                            L"Buat pintasan Start Menu (SekaiPop, Control Center, Restore Explorer, Uninstall)",
                                            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                            pad, y, w, g_ui.s(22), hwnd, reinterpret_cast<HMENU>(IDC_CHK_STARTMENU), nullptr, nullptr);
        SendMessageW(g_ui.chkStartMenu, WM_SETFONT, reinterpret_cast<WPARAM>(g_ui.fontBody), TRUE);
        SendMessageW(g_ui.chkStartMenu, BM_SETCHECK, BST_CHECKED, 0);
        y += g_ui.s(26);

        g_ui.chkDesktop = CreateWindowExW(0, L"BUTTON",
                                          L"Buat pintasan SekaiPop Control Center di Desktop",
                                          WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                          pad, y, w, g_ui.s(22), hwnd, reinterpret_cast<HMENU>(IDC_CHK_DESKTOP), nullptr, nullptr);
        SendMessageW(g_ui.chkDesktop, WM_SETFONT, reinterpret_cast<WPARAM>(g_ui.fontBody), TRUE);
        SendMessageW(g_ui.chkDesktop, BM_SETCHECK, BST_CHECKED, 0);
        y += g_ui.s(26);

        g_ui.chkLaunch = CreateWindowExW(0, L"BUTTON",
                                         L"Langsung jalankan SekaiPop setelah instalasi selesai",
                                         WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                         pad, y, w, g_ui.s(22), hwnd, reinterpret_cast<HMENU>(IDC_CHK_LAUNCH), nullptr, nullptr);
        SendMessageW(g_ui.chkLaunch, WM_SETFONT, reinterpret_cast<WPARAM>(g_ui.fontBody), TRUE);
        SendMessageW(g_ui.chkLaunch, BM_SETCHECK, BST_CHECKED, 0);
        y += g_ui.s(34);

        g_ui.lblStatus = CreateWindowExW(0, L"STATIC", L"Siap menginstal SekaiPop Desktop Shell 1.0.0",
                                         WS_CHILD | WS_VISIBLE | SS_PATHELLIPSIS,
                                         pad, y, w, g_ui.s(20), hwnd, reinterpret_cast<HMENU>(IDC_STATUS), nullptr, nullptr);
        SendMessageW(g_ui.lblStatus, WM_SETFONT, reinterpret_cast<WPARAM>(g_ui.fontBody), TRUE);
        y += g_ui.s(24);

        g_ui.progress = CreateWindowExW(0, PROGRESS_CLASSW, nullptr,
                                        WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
                                        pad, y, w, g_ui.s(20), hwnd, reinterpret_cast<HMENU>(IDC_PROGRESS), nullptr, nullptr);
        SendMessageW(g_ui.progress, PBM_SETRANGE32, 0, 100);
        y += g_ui.s(34);

        g_ui.btnInstall = CreateWindowExW(0, L"BUTTON", L"Install Sekarang",
                                          WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                          pad + w - g_ui.s(266), y, g_ui.s(154), g_ui.s(36),
                                          hwnd, reinterpret_cast<HMENU>(IDC_BTN_INSTALL), nullptr, nullptr);
        SendMessageW(g_ui.btnInstall, WM_SETFONT, reinterpret_cast<WPARAM>(g_ui.fontBold), TRUE);

        g_ui.btnCancel = CreateWindowExW(0, L"BUTTON", L"Batal",
                                         WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                         pad + w - g_ui.s(104), y, g_ui.s(104), g_ui.s(36),
                                         hwnd, reinterpret_cast<HMENU>(IDC_BTN_CANCEL), nullptr, nullptr);
        SendMessageW(g_ui.btnCancel, WM_SETFONT, reinterpret_cast<WPARAM>(g_ui.fontBody), TRUE);
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps = {};
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc = {};
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, g_ui.bgBrush);

        const int hdrH = g_ui.s(76);
        RECT hdr = {0, 0, rc.right, hdrH};
        HBRUSH navy = CreateSolidBrush(RGB(22, 34, 58));
        FillRect(hdc, &hdr, navy);
        DeleteObject(navy);

        RECT accent = {0, hdrH - g_ui.s(3), rc.right, hdrH};
        HBRUSH mint = CreateSolidBrush(RGB(94, 234, 212));
        FillRect(hdc, &accent, mint);
        DeleteObject(mint);

        if (g_ui.appIcon) {
            DrawIconEx(hdc, g_ui.s(20), g_ui.s(17), g_ui.appIcon, g_ui.s(42), g_ui.s(42), 0, nullptr, DI_NORMAL);
        }

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(255, 255, 255));
        HGDIOBJ oldF = SelectObject(hdc, g_ui.fontTitle);
        RECT tRect = {g_ui.s(74), g_ui.s(14), rc.right - g_ui.s(20), g_ui.s(44)};
        DrawTextW(hdc, L"SekaiPop Desktop Shell 1.0 Setup", -1, &tRect, DT_LEFT | DT_SINGLELINE);

        SetTextColor(hdc, RGB(180, 212, 230));
        SelectObject(hdc, g_ui.fontSub);
        RECT sRect = {g_ui.s(74), g_ui.s(44), rc.right - g_ui.s(20), g_ui.s(66)};
        DrawTextW(hdc, L"Installer Mandiri Native Windows - Pengganti UI Desktop Penuh Tanpa Terminal", -1, &sRect, DT_LEFT | DT_SINGLELINE);
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

    case WM_APP_PROGRESS: {
        SendMessageW(g_ui.progress, PBM_SETPOS, wParam, 0);
        auto* txt = reinterpret_cast<std::wstring*>(lParam);
        if (txt) {
            SetWindowTextW(g_ui.lblStatus, txt->c_str());
            delete txt;
        }
        return 0;
    }

    case WM_APP_DONE: {
        g_ui.installing.store(false);
        auto* res = reinterpret_cast<InstallOutcome*>(lParam);
        if (!res) return 0;

        if (!res->ok) {
            setControlsEnabled(true);
            SendMessageW(g_ui.progress, PBM_SETPOS, 0, 0);
            SetWindowTextW(g_ui.lblStatus, L"Instalasi gagal.");
            MessageBoxW(hwnd, res->errorMsg.c_str(), L"Instalasi Gagal", MB_OK | MB_ICONERROR);
            delete res;
            return 0;
        }

        g_ui.installDone = true;
        g_ui.installedMode = res->mode;
        g_ui.installedDir = res->dir;
        SendMessageW(g_ui.progress, PBM_SETPOS, 100, 0);
        SetWindowTextW(g_ui.lblStatus, L"SekaiPop Desktop Shell berhasil diinstal!");
        SetWindowTextW(g_ui.btnCancel, L"Selesai");
        EnableWindow(g_ui.btnCancel, TRUE);

        if (res->launchAfter) {
            if (res->mode == ShellMode::NativeWinlogonShell) {
                switchLiveToNativeShell(res->dir);
            } else {
                launchAsDesktopUser(res->dir + L"\\SekaiPopShell.exe", L"", res->dir);
            }
        }

        std::wstring doneMsg = L"SekaiPop Desktop Shell 1.0 berhasil diinstal ke:\n" + res->dir + L"\n\n";
        if (res->mode == ShellMode::NativeWinlogonShell) {
            doneMsg += L"Mode Pengganti UI Penuh (Winlogon Native Shell) telah aktif sepenuhnya:\n"
                       L"- explorer.exe telah digantikan oleh SekaiPopBoot.exe (juga otomatis setiap Restart/Login Windows).\n"
                       L"- Pengaman darurat: tahan Left Shift saat login atau tekan Ctrl + Alt + Shift + E kapan saja.\n"
                       L"- Ganti mode atau Uninstall kapan saja lewat Start Menu -> SekaiPop Control Center.";
        } else {
            doneMsg += L"Mode Seamless (Auto-Start + Sembunyikan Taskbar) telah diaktifkan.\n"
                       L"Anda dapat mengubah mode atau Uninstall kapan saja lewat Start Menu -> SekaiPop Control Center.";
        }
        MessageBoxW(hwnd, doneMsg.c_str(), L"Instalasi SekaiPop Berhasil", MB_OK | MB_ICONINFORMATION);
        delete res;
        DestroyWindow(hwnd);
        return 0;
    }

    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        if (id == IDC_BTN_BROWSE) {
            browseInstallFolder();
            return 0;
        }
        if (id == IDC_BTN_INSTALL) {
            startInstallWorker();
            return 0;
        }
        if (id == IDC_BTN_CANCEL || id == IDCANCEL) {
            if (!g_ui.installing.load()) {
                DestroyWindow(hwnd);
            }
            return 0;
        }
        break;
    }

    case WM_CLOSE:
        if (g_ui.installing.load()) {
            return 0;
        }
        DestroyWindow(hwnd);
        return 0;

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

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc >= 2) {
        const std::wstring arg1 = toLowerW(argv[1]);
        if (arg1 == L"--verify-payload") {
            EmbeddedCabFooter footer = {};
            const bool ok = readEmbeddedFooter(selfExePath(), footer);
            LocalFree(argv);
            CoUninitialize();
            return ok ? 0 : 2;
        }
        if (arg1 == L"--extract-only" && argc >= 3) {
            const std::wstring dst = argv[2];
            uint64_t unpacked = 0;
            std::wstring err;
            const bool ok = unpackPayloadToDir(selfExePath(), dst, nullptr, unpacked, err);
            LocalFree(argv);
            CoUninitialize();
            return ok ? 0 : 3;
        }
    }
    if (argv) {
        LocalFree(argv);
    }

    g_ui.appIcon = LoadIconW(hInst, MAKEINTRESOURCEW(1));

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &setupWndProc;
    wc.hInstance = hInst;
    wc.hIcon = g_ui.appIcon;
    wc.hIconSm = g_ui.appIcon;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"SekaiPopSetupWnd";
    RegisterClassExW(&wc);

    const int sysDpi = querySystemDpi();
    const int winW = MulDiv(636, sysDpi, 96);
    const int winH = MulDiv(566, sysDpi, 96);
    const int scrW = GetSystemMetrics(SM_CXSCREEN);
    const int scrH = GetSystemMetrics(SM_CYSCREEN);

    HWND hwnd = CreateWindowExW(
        0,
        wc.lpszClassName,
        L"SekaiPop Desktop Shell 1.0 Setup",
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
