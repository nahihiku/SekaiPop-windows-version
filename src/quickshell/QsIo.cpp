#include <winsock2.h>
#include "QsIo.h"
#include "../services/AudioSpectrumSource.h"
#include "../services/DiscordVoiceBridge.h"
#include "../services/MatugenEngine.h"
#include "../services/MediaSource.h"
#include "../services/QuickSource.h"
#include "../services/StatusIconsService.h"
#include "../services/VideoWallpaperItem.h"
#include "../win32/ScreenshotOverlay.h"
#include "../win32/Win32Utils.h"
#include "../win32/WindowTracker.h"

#include <QClipboard>
#include <QCursor>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QCryptographicHash>
#include <QImageReader>
#include <QSaveFile>
#include <QScreen>
#include <QStandardPaths>
#include <QUrlQuery>
#include <thread>
#include <vector>

#include <windows.h>
#include <bluetoothapis.h>
#include <dwmapi.h>
#include <iphlpapi.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <wlanapi.h>

namespace SekaiPop::Quickshell {

namespace {

Services::AudioSpectrumSource* s_spectrumSource = nullptr;
Services::MediaSource* s_mediaSource = nullptr;
Services::QuickSource* s_quickSource = nullptr;
Services::StatusIconsService* s_statusIconsService = nullptr;
Win32::WindowTracker* s_windowTracker = nullptr;
int s_activeCavaCount = 0;

QString findNodeExecutablePath() {
    static const QStringList candidates = {
        QStringLiteral("C:/Program Files/nodejs/node.exe"),
        QStringLiteral("C:/Program Files (x86)/nodejs/node.exe")
    };
    for (const QString& c : candidates) {
        if (QFileInfo::exists(c)) {
            return c;
        }
    }
    const QString fromPath = QStandardPaths::findExecutable(QStringLiteral("node"));
    if (!fromPath.isEmpty()) {
        return fromPath;
    }
    return QStringLiteral("node.exe");
}

QString findUkikuraLyricsIndexPath() {
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(appDir).filePath(QStringLiteral("ukikura-lyrics/index.js")),
        QDir(appDir).filePath(QStringLiteral("../ukikura-lyrics/index.js")),
        QDir(appDir).filePath(QStringLiteral("../../ukikura-lyrics/index.js"))
    };
    for (const QString& c : candidates) {
        const QFileInfo fi(c);
        if (fi.exists() && fi.isFile()) {
            return fi.canonicalFilePath();
        }
    }
    return {};
}

struct ClipEntry {
    int id = 0;
    QString text;
};

struct ClipboardHistoryStore {
    QList<ClipEntry> entries;
    int nextId = 1;
    bool initialized = false;

    void ensureInit() {
        if (initialized) {
            return;
        }
        initialized = true;
        if (QClipboard* cb = QGuiApplication::clipboard()) {
            const QString initial = cb->text();
            if (!initial.trimmed().isEmpty()) {
                record(initial);
            }
            QObject::connect(cb, &QClipboard::dataChanged, QCoreApplication::instance(), [this, cb]() {
                const QString t = cb->text();
                if (!t.trimmed().isEmpty()) {
                    record(t);
                }
            });
        }
    }

    void record(const QString& text) {
        if (text.isEmpty()) return;
        for (int i = 0; i < entries.size(); ++i) {
            if (entries.at(i).text == text) {
                entries.removeAt(i);
                break;
            }
        }
        entries.prepend(ClipEntry{nextId++, text});
        while (entries.size() > 50) {
            entries.removeLast();
        }
    }

    QString listFormatted() {
        ensureInit();
        QString out;
        for (const ClipEntry& e : entries) {
            QString preview = e.text;
            preview.replace(QLatin1Char('\r'), QLatin1Char(' '));
            preview.replace(QLatin1Char('\n'), QLatin1Char(' '));
            preview.replace(QLatin1Char('\t'), QLatin1Char(' '));
            if (preview.size() > 300) {
                preview = preview.left(300);
            }
            out += QStringLiteral("%1\t%2\n").arg(e.id).arg(preview);
        }
        return out;
    }

    bool copyById(const QString& idStr) {
        ensureInit();
        const int id = idStr.toInt();
        for (const ClipEntry& e : entries) {
            if (e.id == id) {
                if (QClipboard* cb = QGuiApplication::clipboard()) {
                    cb->setText(e.text);
                    return true;
                }
            }
        }
        return false;
    }

    void deleteById(const QString& idStr) {
        ensureInit();
        const int id = idStr.toInt();
        for (int i = 0; i < entries.size(); ++i) {
            if (entries.at(i).id == id) {
                entries.removeAt(i);
                return;
            }
        }
    }

