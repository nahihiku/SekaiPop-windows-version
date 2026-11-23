#pragma once

#include "QsTypes.h"
#include "../services/MediaSource.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QQmlAbstractUrlInterceptor>
#include <QQmlListProperty>
#include <QQuickWindow>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

namespace SekaiPop::Services {
class AppSource;
class BatterySource;
class MediaSource;
class NotificationSource;
class QuickSource;
}

namespace SekaiPop::Win32 {
class WindowTracker;
}

namespace SekaiPop::Quickshell {

// ============================================================================
// URL Interceptor for Windows file:// paths & baked shaders
// ============================================================================

class QsUrlInterceptor : public QQmlAbstractUrlInterceptor {
public:
    explicit QsUrlInterceptor(const QString& shadersDir) : m_shadersDir(shadersDir) {}
    QUrl intercept(const QUrl& url, DataType type) override;

private:
    QString m_shadersDir;
};

// ============================================================================
// DesktopEntries & QsDesktopEntry
// ============================================================================

class QsDesktopEntry : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString id READ id CONSTANT)
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(QString genericName READ genericName CONSTANT)
    Q_PROPERTY(QString icon READ icon CONSTANT)
    Q_PROPERTY(QString comment READ comment CONSTANT)
    Q_PROPERTY(QStringList keywords READ keywords CONSTANT)
    Q_PROPERTY(bool noDisplay READ noDisplay CONSTANT)
    Q_PROPERTY(QString execString READ execString CONSTANT)

public:
    QsDesktopEntry(
        const QString& id,
        const QString& name,
        const QString& genericName,
        const QString& icon,
        const QString& comment,
        const QStringList& keywords,
        const QVariantMap& appMap,
        Services::AppSource* appSource,
        QObject* parent = nullptr
    );

    QString id() const { return m_id; }
    QString name() const { return m_name; }
    QString genericName() const { return m_genericName; }
    QString icon() const { return m_icon; }
    QString comment() const { return m_comment; }
    QStringList keywords() const { return m_keywords; }
    bool noDisplay() const { return false; }
    QString execString() const { return m_id; }

    Q_INVOKABLE void execute();

private:
    QString m_id;
    QString m_name;
    QString m_genericName;
    QString m_icon;
    QString m_comment;
    QStringList m_keywords;
    QVariantMap m_appMap;
    Services::AppSource* m_appSource = nullptr;
};

class QsDesktopEntries : public QObject {
    Q_OBJECT
    Q_PROPERTY(QsObjectModel* applications READ applications NOTIFY applicationsChanged)

public:
    explicit QsDesktopEntries(Services::AppSource* appSource, Win32::WindowTracker* tracker = nullptr, QObject* parent = nullptr);

    QsObjectModel* applications() const { return m_applications; }
    Q_INVOKABLE QObject* byId(const QString& id) const;
    Q_INVOKABLE QObject* heuristicLookup(const QString& query) const;

Q_SIGNALS:
    void applicationsChanged();

private:
    void rebuild();

    Services::AppSource* m_appSource = nullptr;
    Win32::WindowTracker* m_windowTracker = nullptr;
    QList<QsDesktopEntry*> m_entryObjs;
    mutable QHash<QString, QsDesktopEntry*> m_dynamicRunningEntries;
    QsObjectModel* m_applications = nullptr;
};

// ============================================================================
// Hyprland & Wayland Toplevels
// ============================================================================

class QsHyprlandMonitor;

class QsHyprlandWorkspace : public QObject {
    Q_OBJECT
    Q_PROPERTY(int id READ id NOTIFY changed)
    Q_PROPERTY(QString name READ name NOTIFY changed)
    Q_PROPERTY(QObject* monitor READ monitor NOTIFY changed)
    Q_PROPERTY(QsObjectModel* toplevels READ toplevels CONSTANT)
    Q_PROPERTY(QVariantMap lastIpcObject READ lastIpcObject NOTIFY changed)

public:
    QsHyprlandWorkspace(int id, QsHyprlandMonitor* mon, QObject* parent = nullptr, const QString& customName = QString());

