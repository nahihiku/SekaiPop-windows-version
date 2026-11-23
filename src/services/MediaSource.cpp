#include "MediaSource.h"
#include "../win32/Win32Utils.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QMetaObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <algorithm>
#include <asyncinfo.h>
#include <chrono>
#include <cmath>
#include <cstring>
#include <inspectable.h>
#include <iterator>
#include <objidl.h>
#include <roapi.h>
#include <windows.h>
#include <winstring.h>

namespace SekaiPop::Services {

double MediaSessionInfo::currentPositionSec() const {
    double pos = basePositionSec;
    if (playing && lastUpdatedEpochMs > 0) {
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        const double elapsed = static_cast<double>(nowMs - lastUpdatedEpochMs) / 1000.0;
        if (elapsed >= 0.0 && elapsed < 36000.0) {
            pos += elapsed;
        }
    }
    if (pos < 0.0) {
        pos = 0.0;
    }
    if (durationSec > 0.0 && pos > durationSec) {
        pos = durationSec;
    }
    return pos;
}

namespace {

// Verified against Windows.Media.winmd & Ukikura bridge_winrt_abi.h
constexpr GUID kIidSessionManagerStatics = { 0x2050C4EE, 0x11A0, 0x57DE, { 0xAE, 0xD7, 0xC9, 0x7C, 0x70, 0x33, 0x82, 0x45 } };
constexpr GUID kIidAsyncInfo             = { 0x00000036, 0x0000, 0x0000, { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
constexpr GUID kIidBufferFactory         = { 0x71AF914D, 0xC10F, 0x484B, { 0xBC, 0x50, 0x14, 0xBC, 0x62, 0x3B, 0x3A, 0x27 } };
constexpr GUID kIidBufferByteAccess      = { 0x905A0FEF, 0xBC53, 0x11DF, { 0x8C, 0x49, 0x00, 0x1E, 0x4F, 0xC6, 0x86, 0xDA } };
constexpr GUID kIidRandomAccessStream    = { 0x905A0FE1, 0xBC53, 0x11DF, { 0x8C, 0x49, 0x00, 0x1E, 0x4F, 0xC6, 0x86, 0xDA } };
constexpr GUID kIidInputStream           = { 0x905A0FE2, 0xBC53, 0x11DF, { 0x8C, 0x49, 0x00, 0x1E, 0x4F, 0xC6, 0x86, 0xDA } };
constexpr GUID kIidIStream               = { 0x0000000C, 0x0000, 0x0000, { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };

using CreateStreamOverRasFn = HRESULT(WINAPI*)(IUnknown* randomAccessStream, REFIID riid, void** ppv);

template <typename Fn>
Fn vtableSlot(IUnknown* obj, size_t index) {
    void** vt = *reinterpret_cast<void***>(obj);
    return reinterpret_cast<Fn>(vt[index]);
}

bool waitAsyncOp(IInspectable* asyncOp, const std::atomic<bool>* stopFlag = nullptr, int timeoutMs = 1200) {
    if (!asyncOp) {
        return false;
    }
    IAsyncInfo* info = nullptr;
    if (FAILED(asyncOp->QueryInterface(kIidAsyncInfo, reinterpret_cast<void**>(&info))) || !info) {
        return false;
    }
    AsyncStatus status = Started;
    int waited = 0;
    while (waited < timeoutMs && (!stopFlag || !stopFlag->load())) {
        if (FAILED(info->get_Status(&status))) {
            info->Release();
            return false;
        }
        if (status != Started) {
            break;
        }
        Sleep(5);
        waited += 5;
    }
    info->Release();
    return status == Completed;
}

template <typename ResultT>
bool awaitGetResults(IInspectable* asyncOp, ResultT* outResult, const std::atomic<bool>* stopFlag = nullptr, int timeoutMs = 1200) {
    if (!waitAsyncOp(asyncOp, stopFlag, timeoutMs)) {
        return false;
    }
    using GetResultsFn = HRESULT(STDMETHODCALLTYPE*)(IInspectable*, ResultT*);
    auto fn = vtableSlot<GetResultsFn>(asyncOp, 8);
    return SUCCEEDED(fn(asyncOp, outResult));
}

template <typename ResultT>
bool awaitProgressGetResults(IInspectable* asyncOp, ResultT* outResult, const std::atomic<bool>* stopFlag = nullptr, int timeoutMs = 1200) {
    if (!waitAsyncOp(asyncOp, stopFlag, timeoutMs)) {
        return false;
    }
    using GetResultsFn = HRESULT(STDMETHODCALLTYPE*)(IInspectable*, ResultT*);
    auto fn = vtableSlot<GetResultsFn>(asyncOp, 10);
    return SUCCEEDED(fn(asyncOp, outResult));
}

QString hstringToQString(HSTRING hs) {
    if (!hs) {
        return {};
    }
    UINT32 len = 0;
    const wchar_t* raw = WindowsGetStringRawBuffer(hs, &len);
    if (!raw || len == 0) {
        return {};
    }
    return QString::fromWCharArray(raw, static_cast<int>(len));
}

QString readSessionAumid(IInspectable* session) {
    if (!session) return {};
    using GetAumidFn = HRESULT(STDMETHODCALLTYPE*)(IInspectable*, HSTRING*);
    HSTRING hAumid = nullptr;
    QString out;
    if (SUCCEEDED(vtableSlot<GetAumidFn>(session, 6)(session, &hAumid)) && hAumid) {
        out = hstringToQString(hAumid);
        WindowsDeleteString(hAumid);
    }
    return out;
}

int readSessionPlaybackStatus(IInspectable* session) {
    if (!session) return 0;
    using GetPlaybackInfoFn = HRESULT(STDMETHODCALLTYPE*)(IInspectable*, IInspectable**);
    IInspectable* pbInfo = nullptr;
    int pbStatus = 0;
    if (SUCCEEDED(vtableSlot<GetPlaybackInfoFn>(session, 9)(session, &pbInfo)) && pbInfo) {
        using GetPlaybackStatusFn = HRESULT(STDMETHODCALLTYPE*)(IInspectable*, int*);
        vtableSlot<GetPlaybackStatusFn>(pbInfo, 7)(pbInfo, &pbStatus);
        pbInfo->Release();
    }
    return pbStatus;
}

struct RawTimeline {
    double durationSec = 0.0;
    double positionSec = 0.0;
    qint64 lastUpdatedTicks = 0;
    qint64 lastUpdatedEpochMs = 0;
    bool valid = false;
};

RawTimeline readSessionTimeline(IInspectable* session) {
    RawTimeline out;
    if (!session) return out;
    // IGlobalSystemMediaTransportControlsSession::GetTimelineProperties = slot 8
    using GetTimelineFn = HRESULT(STDMETHODCALLTYPE*)(IInspectable*, IInspectable**);
    IInspectable* tl = nullptr;
    if (SUCCEEDED(vtableSlot<GetTimelineFn>(session, 8)(session, &tl)) && tl) {
        using GetI64Fn = HRESULT(STDMETHODCALLTYPE*)(IInspectable*, INT64*);
        INT64 endTicks = 0;
        INT64 posTicks = 0;
        INT64 lastUpdatedTicks = 0;
        // IGlobalSystemMediaTransportControlsSessionTimelineProperties:
        // slot 6=StartTime, 7=EndTime, 8=MinSeekTime, 9=MaxSeekTime, 10=Position, 11=LastUpdatedTime
        vtableSlot<GetI64Fn>(tl, 7)(tl, &endTicks);
        vtableSlot<GetI64Fn>(tl, 10)(tl, &posTicks);
        vtableSlot<GetI64Fn>(tl, 11)(tl, &lastUpdatedTicks);
        tl->Release();

        out.durationSec = endTicks > 0 ? static_cast<double>(endTicks) / 10000000.0 : 0.0;
        out.positionSec = posTicks > 0 ? static_cast<double>(posTicks) / 10000000.0 : 0.0;
        out.lastUpdatedTicks = static_cast<qint64>(lastUpdatedTicks);
        if (lastUpdatedTicks > 116444736000000000LL) {
            out.lastUpdatedEpochMs = static_cast<qint64>((lastUpdatedTicks - 116444736000000000LL) / 10000LL);
        }
        out.valid = true;
    }
    return out;
}

QString friendlyIdentityFromAumid(const QString& rawAumid) {
    QString s = rawAumid.trimmed();
    if (s.isEmpty()) {
        return QStringLiteral("Media");
    }
    const QString low = s.toLower();
    if (low.contains(QStringLiteral("brave"))) return QStringLiteral("Brave");
    if (low.contains(QStringLiteral("chrome"))) return QStringLiteral("Chrome");
    if (low.contains(QStringLiteral("msedge")) || low.contains(QStringLiteral("edge"))) return QStringLiteral("Edge");
    if (low.contains(QStringLiteral("firefox"))) return QStringLiteral("Firefox");
    if (low.contains(QStringLiteral("zen"))) return QStringLiteral("Zen");
    if (low.contains(QStringLiteral("opera"))) return QStringLiteral("Opera");
    if (low.contains(QStringLiteral("vivaldi"))) return QStringLiteral("Vivaldi");
    if (low.contains(QStringLiteral("spotify"))) return QStringLiteral("Spotify");
    if (low.contains(QStringLiteral("vlc"))) return QStringLiteral("VLC");
    if (low.contains(QStringLiteral("mpv"))) return QStringLiteral("mpv");
    if (low.contains(QStringLiteral("tidal"))) return QStringLiteral("Tidal");
    if (low.contains(QStringLiteral("applemusic")) || low.contains(QStringLiteral("itunes"))) return QStringLiteral("Apple Music");
    if (low.contains(QStringLiteral("zunemusic")) || low.contains(QStringLiteral("windowsmediaplayer"))) return QStringLiteral("Media Player");
    if (low.contains(QStringLiteral("foobar"))) return QStringLiteral("foobar2000");
    if (low.contains(QStringLiteral("aimp"))) return QStringLiteral("AIMP");
    if (low.contains(QStringLiteral("discord"))) return QStringLiteral("Discord");
    if (low.contains(QStringLiteral("musicbee"))) return QStringLiteral("MusicBee");
    if (low.contains(QStringLiteral("ytmdesktop")) || low.contains(QStringLiteral("youtube-music"))) return QStringLiteral("YouTube Music");

    const int bang = s.indexOf(QLatin1Char('!'));
    if (bang > 0) s = s.left(bang);
    const int under = s.indexOf(QLatin1Char('_'));
    if (under > 0 && s.contains(QLatin1Char('.'))) s = s.left(under);
    const int slash = std::max(s.lastIndexOf(QLatin1Char('\\')), s.lastIndexOf(QLatin1Char('/')));
    if (slash >= 0) s = s.mid(slash + 1);
    if (s.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)) s.chop(4);
    const int dot = s.lastIndexOf(QLatin1Char('.'));
    if (dot >= 0 && dot + 1 < s.size()) s = s.mid(dot + 1);
    if (s.isEmpty()) return QStringLiteral("Media");
    s[0] = s[0].toUpper();
    return s;
}

QString desktopEntryFromAumid(const QString& rawAumid) {
    const QString low = rawAumid.trimmed().toLower();
    if (low.contains(QStringLiteral("brave"))) return QStringLiteral("brave");
    if (low.contains(QStringLiteral("chrome"))) return QStringLiteral("chrome");
    if (low.contains(QStringLiteral("msedge")) || low.contains(QStringLiteral("edge"))) return QStringLiteral("msedge");
    if (low.contains(QStringLiteral("firefox"))) return QStringLiteral("firefox");
    if (low.contains(QStringLiteral("zen"))) return QStringLiteral("zen");
    if (low.contains(QStringLiteral("opera"))) return QStringLiteral("opera");
    if (low.contains(QStringLiteral("vivaldi"))) return QStringLiteral("vivaldi");
    if (low.contains(QStringLiteral("spotify"))) return QStringLiteral("spotify");
    if (low.contains(QStringLiteral("vlc"))) return QStringLiteral("vlc");
    if (low.contains(QStringLiteral("mpv"))) return QStringLiteral("mpv");
    if (low.contains(QStringLiteral("tidal"))) return QStringLiteral("tidal");
    if (low.contains(QStringLiteral("applemusic")) || low.contains(QStringLiteral("itunes"))) return QStringLiteral("applemusic");
    if (low.contains(QStringLiteral("foobar"))) return QStringLiteral("foobar2000");
    if (low.contains(QStringLiteral("aimp"))) return QStringLiteral("aimp");
    if (low.contains(QStringLiteral("discord"))) return QStringLiteral("discord");
    if (low.contains(QStringLiteral("musicbee"))) return QStringLiteral("musicbee");
    QString id = friendlyIdentityFromAumid(rawAumid).toLower();
    id.remove(QLatin1Char(' '));
    return id.isEmpty() ? QStringLiteral("media") : id;
}

QString normalizeFallbackKey(const QString& title, const QString& artist) {
    return title.trimmed().toLower() + QLatin1Char('|') + artist.trimmed().toLower();
}

bool readSmtcThumbnailBytes(
    IInspectable* props,
    CreateStreamOverRasFn pfnCreateStream,
    IInspectable* bufferFactory,
    const std::atomic<bool>* stopFlag,
    QByteArray& outBytes,
    bool& outIsAppIcon
) {
    outBytes.clear();
    outIsAppIcon = false;
    if (!props) {
        return false;
    }

    // IGlobalSystemMediaTransportControlsSessionMediaProperties::get_Thumbnail = slot 15
    using GetThumbFn = HRESULT(STDMETHODCALLTYPE*)(IInspectable*, IInspectable**);
    IInspectable* thumbRef = nullptr;
    if (FAILED(vtableSlot<GetThumbFn>(props, 15)(props, &thumbRef)) || !thumbRef) {
        return false;
    }

    // IRandomAccessStreamReference::OpenReadAsync = slot 6
    using OpenReadAsyncFn = HRESULT(STDMETHODCALLTYPE*)(IInspectable*, IInspectable**);
    IInspectable* openOp = nullptr;
    IInspectable* streamWithType = nullptr;
    if (SUCCEEDED(vtableSlot<OpenReadAsyncFn>(thumbRef, 6)(thumbRef, &openOp)) && openOp) {
        awaitGetResults<IInspectable*>(openOp, &streamWithType, stopFlag, 1000);
        openOp->Release();
    }
    thumbRef->Release();
    if (!streamWithType) {
        return false;
    }

    constexpr UINT32 kMaxBytes = 4 * 1024 * 1024;

    // Fast path (Ukikura): CreateStreamOverRandomAccessStream -> synchronous IStream::Read
    if (pfnCreateStream) {
        IStream* comStream = nullptr;
        if (SUCCEEDED(pfnCreateStream(streamWithType, kIidIStream, reinterpret_cast<void**>(&comStream))) && comStream) {
            STATSTG st = {};
            if (SUCCEEDED(comStream->Stat(&st, STATFLAG_NONAME)) && st.cbSize.QuadPart > 0 && st.cbSize.QuadPart <= kMaxBytes) {
                const ULONG targetSize = static_cast<ULONG>(st.cbSize.QuadPart);
                outBytes.resize(static_cast<qsizetype>(targetSize));
                ULONG totalRead = 0;
                while (totalRead < targetSize && (!stopFlag || !stopFlag->load())) {
                    ULONG chunkRead = 0;
                    const HRESULT rhr = comStream->Read(outBytes.data() + totalRead, targetSize - totalRead, &chunkRead);
                    if (FAILED(rhr) || chunkRead == 0) {
                        break;
                    }
                    totalRead += chunkRead;
                }
                outBytes.resize(static_cast<qsizetype>(totalRead));
            }
            comStream->Release();
            if (!outBytes.isEmpty()) {
                streamWithType->Release();
                // Ukikura heuristic: Chromium/Windows generic app icon placeholder is 17372 bytes
                outIsAppIcon = (outBytes.size() == 17372);
                return true;
            }
        }
    }

    // Fallback path: WinRT BufferFactory async read
    if (!bufferFactory) {
        streamWithType->Release();
        return false;
    }

    IInspectable* ras = nullptr;
    IInspectable* inputStream = nullptr;
    UINT64 streamSize = 0;
    if (SUCCEEDED(streamWithType->QueryInterface(kIidRandomAccessStream, reinterpret_cast<void**>(&ras))) && ras) {
        using GetSizeFn = HRESULT(STDMETHODCALLTYPE*)(IInspectable*, UINT64*);
        vtableSlot<GetSizeFn>(ras, 6)(ras, &streamSize);
        using GetInputAtFn = HRESULT(STDMETHODCALLTYPE*)(IInspectable*, UINT64, IInspectable**);
        vtableSlot<GetInputAtFn>(ras, 8)(ras, 0, &inputStream);
        ras->Release();
    }
    if (!inputStream) {
        streamWithType->QueryInterface(kIidInputStream, reinterpret_cast<void**>(&inputStream));
    }
    streamWithType->Release();
    if (!inputStream || streamSize > kMaxBytes) {
        if (inputStream) inputStream->Release();
        return false;
    }

    const UINT32 totalTarget = (streamSize > 0) ? static_cast<UINT32>(streamSize) : kMaxBytes;
    const UINT32 chunkCap = (streamSize > 0 && streamSize <= 512 * 1024) ? static_cast<UINT32>(streamSize) : (64 * 1024);
    if (streamSize > 0) {
        outBytes.reserve(static_cast<qsizetype>(streamSize));
    }

    using CreateBufFn = HRESULT(STDMETHODCALLTYPE*)(IInspectable*, UINT32, IInspectable**);
    using ReadAsyncFn = HRESULT(STDMETHODCALLTYPE*)(IInspectable*, IInspectable*, UINT32, UINT32, IInspectable**);
    using GetLenFn = HRESULT(STDMETHODCALLTYPE*)(IInspectable*, UINT32*);
    using GetRawBufFn = HRESULT(STDMETHODCALLTYPE*)(IUnknown*, BYTE**);

    while (static_cast<UINT32>(outBytes.size()) < totalTarget && (!stopFlag || !stopFlag->load())) {
        const UINT32 remain = totalTarget - static_cast<UINT32>(outBytes.size());
        const UINT32 want = std::min(chunkCap, remain);

        IInspectable* reqBuf = nullptr;
        if (FAILED(vtableSlot<CreateBufFn>(bufferFactory, 6)(bufferFactory, want, &reqBuf)) || !reqBuf) {
            break;
        }

        IInspectable* readOp = nullptr;
        IInspectable* outBuf = nullptr;
        if (SUCCEEDED(vtableSlot<ReadAsyncFn>(inputStream, 6)(inputStream, reqBuf, want, 0, &readOp)) && readOp) {
            awaitProgressGetResults<IInspectable*>(readOp, &outBuf, stopFlag, 1000);
            readOp->Release();
        }
        reqBuf->Release();
        if (!outBuf) {
            break;
        }

        UINT32 gotLen = 0;
        vtableSlot<GetLenFn>(outBuf, 7)(outBuf, &gotLen);
        if (gotLen == 0) {
            outBuf->Release();
            break;
        }

        IUnknown* byteAccess = nullptr;
        if (SUCCEEDED(outBuf->QueryInterface(kIidBufferByteAccess, reinterpret_cast<void**>(&byteAccess))) && byteAccess) {
            BYTE* rawPtr = nullptr;
            if (SUCCEEDED(vtableSlot<GetRawBufFn>(byteAccess, 3)(byteAccess, &rawPtr)) && rawPtr) {
                outBytes.append(reinterpret_cast<const char*>(rawPtr), static_cast<qsizetype>(gotLen));
            }
            byteAccess->Release();
        }
        outBuf->Release();

        if (streamSize == 0 && gotLen < want) {
            break;
        }
    }

    inputStream->Release();
    outIsAppIcon = (outBytes.size() == 17372);
    return !outBytes.isEmpty();
}

bool sendYouTubeShortcut(bool isNext) {
    struct SearchCtx {
        HWND ytHwnd = nullptr;
        HWND browserHwnd = nullptr;
    } ctx;

    EnumWindows([](HWND hWnd, LPARAM lParam) -> BOOL {
        if (!IsWindowVisible(hWnd)) return TRUE;
        auto* c = reinterpret_cast<SearchCtx*>(lParam);
        wchar_t title[512] = {};
        if (GetWindowTextW(hWnd, title, 512) > 0) {
            const QString t = QString::fromWCharArray(title);
            if (t.contains(QStringLiteral("YouTube"), Qt::CaseInsensitive)) {
                c->ytHwnd = hWnd;
                return FALSE;
            }
            if (!c->browserHwnd &&
                (t.contains(QStringLiteral("Brave"), Qt::CaseInsensitive) ||
                 t.contains(QStringLiteral("Chrome"), Qt::CaseInsensitive) ||
                 t.contains(QStringLiteral("Edge"), Qt::CaseInsensitive) ||
                 t.contains(QStringLiteral("Firefox"), Qt::CaseInsensitive) ||
                 t.contains(QStringLiteral("Opera"), Qt::CaseInsensitive))) {
                c->browserHwnd = hWnd;
            }
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));

    HWND target = ctx.ytHwnd ? ctx.ytHwnd : ctx.browserHwnd;
    if (!target) {
        return false;
    }
    Win32::focusWindowForce(target);
    Sleep(30);
    const BYTE key = isNext ? 0x4E : 0x50; // Shift+N / Shift+P
    keybd_event(VK_SHIFT, 0, 0, 0);
    keybd_event(key, 0, 0, 0);
    keybd_event(key, 0, KEYEVENTF_KEYUP, 0);
    keybd_event(VK_SHIFT, 0, KEYEVENTF_KEYUP, 0);
    return true;
}

bool invokeSessionBoolOp(IInspectable* session, size_t slot, const std::atomic<bool>* stopFlag, int timeoutMs = 800) {
    if (!session) return false;
    using ActionFn = HRESULT(STDMETHODCALLTYPE*)(IInspectable*, IInspectable**);
    IInspectable* op = nullptr;
    bool res = false;
    if (SUCCEEDED(vtableSlot<ActionFn>(session, slot)(session, &op)) && op) {
        boolean b = FALSE;
        if (awaitGetResults<boolean>(op, &b, stopFlag, timeoutMs)) {
            res = (b != FALSE);
        }
        op->Release();
    }
    return res;
}

bool invokeSessionSeekOp(IInspectable* session, INT64 ticks, const std::atomic<bool>* stopFlag, int timeoutMs = 600) {
    if (!session) return false;
    // TryChangePlaybackPositionAsync = slot 24
    using SeekFn = HRESULT(STDMETHODCALLTYPE*)(IInspectable*, INT64, IInspectable**);
    IInspectable* op = nullptr;
    bool res = false;
    if (SUCCEEDED(vtableSlot<SeekFn>(session, 24)(session, ticks, &op)) && op) {
        boolean b = FALSE;
        if (awaitGetResults<boolean>(op, &b, stopFlag, timeoutMs)) {
            res = (b != FALSE);
        }
        op->Release();
    }
    return res;
}

QString processStemForHwnd(HWND hwnd) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) return {};
    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc) return {};
    wchar_t pathBuf[MAX_PATH] = {};
    DWORD sz = MAX_PATH;
    QString stem;
    if (QueryFullProcessImageNameW(hProc, 0, pathBuf, &sz) && sz > 0) {
        stem = QFileInfo(QString::fromWCharArray(pathBuf, static_cast<int>(sz))).completeBaseName().toLower();
    }
    CloseHandle(hProc);
    return stem;
}

struct SessionMetaCache {
    QString title;
    QString artist;
    QString album;
    QString artUrl;
    bool isAppIcon = false;
    QString lastThumbKey;
    int thumbRetriesLeft = 0;
    double lastDurationSec = -1.0;
    int lastStatus = -1;
    qint64 lastMetaFetchMs = 0;
    double lastRawPosSec = -1.0;
    qint64 fallbackCaptureMs = 0;
};

} // namespace

QString MediaSource::saveCoverBytesToCache(const QByteArray& bytes, const QString& prefix) {
    if (bytes.size() < 16) {
        return {};
    }

    QString ext;
    const auto* u = reinterpret_cast<const unsigned char*>(bytes.constData());
    if (u[0] == 0x89 && u[1] == 0x50 && u[2] == 0x4E && u[3] == 0x47) {
        ext = QStringLiteral("png");
    } else if (u[0] == 0xFF && u[1] == 0xD8 && u[2] == 0xFF) {
        ext = QStringLiteral("jpg");
    } else if (bytes.size() >= 12 && std::memcmp(u, "RIFF", 4) == 0 && std::memcmp(u + 8, "WEBP", 4) == 0) {
        ext = QStringLiteral("webp");
    } else if (std::memcmp(u, "GIF8", 4) == 0) {
        ext = QStringLiteral("gif");
    } else if (u[0] == 0x42 && u[1] == 0x4D) {
        ext = QStringLiteral("bmp");
    }

    QString localAppData = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (localAppData.isEmpty()) {
        localAppData = QDir::homePath() + QStringLiteral("/AppData/Local");
    }
    const QString cacheDir = QDir(localAppData).filePath(QStringLiteral("SekaiPop/cache/sekaipop/cover-art"));
    QDir().mkpath(cacheDir);

    const QString hash = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex().left(16));
    const QString filePath = QDir(cacheDir).filePath(
        QStringLiteral("%1_%2.%3").arg(prefix, hash, ext.isEmpty() ? QStringLiteral("png") : ext)
    );