    void wipe() {
        ensureInit();
        entries.clear();
    }
};

ClipboardHistoryStore& clipHistory() {
    static ClipboardHistoryStore s_store;
    return s_store;
}

QString sampleCursorPixelHex() {
    POINT pt = {};
    GetCursorPos(&pt);
    HDC dc = GetDC(nullptr);
    COLORREF c = dc ? GetPixel(dc, pt.x, pt.y) : RGB(255, 255, 255);
    if (dc) {
        ReleaseDC(nullptr, dc);
    }
    const QString hex = QStringLiteral("#%1%2%3")
        .arg(GetRValue(c), 2, 16, QLatin1Char('0'))
        .arg(GetGValue(c), 2, 16, QLatin1Char('0'))
        .arg(GetBValue(c), 2, 16, QLatin1Char('0'))
        .toUpper();
    if (QClipboard* cb = QGuiApplication::clipboard()) {
        cb->setText(hex);
    }
    return hex;
}

WORD mapKeysymToVk(const QString& sym) {
    const QString s = sym.trimmed();
    const QString low = s.toLower();
    if (low == QStringLiteral("backspace")) return VK_BACK;
    if (low == QStringLiteral("return") || low == QStringLiteral("enter")) return VK_RETURN;
    if (low == QStringLiteral("tab")) return VK_TAB;
    if (low == QStringLiteral("escape") || low == QStringLiteral("esc")) return VK_ESCAPE;
    if (low == QStringLiteral("space")) return VK_SPACE;
    if (low == QStringLiteral("left")) return VK_LEFT;
    if (low == QStringLiteral("right")) return VK_RIGHT;
    if (low == QStringLiteral("up")) return VK_UP;
    if (low == QStringLiteral("down")) return VK_DOWN;
    if (low == QStringLiteral("home")) return VK_HOME;
    if (low == QStringLiteral("end")) return VK_END;
    if (low == QStringLiteral("prior") || low == QStringLiteral("page_up")) return VK_PRIOR;
    if (low == QStringLiteral("next") || low == QStringLiteral("page_down")) return VK_NEXT;
    if (low == QStringLiteral("delete")) return VK_DELETE;
    if (low == QStringLiteral("insert")) return VK_INSERT;
    if (low == QStringLiteral("shift")) return VK_SHIFT;
    if (low == QStringLiteral("ctrl") || low == QStringLiteral("control")) return VK_CONTROL;
    if (low == QStringLiteral("alt")) return VK_MENU;
    if (low == QStringLiteral("logo") || low == QStringLiteral("super") || low == QStringLiteral("meta")) return VK_LWIN;
    if (low.startsWith(QLatin1Char('f')) && low.size() >= 2 && low.size() <= 3) {
        bool ok = false;
        const int n = low.mid(1).toInt(&ok);
        if (ok && n >= 1 && n <= 12) return static_cast<WORD>(VK_F1 + (n - 1));
    }
    if (s.size() == 1) {
        const SHORT vk = VkKeyScanW(static_cast<wchar_t>(s.at(0).unicode()));
        if (vk != -1) {
            return static_cast<WORD>(vk & 0xFF);
        }
    }
    return 0;
}

void sendWin32UnicodeText(const QString& text) {
    if (text.isEmpty()) {
        return;
    }
    std::vector<INPUT> inputs;
    inputs.reserve(static_cast<size_t>(text.size()) * 2);
    for (const QChar ch : text) {
        INPUT down = {};
        down.type = INPUT_KEYBOARD;
        down.ki.wScan = static_cast<WORD>(ch.unicode());
        down.ki.dwFlags = KEYEVENTF_UNICODE;
        inputs.push_back(down);

        INPUT up = down;
        up.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        inputs.push_back(up);
    }
    if (!inputs.empty()) {
        SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
    }
}

void sendWin32Wtype(const QStringList& args) {
    std::vector<INPUT> inputs;
    auto pushVk = [&](WORD vk, bool keyUp) {
        if (vk == 0) return;
        INPUT in = {};
        in.type = INPUT_KEYBOARD;
        in.ki.wVk = vk;
        in.ki.dwFlags = keyUp ? KEYEVENTF_KEYUP : 0;
        inputs.push_back(in);
    };

    for (int i = 1; i < args.size(); ++i) {
        const QString& a = args.at(i);
        if (a == QStringLiteral("--") && i + 1 < args.size()) {
            if (!inputs.empty()) {
                SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
                inputs.clear();
            }
            sendWin32UnicodeText(args.mid(i + 1).join(QLatin1Char(' ')));
            return;
        }
        if (a == QStringLiteral("-M") && i + 1 < args.size()) {
            pushVk(mapKeysymToVk(args.at(++i)), false);
        } else if (a == QStringLiteral("-m") && i + 1 < args.size()) {
            pushVk(mapKeysymToVk(args.at(++i)), true);
        } else if (a == QStringLiteral("-k") && i + 1 < args.size()) {
            const QString sym = args.at(++i);
            const WORD vk = mapKeysymToVk(sym);
            if (vk != 0) {
                pushVk(vk, false);
                pushVk(vk, true);
            } else if (!sym.isEmpty()) {
                if (!inputs.empty()) {
                    SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
                    inputs.clear();
                }
                sendWin32UnicodeText(sym);
            }
        } else if (!a.startsWith(QLatin1Char('-'))) {
            if (!inputs.empty()) {
                SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
                inputs.clear();
            }
            sendWin32UnicodeText(a);
        }
    }
    if (!inputs.empty()) {
        SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
    }
}

QString resolveWindowsUserDir(const QString& xdgKey) {
    const QString k = xdgKey.trimmed().toUpper();
    if (k == QStringLiteral("DESKTOP")) return QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    if (k == QStringLiteral("DOWNLOAD") || k == QStringLiteral("DOWNLOADS")) return QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (k == QStringLiteral("DOCUMENTS")) return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (k == QStringLiteral("PICTURES")) return QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (k == QStringLiteral("MUSIC")) return QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    if (k == QStringLiteral("VIDEOS")) return QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    return QDir::homePath();
}

QString listWindowsWifiForNmcli(const QString& connectedSsid = {}) {
    QString out;
    QSet<QString> seen;
    DWORD negotiatedVer = 0;
    HANDLE hWlan = nullptr;
    if (WlanOpenHandle(2, nullptr, &negotiatedVer, &hWlan) == ERROR_SUCCESS && hWlan) {
        PWLAN_INTERFACE_INFO_LIST ifList = nullptr;
        if (WlanEnumInterfaces(hWlan, nullptr, &ifList) == ERROR_SUCCESS && ifList) {
            for (DWORD i = 0; i < ifList->dwNumberOfItems; ++i) {
                const GUID guid = ifList->InterfaceInfo[i].InterfaceGuid;
                PWLAN_AVAILABLE_NETWORK_LIST netList = nullptr;
                if (WlanGetAvailableNetworkList(hWlan, &guid, 0, nullptr, &netList) == ERROR_SUCCESS && netList) {
                    for (DWORD j = 0; j < netList->dwNumberOfItems; ++j) {
                        const WLAN_AVAILABLE_NETWORK& n = netList->Network[j];
                        if (n.dot11Ssid.uSSIDLength == 0 || n.dot11Ssid.uSSIDLength > DOT11_SSID_MAX_LENGTH) {
                            continue;
                        }
                        QString ssid = QString::fromUtf8(
                            reinterpret_cast<const char*>(n.dot11Ssid.ucSSID),
                            static_cast<int>(n.dot11Ssid.uSSIDLength));
                        if (ssid.isEmpty() || seen.contains(ssid)) {
                            continue;
                        }
                        seen.insert(ssid);
                        ssid.replace(QLatin1Char('|'), QLatin1Char('?'));
                        const int sig = static_cast<int>(n.wlanSignalQuality);
                        const QString sec = n.bSecurityEnabled ? QStringLiteral("WPA2") : QStringLiteral("--");
                        const QString inUse = (n.dwFlags & WLAN_AVAILABLE_NETWORK_CONNECTED) ? QStringLiteral("*") : QString();
                        out += QStringLiteral("%1|%2|%3|%4\n").arg(sig).arg(sec, inUse, ssid);
                    }
                    WlanFreeMemory(netList);
                }
            }
            // Windows 11 24H2+ denies WlanGetAvailableNetworkList when Location Services is off.
            // Fall back to the active NLM Wi-Fi name + saved WLAN profiles (WlanGetProfileList)
            // so the Network dialog still shows the connected SSID and saved networks cleanly.
            if (out.isEmpty()) {
                if (!connectedSsid.isEmpty()) {
                    QString safeSsid = connectedSsid;
                    safeSsid.replace(QLatin1Char('|'), QLatin1Char('?'));
                    seen.insert(connectedSsid);
                    out += QStringLiteral("100|WPA2|*|%1\n").arg(safeSsid);
                }
                int addedProfiles = 0;
                for (DWORD i = 0; i < ifList->dwNumberOfItems && addedProfiles < 25; ++i) {
                    const GUID guid = ifList->InterfaceInfo[i].InterfaceGuid;
                    PWLAN_PROFILE_INFO_LIST profList = nullptr;
                    if (WlanGetProfileList(hWlan, &guid, nullptr, &profList) == ERROR_SUCCESS && profList) {
                        for (DWORD j = 0; j < profList->dwNumberOfItems && addedProfiles < 25; ++j) {
                            QString pname = QString::fromWCharArray(profList->ProfileInfo[j].strProfileName).trimmed();
                            if (pname.isEmpty() || seen.contains(pname)) {
                                continue;
                            }
                            seen.insert(pname);
                            pname.replace(QLatin1Char('|'), QLatin1Char('?'));
                            out += QStringLiteral("75|WPA2||%1\n").arg(pname);
                            ++addedProfiles;
                        }
                        WlanFreeMemory(profList);
                    }
                }
            }
            WlanFreeMemory(ifList);
        }
        WlanCloseHandle(hWlan, nullptr);
    }
    if (out.isEmpty() && !connectedSsid.isEmpty()) {
        QString safeSsid = connectedSsid;
        safeSsid.replace(QLatin1Char('|'), QLatin1Char('?'));
        out += QStringLiteral("100|WPA2|*|%1\n").arg(safeSsid);
    }
    return out;
}

QString listWindowsBluetoothPaired() {
    QString out;
    BLUETOOTH_DEVICE_SEARCH_PARAMS dParams = {};
    dParams.dwSize = sizeof(dParams);
    dParams.fReturnAuthenticated = TRUE;
    dParams.fReturnRemembered = TRUE;
    dParams.fReturnConnected = TRUE;
    dParams.fIssueInquiry = FALSE;
    dParams.cTimeoutMultiplier = 0;

    BLUETOOTH_DEVICE_INFO devInfo = {};
    devInfo.dwSize = sizeof(devInfo);
    HBLUETOOTH_DEVICE_FIND hDev = BluetoothFindFirstDevice(&dParams, &devInfo);
    if (hDev) {
        do {
            const QString name = QString::fromWCharArray(devInfo.szName).trimmed();
            if (name.isEmpty()) continue;
            const auto& b = devInfo.Address.rgBytes;
            const QString mac = QStringLiteral("%1:%2:%3:%4:%5:%6")
                .arg(b[5], 2, 16, QLatin1Char('0'))
                .arg(b[4], 2, 16, QLatin1Char('0'))
                .arg(b[3], 2, 16, QLatin1Char('0'))
                .arg(b[2], 2, 16, QLatin1Char('0'))
                .arg(b[1], 2, 16, QLatin1Char('0'))
                .arg(b[0], 2, 16, QLatin1Char('0'))
                .toUpper();
            const QString conn = devInfo.fConnected ? QStringLiteral("1") : QStringLiteral("0");
            out += QStringLiteral("%1|%2||%3\n").arg(mac, conn, name);
        } while (BluetoothFindNextDevice(hDev, &devInfo));
        BluetoothFindDeviceClose(hDev);
    }
    return out;
}

QNetworkAccessManager* sharedNam() {
    static QNetworkAccessManager* s_nam = new QNetworkAccessManager(QCoreApplication::instance());
    return s_nam;
}

QString stateRootDir() {
    if (QsGlobal* g = QsGlobal::instance()) {
        return normalizeWinPath(g->stateHome()) + QStringLiteral("/sekaipop");
    }
    return QDir::homePath() + QStringLiteral("/AppData/Local/SekaiPop/state/sekaipop");
}

QString configRootDir() {
    if (QsGlobal* g = QsGlobal::instance()) {
        return normalizeWinPath(g->configHome()) + QStringLiteral("/sekaipop");
    }
    return QDir::homePath() + QStringLiteral("/AppData/Local/SekaiPop/config/sekaipop");
}

QString cacheRootDir() {
    if (QsGlobal* g = QsGlobal::instance()) {
        return normalizeWinPath(g->cacheHome()) + QStringLiteral("/sekaipop");
    }
    return QDir::homePath() + QStringLiteral("/AppData/Local/SekaiPop/cache/sekaipop");
}

QString dataRootDir() {
    return configRootDir() + QStringLiteral("/data");
}

QString toSlashPath(const QString& p) {
    QString clean = normalizeWinPath(p);
    if (clean.size() >= 2 && clean.at(0).isLetter() && clean.at(1) == QLatin1Char(':')) {
        return QLatin1Char('/') + clean;
    }
    return clean;
}

QString readTextFile(const QString& path) {
    QFile f(normalizeWinPath(path));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    return QString::fromUtf8(f.readAll());
}

bool writeTextFile(const QString& path, const QString& content) {
    const QString clean = normalizeWinPath(path);
    QDir().mkpath(QFileInfo(clean).absolutePath());
    QSaveFile f(clean);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    f.write(content.toUtf8());
    return f.commit();
}

QString synthProcMeminfo() {
    MEMORYSTATUSEX ms = {};
    ms.dwLength = sizeof(ms);
    GlobalMemoryStatusEx(&ms);
    const quint64 totalKb = ms.ullTotalPhys / 1024ULL;
    const quint64 availKb = ms.ullAvailPhys / 1024ULL;
    return QStringLiteral("MemTotal: %1 kB\nMemFree: %2 kB\nMemAvailable: %2 kB\n")
        .arg(totalKb)
        .arg(availKb);
}

QString synthProcStat() {
    FILETIME idleFt = {}, kernelFt = {}, userFt = {};
    GetSystemTimes(&idleFt, &kernelFt, &userFt);
    auto toU64 = [](const FILETIME& ft) -> quint64 {
        return (static_cast<quint64>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    };
    const quint64 idle = toU64(idleFt) / 10000ULL;
    const quint64 kernel = toU64(kernelFt) / 10000ULL;
    const quint64 user = toU64(userFt) / 10000ULL;
    const quint64 sys = kernel > idle ? (kernel - idle) : 0ULL;
    return QStringLiteral("cpu  %1 0 %2 %3 0 0 0 0 0 0\n").arg(user).arg(sys).arg(idle);
}

QString synthProcNetDev() {
    quint64 rx = 0;
    quint64 tx = 0;
    ULONG sz = 0;
    if (GetIfTable(nullptr, &sz, FALSE) == ERROR_INSUFFICIENT_BUFFER && sz > 0) {
        QByteArray buf(static_cast<int>(sz), 0);
        auto* tbl = reinterpret_cast<MIB_IFTABLE*>(buf.data());
        if (GetIfTable(tbl, &sz, FALSE) == NO_ERROR) {
            for (DWORD i = 0; i < tbl->dwNumEntries; ++i) {
                const MIB_IFROW& row = tbl->table[i];
                if (row.dwType == IF_TYPE_SOFTWARE_LOOPBACK) {
                    continue;
                }
                if (row.dwOperStatus == IF_OPER_STATUS_OPERATIONAL ||
                    row.dwOperStatus == IF_OPER_STATUS_CONNECTED) {
                    rx += row.dwInOctets;
                    tx += row.dwOutOctets;
                }
            }
        }
    }
    return QStringLiteral("Inter-|   Receive                                                |  Transmit\n"
                          "  eth0: %1 0 0 0 0 0 0 0 %2 0 0 0 0 0 0 0\n")
        .arg(rx)
        .arg(tx);
}

static bool isMediaVideo(const QString& path) {
    const QString ext = QFileInfo(path).suffix().toLower();
    return (ext == QStringLiteral("mp4") || ext == QStringLiteral("webm") ||
            ext == QStringLiteral("mkv") || ext == QStringLiteral("mov") ||
            ext == QStringLiteral("avi"));
}

static QString getMediaKey(const QString& path, const QFileInfo& fi) {
    const QString keyStr = QStringLiteral("%1|%2-%3")
        .arg(toSlashPath(path))
        .arg(fi.size())
        .arg(fi.lastModified().toSecsSinceEpoch());
    return QString::fromUtf8(QCryptographicHash::hash(keyStr.toUtf8(), QCryptographicHash::Md5).toHex());
}

static bool extractVideoFrameWin32(const QString& filePath, const QString& outputPath, int targetW, int targetH) {
    // Primary path: native Windows Media Foundation frame decode (sharp full-res frame, works on worker threads)
    if (Services::VideoWallpaperItem::extractFrameToJpeg(filePath, outputPath, targetW, targetH, 85)) {
        return true;
    }

    // Fallback path: Windows Explorer shell thumbnail provider
    const HRESULT hrCom = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool comInit = SUCCEEDED(hrCom);
    const std::wstring wpath = QDir::toNativeSeparators(filePath).toStdWString();
    IShellItem* item = nullptr;
    if (FAILED(SHCreateItemFromParsingName(wpath.c_str(), nullptr, IID_PPV_ARGS(&item))) || !item) {
        if (comInit) CoUninitialize();
        return false;
    }
    IShellItemImageFactory* factory = nullptr;
    bool ok = false;
    if (SUCCEEDED(item->QueryInterface(IID_PPV_ARGS(&factory))) && factory) {
        HBITMAP hbm = nullptr;
        const SIZE sz = {static_cast<LONG>(targetW), static_cast<LONG>(targetH)};
        HRESULT hr = factory->GetImage(sz, SIIGBF_BIGGERSIZEOK, &hbm);
        if (FAILED(hr) || !hbm) {
            hr = factory->GetImage(sz, SIIGBF_RESIZETOFIT, &hbm);
        }
        if (SUCCEEDED(hr) && hbm) {
            BITMAP bm = {};
            if (GetObjectW(hbm, sizeof(bm), &bm) && bm.bmWidth > 0 && bm.bmHeight != 0) {
                const int w = bm.bmWidth;
                const int h = qAbs(bm.bmHeight);
                std::vector<quint32> pixels(static_cast<size_t>(w) * static_cast<size_t>(h));
                BITMAPINFO bi = {};
                bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bi.bmiHeader.biWidth = w;
                bi.bmiHeader.biHeight = -h; // top-down
                bi.bmiHeader.biPlanes = 1;
                bi.bmiHeader.biBitCount = 32;
                bi.bmiHeader.biCompression = BI_RGB;
                HDC hdc = CreateCompatibleDC(nullptr);
                if (GetDIBits(hdc, hbm, 0, h, pixels.data(), &bi, DIB_RGB_COLORS)) {
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
                    QImage img(reinterpret_cast<const uchar*>(pixels.data()), w, h, w * 4, QImage::Format_ARGB32);
                    QDir().mkpath(QFileInfo(outputPath).dir().absolutePath());
                    ok = img.save(outputPath, "JPG", 85);
                }
                DeleteDC(hdc);
            }
            DeleteObject(hbm);
        }
        factory->Release();
    }
    item->Release();
    if (comInit) CoUninitialize();
    return ok;
}

static bool extractImageThumb(const QString& filePath, const QString& outputPath, int targetW) {
    QImageReader reader(filePath);
    reader.setAutoTransform(true);
    const QSize origSize = reader.size();
    if (origSize.isValid() && origSize.width() > targetW) {
        const int targetH = (origSize.height() * targetW) / origSize.width();
        reader.setScaledSize(QSize(targetW, std::max(1, targetH)));
    }
    const QImage img = reader.read();
    if (img.isNull()) {
        return false;
    }
    QDir().mkpath(QFileInfo(outputPath).dir().absolutePath());
    return img.save(outputPath, "JPG", 82);
}

static QString unescapeHtmlBasic(QString s) {
    s.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    s.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    s.replace(QStringLiteral("&#39;"), QStringLiteral("'"));
    s.replace(QStringLiteral("&#x27;"), QStringLiteral("'"));
    s.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
    s.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
    s.replace(QStringLiteral("&nbsp;"), QStringLiteral(" "));
    return s.trimmed();
}

static QString parseMotionBgsHtmlToJson(const QString& html, int page, const QString& catLabel) {
    const int tmbIdx = html.indexOf(QStringLiteral("class=tmb"));
    QString chunk = (tmbIdx >= 0) ? html.mid(tmbIdx) : html;
    bool hasNext = false;
    const int pagIdx = chunk.indexOf(QStringLiteral("<section class=pag>"));
    if (pagIdx >= 0) {
        const QString pagChunk = chunk.mid(pagIdx, 800);
        hasNext = pagChunk.contains(QStringLiteral("Next"), Qt::CaseInsensitive) &&
                  pagChunk.contains(QStringLiteral("<a "));
        chunk = chunk.left(pagIdx);
    }

    static const QRegularExpression cardRe(
        QStringLiteral(
            "<a\\s[^>]*?href=(?:\"|')?(/[a-z0-9][a-z0-9-]+)(?:\"|'|\\s|>)[^>]*>.*?"
            "/media/(\\d+)/([^\\s\"'>]+?\\.(?:jpg|jpeg|png))"
            ".*?<span\\s+class=(?:\"|')?ttl(?:\"|')?[^>]*>([^<]+)</span>"
            "(?:.*?<span\\s+class=(?:\"|')?frm(?:\"|')?[^>]*>([^<]*)</span>)?"
            ".*?</a>"
        ),
        QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption
    );

    QJsonArray dataArr;
    QSet<QString> seenIds;
    auto it = cardRe.globalMatch(chunk);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const QString href = m.captured(1);
        const QString mediaId = m.captured(2);
        const QString imgFile = m.captured(3);
        const QString title = unescapeHtmlBasic(m.captured(4));
        const QString resTag = m.captured(5).trimmed().toUpper();
        if (mediaId.isEmpty() || imgFile.isEmpty() || seenIds.contains(mediaId)) {
            continue;
        }
        seenIds.insert(mediaId);

        const QString fullImg = QStringLiteral("https://motionbgs.com/media/%1/%2").arg(mediaId, imgFile);
        const QString thumbLarge = QStringLiteral("https://motionbgs.com/i/c/546x308/media/%1/%2").arg(mediaId, imgFile);
        const QString thumbSmall = QStringLiteral("https://motionbgs.com/i/c/364x205/media/%1/%2").arg(mediaId, imgFile);

        QJsonObject thumbs;
        thumbs.insert(QStringLiteral("large"), thumbLarge);
        thumbs.insert(QStringLiteral("original"), fullImg);
        thumbs.insert(QStringLiteral("small"), thumbSmall);

        QJsonObject item;
        item.insert(QStringLiteral("id"), QStringLiteral("mb-") + mediaId);
        item.insert(QStringLiteral("title"), title.isEmpty() ? (QStringLiteral("Live #") + mediaId) : title);
        item.insert(QStringLiteral("url"), QStringLiteral("https://motionbgs.com") + href);
        item.insert(QStringLiteral("path"), QStringLiteral("https://motionbgs.com/dl/hd/") + mediaId);
        item.insert(QStringLiteral("preview"), fullImg);
        item.insert(QStringLiteral("thumbs"), thumbs);
        item.insert(QStringLiteral("dimension_x"), 1920);
        item.insert(QStringLiteral("dimension_y"), 1080);
        item.insert(QStringLiteral("resolution"), resTag == QStringLiteral("4K")
            ? QStringLiteral("1920x1080") : QStringLiteral("1920x1080"));
        item.insert(QStringLiteral("file_size"), 0);
        item.insert(QStringLiteral("file_type"), QStringLiteral("mp4"));
        item.insert(QStringLiteral("category"), catLabel.isEmpty() ? QStringLiteral("Live Video") : catLabel);
        item.insert(QStringLiteral("views"), 0);
        item.insert(QStringLiteral("favorites"), 0);
        item.insert(QStringLiteral("colors"), QJsonArray());
        item.insert(QStringLiteral("is_video"), true);
        item.insert(QStringLiteral("provider"), QStringLiteral("motionbgs"));
        dataArr.append(item);
    }

    QJsonObject meta;
    meta.insert(QStringLiteral("current_page"), page);
    meta.insert(QStringLiteral("last_page"), hasNext ? (page + 1) : page);
    meta.insert(QStringLiteral("total"), hasNext ? (page * 36 + 36) : ((page - 1) * 36 + dataArr.size()));

    QJsonObject root;
    root.insert(QStringLiteral("data"), dataArr);
    root.insert(QStringLiteral("meta"), meta);
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

struct OnlineSearchCacheEntry {
    QString json;
    qint64 savedEpochSec = 0;
};

QHash<QString, OnlineSearchCacheEntry>& onlineSearchRamCache() {
    static QHash<QString, OnlineSearchCacheEntry> s_cache;
    return s_cache;
}

QString onlineSearchDiskPath(const QString& cacheKey) {
    const QString hash = QString::fromUtf8(
        QCryptographicHash::hash(cacheKey.toUtf8(), QCryptographicHash::Md5).toHex());
    return cacheRootDir() + QStringLiteral("/online_search/") + hash + QStringLiteral(".json");
}

QString readOnlineSearchCache(const QString& cacheKey, qint64 maxAgeSec) {
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    auto& ram = onlineSearchRamCache();
    auto it = ram.constFind(cacheKey);
    if (it != ram.constEnd() && !it->json.isEmpty() && (now - it->savedEpochSec) <= maxAgeSec) {
        return it->json;
    }
    const QString diskPath = onlineSearchDiskPath(cacheKey);
    const QFileInfo fi(diskPath);
    if (fi.exists() && fi.size() > 2) {
        const qint64 mtime = fi.lastModified().toSecsSinceEpoch();
        if ((now - mtime) <= maxAgeSec) {
            const QString txt = readTextFile(diskPath);
            if (!txt.isEmpty()) {
                if (ram.size() > 64) {
                    ram.clear();
                }
                ram.insert(cacheKey, OnlineSearchCacheEntry{txt, mtime});
                return txt;
            }
        }
    }
    return QString();
}

void writeOnlineSearchCache(const QString& cacheKey, const QString& json) {
    if (json.isEmpty()) {
        return;
    }
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    auto& ram = onlineSearchRamCache();
    if (ram.size() > 64) {
        ram.clear();
    }
    ram.insert(cacheKey, OnlineSearchCacheEntry{json, now});
    writeTextFile(onlineSearchDiskPath(cacheKey), json);
}

bool resolveMotionBgsRequest(const QString& synthUrlStr, QString& outRealUrl, int& outPage, QString& outCatLabel) {
    const QUrl synthUrl(synthUrlStr);
    const QUrlQuery q(synthUrl);
    const QString query = q.queryItemValue(QStringLiteral("q"), QUrl::FullyDecoded).trimmed();
    const QString cat = q.queryItemValue(QStringLiteral("cat"), QUrl::FullyDecoded).trimmed().toLower();
    outPage = qMax(1, q.queryItemValue(QStringLiteral("page")).toInt());

    if (!query.isEmpty() && outPage > 1) {
        outRealUrl.clear();
        outCatLabel = QStringLiteral("Live Video");
        return false;
    }

    if (!query.isEmpty()) {
        outRealUrl = QStringLiteral("https://motionbgs.com/search?q=") + QString::fromUtf8(QUrl::toPercentEncoding(query));
        outCatLabel = QStringLiteral("Live Video");
    } else {
        QString slug = QStringLiteral("tag:anime");
        outCatLabel = QStringLiteral("Anime");
        if (cat == QStringLiteral("games")) { slug = QStringLiteral("tag:games"); outCatLabel = QStringLiteral("Games"); }
        else if (cat == QStringLiteral("nature")) { slug = QStringLiteral("tag:nature"); outCatLabel = QStringLiteral("Nature"); }
        else if (cat == QStringLiteral("girl")) { slug = QStringLiteral("tag:girl"); outCatLabel = QStringLiteral("Anime Girl"); }
        else if (cat == QStringLiteral("anime-nature")) { slug = QStringLiteral("tag:anime-nature"); outCatLabel = QStringLiteral("Anime Nature"); }
        else if (cat == QStringLiteral("superhero")) { slug = QStringLiteral("tag:superhero"); outCatLabel = QStringLiteral("Superhero"); }
        else if (cat == QStringLiteral("4k") || cat == QStringLiteral("all")) { slug = QStringLiteral("4k"); outCatLabel = QStringLiteral("4K Live"); }

        outRealUrl = outPage > 1
            ? QStringLiteral("https://motionbgs.com/%1/%2/").arg(slug).arg(outPage)
            : QStringLiteral("https://motionbgs.com/%1/").arg(slug);
    }
    return true;
}

void preloadOnlineWallpaperUrl(const QString& urlStr) {
    if (!urlStr.startsWith(QStringLiteral("https://motionbgs.com/api/sekaipop"))) {
        return;
    }
    QString realUrl;
    int page = 1;
    QString catLabel;
    if (!resolveMotionBgsRequest(urlStr, realUrl, page, catLabel) || realUrl.isEmpty()) {
        return;
    }
    constexpr qint64 kFreshTtlSec = 900; // 15 minutes
    if (!readOnlineSearchCache(realUrl, kFreshTtlSec).isEmpty()) {
        return;
    }
    static QSet<QString> s_inFlightPreloads;
    if (s_inFlightPreloads.contains(realUrl)) {
        return;
    }
    s_inFlightPreloads.insert(realUrl);

    QNetworkRequest req{QUrl(realUrl)};
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("SekaiPop/1.0"));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, true);
    req.setTransferTimeout(12000);

    QNetworkReply* reply = sharedNam()->get(req);
    QObject::connect(reply, &QNetworkReply::finished, QCoreApplication::instance(), [reply, realUrl, page, catLabel]() {
        s_inFlightPreloads.remove(realUrl);
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() == QNetworkReply::NoError && status == 200) {
            const QString html = QString::fromUtf8(reply->readAll());
            const QString json = parseMotionBgsHtmlToJson(html, page, catLabel);
            if (!json.isEmpty()) {
                writeOnlineSearchCache(realUrl, json);
                qInfo("[MotionBGs] preloaded %s -> json=%lldB", qPrintable(realUrl), static_cast<long long>(json.size()));
            }
        } else {
            qWarning("[MotionBGs] preload failed (%d): %s", status, qPrintable(reply->errorString()));
        }
    });
}

