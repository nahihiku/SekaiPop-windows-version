#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QVector>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace SekaiPop::Services {

struct MediaSessionInfo {
    QString aumid;
    QString identity = QStringLiteral("Media");
    QString desktopEntry = QStringLiteral("media");
    bool present = false;
    bool playing = false;
    int playbackStatus = 0; // SMTC PlaybackStatus: 3=Stopped, 4=Playing, 5=Paused
    QString title;
    QString artist;
    QString album;
    QString artUrl;
    bool isAppIcon = false;
    double basePositionSec = 0.0;
    double durationSec = 0.0;
    qint64 lastUpdatedEpochMs = 0;
    qint64 timelineOrderTicks = 0;

    double currentPositionSec() const;
};

/**
 * Windows System Media Transport Controls (SMTC) backend replacing Linux MPRIS.
 * Ported from Ukikura's battle-tested SMTC engine:
 *   - Multi-session enumeration (Playing > Paused > latest LastUpdatedTime)
 *   - Real SMTC Timeline (EndTime, Position, LastUpdatedTime + live millisecond extrapolation)
 *   - Fast thumbnail read via CreateStreamOverRandomAccessStream (with BufferFactory fallback)
 *   - App-icon detection (17372 bytes) + Ukikura high-res cover art fallback
 *   - Robust per-session command dispatch (TryPlay/TryPause/TryToggle/TrySeek/TrySkip + YouTube/media-key fallbacks)
 */
class MediaSource final : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool present READ present NOTIFY changed)
    Q_PROPERTY(bool playing READ playing NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString artist READ artist NOTIFY changed)
    Q_PROPERTY(QString album READ album NOTIFY changed)
    Q_PROPERTY(QString artUrl READ artUrl NOTIFY changed)
    Q_PROPERTY(QString identity READ identity NOTIFY changed)
    Q_PROPERTY(double position READ position NOTIFY timelineUpdated)
    Q_PROPERTY(double duration READ duration NOTIFY changed)
    Q_PROPERTY(bool volumeAvailable READ volumeAvailable CONSTANT)

public:
    explicit MediaSource(QObject* parent = nullptr);
    ~MediaSource() override;

    bool present() const { return m_present; }
    bool playing() const { return m_playing; }
    QString title() const { return m_title; }
    QString artist() const { return m_artist; }
    QString album() const { return m_album; }
    QString artUrl() const { return m_artUrl; }
    QString identity() const { return m_identity; }
    double position() const;
    double duration() const { return m_duration; }
    bool volumeAvailable() const { return true; }

    QVector<MediaSessionInfo> sessions() const { return m_sessions; }

    Q_INVOKABLE void togglePlayPause(const QString& aumid = QString());
    Q_INVOKABLE void play(const QString& aumid = QString());
    Q_INVOKABLE void pause(const QString& aumid = QString());
    Q_INVOKABLE void stop(const QString& aumid = QString());
    Q_INVOKABLE void next(const QString& aumid = QString());
    Q_INVOKABLE void previous(const QString& aumid = QString());
    Q_INVOKABLE void seek(double seconds, const QString& aumid = QString());
    Q_INVOKABLE void raisePlayer(const QString& aumid = QString(), const QString& trackTitle = QString());

    /** Stores a high-res fallback cover URL (e.g. from Ukikura lyrics engine) for a track. */
    void setFallbackCover(const QString& title, const QString& artist, const QString& artUrl);

    /** Saves raw image bytes to SekaiPop's cover-art cache and returns its file:/// URL. */
    static QString saveCoverBytesToCache(const QByteArray& bytes, const QString& prefix = QStringLiteral("smtc"));

Q_SIGNALS:
    void changed();
    void sessionsChanged();
    void timelineUpdated();

private:
    enum class CommandType {
        TogglePlayPause,
        Play,
        Pause,
        Stop,
        Next,
        Previous,
        Seek
    };

    struct PendingCommand {
        CommandType type = CommandType::TogglePlayPause;
        QString aumid;
        double seekSec = 0.0;
    };

    void workerLoop();
    void enqueueCommand(const PendingCommand& cmd);
    QString lookupFallbackCover(const QString& title, const QString& artist) const;

    std::atomic<bool> m_stop{ false };
    std::mutex m_cmdMutex;
    std::condition_variable m_cmdCv;
    std::deque<PendingCommand> m_cmdQueue;
    std::atomic<bool> m_forceRefresh{ false };

    mutable std::mutex m_fallbackMutex;
    QHash<QString, QString> m_fallbackCovers;

    std::thread m_worker;

    QVector<MediaSessionInfo> m_sessions;
    bool m_present = false;
    bool m_playing = false;
    QString m_title;
    QString m_artist;
    QString m_album;
    QString m_artUrl;
    QString m_identity = QStringLiteral("Media");
    double m_basePosition = 0.0;
    double m_duration = 0.0;
    qint64 m_lastUpdatedEpochMs = 0;
};

} // namespace SekaiPop::Services
