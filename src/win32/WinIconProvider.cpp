#include "WinIconProvider.h"
#include "Win32Utils.h"

#include <QByteArray>
#include <QFileInfo>
#include <QPainter>
#include <algorithm>
#include <cstring>
#include <iterator>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <vector>

namespace SekaiPop::Win32 {

namespace {

bool readDwordPixels(HBITMAP hbm, int w, int h, std::vector<quint32>& out) {
    if (!hbm || w <= 0 || h <= 0) {
        return false;
    }
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    out.resize(static_cast<size_t>(w) * static_cast<size_t>(h));
    HDC hdc = GetDC(nullptr);
    const int rows = GetDIBits(hdc, hbm, 0, static_cast<UINT>(h), out.data(), &bmi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, hdc);
    return rows != 0;
}

QImage hbitmapToQImage(HBITMAP hbm, bool premultiplied = false) {
    if (!hbm) {
        return {};
    }
    BITMAP bm = {};
    if (!GetObjectW(hbm, sizeof(bm), &bm) || bm.bmWidth <= 0 || bm.bmHeight == 0) {
        return {};
    }
    const int w = bm.bmWidth;
    const int h = bm.bmHeight > 0 ? bm.bmHeight : -bm.bmHeight;

    std::vector<quint32> pixels;
    if (!readDwordPixels(hbm, w, h, pixels)) {
        return {};
    }

    // Detect if the bitmap actually carries alpha data (some legacy 32bpp icons leave A=0).
    bool hasAlpha = false;
    for (quint32 px : pixels) {
        if ((px & 0xFF000000u) != 0) {
            hasAlpha = true;
            break;
        }
    }
    if (!hasAlpha) {
        for (quint32& px : pixels) {
            if (px != 0) {
                px |= 0xFF000000u;
            }
        }
    }

    const QImage::Format fmt = (premultiplied && hasAlpha)
        ? QImage::Format_ARGB32_Premultiplied
        : QImage::Format_ARGB32;
    QImage img(reinterpret_cast<const uchar*>(pixels.data()), w, h, w * 4, fmt);
    return img.copy();
}

QImage hiconToQImage(HICON hicon) {
    if (!hicon) {
        return {};
    }
    ICONINFO ii = {};
    if (!GetIconInfo(hicon, &ii)) {
        return {};
    }

    QImage result;
    if (ii.hbmColor) {
        BITMAP bm = {};
        if (GetObjectW(ii.hbmColor, sizeof(bm), &bm) && bm.bmWidth > 0 && bm.bmHeight != 0) {
            const int w = bm.bmWidth;
            const int h = qAbs(bm.bmHeight);
            std::vector<quint32> pixels;
            if (readDwordPixels(ii.hbmColor, w, h, pixels)) {
                bool hasAlpha = false;
                for (quint32 px : pixels) {
                    if ((px & 0xFF000000u) != 0) {
                        hasAlpha = true;
                        break;
                    }
                }
                if (!hasAlpha && ii.hbmMask) {
                    BITMAP bmMask = {};
                    if (GetObjectW(ii.hbmMask, sizeof(bmMask), &bmMask) && bmMask.bmWidth == w) {
                        const int maskH = qAbs(bmMask.bmHeight);
                        std::vector<quint32> maskPixels;
                        if (maskH >= h && readDwordPixels(ii.hbmMask, w, maskH, maskPixels)) {
                            const size_t count = static_cast<size_t>(w) * static_cast<size_t>(h);
                            for (size_t i = 0; i < count; ++i) {
                                if ((maskPixels[i] & 0x00FFFFFFu) != 0) {
                                    pixels[i] = 0x00000000u;
                                } else {
                                    pixels[i] |= 0xFF000000u;
                                }
                            }
                            hasAlpha = true;
                        }
                    }
                }
                if (!hasAlpha) {
                    for (quint32& px : pixels) {
                        if (px != 0) {
                            px |= 0xFF000000u;
                        }
                    }
                }
                result = QImage(reinterpret_cast<const uchar*>(pixels.data()), w, h, w * 4, QImage::Format_ARGB32).copy();
            }
        }
    } else if (ii.hbmMask) {
        BITMAP bmMask = {};
        if (GetObjectW(ii.hbmMask, sizeof(bmMask), &bmMask) && bmMask.bmWidth > 0 && qAbs(bmMask.bmHeight) >= 2) {
            const int w = bmMask.bmWidth;
            const int maskH = qAbs(bmMask.bmHeight);
            const int h = maskH / 2;
            std::vector<quint32> maskPixels;
            if (h > 0 && readDwordPixels(ii.hbmMask, w, maskH, maskPixels)) {
                std::vector<quint32> out(static_cast<size_t>(w) * static_cast<size_t>(h));
                for (int y = 0; y < h; ++y) {
                    for (int x = 0; x < w; ++x) {
                        const size_t idx = static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x);
                        const bool and1 = (maskPixels[idx] & 0x00FFFFFFu) != 0;
                        const bool xor1 = (maskPixels[idx + static_cast<size_t>(h) * static_cast<size_t>(w)] & 0x00FFFFFFu) != 0;
                        if (and1 && !xor1) {
                            out[idx] = 0x00000000u;
                        } else if (!and1 && !xor1) {
                            out[idx] = 0xFF000000u;
                        } else if (!and1 && xor1) {
                            out[idx] = 0xFFFFFFFFu;
                        } else {
                            out[idx] = 0xFF2B2938u;
                        }
                    }
                }
                result = QImage(reinterpret_cast<const uchar*>(out.data()), w, h, w * 4, QImage::Format_ARGB32).copy();
            }
        }
    }