QString handleStateScript(const QString& scriptName, const QStringList& args, int& exitCode) {
    exitCode = 0;
    const QString stateDir = stateRootDir();
    const QString configDir = configRootDir();
    const QString cacheDir = cacheRootDir();
    const QString dataDir = dataRootDir();
    QDir().mkpath(stateDir);
    QDir().mkpath(configDir);
    QDir().mkpath(cacheDir);
    QDir().mkpath(dataDir);

    const QString mode = args.value(0);

    // 1. Simple JSON / value state scripts
    static const QHash<QString, QString> simpleStateFiles = {
        {QStringLiteral("app-usage-state.sh"), QStringLiteral("app-usage.json")},
        {QStringLiteral("barlayout-state.sh"), QStringLiteral("bar-layout.json")},
        {QStringLiteral("barposition-state.sh"), QStringLiteral("bar-position")},
        {QStringLiteral("barstyle-state.sh"), QStringLiteral("bar-style")},
        {QStringLiteral("calendarcard-state.sh"), QStringLiteral("calendarcard.json")},
        {QStringLiteral("clock-state.sh"), QStringLiteral("clock.json")},
        {QStringLiteral("clones-state.sh"), QStringLiteral("clones.json")},
        {QStringLiteral("edit-state.sh"), QStringLiteral("edit.json")},
        {QStringLiteral("image-state.sh"), QStringLiteral("images.json")},
        {QStringLiteral("lyrics-state.sh"), QStringLiteral("lyrics.json")},
        {QStringLiteral("monitors-state.sh"), QStringLiteral("monitors.json")},
        {QStringLiteral("nightlight-state.sh"), QStringLiteral("nightlight")},
        {QStringLiteral("notes-state.sh"), QStringLiteral("notes.json")},
        {QStringLiteral("palette-state.sh"), QStringLiteral("palette.json")},
        {QStringLiteral("todo-state.sh"), QStringLiteral("todo.json")},
        {QStringLiteral("usercard-state.sh"), QStringLiteral("usercard.json")},
        {QStringLiteral("visualizer-state.sh"), QStringLiteral("visualizer.json")},
        {QStringLiteral("weathercard-state.sh"), QStringLiteral("weathercard.json")}
    };

    if (simpleStateFiles.contains(scriptName)) {
        const QString filePath = stateDir + QLatin1Char('/') + simpleStateFiles.value(scriptName);
        if (mode == QStringLiteral("load")) {
            return readTextFile(filePath);
        }
        if (mode == QStringLiteral("save")) {
            writeTextFile(filePath, args.value(1));
            return QString();
        }
        return QString();
    }

    if (scriptName == QStringLiteral("settings-state.sh")) {
        const QString filePath = configDir + QStringLiteral("/settings.json");
        auto syncRuntimeGfx = [](const QString& jsonText) {
            if (!s_spectrumSource) return;
            const QJsonDocument doc = QJsonDocument::fromJson(jsonText.toUtf8());
            if (doc.isObject()) {
                const int uiFps = doc.object().value(QStringLiteral("gfx.uiFps")).toInt(60);
                s_spectrumSource->setTargetFps(uiFps);
            } else {
                s_spectrumSource->setTargetFps(60);
            }
        };
        if (mode == QStringLiteral("load")) {
            const QString txt = readTextFile(filePath);
            syncRuntimeGfx(txt);
            return txt;
        }
        if (mode == QStringLiteral("save")) {
            const QString txt = args.value(1);
            writeTextFile(filePath, txt);
            syncRuntimeGfx(txt);
            return QString();
        }
        return QString();
    }

    if (scriptName == QStringLiteral("scale-state.sh")) {
        const QString filePath = configDir + QStringLiteral("/scale.json");
        if (!QFileInfo::exists(filePath)) {
            writeTextFile(filePath, QStringLiteral("{\n  \"default\": 1,\n  \"monitors\": {}\n}\n"));
        }
        return QString();
    }

    if (scriptName == QStringLiteral("welcome-state.sh")) {
        const QString filePath = stateDir + QStringLiteral("/welcome");
        if (mode == QStringLiteral("check")) {
            // Skip welcome tour by default on desktop start unless explicitly reset/shown
            if (!QFileInfo::exists(filePath)) {
                writeTextFile(filePath, QStringLiteral("done\n"));
            }
            const QString st = readTextFile(filePath).trimmed();
            return (st == QStringLiteral("done")) ? QStringLiteral("SKIP\n") : QStringLiteral("SHOW\n");
        }
        if (mode == QStringLiteral("done")) {
            writeTextFile(filePath, QStringLiteral("done\n"));
            return QString();
        }
        if (mode == QStringLiteral("reset")) {
            QFile::remove(filePath);
            return QString();
        }
        if (mode == QStringLiteral("status")) {
            const QString st = readTextFile(filePath).trimmed();
            return st.isEmpty() ? QStringLiteral("none\n") : (st + QLatin1Char('\n'));
        }
        return QStringLiteral("SKIP\n");
    }

    if (scriptName == QStringLiteral("theme-state.sh")) {
        if (mode == QStringLiteral("load")) return readTextFile(stateDir + QStringLiteral("/theme"));
        if (mode == QStringLiteral("save")) { writeTextFile(stateDir + QStringLiteral("/theme"), args.value(1)); return QString(); }
        if (mode == QStringLiteral("load-dark")) return readTextFile(stateDir + QStringLiteral("/dark"));
        if (mode == QStringLiteral("save-dark")) { writeTextFile(stateDir + QStringLiteral("/dark"), args.value(1)); return QString(); }
        if (mode == QStringLiteral("load-darker")) return readTextFile(stateDir + QStringLiteral("/darker"));
        if (mode == QStringLiteral("save-darker")) { writeTextFile(stateDir + QStringLiteral("/darker"), args.value(1)); return QString(); }
        if (mode == QStringLiteral("load-custom")) return readTextFile(stateDir + QStringLiteral("/custom-palette"));
        if (mode == QStringLiteral("save-custom")) { writeTextFile(stateDir + QStringLiteral("/custom-palette"), args.value(1)); return QString(); }
        return QString();
    }

    if (scriptName == QStringLiteral("wallpaper-state.sh")) {
        if (mode == QStringLiteral("load")) return readTextFile(stateDir + QStringLiteral("/wallpaper"));
        if (mode == QStringLiteral("save")) { writeTextFile(stateDir + QStringLiteral("/wallpaper"), args.value(1)); return QString(); }
        if (mode == QStringLiteral("loadscreens")) return readTextFile(stateDir + QStringLiteral("/wallpaper-screens"));
        if (mode == QStringLiteral("savescreens")) {
            QString out;
            for (int i = 1; i + 1 < args.size(); i += 2) {
                out += args.at(i) + QLatin1Char('\t') + args.at(i + 1) + QLatin1Char('\n');
            }
            writeTextFile(stateDir + QStringLiteral("/wallpaper-screens"), out);
            return QString();
        }
        if (mode == QStringLiteral("loadlock")) return readTextFile(stateDir + QStringLiteral("/lockwallpaper"));
        if (mode == QStringLiteral("savelock")) { writeTextFile(stateDir + QStringLiteral("/lockwallpaper"), args.value(1)); return QString(); }
        if (mode == QStringLiteral("loadlockscreens")) return readTextFile(stateDir + QStringLiteral("/lockwallpaper-screens"));
        if (mode == QStringLiteral("savelockscreens")) {
            QString out;
            for (int i = 1; i + 1 < args.size(); i += 2) {
                out += args.at(i) + QLatin1Char('\t') + args.at(i + 1) + QLatin1Char('\n');
            }
            writeTextFile(stateDir + QStringLiteral("/lockwallpaper-screens"), out);
            return QString();
        }
        return QString();
    }

    if (scriptName == QStringLiteral("pinned-apps-state.sh")) {
        if (mode == QStringLiteral("load")) return readTextFile(stateDir + QStringLiteral("/pinned-apps"));
        if (mode == QStringLiteral("save")) {
            QString out;
            for (int i = 1; i + 1 < args.size(); i += 2) {
                out += args.at(i) + QLatin1Char('\t') + args.at(i + 1) + QLatin1Char('\n');
            }
            writeTextFile(stateDir + QStringLiteral("/pinned-apps"), out);
            return QString();
        }
        if (mode == QStringLiteral("dock-load")) return readTextFile(stateDir + QStringLiteral("/dock-pinned"));
        if (mode == QStringLiteral("dock-save")) { writeTextFile(stateDir + QStringLiteral("/dock-pinned"), args.value(1)); return QString(); }
        if (mode == QStringLiteral("layout-load")) return readTextFile(stateDir + QStringLiteral("/dock-layout.json"));
        if (mode == QStringLiteral("layout-save")) { writeTextFile(stateDir + QStringLiteral("/dock-layout.json"), args.value(1)); return QString(); }
        if (mode == QStringLiteral("shelf-load")) return readTextFile(stateDir + QStringLiteral("/dock-shelf.json"));
        if (mode == QStringLiteral("shelf-save")) { writeTextFile(stateDir + QStringLiteral("/dock-shelf.json"), args.value(1)); return QString(); }
        return QString();
    }

    if (scriptName == QStringLiteral("qp-tiles-state.sh")) {
        const QString filePath = stateDir + QStringLiteral("/qp-tiles");
        if (mode == QStringLiteral("load")) return readTextFile(filePath);
        if (mode == QStringLiteral("save")) {
            QString out = QStringLiteral("#v2\n");
            if (args.size() < 3) {
                out += QStringLiteral("-\n");
            } else {
                for (int i = 1; i + 1 < args.size(); i += 2) {
                    out += args.at(i) + QLatin1Char('\t') + args.at(i + 1) + QLatin1Char('\n');
                }
            }
            writeTextFile(filePath, out);
            return QString();
        }
        return QString();
    }

    if (scriptName == QStringLiteral("extras-state.sh")) {
        const QString filePath = stateDir + QStringLiteral("/recent-emoji");
        if (mode == QStringLiteral("load-emoji")) return readTextFile(filePath);
        if (mode == QStringLiteral("save-emoji")) { writeTextFile(filePath, args.value(1)); return QString(); }
        return QString();
    }

    if (scriptName == QStringLiteral("keep-awake-state.sh")) {
        const QString filePath = stateDir + QStringLiteral("/keep-awake");
        if (mode == QStringLiteral("restore") || mode == QStringLiteral("load")) return readTextFile(filePath);
        if (mode == QStringLiteral("save")) {
            const QString val = args.value(1);
            writeTextFile(filePath, val);
            if (val == QStringLiteral("on")) {
                SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED);
            } else {
                SetThreadExecutionState(ES_CONTINUOUS);
            }
            return QString();
        }
        return QString();
    }

    if (scriptName == QStringLiteral("open-settings-app.sh")) {
        if (mode == QStringLiteral("wifi") || mode == QStringLiteral("network")) {
            Win32::launchShellTarget(QStringLiteral("ms-settings:network-wifi"));
        } else if (mode == QStringLiteral("bluetooth")) {
            Win32::launchShellTarget(QStringLiteral("ms-settings:bluetooth"));
        } else {
            Win32::launchShellTarget(QStringLiteral("ms-settings:"));
        }
        return QString();
    }

    if (scriptName == QStringLiteral("session-logout.sh")) {
        if (s_quickSource) s_quickSource->signOut();
        return QString();
    }

    if (scriptName == QStringLiteral("restart-shell.sh")) {
        QsIpcHandler::dispatchCall(QStringLiteral("shell"), QStringLiteral("restart"));
        return QString();
    }

    if (scriptName == QStringLiteral("lyrics-cache.sh")) {
        const QString lyrDir = cacheDir + QStringLiteral("/lyrics");
        QDir().mkpath(lyrDir);
        const QString key = args.value(1);
        if (mode == QStringLiteral("get") && !key.isEmpty()) {
            return readTextFile(lyrDir + QLatin1Char('/') + key + QStringLiteral(".json"));
        }
        if (mode == QStringLiteral("put") && !key.isEmpty()) {
            writeTextFile(lyrDir + QLatin1Char('/') + key + QStringLiteral(".json"), args.value(2));
            return QString();
        }
        if (mode == QStringLiteral("local") && !key.isEmpty()) {
            const QString audioPath = normalizeWinPath(key);
            if (!audioPath.isEmpty()) {
                const QFileInfo fi(audioPath);
                const QString lrcPath = fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName() + QStringLiteral(".lrc");
                if (QFileInfo::exists(lrcPath)) {
                    return readTextFile(lrcPath);
                }
            }
            return QString();
        }
        return QString();
    }

    if (scriptName == QStringLiteral("preset-state.sh")) {
        const QString presetsDir = configDir + QStringLiteral("/presets");
        QDir().mkpath(presetsDir);
        if (mode == QStringLiteral("all")) {
            QString out;
            QDir pd(presetsDir);
            for (const QFileInfo& fi : pd.entryInfoList(QStringList{QStringLiteral("*.json")}, QDir::Files)) {
                const QString name = fi.completeBaseName();
                if (name.startsWith(QLatin1Char('.'))) continue;
                QString json = readTextFile(fi.absoluteFilePath());
                json.remove(QLatin1Char('\n')).remove(QLatin1Char('\r'));
                out += name + QLatin1Char('\t') + json + QLatin1Char('\n');
            }
            const QString prevFile = presetsDir + QStringLiteral("/.previous.json");
            if (QFileInfo::exists(prevFile)) {
                QString json = readTextFile(prevFile);
                json.remove(QLatin1Char('\n')).remove(QLatin1Char('\r'));
                out += QStringLiteral(".previous\t") + json + QLatin1Char('\n');
            }
            return out;
        }
        if (mode == QStringLiteral("save")) {
            const QString name = args.value(1);
            const QString json = args.value(2);
            if (!name.isEmpty() && !json.isEmpty()) {
                writeTextFile(presetsDir + QLatin1Char('/') + name + QStringLiteral(".json"), json);
            }
            return QString();
        }
        if (mode == QStringLiteral("rename")) {
            const QString oldName = args.value(1);
            const QString newName = args.value(2);
            const QString json = args.value(3);
            if (!oldName.isEmpty() && !newName.isEmpty() && !json.isEmpty()) {
                writeTextFile(presetsDir + QLatin1Char('/') + newName + QStringLiteral(".json"), json);
                if (oldName != newName) {
                    QFile::remove(presetsDir + QLatin1Char('/') + oldName + QStringLiteral(".json"));
                }
            }
            return QString();
        }
        if (mode == QStringLiteral("delete") || mode == QStringLiteral("trash")) {
            const QString name = args.value(1);
            QFile::remove(presetsDir + QLatin1Char('/') + name + QStringLiteral(".json"));
            return QString();
        }
        if (mode == QStringLiteral("exists")) {
            QString out;
            for (int i = 1; i < args.size(); ++i) {
                if (QFileInfo::exists(normalizeWinPath(args.at(i)))) {
                    out += args.at(i) + QLatin1Char('\n');
                }
            }
            return out;
        }
        return QString();
    }

    if (scriptName == QStringLiteral("default-wallpaper.sh")) {
        if (mode == QStringLiteral("seed")) {
            const QString src = normalizeWinPath(args.value(1));
            const QString dir = normalizeWinPath(args.value(2));
            if (!QFileInfo::exists(src)) {
                exitCode = 1;
                return QString();
            }
            if (!dir.isEmpty() && QDir().mkpath(dir)) {
                const QString dst = QDir(dir).filePath(QFileInfo(src).fileName());
                if (!QFileInfo::exists(dst)) {
                    QFile::copy(src, dst);
                }
                if (QFileInfo::exists(dst)) {
                    return toSlashPath(dst);
                }
            }
            return toSlashPath(src);
        }
    }

    if (scriptName == QStringLiteral("wallpaper-thumbs.sh")) {
        const QString thumbsDir = cacheDir + QStringLiteral("/wallpaper/thumbs");
        const QString stillsDir = cacheDir + QStringLiteral("/wallpaper/stills");
        QDir().mkpath(thumbsDir);
        QDir().mkpath(stillsDir);

        if (mode == QStringLiteral("still")) {
            const QString videoPath = normalizeWinPath(args.value(1));
            QFileInfo fi(videoPath);
            if (!fi.exists() || !fi.isFile()) {
                exitCode = 1;
                return QString();
            }
            const QString key = getMediaKey(videoPath, fi);
            const QString outPath = stillsDir + QStringLiteral("/") + key + QStringLiteral(".jpg");
            if (QFileInfo::exists(outPath) && QFileInfo(outPath).size() > 0) {
                return toSlashPath(outPath) + QLatin1Char('\n');
            }
            if (isMediaVideo(videoPath)) {
                if (extractVideoFrameWin32(videoPath, outPath, 1920, 1080)) {
                    return toSlashPath(outPath) + QLatin1Char('\n');
                }
            } else {
                QImage img;
                if (img.load(videoPath) && img.save(outPath, "JPG", 85)) {
                    return toSlashPath(outPath) + QLatin1Char('\n');
                }
            }
            exitCode = 1;
            return QString();
        }

        if (mode == QStringLiteral("scan")) {
            const QString dirPath = normalizeWinPath(args.value(1));
            QDir dir(dirPath);
            if (!dir.exists()) {
                return QString();
            }
            static const QStringList filters = {
                QStringLiteral("*.png"), QStringLiteral("*.jpg"), QStringLiteral("*.jpeg"),
                QStringLiteral("*.webp"), QStringLiteral("*.bmp"), QStringLiteral("*.mp4"),
                QStringLiteral("*.webm"), QStringLiteral("*.mkv"), QStringLiteral("*.mov"),
                QStringLiteral("*.avi")
            };
            const QFileInfoList entries = dir.entryInfoList(filters, QDir::Files, QDir::Name);
            QString out;
            for (const QFileInfo& fi : entries) {
                const QString fileKey = QDir::fromNativeSeparators(fi.absoluteFilePath());
                const QString key = getMediaKey(fi.absoluteFilePath(), fi);
                const QString thumbPath = thumbsDir + QStringLiteral("/") + key + QStringLiteral(".jpg");
                const bool exists = QFileInfo::exists(thumbPath) && QFileInfo(thumbPath).size() > 0;
                out += fileKey + QLatin1Char('\t')
                    + toSlashPath(thumbPath) + QLatin1Char('\t')
                    + (exists ? QLatin1Char('1') : QLatin1Char('0')) + QLatin1Char('\n');
            }
            return out;
        }

        if (mode == QStringLiteral("make")) {
            const QString dirPath = normalizeWinPath(args.value(1));
            QDir dir(dirPath);
            if (!dir.exists()) {
                return QString();
            }
            static const QStringList filters = {
                QStringLiteral("*.png"), QStringLiteral("*.jpg"), QStringLiteral("*.jpeg"),
                QStringLiteral("*.webp"), QStringLiteral("*.bmp"), QStringLiteral("*.mp4"),
                QStringLiteral("*.webm"), QStringLiteral("*.mkv"), QStringLiteral("*.mov"),
                QStringLiteral("*.avi")
            };
            const QFileInfoList entries = dir.entryInfoList(filters, QDir::Files, QDir::Name);
            QString out;
            for (const QFileInfo& fi : entries) {
                const QString fileKey = QDir::fromNativeSeparators(fi.absoluteFilePath());
                const QString key = getMediaKey(fi.absoluteFilePath(), fi);
                const QString thumbPath = thumbsDir + QStringLiteral("/") + key + QStringLiteral(".jpg");
                if (!QFileInfo::exists(thumbPath) || QFileInfo(thumbPath).size() == 0) {
                    bool ok = false;
                    if (isMediaVideo(fi.absoluteFilePath())) {
                        ok = extractVideoFrameWin32(fi.absoluteFilePath(), thumbPath, 720, 405);
                    } else {
                        ok = extractImageThumb(fi.absoluteFilePath(), thumbPath, 720);
                    }
                    if (ok) {
                        out += QStringLiteral("ok\t") + fileKey + QLatin1Char('\t') + toSlashPath(thumbPath) + QLatin1Char('\n');
                    }
                }
            }
            return out;
        }

        if (mode == QStringLiteral("clear")) {
            QDir(thumbsDir).removeRecursively();
            QDir(stillsDir).removeRecursively();
            return QString();
        }

        return QString();
    }

    if (scriptName == QStringLiteral("pick-image.sh")) {
        if (mode == QStringLiteral("load")) {
            QString out;
            QDir dd(dataDir);
            for (const QString& kind : {QStringLiteral("avatar"), QStringLiteral("banner"), QStringLiteral("phone")}) {
                const QFileInfoList list = dd.entryInfoList(QStringList{kind + QStringLiteral("-*")}, QDir::Files, QDir::Time);
                if (!list.isEmpty()) {
                    out += kind + QLatin1Char(' ') + toSlashPath(list.first().absoluteFilePath()) + QLatin1Char('\n');
                }
            }
            return out;
        }
        if (mode == QStringLiteral("clear")) {
            const QString kind = args.value(1);
            QDir dd(dataDir);
            for (const QFileInfo& fi : dd.entryInfoList(QStringList{kind + QStringLiteral("-*")}, QDir::Files)) {
                QFile::remove(fi.absoluteFilePath());
            }
            return QString();
        }
        if (mode == QStringLiteral("drop")) {
            const QString f = normalizeWinPath(args.value(1));
            if (QFileInfo::exists(f)) {
                QFile::remove(f);
            }
            return QString();
        }
        if (mode == QStringLiteral("pick")) {
            const QString kind = args.value(1);
            QWidget topParent;
            topParent.setWindowFlags(Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint | Qt::Tool);
            const QString picked = QFileDialog::getOpenFileName(
                &topParent,
                QStringLiteral("Choose ") + kind,
                QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
                QStringLiteral("Images (*.png *.jpg *.jpeg *.webp *.gif *.bmp *.svg)")
            );
            if (picked.isEmpty() || !QFileInfo::exists(picked)) {
                exitCode = 1;
                return QString();
            }
            const QString ext = QFileInfo(picked).suffix().toLower();
            const QString dst = QDir(dataDir).filePath(
                QStringLiteral("%1-%2.%3").arg(kind).arg(QDateTime::currentSecsSinceEpoch()).arg(ext)
            );
            QFile::copy(picked, dst);
            return toSlashPath(dst) + QLatin1Char('\n');
        }
    }

    if (scriptName == QStringLiteral("pick-custom-image.sh")) {
        const QString customDir = dataDir + QStringLiteral("/custom-images");
        QDir().mkpath(customDir);
        auto storeImage = [&](const QString& srcFile) -> QString {
            const QString clean = normalizeWinPath(srcFile);
            if (!QFileInfo::exists(clean)) {
                exitCode = 1;
                return QString();
            }
            const QString ext = QFileInfo(clean).suffix().toLower();
            const QString dst = QDir(customDir).filePath(
                QStringLiteral("img-%1-%2.%3")
                    .arg(QDateTime::currentMSecsSinceEpoch())
                    .arg(QCoreApplication::applicationPid())
                    .arg(ext)
            );
            if (!QFile::copy(clean, dst)) {
                exitCode = 1;
                return QString();
            }
            return toSlashPath(dst) + QLatin1Char('\n');
        };
        if (mode == QStringLiteral("import")) {
            return storeImage(args.value(1));
        }
        if (mode == QStringLiteral("remove")) {
            const QString f = normalizeWinPath(args.value(1));
            if (QFileInfo::exists(f)) {
                QFile::remove(f);
            }
            return QString();
        }
        if (mode == QStringLiteral("pick")) {
            QWidget topParent;
            topParent.setWindowFlags(Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint | Qt::Tool);
            const QString picked = QFileDialog::getOpenFileName(
                &topParent,
                QStringLiteral("Choose a picture"),
                QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
                QStringLiteral("Pictures (*.png *.jpg *.jpeg *.webp *.gif *.apng *.bmp *.svg)")
            );
            if (picked.isEmpty()) {
                exitCode = 1;
                return QString();
            }
            return storeImage(picked);
        }
    }

    if (scriptName == QStringLiteral("pick-folder.sh")) {
        const QString startDir = normalizeWinPath(args.value(1, QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)));
        QWidget topParent;
        topParent.setWindowFlags(Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint | Qt::Tool);
        const QString picked = QFileDialog::getExistingDirectory(
            &topParent,
            QStringLiteral("Choose Wallpaper Folder"),
            startDir
        );
        if (picked.isEmpty()) {
            exitCode = 1;
            return QString();
        }
        return toSlashPath(picked) + QLatin1Char('\n');
    }

    if (scriptName == QStringLiteral("equalizer.sh")) {
        const QString eqDir = normalizeWinPath(args.value(0));
        const QString cmd = args.value(1);
        const QString a1 = args.value(2);
        const QString a2 = args.value(3);
        QDir().mkpath(eqDir);
        const QString stateFile = eqDir + QStringLiteral("/eq_state.json");
        const QString commitFile = eqDir + QStringLiteral("/eq_committed.json");
        const QString customFile = eqDir + QStringLiteral("/custom_presets.json");
        const QString autoFile = eqDir + QStringLiteral("/auto_eq_enabled");
        const QString activeFile = eqDir + QStringLiteral("/active_preset");
        const QString keyFile = eqDir + QStringLiteral("/lastfm_api_key");

        const QString defaultState = QStringLiteral(
            "{\"b1\":0,\"b2\":0,\"b3\":0,\"b4\":0,\"b5\":0,\"b6\":0,\"b7\":0,\"b8\":0,\"b9\":0,\"b10\":0,"
            "\"preset\":\"Flat\",\"preamp\":0,\"dim\":0.3,\"pending\":false}\n"
        );
        if (!QFileInfo::exists(stateFile)) writeTextFile(stateFile, defaultState);
        if (!QFileInfo::exists(commitFile)) writeTextFile(commitFile, defaultState);
        if (!QFileInfo::exists(customFile)) writeTextFile(customFile, QStringLiteral("{}\n"));
        if (!QFileInfo::exists(autoFile)) writeTextFile(autoFile, QStringLiteral("false\n"));
        if (!QFileInfo::exists(activeFile)) writeTextFile(activeFile, QStringLiteral("Windows Audio\n"));

        auto loadStateObj = [&]() -> QJsonObject {
            return QJsonDocument::fromJson(readTextFile(stateFile).toUtf8()).object();
        };
        auto saveStateObj = [&](const QJsonObject& obj, bool commit) {
            const QString s = QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)) + QLatin1Char('\n');
            writeTextFile(stateFile, s);
            if (commit) {
                writeTextFile(commitFile, s);
            }
        };

        if (cmd == QStringLiteral("get")) return readTextFile(stateFile);
        if (cmd == QStringLiteral("get_custom")) return readTextFile(customFile);
        if (cmd == QStringLiteral("get_active_preset")) return readTextFile(activeFile);
        if (cmd == QStringLiteral("get_needs_save")) return QStringLiteral("false\n");
        if (cmd == QStringLiteral("get_auto")) return readTextFile(autoFile);
        if (cmd == QStringLiteral("set_auto")) {
            writeTextFile(autoFile, (a1 == QStringLiteral("true") || a1 == QStringLiteral("1")) ? QStringLiteral("true\n") : QStringLiteral("false\n"));
            return QString();
        }
        if (cmd == QStringLiteral("list_presets")) return QStringLiteral("[\"Windows Audio\"]\n");
        if (cmd == QStringLiteral("get_lastfm_key")) return readTextFile(keyFile).trimmed();
        if (cmd == QStringLiteral("set_lastfm_key")) { writeTextFile(keyFile, a1.trimmed()); return QString(); }
        if (cmd == QStringLiteral("genre_tags") || cmd == QStringLiteral("track_genre_tags")) return QStringLiteral("[]\n");
        if (cmd == QStringLiteral("set_band")) {
            QJsonObject obj = loadStateObj();
            obj.insert(QStringLiteral("b") + a1, a2.toDouble());
            obj.insert(QStringLiteral("preset"), QStringLiteral("Custom"));
            obj.insert(QStringLiteral("pending"), true);
            saveStateObj(obj, false);
            return QString();
        }
        if (cmd == QStringLiteral("set_preamp")) {
            QJsonObject obj = loadStateObj();
            obj.insert(QStringLiteral("preamp"), a1.toDouble());
            obj.insert(QStringLiteral("pending"), true);
            saveStateObj(obj, false);
            return QString();
        }
        if (cmd == QStringLiteral("set_dim")) {
            QJsonObject obj = loadStateObj();
            obj.insert(QStringLiteral("dim"), a1.toDouble());
            saveStateObj(obj, false);
            return QString();
        }
        if (cmd == QStringLiteral("save")) {
            QJsonObject obj = loadStateObj();
            obj.insert(QStringLiteral("pending"), false);
            saveStateObj(obj, true);
            return QString();
        }
        if (cmd == QStringLiteral("revert_preview")) {
            if (QFileInfo::exists(commitFile)) {
                writeTextFile(stateFile, readTextFile(commitFile));
            }
            return QString();
        }
        if (cmd == QStringLiteral("preset")) {
            static const QHash<QString, QList<int>> builtIn = {
                {QStringLiteral("Flat"),    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
                {QStringLiteral("Bass"),    {5, 7, 5, 2, 1, 0, 0, 0, 1, 2}},
                {QStringLiteral("Treble"),  {-2, -1, 0, 1, 2, 3, 4, 5, 6, 6}},
                {QStringLiteral("Vocal"),   {-2, -1, 1, 3, 5, 5, 4, 2, 1, 0}},
                {QStringLiteral("Pop"),     {2, 4, 2, 0, 1, 2, 4, 2, 1, 2}},
                {QStringLiteral("Rock"),    {5, 4, 2, -1, -2, -1, 2, 4, 5, 6}},
                {QStringLiteral("Jazz"),    {3, 3, 1, 1, 1, 1, 2, 1, 2, 3}},
                {QStringLiteral("Classic"), {0, 1, 2, 2, 2, 2, 1, 2, 3, 4}}
            };
            QJsonObject obj = loadStateObj();
            if (builtIn.contains(a1)) {
                const QList<int> vals = builtIn.value(a1);
                for (int i = 0; i < 10; ++i) {
                    obj.insert(QStringLiteral("b%1").arg(i + 1), vals.at(i));
                }
                obj.insert(QStringLiteral("preset"), a1);
                obj.insert(QStringLiteral("pending"), false);
                saveStateObj(obj, true);
            } else {
                const QJsonObject customs = QJsonDocument::fromJson(readTextFile(customFile).toUtf8()).object();
                if (customs.contains(a1)) {
                    const QJsonObject c = customs.value(a1).toObject();
                    for (int i = 1; i <= 10; ++i) {
                        const QString k = QStringLiteral("b%1").arg(i);
                        obj.insert(k, c.value(k).toDouble());
                    }
                    obj.insert(QStringLiteral("preset"), a1);
                    obj.insert(QStringLiteral("pending"), false);
                    saveStateObj(obj, true);
                }
            }
            return QString();
        }
        if (cmd == QStringLiteral("save_custom")) {
            QJsonObject customs = QJsonDocument::fromJson(readTextFile(customFile).toUtf8()).object();
            QJsonObject curve;
            QJsonObject obj = loadStateObj();
            for (int i = 0; i < 10; ++i) {
                const QString k = QStringLiteral("b%1").arg(i + 1);
                const double v = args.value(3 + i, QStringLiteral("0")).toDouble();
                curve.insert(k, v);
                obj.insert(k, v);
            }
            customs.insert(a1, curve);
            writeTextFile(customFile, QString::fromUtf8(QJsonDocument(customs).toJson(QJsonDocument::Compact)) + QLatin1Char('\n'));
            obj.insert(QStringLiteral("preset"), a1);
            obj.insert(QStringLiteral("pending"), false);
            saveStateObj(obj, true);
            return QString();
        }
        if (cmd == QStringLiteral("delete_custom")) {
            QJsonObject customs = QJsonDocument::fromJson(readTextFile(customFile).toUtf8()).object();
            customs.remove(a1);
            writeTextFile(customFile, QString::fromUtf8(QJsonDocument(customs).toJson(QJsonDocument::Compact)) + QLatin1Char('\n'));
            return QString();
        }
        return QString();
    }

    if (scriptName == QStringLiteral("status-icons.sh")) {
        if (s_quickSource) {
            const QString wifi = s_quickSource->wifiOn() ? QStringLiteral("enabled") : QStringLiteral("disabled");
            const QString ssid = s_quickSource->wifiOn() ? s_quickSource->wifiName() : QString();
            const QString eth = s_quickSource->ethernetName();
            const QString bt = s_quickSource->bluetoothOn() ? QStringLiteral("yes") : QStringLiteral("no");
            const QString btdev = s_quickSource->bluetoothOn() ? s_quickSource->bluetoothDetail() : QString();
            return QStringLiteral("wifi=%1\nssid=%2\neth=%3\nbt=%4\nbtdev=%5\n")
                .arg(wifi, ssid, eth, bt, btdev);
        }
        return QStringLiteral("wifi=enabled\nssid=Connected\neth=\nbt=yes\nbtdev=\n");
    }

    if (scriptName == QStringLiteral("matugen-state.sh")) {
        if (mode == QStringLiteral("installed")) {
            return QStringLiteral("yes\n");
        }
        if (mode == QStringLiteral("check")) {
            return QStringLiteral("matugen: built-in Material 3 OKLCH engine\n");
        }
        if (mode == QStringLiteral("sources")) {
            const QString imgPath = normalizeWinPath(args.value(1));
            const QStringList cols = Services::MatugenEngine::extractSourceColors(imgPath);
            if (cols.isEmpty()) {
                exitCode = 2;
                return QString();
            }
            return cols.join(QLatin1Char('\n')) + QLatin1Char('\n');
        }
        if (mode == QStringLiteral("apply")) {
            const QString imgPath = normalizeWinPath(args.value(1));
            const QString scheme = args.value(2).isEmpty() ? QStringLiteral("scheme-tonal-spot") : args.value(2);
            const QString outName = args.value(3).isEmpty() ? QStringLiteral("colors") : args.value(3);
            bool okSeed = false;
            const int seed = args.value(4).toInt(&okSeed);
            bool okContrast = false;
            const double contrast = args.value(5).toDouble(&okContrast);
            QString err;
            if (!Services::MatugenEngine::applyToCache(
                    imgPath,
                    scheme,
                    outName,
                    okSeed ? seed : 0,
                    okContrast ? contrast : 0.0,
                    cacheDir + QStringLiteral("/matugen"),
                    nullptr,
                    &err)) {
                exitCode = 2;
            }
            return QString();
        }
        return QString();
    }

    if (scriptName == QStringLiteral("update-check.sh")) {
        if (mode == QStringLiteral("info")) {
            return QStringLiteral("branch=stable-1.0\nrev=1.0.0\nclean=1\n");
        }
        if (mode == QStringLiteral("check")) {
            return QStringLiteral("ahead=0\nbehind=0\n");
        }
        return QString();
    }

    if (scriptName == QStringLiteral("pkg-updates.sh")) {
        return QStringLiteral("0\n");
    }

    if (scriptName == QStringLiteral("app-fonts.sh")) {
        if (mode == QStringLiteral("get")) {
            return QStringLiteral("ui=Segoe UI Variable\nmono=Consolas\n");
        }
        return QStringLiteral("Segoe UI Variable\nSegoe UI\n--mono--\nConsolas\nCascadia Mono\n");
    }

    if (scriptName == QStringLiteral("hyprland-settings.sh") || scriptName == QStringLiteral("keybinds.sh")) {
        if (mode == QStringLiteral("check")) {
            return QStringLiteral("ok\n");
        }
        return QString();
    }

    return QString();
}

} // namespace