    if (!QFileInfo::exists(filePath) || QFileInfo(filePath).size() <= 0) {
        if (!ext.isEmpty()) {
            QSaveFile f(filePath);
            if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size() || !f.commit()) {
                return {};
            }
        } else {
            QImage img;
            if (!img.loadFromData(bytes) || img.isNull() || !img.save(filePath, "PNG")) {
                return {};
            }
        }

        // Keep cover cache tidy (retain newest 60 files)
        QDir dir(cacheDir);
        const QFileInfoList oldFiles = dir.entryInfoList(
            {QStringLiteral("smtc_*"), QStringLiteral("ukikura_*")}, QDir::Files, QDir::Time | QDir::Reversed);
        if (oldFiles.size() > 60) {
            const int toDrop = oldFiles.size() - 60;
            for (int i = 0; i < toDrop; ++i) {
                QFile::remove(oldFiles.at(i).absoluteFilePath());
            }
        }
    }

    return QUrl::fromLocalFile(filePath).toString();
}

MediaSource::MediaSource(QObject* parent)
    : QObject(parent) {
    m_worker = std::thread([this]() { workerLoop(); });
}

MediaSource::~MediaSource() {
    m_stop.store(true);
    m_cmdCv.notify_all();
    if (m_worker.joinable()) {
        m_worker.join();
    }
}

