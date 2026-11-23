#pragma once

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QQmlApplicationEngine>
#include <QString>
#include <QUrl>

namespace SekaiPop::Services {
class AppSource;
class AudioSpectrumSource;
class BatterySource;
class MediaSource;
class NotificationSource;
class QuickSource;
class StatusIconsService;
}

namespace SekaiPop::Win32 {
class AppBarManager;
class WallpaperHost;
class WindowTracker;
}

namespace SekaiPop::Quickshell {

struct QsRuntimeDeps {
    QString baseDir;
    QString exeDir;
    Services::AppSource* appSource = nullptr;
    Services::AudioSpectrumSource* spectrumSource = nullptr;
    Services::BatterySource* batterySource = nullptr;
    Services::MediaSource* mediaSource = nullptr;
    Services::NotificationSource* notificationSource = nullptr;
    Services::QuickSource* quickSource = nullptr;
    Services::StatusIconsService* statusIconsService = nullptr;
    Win32::WallpaperHost* wallpaperHost = nullptr;
    Win32::AppBarManager* appBarManager = nullptr;
    Win32::WindowTracker* windowTracker = nullptr;
};

struct StagedSingletonEntry {
    QString name;
    QString stagedPath;
    int priority = 100;
};

// Manages non-blocking micro-batched initialization of Tier-2 singletons AFTER
// Wallpaper + Bar reach first paint so startup UI appears dramatically faster.
class QsDeferredBoot : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool tier2Ready READ tier2Ready NOTIFY tier2ReadyChanged)

public:
    QsDeferredBoot(QQmlApplicationEngine* engine, QList<StagedSingletonEntry> tier2, QObject* parent = nullptr);

    bool tier2Ready() const { return m_tier2Ready; }

    void startAsync();
    Q_INVOKABLE void ensureTier2Now();

Q_SIGNALS:
    void tier2ReadyChanged();

private:
    void initOne(const StagedSingletonEntry& info);
    void scheduleNextBatch();
    void finalizeTier2();

    QQmlApplicationEngine* m_engine = nullptr;
    QList<StagedSingletonEntry> m_tier2;
    int m_index = 0;
    bool m_started = false;
    bool m_tier2Ready = false;
    QElapsedTimer m_timer;
};

/**
 * Stages the sekaipop/ QML tree into %LOCALAPPDATA%/SekaiPop/qs_stage/qs/,
 * registers all Quickshell.* QML types & singletons, instantiates Tier-1 singletons
 * required for Wallpaper + Bar, pre-settles fast local state loads, prepares Tier-2
 * deferred singletons in *outDeferred, and returns the QUrl to staged shell.qml.
 */
QUrl setupQuickshellEngine(QQmlApplicationEngine& engine, const QsRuntimeDeps& deps, QsDeferredBoot** outDeferred = nullptr);

/**
 * Headless cache pre-warmer (--prewarm): stages qs_stage and compiles core QML components
 * without creating any windows so Qt writes fresh .qmlc bytecode cache files to disk.
 */
void prewarmQuickshellCache(QQmlApplicationEngine& engine, const QsRuntimeDeps& deps);

} // namespace SekaiPop::Quickshell