// ============================================================================
// QsSplitParser
// ============================================================================

void QsSplitParser::feedChunk(const QString& chunk) {
    if (m_splitMarker.isEmpty()) {
        if (!chunk.isEmpty()) {
            Q_EMIT read(chunk);
        }
        return;
    }
    const QStringList parts = chunk.split(m_splitMarker);
    for (const QString& p : parts) {
        if (!p.isEmpty()) {
            Q_EMIT read(p);
        }
    }
}

// ============================================================================
// QsProcess
// ============================================================================

void QsProcess::initBackends(
    Services::AudioSpectrumSource* spectrumSource,
    Services::QuickSource* quickSource,
    Services::StatusIconsService* statusIconsService,
    Win32::WindowTracker* windowTracker,
    Services::MediaSource* mediaSource
) {
    s_spectrumSource = spectrumSource;
    s_mediaSource = mediaSource;
    s_quickSource = quickSource;
    s_statusIconsService = statusIconsService;
    s_windowTracker = windowTracker;
    clipHistory().ensureInit();
}

void QsProcess::executeDetachedCommand(const QStringList& rawArgs) {
    if (rawArgs.isEmpty()) {
        return;
    }
    QStringList args = rawArgs;
    while (!args.isEmpty()) {
        const QString& head = args.first();
        if (head == QStringLiteral("setsid") || head == QStringLiteral("env")) {
            args.removeFirst();
        } else if (head.contains(QLatin1Char('=')) && !head.startsWith(QLatin1Char('-'))) {
            args.removeFirst();
        } else {
            break;
        }
    }
    if (args.isEmpty()) {
        return;
    }
    const QString cmd0 = QFileInfo(args.first()).fileName().toLower();

    // 1. Self-IPC calls: ["qs", "-c", "sekaipop", "ipc", "call", target, fn, ...args]
    if (cmd0 == QStringLiteral("qs")) {
        const int callIdx = args.indexOf(QStringLiteral("call"));
        if (callIdx >= 0 && callIdx + 2 < args.size()) {
            const QString target = args.at(callIdx + 1);
            const QString fn = args.at(callIdx + 2);
            QsIpcHandler::dispatchCall(target, fn, args.mid(callIdx + 3));
        }
        return;
    }

    // 2. Native Windows Lock trigger
    if (cmd0 == QStringLiteral("win-lock")) {
        if (s_quickSource) {
            s_quickSource->lockWorkstation();
        } else {
            LockWorkStation();
        }
        return;
    }

    // 3. URL / File / Folder opener (xdg-open)
    if (cmd0 == QStringLiteral("xdg-open")) {
        const QString target = args.value(1);
        if (!target.isEmpty()) {
            if (target.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive) ||
                target.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive) ||
                target.startsWith(QStringLiteral("ms-settings:"), Qt::CaseInsensitive)) {
                Win32::launchShellTarget(target);
            } else {
                Win32::launchShellTarget(QDir::toNativeSeparators(normalizeWinPath(target)));
            }
        }
        return;
    }

    // 4. Native Windows Clipboard, Clipboard History, & Virtual Typing (wl-copy / cliphist / wtype)
    if (cmd0 == QStringLiteral("wtype")) {
        sendWin32Wtype(args);
        return;
    }
    if (cmd0 == QStringLiteral("wl-copy")) {
        QString text;
        for (int i = 1; i < args.size(); ++i) {
            if (args.at(i) == QStringLiteral("--")) {
                text = args.mid(i + 1).join(QLatin1Char(' '));
                break;
            }
            if (!args.at(i).startsWith(QLatin1Char('-'))) {
                text = args.mid(i).join(QLatin1Char(' '));
                break;
            }
        }
        if (QClipboard* cb = QGuiApplication::clipboard()) {
            cb->setText(text);
        }
        return;
    }
    if (cmd0 == QStringLiteral("cliphist")) {
        if (args.value(1) == QStringLiteral("wipe")) {
            clipHistory().wipe();
        }
        return;
    }

    // 5. Color picker (hyprpicker) -> sample pixel under cursor & copy HEX
    if (cmd0 == QStringLiteral("hyprpicker")) {
        sampleCursorPixelHex();
        return;
    }

    // 6. Display settings (nwg-displays)
    if (cmd0 == QStringLiteral("nwg-displays")) {
        Win32::launchShellTarget(QStringLiteral("ms-settings:display"));
        return;
    }

    // 7. Directory creation (mkdir -p)
    if (cmd0 == QStringLiteral("mkdir")) {
        for (int i = 1; i < args.size(); ++i) {
            if (!args.at(i).startsWith(QLatin1Char('-'))) {
                QDir().mkpath(normalizeWinPath(args.at(i)));
            }
        }
        return;
    }

    // 8. Shell scripts and inline sh/bash -c commands
    if (cmd0 == QStringLiteral("sh") || cmd0 == QStringLiteral("bash")) {
        if (args.size() >= 2 && args.at(1) != QStringLiteral("-c")) {
            int code = 0;
            const QString scriptName = QFileInfo(args.at(1)).fileName();
            if (scriptName == QStringLiteral("wallhaven.sh") && args.value(2) == QStringLiteral("preload")) {
                preloadOnlineWallpaperUrl(args.value(3));
                return;
            }
            handleStateScript(scriptName, args.mid(2), code);
            return;
        }
        if (args.size() >= 3 && args.at(1) == QStringLiteral("-c")) {
            const QString script = args.at(2).trimmed();

            // Delayed execution helper: "sleep $1; shift; exec $@"
            if (script.contains(QStringLiteral("sleep")) && script.contains(QStringLiteral("hyprpicker"))) {
                QTimer::singleShot(900, QCoreApplication::instance(), []() {
                    sampleCursorPixelHex();
                });
                return;
            }

            // Screenshot region (grim + slurp) -> SekaiPop Native Multi-Monitor Screenshot Overlay
            if (script.contains(QStringLiteral("grim")) || script.contains(QStringLiteral("slurp"))) {
                if (s_quickSource) {
                    s_quickSource->triggerScreenSnip();
                } else {
                    QTimer::singleShot(160, []() {
                        Win32::ScreenshotOverlay::startRegionSnip();
                    });
                }
                return;
            }

            // Clipboard history decode / delete
            if (script.contains(QStringLiteral("cliphist decode"))) {
                clipHistory().copyById(args.value(4));
                return;
            }
            if (script.contains(QStringLiteral("cliphist delete"))) {
                clipHistory().deleteById(args.value(4));
                return;
            }

            // Delayed wtype typing helper (ExtrasSource.qml)
            if (script.contains(QStringLiteral("wtype"))) {
                const QString payload = args.value(4);
                if (!payload.isEmpty()) {
                    QTimer::singleShot(350, QCoreApplication::instance(), [payload]() {
                        sendWin32UnicodeText(payload);
                    });
                }
                return;
            }

            // Timer alarm chime (canberra-gtk-play / paplay -> native Windows MessageBeep)
            if (script.contains(QStringLiteral("canberra-gtk-play")) || script.contains(QStringLiteral("paplay"))) {
                MessageBeep(MB_ICONASTERISK);
                return;
            }

            // Reveal file in Windows Explorer (FileManager1 ShowItems)
            if (script.contains(QStringLiteral("FileManager1.ShowItems"))) {
                QString pathArg = args.value(4);
                if (pathArg.startsWith(QStringLiteral("file://"), Qt::CaseInsensitive)) {
                    pathArg = QUrl(pathArg).toLocalFile();
                }
                pathArg = normalizeWinPath(pathArg);
                if (!pathArg.isEmpty() && QFileInfo::exists(pathArg)) {
                    QProcess::startDetached(QStringLiteral("explorer.exe"),
                                            {QStringLiteral("/select,") + QDir::toNativeSeparators(pathArg)});
                } else {
                    const QString dir = normalizeWinPath(args.value(5));
                    QProcess::startDetached(QStringLiteral("explorer.exe"),
                                            {QDir::toNativeSeparators(dir.isEmpty() ? QDir::homePath() : dir)});
                }
                return;
            }

            // Empty / Open Windows Recycle Bin (gio trash)
            if (script.contains(QStringLiteral("gio trash --empty"))) {
                SHEmptyRecycleBinW(nullptr, nullptr, SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI | SHERB_NOSOUND);
                return;
            }
            if (script.contains(QStringLiteral("gio open trash:///"))) {
                Win32::launchShellTarget(QStringLiteral("shell:RecycleBinFolder"));
                return;
            }

            // Pipe into wl-copy (Palette HEX, Shelf, Files, Emoji, Keybinds, ReloadPopup)
            if (script.contains(QStringLiteral("wl-copy"))) {
                const QString payload = args.size() >= 5 ? args.mid(4).join(QStringLiteral("\n\n")) : QString();
                if (QClipboard* cb = QGuiApplication::clipboard()) {
                    cb->setText(payload);
                }
                return;
            }

            // Dock user folders: xdg-user-dir DOWNLOAD / DOCUMENTS / PICTURES / MUSIC / VIDEOS / DESKTOP
            if (script.contains(QStringLiteral("xdg-user-dir"))) {
                const QString dir = resolveWindowsUserDir(args.value(4));
                Win32::launchShellTarget(QDir::toNativeSeparators(dir));
                return;
            }

            // Open parent folder: xdg-open "$(dirname "$1")"
            if (script.contains(QStringLiteral("dirname")) && script.contains(QStringLiteral("xdg-open"))) {
                const QString file = normalizeWinPath(args.value(4));
                const QString dir = QFileInfo(file).absolutePath();
                Win32::launchShellTarget(QDir::toNativeSeparators(dir));
                return;
            }

            // mkdir -p "$1" && xdg-open "$1" or plain xdg-open
            if (script.contains(QStringLiteral("xdg-open"))) {
                const QString target = args.size() >= 5 ? normalizeWinPath(args.at(4)) : QDir::homePath();
                if (script.contains(QStringLiteral("mkdir"))) {
                    QDir().mkpath(target);
                }
                Win32::launchShellTarget(QDir::toNativeSeparators(target));
                return;
            }

            // Dock Files button default Linux file managers -> Windows Explorer
            const QString lowScript = script.toLower();
            if (lowScript == QStringLiteral("dolphin") || lowScript == QStringLiteral("nautilus") ||
                lowScript == QStringLiteral("thunar") || lowScript == QStringLiteral("nemo") ||
                lowScript == QStringLiteral("pcmanfm") || lowScript == QStringLiteral("yazi") ||
                lowScript == QStringLiteral("files") || lowScript.isEmpty()) {
                Win32::launchShellTarget(QStringLiteral("explorer.exe"));
                return;
            }

            // rm -f cleanup
            if (script.startsWith(QStringLiteral("rm -f"))) {
                const QString target = normalizeWinPath(args.value(4));
                if (!target.isEmpty() && QFileInfo::exists(target)) {
                    QFile::remove(target);
                }
                return;
            }
        }
        return;
    }

    // Ignore Linux-only helper commands cleanly (never spawn missing POSIX binaries)
    if (cmd0 == QStringLiteral("notify-send") || cmd0 == QStringLiteral("hyprctl") ||
        cmd0 == QStringLiteral("pactl") || cmd0 == QStringLiteral("pkill") ||
        cmd0 == QStringLiteral("pkexec") || cmd0 == QStringLiteral("pacman") ||
        cmd0 == QStringLiteral("paru") || cmd0 == QStringLiteral("yay") ||
        cmd0 == QStringLiteral("checkupdates") || cmd0 == QStringLiteral("hyprpm") ||
        cmd0 == QStringLiteral("systemctl") || cmd0 == QStringLiteral("loginctl") ||
        cmd0 == QStringLiteral("nmcli") || cmd0 == QStringLiteral("bluetoothctl") ||
        cmd0 == QStringLiteral("grim") || cmd0 == QStringLiteral("slurp") ||
        cmd0 == QStringLiteral("warp-cli") || cmd0 == QStringLiteral("fprintd-verify")) {
        return;
    }

    // Native Win32 ShellExecuteW launch (supports .exe, .lnk shortcuts, UWP shell:AppsFolder\..., and URLs)
    const QString target = args.first();
    const QString params = args.size() > 1 ? args.mid(1).join(QLatin1Char(' ')) : QString();
    Win32::launchShellTarget(target, params);
}

