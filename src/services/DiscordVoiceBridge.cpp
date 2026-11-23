#include "DiscordVoiceBridge.h"
#include "../quickshell/QsTypes.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>
#include <cstring>
#include <winhttp.h>

namespace SekaiPop::Services {

namespace {

constexpr const char* kClientId = "207646673902501888"; // Discord StreamKit public client
constexpr uint32_t kOpHandshake = 0;
constexpr uint32_t kOpFrame = 1;
constexpr uint32_t kOpClose = 2;
constexpr uint32_t kOpPing = 3;
constexpr uint32_t kOpPong = 4;
constexpr ULONGLONG kAuthTimeoutMs = 20000;
constexpr uint32_t kMaxFrameBytes = 4 * 1024 * 1024;

QString defaultTokenPath() {
    QString cacheRoot;
    if (auto* g = Quickshell::QsGlobal::instance()) {
        cacheRoot = Quickshell::normalizeWinPath(g->cacheHome());
    }
    if (cacheRoot.isEmpty()) {
        cacheRoot = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
    }
    return QDir(cacheRoot).filePath(QStringLiteral("sekaipop/discord-voice-token.json"));
}

} // namespace

DiscordVoiceBridge::DiscordVoiceBridge(QObject* parent)
    : QObject(parent)
    , m_tokenPath(defaultTokenPath()) {
    m_wakeEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
}

DiscordVoiceBridge::~DiscordVoiceBridge() {
    stop();
    if (m_wakeEvent) {
        CloseHandle(m_wakeEvent);
        m_wakeEvent = nullptr;
    }
}

void DiscordVoiceBridge::start() {
    if (m_worker.joinable()) {
        return;
    }
    m_stop.store(false);
    if (m_wakeEvent) {
        ResetEvent(m_wakeEvent);
    }
    m_worker = std::thread([this]() { workerMain(); });
}

void DiscordVoiceBridge::stop() {
    m_stop.store(true);
    if (m_wakeEvent) {
        SetEvent(m_wakeEvent);
    }
    if (m_pipe != INVALID_HANDLE_VALUE) {
        CancelIoEx(m_pipe, nullptr);
    }
    if (m_worker.joinable()) {
        m_worker.join();
    }
    closePipe();
}

void DiscordVoiceBridge::sendCommand(const QString& jsonLine) {
    const QStringList lines = jsonLine.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    bool queuedAny = false;
    {
        std::lock_guard<std::mutex> lock(m_cmdMutex);
        for (const QString& raw : lines) {
            QJsonParseError err;
            const QJsonDocument doc = QJsonDocument::fromJson(raw.trimmed().toUtf8(), &err);
            if (err.error == QJsonParseError::NoError && doc.isObject()) {
                m_cmdQueue.enqueue(doc.object());
                queuedAny = true;
            }
        }
    }
    if (queuedAny && m_wakeEvent) {
        SetEvent(m_wakeEvent);
    }
}

void DiscordVoiceBridge::emitJson(const QString& type, const QJsonObject& extra) {
    QJsonObject obj = extra;
    obj.insert(QStringLiteral("type"), type);
    const QString line = QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    Q_EMIT lineReady(line);
}

QString DiscordVoiceBridge::loadSavedToken() const {
    QFile f(m_tokenPath);
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) {
        return {};
    }
    return doc.object().value(QStringLiteral("access_token")).toString().trimmed();
}

void DiscordVoiceBridge::saveToken(const QString& accessToken) const {
    QDir().mkpath(QFileInfo(m_tokenPath).absolutePath());
    QFile f(m_tokenPath);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QJsonObject obj;
        obj.insert(QStringLiteral("access_token"), accessToken);
        f.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    }
}

void DiscordVoiceBridge::clearSavedToken() const {
    QFile::remove(m_tokenPath);
}