double MediaSource::position() const {
    if (!m_present) return 0.0;
    double pos = m_basePosition;
    if (m_playing && m_lastUpdatedEpochMs > 0) {
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        const double elapsed = static_cast<double>(nowMs - m_lastUpdatedEpochMs) / 1000.0;
        if (elapsed >= 0.0 && elapsed < 36000.0) {
            pos += elapsed;
        }
    }
    if (pos < 0.0) pos = 0.0;
    if (m_duration > 0.0 && pos > m_duration) pos = m_duration;
    return pos;
}

void MediaSource::enqueueCommand(const PendingCommand& cmd) {
    {
        std::lock_guard<std::mutex> lock(m_cmdMutex);
        // Coalesce rapid seeks on the same player so dragging never queues lag.
        if (cmd.type == CommandType::Seek && !m_cmdQueue.empty() &&
            m_cmdQueue.back().type == CommandType::Seek && m_cmdQueue.back().aumid == cmd.aumid) {
            m_cmdQueue.back().seekSec = cmd.seekSec;
        } else {
            m_cmdQueue.push_back(cmd);
        }
    }
    m_cmdCv.notify_one();
}

void MediaSource::togglePlayPause(const QString& aumid) {
    enqueueCommand({ CommandType::TogglePlayPause, aumid, 0.0 });
}

