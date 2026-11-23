#pragma once

#include <QQmlApplicationEngine>
#include <QString>

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

/**
 * Stages the original sekaipop/ QML tree into <exeDir>/qs_stage/qs/,
 * registers all Quickshell.* QML types & singletons, instantiates all
 * sekaipop singletons in two phases (beginCreate -> setContextProperty -> completeCreate),
 * and returns the local QUrl to <exeDir>/qs_stage/qs/shell.qml.
 */
QUrl setupQuickshellEngine(QQmlApplicationEngine& engine, const QsRuntimeDeps& deps);

} // namespace SekaiPop::Quickshell