QsProcess::QsProcess(QObject* parent)
    : QObject(parent) {}

QsProcess::~QsProcess() {
    stopExecution();
}

void QsProcess::setRunning(bool r) {
    if (m_running == r) {
        return;
    }
    if (r) {
        if (m_complete) {
            startExecution();
        } else {
            m_running = true;
            Q_EMIT runningChanged();
        }
    } else {
        stopExecution();
    }
}

void QsProcess::setCommand(const QVariant& c) {
    if (m_command != c) {
        m_command = c;
        Q_EMIT commandChanged();
    }
}

void QsProcess::setWorkingDirectory(const QString& d) {
    if (m_workingDir != d) {
        m_workingDir = d;
        Q_EMIT workingDirectoryChanged();
    }
}

void QsProcess::setEnvironment(const QVariantMap& e) {
    if (m_env != e) {
        m_env = e;
        Q_EMIT environmentChanged();
    }
}

void QsProcess::setStdinEnabled(bool e) {
    if (m_stdinEnabled != e) {
        m_stdinEnabled = e;
        Q_EMIT stdinEnabledChanged();
    }
}

void QsProcess::setStdoutParser(QObject* p) {
    if (m_stdout != p) {
        m_stdout = p;
        Q_EMIT stdoutParserChanged();
    }
}

void QsProcess::setStderrParser(QObject* p) {
    if (m_stderr != p) {
        m_stderr = p;
        Q_EMIT stderrParserChanged();
    }
}

void QsProcess::exec(const QVariant& cmd) {
    setCommand(cmd);
    stopExecution();
    startExecution();
}

void QsProcess::write(const QString& data) {
    if (m_discordBridge) {
        m_discordBridge->sendCommand(data);
        return;
    }
    if (m_childProc && m_childProc->state() == QProcess::Running) {
        m_childProc->write(data.toUtf8());
    }
}

void QsProcess::signal(int sig) {
    const bool wasRunning = m_running;
    stopExecution();
    if (wasRunning) {
        Q_EMIT exited(128 + (sig > 0 ? sig : 15), 0);
    }
}

void QsProcess::componentComplete() {
    m_complete = true;
    if (m_running) {
        m_running = false;
        startExecution();
    }
}

QStringList QsProcess::parseCommandArgs(const QVariant& cmd) const {
    QStringList out;
    if (cmd.typeId() == QMetaType::QStringList) {
        return cmd.toStringList();
    }
    if (cmd.canConvert<QVariantList>()) {
        for (const QVariant& v : cmd.toList()) {
            out.append(v.toString());
        }
    } else if (cmd.typeId() == QMetaType::QString) {
        const QString s = cmd.toString().trimmed();
        if (!s.isEmpty()) {
            out.append(s);
        }
    }
    return out;
}

void QsProcess::stopExecution() {
    ++m_runGeneration;
    if (m_activeReply) {
        QNetworkReply* r = m_activeReply;
        m_activeReply = nullptr;
        r->abort();
    }
    if (m_discordBridge) {
        Services::DiscordVoiceBridge* b = m_discordBridge;
        m_discordBridge = nullptr;
        b->disconnect(this);
        b->stop();
        b->deleteLater();
    }
    if (m_childProc) {
        QProcess* p = m_childProc;
        m_childProc = nullptr;
        p->disconnect(this);
        if (p->state() != QProcess::NotRunning) {
            p->kill();
            p->waitForFinished(300);
        }
        p->deleteLater();
    }
    m_childStdoutBuf.clear();
    m_lastStreamLine.clear();
    if (m_isCavaStream) {
        m_isCavaStream = false;
        s_activeCavaCount = qMax(0, s_activeCavaCount - 1);
        if (s_spectrumSource) {
            disconnect(s_spectrumSource, &Services::AudioSpectrumSource::updated,
                       this, &QsProcess::onCavaSpectrumTick);
            if (s_activeCavaCount == 0) {
                s_spectrumSource->setEnabled(false);
            }
        }
    }
    if (m_running) {
        m_running = false;
        Q_EMIT runningChanged();
    }
}

void QsProcess::onCavaSpectrumTick() {
    if (!m_running || !m_isCavaStream || !s_spectrumSource) {
        return;
    }
    const QVariantList pts = s_spectrumSource->levels();
    if (pts.isEmpty()) {
        return;
    }
    QString line;
    line.reserve(m_cavaBars * 5);
    for (int i = 0; i < m_cavaBars; ++i) {
        const int srcIdx = (i * static_cast<int>(pts.size())) / qMax(1, m_cavaBars);
        const double v = pts.at(qBound(0, srcIdx, static_cast<int>(pts.size()) - 1)).toDouble();
        const int scaled = qBound(0, qRound(v * 1000.0), 1000);
        line.append(QString::number(scaled));
        line.append(QLatin1Char(';'));
    }
    if (auto* sp = qobject_cast<QsSplitParser*>(m_stdout)) {
        sp->feedLine(line);
    }
}

void QsProcess::finishWithOutput(const QString& out, const QString& err, int exitCode) {
    if (!err.isEmpty()) {
        if (auto* col = qobject_cast<QsStdioCollector*>(m_stderr)) {
            col->appendAndFinish(err);
        } else if (auto* sp = qobject_cast<QsSplitParser*>(m_stderr)) {
            sp->feedChunk(err);
        }
    }
    if (auto* col = qobject_cast<QsStdioCollector*>(m_stdout)) {
        col->appendAndFinish(out);
    } else if (auto* sp = qobject_cast<QsSplitParser*>(m_stdout)) {
        sp->feedChunk(out);
    }
    m_running = false;
    Q_EMIT runningChanged();
    Q_EMIT exited(exitCode, 0);
}

void QsProcess::startExecution() {
    const QStringList rawArgs = parseCommandArgs(m_command);
    if (rawArgs.isEmpty()) {
        return;
    }

    // Strip leading env / ENV=VAL / setsid / setpriv / nice wrappers
    QStringList args = rawArgs;
    while (!args.isEmpty()) {
        const QString& head = args.first();
        if (head == QStringLiteral("env") || head == QStringLiteral("setsid")) {
            args.removeFirst();
        } else if (head.contains(QLatin1Char('=')) && !head.startsWith(QLatin1Char('-'))) {
            args.removeFirst();
        } else if (head == QStringLiteral("setpriv") && args.size() >= 3) {
            args = args.mid(3);
        } else if (head == QStringLiteral("nice") && args.size() >= 3) {
            args = args.mid(3);
        } else {
            break;
        }
    }
    if (args.isEmpty()) {
        return;
    }

    m_running = true;
    Q_EMIT runningChanged();
    Q_EMIT started();
    const int gen = ++m_runGeneration;

    const QString prog = QFileInfo(args.first()).fileName().toLower();

    // 1. Cava live audio spectrum stream
    if (prog == QStringLiteral("cava")) {
        if (!m_isCavaStream) {
            m_isCavaStream = true;
            ++s_activeCavaCount;
        }
        m_cavaBars = args.join(QLatin1Char(' ')).contains(QStringLiteral("cava-pc.conf")) ? 50 : 24;
        if (s_spectrumSource) {
            s_spectrumSource->setEnabled(true);
            connect(s_spectrumSource, &Services::AudioSpectrumSource::updated,
                    this, &QsProcess::onCavaSpectrumTick, Qt::UniqueConnection);
        }
        return;
    }

    // 2. Python scripts (sekai_analyzer.py -> exit 127 to use cava; discord_voice_bridge.py -> stay idle)
    if (prog == QStringLiteral("python3") || prog == QStringLiteral("python")) {
        const QString script = args.value(1);
        if (script.contains(QStringLiteral("sekai_analyzer.py"))) {
            QTimer::singleShot(10, this, [this, gen]() {
                if (m_runGeneration == gen && m_running) {
                    finishWithOutput(QString(), QString(), 127);
                }
            });
            return;
        }
        if (script.contains(QStringLiteral("discord_voice_bridge.py"))) {
            auto* bridge = new Services::DiscordVoiceBridge(this);
            m_discordBridge = bridge;
            connect(bridge, &Services::DiscordVoiceBridge::lineReady, this,
                    [this, gen](const QString& line) {
                        if (m_runGeneration != gen || !m_running) {
                            return;
                        }
                        if (auto* sp = qobject_cast<QsSplitParser*>(m_stdout)) {
                            sp->feedLine(line);
                        } else if (auto* col = qobject_cast<QsStdioCollector*>(m_stdout)) {
                            col->appendAndFinish(line + QLatin1Char('\n'));
                        }
                    }, Qt::QueuedConnection);
            bridge->start();
            return;
        }
    }

    // 3. Long-running daemons (pactl subscribe, wl-paste --watch, systemd-inhibit)
    if ((prog == QStringLiteral("pactl") && args.value(1) == QStringLiteral("subscribe")) ||
        prog == QStringLiteral("wl-paste") ||
        prog == QStringLiteral("systemd-inhibit")) {
        return;
    }

    // 3b. Native Ukikura multi-source lyrics engine (YouTube CC + Forced Alignment + Romaji + Sub Indo + Cover fallback)
    if (prog == QStringLiteral("ukikura-lyrics")) {
        const QString title = args.value(1);
        const QString artist = args.value(2);
        const QString durStr = args.value(3, QStringLiteral("0"));
        const QString idxPath = findUkikuraLyricsIndexPath();
        const QString nodeExe = findNodeExecutablePath();

        if (title.trimmed().isEmpty() || idxPath.isEmpty()) {
            QTimer::singleShot(0, this, [this, gen]() {
                if (m_runGeneration == gen && m_running) {
                    finishWithOutput(QString(), QStringLiteral("ukikura-lyrics: engine unavailable\n"), 1);
                }
            });
            return;
        }

        QString escapedIdx = idxPath;
        escapedIdx.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
        escapedIdx.replace(QLatin1Char('\''), QStringLiteral("\\'"));

        const QString script = QStringLiteral(
            "const {getCompleteSyncedLyrics}=require('%1');"
            "const t=process.argv[1]||'';const a=process.argv[2]||'';const d=Number(process.argv[3])||0;"
            "getCompleteSyncedLyrics(t,a,d,(fast)=>{"
            "if(fast&&((fast.lines&&fast.lines.length)||fast.coverB64)){"
            "process.stdout.write('FAST:'+JSON.stringify(fast)+'\\n');}"
            "}).then(res=>{"
            "if(res&&((res.lines&&res.lines.length)||res.coverB64)){"
            "process.stdout.write('FINAL:'+JSON.stringify(res)+'\\n',()=>process.exit(0));"
            "setTimeout(()=>process.exit(0),500);"
            "}else{"
            "process.stdout.write('EMPTY:{}\\n',()=>process.exit(0));"
            "}}).catch(e=>{"
            "process.stderr.write(String(e&&e.message||e)+'\\n');"
            "process.exit(1);});"
        ).arg(escapedIdx);

        auto* proc = new QProcess(this);
        m_childProc = proc;
        m_childStdoutBuf.clear();
        m_lastStreamLine.clear();
        proc->setProcessChannelMode(QProcess::SeparateChannels);
#ifdef Q_OS_WIN
        proc->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* cpArgs) {
            cpArgs->flags |= CREATE_NO_WINDOW;
        });