QString DiscordVoiceBridge::exchangeAuthCodeWinHttp(const QString& code) const {
    HINTERNET hSession = WinHttpOpen(
        L"quickshell-ii/DiscordVoice",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );
    if (!hSession) {
        return {};
    }
    WinHttpSetTimeouts(hSession, 5000, 8000, 8000, 10000);

    HINTERNET hConnect = WinHttpConnect(hSession, L"streamkit.discord.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return {};
    }

    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect,
        L"POST",
        L"/overlay/token",
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE
    );
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return {};
    }

    QJsonObject reqObj;
    reqObj.insert(QStringLiteral("code"), code);
    const QByteArray reqBody = QJsonDocument(reqObj).toJson(QJsonDocument::Compact);

    const wchar_t* headers = L"Content-Type: application/json\r\n";
    QByteArray respBody;
    if (WinHttpSendRequest(
            hRequest,
            headers,
            static_cast<DWORD>(wcslen(headers)),
            const_cast<char*>(reqBody.constData()),
            static_cast<DWORD>(reqBody.size()),
            static_cast<DWORD>(reqBody.size()),
            0) &&
        WinHttpReceiveResponse(hRequest, nullptr)) {
        DWORD statusCode = 0;
        DWORD statusLen = sizeof(statusCode);
        WinHttpQueryHeaders(
            hRequest,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &statusCode,
            &statusLen,
            WINHTTP_NO_HEADER_INDEX
        );
        if (statusCode >= 200 && statusCode < 300) {
            DWORD avail = 0;
            while (WinHttpQueryDataAvailable(hRequest, &avail) && avail > 0) {
                const int oldSize = respBody.size();
                if (oldSize > 128 * 1024) {
                    break;
                }
                respBody.resize(oldSize + static_cast<int>(avail));
                DWORD readBytes = 0;
                if (!WinHttpReadData(hRequest, respBody.data() + oldSize, avail, &readBytes)) {
                    break;
                }
                respBody.resize(oldSize + static_cast<int>(readBytes));
            }
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    const QJsonDocument doc = QJsonDocument::fromJson(respBody);
    if (!doc.isObject()) {
        return {};
    }
    return doc.object().value(QStringLiteral("access_token")).toString().trimmed();
}

void DiscordVoiceBridge::closePipe() {
    if (m_pipe != INVALID_HANDLE_VALUE) {
        CancelIoEx(m_pipe, nullptr);
        CloseHandle(m_pipe);
        m_pipe = INVALID_HANDLE_VALUE;
    }
    m_currentPipePath.clear();
    m_pending.clear();
    m_authPendingNonce.clear();
    m_authPendingPath.clear();
    m_authDeadlineTick = 0;
}

bool DiscordVoiceBridge::writePipeFrame(uint32_t opcode, const QByteArray& body) {
    if (m_pipe == INVALID_HANDLE_VALUE || m_stop.load()) {
        return false;
    }
    QByteArray packet;
    packet.resize(8 + body.size());
    const uint32_t len = static_cast<uint32_t>(body.size());
    std::memcpy(packet.data(), &opcode, 4);
    std::memcpy(packet.data() + 4, &len, 4);
    if (!body.isEmpty()) {
        std::memcpy(packet.data() + 8, body.constData(), static_cast<size_t>(body.size()));
    }

    OVERLAPPED ov = {};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ov.hEvent) {
        return false;
    }

    const char* ptr = packet.constData();
    size_t remaining = static_cast<size_t>(packet.size());
    bool ok = true;
    while (remaining > 0 && !m_stop.load()) {
        ResetEvent(ov.hEvent);
        DWORD written = 0;
        BOOL r = WriteFile(m_pipe, ptr, static_cast<DWORD>(remaining), &written, &ov);
        if (!r) {
            const DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                const DWORD waitRes = WaitForSingleObject(ov.hEvent, 3000);
                if (waitRes != WAIT_OBJECT_0 || !GetOverlappedResult(m_pipe, &ov, &written, FALSE)) {
                    CancelIoEx(m_pipe, &ov);
                    ok = false;
                    break;
                }
            } else {
                ok = false;
                break;
            }
        }
        if (written == 0) {
            ok = false;
            break;
        }
        ptr += written;
        remaining -= written;
    }

    CloseHandle(ov.hEvent);
    return ok;
}

