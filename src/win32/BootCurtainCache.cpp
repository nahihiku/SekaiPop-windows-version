#include "BootCurtainCache.h"
#include "../quickshell/QsWindowing.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QScreen>
#include <QStandardPaths>
#include <QTextStream>
#include <algorithm>
#include <cstdint>
#include <vector>
#include <windows.h>

namespace SekaiPop::Win32 {

namespace {

struct PhysMonitorInfo {
    HMONITOR hmon = nullptr;
    RECT rect = {};
    int index = 0;
};

BOOL CALLBACK enumPhysMonitorsProc(HMONITOR hmon, HDC, LPRECT, LPARAM lParam) {
    auto* list = reinterpret_cast<std::vector<PhysMonitorInfo>*>(lParam);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(hmon, &mi)) {
        PhysMonitorInfo info;
        info.hmon = hmon;
        info.rect = mi.rcMonitor;
        info.index = static_cast<int>(list->size());
        list->push_back(info);
    }
    return TRUE;
}

// Writes an uncompressed 32-bit BI_RGB BMP (top-down, negative biHeight) atomically.
// Top-down 32bpp BGRA matches QImage::Format_ARGB32 / Format_RGB32 memory layout directly,
// allowing SekaiPopBoot.exe to ReadFile directly into a CreateDIBSection buffer in ~2-4ms
// with zero decoding and zero row flipping.
bool writeFastBmp32Atomic(const QString& finalPath, const QImage& img) {
    if (img.isNull() || img.width() <= 0 || img.height() <= 0) {
        return false;
    }
    const QImage bgra = (img.format() == QImage::Format_RGB32 || img.format() == QImage::Format_ARGB32)
        ? img
        : img.convertToFormat(QImage::Format_RGB32);

    const int w = bgra.width();
    const int h = bgra.height();
    const uint32_t rowBytes = static_cast<uint32_t>(w) * 4U;
    const uint32_t pixelBytes = rowBytes * static_cast<uint32_t>(h);

    BITMAPFILEHEADER bfh = {};
    bfh.bfType = 0x4D42; // 'BM'
    bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    bfh.bfSize = bfh.bfOffBits + pixelBytes;

    BITMAPINFOHEADER bih = {};
    bih.biSize = sizeof(BITMAPINFOHEADER);
    bih.biWidth = w;
    bih.biHeight = -h; // Top-down DIB: matches QImage scanline order 1:1
    bih.biPlanes = 1;
    bih.biBitCount = 32;
    bih.biCompression = BI_RGB;
    bih.biSizeImage = pixelBytes;

    const QString tmpPath = finalPath + QStringLiteral(".tmp");
    const std::wstring wTmp = QDir::toNativeSeparators(tmpPath).toStdWString();
    const std::wstring wFinal = QDir::toNativeSeparators(finalPath).toStdWString();

    HANDLE hFile = CreateFileW(wTmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        return false;
    }

    bool ok = true;
    DWORD written = 0;
    if (!WriteFile(hFile, &bfh, sizeof(bfh), &written, nullptr) || written != sizeof(bfh) ||
        !WriteFile(hFile, &bih, sizeof(bih), &written, nullptr) || written != sizeof(bih)) {
        ok = false;
    }
    if (ok) {
        if (static_cast<uint32_t>(bgra.bytesPerLine()) == rowBytes) {
            if (!WriteFile(hFile, bgra.constBits(), pixelBytes, &written, nullptr) || written != pixelBytes) {
                ok = false;
            }
        } else {
            for (int y = 0; y < h; ++y) {
                if (!WriteFile(hFile, bgra.constScanLine(y), rowBytes, &written, nullptr) || written != rowBytes) {
                    ok = false;
                    break;
                }
            }
        }
    }
    CloseHandle(hFile);

    if (!ok) {
        DeleteFileW(wTmp.c_str());
        return false;
    }
    if (!MoveFileExW(wTmp.c_str(), wFinal.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(wTmp.c_str());
        return false;
    }
    return true;
}

int panelLayerRank(Quickshell::QsPanelWindow* w) {
    using namespace SekaiPop::Quickshell;
    auto* ls = w->layerShell();
    const int layer = ls ? ls->layer() : WlrLayer::Top;
    const QString ns = ls ? ls->ns() : QString();

    // Exclude transient/edit/hotedge/overlay windows from the clean desktop boot curtain
    if (layer == WlrLayer::Overlay ||
        ns == QStringLiteral("sekaipop-hotedge") ||
        ns.startsWith(QStringLiteral("sekaipop-edit")) ||
        ns.contains(QStringLiteral("phone")) ||
        ns.contains(QStringLiteral("quickpanel")) ||
        ns.contains(QStringLiteral("overview")) ||
        ns.contains(QStringLiteral("osd")) ||
        ns.contains(QStringLiteral("notif")) ||
        ns.contains(QStringLiteral("popup")) ||
        ns.contains(QStringLiteral("menu")) ||
        ns.contains(QStringLiteral("welcome")) ||
        ns.contains(QStringLiteral("region")) ||
        ns.contains(QStringLiteral("keyboard"))) {
        return -1;
    }

    if (layer == WlrLayer::Background && w->mask() == nullptr) {
        return 0; // WallpaperLayer
    }
    if (ns == QStringLiteral("sekaipop-visualizer")) {
        return 1; // Audio Visualizer behind widgets & dock
    }
    if (layer == WlrLayer::Background || (layer == WlrLayer::Bottom && ns != QStringLiteral("sekaipop-dock"))) {
        return 2; // Desktop widgets (Clock, UserCard, Calendar, etc.)
    }
    if (ns == QStringLiteral("sekaipop-dock")) {
        return 3; // Dock
    }
    if (layer == WlrLayer::Top) {
        return 4; // Top / edge Bar
    }
    return -1;
}

} // namespace

QString BootCurtainCache::cacheDir() {
    QString localAppData = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (localAppData.isEmpty()) {
        localAppData = QDir::homePath() + QStringLiteral("/AppData/Local");
    }
    return QDir(localAppData).filePath(QStringLiteral("SekaiPop/cache/boot_curtain"));
}

bool BootCurtainCache::captureFromPanels(const QList<Quickshell::QsPanelWindow*>& panels, bool isEditModeActive) {
    if (isEditModeActive || panels.isEmpty()) {
        return false;
    }

    QElapsedTimer timer;
    timer.start();

    std::vector<PhysMonitorInfo> monitors;
    EnumDisplayMonitors(nullptr, nullptr, &enumPhysMonitorsProc, reinterpret_cast<LPARAM>(&monitors));
    if (monitors.empty()) {
        return false;
    }

    // Stable deterministic order: left-to-right, top-to-bottom
    std::sort(monitors.begin(), monitors.end(), [](const PhysMonitorInfo& a, const PhysMonitorInfo& b) {
        if (a.rect.left != b.rect.left) return a.rect.left < b.rect.left;
        return a.rect.top < b.rect.top;
    });
    for (size_t i = 0; i < monitors.size(); ++i) {
        monitors[i].index = static_cast<int>(i);
    }

    const QString dir = cacheDir();
    QDir().mkpath(dir);

    struct SavedEntry {
        RECT rect;
        QString fileName;
    };
    std::vector<SavedEntry> saved;
    QRgb sampledBg = qRgb(24, 24, 37);

    for (const PhysMonitorInfo& mon : monitors) {
        const int monW = mon.rect.right - mon.rect.left;
        const int monH = mon.rect.bottom - mon.rect.top;
        if (monW <= 0 || monH <= 0 || monW > 8192 || monH > 8192) {
            continue;
        }

        struct RankedPanel {
            Quickshell::QsPanelWindow* win;
            int rank;
            RECT winRect;
        };
        std::vector<RankedPanel> monPanels;
        bool hasWallpaper = false;

        for (Quickshell::QsPanelWindow* w : panels) {
            if (!w || !w->isVisible() || !w->isExposed() || !w->handle()) {
                continue;
            }
            const int rank = panelLayerRank(w);
            if (rank < 0) {
                continue;
            }
            HWND hwnd = reinterpret_cast<HWND>(w->winId());
            if (!hwnd || !IsWindow(hwnd) || !IsWindowVisible(hwnd)) {
                continue;
            }
            if (MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST) != mon.hmon) {
                continue;
            }
            RECT wr = {};
            if (!GetWindowRect(hwnd, &wr)) {
                continue;
            }
            if ((wr.right - wr.left) <= 0 || (wr.bottom - wr.top) <= 0) {
                continue;
            }
            if (rank == 0) {
                hasWallpaper = true;
            }
            monPanels.push_back(RankedPanel{w, rank, wr});
        }

        if (!hasWallpaper || monPanels.empty()) {
            continue;
        }

        std::stable_sort(monPanels.begin(), monPanels.end(), [](const RankedPanel& a, const RankedPanel& b) {
            return a.rank < b.rank;
        });

        QImage canvas(monW, monH, QImage::Format_RGB32);
        canvas.fill(QColor(24, 24, 37));
        QPainter painter(&canvas);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);