    int id() const { return m_id; }
    void setId(int id);
    QString name() const { return m_customName.isEmpty() ? QString::number(m_id) : m_customName; }
    QObject* monitor() const;
    QsObjectModel* toplevels() const { return m_toplevels; }
    QVariantMap lastIpcObject() const { return m_ipc; }
    void setWindowCount(int count, bool hasFullscreen);

Q_SIGNALS:
    void changed();

private:
    int m_id = 1;
    QString m_customName;
    QsHyprlandMonitor* m_monitor = nullptr;
    QsObjectModel* m_toplevels = nullptr;
    QVariantMap m_ipc;
};

class QsHyprlandMonitor : public QObject {
    Q_OBJECT
    Q_PROPERTY(int id READ id CONSTANT)
    Q_PROPERTY(QString name READ name NOTIFY changed)
    Q_PROPERTY(QString description READ description NOTIFY changed)
    Q_PROPERTY(int width READ width NOTIFY changed)
    Q_PROPERTY(int height READ height NOTIFY changed)
    Q_PROPERTY(int x READ x NOTIFY changed)
    Q_PROPERTY(int y READ y NOTIFY changed)
    Q_PROPERTY(qreal scale READ scale NOTIFY changed)
    Q_PROPERTY(bool focused READ focused NOTIFY changed)
    Q_PROPERTY(QObject* activeWorkspace READ activeWorkspace NOTIFY changed)
    Q_PROPERTY(QVariantMap lastIpcObject READ lastIpcObject NOTIFY changed)

public:
    QsHyprlandMonitor(int id, QsScreen* screen, QObject* parent = nullptr);

    int id() const { return m_id; }
    QString name() const { return m_screen ? m_screen->name() : QStringLiteral("Primary"); }
    QString description() const { return m_screen ? m_screen->model() : QStringLiteral("Monitor"); }
    int width() const { return m_screen ? m_screen->width() : 1920; }
    int height() const { return m_screen ? m_screen->height() : 1080; }
    int x() const { return m_screen ? m_screen->x() : 0; }
    int y() const { return m_screen ? m_screen->y() : 0; }
    qreal scale() const { return 1.0; }
    bool focused() const { return true; }
    QsScreen* qsScreen() const { return m_screen; }

    QObject* activeWorkspace() const { return m_activeWorkspace; }
    QsHyprlandWorkspace* monitorWorkspace() const { return m_monitorWorkspace; }
    void setActiveWorkspace(QsHyprlandWorkspace* ws);
    QVariantMap lastIpcObject() const;

Q_SIGNALS:
    void changed();

private:
    int m_id = 0;
    QPointer<QsScreen> m_screen;
    QPointer<QsHyprlandWorkspace> m_activeWorkspace;
    QsHyprlandWorkspace* m_monitorWorkspace = nullptr;
};

class QsHyprlandToplevel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString address READ address NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(bool activated READ activated NOTIFY changed)
    Q_PROPERTY(bool maximized READ maximized NOTIFY changed)
    Q_PROPERTY(bool fullscreen READ fullscreen NOTIFY changed)
    Q_PROPERTY(bool minimized READ minimized NOTIFY changed)
    Q_PROPERTY(QObject* workspace READ workspace NOTIFY changed)
    Q_PROPERTY(QObject* monitor READ monitor NOTIFY changed)
    Q_PROPERTY(QVariantMap wayland READ wayland NOTIFY changed)
    Q_PROPERTY(QVariantMap lastIpcObject READ lastIpcObject NOTIFY changed)

public:
    QsHyprlandToplevel(
        const QString& address,
        const QString& title,
        const QString& appClass,
        bool activated,
        bool minimized,
        bool maximized,
        QsHyprlandWorkspace* ws,
        QsHyprlandMonitor* mon,
        QObject* parent = nullptr
    );

    QString address() const { return m_address; }
    QString title() const { return m_title; }
    bool activated() const { return m_activated; }
    bool maximized() const { return m_maximized; }
    bool fullscreen() const { return m_maximized; }
    bool minimized() const { return m_minimized || (m_workspace && m_workspace->id() == -99); }
    QObject* workspace() const { return m_workspace; }
    QObject* monitor() const { return m_monitor; }
    QVariantMap wayland() const { return QVariantMap{{QStringLiteral("appId"), m_class}, {QStringLiteral("title"), m_title}, {QStringLiteral("activated"), m_activated}}; }
    QVariantMap lastIpcObject() const { return m_ipc; }