#endif

        auto handleStreamLine = [this, title, artist](const QByteArray& rawLine) {
            const QByteArray trimmed = rawLine.trimmed();
            if (trimmed.isEmpty()) return;

            const bool isFast = trimmed.startsWith("FAST:");
            const bool isFinal = trimmed.startsWith("FINAL:");
            const bool isEmpty = trimmed.startsWith("EMPTY:");
            if (!isFast && !isFinal && !isEmpty) return;

            QString outLine;
            if (isFast || isFinal) {
                const int prefixLen = isFast ? 5 : 6;
                const QByteArray jsonBytes = trimmed.mid(prefixLen);
                QJsonParseError perr{};
                const QJsonDocument doc = QJsonDocument::fromJson(jsonBytes, &perr);
                if (perr.error == QJsonParseError::NoError && doc.isObject()) {
                    QJsonObject obj = doc.object();
                    const QString coverB64 = obj.value(QStringLiteral("coverB64")).toString();
                    if (!coverB64.isEmpty()) {
                        const QByteArray imgBytes = QByteArray::fromBase64(coverB64.toLatin1());
                        const QString cachedCover = Services::MediaSource::saveCoverBytesToCache(imgBytes, QStringLiteral("ukikura"));
                        if (!cachedCover.isEmpty()) {
                            obj.insert(QStringLiteral("coverFile"), cachedCover);
                            if (s_mediaSource) {
                                s_mediaSource->setFallbackCover(title, artist, cachedCover);
                            }
                        }
                        // Strip large base64 blob before handing JSON to QML thread.
                        obj.remove(QStringLiteral("coverB64"));
                    }
                    outLine = (isFast ? QStringLiteral("FAST:") : QStringLiteral("FINAL:"))
                            + QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
                } else {
                    outLine = QString::fromUtf8(trimmed);
                }
            } else {
                outLine = QStringLiteral("EMPTY:{}");
            }

            m_lastStreamLine = outLine;
            if (auto* sp = qobject_cast<QsSplitParser*>(m_stdout)) {
                sp->feedLine(outLine);
            }
        };

        connect(proc, &QProcess::readyReadStandardOutput, this, [this, proc, gen, handleStreamLine]() {
            if (m_runGeneration != gen || !m_running || m_childProc != proc) return;
            m_childStdoutBuf.append(proc->readAllStandardOutput());
            int nl = m_childStdoutBuf.indexOf('\n');
            while (nl >= 0) {
                const QByteArray line = m_childStdoutBuf.left(nl);
                m_childStdoutBuf.remove(0, nl + 1);
                handleStreamLine(line);
                nl = m_childStdoutBuf.indexOf('\n');
            }
        });

        connect(proc, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                [this, proc, gen, handleStreamLine](int exitCode, QProcess::ExitStatus) {
            if (m_childProc == proc) {
                m_childProc = nullptr;
            }
            const QByteArray tail = proc->readAllStandardOutput();
            const QString errText = QString::fromUtf8(proc->readAllStandardError());
            proc->deleteLater();
            if (m_runGeneration != gen || !m_running) return;

            if (!tail.isEmpty()) {
                m_childStdoutBuf.append(tail);
            }
            while (!m_childStdoutBuf.isEmpty()) {
                int nl = m_childStdoutBuf.indexOf('\n');
                if (nl < 0) {
                    handleStreamLine(m_childStdoutBuf);
                    m_childStdoutBuf.clear();
                } else {
                    handleStreamLine(m_childStdoutBuf.left(nl));
                    m_childStdoutBuf.remove(0, nl + 1);
                }
            }

            if (auto* col = qobject_cast<QsStdioCollector*>(m_stdout)) {
                col->appendAndFinish(m_lastStreamLine);
            }
            if (!errText.isEmpty()) {
                if (auto* ecol = qobject_cast<QsStdioCollector*>(m_stderr)) {
                    ecol->appendAndFinish(errText);
                }
            }
            m_running = false;
            Q_EMIT runningChanged();
            Q_EMIT exited(exitCode, 0);
        });

        connect(proc, &QProcess::errorOccurred, this, [this, proc, gen](QProcess::ProcessError) {
            if (m_childProc == proc) {
                m_childProc = nullptr;
            }
            const QString errStr = proc->errorString();
            proc->deleteLater();
            if (m_runGeneration != gen || !m_running) return;
            finishWithOutput(QString(), errStr, 1);
        });

        // 22-second in-flight timeout (respects Ukikura >=18s rule for multi-track YouTube CC + Forced Alignment)
        QTimer::singleShot(22000, this, [this, proc, gen]() {
            if (m_runGeneration != gen || !m_running || m_childProc != proc) return;
            qWarning("[ukikura-lyrics] timed out after 22s for '%s'", qPrintable(m_lastStreamLine.left(40)));
            m_childProc = nullptr;
            proc->disconnect(this);
            proc->kill();
            proc->deleteLater();
            if (auto* col = qobject_cast<QsStdioCollector*>(m_stdout)) {
                col->appendAndFinish(m_lastStreamLine);
            }
            m_running = false;
            Q_EMIT runningChanged();
            Q_EMIT exited(m_lastStreamLine.isEmpty() ? 124 : 0, 0);
        });

        proc->start(nodeExe, QStringList{ QStringLiteral("-e"), script, title, artist, durStr });
        return;
    }

    // 3c. Non-blocking sleep (used by LyricsService finalize wait timer)
    if (prog == QStringLiteral("sleep")) {
        const double sec = args.value(1).toDouble();
        const int ms = qBound(10, qRound(sec * 1000.0), 120000);
        QTimer::singleShot(ms, this, [this, gen]() {
            if (m_runGeneration == gen && m_running) {
                finishWithOutput(QString(), QString(), 0);
            }
        });
        return;
    }

    // 4. Curl HTTP requests (Open-Meteo weather, Lyrics providers, Wallhaven, DuckDuckGo autocomplete)
    if (prog == QStringLiteral("curl")) {
        QString urlStr;
        QString outFile;
        QString method;
        QString writeOutFmt;
        QString customUa;
        QByteArray rawBody;
        bool hasRawBody = false;
        bool isGetWithQuery = false;
        int maxTimeSec = 12;
        QUrlQuery query;
        QMap<QByteArray, QByteArray> headers;

        for (int i = 1; i < args.size(); ++i) {
            const QString& a = args.at(i);
            if (a == QStringLiteral("-o") && i + 1 < args.size()) {
                outFile = normalizeWinPath(args.at(++i));
            } else if (a == QStringLiteral("-G") || a == QStringLiteral("--get")) {
                isGetWithQuery = true;
            } else if ((a == QStringLiteral("-X") || a == QStringLiteral("--request")) && i + 1 < args.size()) {
                method = args.at(++i).trimmed().toUpper();
            } else if ((a == QStringLiteral("--data-binary") || a == QStringLiteral("--data-raw")) && i + 1 < args.size()) {
                rawBody = args.at(++i).toUtf8();
                hasRawBody = true;
            } else if (a == QStringLiteral("--data-urlencode") && i + 1 < args.size()) {
                const QString kv = args.at(++i);
                const int eq = kv.indexOf(QLatin1Char('='));
                if (eq > 0) {
                    query.addQueryItem(kv.left(eq), kv.mid(eq + 1));
                }
            } else if ((a == QStringLiteral("-d") || a == QStringLiteral("--data")) && i + 1 < args.size()) {
                const QString val = args.at(++i);
                if (isGetWithQuery) {
                    const int eq = val.indexOf(QLatin1Char('='));
                    if (eq > 0) {
                        query.addQueryItem(val.left(eq), val.mid(eq + 1));
                    }
                } else {
                    rawBody = val.toUtf8();
                    hasRawBody = true;
                }
            } else if ((a == QStringLiteral("-H") || a == QStringLiteral("--header")) && i + 1 < args.size()) {
                const QString h = args.at(++i);
                const int col = h.indexOf(QLatin1Char(':'));
                if (col > 0) {
                    headers.insert(h.left(col).trimmed().toUtf8(), h.mid(col + 1).trimmed().toUtf8());
                }
            } else if ((a == QStringLiteral("-w") || a == QStringLiteral("--write-out")) && i + 1 < args.size()) {
                writeOutFmt = args.at(++i);
            } else if ((a == QStringLiteral("-m") || a == QStringLiteral("--max-time")) && i + 1 < args.size()) {
                const int t = qRound(args.at(++i).toDouble());
                if (t > 0) maxTimeSec = t;
            } else if ((a == QStringLiteral("-A") || a == QStringLiteral("--user-agent")) && i + 1 < args.size()) {
                customUa = args.at(++i);
            } else if (a.startsWith(QStringLiteral("http://")) || a.startsWith(QStringLiteral("https://"))) {
                urlStr = a;
            }
        }

        if (urlStr.isEmpty()) {
            QTimer::singleShot(0, this, [this, gen, writeOutFmt]() {
                if (m_runGeneration == gen && m_running) {
                    if (!writeOutFmt.isEmpty()) {
                        QString w = writeOutFmt;
                        w.replace(QStringLiteral("\\n"), QStringLiteral("\n"));
                        w.replace(QStringLiteral("%{http_code}"), QStringLiteral("0"));
                        finishWithOutput(w, QString(), 0);
                    } else {
                        finishWithOutput(QString(), QString(), 1);
                    }
                }
            });
            return;
        }

        QUrl url(urlStr);
        if (isGetWithQuery && !query.isEmpty()) {
            QUrlQuery existing(url);
            for (const auto& pair : query.queryItems()) {
                existing.addQueryItem(pair.first, pair.second);
            }
            url.setQuery(existing);
        }

        QNetworkRequest req(url);
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        req.setTransferTimeout(qBound(2000, maxTimeSec * 1000, 30000));
        req.setHeader(QNetworkRequest::UserAgentHeader,
                      customUa.isEmpty() ? QStringLiteral("SekaiPopShell/1.0 (Windows NT 10.0; Win64; x64)") : customUa);
        for (auto it = headers.begin(); it != headers.end(); ++it) {
            req.setRawHeader(it.key(), it.value());
        }

        const bool isPost = (method == QStringLiteral("POST")) || (method.isEmpty() && hasRawBody && !isGetWithQuery);
        QNetworkReply* reply = isPost ? sharedNam()->post(req, rawBody) : sharedNam()->get(req);
        m_activeReply = reply;
        connect(reply, &QNetworkReply::finished, this, [this, reply, outFile, writeOutFmt, gen]() {
            if (m_activeReply == reply) {
                m_activeReply = nullptr;
            }
            reply->deleteLater();
            if (m_runGeneration != gen || !m_running) {
                return;
            }
            const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QByteArray bytes = reply->isOpen() ? reply->readAll() : QByteArray();

            if (!writeOutFmt.isEmpty()) {
                QString w = writeOutFmt;
                w.replace(QStringLiteral("\\n"), QStringLiteral("\n"));
                w.replace(QStringLiteral("%{http_code}"), QString::number(httpStatus));
                if (!outFile.isEmpty() && httpStatus >= 200 && httpStatus < 300) {
                    QDir().mkpath(QFileInfo(outFile).absolutePath());
                    QSaveFile f(outFile);
                    if (f.open(QIODevice::WriteOnly)) {
                        f.write(bytes);
                        f.commit();
                    }
                    finishWithOutput(w, QString(), 0);
                } else if (httpStatus > 0) {
                    finishWithOutput(QString::fromUtf8(bytes) + w, QString(), 0);
                } else {
                    finishWithOutput(w, reply->errorString(), 0);
                }
                return;
            }

            if (reply->error() != QNetworkReply::NoError) {
                finishWithOutput(QString(), reply->errorString(), 1);
                return;
            }
            if (!outFile.isEmpty()) {
                QDir().mkpath(QFileInfo(outFile).absolutePath());
                QSaveFile f(outFile);
                if (f.open(QIODevice::WriteOnly)) {
                    f.write(bytes);
                    f.commit();
                }
                finishWithOutput(QString(), QString(), 0);
            } else {
                finishWithOutput(QString::fromUtf8(bytes), QString(), 0);
            }
        });
        return;
    }

    // 5. Synchronous / fast asynchronous command evaluation
    QTimer::singleShot(0, this, [this, args, prog, gen]() {
        if (m_runGeneration != gen || !m_running) {
            return;
        }

        QString out;
        int code = 0;

        if (prog == QStringLiteral("uname")) {
            out = qEnvironmentVariable("COMPUTERNAME", QStringLiteral("windows")) + QLatin1Char('\n');
        } else if (prog == QStringLiteral("cat") && args.value(1) == QStringLiteral("/proc/uptime")) {
            const double secs = static_cast<double>(GetTickCount64()) / 1000.0;
            out = QStringLiteral("%1 %1\n").arg(secs, 0, 'f', 2);
        } else if (prog == QStringLiteral("hyprctl")) {
            if (args.contains(QStringLiteral("cursorpos"))) {
                const QPoint pt = QCursor::pos();
                out = QStringLiteral("{\"x\":%1,\"y\":%2}\n").arg(pt.x()).arg(pt.y());
            } else if (args.contains(QStringLiteral("monitors"))) {
                QJsonArray arr;
                int id = 0;
                for (QScreen* s : QGuiApplication::screens()) {
                    QJsonObject m;
                    m.insert(QStringLiteral("id"), id++);
                    m.insert(QStringLiteral("name"), s->name());
                    m.insert(QStringLiteral("description"), s->model());
                    m.insert(QStringLiteral("width"), s->geometry().width());
                    m.insert(QStringLiteral("height"), s->geometry().height());
                    m.insert(QStringLiteral("refreshRate"), s->refreshRate());
                    m.insert(QStringLiteral("x"), s->geometry().x());
                    m.insert(QStringLiteral("y"), s->geometry().y());
                    m.insert(QStringLiteral("scale"), 1.0);
                    m.insert(QStringLiteral("focused"), s == QGuiApplication::primaryScreen());
                    QJsonObject ws;
                    ws.insert(QStringLiteral("id"), s_windowTracker ? s_windowTracker->activeWorkspace() : 1);
                    ws.insert(QStringLiteral("name"), QString::number(s_windowTracker ? s_windowTracker->activeWorkspace() : 1));
                    m.insert(QStringLiteral("activeWorkspace"), ws);
                    arr.append(m);
                }
                out = QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)) + QLatin1Char('\n');
            } else if (args.contains(QStringLiteral("clients"))) {
                QJsonArray arr;
                if (s_windowTracker) {
                    const QList<QScreen*> screens = QGuiApplication::screens();
                    QScreen* primaryScr = QGuiApplication::primaryScreen();
                    for (const QVariant& wv : s_windowTracker->openWindows()) {
                        const QVariantMap wm = wv.toMap();
                        const qulonglong hwndVal = wm.value(QStringLiteral("hwnd")).toULongLong();
                        HWND hwnd = reinterpret_cast<HWND>(static_cast<quintptr>(hwndVal));
                        const bool isMin = wm.value(QStringLiteral("minimized")).toBool() || (hwnd && IsIconic(hwnd));
                        DWORD cloaked = 0;
                        const bool isCloaked = hwnd && !isMin &&
                            SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) &&
                            cloaked != 0;
                        int lx = 0, ly = 0, lw = 0, lh = 0;
                        int monIdx = 0;
                        if (hwnd && !isMin && !isCloaked) {
                            RECT rc = {};
                            if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &rc, sizeof(rc)))) {
                                GetWindowRect(hwnd, &rc);
                            }
                            HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
                            MONITORINFOEXW mi = {};
                            mi.cbSize = sizeof(mi);
                            QScreen* matchedScr = primaryScr;
                            if (hMon && GetMonitorInfoW(hMon, &mi)) {
                                const QString dev = QString::fromWCharArray(mi.szDevice);
                                for (int i = 0; i < screens.size(); ++i) {
                                    if (screens.at(i) && screens.at(i)->name().compare(dev, Qt::CaseInsensitive) == 0) {
                                        matchedScr = screens.at(i);
                                        monIdx = i;
                                        break;
                                    }
                                }
                                if (matchedScr) {
                                    const qreal dpr = qMax<qreal>(0.5, matchedScr->devicePixelRatio());
                                    const QRect lg = matchedScr->geometry();
                                    lx = lg.x() + qRound((rc.left - mi.rcMonitor.left) / dpr);
                                    ly = lg.y() + qRound((rc.top - mi.rcMonitor.top) / dpr);
                                    lw = qMax(0, qRound((rc.right - rc.left) / dpr));
                                    lh = qMax(0, qRound((rc.bottom - rc.top) / dpr));
                                } else {
                                    lx = static_cast<int>(rc.left);
                                    ly = static_cast<int>(rc.top);
                                    lw = qMax(0, static_cast<int>(rc.right - rc.left));
                                    lh = qMax(0, static_cast<int>(rc.bottom - rc.top));
                                }
                            } else {
                                lx = static_cast<int>(rc.left);
                                ly = static_cast<int>(rc.top);
                                lw = qMax(0, static_cast<int>(rc.right - rc.left));
                                lh = qMax(0, static_cast<int>(rc.bottom - rc.top));
                            }
                        }
                        QJsonObject c;
                        c.insert(QStringLiteral("address"), QStringLiteral("0x") + QString::number(hwndVal, 16));
                        c.insert(QStringLiteral("class"), wm.value(QStringLiteral("app")).toString());
                        c.insert(QStringLiteral("title"), wm.value(QStringLiteral("title")).toString());
                        c.insert(QStringLiteral("at"), QJsonArray{lx, ly});
                        c.insert(QStringLiteral("size"), QJsonArray{lw, lh});
                        QJsonObject ws;
                        const int wsId = isMin ? -99 : wm.value(QStringLiteral("workspace"), s_windowTracker->activeWorkspace()).toInt();
                        ws.insert(QStringLiteral("id"), wsId);
                        ws.insert(QStringLiteral("name"), isMin ? QStringLiteral("special:dockmin") : QString::number(wsId));
                        c.insert(QStringLiteral("workspace"), ws);
                        c.insert(QStringLiteral("monitor"), monIdx);
                        c.insert(QStringLiteral("mapped"), !isCloaked);
                        c.insert(QStringLiteral("hidden"), isMin || isCloaked);
                        arr.append(c);
                    }
                }
                out = QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)) + QLatin1Char('\n');
            } else if (args.contains(QStringLiteral("binds")) || args.contains(QStringLiteral("devices"))) {
                out = QStringLiteral("[]\n");
            }
        } else if (prog == QStringLiteral("wtype")) {
            sendWin32Wtype(args);
        } else if (prog == QStringLiteral("cliphist")) {
            if (args.value(1) == QStringLiteral("list")) {
                out = clipHistory().listFormatted();
            } else if (args.value(1) == QStringLiteral("wipe")) {
                clipHistory().wipe();
            }
        } else if (prog == QStringLiteral("hyprpicker")) {
            out = sampleCursorPixelHex() + QLatin1Char('\n');
        } else if (prog == QStringLiteral("mkdir")) {
            for (int i = 1; i < args.size(); ++i) {
                if (!args.at(i).startsWith(QLatin1Char('-'))) {
                    QDir().mkpath(normalizeWinPath(args.at(i)));
                }
            }
        } else if (prog == QStringLiteral("wpctl")) {
            if (s_quickSource) {
                if (args.value(1) == QStringLiteral("set-volume")) {
                    QString pctStr = args.value(3);
                    pctStr.remove(QLatin1Char('%'));
                    const int vol = pctStr.toInt();
                    if (args.value(2).contains(QStringLiteral("SINK"))) {
                        s_quickSource->setSpeakerVolume(vol);
                    } else {
                        s_quickSource->setMicrophoneVolume(vol);
                    }
                } else if (args.value(1) == QStringLiteral("set-mute")) {
                    const bool muted = args.value(3) == QStringLiteral("1");
                    if (args.value(2).contains(QStringLiteral("SINK"))) {
                        if (s_quickSource->speakerOn() == muted) s_quickSource->toggleSpeaker();
                    } else {
                        if (s_quickSource->microphoneOn() == muted) s_quickSource->toggleMicrophone();
                    }
                }
            }
        } else if (prog == QStringLiteral("nmcli")) {
            if (s_quickSource && args.value(1) == QStringLiteral("radio") && args.value(2) == QStringLiteral("wifi")) {
                const bool wantOn = args.value(3) == QStringLiteral("on");
                s_quickSource->setWifiEnabled(wantOn);
            } else if (args.value(1) == QStringLiteral("device") && args.value(2) == QStringLiteral("wifi") && args.value(3) == QStringLiteral("connect")) {
                const QString ssid = args.value(4);
                if (s_quickSource && !ssid.isEmpty()) {
                    s_quickSource->connectWifi(ssid);
                }
            } else if (args.value(1) == QStringLiteral("connection") && args.value(2) == QStringLiteral("down")) {
                Win32::runWindowsCommand(QStringLiteral("netsh wlan disconnect"), false);
                if (s_quickSource) s_quickSource->refresh();
            }
        } else if (prog == QStringLiteral("bluetoothctl")) {
            if (s_quickSource && args.value(1) == QStringLiteral("power")) {
                const bool wantOn = args.value(2) == QStringLiteral("on");
                s_quickSource->setBluetoothEnabled(wantOn);
            }
        } else if (prog == QStringLiteral("ddcutil") || prog == QStringLiteral("brightnessctl")) {
            if (s_quickSource) {
                if (prog == QStringLiteral("brightnessctl")) {
                    const int sIdx = args.indexOf(QStringLiteral("s"));
                    if (sIdx >= 0 && sIdx + 1 < args.size()) {
                        QString pctStr = args.at(sIdx + 1);
                        pctStr.remove(QLatin1Char('%'));
                        s_quickSource->setBrightness(pctStr.toInt());
                    }
                } else if (prog == QStringLiteral("ddcutil")) {
                    const int vcpIdx = args.indexOf(QStringLiteral("setvcp"));
                    if (vcpIdx >= 0 && vcpIdx + 2 < args.size()) {
                        s_quickSource->setBrightness(qBound(0, args.at(vcpIdx + 2).toInt(), 100));
                    }
                }
            }
        } else if (prog == QStringLiteral("sh") || prog == QStringLiteral("bash")) {
            if (args.value(1) == QStringLiteral("-c")) {
                const QString script = args.value(2);
                if (script.contains(QStringLiteral("printf 'wifi=%s")) && s_quickSource) {
                    s_quickSource->refresh();
                    const QString wifi = s_quickSource->wifiOn() ? QStringLiteral("enabled") : QStringLiteral("disabled");
                    const QString ssid = s_quickSource->wifiOn() ? s_quickSource->wifiName() : QString();
                    const QString eth = s_quickSource->ethernetName();
                    const QString bt = s_quickSource->bluetoothOn() ? QStringLiteral("yes") : QStringLiteral("no");
                    const QString btdev = s_quickSource->bluetoothOn() ? s_quickSource->bluetoothDetail() : QString();
                    const QString sm = s_quickSource->speakerOn() ? QStringLiteral("0") : QStringLiteral("1");
                    const QString mm = s_quickSource->microphoneOn() ? QStringLiteral("0") : QStringLiteral("1");
                    const QString sv = QString::number(s_quickSource->speakerVolume() / 100.0, 'f', 2);
                    const QString mv = QString::number(s_quickSource->microphoneVolume() / 100.0, 'f', 2);
                    const QString nl = s_quickSource->nightLightOn() ? QString::number(s_quickSource->nightLightKelvin()) : QStringLiteral("6000");
                    const int gg = qRound(s_quickSource->gamma());
                    out = QStringLiteral("wifi=%1\nssid=%2\neth=%3\nbt=%4\nbtdev=%5\nsinkmuted=%6\nsrcmuted=%7\nsinkvol=%8\nsrcvol=%9\nnl=%10\ngg=%11\ngm=1\ngmm=0\nee=1\naf=\n")
                        .arg(wifi, ssid, eth, bt, btdev, sm, mm, sv, mv, nl).arg(gg);
                } else if (script.contains(QStringLiteral("device wifi list"))) {
                    QPointer<QsProcess> self(this);
                    const QString connectedSsid = (s_quickSource && s_quickSource->wifiOn())
                        ? s_quickSource->wifiName()
                        : QString();
                    std::thread([self, gen, connectedSsid]() {
                        const QString res = listWindowsWifiForNmcli(connectedSsid);
                        if (self) {
                            QMetaObject::invokeMethod(self, [self, gen, res]() {
                                if (self && self->m_runGeneration == gen && self->m_running) {
                                    self->finishWithOutput(res, QString(), 0);
                                }
                            }, Qt::QueuedConnection);
                        }
                    }).detach();
                    return;
                } else if (script.contains(QStringLiteral("bluetoothctl devices Paired"))) {
                    QPointer<QsProcess> self(this);
                    std::thread([self, gen]() {
                        const QString res = listWindowsBluetoothPaired();
                        if (self) {
                            QMetaObject::invokeMethod(self, [self, gen, res]() {
                                if (self && self->m_runGeneration == gen && self->m_running) {
                                    self->finishWithOutput(res, QString(), 0);
                                }
                            }, Qt::QueuedConnection);
                        }
                    }).detach();
                    return;
                } else if (script.contains(QStringLiteral("command -v cliphist"))) {
                    code = 0;
                } else if (script.contains(QStringLiteral("command -v \"$1\""))) {
                    const QString probeProg = args.value(4).trimmed().toLower();
                    code = (probeProg == QStringLiteral("wtype") || probeProg == QStringLiteral("cliphist") ||
                            probeProg == QStringLiteral("wl-copy")) ? 0 : 127;
                } else if (script.contains(QStringLiteral("command -v warp-cli"))) {
                    code = 1;
                } else if (script.contains(QStringLiteral("cliphist")) && script.contains(QStringLiteral("list"))) {
                    out = clipHistory().listFormatted();
                } else if (script.contains(QStringLiteral("hyprpicker"))) {
                    out = sampleCursorPixelHex() + QLatin1Char('\n');
                } else if (script.contains(QStringLiteral("hyprsunset gamma")) && s_quickSource) {
                    static const QRegularExpression gammaRe(QStringLiteral("hyprsunset\\s+gamma\\s+(\\d+)"));
                    const auto m = gammaRe.match(script);
                    if (m.hasMatch()) {
                        s_quickSource->setGamma(m.captured(1).toInt());
                    } else {
                        out = QString::number(qRound(s_quickSource->gamma())) + QLatin1Char('\n');
                    }
                } else if (script.contains(QStringLiteral("hyprsunset temperature")) && s_quickSource) {
                    static const QRegularExpression tempRe(QStringLiteral("temperature\\s+(\\d+)"));
                    const auto m = tempRe.match(script);
                    if (m.hasMatch()) {
                        s_quickSource->setNightLightTemperature(m.captured(1).toInt());
                    }
                } else if (script.contains(QStringLiteral("brightnessctl g"))) {
                    const int b = s_quickSource ? qRound(s_quickSource->brightness()) : 100;
                    out = QStringLiteral("%1 100\n").arg(b);
                } else if (script.contains(QStringLiteral("sekaipop-gamemode"))) {
                    out = QStringLiteral("0\n");
                } else if (script.contains(QStringLiteral("nwg-displays"))) {
                    out = QStringLiteral("yes\n");
                } else if (script.contains(QStringLiteral("printf '{\"c\":'; hyprctl -j clients"))) {
                    // EditMode.qml park probe: return empty clients list so it stays on current workspace
                    out = QStringLiteral("{\"c\":[],\"m\":[]}\n");
                } else if (script.contains(QStringLiteral("list ui; echo '--mono--'"))) {
                    out = QStringLiteral("Segoe UI Variable\nSegoe UI\n--mono--\nConsolas\nCascadia Mono\n");
                } else if (script.contains(QStringLiteral("gnome-system-monitor")) || script.contains(QStringLiteral("btop"))) {
                    if (s_quickSource) s_quickSource->openTaskManager();
                } else if (script.contains(QStringLiteral("--firmware-setup"))) {
                    Win32::runWindowsCommand(QStringLiteral("shutdown /r /fw /t 0"), false);
                } else if (script.contains(QStringLiteral("systemctl poweroff"))) {
                    if (s_quickSource) s_quickSource->powerOff();
                } else if (script.contains(QStringLiteral("systemctl reboot"))) {
                    if (s_quickSource) s_quickSource->reboot();
                } else if (script.contains(QStringLiteral("systemctl suspend"))) {
                    if (s_quickSource) s_quickSource->sleep();
                } else if (script.contains(QStringLiteral("systemctl hibernate"))) {
                    if (s_quickSource) s_quickSource->hibernate();
                } else if (script.contains(QStringLiteral("stat -c %s")) && args.size() >= 4) {
                    const QFileInfo fi(normalizeWinPath(args.at(3)));
                    out = QString::number(fi.exists() ? fi.size() : 0) + QLatin1Char('\n');
                } else if (script.contains(QStringLiteral("base64 -d")) && args.size() >= 5) {
                    out = QString::fromUtf8(QByteArray::fromBase64(args.at(4).toUtf8()));
                } else if (script.contains(QStringLiteral("apply")) && script.contains(QStringLiteral("&& cat")) &&
                           args.size() >= 7 && QFileInfo(args.at(3)).fileName() == QStringLiteral("matugen-state.sh")) {
                    // Per-monitor matugen: ["sh", "-c", 'sh "$0" apply "$1" "$2" "$3" "$5" "$6" && cat "$4"',
                    //                       matugenScript, imgPath, schemeType, "screen-"+monName, outPath, seed, contrast]
                    const QString imgPath = normalizeWinPath(args.value(4));
                    const QString scheme = args.value(5).isEmpty() ? QStringLiteral("scheme-tonal-spot") : args.value(5);
                    const QString outName = args.value(6).isEmpty() ? QStringLiteral("screen") : args.value(6);
                    bool okSeed = false;
                    const int seed = args.value(8).toInt(&okSeed);
                    bool okContrast = false;
                    const double contrast = args.value(9).toDouble(&okContrast);
                    const QString matugenDir = cacheRootDir() + QStringLiteral("/matugen");
                    QPointer<QsProcess> self(this);
                    std::thread([self, imgPath, scheme, outName, seed = okSeed ? seed : 0,
                                 contrast = okContrast ? contrast : 0.0, matugenDir, gen]() {
                        QString json;
                        QString err;
                        const bool ok = Services::MatugenEngine::applyToCache(
                            imgPath, scheme, outName, seed, contrast, matugenDir, &json, &err);
                        if (!self) return;
                        QMetaObject::invokeMethod(self, [self, ok, json, err, gen]() {
                            if (!self || self->m_runGeneration != gen || !self->m_running) return;
                            self->finishWithOutput(ok ? json : QString(), ok ? QString() : (err + QLatin1Char('\n')), ok ? 0 : 2);
                        }, Qt::QueuedConnection);
                    }).detach();
                    return;
                } else if (script.contains(QStringLiteral("curl -fsSL")) && args.size() >= 6) {
                    // VisualizerCover.qml cover art download: ["bash", "-c", "...", "_", filePath, artUrl]
                    const QString dstPath = normalizeWinPath(args.at(4));
                    const QString artUrl = args.at(5);
                    if (QFileInfo::exists(dstPath) && QFileInfo(dstPath).size() > 0) {
                        finishWithOutput(QString(), QString(), 0);
                        return;
                    }
                    QNetworkRequest req{QUrl(artUrl)};
                    QNetworkReply* reply = sharedNam()->get(req);
                    connect(reply, &QNetworkReply::finished, this, [this, reply, dstPath, gen]() {
                        reply->deleteLater();
                        if (m_runGeneration != gen || !m_running) return;
                        if (reply->error() == QNetworkReply::NoError) {
                            QDir().mkpath(QFileInfo(dstPath).absolutePath());
                            QSaveFile f(dstPath);
                            if (f.open(QIODevice::WriteOnly)) {
                                f.write(reply->readAll());
                                f.commit();
                            }
                            finishWithOutput(QString(), QString(), 0);
                        } else {
                            finishWithOutput(QString(), reply->errorString(), 1);
                        }
                    });
                    return;
                }
            } else if (args.size() >= 2) {
                const QString scriptName = QFileInfo(args.at(1)).fileName();
                if (scriptName == QStringLiteral("matugen-state.sh")) {
                    const QString mMode = args.value(2);
                    if (mMode == QStringLiteral("installed")) {
                        finishWithOutput(QStringLiteral("yes\n"), QString(), 0);
                        return;
                    }
                    if (mMode == QStringLiteral("check")) {
                        finishWithOutput(QStringLiteral("matugen: built-in Material 3 OKLCH engine\n"), QString(), 0);
                        return;
                    }
                    if (mMode == QStringLiteral("sources")) {
                        const QString imgPath = normalizeWinPath(args.value(3));
                        QPointer<QsProcess> self(this);
                        std::thread([self, imgPath, gen]() {
                            const QStringList cols = Services::MatugenEngine::extractSourceColors(imgPath);
                            if (!self) return;
                            const QString outStr = cols.isEmpty() ? QString() : (cols.join(QLatin1Char('\n')) + QLatin1Char('\n'));
                            const QString errStr = cols.isEmpty()
                                ? (QStringLiteral("matugen-state: no such or unreadable image: ") + imgPath + QLatin1Char('\n'))
                                : QString();
                            const int rc = cols.isEmpty() ? 2 : 0;
                            QMetaObject::invokeMethod(self, [self, outStr, errStr, rc, gen]() {
                                if (!self || self->m_runGeneration != gen || !self->m_running) return;
                                self->finishWithOutput(outStr, errStr, rc);
                            }, Qt::QueuedConnection);
                        }).detach();
                        return;
                    }
                    if (mMode == QStringLiteral("apply")) {
                        const QString imgPath = normalizeWinPath(args.value(3));
                        const QString scheme = args.value(4).isEmpty() ? QStringLiteral("scheme-tonal-spot") : args.value(4);
                        const QString outName = args.value(5).isEmpty() ? QStringLiteral("colors") : args.value(5);
                        bool okSeed = false;
                        const int seed = args.value(6).toInt(&okSeed);
                        bool okContrast = false;
                        const double contrast = args.value(7).toDouble(&okContrast);
                        const QString matugenDir = cacheRootDir() + QStringLiteral("/matugen");
                        QPointer<QsProcess> self(this);
                        std::thread([self, imgPath, scheme, outName, seed = okSeed ? seed : 0,
                                     contrast = okContrast ? contrast : 0.0, matugenDir, gen]() {
                            QString err;
                            const bool ok = Services::MatugenEngine::applyToCache(
                                imgPath, scheme, outName, seed, contrast, matugenDir, nullptr, &err);
                            if (!self) return;
                            QMetaObject::invokeMethod(self, [self, ok, err, gen]() {
                                if (!self || self->m_runGeneration != gen || !self->m_running) return;
                                self->finishWithOutput(QString(), ok ? QString() : (err + QLatin1Char('\n')), ok ? 0 : 2);
                            }, Qt::QueuedConnection);
                        }).detach();
                        return;
                    }
                    finishWithOutput(QString(), QStringLiteral("matugen-state: unknown mode\n"), 2);
                    return;
                }
                if (scriptName == QStringLiteral("wallhaven.sh")) {
                    const QString whMode = args.value(2);
                    if (whMode == QStringLiteral("have")) {
                        const QString dirPath = normalizeWinPath(args.value(3));
                        QDir dir(dirPath);
                        if (dir.exists()) {
                            const QFileInfoList files = dir.entryInfoList(
                                {QStringLiteral("wallhaven-*"), QStringLiteral("motionbgs-*")},
                                QDir::Files | QDir::NoDotAndDotDot);
                            for (const QFileInfo& fi : files) {
                                const QString fn = fi.fileName();
                                if (fn.endsWith(QStringLiteral(".part"), Qt::CaseInsensitive)) {
                                    continue;
                                }
                                QString id;
                                if (fn.startsWith(QStringLiteral("wallhaven-"))) {
                                    id = fn.mid(QStringLiteral("wallhaven-").size());
                                    const int dot = id.lastIndexOf(QLatin1Char('.'));
                                    if (dot > 0) id = id.left(dot);
                                } else if (fn.startsWith(QStringLiteral("motionbgs-"))) {
                                    id = fn.mid(QStringLiteral("motionbgs-").size());
                                    const int dot = id.lastIndexOf(QLatin1Char('.'));
                                    if (dot > 0) id = id.left(dot);
                                    if (!id.isEmpty()) id = QStringLiteral("mb-") + id;
                                }
                                if (!id.isEmpty()) {
                                    out += id + QLatin1Char('\n');
                                }
                            }
                        }
                        finishWithOutput(out, QString(), 0);
                        return;
                    }
                    if (whMode == QStringLiteral("search")) {
                        const QString urlStr = args.value(3);
                        constexpr qint64 kFreshTtlSec = 900;     // 15 minutes
                        constexpr qint64 kStaleTtlSec = 86400;   // 24 hours offline fallback
                        if (urlStr.startsWith(QStringLiteral("https://motionbgs.com/api/sekaipop"))) {
                            QString realUrl;
                            int page = 1;
                            QString catLabel;
                            if (!resolveMotionBgsRequest(urlStr, realUrl, page, catLabel)) {
                                finishWithOutput(QStringLiteral("{\"data\":[],\"meta\":{\"current_page\":%1,\"last_page\":1,\"total\":0}}").arg(page), QString(), 0);
                                return;
                            }

                            const QString cached = readOnlineSearchCache(realUrl, kFreshTtlSec);
                            if (!cached.isEmpty()) {
                                qInfo("[MotionBGs] cache hit %s -> json=%lldB",
                                      qPrintable(realUrl), static_cast<long long>(cached.size()));
                                finishWithOutput(cached, QString(), 0);
                                return;
                            }

                            QNetworkRequest req{QUrl(realUrl)};
                            req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("SekaiPop/1.0"));
                            req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
                            req.setAttribute(QNetworkRequest::Http2AllowedAttribute, true);
                            req.setTransferTimeout(12000);
                            QNetworkReply* reply = sharedNam()->get(req);
                            m_activeReply = reply;
                            connect(reply, &QNetworkReply::finished, this, [this, reply, gen, page, catLabel, realUrl]() {
                                if (m_activeReply == reply) {
                                    m_activeReply = nullptr;
                                }
                                reply->deleteLater();
                                if (m_runGeneration != gen || !m_running) {
                                    return;
                                }
                                const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                                if (reply->error() == QNetworkReply::NoError && status == 200) {
                                    const QString html = QString::fromUtf8(reply->readAll());
                                    const QString json = parseMotionBgsHtmlToJson(html, page, catLabel);
                                    writeOnlineSearchCache(realUrl, json);
                                    qInfo("[MotionBGs] fetched %s -> html=%lldB json=%lldB",
                                          qPrintable(realUrl),
                                          static_cast<long long>(html.size()),
                                          static_cast<long long>(json.size()));
                                    finishWithOutput(json, QString(), 0);
                                } else {
                                    const QString stale = readOnlineSearchCache(realUrl, kStaleTtlSec);
                                    if (!stale.isEmpty()) {
                                        qWarning("[MotionBGs] network error (HTTP %d), serving stale cache for %s", status, qPrintable(realUrl));
                                        finishWithOutput(stale, QString(), 0);
                                        return;
                                    }
                                    if (status == 429) {
                                        finishWithOutput(QString(), QStringLiteral("motionbgs: too many requests\n"), 3);
                                    } else if (status == 0) {
                                        qWarning("[MotionBGs] search network error: %s", qPrintable(reply->errorString()));
                                        finishWithOutput(QString(), reply->errorString() + QLatin1Char('\n'), 5);
                                    } else {
                                        qWarning("[MotionBGs] search HTTP %d: %s", status, qPrintable(reply->errorString()));
                                        finishWithOutput(QString(), QStringLiteral("motionbgs: HTTP %1\n").arg(status), 4);
                                    }
                                }
                            });
                            return;
                        }

                        if (!urlStr.startsWith(QStringLiteral("https://wallhaven.cc/api/v1/search"))) {
                            finishWithOutput(QString(), QStringLiteral("wallhaven: not a search URL\n"), 2);
                            return;
                        }
                        const bool isRandomSort = urlStr.contains(QStringLiteral("sorting=random"));
                        if (!isRandomSort) {
                            const QString cached = readOnlineSearchCache(urlStr, kFreshTtlSec);
                            if (!cached.isEmpty()) {
                                finishWithOutput(cached, QString(), 0);
                                return;
                            }
                        }
                        QNetworkRequest req{QUrl(urlStr)};
                        req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("SekaiPop/1.0"));
                        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
                        req.setAttribute(QNetworkRequest::Http2AllowedAttribute, true);
                        req.setTransferTimeout(12000);
                        QNetworkReply* reply = sharedNam()->get(req);
                        m_activeReply = reply;
                        connect(reply, &QNetworkReply::finished, this, [this, reply, gen, urlStr, isRandomSort]() {
                            if (m_activeReply == reply) {
                                m_activeReply = nullptr;
                            }
                            reply->deleteLater();
                            if (m_runGeneration != gen || !m_running) {
                                return;
                            }
                            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                            if (reply->error() == QNetworkReply::NoError && status == 200) {
                                const QString body = QString::fromUtf8(reply->readAll());
                                if (!isRandomSort) {
                                    writeOnlineSearchCache(urlStr, body);
                                }
                                finishWithOutput(body, QString(), 0);
                            } else {
                                if (!isRandomSort) {
                                    const QString stale = readOnlineSearchCache(urlStr, kStaleTtlSec);
                                    if (!stale.isEmpty()) {
                                        finishWithOutput(stale, QString(), 0);
                                        return;
                                    }
                                }
                                if (status == 429) {
                                    finishWithOutput(QString(), QStringLiteral("wallhaven: too many requests\n"), 3);
                                } else if (status == 0) {
                                    qWarning("[Wallhaven] search network error: %s", qPrintable(reply->errorString()));
                                    finishWithOutput(QString(), reply->errorString() + QLatin1Char('\n'), 5);
                                } else {
                                    qWarning("[Wallhaven] search HTTP %d: %s", status, qPrintable(reply->errorString()));
                                    finishWithOutput(QString(), QStringLiteral("wallhaven: HTTP %1\n").arg(status), 4);
                                }
                            }
                        });
                        return;
                    }
                    if (whMode == QStringLiteral("download")) {
                        const QString urlStr = args.value(3);
                        const QString dirPath = normalizeWinPath(args.value(4));
                        const QString fileName = args.value(5);
                        const bool safeName =
                            !fileName.contains(QLatin1Char('/')) &&
                            !fileName.contains(QLatin1Char('\\')) &&
                            !fileName.contains(QStringLiteral(".."));
                        const bool isValidWh =
                            urlStr.startsWith(QStringLiteral("https://w.wallhaven.cc/full/")) &&
                            fileName.startsWith(QStringLiteral("wallhaven-")) &&
                            (fileName.endsWith(QStringLiteral(".jpg"), Qt::CaseInsensitive) ||
                             fileName.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive) ||
                             fileName.endsWith(QStringLiteral(".webp"), Qt::CaseInsensitive));
                        const bool isValidMb =
                            (urlStr.startsWith(QStringLiteral("https://motionbgs.com/dl/hd/")) ||
                             urlStr.startsWith(QStringLiteral("https://motionbgs.com/dl/4k/"))) &&
                            fileName.startsWith(QStringLiteral("motionbgs-")) &&
                            fileName.endsWith(QStringLiteral(".mp4"), Qt::CaseInsensitive);
                        if (!safeName || (!isValidWh && !isValidMb) || dirPath.isEmpty()) {
                            finishWithOutput(QString(), QStringLiteral("wallhaven: bad download arguments\n"), 2);
                            return;
                        }
                        const QString finalPath = QDir::fromNativeSeparators(QDir(dirPath).filePath(fileName));
                        if (QFileInfo::exists(finalPath) && QFileInfo(finalPath).size() > 0) {
                            finishWithOutput(finalPath + QLatin1Char('\n'), QString(), 0);
                            return;
                        }
                        QDir().mkpath(dirPath);
                        const QString partPath = finalPath + QStringLiteral(".part");
                        QFile::remove(partPath);
                        auto* partFile = new QFile(partPath);
                        if (!partFile->open(QIODevice::WriteOnly)) {
                            delete partFile;
                            finishWithOutput(QString(), QStringLiteral("wallhaven: cannot open part file\n"), 1);
                            return;
                        }
                        QNetworkRequest req{QUrl(urlStr)};
                        req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("SekaiPop"));
                        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
                        req.setTransferTimeout(120000);
                        QNetworkReply* reply = sharedNam()->get(req);
                        partFile->setParent(reply);
                        m_activeReply = reply;
                        connect(reply, &QNetworkReply::readyRead, this, [reply, partFile]() {
                            if (partFile->isOpen()) {
                                const QByteArray chunk = reply->readAll();
                                if (!chunk.isEmpty()) {
                                    partFile->write(chunk);
                                    partFile->flush();
                                }
                            }
                        });
                        connect(reply, &QNetworkReply::finished, this, [this, reply, partFile, partPath, finalPath, gen]() {
                            if (m_activeReply == reply) {
                                m_activeReply = nullptr;
                            }
                            if (partFile->isOpen()) {
                                const QByteArray tail = reply->readAll();
                                if (!tail.isEmpty()) {
                                    partFile->write(tail);
                                }
                                partFile->flush();
                                partFile->close();
                            }
                            reply->deleteLater();

                            if (m_runGeneration != gen || !m_running) {
                                QFile::remove(partPath);
                                return;
                            }
                            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                            if (reply->error() == QNetworkReply::NoError && status == 200 && QFileInfo(partPath).size() > 0) {
                                QFile::remove(finalPath);
                                if (QFile::rename(partPath, finalPath)) {
                                    finishWithOutput(finalPath + QLatin1Char('\n'), QString(), 0);
                                } else {
                                    QFile::remove(partPath);
                                    finishWithOutput(QString(), QStringLiteral("wallhaven: failed to move file\n"), 1);
                                }
                            } else {
                                QFile::remove(partPath);
                                if (status == 429) {
                                    finishWithOutput(QString(), QStringLiteral("wallhaven: too many requests\n"), 3);
                                } else if (status == 0) {
                                    qWarning("[Wallhaven] download network error: %s", qPrintable(reply->errorString()));
                                    finishWithOutput(QString(), reply->errorString() + QLatin1Char('\n'), 5);
                                } else {
                                    qWarning("[Wallhaven] download HTTP %d: %s", status, qPrintable(reply->errorString()));
                                    finishWithOutput(QString(), QStringLiteral("wallhaven: HTTP %1\n").arg(status), 4);
                                }
                            }
                        });
                        return;
                    }
                    finishWithOutput(QString(), QStringLiteral("wallhaven: unknown mode\n"), 2);
                    return;
                }
                if (scriptName == QStringLiteral("wallpaper-thumbs.sh") && args.value(2) == QStringLiteral("make")) {
                    const QString dirPath = normalizeWinPath(args.value(3));
                    const QString thumbsDir = cacheRootDir() + QStringLiteral("/wallpaper/thumbs");
                    QDir().mkpath(thumbsDir);
                    QPointer<QsProcess> self(this);
                    std::thread([self, dirPath, thumbsDir, gen]() {
                        QDir dir(dirPath);
                        if (dir.exists()) {
                            static const QStringList filters = {
                                QStringLiteral("*.png"), QStringLiteral("*.jpg"), QStringLiteral("*.jpeg"),
                                QStringLiteral("*.webp"), QStringLiteral("*.bmp"), QStringLiteral("*.mp4"),
                                QStringLiteral("*.webm"), QStringLiteral("*.mkv"), QStringLiteral("*.mov"),
                                QStringLiteral("*.avi")
                            };
                            const QFileInfoList entries = dir.entryInfoList(filters, QDir::Files, QDir::Time);
                            for (const QFileInfo& fi : entries) {
                                if (!self) return;
                                const QString fileKey = QDir::fromNativeSeparators(fi.absoluteFilePath());
                                const QString key = getMediaKey(fi.absoluteFilePath(), fi);
                                const QString thumbPath = thumbsDir + QStringLiteral("/") + key + QStringLiteral(".jpg");
                                if (!QFileInfo::exists(thumbPath) || QFileInfo(thumbPath).size() == 0) {
                                    const bool ok = isMediaVideo(fi.absoluteFilePath())
                                        ? extractVideoFrameWin32(fi.absoluteFilePath(), thumbPath, 720, 405)
                                        : extractImageThumb(fi.absoluteFilePath(), thumbPath, 720);
                                    if (ok && self) {
                                        const QString line = QStringLiteral("ok\t") + fileKey + QLatin1Char('\t') + toSlashPath(thumbPath);
                                        QMetaObject::invokeMethod(self, [self, line, gen]() {
                                            if (!self || self->m_runGeneration != gen || !self->m_running) return;
                                            if (auto* sp = qobject_cast<QsSplitParser*>(self->m_stdout)) {
                                                sp->feedLine(line);
                                            }
                                        }, Qt::QueuedConnection);
                                    }
                                }
                            }
                        }
                        if (!self) return;
                        QMetaObject::invokeMethod(self, [self, gen]() {
                            if (!self || self->m_runGeneration != gen || !self->m_running) return;
                            self->finishWithOutput(QString(), QString(), 0);
                        }, Qt::QueuedConnection);
                    }).detach();
                    return;
                }
                if (scriptName == QStringLiteral("wallpaper-thumbs.sh") && args.value(2) == QStringLiteral("still")) {
                    const QStringList subArgs = args.mid(2);
                    QPointer<QsProcess> self(this);
                    std::thread([self, scriptName, subArgs, gen]() {
                        int bgCode = 0;
                        const QString bgOut = handleStateScript(scriptName, subArgs, bgCode);
                        if (!self) return;
                        QMetaObject::invokeMethod(self, [self, bgOut, bgCode, gen]() {
                            if (!self || self->m_runGeneration != gen || !self->m_running) return;
                            self->finishWithOutput(bgOut, QString(), bgCode);
                        }, Qt::QueuedConnection);
                    }).detach();
                    return;
                }
                out = handleStateScript(scriptName, args.mid(2), code);
            }
        }

        finishWithOutput(out, QString(), code);
    });
}

