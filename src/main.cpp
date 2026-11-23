#include <QApplication>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QIcon>
#include <QMenu>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QPixmap>
#include <QPixmapCache>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QScreen>
#include <QStandardPaths>
#include <QSurfaceFormat>
#include <QSystemTrayIcon>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <cstdio>

#include "quickshell/QsEngineSetup.h"
#include "quickshell/QsTypes.h"
#include "quickshell/QsWindowing.h"
#include "services/AppSource.h"
#include "services/AudioSpectrumSource.h"
#include "services/BatterySource.h"
#include "services/MediaSource.h"
#include "services/QuickSource.h"
#include "services/SettingsService.h"
#include "win32/AppBarManager.h"
#include "win32/HotkeyManager.h"
#include "win32/ScreenshotOverlay.h"
#include "win32/WallpaperHost.h"
#include "win32/Win32Utils.h"
#include "win32/WinIconProvider.h"
#include "win32/WindowTracker.h"

#include <windows.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <QSessionManager>
#include <thread>

namespace {

struct TimePeriodGuard {
    bool active = false;
    TimePeriodGuard() {
        active = (timeBeginPeriod(1) == TIMERR_NOERROR);
    }
    ~TimePeriodGuard() {
        if (active) {
            timeEndPeriod(1);
        }
    }
};

QString resolveBaseDir() {
    const QString exeDir = QCoreApplication::applicationDirPath();
    if (QFileInfo::exists(QDir(exeDir).filePath(QStringLiteral("sekaipop/shell.qml")))) {
        return exeDir;
    }
    const QString cwd = QDir::currentPath();
    if (QFileInfo::exists(QDir(cwd).filePath(QStringLiteral("sekaipop/shell.qml")))) {
        return cwd;
    }
    QDir probe(exeDir);
    for (int i = 0; i < 4; ++i) {
        if (QFileInfo::exists(probe.filePath(QStringLiteral("sekaipop/shell.qml")))) {
            return probe.absolutePath();
        }
        if (!probe.cdUp()) {
            break;
        }
    }
    return exeDir;
}

QString pickShellFontFamily() {
    const QStringList families = QFontDatabase::families();
    if (families.contains(QStringLiteral("Google Sans Flex"), Qt::CaseInsensitive)) {
        return QStringLiteral("Google Sans Flex");
    }
    if (families.contains(QStringLiteral("Segoe UI Variable"), Qt::CaseInsensitive)) {
        return QStringLiteral("Segoe UI Variable");
    }
    return QStringLiteral("Segoe UI");
}

void shellMessageHandler(QtMsgType type, const QMessageLogContext&, const QString& msg) {
    const char* tag = "INFO";
    switch (type) {
    case QtDebugMsg: tag = "DEBUG"; break;
    case QtInfoMsg: tag = "INFO"; break;
    case QtWarningMsg: tag = "WARN"; break;
    case QtCriticalMsg: tag = "CRIT"; break;
    case QtFatalMsg: tag = "FATAL"; break;
    }
    const QString ts = QTime::currentTime().toString(QStringLiteral("HH:mm:ss.zzz"));
    const QByteArray utf8 = QStringLiteral("[%1] [%2] %3\n").arg(ts, QLatin1String(tag), msg).toUtf8();
    std::fwrite(utf8.constData(), 1, static_cast<size_t>(utf8.size()), stderr);

    static QMutex logMutex;
    static QFile logFile;
    static bool logInit = false;
    QMutexLocker locker(&logMutex);
    if (!logInit) {
        logInit = true;
        const QString dir = SekaiPop::Services::SettingsService::stateDir();
        QDir().mkpath(dir);
        const QString logPath = QDir(dir).filePath(QStringLiteral("shell.log"));
        if (QFileInfo(logPath).size() > 1024 * 1024) {
            QFile::remove(logPath);
        }
        logFile.setFileName(logPath);
        logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    }
    if (logFile.isOpen()) {
        logFile.write(utf8);
        if (type >= QtInfoMsg) {
            logFile.flush();
        }
    }
}

} // namespace