Q_SIGNALS:
    void changed();

private:
    QString m_address;
    QString m_title;
    QString m_class;
    bool m_activated = false;
    bool m_minimized = false;
    bool m_maximized = false;
    QPointer<QsHyprlandWorkspace> m_workspace;
    QPointer<QsHyprlandMonitor> m_monitor;
    QVariantMap m_ipc;
};

class QsHyprland : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject* focusedMonitor READ focusedMonitor NOTIFY stateChanged)
    Q_PROPERTY(QObject* focusedWorkspace READ focusedWorkspace NOTIFY stateChanged)
    Q_PROPERTY(QObject* activeToplevel READ activeToplevel NOTIFY stateChanged)
    Q_PROPERTY(QsObjectModel* monitors READ monitors CONSTANT)
    Q_PROPERTY(QsObjectModel* workspaces READ workspaces CONSTANT)
    Q_PROPERTY(QsObjectModel* toplevels READ toplevels CONSTANT)
    Q_PROPERTY(bool usingLua READ usingLua CONSTANT)

public:
    explicit QsHyprland(Win32::WindowTracker* tracker, QObject* parent = nullptr);

    QObject* focusedMonitor() const;
    QObject* focusedWorkspace() const;
    QObject* activeToplevel() const { return m_activeToplevel; }
    QsObjectModel* monitors() const { return m_monitors; }
    QsObjectModel* workspaces() const { return m_workspaces; }
    QsObjectModel* toplevels() const { return m_toplevels; }
    bool usingLua() const { return false; }

    Q_INVOKABLE QObject* monitorFor(QObject* screenObj) const;
    Q_INVOKABLE void dispatch(const QString& cmd);
    Q_INVOKABLE void refreshMonitors() { scheduleSyncFromTracker(); }
    Q_INVOKABLE void refreshWorkspaces() { scheduleSyncFromTracker(); }
    Q_INVOKABLE void refreshToplevels() { scheduleSyncFromTracker(); }

Q_SIGNALS:
    void stateChanged();
    void rawEvent(const QVariantMap& event);

private:
    void scheduleSyncFromTracker(const QString& pendingEvent = QString());
    void syncFromTracker();

    Win32::WindowTracker* m_tracker = nullptr;
    QList<QsHyprlandMonitor*> m_monitorObjs;
    QList<QsHyprlandWorkspace*> m_workspaceObjs;
    QList<QsHyprlandToplevel*> m_toplevelObjs;
    QPointer<QsHyprlandToplevel> m_activeToplevel;
    QsObjectModel* m_monitors = nullptr;
    QsObjectModel* m_workspaces = nullptr;
    QsObjectModel* m_toplevels = nullptr;
    QTimer m_syncTimer;
    QStringList m_pendingRawEvents;
};

class QsHyprlandFocusGrab : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList windows READ windows WRITE setWindows NOTIFY windowsChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)

public:
    explicit QsHyprlandFocusGrab(QObject* parent = nullptr);

    QVariantList windows() const { return m_windows; }
    void setWindows(const QVariantList& w);

    bool active() const { return m_active; }
    void setActive(bool a);

Q_SIGNALS:
    void windowsChanged();
    void activeChanged();
    void cleared();

private:
    void checkGrab();

    QVariantList m_windows;
    bool m_active = false;
    bool m_wasPressed = false;
    QTimer m_pollTimer;
};

class QsGlobalShortcut : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString appid READ appid WRITE setAppid NOTIFY appidChanged)
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
    Q_PROPERTY(QString description READ description WRITE setDescription NOTIFY descriptionChanged)

public:
    using QObject::QObject;

    QString appid() const { return m_appid; }
    void setAppid(const QString& a) { if (m_appid != a) { m_appid = a; Q_EMIT appidChanged(); } }

    QString name() const { return m_name; }
    void setName(const QString& n) { if (m_name != n) { m_name = n; Q_EMIT nameChanged(); } }

    QString description() const { return m_desc; }
    void setDescription(const QString& d) { if (m_desc != d) { m_desc = d; Q_EMIT descriptionChanged(); } }