// ============================================================================
// QsFileView
// ============================================================================

QsFileView::QsFileView(QObject* parent)
    : QObject(parent) {
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this](const QString&) {
        Q_EMIT fileChanged();
    });
}

void QsFileView::setPath(const QString& p) {
    if (m_path == p) {
        return;
    }
    m_path = p;
    Q_EMIT pathChanged();
    updateWatcher();
    if (m_complete && !m_blockLoading) {
        performLoad(true);
    }
}

void QsFileView::setWatchChanges(bool w) {
    if (m_watchChanges != w) {
        m_watchChanges = w;
        Q_EMIT watchChangesChanged();
        updateWatcher();
    }
}

void QsFileView::setBlockLoading(bool b) {
    if (m_blockLoading != b) {
        m_blockLoading = b;
        Q_EMIT blockLoadingChanged();
    }
}

QString QsFileView::text() {
    if (!m_isLoaded && !m_path.isEmpty()) {
        performLoad(false);
    }
    return m_cachedText;
}

void QsFileView::reload() {
    performLoad(true);
}

void QsFileView::setText(const QString& content) {
    if (m_blockWrites || m_path.isEmpty()) {
        return;
    }
    m_cachedText = content;
    if (writeTextFile(m_path, content)) {
        m_isLoaded = true;
        Q_EMIT loadedChanged();
        Q_EMIT saved();
        updateWatcher();
    } else {
        Q_EMIT saveFailed(1);
    }
}