bool DiscordVoiceBridge::readPipeExact(void* dst, size_t len, DWORD timeoutMs) {
    if (m_pipe == INVALID_HANDLE_VALUE || m_stop.load()) {
        return false;
    }
    OVERLAPPED ov = {};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ov.hEvent) {
        return false;
    }

    char* ptr = static_cast<char*>(dst);
    size_t remaining = len;
    bool ok = true;
    const ULONGLONG deadline = GetTickCount64() + timeoutMs;

    while (remaining > 0 && !m_stop.load()) {
        ResetEvent(ov.hEvent);
        DWORD got = 0;
        BOOL r = ReadFile(m_pipe, ptr, static_cast<DWORD>(remaining), &got, &ov);
        if (!r) {
            const DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                const ULONGLONG now = GetTickCount64();
                const DWORD waitMs = (now >= deadline) ? 0 : static_cast<DWORD>(deadline - now);
                const DWORD waitRes = WaitForSingleObject(ov.hEvent, waitMs);
                if (waitRes != WAIT_OBJECT_0 || !GetOverlappedResult(m_pipe, &ov, &got, FALSE)) {
                    CancelIoEx(m_pipe, &ov);
                    ok = false;
                    break;
                }
            } else {
                ok = false;
                break;
            }
        }
        if (got == 0) {
            ok = false;
            break;
        }
        ptr += got;
        remaining -= got;
    }

    CloseHandle(ov.hEvent);
    return ok;
}

