#include "ScreenshotOverlay.h"

#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QStandardPaths>
#include <QString>

#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace SekaiPop::Win32 {

namespace {

struct CapturedDesktop {
    int vx = 0;
    int vy = 0;
    int vw = 0;
    int vh = 0;
    HDC origDC = nullptr;
    HBITMAP origBmp = nullptr;
    HGDIOBJ origOld = nullptr;
    uint32_t* origBits = nullptr;

    HDC dimDC = nullptr;
    HBITMAP dimBmp = nullptr;
    HGDIOBJ dimOld = nullptr;
    uint32_t* dimBits = nullptr;

    HDC backDC = nullptr;
    HBITMAP backBmp = nullptr;
    HGDIOBJ backOld = nullptr;

    bool capture(bool buildDimAndBack) {
        release();
        vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
        vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
        vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        if (vw <= 0 || vh <= 0) {
            return false;
        }

        HDC screenDC = GetDC(nullptr);
        if (!screenDC) {
            return false;
        }

        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = vw;
        bmi.bmiHeader.biHeight = -vh; // top-down BGRA32
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        origDC = CreateCompatibleDC(screenDC);
        origBmp = CreateDIBSection(screenDC, &bmi, DIB_RGB_COLORS, reinterpret_cast<void**>(&origBits), nullptr, 0);
        if (!origDC || !origBmp || !origBits) {
            ReleaseDC(nullptr, screenDC);
            release();
            return false;
        }
        origOld = SelectObject(origDC, origBmp);
        BitBlt(origDC, 0, 0, vw, vh, screenDC, vx, vy, SRCCOPY | CAPTUREBLT);

        if (buildDimAndBack) {
            dimDC = CreateCompatibleDC(screenDC);
            dimBmp = CreateDIBSection(screenDC, &bmi, DIB_RGB_COLORS, reinterpret_cast<void**>(&dimBits), nullptr, 0);
            backDC = CreateCompatibleDC(screenDC);
            backBmp = CreateCompatibleBitmap(screenDC, vw, vh);
            if (!dimDC || !dimBmp || !dimBits || !backDC || !backBmp) {
                ReleaseDC(nullptr, screenDC);
                release();
                return false;
            }
            dimOld = SelectObject(dimDC, dimBmp);
            backOld = SelectObject(backDC, backBmp);

            // Fast integer ~45% dark scrim blended toward SekaiPop deep navy (10, 16, 30)
            const size_t totalPx = static_cast<size_t>(vw) * static_cast<size_t>(vh);
            for (size_t i = 0; i < totalPx; ++i) {
                const uint32_t px = origBits[i];
                const uint32_t b = (((px & 0xFFu) * 110u) >> 8) + 16u;
                const uint32_t g = ((((px >> 8) & 0xFFu) * 110u) >> 8) + 9u;
                const uint32_t r = ((((px >> 16) & 0xFFu) * 110u) >> 8) + 6u;
                dimBits[i] = 0xFF000000u | (r << 16) | (g << 8) | b;
            }
        }

        ReleaseDC(nullptr, screenDC);
        return true;
    }

    QImage cropImage(int x1, int y1, int x2, int y2) const {
        if (!origBits || vw <= 0 || vh <= 0) {
            return {};
        }
        const int lx1 = std::clamp(std::min(x1, x2), 0, vw);
        const int ly1 = std::clamp(std::min(y1, y2), 0, vh);
        const int lx2 = std::clamp(std::max(x1, x2), 0, vw);
        const int ly2 = std::clamp(std::max(y1, y2), 0, vh);
        const int cw = lx2 - lx1;
        const int ch = ly2 - ly1;
        if (cw <= 0 || ch <= 0) {
            return {};
        }

        QImage out(cw, ch, QImage::Format_RGB32);
        for (int row = 0; row < ch; ++row) {
            const uint32_t* srcRow = origBits + static_cast<size_t>(ly1 + row) * static_cast<size_t>(vw) + lx1;
            auto* dstRow = reinterpret_cast<uint32_t*>(out.scanLine(row));
            for (int col = 0; col < cw; ++col) {
                dstRow[col] = srcRow[col] | 0xFF000000u;
            }
        }
        return out;
    }