Q_SIGNALS:
    void appidChanged();
    void nameChanged();
    void descriptionChanged();
    void pressed();
    void released();

private:
    QString m_appid;
    QString m_name;
    QString m_desc;
};

class QsToplevelManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QsObjectModel* toplevels READ toplevels NOTIFY toplevelsChanged)
    Q_PROPERTY(QObject* activeToplevel READ activeToplevel NOTIFY toplevelsChanged)

public:
    explicit QsToplevelManager(QsHyprland* hypr, QObject* parent = nullptr);

    QsObjectModel* toplevels() const;
    QObject* activeToplevel() const;

Q_SIGNALS:
    void toplevelsChanged();

private:
    QsHyprland* m_hypr = nullptr;
};

// ============================================================================
// Mpris & MprisPlayer
// ============================================================================

class MprisPlaybackState : public QObject {
    Q_OBJECT
public:
    enum State {
        Stopped = 0,
        Playing = 1,
        Paused = 2
    };
    Q_ENUM(State)
    using QObject::QObject;
};

class QsMprisPlayer : public QObject {
    Q_OBJECT
    Q_PROPERTY(int playbackState READ playbackState NOTIFY playbackStateChanged)
    Q_PROPERTY(bool isPlaying READ isPlaying NOTIFY playbackStateChanged)
    Q_PROPERTY(QString trackTitle READ trackTitle NOTIFY changed)
    Q_PROPERTY(QString trackArtist READ trackArtist NOTIFY changed)
    Q_PROPERTY(QString trackAlbum READ trackAlbum NOTIFY changed)
    Q_PROPERTY(QString trackArtUrl READ trackArtUrl NOTIFY changed)
    Q_PROPERTY(QString identity READ identity NOTIFY changed)
    Q_PROPERTY(QString desktopEntry READ desktopEntry NOTIFY changed)
    Q_PROPERTY(QString dbusName READ dbusName NOTIFY changed)
    Q_PROPERTY(qreal position READ position WRITE setPosition NOTIFY positionChanged)
    Q_PROPERTY(qreal length READ length NOTIFY changed)
    Q_PROPERTY(bool positionSupported READ positionSupported NOTIFY changed)
    Q_PROPERTY(bool lengthSupported READ lengthSupported NOTIFY changed)
    Q_PROPERTY(bool canSeek READ canSeek NOTIFY changed)
    Q_PROPERTY(bool canRaise READ canRaise CONSTANT)
    Q_PROPERTY(qreal volume READ volume WRITE setVolume NOTIFY changed)
    Q_PROPERTY(bool volumeSupported READ volumeSupported CONSTANT)
    Q_PROPERTY(bool canControl READ canControl CONSTANT)
    Q_PROPERTY(bool canTogglePlaying READ canControl CONSTANT)
    Q_PROPERTY(bool canPlay READ canControl CONSTANT)
    Q_PROPERTY(bool canPause READ canControl CONSTANT)
    Q_PROPERTY(bool canGoNext READ canControl CONSTANT)
    Q_PROPERTY(bool canGoPrevious READ canControl CONSTANT)
    Q_PROPERTY(QVariantMap metadata READ metadata NOTIFY changed)

public:
    explicit QsMprisPlayer(
        const QString& aumid,
        Services::MediaSource* mediaSource,
        Services::QuickSource* quickSource = nullptr,
        QObject* parent = nullptr
    );

    QString aumid() const { return m_aumid; }
    void updateFromSession(const Services::MediaSessionInfo& info);

    int playbackState() const;
    bool isPlaying() const;
    QString trackTitle() const;
    QString trackArtist() const;
    QString trackAlbum() const;
    QString trackArtUrl() const;
    QString identity() const;
    QString desktopEntry() const;
    QString dbusName() const;
    qreal position() const;
    void setPosition(qreal p);
    qreal length() const;
    bool positionSupported() const;
    bool lengthSupported() const;
    bool canSeek() const;
    bool canRaise() const { return true; }
    qreal volume() const;
    void setVolume(qreal v);
    bool volumeSupported() const { return m_quick != nullptr; }
    bool canControl() const { return true; }
    QVariantMap metadata() const;