bool DiscordVoiceBridge::readPipeFrame(uint32_t& outOpcode, QJsonObject& outPayload, DWORD timeoutMs) {
    uint32_t header[2] = {0, 0};
    if (!readPipeExact(header, sizeof(header), timeoutMs)) {
        return false;
    }
    outOpcode = header[0];
    const uint32_t length = header[1];
    if (length > kMaxFrameBytes) {
        return false;
    }
    QByteArray body(static_cast<int>(length), Qt::Uninitialized);
    if (length > 0 && !readPipeExact(body.data(), length, timeoutMs)) {
        return false;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    outPayload = doc.isObject() ? doc.object() : QJsonObject();
    return true;
}

bool DiscordVoiceBridge::connectPipe() {
    if (m_pipe != INVALID_HANDLE_VALUE) {
        return true;
    }

    for (int i = 0; i < 10 && !m_stop.load(); ++i) {
        const QString path = QStringLiteral("\\\\.\\pipe\\discord-ipc-%1").arg(i);
        if (m_authFailedPaths.contains(path)) {
            continue;
        }
        const std::wstring wpath = path.toStdWString();
        HANDLE h = CreateFileW(
            wpath.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED,
            nullptr
        );
        if (h == INVALID_HANDLE_VALUE) {
            continue;
        }

        m_pipe = h;
        m_currentPipePath = path;

        QJsonObject hs;
        hs.insert(QStringLiteral("v"), 1);
        hs.insert(QStringLiteral("client_id"), QString::fromLatin1(kClientId));
        if (!writePipeFrame(kOpHandshake, QJsonDocument(hs).toJson(QJsonDocument::Compact))) {
            closePipe();
            continue;
        }

        uint32_t op = 0;
        QJsonObject ready;
        if (!readPipeFrame(op, ready, 4000) || op == kOpClose ||
            ready.value(QStringLiteral("evt")).toString() != QStringLiteral("READY")) {
            closePipe();
            continue;
        }

        qInfo("[DiscordVoiceBridge] Connected to %s", qPrintable(path));
        emitJson(QStringLiteral("backend"), QJsonObject{{QStringLiteral("backend"), QStringLiteral("discord")}});
        emitJson(QStringLiteral("connected"), QJsonObject{{QStringLiteral("socket"), path}});

        const QString token = loadSavedToken();
        if (!token.isEmpty()) {
            sendAuthenticate(token);
        } else {
            emitJson(QStringLiteral("auth_required"));
        }
        return true;
    }

    emitJson(QStringLiteral("unavailable"),
             QJsonObject{{QStringLiteral("message"), QStringLiteral("Discord is not running or RPC is unavailable")}});
    return false;
}

void DiscordVoiceBridge::sendRpcCommand(const QString& cmd, const QJsonObject& args, bool hasArgs) {
    if (m_pipe == INVALID_HANDLE_VALUE) {
        return;
    }
    const QString nonce = QString::number(++m_nonce);
    m_pending.insert(nonce, cmd);
    QJsonObject payload;
    payload.insert(QStringLiteral("cmd"), cmd);
    payload.insert(QStringLiteral("nonce"), nonce);
    if (hasArgs) {
        payload.insert(QStringLiteral("args"), args);
    }
    if (!writePipeFrame(kOpFrame, QJsonDocument(payload).toJson(QJsonDocument::Compact))) {
        closePipe();
        m_users.clear();
        m_channel = QJsonObject();
        m_hasChannel = false;
        emitJson(QStringLiteral("disconnected"));
    }
}

void DiscordVoiceBridge::sendRpcSubscribe(const QString& evt, const QString& channelId) {
    if (m_pipe == INVALID_HANDLE_VALUE) {
        return;
    }
    const QString nonce = QString::number(++m_nonce);
    m_pending.insert(nonce, QStringLiteral("SUB_") + evt);
    QJsonObject payload;
    payload.insert(QStringLiteral("cmd"), QStringLiteral("SUBSCRIBE"));
    payload.insert(QStringLiteral("evt"), evt);
    payload.insert(QStringLiteral("nonce"), nonce);
    if (!channelId.isEmpty()) {
        payload.insert(QStringLiteral("args"), QJsonObject{{QStringLiteral("channel_id"), channelId}});
    }
    writePipeFrame(kOpFrame, QJsonDocument(payload).toJson(QJsonDocument::Compact));
}

void DiscordVoiceBridge::sendAuthenticate(const QString& accessToken) {
    sendRpcCommand(QStringLiteral("AUTHENTICATE"),
                   QJsonObject{{QStringLiteral("access_token"), accessToken}},
                   true);
}

void DiscordVoiceBridge::startAuthorize() {
    if (!connectPipe()) {
        return;
    }
    emitJson(QStringLiteral("authorizing"));
    const QString nonce = QString::number(++m_nonce);
    m_pending.insert(nonce, QStringLiteral("AUTHORIZE"));
    m_authPendingNonce = nonce;
    m_authPendingPath = m_currentPipePath;
    m_authDeadlineTick = GetTickCount64() + kAuthTimeoutMs;

    QJsonObject args;
    args.insert(QStringLiteral("client_id"), QString::fromLatin1(kClientId));
    args.insert(QStringLiteral("scopes"), QJsonArray{
        QStringLiteral("rpc"),
        QStringLiteral("rpc.voice.read"),
        QStringLiteral("rpc.voice.write")
    });
    args.insert(QStringLiteral("prompt"), QStringLiteral("none"));

    QJsonObject payload;
    payload.insert(QStringLiteral("cmd"), QStringLiteral("AUTHORIZE"));
    payload.insert(QStringLiteral("args"), args);
    payload.insert(QStringLiteral("nonce"), nonce);
    writePipeFrame(kOpFrame, QJsonDocument(payload).toJson(QJsonDocument::Compact));
}

QJsonArray DiscordVoiceBridge::currentUsersArray() const {
    QJsonArray arr;
    for (auto it = m_users.cbegin(); it != m_users.cend(); ++it) {
        arr.append(it.value());
    }
    return arr;
}

QJsonObject DiscordVoiceBridge::parseVoiceUser(const QJsonObject& data) {
    const QJsonObject user = data.value(QStringLiteral("user")).toObject();
    const QJsonObject voice = data.value(QStringLiteral("voice_state")).toObject();
    const QString username = user.value(QStringLiteral("username")).toString();
    QString nick = data.value(QStringLiteral("nick")).toString();
    if (nick.isEmpty()) {
        nick = user.value(QStringLiteral("global_name")).toString();
    }
    if (nick.isEmpty()) {
        nick = username;
    }
    const bool muted = voice.value(QStringLiteral("mute")).toBool() || voice.value(QStringLiteral("self_mute")).toBool();
    const bool deaf = voice.value(QStringLiteral("deaf")).toBool() || voice.value(QStringLiteral("self_deaf")).toBool();
    return QJsonObject{
        {QStringLiteral("id"), user.value(QStringLiteral("id")).toString()},
        {QStringLiteral("username"), username},
        {QStringLiteral("nick"), nick},
        {QStringLiteral("avatar"), user.value(QStringLiteral("avatar")).toString()},
        {QStringLiteral("mute"), muted},
        {QStringLiteral("deaf"), deaf},
        {QStringLiteral("speaking"), false}
    };
}

void DiscordVoiceBridge::selectChannel(const QJsonObject& data, bool hasChannel) {
    m_users.clear();
    if (!hasChannel || data.value(QStringLiteral("id")).toString().isEmpty()) {
        m_channel = QJsonObject();
        m_hasChannel = false;
        emitJson(QStringLiteral("voice_channel"), QJsonObject{
            {QStringLiteral("channel"), QJsonValue::Null},
            {QStringLiteral("users"), QJsonArray()}
        });
        return;
    }

    const QString chId = data.value(QStringLiteral("id")).toString();
    const QString guildId = data.value(QStringLiteral("guild_id")).toString();
    const GuildInfo gInfo = m_guilds.value(guildId);

    m_channel = QJsonObject{
        {QStringLiteral("id"), chId},
        {QStringLiteral("name"), data.value(QStringLiteral("name")).toString(QStringLiteral("Voice channel"))},
        {QStringLiteral("guild_id"), guildId},
        {QStringLiteral("guild_name"), gInfo.name},
        {QStringLiteral("guild_icon_url"), gInfo.iconUrl}
    };
    m_hasChannel = true;

    const QJsonArray states = data.value(QStringLiteral("voice_states")).toArray();
    for (const QJsonValue& v : states) {
        const QJsonObject participant = parseVoiceUser(v.toObject());
        const QString uid = participant.value(QStringLiteral("id")).toString();
        if (!uid.isEmpty()) {
            m_users.insert(uid, participant);
        }
    }

    emitJson(QStringLiteral("voice_channel"), QJsonObject{
        {QStringLiteral("channel"), m_channel},
        {QStringLiteral("users"), currentUsersArray()}
    });

    for (const char* ev : {"VOICE_STATE_CREATE", "VOICE_STATE_UPDATE", "VOICE_STATE_DELETE",
                           "SPEAKING_START", "SPEAKING_STOP"}) {
        sendRpcSubscribe(QString::fromLatin1(ev), chId);
    }
    sendRpcCommand(QStringLiteral("GET_VOICE_SETTINGS"));
    if (!guildId.isEmpty()) {
        sendRpcCommand(QStringLiteral("GET_GUILD"), QJsonObject{{QStringLiteral("guild_id"), guildId}}, true);
    }
}

void DiscordVoiceBridge::handleRpcResponse(const QString& cmd, const QJsonObject& data) {
    if (cmd == QStringLiteral("AUTHORIZE")) {
        const QString code = data.value(QStringLiteral("code")).toString();
        if (code.isEmpty()) {
            emitJson(QStringLiteral("error"), QJsonObject{{QStringLiteral("message"), QStringLiteral("Discord authorization was not completed")}});
            return;
        }
        const QString token = exchangeAuthCodeWinHttp(code);
        if (token.isEmpty()) {
            emitJson(QStringLiteral("error"), QJsonObject{{QStringLiteral("message"), QStringLiteral("Discord token exchange failed")}});
            return;
        }
        saveToken(token);
        sendAuthenticate(token);
    } else if (cmd == QStringLiteral("AUTHENTICATE")) {
        qInfo("[DiscordVoiceBridge] Authenticated as %s",
              qPrintable(data.value(QStringLiteral("user")).toObject().value(QStringLiteral("username")).toString()));
        emitJson(QStringLiteral("authenticated"), QJsonObject{{QStringLiteral("user"), data.value(QStringLiteral("user")).toObject()}});
        sendRpcSubscribe(QStringLiteral("VOICE_CHANNEL_SELECT"));
        sendRpcSubscribe(QStringLiteral("VOICE_SETTINGS_UPDATE"));
        sendRpcCommand(QStringLiteral("GET_SELECTED_VOICE_CHANNEL"));
        sendRpcCommand(QStringLiteral("GET_VOICE_SETTINGS"));
    } else if (cmd == QStringLiteral("GET_SELECTED_VOICE_CHANNEL")) {
        selectChannel(data, !data.isEmpty() && !data.value(QStringLiteral("id")).toString().isEmpty());
    } else if (cmd == QStringLiteral("GET_GUILD")) {
        const QString guildId = data.value(QStringLiteral("id")).toString();
        if (!guildId.isEmpty()) {
            GuildInfo info;
            info.name = data.value(QStringLiteral("name")).toString();
            info.iconUrl = data.value(QStringLiteral("icon_url")).toString();
            m_guilds.insert(guildId, info);
            if (m_hasChannel && m_channel.value(QStringLiteral("guild_id")).toString() == guildId) {
                m_channel.insert(QStringLiteral("guild_name"), info.name);
                m_channel.insert(QStringLiteral("guild_icon_url"), info.iconUrl);
                emitJson(QStringLiteral("voice_channel"), QJsonObject{
                    {QStringLiteral("channel"), m_channel},
                    {QStringLiteral("users"), currentUsersArray()}
                });
            }
        }
    } else if (cmd == QStringLiteral("GET_VOICE_SETTINGS") || cmd == QStringLiteral("SET_VOICE_SETTINGS")) {
        m_muted = data.value(QStringLiteral("mute")).toBool();
        m_deafened = data.value(QStringLiteral("deaf")).toBool();
        emitJson(QStringLiteral("voice_settings"), QJsonObject{
            {QStringLiteral("mute"), m_muted},
            {QStringLiteral("deaf"), m_deafened}
        });
    }
}

void DiscordVoiceBridge::handleRpcDispatch(const QString& evt, const QJsonObject& data) {
    if (evt == QStringLiteral("VOICE_CHANNEL_SELECT")) {
        sendRpcCommand(QStringLiteral("GET_SELECTED_VOICE_CHANNEL"));
    } else if (evt == QStringLiteral("VOICE_STATE_CREATE") || evt == QStringLiteral("VOICE_STATE_UPDATE")) {
        QJsonObject participant = parseVoiceUser(data);
        const QString uid = participant.value(QStringLiteral("id")).toString();
        if (!uid.isEmpty()) {
            if (m_users.contains(uid)) {
                participant.insert(QStringLiteral("speaking"), m_users.value(uid).value(QStringLiteral("speaking")).toBool());
            }
            m_users.insert(uid, participant);
        }
        emitJson(QStringLiteral("voice_state"), QJsonObject{{QStringLiteral("users"), currentUsersArray()}});
    } else if (evt == QStringLiteral("VOICE_STATE_DELETE")) {
        const QString uid = data.value(QStringLiteral("user")).toObject().value(QStringLiteral("id")).toString();
        if (!uid.isEmpty()) {
            m_users.remove(uid);
        }
        emitJson(QStringLiteral("voice_state"), QJsonObject{{QStringLiteral("users"), currentUsersArray()}});
    } else if (evt == QStringLiteral("SPEAKING_START") || evt == QStringLiteral("SPEAKING_STOP")) {
        const QString uid = data.value(QStringLiteral("user_id")).toString();
        auto it = m_users.find(uid);
        if (it != m_users.end()) {
            it->insert(QStringLiteral("speaking"), evt == QStringLiteral("SPEAKING_START"));
        }
        emitJson(QStringLiteral("voice_state"), QJsonObject{{QStringLiteral("users"), currentUsersArray()}});
    } else if (evt == QStringLiteral("VOICE_SETTINGS_UPDATE")) {
        m_muted = data.value(QStringLiteral("mute")).toBool();
        m_deafened = data.value(QStringLiteral("deaf")).toBool();
        emitJson(QStringLiteral("voice_settings"), QJsonObject{
            {QStringLiteral("mute"), m_muted},
            {QStringLiteral("deaf"), m_deafened}
        });
    }
}

void DiscordVoiceBridge::handleRpcFrame(const QJsonObject& payload) {
    const QString nonce = payload.value(QStringLiteral("nonce")).toString();
    const QString evt = payload.value(QStringLiteral("evt")).toString();
    const QJsonObject data = payload.value(QStringLiteral("data")).toObject();

    if (evt == QStringLiteral("ERROR")) {
        const QString cmd = m_pending.take(nonce);
        if (nonce == m_authPendingNonce) {
            m_authPendingNonce.clear();
            m_authDeadlineTick = 0;
        }
        if (cmd == QStringLiteral("GET_GUILD")) {
            return;
        }
        if (cmd == QStringLiteral("AUTHENTICATE")) {
            clearSavedToken();
            emitJson(QStringLiteral("auth_required"));
        } else {
            emitJson(QStringLiteral("error"), QJsonObject{
                {QStringLiteral("message"), data.value(QStringLiteral("message")).toString(QStringLiteral("Discord RPC error"))}
            });
        }
        return;
    }

    if (!nonce.isEmpty() && m_pending.contains(nonce)) {
        const QString cmd = m_pending.take(nonce);
        if (nonce == m_authPendingNonce) {
            m_authPendingNonce.clear();
            m_authDeadlineTick = 0;
        }
        handleRpcResponse(cmd, data);
        return;
    }

    if (payload.value(QStringLiteral("cmd")).toString() == QStringLiteral("DISPATCH")) {
        handleRpcDispatch(evt, data);
    }
}

void DiscordVoiceBridge::handleIncomingCommand(const QJsonObject& msg) {
    const QString cmd = msg.value(QStringLiteral("cmd")).toString();
    if (cmd == QStringLiteral("connect")) {
        connectPipe();
    } else if (cmd == QStringLiteral("authorize")) {
        startAuthorize();
    } else if (cmd == QStringLiteral("set_voice_settings")) {
        QJsonObject args;
        if (msg.contains(QStringLiteral("mute"))) {
            args.insert(QStringLiteral("mute"), msg.value(QStringLiteral("mute")).toBool());
        }
        if (msg.contains(QStringLiteral("deaf"))) {
            args.insert(QStringLiteral("deaf"), msg.value(QStringLiteral("deaf")).toBool());
        }
        if (m_pipe != INVALID_HANDLE_VALUE && !args.isEmpty()) {
            sendRpcCommand(QStringLiteral("SET_VOICE_SETTINGS"), args, true);
        }
    } else if (cmd == QStringLiteral("disconnect")) {
        closePipe();
        m_users.clear();
        m_channel = QJsonObject();
        m_hasChannel = false;
    }
}

void DiscordVoiceBridge::workerMain() {
    emitJson(QStringLiteral("ready"));

    OVERLAPPED readOv = {};
    readOv.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    QByteArray readBuf;
    char chunk[4096];
    bool readPending = false;

    auto drainCommands = [&]() {
        QQueue<QJsonObject> local;
        {
            std::lock_guard<std::mutex> lock(m_cmdMutex);
            local.swap(m_cmdQueue);
        }
        if (m_wakeEvent) {
            ResetEvent(m_wakeEvent);
        }
        while (!local.isEmpty() && !m_stop.load()) {
            handleIncomingCommand(local.dequeue());
        }
    };

    auto processReadBuffer = [&]() -> bool {
        while (readBuf.size() >= 8) {
            uint32_t opcode = 0;
            uint32_t length = 0;
            std::memcpy(&opcode, readBuf.constData(), 4);
            std::memcpy(&length, readBuf.constData() + 4, 4);
            if (length > kMaxFrameBytes) {
                return false;
            }
            if (readBuf.size() < static_cast<qsizetype>(8 + length)) {
                break;
            }
            const QByteArray body = readBuf.mid(8, static_cast<qsizetype>(length));
            readBuf.remove(0, static_cast<qsizetype>(8 + length));

            if (opcode == kOpPing) {
                writePipeFrame(kOpPong, body);
            } else if (opcode == kOpClose) {
                return false;
            } else if (opcode == kOpFrame) {
                const QJsonDocument doc = QJsonDocument::fromJson(body);
                if (doc.isObject()) {
                    handleRpcFrame(doc.object());
                }
            }
        }
        return true;
    };

    while (!m_stop.load()) {
        drainCommands();
        if (m_stop.load()) {
            break;
        }

        // Check authorization timeout if waiting for user prompt in Discord
        if (!m_authPendingNonce.isEmpty() && m_authDeadlineTick > 0 && GetTickCount64() >= m_authDeadlineTick) {
            const QString failedPath = m_authPendingPath;
            m_pending.remove(m_authPendingNonce);
            m_authPendingNonce.clear();
            m_authPendingPath.clear();
            m_authDeadlineTick = 0;
            if (!failedPath.isEmpty()) {
                m_authFailedPaths.insert(failedPath);
            }
            if (readPending && m_pipe != INVALID_HANDLE_VALUE) {
                CancelIoEx(m_pipe, &readOv);
                readPending = false;
            }
            readBuf.clear();
            closePipe();
            if (connectPipe()) {
                startAuthorize();
            } else {
                m_authFailedPaths.clear();
                emitJson(QStringLiteral("error"), QJsonObject{
                    {QStringLiteral("message"), QStringLiteral("Discord authorization timed out; click Link Discord to retry.")}
                });
            }
            continue;
        }

        if (m_pipe == INVALID_HANDLE_VALUE) {
            readPending = false;
            readBuf.clear();
            WaitForSingleObject(m_wakeEvent, 250);
            continue;
        }

        if (!readPending) {
            ResetEvent(readOv.hEvent);
            DWORD got = 0;
            BOOL r = ReadFile(m_pipe, chunk, sizeof(chunk), &got, &readOv);
            if (r) {
                if (got == 0) {
                    closePipe();
                    m_users.clear();
                    m_channel = QJsonObject();
                    m_hasChannel = false;
                    readBuf.clear();
                    emitJson(QStringLiteral("disconnected"));
                    continue;
                }
                readBuf.append(chunk, static_cast<qsizetype>(got));
                if (!processReadBuffer()) {
                    closePipe();
                    m_users.clear();
                    m_channel = QJsonObject();
                    m_hasChannel = false;
                    readBuf.clear();
                    emitJson(QStringLiteral("disconnected"));
                }
                continue;
            }
            const DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                readPending = true;
            } else {
                closePipe();
                m_users.clear();
                m_channel = QJsonObject();
                m_hasChannel = false;
                readBuf.clear();
                emitJson(QStringLiteral("disconnected"));
                continue;
            }
        }

        HANDLE handles[2] = { readOv.hEvent, m_wakeEvent };
        const DWORD waitMs = m_authPendingNonce.isEmpty() ? 500 : 250;
        const DWORD waitRes = WaitForMultipleObjects(2, handles, FALSE, waitMs);
        if (waitRes == WAIT_OBJECT_0) {
            readPending = false;
            DWORD got = 0;
            if (!GetOverlappedResult(m_pipe, &readOv, &got, FALSE) || got == 0) {
                closePipe();
                m_users.clear();
                m_channel = QJsonObject();
                m_hasChannel = false;
                readBuf.clear();
                emitJson(QStringLiteral("disconnected"));
                continue;
            }
            readBuf.append(chunk, static_cast<qsizetype>(got));
            if (!processReadBuffer()) {
                closePipe();
                m_users.clear();
                m_channel = QJsonObject();
                m_hasChannel = false;
                readBuf.clear();
                emitJson(QStringLiteral("disconnected"));
            }
        } else if (waitRes == WAIT_OBJECT_0 + 1) {
            drainCommands();
            if (m_pipe == INVALID_HANDLE_VALUE) {
                readPending = false;
                readBuf.clear();
            }
        }
    }

    if (readPending && m_pipe != INVALID_HANDLE_VALUE) {
        CancelIoEx(m_pipe, &readOv);
    }
    if (readOv.hEvent) {
        CloseHandle(readOv.hEvent);
    }
    closePipe();
}

} // namespace SekaiPop::Services