void MediaSource::play(const QString& aumid) {
    enqueueCommand({ CommandType::Play, aumid, 0.0 });
}

void MediaSource::pause(const QString& aumid) {
    enqueueCommand({ CommandType::Pause, aumid, 0.0 });
}

void MediaSource::stop(const QString& aumid) {
    enqueueCommand({ CommandType::Stop, aumid, 0.0 });
}

void MediaSource::next(const QString& aumid) {
    enqueueCommand({ CommandType::Next, aumid, 0.0 });
}

void MediaSource::previous(const QString& aumid) {
    enqueueCommand({ CommandType::Previous, aumid, 0.0 });
}

void MediaSource::seek(double seconds, const QString& aumid) {
    enqueueCommand({ CommandType::Seek, aumid, std::max(0.0, seconds) });
}

void MediaSource::setFallbackCover(const QString& title, const QString& artist, const QString& artUrl) {
    if (title.trimmed().isEmpty() || artUrl.trimmed().isEmpty()) {
        return;
    }
    const QString key = normalizeFallbackKey(title, artist);
    const QString titleOnlyKey = normalizeFallbackKey(title, QString());
    {
        std::lock_guard<std::mutex> lock(m_fallbackMutex);
        if (m_fallbackCovers.size() > 80) {
            m_fallbackCovers.clear();
        }
        m_fallbackCovers.insert(key, artUrl);
        if (!artist.trimmed().isEmpty()) {
            m_fallbackCovers.insert(titleOnlyKey, artUrl);
        }
    }
    m_forceRefresh.store(true);
    m_cmdCv.notify_one();
}

