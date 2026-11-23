#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QString>
#include <atomic>
#include <mutex>
#include <thread>
#include <windows.h>

namespace SekaiPop::Services {

/**
 * Native Windows Discord Voice RPC bridge over \\.\pipe\discord-ipc-0..9.
 * Implements the exact line-delimited JSON protocol expected by SekaiPop's
 * DiscordVoiceService.qml with zero Python or subprocess dependency:
 *   - Discovers Discord Desktop RPC named pipes (0..9)
 *   - Performs StreamKit OAuth2 authorization + WinHTTP token exchange
 *   - Tracks live voice channel, guild icon, participants, speaking rings,
 *     and mute/deafen voice settings in real time.
 */
class DiscordVoiceBridge final : public QObject {
    Q_OBJECT

public:
    explicit DiscordVoiceBridge(QObject* parent = nullptr);
    ~DiscordVoiceBridge() override;

    void start();
    void stop();
    void sendCommand(const QString& jsonLine);

Q_SIGNALS:
    void lineReady(const QString& jsonLine);

private:
    struct GuildInfo {
        QString name;
        QString iconUrl;
    };

    void workerMain();
    bool connectPipe();
    void closePipe();
    bool writePipeFrame(uint32_t opcode, const QByteArray& body);
    bool readPipeExact(void* dst, size_t len, DWORD timeoutMs);
    bool readPipeFrame(uint32_t& outOpcode, QJsonObject& outPayload, DWORD timeoutMs);

    void handleIncomingCommand(const QJsonObject& msg);
    void sendRpcCommand(const QString& cmd, const QJsonObject& args = QJsonObject(), bool hasArgs = false);
    void sendRpcSubscribe(const QString& evt, const QString& channelId = QString());
    void startAuthorize();
    void sendAuthenticate(const QString& accessToken);

    void handleRpcFrame(const QJsonObject& payload);
    void handleRpcResponse(const QString& cmd, const QJsonObject& data);
    void handleRpcDispatch(const QString& evt, const QJsonObject& data);
    void selectChannel(const QJsonObject& data, bool hasChannel);

    QString loadSavedToken() const;
    void saveToken(const QString& accessToken) const;
    void clearSavedToken() const;
    QString exchangeAuthCodeWinHttp(const QString& code) const;

    void emitJson(const QString& type, const QJsonObject& extra = QJsonObject());
    QJsonArray currentUsersArray() const;
    static QJsonObject parseVoiceUser(const QJsonObject& data);

    HANDLE m_pipe = INVALID_HANDLE_VALUE;
    HANDLE m_wakeEvent = nullptr;
    std::atomic<bool> m_stop{false};
    std::thread m_worker;

    std::mutex m_cmdMutex;
    QQueue<QJsonObject> m_cmdQueue;

    QString m_tokenPath;
    QString m_currentPipePath;
    QSet<QString> m_authFailedPaths;
    int m_nonce = 0;
    QHash<QString, QString> m_pending;
    QHash<QString, QJsonObject> m_users;
    QJsonObject m_channel;
    bool m_hasChannel = false;
    QHash<QString, GuildInfo> m_guilds;
    bool m_muted = false;
    bool m_deafened = false;

    QString m_authPendingNonce;
    QString m_authPendingPath;
    ULONGLONG m_authDeadlineTick = 0;
};

} // namespace SekaiPop::Services