        for (const RankedPanel& rp : monPanels) {
            QImage frame = rp.win->grabWindow();
            if (frame.isNull()) {
                continue;
            }
            // grabWindow() returns physical backing pixels tagged with screen DPR (e.g. 1.25x);
            // reset DPR to 1.0 so QPainter blits 1:1 physical pixels onto our physical-resolution canvas.
            frame.setDevicePixelRatio(1.0);
            const int dstX = rp.winRect.left - mon.rect.left;
            const int dstY = rp.winRect.top - mon.rect.top;
            const int dstW = rp.winRect.right - rp.winRect.left;
            const int dstH = rp.winRect.bottom - rp.winRect.top;
            if (frame.width() == dstW && frame.height() == dstH) {
                painter.drawImage(dstX, dstY, frame);
            } else {
                painter.drawImage(QRect(dstX, dstY, dstW, dstH), frame);
            }
            if (rp.rank == 0 && mon.index == 0) {
                sampledBg = canvas.pixel(monW / 2, monH / 2);
            }
        }
        painter.end();

        const QString fileName = QStringLiteral("mon_%1.bmp").arg(mon.index);
        const QString fullPath = QDir(dir).filePath(fileName);
        if (writeFastBmp32Atomic(fullPath, canvas)) {
            saved.push_back(SavedEntry{mon.rect, fileName});
        }
    }

    if (saved.empty()) {
        return false;
    }

    QString manifest;
    QTextStream ts(&manifest);
    ts << "SKPP_CURTAIN_V1\n";
    ts << qRed(sampledBg) << ' ' << qGreen(sampledBg) << ' ' << qBlue(sampledBg) << '\n';
    ts << saved.size() << '\n';
    for (const SavedEntry& e : saved) {
        ts << e.rect.left << ' ' << e.rect.top << ' '
           << (e.rect.right - e.rect.left) << ' ' << (e.rect.bottom - e.rect.top) << ' '
           << e.fileName << '\n';
    }
    ts.flush();

    const QString manifestPath = QDir(dir).filePath(QStringLiteral("manifest.txt"));
    const QString tmpManifest = manifestPath + QStringLiteral(".tmp");
    {
        QFile mf(tmpManifest);
        if (!mf.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            return false;
        }
        mf.write(manifest.toUtf8());
        mf.close();
    }
    const std::wstring wTmp = QDir::toNativeSeparators(tmpManifest).toStdWString();
    const std::wstring wFinal = QDir::toNativeSeparators(manifestPath).toStdWString();
    if (!MoveFileExW(wTmp.c_str(), wFinal.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(wTmp.c_str());
        return false;
    }

    qInfo("[BootCurtain] Saved %d monitor snapshot(s) in %lldms",
          static_cast<int>(saved.size()), timer.elapsed());
    return true;
}

} // namespace SekaiPop::Win32