    void release() {
        if (backDC) {
            if (backOld) SelectObject(backDC, backOld);
            DeleteDC(backDC);
            backDC = nullptr;
            backOld = nullptr;
        }
        if (backBmp) { DeleteObject(backBmp); backBmp = nullptr; }

        if (dimDC) {
            if (dimOld) SelectObject(dimDC, dimOld);
            DeleteDC(dimDC);
            dimDC = nullptr;
            dimOld = nullptr;
        }
        if (dimBmp) { DeleteObject(dimBmp); dimBmp = nullptr; }
        dimBits = nullptr;

        if (origDC) {
            if (origOld) SelectObject(origDC, origOld);
            DeleteDC(origDC);
            origDC = nullptr;
            origOld = nullptr;
        }
        if (origBmp) { DeleteObject(origBmp); origBmp = nullptr; }
        origBits = nullptr;
        vw = 0;
        vh = 0;
    }

    ~CapturedDesktop() {
        release();
    }
};

// ============================================================================
// Sleek Non-Activating Toast Notification (bottom-right preview + click to open)
// ============================================================================
struct ToastState {
    HWND hwnd = nullptr;
    HBITMAP thumbBmp = nullptr;
    int thumbW = 0;
    int thumbH = 0;
    std::wstring title;
    std::wstring subtitle;
    std::wstring savedPath;
    HFONT fontBold = nullptr;
    HFONT fontSmall = nullptr;
    int dpi = 96;
};

ToastState* g_toast = nullptr;

int queryDpiAtMonitor(HMONITOR hMon) {
    if (hMon) {
        using GetDpiForMonFn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
        if (HMODULE shcore = LoadLibraryW(L"shcore.dll")) {
            auto fn = reinterpret_cast<GetDpiForMonFn>(GetProcAddress(shcore, "GetDpiForMonitor"));
            UINT dx = 96, dy = 96;
            bool ok = (fn && SUCCEEDED(fn(hMon, 0 /*MDT_EFFECTIVE_DPI*/, &dx, &dy)) && dy > 0);
            FreeLibrary(shcore);
            if (ok) return static_cast<int>(dy);
        }
    }
    HDC hdc = GetDC(nullptr);
    int d = hdc ? GetDeviceCaps(hdc, LOGPIXELSY) : 96;
    if (hdc) ReleaseDC(nullptr, hdc);
    return d > 0 ? d : 96;
}

void destroyActiveToast() {
    if (g_toast && g_toast->hwnd && IsWindow(g_toast->hwnd)) {
        DestroyWindow(g_toast->hwnd);
    }
}

LRESULT CALLBACK toastWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;

    case WM_LBUTTONUP: {
        if (g_toast && !g_toast->savedPath.empty()) {
            std::wstring arg = L"/select,\"" + g_toast->savedPath + L"\"";
            ShellExecuteW(nullptr, L"open", L"explorer.exe", arg.c_str(), nullptr, SW_SHOWNORMAL);
        }
        DestroyWindow(hwnd);
        return 0;
    }

    case WM_RBUTTONUP:
        DestroyWindow(hwnd);
        return 0;

    case WM_TIMER:
        if (wParam == 1) {
            KillTimer(hwnd, 1);
            DestroyWindow(hwnd);
        }
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps = {};
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc = {};
        GetClientRect(hwnd, &rc);
        const int dpi = g_toast ? g_toast->dpi : 96;
        auto sc = [dpi](int v) { return MulDiv(v, dpi, 96); };