    Q_INVOKABLE void togglePlaying();
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void raise();

Q_SIGNALS:
    void changed();
    void playbackStateChanged();
    void trackChanged();
    void positionChanged();

private:
    QString m_aumid;
    Services::MediaSource* m_media = nullptr;
    Services::QuickSource* m_quick = nullptr;
    Services::MediaSessionInfo m_info;
    int m_lastPlaybackState = MprisPlaybackState::Stopped;
    qint64 m_optimisticSeekUntilMs = 0;
};

class QsMpris : public QObject {
    Q_OBJECT
    Q_PROPERTY(QsObjectModel* players READ players NOTIFY playersChanged)

public:
    explicit QsMpris(
        Services::MediaSource* mediaSource,
        Services::QuickSource* quickSource = nullptr,
        QObject* parent = nullptr
    );
    QsObjectModel* players() const { return m_players; }

Q_SIGNALS:
    void playersChanged();

private:
    void syncSessions();

    Services::MediaSource* m_media = nullptr;
    Services::QuickSource* m_quick = nullptr;
    QHash<QString, QsMprisPlayer*> m_playerMap;
    QStringList m_orderedKeys;
    QsObjectModel* m_players = nullptr;
};

// ============================================================================
// Pipewire
// ============================================================================

class QsPwAudio : public QObject {
    Q_OBJECT
    Q_PROPERTY(qreal volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)

public:
    QsPwAudio(Services::QuickSource* qs, bool isSink, QObject* parent = nullptr);

    qreal volume() const;
    void setVolume(qreal v);

    bool muted() const;
    void setMuted(bool m);

Q_SIGNALS:
    void volumeChanged();
    void mutedChanged();

private:
    Services::QuickSource* m_qs = nullptr;
    bool m_isSink = true;
    qreal m_lastVol = -1;
    bool m_lastMuted = false;
};

class QsPwNode : public QObject {
    Q_OBJECT
    Q_PROPERTY(int id READ id CONSTANT)
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(QString description READ name CONSTANT)
    Q_PROPERTY(bool ready READ ready CONSTANT)
    Q_PROPERTY(bool isSink READ isSink CONSTANT)
    Q_PROPERTY(bool isStream READ isStream CONSTANT)
    Q_PROPERTY(QsPwAudio* audio READ audio CONSTANT)
    Q_PROPERTY(QVariantMap properties READ properties CONSTANT)

public:
    QsPwNode(int id, const QString& name, bool isSink, Services::QuickSource* qs, QObject* parent = nullptr);

    int id() const { return m_id; }
    QString name() const { return m_name; }
    bool ready() const { return true; }
    bool isSink() const { return m_isSink; }
    bool isStream() const { return false; }
    QsPwAudio* audio() const { return m_audio; }
    QVariantMap properties() const { return QVariantMap{}; }

private:
    int m_id = 1;
    QString m_name;
    bool m_isSink = true;
    QsPwAudio* m_audio = nullptr;
};

class QsPipewire : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready CONSTANT)
    Q_PROPERTY(QsPwNode* defaultAudioSink READ defaultAudioSink CONSTANT)
    Q_PROPERTY(QsPwNode* defaultAudioSource READ defaultAudioSource CONSTANT)
    Q_PROPERTY(QsObjectModel* nodes READ nodes CONSTANT)
    Q_PROPERTY(QsObjectModel* links READ links CONSTANT)

public:
    explicit QsPipewire(Services::QuickSource* qs, QObject* parent = nullptr);

    bool ready() const { return true; }
    QsPwNode* defaultAudioSink() const { return m_sink; }
    QsPwNode* defaultAudioSource() const { return m_source; }
    QsObjectModel* nodes() const { return m_nodes; }
    QsObjectModel* links() const { return m_links; }

private:
    QsPwNode* m_sink = nullptr;
    QsPwNode* m_source = nullptr;
    QsObjectModel* m_nodes = nullptr;
    QsObjectModel* m_links = nullptr;
};

class QsPwObjectTracker : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList objects READ objects WRITE setObjects NOTIFY objectsChanged)

