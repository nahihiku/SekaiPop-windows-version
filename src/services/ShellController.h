#pragma once

#include <QObject>
#include <QString>
#include <QWindow>

namespace SekaiPop::Win32 {
class AppBarManager;
class HotkeyManager;
class WallpaperHost;
class WindowTracker;
}

namespace SekaiPop::Services {

class BarService;
class SettingsService;

/**
 * Top-level shell coordinator bridging QML windows (Wallpaper, Bar, Launcher)
 * with Win32 windowing services and emergency Windows Explorer recovery.
 */
class ShellController final : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool launcherOpen READ launcherOpen WRITE setLauncherOpen NOTIFY launcherOpenChanged)
    Q_PROPERTY(bool phoneOpen READ phoneOpen NOTIFY phoneChanged)
    Q_PROPERTY(QString phoneScreen READ phoneScreen NOTIFY phoneChanged)
    Q_PROPERTY(bool quickPowerOpen READ quickPowerOpen WRITE setQuickPowerOpen NOTIFY phoneChanged)
    Q_PROPERTY(bool quickEditOpen READ quickEditOpen WRITE setQuickEditOpen NOTIFY phoneChanged)
    Q_PROPERTY(QString activePopup READ activePopup NOTIFY activePopupChanged)
    Q_PROPERTY(bool editModeOpen READ editModeOpen NOTIFY editModeChanged)
    Q_PROPERTY(bool explorerRunning READ explorerRunning NOTIFY explorerRunningChanged)
    Q_PROPERTY(bool windowsTaskbarVisible READ windowsTaskbarVisible NOTIFY explorerRunningChanged)

public:
    ShellController(
        Win32::WallpaperHost* wallpaperHost,
        Win32::AppBarManager* appBarManager,
        Win32::WindowTracker* windowTracker,
        Win32::HotkeyManager* hotkeyManager,
        BarService* barService,
        SettingsService* settingsService,
        QObject* parent = nullptr
    );
    ~ShellController() override;

    bool launcherOpen() const { return m_launcherOpen; }
    void setLauncherOpen(bool open);
    bool phoneOpen() const { return m_phoneOpen; }
    QString phoneScreen() const { return m_phoneScreen; }
    bool quickPowerOpen() const { return m_quickPowerOpen; }
    void setQuickPowerOpen(bool open);
    bool quickEditOpen() const { return m_quickEditOpen; }
    void setQuickEditOpen(bool open);
    QString activePopup() const { return m_activePopup; }
    bool editModeOpen() const { return m_editModeOpen; }
    bool explorerRunning() const;
    bool windowsTaskbarVisible() const;

    Q_INVOKABLE void toggleLauncher();
    Q_INVOKABLE void openLauncher();
    Q_INVOKABLE void closeLauncher();

    Q_INVOKABLE void togglePhone(const QString& screen = QStringLiteral("quick"));
    Q_INVOKABLE void openPhone(const QString& screen = QStringLiteral("quick"));
    Q_INVOKABLE void closePhone();
    Q_INVOKABLE void toggleQuickPanel();
    Q_INVOKABLE void openQuickPanel();
    Q_INVOKABLE void openSettings();
    Q_INVOKABLE void openWallpaperSelector();
    Q_INVOKABLE void toggleEditMode();
    Q_INVOKABLE void openEditMode();
    Q_INVOKABLE void closeEditMode();
    Q_INVOKABLE void setWindowsTaskbarVisible(bool visible);
    Q_INVOKABLE void setActivePopup(const QString& name);

    Q_INVOKABLE void registerWallpaperWindow(QWindow* window);
    Q_INVOKABLE void registerWidgetWindow(QWindow* window, bool clickThrough = false, bool focusable = false);
    Q_INVOKABLE void repinDesktopLayer();
    Q_INVOKABLE void registerBarWindow(QWindow* window, const QString& position, int stripSizePx, bool autoHide);
    Q_INVOKABLE void configureDockWindow(QWindow* window);
    Q_INVOKABLE void configureLauncherWindow(QWindow* window);
    Q_INVOKABLE void activateLauncherWindow(QWindow* window);
    Q_INVOKABLE void configurePopupWindow(QWindow* window);
    Q_INVOKABLE void configureOsdWindow(QWindow* window);

    /**
     * Emergency recovery: unregisters AppBars, restores original Windows workareas,
     * and launches C:\Windows\explorer.exe.
     */
    Q_INVOKABLE void restoreExplorer();

    /** Cleanly restores desktop workareas and exits SekaiPopShell.exe. */
    Q_INVOKABLE void exitShell();

Q_SIGNALS:
    void launcherOpenChanged();
    void phoneChanged();
    void activePopupChanged();
    void editModeChanged();
    void explorerRunningChanged();

private:
    void applyWindowsTaskbarSetting(bool reapplyBars);

    Win32::WallpaperHost* m_wallpaperHost = nullptr;
    Win32::AppBarManager* m_appBarManager = nullptr;
    Win32::WindowTracker* m_windowTracker = nullptr;
    Win32::HotkeyManager* m_hotkeyManager = nullptr;
    BarService* m_barService = nullptr;
    SettingsService* m_settings = nullptr;
    bool m_launcherOpen = false;
    bool m_phoneOpen = false;
    bool m_quickPowerOpen = false;
    bool m_quickEditOpen = false;
    bool m_editModeOpen = false;
    bool m_restoring = false;
    QString m_phoneScreen = QStringLiteral("quick");
    QString m_activePopup;
};

} // namespace SekaiPop::Services