        HBRUSH bg = CreateSolidBrush(RGB(22, 34, 58));
        HPEN border = CreatePen(PS_SOLID, sc(2), RGB(94, 234, 212));
        HGDIOBJ oldB = SelectObject(hdc, bg);
        HGDIOBJ oldP = SelectObject(hdc, border);
        RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, sc(14), sc(14));
        SelectObject(hdc, oldP);
        SelectObject(hdc, oldB);
        DeleteObject(border);
        DeleteObject(bg);

        int textLeft = sc(14);
        if (g_toast && g_toast->thumbBmp && g_toast->thumbW > 0 && g_toast->thumbH > 0) {
            HDC memDC = CreateCompatibleDC(hdc);
            HGDIOBJ oldBmp = SelectObject(memDC, g_toast->thumbBmp);
            const int tx = sc(12);
            const int ty = (rc.bottom - g_toast->thumbH) / 2;
            BitBlt(hdc, tx, ty, g_toast->thumbW, g_toast->thumbH, memDC, 0, 0, SRCCOPY);
            SelectObject(memDC, oldBmp);
            DeleteDC(memDC);

            HPEN tbPen = CreatePen(PS_SOLID, 1, RGB(94, 234, 212));
            HGDIOBJ op = SelectObject(hdc, tbPen);
            HGDIOBJ ob = SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Rectangle(hdc, tx, ty, tx + g_toast->thumbW, ty + g_toast->thumbH);
            SelectObject(hdc, ob);
            SelectObject(hdc, op);
            DeleteObject(tbPen);

            textLeft = tx + g_toast->thumbW + sc(12);
        }

        if (g_toast) {
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(255, 255, 255));
            HGDIOBJ oldF = SelectObject(hdc, g_toast->fontBold);
            RECT tr = {textLeft, sc(14), rc.right - sc(12), sc(36)};
            DrawTextW(hdc, g_toast->title.c_str(), -1, &tr, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

            SetTextColor(hdc, RGB(180, 216, 230));
            SelectObject(hdc, g_toast->fontSmall);
            RECT sr = {textLeft, sc(38), rc.right - sc(12), rc.bottom - sc(10)};
            DrawTextW(hdc, g_toast->subtitle.c_str(), -1, &sr, DT_LEFT | DT_WORDBREAK);
            SelectObject(hdc, oldF);
        }

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DESTROY:
        if (g_toast) {
            if (g_toast->thumbBmp) DeleteObject(g_toast->thumbBmp);
            if (g_toast->fontBold) DeleteObject(g_toast->fontBold);
            if (g_toast->fontSmall) DeleteObject(g_toast->fontSmall);
            delete g_toast;
            g_toast = nullptr;
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void showCaptureToast(const QImage& img, const QString& savedPath, const POINT& anchorPt) {
    destroyActiveToast();

    HMONITOR hMon = MonitorFromPoint(anchorPt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(hMon, &mi)) {
        return;
    }
    const int dpi = queryDpiAtMonitor(hMon);
    auto sc = [dpi](int v) { return MulDiv(v, dpi, 96); };

    static bool regDone = false;
    if (!regDone) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &toastWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_HAND);
        wc.lpszClassName = L"SekaiPopScreenshotToast";
        RegisterClassExW(&wc);
        regDone = true;
    }

    auto* st = new ToastState();
    st->dpi = dpi;
    st->savedPath = QDir::toNativeSeparators(savedPath).toStdWString();
    st->title = L"Screenshot tersalin ke Clipboard (" + std::to_wstring(img.width()) + L" x " + std::to_wstring(img.height()) + L")";
    st->subtitle = savedPath.isEmpty()
        ? L"Tekan Ctrl+V untuk menempel langsung."
        : L"Tersimpan di Pictures\\Screenshots. Klik di sini untuk membuka folder.";
    st->fontBold = CreateFontW(-sc(13), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    st->fontSmall = CreateFontW(-sc(11), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

    // Build crisp thumbnail HBITMAP
    const QImage thumb = img.scaled(sc(96), sc(60), Qt::KeepAspectRatio, Qt::SmoothTransformation)
                            .convertToFormat(QImage::Format_RGB32);
    if (!thumb.isNull() && thumb.width() > 0 && thumb.height() > 0) {
        HDC sdc = GetDC(nullptr);
        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = thumb.width();
        bmi.bmiHeader.biHeight = -thumb.height();
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        st->thumbBmp = CreateDIBSection(sdc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (st->thumbBmp && bits) {
            for (int y = 0; y < thumb.height(); ++y) {
                std::memcpy(static_cast<uint8_t*>(bits) + static_cast<size_t>(y) * static_cast<size_t>(thumb.width()) * 4,
                            thumb.constScanLine(y),
                            static_cast<size_t>(thumb.width()) * 4);
            }
            st->thumbW = thumb.width();
            st->thumbH = thumb.height();
        }
        if (sdc) ReleaseDC(nullptr, sdc);
    }

    const int toastW = sc(372);
    const int toastH = sc(86);
    const int margin = sc(24);
    const int x = mi.rcWork.right - toastW - margin;
    const int y = mi.rcWork.bottom - toastH - margin;

    g_toast = st;
    HWND hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"SekaiPopScreenshotToast",
        L"",
        WS_POPUP,
        x, y, toastW, toastH,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr
    );
    if (!hwnd) {
        delete st;
        g_toast = nullptr;
        return;
    }
    st->hwnd = hwnd;

    // Rounded window region matching the painted rounded rectangle
    HRGN rgn = CreateRoundRectRgn(0, 0, toastW + 1, toastH + 1, sc(14), sc(14));
    SetWindowRgn(hwnd, rgn, TRUE);

    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    SetTimer(hwnd, 1, 3800, nullptr);
}

void finalizeScreenshot(const QImage& img, const POINT& anchorPt) {
    if (img.isNull() || img.width() <= 0 || img.height() <= 0) {
        return;
    }
    if (QClipboard* cb = QGuiApplication::clipboard()) {
        cb->setImage(img);
    }

    QString savedPath;
    QString picDir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (picDir.isEmpty()) {
        picDir = QDir::homePath() + QStringLiteral("/Pictures");
    }
    const QString ssDir = QDir(picDir).filePath(QStringLiteral("Screenshots"));
    if (QDir().mkpath(ssDir)) {
        const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss"));
        QString candidate = QDir(ssDir).filePath(QStringLiteral("Screenshot_%1.png").arg(stamp));
        int suffix = 1;
        while (QFileInfo::exists(candidate) && suffix < 100) {
            candidate = QDir(ssDir).filePath(QStringLiteral("Screenshot_%1_%2.png").arg(stamp).arg(suffix++));
        }
        if (img.save(candidate, "PNG")) {
            savedPath = candidate;
            qInfo("[Screenshot] Captured %dx%d -> clipboard + %s", img.width(), img.height(), qPrintable(savedPath));
        }
    }
    if (savedPath.isEmpty()) {
        qInfo("[Screenshot] Captured %dx%d -> clipboard", img.width(), img.height());
    }

    showCaptureToast(img, savedPath, anchorPt);
}

// ============================================================================
// Interactive Multi-Monitor Physical-Pixel Snip Overlay
// ============================================================================
struct ToolbarBtn {
    RECT rc = {};
    const wchar_t* label = L"";
    int action = 0; // 1 = area (hint), 2 = active monitor, 3 = all monitors, 4 = cancel
};

struct SnipSession {
    HWND hwnd = nullptr;
    CapturedDesktop desktop;
    RECT activeMonLocal = {};
    int dpi = 96;
    HFONT fontUi = nullptr;
    HFONT fontSmall = nullptr;

    RECT toolbarRc = {};
    ToolbarBtn btns[4];
    int hoverBtn = -1;

    bool dragging = false;
    POINT dragStart = {};
    POINT dragCur = {};
    POINT cursorAnchor = {};

    int sc(int v) const { return MulDiv(v, dpi, 96); }

    void layoutToolbar() {
        const int monW = activeMonLocal.right - activeMonLocal.left;
        const int barW = sc(390);
        const int barH = sc(40);
        const int bx = activeMonLocal.left + (monW - barW) / 2;
        const int by = activeMonLocal.top + sc(18);
        toolbarRc = {bx, by, bx + barW, by + barH};

        const int pad = sc(6);
        const int gap = sc(6);
        const int widths[4] = {sc(78), sc(92), sc(108), sc(82)};
        const wchar_t* labels[4] = {L"Area", L"Layar Ini", L"Semua Layar", L"Batal (Esc)"};
        int cx = bx + pad;
        for (int i = 0; i < 4; ++i) {
            btns[i].rc = {cx, by + pad, cx + widths[i], by + barH - pad};
            btns[i].label = labels[i];
            btns[i].action = i + 1;
            cx += widths[i] + gap;
        }
    }

    int hitTestButton(int x, int y) const {
        POINT pt{x, y};
        for (int i = 0; i < 4; ++i) {
            if (PtInRect(&btns[i].rc, pt)) {
                return i;
            }
        }
        return -1;
    }

    void paintTo(HDC dstDC) {
        if (!desktop.backDC || !desktop.dimDC || !desktop.origDC) {
            return;
        }
        HDC dc = desktop.backDC;
        // 1. Start with darkened frozen virtual desktop
        BitBlt(dc, 0, 0, desktop.vw, desktop.vh, desktop.dimDC, 0, 0, SRCCOPY);

        // 2. If user is dragging a selection, reveal crisp full-bright pixels inside selection rect
        if (dragging) {
            const int x1 = std::min(dragStart.x, dragCur.x);
            const int y1 = std::min(dragStart.y, dragCur.y);
            const int x2 = std::max(dragStart.x, dragCur.x);
            const int y2 = std::max(dragStart.y, dragCur.y);
            const int sw = x2 - x1;
            const int sh = y2 - y1;
            if (sw > 0 && sh > 0) {
                BitBlt(dc, x1, y1, sw, sh, desktop.origDC, x1, y1, SRCCOPY);

                // Crisp mint/cyan selection border
                HPEN pen = CreatePen(PS_SOLID, std::max(2, sc(2)), RGB(94, 234, 212));
                HGDIOBJ op = SelectObject(dc, pen);
                HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
                Rectangle(dc, x1, y1, x2, y2);
                SelectObject(dc, ob);
                SelectObject(dc, op);
                DeleteObject(pen);

                // Dimension pill (e.g. "1280 x 720")
                const std::wstring dimTxt = std::to_wstring(sw) + L" x " + std::to_wstring(sh);
                HGDIOBJ of = SelectObject(dc, fontSmall);
                SIZE tsz = {};
                GetTextExtentPoint32W(dc, dimTxt.c_str(), static_cast<int>(dimTxt.size()), &tsz);
                const int pw = tsz.cx + sc(16);
                const int ph = tsz.cy + sc(8);
                int px = x1;
                int py = y1 - ph - sc(8);
                if (py < 6) py = y2 + sc(8);
                if (py + ph > desktop.vh - 6) py = y1 + sc(8);
                if (px + pw > desktop.vw - 6) px = desktop.vw - pw - 6;

                HBRUSH pb = CreateSolidBrush(RGB(22, 34, 58));
                HPEN pp = CreatePen(PS_SOLID, 1, RGB(94, 234, 212));
                HGDIOBJ oldB = SelectObject(dc, pb);
                HGDIOBJ oldP = SelectObject(dc, pp);
                RoundRect(dc, px, py, px + pw, py + ph, sc(8), sc(8));
                SelectObject(dc, oldP);
                SelectObject(dc, oldB);
                DeleteObject(pp);
                DeleteObject(pb);

                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, RGB(240, 253, 250));
                RECT tr = {px, py, px + pw, py + ph};
                DrawTextW(dc, dimTxt.c_str(), -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                SelectObject(dc, of);
            }
        }

        // 3. Draw floating SekaiPop toolbar (hidden while actively dragging near it)
        if (!dragging) {
            HBRUSH tbBg = CreateSolidBrush(RGB(22, 34, 58));
            HPEN tbPen = CreatePen(PS_SOLID, std::max(1, sc(2)), RGB(94, 234, 212));
            HGDIOBJ ob = SelectObject(dc, tbBg);
            HGDIOBJ op = SelectObject(dc, tbPen);
            RoundRect(dc, toolbarRc.left, toolbarRc.top, toolbarRc.right, toolbarRc.bottom, sc(12), sc(12));
            SelectObject(dc, op);
            SelectObject(dc, ob);
            DeleteObject(tbPen);
            DeleteObject(tbBg);

            HGDIOBJ of = SelectObject(dc, fontUi);
            SetBkMode(dc, TRANSPARENT);
            for (int i = 0; i < 4; ++i) {
                const bool isActive = (i == 0);
                const bool isHover = (i == hoverBtn);
                COLORREF fill = isActive ? RGB(94, 234, 212) : (isHover ? RGB(42, 58, 92) : RGB(30, 44, 72));
                COLORREF textCol = isActive ? RGB(15, 23, 42) : RGB(241, 245, 249);
                if (i == 3 && isHover) {
                    fill = RGB(185, 28, 28);
                }
                HBRUSH bb = CreateSolidBrush(fill);
                HPEN bp = CreatePen(PS_NULL, 0, 0);
                HGDIOBJ o1 = SelectObject(dc, bb);
                HGDIOBJ o2 = SelectObject(dc, bp);
                RoundRect(dc, btns[i].rc.left, btns[i].rc.top, btns[i].rc.right, btns[i].rc.bottom, sc(8), sc(8));
                SelectObject(dc, o2);
                SelectObject(dc, o1);
                DeleteObject(bp);
                DeleteObject(bb);

                SetTextColor(dc, textCol);
                RECT tr = btns[i].rc;
                DrawTextW(dc, btns[i].label, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
            SelectObject(dc, of);
        }

        BitBlt(dstDC, 0, 0, desktop.vw, desktop.vh, dc, 0, 0, SRCCOPY);
    }
};

SnipSession* g_snip = nullptr;

void finishAndCloseSnip(const QImage& cropped, const POINT& anchorPt) {
    HWND hwnd = g_snip ? g_snip->hwnd : nullptr;
    if (hwnd && IsWindow(hwnd)) {
        DestroyWindow(hwnd);
    }
    if (!cropped.isNull()) {
        finalizeScreenshot(cropped, anchorPt);
    }
}

LRESULT CALLBACK snipWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;

    case WM_SETCURSOR: {
        if (g_snip && !g_snip->dragging) {
            POINT pt = {};
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            if (g_snip->hitTestButton(pt.x, pt.y) >= 0) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
        }
        SetCursor(LoadCursorW(nullptr, IDC_CROSS));
        return TRUE;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps = {};
        HDC hdc = BeginPaint(hwnd, &ps);
        if (g_snip) {
            g_snip->paintTo(hdc);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_KEYDOWN: {
        if (wParam == VK_ESCAPE) {
            DestroyWindow(hwnd);
            return 0;
        }
        if (wParam == VK_RETURN && g_snip) {
            const RECT& m = g_snip->activeMonLocal;
            QImage img = g_snip->desktop.cropImage(m.left, m.top, m.right, m.bottom);
            POINT anchor = g_snip->cursorAnchor;
            finishAndCloseSnip(img, anchor);
            return 0;
        }
        break;
    }

    case WM_RBUTTONDOWN:
        DestroyWindow(hwnd);
        return 0;

    case WM_LBUTTONDOWN: {
        if (!g_snip) return 0;
        const int x = static_cast<short>(LOWORD(lParam));
        const int y = static_cast<short>(HIWORD(lParam));
        const int btnIdx = g_snip->hitTestButton(x, y);
        if (btnIdx >= 0) {
            const int act = g_snip->btns[btnIdx].action;
            if (act == 2) {
                // Active monitor
                const RECT& m = g_snip->activeMonLocal;
                QImage img = g_snip->desktop.cropImage(m.left, m.top, m.right, m.bottom);
                POINT anchor = g_snip->cursorAnchor;
                finishAndCloseSnip(img, anchor);
                return 0;
            }
            if (act == 3) {
                // All monitors
                QImage img = g_snip->desktop.cropImage(0, 0, g_snip->desktop.vw, g_snip->desktop.vh);
                POINT anchor = g_snip->cursorAnchor;
                finishAndCloseSnip(img, anchor);
                return 0;
            }
            if (act == 4) {
                DestroyWindow(hwnd);
                return 0;
            }
            return 0;
        }

        g_snip->dragging = true;
        g_snip->dragStart = {x, y};
        g_snip->dragCur = {x, y};
        SetCapture(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (!g_snip) return 0;
        const int x = static_cast<short>(LOWORD(lParam));
        const int y = static_cast<short>(HIWORD(lParam));
        if (g_snip->dragging) {
            g_snip->dragCur = {x, y};
            InvalidateRect(hwnd, nullptr, FALSE);
        } else {
            const int hov = g_snip->hitTestButton(x, y);
            if (hov != g_snip->hoverBtn) {
                g_snip->hoverBtn = hov;
                InvalidateRect(hwnd, &g_snip->toolbarRc, FALSE);
            }
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        if (!g_snip || !g_snip->dragging) return 0;
        ReleaseCapture();
        g_snip->dragging = false;
        const int x = static_cast<short>(LOWORD(lParam));
        const int y = static_cast<short>(HIWORD(lParam));
        g_snip->dragCur = {x, y};

        const int w = std::abs(g_snip->dragCur.x - g_snip->dragStart.x);
        const int h = std::abs(g_snip->dragCur.y - g_snip->dragStart.y);
        if (w >= 4 && h >= 4) {
            QImage cropped = g_snip->desktop.cropImage(
                g_snip->dragStart.x, g_snip->dragStart.y,
                g_snip->dragCur.x, g_snip->dragCur.y);
            POINT anchor = {
                g_snip->desktop.vx + (g_snip->dragStart.x + g_snip->dragCur.x) / 2,
                g_snip->desktop.vy + (g_snip->dragStart.y + g_snip->dragCur.y) / 2
            };
            finishAndCloseSnip(cropped, anchor);
        } else {
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_DESTROY:
        if (g_snip) {
            if (g_snip->fontUi) DeleteObject(g_snip->fontUi);
            if (g_snip->fontSmall) DeleteObject(g_snip->fontSmall);
            g_snip->desktop.release();
            delete g_snip;
            g_snip = nullptr;
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

void ScreenshotOverlay::startRegionSnip() {
    if (g_snip && g_snip->hwnd && IsWindow(g_snip->hwnd)) {
        SetForegroundWindow(g_snip->hwnd);
        return;
    }

    // Dismiss any lingering capture toast so it never appears inside the new screenshot.
    destroyActiveToast();

    auto* sess = new SnipSession();
    if (!sess->desktop.capture(true)) {
        delete sess;
        return;
    }

    POINT cur = {};
    GetCursorPos(&cur);
    sess->cursorAnchor = cur;
    HMONITOR hMon = MonitorFromPoint(cur, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(hMon, &mi)) {
        sess->activeMonLocal = {
            mi.rcMonitor.left - sess->desktop.vx,
            mi.rcMonitor.top - sess->desktop.vy,
            mi.rcMonitor.right - sess->desktop.vx,
            mi.rcMonitor.bottom - sess->desktop.vy
        };
    } else {
        sess->activeMonLocal = {0, 0, sess->desktop.vw, sess->desktop.vh};
    }
    sess->dpi = queryDpiAtMonitor(hMon);
    sess->fontUi = CreateFontW(-sess->sc(13), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    sess->fontSmall = CreateFontW(-sess->sc(12), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                  DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    sess->layoutToolbar();

    static bool regDone = false;
    if (!regDone) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &snipWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_CROSS);
        wc.lpszClassName = L"SekaiPopScreenshotOverlay";
        RegisterClassExW(&wc);
        regDone = true;
    }

    g_snip = sess;
    HWND hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"SekaiPopScreenshotOverlay",
        L"SekaiPop Screenshot",
        WS_POPUP,
        sess->desktop.vx, sess->desktop.vy,
        sess->desktop.vw, sess->desktop.vh,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr
    );
    if (!hwnd) {
        if (sess->fontUi) DeleteObject(sess->fontUi);
        if (sess->fontSmall) DeleteObject(sess->fontSmall);
        delete sess;
        g_snip = nullptr;
        return;
    }
    sess->hwnd = hwnd;
    ShowWindow(hwnd, SW_SHOW);
    SetWindowPos(hwnd, HWND_TOPMOST,
                 sess->desktop.vx, sess->desktop.vy,
                 sess->desktop.vw, sess->desktop.vh,
                 SWP_SHOWWINDOW);
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);
}

void ScreenshotOverlay::captureActiveMonitor() {
    if (g_snip) return;
    destroyActiveToast();

    CapturedDesktop desk;
    if (!desk.capture(false)) return;

    POINT cur = {};
    GetCursorPos(&cur);
    HMONITOR hMon = MonitorFromPoint(cur, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(hMon, &mi)) return;

    const QImage img = desk.cropImage(
        mi.rcMonitor.left - desk.vx,
        mi.rcMonitor.top - desk.vy,
        mi.rcMonitor.right - desk.vx,
        mi.rcMonitor.bottom - desk.vy);
    finalizeScreenshot(img, cur);
}

void ScreenshotOverlay::captureActiveWindow() {
    if (g_snip) return;
    destroyActiveToast();

    HWND fg = GetForegroundWindow();
    if (!fg || !IsWindow(fg)) {
        captureActiveMonitor();
        return;
    }
    RECT wr = {};
    if (FAILED(DwmGetWindowAttribute(fg, DWMWA_EXTENDED_FRAME_BOUNDS, &wr, sizeof(wr)))) {
        if (!GetWindowRect(fg, &wr)) {
            captureActiveMonitor();
            return;
        }
    }
    CapturedDesktop desk;
    if (!desk.capture(false)) return;

    const QImage img = desk.cropImage(
        wr.left - desk.vx,
        wr.top - desk.vy,
        wr.right - desk.vx,
        wr.bottom - desk.vy);
    POINT anchor{(wr.left + wr.right) / 2, (wr.top + wr.bottom) / 2};
    finalizeScreenshot(img, anchor);
}

void ScreenshotOverlay::captureAllMonitors() {
    if (g_snip) return;
    destroyActiveToast();

    CapturedDesktop desk;
    if (!desk.capture(false)) return;

    POINT cur = {};
    GetCursorPos(&cur);
    const QImage img = desk.cropImage(0, 0, desk.vw, desk.vh);
    finalizeScreenshot(img, cur);
}

} // namespace SekaiPop::Win32