public:
    using QObject::QObject;
    QVariantList objects() const { return m_objects; }
    void setObjects(const QVariantList& o) {
        if (m_objects != o) {
            m_objects = o;
            Q_EMIT objectsChanged();
        }
    }

Q_SIGNALS:
    void objectsChanged();

private:
    QVariantList m_objects;
};

// ============================================================================
// UPower
// ============================================================================

class UPowerDeviceState : public QObject {
    Q_OBJECT
public:
    enum State {
        Unknown = 0,
        Charging = 1,
        Discharging = 2,
        Empty = 3,
        FullyCharged = 4,
        PendingCharge = 5,
        PendingDischarge = 6
    };
    Q_ENUM(State)
    using QObject::QObject;
};

class QsUPowerDevice : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready CONSTANT)
    Q_PROPERTY(bool isLaptopBattery READ isLaptopBattery NOTIFY changed)
    Q_PROPERTY(qreal percentage READ percentage NOTIFY changed)
    Q_PROPERTY(int state READ state NOTIFY changed)
    Q_PROPERTY(qreal timeToEmpty READ timeToEmpty NOTIFY changed)
    Q_PROPERTY(qreal timeToFull READ timeToFull NOTIFY changed)
    Q_PROPERTY(qreal changeRate READ changeRate NOTIFY changed)
    Q_PROPERTY(qreal energyRate READ changeRate NOTIFY changed)
    Q_PROPERTY(qreal energy READ energy NOTIFY changed)
    Q_PROPERTY(qreal energyCapacity READ energyCapacity NOTIFY changed)

public:
    explicit QsUPowerDevice(Services::BatterySource* bat, QObject* parent = nullptr);

    bool ready() const { return true; }
    bool isLaptopBattery() const;
    qreal percentage() const;
    int state() const;
    qreal timeToEmpty() const { return 0; }
    qreal timeToFull() const { return 0; }
    qreal changeRate() const { return 0; }
    qreal energy() const { return percentage() * 50.0; }
    qreal energyCapacity() const { return 50.0; }

Q_SIGNALS:
    void changed();

private:
    Services::BatterySource* m_bat = nullptr;
};

class QsUPower : public QObject {
    Q_OBJECT
    Q_PROPERTY(QsUPowerDevice* displayDevice READ displayDevice CONSTANT)
    Q_PROPERTY(bool onBattery READ onBattery NOTIFY changed)
    Q_PROPERTY(QsObjectModel* devices READ devices CONSTANT)

public:
    explicit QsUPower(Services::BatterySource* bat, QObject* parent = nullptr);

    QsUPowerDevice* displayDevice() const { return m_device; }
    bool onBattery() const;
    QsObjectModel* devices() const { return m_devices; }

Q_SIGNALS:
    void changed();

private:
    QsUPowerDevice* m_device = nullptr;
    QsObjectModel* m_devices = nullptr;
    Services::BatterySource* m_bat = nullptr;
};

class PowerProfile : public QObject {
    Q_OBJECT
public:
    enum Profile {
        PowerSaver = 0,
        Balanced = 1,
        Performance = 2
    };
    Q_ENUM(Profile)
    using QObject::QObject;
};

class QsPowerProfiles : public QObject {
    Q_OBJECT
    Q_PROPERTY(int profile READ profile WRITE setProfile NOTIFY profileChanged)
    Q_PROPERTY(bool hasPerformanceProfile READ hasPerformanceProfile CONSTANT)

public:
    using QObject::QObject;
    int profile() const { return m_profile; }
    void setProfile(int p) {
        if (m_profile != p) {
            m_profile = p;
            Q_EMIT profileChanged();
        }
    }
    bool hasPerformanceProfile() const { return true; }

Q_SIGNALS:
    void profileChanged();

private:
    int m_profile = PowerProfile::Balanced;
};

// ============================================================================
// Notifications, SystemTray, Polkit, Pam
// ============================================================================