    if (ii.hbmColor) {
        DeleteObject(ii.hbmColor);
    }
    if (ii.hbmMask) {
        DeleteObject(ii.hbmMask);
    }
    return result;
}

QImage normalizeIconImage(const QImage& src, int targetSize) {
    if (src.isNull() || src.width() <= 0 || src.height() <= 0) {
        return {};
    }
    const QImage argb = src.format() == QImage::Format_ARGB32
        ? src
        : src.convertToFormat(QImage::Format_ARGB32);

    // Check inner content bounds (skipping outer 4px Windows thumbnail border plate if present).
    const int margin = argb.width() >= 64 ? 5 : 0;
    int minX = argb.width();
    int minY = argb.height();
    int maxX = -1;
    int maxY = -1;
    for (int y = margin; y < argb.height() - margin; ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(argb.constScanLine(y));
        for (int x = margin; x < argb.width() - margin; ++x) {
            // Threshold > 40 ignores faint (A~4..16) halos.
            if (qAlpha(row[x]) > 40) {
                minX = std::min(minX, x);
                minY = std::min(minY, y);
                maxX = std::max(maxX, x);
                maxY = std::max(maxY, y);
            }
        }
    }
    if (maxX < minX || maxY < minY) {
        if (margin > 0) {
            // Fall back to full canvas scan for edge-touching icons.
            for (int y = 0; y < argb.height(); ++y) {
                const QRgb* row = reinterpret_cast<const QRgb*>(argb.constScanLine(y));
                for (int x = 0; x < argb.width(); ++x) {
                    if (qAlpha(row[x]) > 40) {
                        minX = std::min(minX, x);
                        minY = std::min(minY, y);
                        maxX = std::max(maxX, x);
                        maxY = std::max(maxY, y);
                    }
                }
            }
        }
        if (maxX < minX || maxY < minY) {
            return {};
        }
    }

    const int bw = maxX - minX + 1;
    const int bh = maxY - minY + 1;

    // Trim oversized transparent canvases when Windows pads a small icon inside a large bitmap.
    if (bw <= qRound(argb.width() * 0.65) && bh <= qRound(argb.height() * 0.65)) {
        const int cropX = qMax(0, minX - 2);
        const int cropY = qMax(0, minY - 2);
        const int cropR = qMin(src.width() - 1, maxX + 2);
        const int cropB = qMin(src.height() - 1, maxY + 2);
        const int inner = qMax(24, qRound(targetSize * 0.88));
        const QImage cropped = src.copy(cropX, cropY, cropR - cropX + 1, cropB - cropY + 1);
        const QImage scaled = cropped.scaled(inner, inner, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QImage canvas(targetSize, targetSize, QImage::Format_ARGB32_Premultiplied);
        canvas.fill(Qt::transparent);
        QPainter p(&canvas);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.drawImage((targetSize - scaled.width()) / 2, (targetSize - scaled.height()) / 2, scaled);
        return canvas;
    }

    if (src.width() < targetSize && src.height() < targetSize) {
        return src.scaled(targetSize, targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return src;
}

QImage extractShellItemImage(const QString& parsingName, int pxSize) {
    const std::wstring wName = toStdWString(parsingName);
    IShellItem* item = nullptr;
    if (FAILED(SHCreateItemFromParsingName(wName.c_str(), nullptr, IID_PPV_ARGS(&item))) || !item) {
        return {};
    }
    IShellItemImageFactory* factory = nullptr;
    QImage out;
    if (SUCCEEDED(item->QueryInterface(IID_PPV_ARGS(&factory))) && factory) {
        HBITMAP hbm = nullptr;
        const SIZE sz{ pxSize, pxSize };
        HRESULT hr = factory->GetImage(sz, SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK, &hbm);
        if (FAILED(hr) || !hbm) {
            hr = factory->GetImage(sz, SIIGBF_RESIZETOFIT, &hbm);
        }
        if (SUCCEEDED(hr) && hbm) {
            out = hbitmapToQImage(hbm, true);
            DeleteObject(hbm);
        }
        factory->Release();
    }
    item->Release();
    return out;
}

QImage extractIconByIndex(const std::wstring& filePath, int idx, int targetSize) {
    HICON hIcon = nullptr;
    UINT iconId = 0;
    if (idx >= 0) {
        if (PrivateExtractIconsW(filePath.c_str(), idx, targetSize, targetSize, &hIcon, &iconId, 1, LR_DEFAULTCOLOR) > 0 && hIcon) {
            QImage out = hiconToQImage(hIcon);
            DestroyIcon(hIcon);
            if (!out.isNull()) {
                return out;
            }
        }
        HICON hLarge = nullptr;
        if (ExtractIconExW(filePath.c_str(), idx, &hLarge, nullptr, 1) > 0 && hLarge) {
            QImage out = hiconToQImage(hLarge);
            DestroyIcon(hLarge);
            if (!out.isNull()) {
                return out;
            }
        }
        return {};
    }

    // Negative icon indices in Windows shortcuts identify resource ID (-idx).
    HMODULE hMod = LoadLibraryExW(filePath.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (hMod) {
        const WORD resId = static_cast<WORD>(-idx);
        HICON hRes = static_cast<HICON>(LoadImageW(hMod, MAKEINTRESOURCEW(resId), IMAGE_ICON, targetSize, targetSize, 0));
        if (!hRes) {
            hRes = static_cast<HICON>(LoadImageW(hMod, MAKEINTRESOURCEW(resId), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));
        }
        if (hRes) {
            QImage out = hiconToQImage(hRes);
            DestroyIcon(hRes);
            FreeLibrary(hMod);
            if (!out.isNull()) {
                return out;
            }
        } else {
            FreeLibrary(hMod);
        }
    }

    if (idx == -1) {
        if (PrivateExtractIconsW(filePath.c_str(), 0, targetSize, targetSize, &hIcon, &iconId, 1, LR_DEFAULTCOLOR) > 0 && hIcon) {
            QImage out = hiconToQImage(hIcon);
            DestroyIcon(hIcon);
            return out;
        }
    }
    return {};
}

} // namespace

WinIconProvider::WinIconProvider()
    : QQuickImageProvider(QQuickImageProvider::Image) {}

QString WinIconProvider::appIconUrl(const QString& targetOrPath) {
    if (targetOrPath.isEmpty()) {
        return {};
    }
    const QByteArray b64 = targetOrPath.toUtf8().toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    return QStringLiteral("image://winicon/app/") + QString::fromLatin1(b64);
}

QString WinIconProvider::hwndIconUrl(quintptr hwnd) {
    if (hwnd == 0) {
        return {};
    }
    return QStringLiteral("image://winicon/hwnd/") + QString::number(hwnd);
}

QString WinIconProvider::fallbackIconUrl(const QString& label) {
    const QString clean = label.trimmed().isEmpty() ? QStringLiteral("A") : label.trimmed();
    const QByteArray b64 = clean.toUtf8().toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    return QStringLiteral("image://winicon/fallback/") + QString::fromLatin1(b64);
}

QImage WinIconProvider::makeFallbackIcon(const QString& label, int targetSize) {
    // IMPORTANT: requestImage() runs on Qt's background QQuickPixmapReader worker thread.
    // NEVER use QFont / QPainter::drawText() here (DirectWrite / QFontEngine is NOT thread-safe
    // off the GUI thread and causes STATUS_FAIL_FAST_EXCEPTION on Windows).
    // Draw a crisp, 100% thread-safe pure geometric vector app badge instead.
    const int sz = qBound(24, targetSize, 256);
    QImage canvas(sz, sz, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);

    QString clean = label.trimmed();
    if (clean.contains(QLatin1Char('\\')) || clean.contains(QLatin1Char('/'))) {
        clean = QFileInfo(clean).completeBaseName();
    }

    static const QColor palette[] = {
        QColor(0x5E, 0xEA, 0xD4),
        QColor(0x7D, 0xD3, 0xFC),
        QColor(0xC4, 0xB5, 0xFD),
        QColor(0xF9, 0xA8, 0xD4),
        QColor(0xFD, 0xBA, 0x74),
        QColor(0x86, 0xEF, 0xAC)
    };
    const uint h = qHash(clean.toLower());
    const QColor fill = palette[h % std::size(palette)];
    const QColor ink(0x1E, 0x1B, 0x2E);

    QPainter p(&canvas);
    p.setRenderHint(QPainter::Antialiasing, true);

    const qreal pad = sz * 0.08;
    const QRectF r(pad, pad, sz - pad * 2.0, sz - pad * 2.0);
    const qreal stroke = qMax<qreal>(1.5, sz * 0.055);
    p.setBrush(fill);
    p.setPen(QPen(ink, stroke));
    p.drawRoundedRect(r, sz * 0.24, sz * 0.24);

    // Inner minimalist geometric app-window glyph (3x3 rounded tiles / window bar)
    const qreal inPad = sz * 0.28;
    const QRectF inner(inPad, inPad, sz - inPad * 2.0, sz - inPad * 2.0);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(ink, qMax<qreal>(1.4, sz * 0.05), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawRoundedRect(inner, sz * 0.08, sz * 0.08);
    const qreal barY = inner.top() + inner.height() * 0.34;
    p.drawLine(QPointF(inner.left(), barY), QPointF(inner.right(), barY));
    p.setPen(Qt::NoPen);
    p.setBrush(ink);
    const qreal dotR = qMax<qreal>(1.0, sz * 0.032);
    p.drawEllipse(QPointF(inner.left() + inner.width() * 0.24, inner.top() + inner.height() * 0.17), dotR, dotR);
    p.drawEllipse(QPointF(inner.left() + inner.width() * 0.44, inner.top() + inner.height() * 0.17), dotR, dotR);

    p.end();
    return canvas;
}

QImage WinIconProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize) {
    const int px = requestedSize.isValid() && requestedSize.width() > 0
        ? qBound(16, requestedSize.width(), 256)
        : 72;

    QString cacheKey;
    QString fallbackLabel;
    QImage img;

    if (id.startsWith(QStringLiteral("app/"))) {
        const QByteArray encoded = id.mid(4).toLatin1();
        const QString target = QString::fromUtf8(QByteArray::fromBase64(encoded, QByteArray::Base64UrlEncoding));
        fallbackLabel = target;
        cacheKey = QStringLiteral("app:%1:%2").arg(px).arg(target);
        {
            QMutexLocker lock(&m_mutex);
            auto it = m_cache.constFind(cacheKey);
            if (it != m_cache.constEnd()) {
                if (size) {
                    *size = it->size();
                }
                return *it;
            }
        }
        img = extractForAppTarget(target, px);
    } else if (id.startsWith(QStringLiteral("hwnd/"))) {
        bool ok = false;
        const quintptr hwndVal = id.mid(5).toULongLong(&ok);
        if (ok && hwndVal != 0) {
            img = extractForHwnd(hwndVal, px);
            if (img.isNull()) {
                wchar_t tbuf[128] = {};
                if (GetWindowTextW(reinterpret_cast<HWND>(hwndVal), tbuf, 128) > 0) {
                    fallbackLabel = QString::fromWCharArray(tbuf);
                }
            }
        }
    } else if (id.startsWith(QStringLiteral("fallback/"))) {
        const QByteArray encoded = id.mid(9).toLatin1();
        fallbackLabel = QString::fromUtf8(QByteArray::fromBase64(encoded, QByteArray::Base64UrlEncoding));
        img = makeFallbackIcon(fallbackLabel, px);
    }

    if (img.isNull()) {
        img = makeFallbackIcon(fallbackLabel, px);
    }

    if (!img.isNull() && !cacheKey.isEmpty()) {
        QMutexLocker lock(&m_mutex);
        if (m_cache.size() >= 128) {
            m_cache.clear();
        }
        m_cache.insert(cacheKey, img);
    }

    if (size) {
        *size = img.size();
    }
    return img;
}

QImage WinIconProvider::extractForAppTarget(const QString& target, int targetSize) {
    if (target.isEmpty()) {
        return {};
    }
    ComInitGuard com;

    QString cleanPath = target;

    // Support explicit "path,index" icon resource strings (common inside .lnk files).
    const int comma = target.lastIndexOf(QLatin1Char(','));
    if (comma > 2) {
        bool idxOk = false;
        const int idx = target.mid(comma + 1).trimmed().toInt(&idxOk);
        const QString filePart = target.left(comma).trimmed();
        if (idxOk && QFileInfo::exists(filePart)) {
            cleanPath = filePart;
            QImage out = normalizeIconImage(extractIconByIndex(toStdWString(filePart), idx, targetSize), targetSize);
            if (!out.isNull()) {
                return out;
            }
        }
    }

    // Prefer direct high-res icon resource extraction for local binaries (.exe/.ico/.dll/.cpl)
    // so Windows Explorer does not wrap small icons inside a padded thumbnail plate.
    const QString lowerClean = cleanPath.toLower();
    const bool isBinaryResource = lowerClean.endsWith(QStringLiteral(".exe"))
        || lowerClean.endsWith(QStringLiteral(".ico"))
        || lowerClean.endsWith(QStringLiteral(".dll"))
        || lowerClean.endsWith(QStringLiteral(".cpl"));
    if (isBinaryResource && QFileInfo::exists(cleanPath)) {
        QImage binImg = normalizeIconImage(extractIconByIndex(toStdWString(cleanPath), 0, targetSize), targetSize);
        if (!binImg.isNull()) {
            return binImg;
        }
    }

    // If target is a .lnk shortcut and ShellItemImageFactory fails, resolve its embedded icon or target .exe
    if (lowerClean.endsWith(QStringLiteral(".lnk")) && QFileInfo::exists(cleanPath)) {
        QImage lnkImg = normalizeIconImage(extractShellItemImage(cleanPath, targetSize), targetSize);
        if (!lnkImg.isNull()) {
            return lnkImg;
        }
        IShellLinkW* psl = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&psl))) && psl) {
            IPersistFile* ppf = nullptr;
            if (SUCCEEDED(psl->QueryInterface(IID_PPV_ARGS(&ppf))) && ppf) {
                const std::wstring wLnk = toStdWString(cleanPath);
                if (SUCCEEDED(ppf->Load(wLnk.c_str(), STGM_READ))) {
                    wchar_t iconBuf[MAX_PATH * 2] = {};
                    int iconIdx = 0;
                    if (SUCCEEDED(psl->GetIconLocation(iconBuf, static_cast<int>(std::size(iconBuf)), &iconIdx))) {
                        QString ip = QString::fromWCharArray(iconBuf).trimmed();
                        if (!ip.isEmpty()) {
                            wchar_t exp[MAX_PATH * 2] = {};
                            const DWORD n = ExpandEnvironmentStringsW(toStdWString(ip).c_str(), exp, static_cast<DWORD>(std::size(exp)));
                            if (n > 0 && n < std::size(exp)) ip = QString::fromWCharArray(exp);
                            if (QFileInfo::exists(ip)) {
                                lnkImg = normalizeIconImage(extractIconByIndex(toStdWString(ip), iconIdx, targetSize), targetSize);
                            }
                        }
                    }
                    if (lnkImg.isNull()) {
                        wchar_t targetBuf[MAX_PATH * 2] = {};
                        WIN32_FIND_DATAW wfd = {};
                        if (SUCCEEDED(psl->GetPath(targetBuf, static_cast<int>(std::size(targetBuf)), &wfd, SLGP_RAWPATH))) {
                            QString tp = QString::fromWCharArray(targetBuf).trimmed();
                            if (!tp.isEmpty()) {
                                wchar_t exp[MAX_PATH * 2] = {};
                                const DWORD n = ExpandEnvironmentStringsW(toStdWString(tp).c_str(), exp, static_cast<DWORD>(std::size(exp)));
                                if (n > 0 && n < std::size(exp)) tp = QString::fromWCharArray(exp);
                                if (QFileInfo::exists(tp)) {
                                    lnkImg = normalizeIconImage(extractIconByIndex(toStdWString(tp), 0, targetSize), targetSize);
                                }
                            }
                        }
                    }
                }
                ppf->Release();
            }
            psl->Release();
        }
        if (!lnkImg.isNull()) {
            return lnkImg;
        }
    }