QString MediaSource::lookupFallbackCover(const QString& title, const QString& artist) const {
    if (title.trimmed().isEmpty()) {
        return {};
    }
    std::lock_guard<std::mutex> lock(m_fallbackMutex);
    const QString key = normalizeFallbackKey(title, artist);
    auto it = m_fallbackCovers.constFind(key);
    if (it != m_fallbackCovers.constEnd()) {
        return it.value();
    }
    const QString titleOnlyKey = normalizeFallbackKey(title, QString());
    it = m_fallbackCovers.constFind(titleOnlyKey);
    if (it != m_fallbackCovers.constEnd()) {
        return it.value();
    }
    return {};
}

void MediaSource::raisePlayer(const QString& aumid, const QString& trackTitle) {
    struct FindCtx {
        QString targetStem;
        QString targetIdentity;
        QString trackTitle;
        HWND exactMatch = nullptr;
        HWND appMatch = nullptr;
        HWND titleMatch = nullptr;
    } ctx;

    ctx.targetStem = desktopEntryFromAumid(aumid);
    ctx.targetIdentity = friendlyIdentityFromAumid(aumid).toLower();
    ctx.trackTitle = trackTitle.trimmed();

    EnumWindows([](HWND hWnd, LPARAM lParam) -> BOOL {
        if (!IsWindowVisible(hWnd) || GetWindow(hWnd, GW_OWNER) != nullptr) {
            return TRUE;
        }
        wchar_t titleBuf[512] = {};
        const int len = GetWindowTextW(hWnd, titleBuf, 512);
        if (len <= 0) {
            return TRUE;
        }
        auto* c = reinterpret_cast<FindCtx*>(lParam);
        const QString winTitle = QString::fromWCharArray(titleBuf, len);
        const QString stem = processStemForHwnd(hWnd);

        const bool stemMatches = !c->targetStem.isEmpty() && !stem.isEmpty() &&
            (stem == c->targetStem || stem.contains(c->targetStem) || c->targetStem.contains(stem) ||
             (!c->targetIdentity.isEmpty() && winTitle.contains(c->targetIdentity, Qt::CaseInsensitive)));
        const bool titleMatches = !c->trackTitle.isEmpty() && winTitle.contains(c->trackTitle, Qt::CaseInsensitive);

        if (stemMatches && titleMatches) {
            c->exactMatch = hWnd;
            return FALSE;
        }
        if (stemMatches && !c->appMatch) {
            c->appMatch = hWnd;
        }
        if (titleMatches && !c->titleMatch) {
            c->titleMatch = hWnd;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));

    HWND best = ctx.exactMatch ? ctx.exactMatch : (ctx.appMatch ? ctx.appMatch : ctx.titleMatch);
    if (best) {
        Win32::focusWindowForce(best);
    }
}

void MediaSource::workerLoop() {
    // Yield initial 240ms of boot CPU/WinRT bandwidth to Wallpaper + Bar first-frame rendering.
    for (int i = 0; i < 12 && !m_stop.load() && !m_forceRefresh.load(); ++i) {
        Sleep(20);
    }
    if (m_stop.load()) {
        return;
    }
    const HRESULT roHr = RoInitialize(RO_INIT_MULTITHREADED);
    const bool needUninit = SUCCEEDED(roHr);

    HMODULE hShcore = GetModuleHandleW(L"shcore.dll");
    if (!hShcore) {
        hShcore = LoadLibraryW(L"shcore.dll");
    }
    CreateStreamOverRasFn pfnCreateStream = hShcore
        ? reinterpret_cast<CreateStreamOverRasFn>(GetProcAddress(hShcore, "CreateStreamOverRandomAccessStream"))
        : nullptr;

    IInspectable* statics = nullptr;
    IInspectable* sessionMgr = nullptr;
    IInspectable* bufferFactory = nullptr;

    static constexpr wchar_t kClassName[] = L"Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager";
    HSTRING_HEADER header = {};
    HSTRING hClassName = nullptr;
    if (SUCCEEDED(WindowsCreateStringReference(kClassName, static_cast<UINT32>(std::size(kClassName) - 1), &header, &hClassName))) {
        RoGetActivationFactory(hClassName, kIidSessionManagerStatics, reinterpret_cast<void**>(&statics));
    }

    static constexpr wchar_t kBufClassName[] = L"Windows.Storage.Streams.Buffer";
    HSTRING_HEADER bufHeader = {};
    HSTRING hBufClassName = nullptr;
    if (SUCCEEDED(WindowsCreateStringReference(kBufClassName, static_cast<UINT32>(std::size(kBufClassName) - 1), &bufHeader, &hBufClassName))) {
        RoGetActivationFactory(hBufClassName, kIidBufferFactory, reinterpret_cast<void**>(&bufferFactory));
    }

    if (statics) {
        using RequestAsyncFn = HRESULT(STDMETHODCALLTYPE*)(IInspectable*, IInspectable**);
        IInspectable* op = nullptr;
        if (SUCCEEDED(vtableSlot<RequestAsyncFn>(statics, 6)(statics, &op)) && op) {
            awaitGetResults<IInspectable*>(op, &sessionMgr, &m_stop, 2500);
            op->Release();
        }
    }

    QHash<QString, SessionMetaCache> metaCache;

    while (!m_stop.load()) {
        // 1. Drain any pending commands first.
        std::deque<PendingCommand> cmds;
        bool forceMeta = m_forceRefresh.exchange(false);
        {
            std::lock_guard<std::mutex> lock(m_cmdMutex);
            cmds.swap(m_cmdQueue);
        }

        // Helper to collect all current SMTC sessions (deduplicated, sorted by priority).
        struct RawSessionEntry {
            IInspectable* session = nullptr;
            QString aumid;
            int status = 0;
            RawTimeline tl;
            int priority = 0; // 2=Playing, 1=Paused, 0=Other
        };

        auto collectSessions = [&]() -> std::vector<RawSessionEntry> {
            std::vector<RawSessionEntry> entries;
            if (!sessionMgr) return entries;

            auto addCandidate = [&](IInspectable* s) {
                if (!s) return;
                const QString aumid = readSessionAumid(s);
                const int st = readSessionPlaybackStatus(s);
                const RawTimeline tl = readSessionTimeline(s);
                const int prio = (st == 4) ? 2 : (st == 5 ? 1 : 0);

                // Deduplicate by AUMID keeping the highest priority / most recently updated session.
                for (auto& ex : entries) {
                    if (ex.aumid.compare(aumid, Qt::CaseInsensitive) == 0) {
                        if (prio > ex.priority || (prio == ex.priority && tl.lastUpdatedTicks > ex.tl.lastUpdatedTicks)) {
                            ex.session->Release();
                            ex.session = s;
                            ex.aumid = aumid;
                            ex.status = st;
                            ex.tl = tl;
                            ex.priority = prio;
                        } else {
                            s->Release();
                        }
                        return;
                    }
                }
                entries.push_back({ s, aumid, st, tl, prio });
            };

            using GetSessionsFn = HRESULT(STDMETHODCALLTYPE*)(IInspectable*, IInspectable**);
            IInspectable* sessionsVec = nullptr;
            if (SUCCEEDED(vtableSlot<GetSessionsFn>(sessionMgr, 7)(sessionMgr, &sessionsVec)) && sessionsVec) {
                using GetSizeFn = HRESULT(STDMETHODCALLTYPE*)(IInspectable*, UINT32*);
                using GetAtFn = HRESULT(STDMETHODCALLTYPE*)(IInspectable*, UINT32, IInspectable**);
                UINT32 count = 0;
                if (SUCCEEDED(vtableSlot<GetSizeFn>(sessionsVec, 7)(sessionsVec, &count))) {
                    for (UINT32 i = 0; i < count; ++i) {
                        IInspectable* cand = nullptr;
                        if (SUCCEEDED(vtableSlot<GetAtFn>(sessionsVec, 6)(sessionsVec, i, &cand)) && cand) {
                            addCandidate(cand);
                        }
                    }
                }
                sessionsVec->Release();
            }

            if (entries.empty()) {
                using GetCurrentSessionFn = HRESULT(STDMETHODCALLTYPE*)(IInspectable*, IInspectable**);
                IInspectable* cur = nullptr;
                if (SUCCEEDED(vtableSlot<GetCurrentSessionFn>(sessionMgr, 6)(sessionMgr, &cur)) && cur) {
                    addCandidate(cur);
                }
            }

            std::sort(entries.begin(), entries.end(), [](const RawSessionEntry& a, const RawSessionEntry& b) {
                if (a.priority != b.priority) return a.priority > b.priority;
                return a.tl.lastUpdatedTicks > b.tl.lastUpdatedTicks;
            });
            return entries;
        };

        auto releaseEntries = [](std::vector<RawSessionEntry>& entries) {
            for (auto& e : entries) {
                if (e.session) {
                    e.session->Release();
                    e.session = nullptr;
                }
            }
            entries.clear();
        };

        std::vector<RawSessionEntry> entries = collectSessions();

        // Execute queued commands (Ukikura robust dispatch with fallbacks).
        if (!cmds.empty()) {
            for (const PendingCommand& cmd : cmds) {
                IInspectable* target = nullptr;
                int targetStatus = 0;
                if (!cmd.aumid.isEmpty()) {
                    for (const auto& e : entries) {
                        if (e.aumid.compare(cmd.aumid, Qt::CaseInsensitive) == 0) {
                            target = e.session;
                            targetStatus = e.status;
                            break;
                        }
                    }
                }
                if (!target && !entries.empty()) {
                    target = entries.front().session;
                    targetStatus = entries.front().status;
                }

                bool handled = false;
                if (target) {
                    switch (cmd.type) {
                    case CommandType::TogglePlayPause: {
                        const bool isPlaying = (targetStatus == 4);
                        handled = invokeSessionBoolOp(target, isPlaying ? 11 /*TryPauseAsync*/ : 10 /*TryPlayAsync*/, &m_stop, 800);
                        if (!handled) {
                            handled = invokeSessionBoolOp(target, 20 /*TryTogglePlayPauseAsync*/, &m_stop, 800);
                        }
                        if (!handled) {
                            keybd_event(VK_MEDIA_PLAY_PAUSE, 0, 0, 0);
                            keybd_event(VK_MEDIA_PLAY_PAUSE, 0, KEYEVENTF_KEYUP, 0);
                            handled = true;
                        }
                        forceMeta = true;
                        break;
                    }
                    case CommandType::Play: {
                        handled = invokeSessionBoolOp(target, 10 /*TryPlayAsync*/, &m_stop, 800);
                        if (!handled) {
                            handled = invokeSessionBoolOp(target, 20 /*TryTogglePlayPauseAsync*/, &m_stop, 800);
                        }
                        if (!handled) {
                            keybd_event(VK_MEDIA_PLAY_PAUSE, 0, 0, 0);
                            keybd_event(VK_MEDIA_PLAY_PAUSE, 0, KEYEVENTF_KEYUP, 0);
                            handled = true;
                        }
                        forceMeta = true;
                        break;
                    }
                    case CommandType::Pause: {
                        handled = invokeSessionBoolOp(target, 11 /*TryPauseAsync*/, &m_stop, 800);
                        if (!handled) {
                            handled = invokeSessionBoolOp(target, 20 /*TryTogglePlayPauseAsync*/, &m_stop, 800);
                        }
                        if (!handled) {
                            keybd_event(VK_MEDIA_PLAY_PAUSE, 0, 0, 0);
                            keybd_event(VK_MEDIA_PLAY_PAUSE, 0, KEYEVENTF_KEYUP, 0);
                            handled = true;
                        }
                        forceMeta = true;
                        break;
                    }
                    case CommandType::Stop: {
                        handled = invokeSessionBoolOp(target, 12 /*TryStopAsync*/, &m_stop, 600);
                        if (!handled) {
                            keybd_event(VK_MEDIA_STOP, 0, 0, 0);
                            keybd_event(VK_MEDIA_STOP, 0, KEYEVENTF_KEYUP, 0);
                            handled = true;
                        }
                        forceMeta = true;
                        break;
                    }
                    case CommandType::Next: {
                        handled = invokeSessionBoolOp(target, 16 /*TrySkipNextAsync*/, &m_stop, 600);
                        if (!handled) {
                            handled = sendYouTubeShortcut(true);
                        }
                        if (!handled) {
                            keybd_event(VK_MEDIA_NEXT_TRACK, 0, 0, 0);
                            keybd_event(VK_MEDIA_NEXT_TRACK, 0, KEYEVENTF_KEYUP, 0);
                            handled = true;
                        }
                        forceMeta = true;
                        break;
                    }
                    case CommandType::Previous: {
                        handled = invokeSessionBoolOp(target, 17 /*TrySkipPreviousAsync*/, &m_stop, 600);
                        if (!handled) {
                            handled = invokeSessionSeekOp(target, 0, &m_stop, 500);
                        }
                        if (!handled) {
                            handled = sendYouTubeShortcut(false);
                        }
                        if (!handled) {
                            keybd_event(VK_MEDIA_PREV_TRACK, 0, 0, 0);
                            keybd_event(VK_MEDIA_PREV_TRACK, 0, KEYEVENTF_KEYUP, 0);
                            handled = true;
                        }
                        forceMeta = true;
                        break;
                    }
                    case CommandType::Seek: {
                        const INT64 ticks = static_cast<INT64>(std::llround(std::max(0.0, cmd.seekSec) * 10000000.0));
                        handled = invokeSessionSeekOp(target, ticks, &m_stop, 600);
                        break;
                    }
                    }
                } else {
                    // No active SMTC session: fallback to global virtual media keys.
                    if (cmd.type == CommandType::TogglePlayPause || cmd.type == CommandType::Play || cmd.type == CommandType::Pause) {
                        keybd_event(VK_MEDIA_PLAY_PAUSE, 0, 0, 0);
                        keybd_event(VK_MEDIA_PLAY_PAUSE, 0, KEYEVENTF_KEYUP, 0);
                    } else if (cmd.type == CommandType::Next) {
                        keybd_event(VK_MEDIA_NEXT_TRACK, 0, 0, 0);
                        keybd_event(VK_MEDIA_NEXT_TRACK, 0, KEYEVENTF_KEYUP, 0);
                    } else if (cmd.type == CommandType::Previous) {
                        keybd_event(VK_MEDIA_PREV_TRACK, 0, 0, 0);
                        keybd_event(VK_MEDIA_PREV_TRACK, 0, KEYEVENTF_KEYUP, 0);
                    } else if (cmd.type == CommandType::Stop) {
                        keybd_event(VK_MEDIA_STOP, 0, 0, 0);
                        keybd_event(VK_MEDIA_STOP, 0, KEYEVENTF_KEYUP, 0);
                    }
                }
            }

            // Short settle then re-read sessions so timeline/playback status reflect the command immediately.
            Sleep(35);
            releaseEntries(entries);
            entries = collectSessions();
        }

        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        QVector<MediaSessionInfo> snapshot;
        snapshot.reserve(static_cast<int>(entries.size()));
        bool anyPlaying = false;

        for (auto& e : entries) {
            const QString cacheKey = e.aumid.toLower();
            SessionMetaCache& mc = metaCache[cacheKey];

            const bool durChanged = (mc.lastDurationSec < 0.0) || (std::abs(e.tl.durationSec - mc.lastDurationSec) > 1.5);
            const bool statusChanged = (e.status != mc.lastStatus);
            const qint64 metaInterval = (e.status == 4) ? 2000 : 3000;
            const bool needMetaRefresh = forceMeta || mc.title.isEmpty() || durChanged || statusChanged ||
                                         (nowMs - mc.lastMetaFetchMs >= metaInterval) ||
                                         ((mc.artUrl.isEmpty() || mc.isAppIcon) && mc.thumbRetriesLeft > 0);

            if (needMetaRefresh && e.session) {
                mc.lastMetaFetchMs = nowMs;
                using TryGetPropsFn = HRESULT(STDMETHODCALLTYPE*)(IInspectable*, IInspectable**);
                IInspectable* propsOp = nullptr;
                if (SUCCEEDED(vtableSlot<TryGetPropsFn>(e.session, 7)(e.session, &propsOp)) && propsOp) {
                    IInspectable* props = nullptr;
                    if (awaitGetResults<IInspectable*>(propsOp, &props, &m_stop, 800) && props) {
                        using GetStrFn = HRESULT(STDMETHODCALLTYPE*)(IInspectable*, HSTRING*);
                        HSTRING hTitle = nullptr;
                        HSTRING hArtist = nullptr;
                        HSTRING hAlbum = nullptr;
                        QString newTitle, newArtist, newAlbum;
                        if (SUCCEEDED(vtableSlot<GetStrFn>(props, 6)(props, &hTitle)) && hTitle) {
                            newTitle = hstringToQString(hTitle);
                            WindowsDeleteString(hTitle);
                        }
                        if (SUCCEEDED(vtableSlot<GetStrFn>(props, 9)(props, &hArtist)) && hArtist) {
                            newArtist = hstringToQString(hArtist);
                            WindowsDeleteString(hArtist);
                        }
                        if (SUCCEEDED(vtableSlot<GetStrFn>(props, 10)(props, &hAlbum)) && hAlbum) {
                            newAlbum = hstringToQString(hAlbum);
                            WindowsDeleteString(hAlbum);
                        }

                        const QString thumbKey = e.aumid + QLatin1Char('|') + newTitle + QLatin1Char('|') + newArtist;
                        if (thumbKey != mc.lastThumbKey) {
                            mc.lastThumbKey = thumbKey;
                            mc.artUrl.clear();
                            mc.isAppIcon = false;
                            mc.thumbRetriesLeft = 4;
                        }

                        mc.title = newTitle;
                        mc.artist = newArtist;
                        mc.album = newAlbum;

                        if ((mc.artUrl.isEmpty() || mc.isAppIcon) && mc.thumbRetriesLeft > 0) {
                            --mc.thumbRetriesLeft;
                            QByteArray thumbBytes;
                            bool isIcon = false;
                            if (readSmtcThumbnailBytes(props, pfnCreateStream, bufferFactory, &m_stop, thumbBytes, isIcon)) {
                                const QString saved = saveCoverBytesToCache(thumbBytes, QStringLiteral("smtc"));
                                if (!saved.isEmpty()) {
                                    mc.artUrl = saved;
                                    mc.isAppIcon = isIcon;
                                    if (!isIcon) {
                                        mc.thumbRetriesLeft = 0;
                                    }
                                }
                            }
                        }
                        props->Release();
                    }
                    propsOp->Release();
                }
            }

            mc.lastDurationSec = e.tl.durationSec;
            mc.lastStatus = e.status;

            // Track fallback capture timestamp if SMTC LastUpdatedTime is missing.
            if (e.tl.lastUpdatedEpochMs <= 0) {
                if (std::abs(e.tl.positionSec - mc.lastRawPosSec) > 0.01 || statusChanged || mc.fallbackCaptureMs <= 0) {
                    mc.lastRawPosSec = e.tl.positionSec;
                    mc.fallbackCaptureMs = nowMs;
                }
            }

            MediaSessionInfo info;
            info.aumid = e.aumid.isEmpty() ? QStringLiteral("default") : e.aumid;
            info.identity = friendlyIdentityFromAumid(e.aumid);
            info.desktopEntry = desktopEntryFromAumid(e.aumid);
            info.playbackStatus = e.status;
            info.playing = (e.status == 4);
            info.title = mc.title.isEmpty() ? info.identity : mc.title;
            info.artist = mc.artist;
            info.album = mc.album;
            info.present = !mc.title.isEmpty() || info.playing || (e.status == 5);
            info.durationSec = e.tl.durationSec;
            info.basePositionSec = e.tl.positionSec;
            info.lastUpdatedEpochMs = (e.tl.lastUpdatedEpochMs > 0) ? e.tl.lastUpdatedEpochMs : mc.fallbackCaptureMs;
            info.timelineOrderTicks = e.tl.lastUpdatedTicks;
            info.isAppIcon = mc.isAppIcon;

            // Apply Ukikura fallback high-res cover if SMTC has no cover or generic app icon.
            const QString fallbackArt = lookupFallbackCover(mc.title, mc.artist);
            if (!fallbackArt.isEmpty() && (mc.artUrl.isEmpty() || mc.isAppIcon)) {
                info.artUrl = fallbackArt;
                info.isAppIcon = false;
            } else {
                info.artUrl = mc.artUrl;
            }

            if (info.playing) {
                anyPlaying = true;
            }
            if (info.present) {
                snapshot.push_back(std::move(info));
            }
        }

        releaseEntries(entries);

        // Prune stale cache entries for closed sessions.
        if (metaCache.size() > 16) {
            QHash<QString, SessionMetaCache> pruned;
            for (const auto& s : snapshot) {
                const QString k = s.aumid.toLower();
                if (metaCache.contains(k)) {
                    pruned.insert(k, metaCache.value(k));
                }
            }
            metaCache.swap(pruned);
        }

        // Dispatch state snapshot to GUI thread.
        QMetaObject::invokeMethod(this, [this, snapshot]() {
            bool metaChanged = (snapshot.size() != m_sessions.size());
            bool tlChanged = false;

            if (!metaChanged) {
                for (int i = 0; i < snapshot.size(); ++i) {
                    const auto& a = snapshot.at(i);
                    const auto& b = m_sessions.at(i);
                    if (a.aumid != b.aumid || a.playing != b.playing || a.playbackStatus != b.playbackStatus ||
                        a.title != b.title || a.artist != b.artist || a.album != b.album ||
                        a.artUrl != b.artUrl || a.identity != b.identity ||
                        std::abs(a.durationSec - b.durationSec) > 1.5) {
                        metaChanged = true;
                        break;
                    }
                    if (std::abs(a.basePositionSec - b.basePositionSec) > 0.75) {
                        tlChanged = true;
                    }
                }
            } else {
                tlChanged = true;
            }

            if (metaChanged || tlChanged) {
                m_sessions = snapshot;

                if (!m_sessions.isEmpty()) {
                    const auto& top = m_sessions.first();
                    m_present = top.present;
                    m_playing = top.playing;
                    m_title = top.title;
                    m_artist = top.artist;
                    m_album = top.album;
                    m_artUrl = top.artUrl;
                    m_identity = top.identity;
                    m_basePosition = top.basePositionSec;
                    m_duration = top.durationSec;
                    m_lastUpdatedEpochMs = top.lastUpdatedEpochMs;
                } else {
                    m_present = false;
                    m_playing = false;
                    m_title.clear();
                    m_artist.clear();
                    m_album.clear();
                    m_artUrl.clear();
                    m_identity = QStringLiteral("Media");
                    m_basePosition = 0.0;
                    m_duration = 0.0;
                    m_lastUpdatedEpochMs = 0;
                }

                if (metaChanged) {
                    Q_EMIT changed();
                }
                Q_EMIT sessionsChanged();
                Q_EMIT timelineUpdated();
            }
        }, Qt::QueuedConnection);

        // Adaptive sleep: 450ms when playing (currentPositionSec() interpolates smoothly locally),
        // 750ms when paused, 1200ms idle. Wakes instantaneously on any user command.
        const int sleepMs = anyPlaying ? 450 : (!snapshot.isEmpty() ? 750 : 1200);
        std::unique_lock<std::mutex> lock(m_cmdMutex);
        m_cmdCv.wait_for(lock, std::chrono::milliseconds(sleepMs), [this]() {
            return m_stop.load() || !m_cmdQueue.empty() || m_forceRefresh.load();
        });
    }

    if (bufferFactory) bufferFactory->Release();
    if (sessionMgr) sessionMgr->Release();
    if (statics) statics->Release();
    if (needUninit) RoUninitialize();
}

} // namespace SekaiPop::Services