class QsNotificationServer : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool bodySupported READ flagTrue WRITE setDummy)
    Q_PROPERTY(bool bodyMarkupSupported READ flagTrue WRITE setDummy)
    Q_PROPERTY(bool bodyHyperlinksSupported READ flagTrue WRITE setDummy)
    Q_PROPERTY(bool bodyImagesSupported READ flagTrue WRITE setDummy)
    Q_PROPERTY(bool actionsSupported READ flagTrue WRITE setDummy)
    Q_PROPERTY(bool actionIconsSupported READ flagTrue WRITE setDummy)
    Q_PROPERTY(bool imageSupported READ flagTrue WRITE setDummy)
    Q_PROPERTY(bool persistenceSupported READ flagTrue WRITE setDummy)
    Q_PROPERTY(bool inlineReplySupported READ flagTrue WRITE setDummy)
    Q_PROPERTY(bool keepOnReload READ flagTrue WRITE setDummy)
    Q_PROPERTY(QsObjectModel* trackedNotifications READ trackedNotifications CONSTANT)

public:
    explicit QsNotificationServer(QObject* parent = nullptr)
        : QObject(parent), m_tracked(new QsObjectModel(this)) {}
    bool flagTrue() const { return true; }
    void setDummy(bool) {}
    QsObjectModel* trackedNotifications() const { return m_tracked; }

Q_SIGNALS:
    void notification(QObject* notif);

private:
    QsObjectModel* m_tracked = nullptr;
};

class QsSystemTray : public QObject {
    Q_OBJECT
    Q_PROPERTY(QsObjectModel* items READ items CONSTANT)

public:
    explicit QsSystemTray(QObject* parent = nullptr)
        : QObject(parent), m_items(new QsObjectModel(this)) {}
    QsObjectModel* items() const { return m_items; }

private:
    QsObjectModel* m_items = nullptr;
};

class QsSystemTrayStatus : public QObject {
    Q_OBJECT
public:
    enum StatusValue {
        Passive = 0,
        Active = 1,
        NeedsAttention = 2
    };
    Q_ENUM(StatusValue)
    using QObject::QObject;
};

class QsPolkitAgent : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool isRegistered READ isRegistered CONSTANT)
    Q_PROPERTY(bool isActive READ isActive CONSTANT)
    Q_PROPERTY(QObject* flow READ flow CONSTANT)

public:
    using QObject::QObject;
    bool isRegistered() const { return false; }
    bool isActive() const { return false; }
    QObject* flow() const { return nullptr; }

Q_SIGNALS:
    void authenticationRequestStarted();
};

class QsPamContext : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString config READ config WRITE setConfig NOTIFY configChanged)
    Q_PROPERTY(QString configDirectory READ configDir WRITE setConfigDir NOTIFY configDirChanged)
    Q_PROPERTY(QString user READ user WRITE setUser NOTIFY userChanged)
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(bool responseRequired READ responseRequired NOTIFY responseRequiredChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(bool messageIsError READ messageIsError NOTIFY messageChanged)

public:
    using QObject::QObject;

    QString config() const { return m_config; }
    void setConfig(const QString& c) { if (m_config != c) { m_config = c; Q_EMIT configChanged(); } }

    QString configDir() const { return m_configDir; }
    void setConfigDir(const QString& d) { if (m_configDir != d) { m_configDir = d; Q_EMIT configDirChanged(); } }

    QString user() const { return m_user; }
    void setUser(const QString& u) { if (m_user != u) { m_user = u; Q_EMIT userChanged(); } }

    bool active() const { return false; }
    bool responseRequired() const { return false; }
    QString message() const { return QString(); }
    bool messageIsError() const { return false; }

    Q_INVOKABLE bool start() { return false; }
    Q_INVOKABLE void abort() {}
    Q_INVOKABLE void respond(const QString&) { Q_EMIT completed(0); }

Q_SIGNALS:
    void configChanged();
    void configDirChanged();
    void userChanged();
    void activeChanged();
    void responseRequiredChanged();
    void messageChanged();
    void pamMessage();
    void completed(int result);
    void error(int err);

private:
    QString m_config;
    QString m_configDir;
    QString m_user;
};

class QsPamResult : public QObject {
    Q_OBJECT
public:
    enum Result {
        Success = 0,
        Error = 1,
        MaxTries = 2,
        Failed = 3
    };
    Q_ENUM(Result)
    using QObject::QObject;
};

} // namespace SekaiPop::Quickshell