    QImage img = normalizeIconImage(extractShellItemImage(cleanPath, targetSize), targetSize);
    if (!img.isNull()) {
        return img;
    }

    if (!isBinaryResource && QFileInfo::exists(cleanPath)) {
        img = normalizeIconImage(extractIconByIndex(toStdWString(cleanPath), 0, targetSize), targetSize);
        if (!img.isNull()) {
            return img;
        }
    }

    const std::wstring wPath = toStdWString(cleanPath);
    SHFILEINFOW sfi = {};
    if (SHGetFileInfoW(wPath.c_str(), 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_LARGEICON) && sfi.hIcon) {
        img = normalizeIconImage(hiconToQImage(sfi.hIcon), targetSize);
        DestroyIcon(sfi.hIcon);
    }
    return img;
}

QImage WinIconProvider::extractForHwnd(quintptr hwndVal, int targetSize) {
    HWND hwnd = reinterpret_cast<HWND>(hwndVal);
    if (!IsWindow(hwnd)) {
        return {};
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    // For UWP ApplicationFrameHost windows, locate the child Windows.UI.Core.CoreWindow PID
    wchar_t clsBuf[64] = {};
    if (GetClassNameW(hwnd, clsBuf, 64) > 0 && wcscmp(clsBuf, L"ApplicationFrameWindow") == 0) {
        struct ChildCtx { DWORD parentPid; DWORD childPid; } cctx{ pid, 0 };
        EnumChildWindows(hwnd, [](HWND ch, LPARAM lp) -> BOOL {
            auto* c = reinterpret_cast<ChildCtx*>(lp);
            DWORD cpid = 0;
            GetWindowThreadProcessId(ch, &cpid);
            if (cpid != 0 && cpid != c->parentPid) {
                c->childPid = cpid;
                return FALSE;
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&cctx));
        if (cctx.childPid != 0) {
            pid = cctx.childPid;
        }
    }

    if (pid != 0) {
        HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (proc) {
            wchar_t exeBuf[MAX_PATH * 2] = {};
            DWORD exeLen = static_cast<DWORD>(std::size(exeBuf));
            if (QueryFullProcessImageNameW(proc, 0, exeBuf, &exeLen) && exeLen > 0) {
                CloseHandle(proc);
                const QString exePath = QString::fromWCharArray(exeBuf, static_cast<int>(exeLen));
                const QString cacheKey = QStringLiteral("exe:%1:%2").arg(targetSize).arg(exePath);
                {
                    QMutexLocker lock(&m_mutex);
                    auto it = m_cache.constFind(cacheKey);
                    if (it != m_cache.constEnd()) {
                        return *it;
                    }
                }
                QImage exeImg = extractForAppTarget(exePath, targetSize);
                if (!exeImg.isNull()) {
                    QMutexLocker lock(&m_mutex);
                    m_cache.insert(cacheKey, exeImg);
                    return exeImg;
                }
            } else {
                CloseHandle(proc);
            }
        }
    }

    DWORD_PTR iconRes = 0;
    HICON hIcon = nullptr;
    if (SendMessageTimeoutW(hwnd, WM_GETICON, ICON_BIG, 0, SMTO_ABORTIFHUNG, 60, &iconRes) && iconRes) {
        hIcon = reinterpret_cast<HICON>(iconRes);
    } else if (SendMessageTimeoutW(hwnd, WM_GETICON, ICON_SMALL2, 0, SMTO_ABORTIFHUNG, 60, &iconRes) && iconRes) {
        hIcon = reinterpret_cast<HICON>(iconRes);
    } else {
        hIcon = reinterpret_cast<HICON>(GetClassLongPtrW(hwnd, GCLP_HICON));
    }
    return hiconToQImage(hIcon);
}

} // namespace SekaiPop::Win32