void QsFileView::componentComplete() {
    m_complete = true;
    updateWatcher();
    if (!m_path.isEmpty() && !m_blockLoading) {
        QTimer::singleShot(0, this, [this]() {
            performLoad(true);
        });
    }
}

void QsFileView::performLoad(bool emitSignals) {
    if (m_path.isEmpty()) {
        m_cachedText.clear();
        m_isLoaded = false;
        return;
    }

    if (m_path == QStringLiteral("/proc/meminfo")) {
        m_cachedText = synthProcMeminfo();
        m_isLoaded = true;
        if (emitSignals) {
            Q_EMIT loadedChanged();
            Q_EMIT loaded();
        }
        return;
    }
    if (m_path == QStringLiteral("/proc/stat")) {
        m_cachedText = synthProcStat();
        m_isLoaded = true;
        if (emitSignals) {
            Q_EMIT loadedChanged();
            Q_EMIT loaded();
        }
        return;
    }
    if (m_path == QStringLiteral("/proc/net/dev")) {
        m_cachedText = synthProcNetDev();
        m_isLoaded = true;
        if (emitSignals) {
            Q_EMIT loadedChanged();
            Q_EMIT loaded();
        }
        return;
    }

    const QString clean = normalizeWinPath(m_path);
    QFile f(clean);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_cachedText.clear();
        m_isLoaded = false;
        if (emitSignals) {
            Q_EMIT loadFailed(1);
        }
        return;
    }
    m_cachedText = QString::fromUtf8(f.readAll());
    m_isLoaded = true;
    if (m_watchChanges && m_watcher.files().isEmpty()) {
        updateWatcher();
    }
    if (emitSignals) {
        Q_EMIT loadedChanged();
        Q_EMIT loaded();
    }
}

void QsFileView::updateWatcher() {
    const QStringList watched = m_watcher.files();
    if (!watched.isEmpty()) {
        m_watcher.removePaths(watched);
    }
    if (m_watchChanges && !m_path.isEmpty() && !m_path.startsWith(QStringLiteral("/proc/"))) {
        const QString clean = normalizeWinPath(m_path);
        if (QFileInfo::exists(clean)) {
            m_watcher.addPath(clean);
        }
    }
}

} // namespace SekaiPop::Quickshell