int main(int argc, char* argv[]) {
    QElapsedTimer startupTimer;
    startupTimer.start();

    // Boost startup priority so cold Windows boot schedules SekaiPop UI ahead of background
    // services and Defender scans; dropped back to NORMAL_PRIORITY_CLASS once first UI paints.
    SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);

    if (!qEnvironmentVariableIsSet("QSG_RENDER_LOOP")) {
        // Basic render loop shares one scenegraph render thread across all shell
        // windows on Windows D3D11 instead of spawning a separate D3D11 device,
        // shader cache, font atlas, and render thread per QQuickWindow.
        qputenv("QSG_RENDER_LOOP", "basic");
    }

    qInstallMessageHandler(shellMessageHandler);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    // Lean default surface format: 2D Qt Quick uses stencil for rounded clips (no 24-bit depth,
    // no MSAA swapchains, double-buffered instead of triple-buffered -> ~35-45% less GPU/RAM).
    // swapInterval(0) prevents multi-window VBlank blocking on the GUI thread under
    // QSG_RENDER_LOOP=basic; Windows DWM still presents all windowed swapchains tear-free at VSync.
    {
        QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();
        fmt.setDepthBufferSize(0);
        fmt.setStencilBufferSize(8);
        fmt.setSamples(0);
        fmt.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
        fmt.setSwapInterval(0);
        QSurfaceFormat::setDefaultFormat(fmt);
    }

    SekaiPop::Win32::ComInitGuard comGuard;
    TimePeriodGuard timePeriodGuard;

    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("SekaiPop"));
    app.setApplicationName(QStringLiteral("SekaiPopShell"));
    app.setQuitOnLastWindowClosed(false);
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QPixmapCache::setCacheLimit(10240); // 10 MB cap keeps icons & thumbnails hot without bloat

    QCommandLineParser parser;
    parser.addHelpOption();
    QCommandLineOption smokeOpt(QStringLiteral("smoke-ms"), QStringLiteral("Exit cleanly after N ms for smoke testing."), QStringLiteral("ms"));
    QCommandLineOption launcherOpt(QStringLiteral("open-launcher"), QStringLiteral("Open App Launcher shortly after startup."));
    QCommandLineOption phoneOpt(QStringLiteral("open-phone"), QStringLiteral("Open Phone panel (quick, apps, wallpapers, settings, edit_desktop, eq) after startup."), QStringLiteral("screen"));
    QCommandLineOption popupOpt(QStringLiteral("open-popup"), QStringLiteral("Open Bar popup after startup."), QStringLiteral("kind"));
    QCommandLineOption ipcOpt(QStringLiteral("ipc"), QStringLiteral("Call IpcHandler target:func[:arg1[:arg2]] after startup."), QStringLiteral("spec"));
    QCommandLineOption dumpOpt(QStringLiteral("dump-dir"), QStringLiteral("Dump live window state and grabWindow PNGs to directory."), QStringLiteral("dir"));
    QCommandLineOption nativeShellOpt(QStringLiteral("native-shell"), QStringLiteral("Running as the primary Windows Winlogon shell under SekaiPopBoot watchdog."));
    parser.addOption(smokeOpt);
    parser.addOption(launcherOpt);
    parser.addOption(phoneOpt);
    parser.addOption(popupOpt);
    parser.addOption(ipcOpt);
    parser.addOption(dumpOpt);
    parser.addOption(nativeShellOpt);
    parser.process(app);
    const bool isNativeShell = parser.isSet(nativeShellOpt);

    // Prevent accidental duplicate SekaiPopShell instances fighting over GPU, z-order, and hotkeys.
    // Automated test runs (--smoke-ms / --dump-dir) bypass the guard.
    struct SingleInstanceGuard {
        HANDLE mutex = nullptr;
        void release() {
            if (mutex) {
                ReleaseMutex(mutex);
                CloseHandle(mutex);
                mutex = nullptr;
            }
        }
        ~SingleInstanceGuard() { release(); }
    } instanceGuard;

    const bool isTestRun = parser.isSet(smokeOpt) || parser.isSet(dumpOpt);
    if (!isTestRun) {
        constexpr wchar_t kMutexName[] = L"Local\\SekaiPopShell_SingleInstance_v1";
        HANDLE h = CreateMutexW(nullptr, TRUE, kMutexName);
        if (h) {
            if (GetLastError() == ERROR_ALREADY_EXISTS) {
                // Allow a brief handoff window when Restart SekaiPop launches the replacement
                const DWORD waitRes = WaitForSingleObject(h, 1500);
                if (waitRes != WAIT_OBJECT_0 && waitRes != WAIT_ABANDONED) {
                    qInfo("[SingleInstance] Another SekaiPopShell instance is already running; exiting duplicate.");
                    CloseHandle(h);
                    return 0;
                }
            }
            instanceGuard.mutex = h;
        }
    }

    const QString baseDir = resolveBaseDir();
    const QString exeDir = QCoreApplication::applicationDirPath();
    const QString fontFamily = pickShellFontFamily();
    app.setFont(QFont(fontFamily));

    // Win32 host subsystems
    SekaiPop::Win32::WallpaperHost wallpaperHost;
    SekaiPop::Win32::AppBarManager appBarManager;
    SekaiPop::Win32::WindowTracker windowTracker;
    SekaiPop::Win32::HotkeyManager hotkeyManager;
    SekaiPop::Win32::NativeTaskmanHost nativeTaskmanHost;
    hotkeyManager.setNativeShellMode(isNativeShell);
    if (isNativeShell || !SekaiPop::Win32::isExplorerShellPresent()) {
        nativeTaskmanHost.ensureActive();
    }

    // Minimal native C++ backend services actively bridged into the Quickshell runtime
    SekaiPop::Services::SettingsService settingsService;
    SekaiPop::Services::AppSource appSource(&settingsService);
    SekaiPop::Services::MediaSource mediaSource;
    SekaiPop::Services::AudioSpectrumSource audioSpectrumSource(&settingsService);
    SekaiPop::Services::BatterySource batterySource;
    SekaiPop::Services::QuickSource quickSource(&settingsService, nullptr);

    // Hide Windows taskbar cleanly when configured and keep it tucked without polling storms
    bool restoringShell = false;
    bool keepTaskbarHidden = settingsService.flag(QStringLiteral("dock.hideWindowsTaskbar"));
    appBarManager.setTaskbarHiddenMode(keepTaskbarHidden);
    if (keepTaskbarHidden) {
        SekaiPop::Win32::setWindowsTaskbarsVisible(false);
    }
    auto* taskbarGuard = new QTimer(&app);
    taskbarGuard->setInterval(2500);
    QObject::connect(taskbarGuard, &QTimer::timeout, &app, [&restoringShell, &keepTaskbarHidden, &appBarManager, &nativeTaskmanHost]() {
        if (!restoringShell && !SekaiPop::Win32::isExplorerShellPresent()) {
            nativeTaskmanHost.ensureActive();
        }
        if (!restoringShell && keepTaskbarHidden) {
            if (SekaiPop::Win32::isWindowsTaskbarVisible()) {
                SekaiPop::Win32::setWindowsTaskbarsVisible(false);
                appBarManager.reapplyAllReservations();
            } else {
                appBarManager.ensureWorkAreasValid();
            }
        }
    });
    taskbarGuard->start();

    auto doRestoreTaskbar = [&keepTaskbarHidden, &appBarManager, &windowTracker, &nativeTaskmanHost]() {
        keepTaskbarHidden = false;
        appBarManager.setTaskbarHiddenMode(false);
        nativeTaskmanHost.release();
        SekaiPop::Win32::setWindowsTaskbarsVisible(true);
        windowTracker.restoreAllWorkspaces();
        appBarManager.restoreAllWorkAreas();
        if (!SekaiPop::Win32::isExplorerShellPresent()) {
            SekaiPop::Win32::launchExplorer();
        }
        qInfo("[Emergency] Restored Windows Taskbar & Work Areas");
    };

    auto doToggleTaskbar = [&keepTaskbarHidden, &appBarManager, &doRestoreTaskbar]() {
        if (keepTaskbarHidden || !SekaiPop::Win32::isWindowsTaskbarVisible()) {
            doRestoreTaskbar();
        } else {
            keepTaskbarHidden = true;
            appBarManager.setTaskbarHiddenMode(true);
            SekaiPop::Win32::setWindowsTaskbarsVisible(false);
            appBarManager.reapplyAllReservations();
            qInfo("[Emergency] Hid Windows Taskbar & Reapplied Work Areas");
        }
    };

    auto doEmergencyRestoreAndExit = [&restoringShell, &keepTaskbarHidden, &appBarManager, &windowTracker, &nativeTaskmanHost]() {
        restoringShell = true;
        keepTaskbarHidden = false;
        appBarManager.setTaskbarHiddenMode(false);
        nativeTaskmanHost.release();
        SekaiPop::Win32::setWindowsTaskbarsVisible(true);
        windowTracker.restoreAllWorkspaces();
        appBarManager.restoreAllWorkAreas();
        SekaiPop::Win32::launchExplorer();
        qInfo("[Emergency] Ctrl+Shift+Alt+E: Launched explorer.exe via CreateProcessW, restored Windows, and exiting safely");
        QCoreApplication::quit();
    };

    auto doEmergencyExit = [&restoringShell, &keepTaskbarHidden, &appBarManager, &windowTracker, &nativeTaskmanHost]() {
        restoringShell = true;
        keepTaskbarHidden = false;
        appBarManager.setTaskbarHiddenMode(false);
        nativeTaskmanHost.release();
        SekaiPop::Win32::setWindowsTaskbarsVisible(true);
        windowTracker.restoreAllWorkspaces();
        appBarManager.restoreAllWorkAreas();
        if (!SekaiPop::Win32::isExplorerShellPresent()) {
            SekaiPop::Win32::launchExplorer();
        }
        qInfo("[Emergency] Exiting SekaiPop Shell and restoring Windows");
        QCoreApplication::quit();
    };

    auto doRestartShell = [&restoringShell, &keepTaskbarHidden, &appBarManager, &windowTracker, &nativeTaskmanHost, &instanceGuard, isNativeShell]() {
        restoringShell = true;
        keepTaskbarHidden = false;
        nativeTaskmanHost.release();
        SekaiPop::Win32::setWindowsTaskbarsVisible(true);
        windowTracker.restoreAllWorkspaces();
        appBarManager.restoreAllWorkAreas();
        instanceGuard.release();
        if (isNativeShell) {
            qInfo("[Emergency] Native-shell restart requested (exit code 42 for SekaiPopBoot watchdog)");
            QCoreApplication::exit(42);
            return;
        }
        const QString selfExe = QCoreApplication::applicationFilePath();
        qInfo("[Emergency] Restarting SekaiPop Shell: %s", qPrintable(selfExe));
        QProcess::startDetached(selfExe, {});
        QCoreApplication::quit();
    };

    // In-process Emergency IPC handler ("shell" target: exit, restart, toggletaskbar, restoretaskbar)
    SekaiPop::Quickshell::QsShellEmergencyIpc shellEmergencyIpc;
    QObject::connect(&shellEmergencyIpc, &SekaiPop::Quickshell::QsShellEmergencyIpc::exitRequested, &app, doEmergencyExit);
    QObject::connect(&shellEmergencyIpc, &SekaiPop::Quickshell::QsShellEmergencyIpc::restartRequested, &app, doRestartShell);
    QObject::connect(&shellEmergencyIpc, &SekaiPop::Quickshell::QsShellEmergencyIpc::toggleTaskbarRequested, &app, doToggleTaskbar);
    QObject::connect(&shellEmergencyIpc, &SekaiPop::Quickshell::QsShellEmergencyIpc::restoreTaskbarRequested, &app, doRestoreTaskbar);

    // Native Windows System Tray Icon for emergency access & quick controls
    QSystemTrayIcon* trayIcon = nullptr;
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        QPixmap pm(32, 32);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setBrush(QColor(0x1E, 0x29, 0x3B));
        p.setPen(QPen(QColor(0x5E, 0xEA, 0xD4), 2.5));
        p.drawEllipse(3, 3, 26, 26);
        p.setPen(Qt::white);
        QFont f(fontFamily, 14, QFont::Bold);
        p.setFont(f);
        p.drawText(pm.rect(), Qt::AlignCenter, QStringLiteral("S"));
        p.end();

        trayIcon = new QSystemTrayIcon(QIcon(pm), &app);
        trayIcon->setToolTip(QStringLiteral("SekaiPop Shell (Emergency: Ctrl+Shift+Alt+E Restore Explorer & Exit / R Restart)"));

        auto* trayMenu = new QMenu();
        QObject::connect(trayMenu->addAction(QStringLiteral("Quick Panel / Phone (Win / Alt+N)")), &QAction::triggered, &app, []() {
            SekaiPop::Quickshell::QsIpcHandler::dispatchCall(QStringLiteral("phone"), QStringLiteral("toggle"));
        });
        QObject::connect(trayMenu->addAction(QStringLiteral("App Launcher (Alt+Space)")), &QAction::triggered, &app, []() {
            SekaiPop::Quickshell::QsIpcHandler::dispatchCall(QStringLiteral("phone"), QStringLiteral("go"), {QStringLiteral("apps")});
        });
        QObject::connect(trayMenu->addAction(QStringLiteral("Edit Desktop")), &QAction::triggered, &app, []() {
            SekaiPop::Quickshell::QsIpcHandler::dispatchCall(QStringLiteral("desktopedit"), QStringLiteral("toggle"));
        });
        trayMenu->addSeparator();
        QObject::connect(trayMenu->addAction(QStringLiteral("Toggle Windows Taskbar")), &QAction::triggered, &app, doToggleTaskbar);
        QObject::connect(trayMenu->addAction(QStringLiteral("Restart SekaiPop (Ctrl+Shift+Alt+R)")), &QAction::triggered, &app, doRestartShell);
        QObject::connect(trayMenu->addAction(QStringLiteral("Emergency: Restore Explorer & Exit (Ctrl+Shift+Alt+E)")), &QAction::triggered, &app, doEmergencyRestoreAndExit);
        QObject::connect(trayMenu->addAction(QStringLiteral("Exit SekaiPop (Ctrl+Shift+Alt+Q)")), &QAction::triggered, &app, doEmergencyExit);

        trayIcon->setContextMenu(trayMenu);
        QObject::connect(trayIcon, &QSystemTrayIcon::activated, &app, [](QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
                SekaiPop::Quickshell::QsIpcHandler::dispatchCall(QStringLiteral("phone"), QStringLiteral("toggle"));
            }
        });
        trayIcon->show();
    }

    // Connect global Win32 hotkeys directly to Quickshell IPC targets & emergency actions
    QObject::connect(&hotkeyManager, &SekaiPop::Win32::HotkeyManager::winKeyTapped, &app, []() {
        SekaiPop::Quickshell::QsIpcHandler::dispatchCall(QStringLiteral("phone"), QStringLiteral("toggle"));
    });
    QObject::connect(&hotkeyManager, &SekaiPop::Win32::HotkeyManager::launcherToggleRequested, &app, []() {
        SekaiPop::Quickshell::QsIpcHandler::dispatchCall(QStringLiteral("phone"), QStringLiteral("go"), {QStringLiteral("apps")});
    });
    QObject::connect(&hotkeyManager, &SekaiPop::Win32::HotkeyManager::quickPanelToggleRequested, &app, []() {
        SekaiPop::Quickshell::QsIpcHandler::dispatchCall(QStringLiteral("phone"), QStringLiteral("toggle"));
    });
    QObject::connect(&hotkeyManager, &SekaiPop::Win32::HotkeyManager::restoreExplorerRequested, &app, doEmergencyRestoreAndExit);
    QObject::connect(&hotkeyManager, &SekaiPop::Win32::HotkeyManager::restartShellRequested, &app, doRestartShell);
    QObject::connect(&hotkeyManager, &SekaiPop::Win32::HotkeyManager::exitShellRequested, &app, doEmergencyExit);

    // Essential Windows shortcuts when running as Full Native Shell (explorer.exe not running)
    auto showDesktopMinimized = std::make_shared<QList<HWND>>();
    QObject::connect(&hotkeyManager, &SekaiPop::Win32::HotkeyManager::nativeWinChordRequested, &app, [&windowTracker, showDesktopMinimized](int vkCode, bool shiftHeld) {
        if (!shiftHeld && vkCode == 'E') {
            ShellExecuteW(nullptr, L"open", L"explorer.exe", L"/separate,::{20D04FE0-3AEA-1069-A2D8-08002B30309D}", nullptr, SW_SHOWNORMAL);
        } else if (!shiftHeld && vkCode == 'R') {
            std::thread([]() {
                CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
                HMODULE hShell = LoadLibraryW(L"shell32.dll");
                if (hShell) {
                    using RunFileDlgFn = void(WINAPI*)(HWND, HICON, LPCWSTR, LPCWSTR, LPCWSTR, UINT);
                    auto pRunDlg = reinterpret_cast<RunFileDlgFn>(GetProcAddress(hShell, MAKEINTRESOURCEA(61)));
                    if (pRunDlg) {
                        pRunDlg(nullptr, nullptr, nullptr, nullptr, nullptr, 0);
                    }
                    FreeLibrary(hShell);
                }
                CoUninitialize();
            }).detach();
        } else if (!shiftHeld && vkCode == 'I') {
            SekaiPop::Win32::launchShellTarget(QStringLiteral("ms-settings:"));
        } else if (shiftHeld && vkCode == 'S') {
            SekaiPop::Win32::ScreenshotOverlay::startRegionSnip();
        } else if (!shiftHeld && vkCode == 'D') {
            const QVariantList wins = windowTracker.openWindows();
            QList<HWND> toMinimize;
            for (const QVariant& v : wins) {
                const QVariantMap m = v.toMap();
                if (!m.value(QStringLiteral("minimized")).toBool()) {
                    HWND h = reinterpret_cast<HWND>(static_cast<quintptr>(m.value(QStringLiteral("hwnd")).toULongLong()));
                    if (h && IsWindow(h) && IsWindowVisible(h) && !IsIconic(h)) {
                        toMinimize.append(h);
                    }
                }
            }
            if (!toMinimize.isEmpty()) {
                *showDesktopMinimized = toMinimize;
                for (HWND h : toMinimize) {
                    ShowWindowAsync(h, SW_MINIMIZE);
                }
            } else if (!showDesktopMinimized->isEmpty()) {
                for (HWND h : *showDesktopMinimized) {
                    if (IsWindow(h) && IsIconic(h)) {
                        ShowWindowAsync(h, SW_RESTORE);
                    }
                }
                showDesktopMinimized->clear();
            }
        }
    });

    // Native multi-monitor screenshot hotkeys (PrtSc / Win+PrtSc / Alt+PrtSc / Ctrl+PrtSc)
    QObject::connect(&hotkeyManager, &SekaiPop::Win32::HotkeyManager::screenshotRequested, &app, [](int mode) {
        switch (mode) {
        case 1: SekaiPop::Win32::ScreenshotOverlay::captureActiveMonitor(); break;
        case 2: SekaiPop::Win32::ScreenshotOverlay::captureActiveWindow(); break;
        case 3: SekaiPop::Win32::ScreenshotOverlay::captureAllMonitors(); break;
        default: SekaiPop::Win32::ScreenshotOverlay::startRegionSnip(); break;
        }
    });

    // Native keyboard volume / mute / media keys when explorer.exe shell is not running
    QObject::connect(&hotkeyManager, &SekaiPop::Win32::HotkeyManager::nativeMediaKeyRequested, &app, [&quickSource, &mediaSource](int vkCode) {
        if (vkCode == VK_VOLUME_UP) {
            quickSource.setSpeakerVolume(qMin<qreal>(100.0, quickSource.speakerVolume() + 2.0));
        } else if (vkCode == VK_VOLUME_DOWN) {
            quickSource.setSpeakerVolume(qMax<qreal>(0.0, quickSource.speakerVolume() - 2.0));
        } else if (vkCode == VK_VOLUME_MUTE) {
            quickSource.toggleSpeaker();
        } else if (vkCode == VK_MEDIA_PLAY_PAUSE) {
            mediaSource.togglePlayPause();
        } else if (vkCode == VK_MEDIA_NEXT_TRACK) {
            mediaSource.next();
        } else if (vkCode == VK_MEDIA_PREV_TRACK) {
            mediaSource.previous();
        }
    });

    // Exit cleanly on Windows Sign Out / Shut Down so OS session shutdown never stalls
    QObject::connect(&app, &QGuiApplication::commitDataRequest, &app, [](QSessionManager& sm) {
        sm.setRestartHint(QSessionManager::RestartNever);
        QCoreApplication::quit();
    });

    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&restoringShell, &keepTaskbarHidden, &appBarManager, &windowTracker, &nativeTaskmanHost, trayIcon]() {
        restoringShell = true;
        keepTaskbarHidden = false;
        if (trayIcon) {
            trayIcon->hide();
        }
        SekaiPop::Quickshell::QsWindowManager::shutdown();
        nativeTaskmanHost.release();
        SekaiPop::Win32::setWindowsTaskbarsVisible(true);
        windowTracker.restoreAllWorkspaces();
        appBarManager.restoreAllWorkAreas();
    });

    // Pause background visualizer audio/shader work automatically when a maximized/fullscreen
    // application covers the primary monitor so idle CPU & GPU stay near 0%.
    audioSpectrumSource.setDesktopOccluded(windowTracker.primaryOccluded());
    QObject::connect(&windowTracker, &SekaiPop::Win32::WindowTracker::primaryOccludedChanged, &app, [&windowTracker, &audioSpectrumSource]() {
        audioSpectrumSource.setDesktopOccluded(windowTracker.primaryOccluded());
    });

    // Automatically hide the Bar, Pinned Dock, and bottom HotEdge on any monitor currently
    // showing a true fullscreen video (YouTube/Netflix/VLC) or fullscreen/borderless game.
    auto syncFullscreenState = [&windowTracker, &appBarManager]() {
        const QSet<quintptr> fsMons = windowTracker.fullscreenMonitors();
        appBarManager.setFullscreenMonitors(fsMons);
        if (auto* wm = SekaiPop::Quickshell::QsWindowManager::instance()) {
            wm->setFullscreenMonitors(fsMons);
        }
    };
    syncFullscreenState();
    QObject::connect(&windowTracker, &SekaiPop::Win32::WindowTracker::fullscreenMonitorsChanged, &app, syncFullscreenState);

    // Share a single Direct3D 11 QRhi device across all shell QQuickWindows so the GPU driver
    // creates ONE device/thread-pool instead of 14+ separate devices (saving ~300 threads & ~400MB RAM).
    SekaiPop::Quickshell::QsWindowManager::initSharedGraphics();

    int rc = 0;
    {
        QQmlApplicationEngine engine;
        if (auto* wm = SekaiPop::Quickshell::QsWindowManager::instance()) {
            wm->setQmlEngine(&engine);
        }
        auto* iconProvider = new SekaiPop::Win32::WinIconProvider();
        engine.addImageProvider(QStringLiteral("winicon"), iconProvider);

        QQmlContext* ctx = engine.rootContext();
        ctx->setContextProperty(QStringLiteral("ShellBaseDir"), baseDir);
        ctx->setContextProperty(QStringLiteral("ShellFontFamily"), fontFamily);

        SekaiPop::Quickshell::QsRuntimeDeps qsDeps;
        qsDeps.baseDir = baseDir;
        qsDeps.exeDir = exeDir;
        qsDeps.appSource = &appSource;
        qsDeps.spectrumSource = &audioSpectrumSource;
        qsDeps.batterySource = &batterySource;
        qsDeps.mediaSource = &mediaSource;
        qsDeps.quickSource = &quickSource;
        qsDeps.wallpaperHost = &wallpaperHost;
        qsDeps.appBarManager = &appBarManager;
        qsDeps.windowTracker = &windowTracker;

        const qint64 tBeforeSetup = startupTimer.elapsed();
        const QUrl shellUrl = SekaiPop::Quickshell::setupQuickshellEngine(engine, qsDeps);
        const qint64 tAfterSetup = startupTimer.elapsed();

        QObject::connect(
            &engine,
            &QQmlApplicationEngine::objectCreationFailed,
            &app,
            []() { QCoreApplication::exit(-1); },
            Qt::QueuedConnection
        );
        engine.load(shellUrl);
        const qint64 tAfterLoad = startupTimer.elapsed();
        qInfo("STARTUP: engine.load(%s) finished rootObjects=%lld (cpp=%lldms setup=%lldms load=%lldms total=%lldms)",
              qPrintable(shellUrl.toString()),
              static_cast<long long>(engine.rootObjects().size()),
              tBeforeSetup,
              tAfterSetup - tBeforeSetup,
              tAfterLoad - tAfterSetup,
              tAfterLoad);

        auto uiReadySignaled = std::make_shared<bool>(false);
        auto signalUiReady = [uiReadySignaled, startupTimer](const char* reason) {
            if (*uiReadySignaled) {
                return;
            }
            *uiReadySignaled = true;
            qInfo("STARTUP: UI ready (%s) at %lldms", reason, startupTimer.elapsed());
            // Wake SekaiPopBoot watchdog so deferred Windows startup apps can launch now
            HANDLE hEvt = OpenEventW(EVENT_MODIFY_STATE, FALSE, L"Local\\SekaiPopShell_UiReady_v1");
            if (hEvt) {
                SetEvent(hEvt);
                CloseHandle(hEvt);
            }
            SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);
        };
        if (auto* wm = SekaiPop::Quickshell::QsWindowManager::instance()) {
            QObject::connect(wm, &SekaiPop::Quickshell::QsWindowManager::uiReady, &app, [signalUiReady]() {
                signalUiReady("wallpaper+bar");
            });
            wm->scheduleMemoryTrim(7000);
        }
        QTimer::singleShot(4500, &app, [signalUiReady]() {
            signalUiReady("failsafe");
        });

        const bool wantLauncher = parser.isSet(launcherOpt);
        const bool wantPhone = parser.isSet(phoneOpt);
        const bool wantPopup = parser.isSet(popupOpt);
        const bool wantIpc = parser.isSet(ipcOpt);
        const bool hasAction = wantLauncher || wantPhone || wantPopup || wantIpc;
        const bool hasDump = parser.isSet(dumpOpt);
        const bool hasSmoke = parser.isSet(smokeOpt);
        const QString actionPhone = parser.value(phoneOpt);
        const QString actionPopup = parser.value(popupOpt);
        const QStringList actionIpcs = parser.values(ipcOpt);
        const QString dumpDir = parser.value(dumpOpt);
        const int smokeMs = hasSmoke ? qMax(200, parser.value(smokeOpt).toInt()) : 0;

        if (hasAction || hasDump || hasSmoke) {
            auto* driver = new QTimer(&app);
            driver->setInterval(200);
            auto* clock = new QElapsedTimer();
            clock->start();

            struct SmokeState {
                bool actionRan = false;
                bool dumpRan = false;
                qint64 actionAtMs = 0;
                qint64 dumpAtMs = 0;
                int postActionTicks = 0;
            };
            auto* st = new SmokeState();

            auto runDump = [dumpDir]() {
                qInfo("DUMP_TIMER_FIRED: dir=%s", qPrintable(dumpDir));
                QDir().mkpath(dumpDir);
                int idx = 0;
                for (QWindow* w : QGuiApplication::allWindows()) {
                    auto* qw = qobject_cast<QQuickWindow*>(w);
                    const QRect g = w->geometry();
                    const HWND hwnd = w->isVisible() ? reinterpret_cast<HWND>(w->winId()) : nullptr;
                    RECT wr = {};
                    if (hwnd) {
                        GetWindowRect(hwnd, &wr);
                    }
                    QString ns;
                    if (auto* pw = qobject_cast<SekaiPop::Quickshell::QsPanelWindow*>(w)) {
                        if (auto* ls = pw->layerShell()) {
                            ns = ls->ns();
                        }
                    }
                    qInfo("DUMP[%d]: ns=%s hwnd=%p vis=%d exp=%d winVis=%d screen=%s qtGeom=(%d,%d,%dx%d) dpr=%.2f win32=(%ld,%ld,%ld,%ld)",
                          idx, qPrintable(ns), hwnd, w->isVisible() ? 1 : 0, w->isExposed() ? 1 : 0,
                          (hwnd && IsWindowVisible(hwnd)) ? 1 : 0,
                          w->screen() ? qPrintable(w->screen()->name()) : "null",
                          g.x(), g.y(), g.width(), g.height(), w->devicePixelRatio(),
                          wr.left, wr.top, wr.right, wr.bottom);
                    if (qw && w->isVisible() && g.width() > 0 && g.height() > 0) {
                        const QImage img = qw->grabWindow();
                        const QString tag = ns.isEmpty() ? QStringLiteral("win") : ns;
                        const QString outPath = QDir(dumpDir).filePath(QStringLiteral("%1_%2_%3x%4.png").arg(idx).arg(tag).arg(img.width()).arg(img.height()));
                        img.save(outPath);
                    }
                    ++idx;
                }
                if (QScreen* pscr = QGuiApplication::primaryScreen()) {
                    const QPixmap pm = pscr->grabWindow(0);
                    pm.save(QDir(dumpDir).filePath(QStringLiteral("desktop_primary.png")));
                }
            };

            QObject::connect(driver, &QTimer::timeout, &app, [driver, clock, st, hasAction, hasDump, hasSmoke, wantLauncher, wantPhone, wantPopup, wantIpc, actionPhone, actionPopup, actionIpcs, smokeMs, runDump, &appBarManager, &windowTracker, &nativeTaskmanHost]() {
                const qint64 nowMs = clock->elapsed();
                if (hasAction && !st->actionRan && nowMs >= 1800) {
                    st->actionRan = true;
                    st->actionAtMs = nowMs;
                    if (wantLauncher) {
                        qInfo("ACTION_FIRED: ipc phone go apps at %lldms", nowMs);
                        SekaiPop::Quickshell::QsIpcHandler::dispatchCall(QStringLiteral("phone"), QStringLiteral("go"), {QStringLiteral("apps")});
                    }
                    if (wantPhone) {
                        qInfo("ACTION_FIRED: openPhone(%s) at %lldms", qPrintable(actionPhone), nowMs);
                        if (actionPhone == QStringLiteral("edit_desktop") || actionPhone == QStringLiteral("desktopedit")) {
                            SekaiPop::Quickshell::QsIpcHandler::dispatchCall(QStringLiteral("desktopedit"), QStringLiteral("open"));
                        } else if (actionPhone == QStringLiteral("eq")) {
                            SekaiPop::Quickshell::QsIpcHandler::dispatchCall(QStringLiteral("eq"), QStringLiteral("open"));
                        } else if (actionPhone == QStringLiteral("palette")) {
                            SekaiPop::Quickshell::QsIpcHandler::dispatchCall(QStringLiteral("palette"), QStringLiteral("show"));
                        } else {
                            SekaiPop::Quickshell::QsIpcHandler::dispatchCall(QStringLiteral("phone"), QStringLiteral("go"), {actionPhone});
                        }
                    }
                    if (wantPopup) {
                        qInfo("ACTION_FIRED: openPopup(%s) at %lldms", qPrintable(actionPopup), nowMs);
                        if (actionPopup == QStringLiteral("eq")) {
                            SekaiPop::Quickshell::QsIpcHandler::dispatchCall(QStringLiteral("eq"), QStringLiteral("open"));
                        }
                    }
                    if (wantIpc) {
                        for (const QString& spec : actionIpcs) {
                            const int firstColon = spec.indexOf(QLatin1Char(':'));
                            const int secondColon = firstColon >= 0 ? spec.indexOf(QLatin1Char(':'), firstColon + 1) : -1;
                            if (firstColon >= 0) {
                                const QString target = spec.left(firstColon);
                                const QString func = (secondColon >= 0) ? spec.mid(firstColon + 1, secondColon - firstColon - 1) : spec.mid(firstColon + 1);
                                const QString remainder = (secondColon >= 0) ? spec.mid(secondColon + 1) : QString();
                                QStringList args;
                                if (!remainder.isEmpty()) {
                                    args.append(remainder);
                                }
                                qInfo("ACTION_FIRED: ipc %s at %lldms (target=%s func=%s args=%s)",
                                      qPrintable(spec), nowMs, qPrintable(target), qPrintable(func), qPrintable(args.join(QLatin1String(", "))));
                                SekaiPop::Quickshell::QsIpcHandler::dispatchCall(target, func, args);
                            }
                        }
                    }
                    return;
                }
                if (hasAction && st->actionRan && !st->dumpRan) {
                    ++st->postActionTicks;
                }
                const qint64 autoDumpMs = (hasSmoke && smokeMs >= 5000) ? qMax<qint64>(2800, smokeMs - 1500) : 2800;
                if (hasDump && !st->dumpRan && ((!hasAction && nowMs >= autoDumpMs) || (hasAction && st->actionRan && st->postActionTicks >= 12 && (nowMs - st->actionAtMs) >= 1800))) {
                    st->dumpRan = true;
                    st->dumpAtMs = nowMs;
                    runDump();
                    return;
                }
                if (hasSmoke && (!hasAction || st->actionRan) && (!hasDump || (st->dumpRan && (nowMs - st->dumpAtMs) >= 200)) && nowMs >= smokeMs) {
                    driver->stop();
                    delete st;
                    delete clock;
                    qInfo("SMOKE_EXIT: quitting now at %lldms", nowMs);
                    nativeTaskmanHost.release();
                    SekaiPop::Win32::setWindowsTaskbarsVisible(true);
                    windowTracker.restoreAllWorkspaces();
                    appBarManager.restoreAllWorkAreas();
                    QCoreApplication::quit();
                }
            });
            driver->start();
        }

        qInfo("STARTUP: entering app.exec()");
        rc = app.exec();
        qInfo("SHUTDOWN: app.exec() returned %d", rc);
    }

    // All QQuickWindows from `engine` are now destroyed; safe to release the shared QRhi.
    SekaiPop::Quickshell::QsWindowManager::releaseSharedGraphics();
    return rc;
}
