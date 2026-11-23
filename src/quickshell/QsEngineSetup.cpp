#include "QsEngineSetup.h"
#include "QsIo.h"
#include "QsServices.h"
#include "QsTypes.h"
#include "QsWindowing.h"
#include "../services/VideoWallpaperItem.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlNetworkAccessManagerFactory>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>
#include <qqml.h>
#include <windows.h>

namespace SekaiPop::Quickshell {

namespace {

class QsNetworkAccessManager : public QNetworkAccessManager {
public:
    explicit QsNetworkAccessManager(QObject* parent = nullptr)
        : QNetworkAccessManager(parent) {}

protected:
    QNetworkReply* createRequest(Operation op, const QNetworkRequest& req, QIODevice* outgoingData) override {
        if (!m_cacheInit) {
            m_cacheInit = true;
            QString localAppData = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
            if (localAppData.isEmpty()) {
                localAppData = QDir::homePath() + QStringLiteral("/AppData/Local");
            }
            const QString cacheDir = QDir(localAppData).filePath(QStringLiteral("SekaiPop/cache/sekaipop/net_cache"));
            QDir().mkpath(cacheDir);
            auto* diskCache = new QNetworkDiskCache(this);
            diskCache->setCacheDirectory(cacheDir);
            diskCache->setMaximumCacheSize(64LL * 1024LL * 1024LL);
            setCache(diskCache);
        }
        QNetworkRequest r(req);
        if (!r.hasRawHeader("User-Agent") || r.rawHeader("User-Agent").isEmpty()) {
            r.setRawHeader("User-Agent", "SekaiPop/1.0");
        }
        r.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        r.setAttribute(QNetworkRequest::Http2AllowedAttribute, true);
        if (op == GetOperation) {
            const QString host = r.url().host().toLower();
            if (host.endsWith(QStringLiteral("motionbgs.com")) ||
                host.endsWith(QStringLiteral("wallhaven.cc"))) {
                r.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
            }
        }
        return QNetworkAccessManager::createRequest(op, r, outgoingData);
    }

private:
    bool m_cacheInit = false;
};

class QsNamFactory : public QQmlNetworkAccessManagerFactory {
public:
    QNetworkAccessManager* create(QObject* parent) override {
        return new QsNetworkAccessManager(parent);
    }
};

void writeFileIfChanged(const QString& path, const QByteArray& content) {
    QFile existing(path);
    if (existing.open(QIODevice::ReadOnly)) {
        if (existing.readAll() == content) {
            return;
        }
        existing.close();
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(content);
        f.commit();
    }
}

void ensureDirJunction(const QString& linkPath, const QString& targetPath) {
    const QFileInfo targetInfo(targetPath);
    if (!targetInfo.exists()) {
        return;
    }
    const QString targetCanon = targetInfo.canonicalFilePath();
    const std::wstring wlink = QDir::toNativeSeparators(linkPath).toStdWString();
    const DWORD attrs = GetFileAttributesW(wlink.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES) {
        const QFileInfo linkInfo(linkPath);
        const QString linkCanon = linkInfo.canonicalFilePath();
        if (!linkCanon.isEmpty() && linkCanon.compare(targetCanon, Qt::CaseInsensitive) == 0) {
            return;
        }
        // Remove stale or dangling directory junction (RemoveDirectoryW removes only the reparse point)
        RemoveDirectoryW(wlink.c_str());
    }
    QDir().mkpath(QFileInfo(linkPath).absolutePath());
    QProcess proc;
    proc.start(QStringLiteral("cmd.exe"), {
        QStringLiteral("/c"),
        QStringLiteral("mklink"),
        QStringLiteral("/J"),
        QDir::toNativeSeparators(linkPath),
        QDir::toNativeSeparators(targetPath)
    });
    proc.waitForFinished(3000);
}

using StagedSingletonInfo = StagedSingletonEntry;

bool isTier1Singleton(const QString& name) {
    // Minimal 11-singleton critical core required to paint Wallpaper + Bar on Frame 0.
    // All remaining 60 singletons initialize cleanly in non-blocking micro-batches right after UI ready.
    static const QSet<QString> tier1 = {
        QStringLiteral("Directories"),
        QStringLiteral("SettingsService"),
        QStringLiteral("GameModeService"),
        QStringLiteral("WallpaperService"),
        QStringLiteral("ShellScale"),
        QStringLiteral("ThemeService"),
        QStringLiteral("Theme"),
        QStringLiteral("BarPositionService"),
        QStringLiteral("BarStyleService"),
        QStringLiteral("BarLayoutService"),
        QStringLiteral("StatusIconsService")
    };
    return tier1.contains(name);
}

int singletonPriority(const QString& name) {
    static const QHash<QString, int> prio = {
        {QStringLiteral("Directories"), 1},
        {QStringLiteral("SettingsService"), 2},
        {QStringLiteral("GameModeService"), 3},
        {QStringLiteral("WallpaperService"), 4},
        {QStringLiteral("ShellScale"), 5},
        {QStringLiteral("ThemeService"), 6},
        {QStringLiteral("Theme"), 7},
        {QStringLiteral("LockPalette"), 8},
        {QStringLiteral("PadPalette"), 9},
        {QStringLiteral("ScrapbookTheme"), 10},
        {QStringLiteral("PresetMotion"), 11},
        {QStringLiteral("PanelImages"), 12},
        {QStringLiteral("PluginRepoService"), 13},
        {QStringLiteral("PluginService"), 14},
        {QStringLiteral("BarPositionService"), 15},
        {QStringLiteral("BarStyleService"), 16},
        {QStringLiteral("BarLayoutService"), 17},
        {QStringLiteral("DesktopMonitors"), 18},
        {QStringLiteral("NiriWorkspaces"), 18},
        {QStringLiteral("LockState"), 19},
        {QStringLiteral("EditGrid"), 20},
        {QStringLiteral("EditMode"), 21},
        {QStringLiteral("FocusService"), 22},
        {QStringLiteral("ComponentActivation"), 23},
        {QStringLiteral("WidgetClones"), 24},
        {QStringLiteral("ClockService"), 25},
        {QStringLiteral("UserCardService"), 26},
        {QStringLiteral("WeatherCardService"), 27},
        {QStringLiteral("CalendarCardService"), 28},
        {QStringLiteral("TodoService"), 29},
        {QStringLiteral("NotesService"), 30},
        {QStringLiteral("ImageService"), 31},
        {QStringLiteral("PaletteService"), 32},
        {QStringLiteral("LyricsService"), 33},
        {QStringLiteral("VisualizerService"), 34},
        {QStringLiteral("EqualizerService"), 35},
        {QStringLiteral("PinnedAppsService"), 36},
        {QStringLiteral("DockService"), 37},
        {QStringLiteral("PresetWidgets"), 38},
        {QStringLiteral("DesktopPresetService"), 39}
    };
    return prio.value(name, 50);
}

void stageQuickshellHelperModules(const QString& stageRoot) {
    // 1. Quickshell.Widgets
    const QString widgetsDir = stageRoot + QStringLiteral("/Quickshell/Widgets");
    QDir().mkpath(widgetsDir);

    writeFileIfChanged(widgetsDir + QStringLiteral("/qmldir"), QByteArrayLiteral(
        "module Quickshell.Widgets\n"
        "ClippingRectangle 1.0 ClippingRectangle.qml\n"
        "WrapperRectangle 1.0 WrapperRectangle.qml\n"
        "IconImage 1.0 IconImage.qml\n"
    ));

    writeFileIfChanged(widgetsDir + QStringLiteral("/ClippingRectangle.qml"), QByteArrayLiteral(
        "import Quickshell\n"
        "QsClippingRectangle {}\n"
    ));

    writeFileIfChanged(widgetsDir + QStringLiteral("/WrapperRectangle.qml"), QByteArrayLiteral(
        "import QtQuick\n"
        "Rectangle {\n"
        "    property real margin: 0\n"
        "    property real topMargin: margin\n"
        "    property real bottomMargin: margin\n"
        "    property real leftMargin: margin\n"
        "    property real rightMargin: margin\n"
        "}\n"
    ));

    writeFileIfChanged(widgetsDir + QStringLiteral("/IconImage.qml"), QByteArrayLiteral(
        "import QtQuick\n"
        "import Quickshell\n"
        "Item {\n"
        "    id: root\n"
        "    property real implicitSize: 32\n"
        "    property var source: \"\"\n"
        "    property alias asynchronous: img.asynchronous\n"
        "    property alias mipmap: img.mipmap\n"
        "    property alias smooth: img.smooth\n"
        "    property alias fillMode: img.fillMode\n"
        "    readonly property int status: img.status\n"
        "    implicitWidth: implicitSize\n"
        "    implicitHeight: implicitSize\n"
        "    width: implicitSize\n"
        "    height: implicitSize\n"
        "    Image {\n"
        "        id: img\n"
        "        anchors.fill: parent\n"
        "        source: {\n"
        "            const s = String(root.source ?? \"\")\n"
        "            if (s === \"\") return \"\"\n"
        "            if (s.indexOf(\"://\") >= 0) return s\n"
        "            return Quickshell.iconPath(s, \"\")\n"
        "        }\n"
        "        sourceSize: Qt.size(Math.max(16, Math.round(root.implicitSize * 2)), Math.max(16, Math.round(root.implicitSize * 2)))\n"
        "        fillMode: Image.PreserveAspectFit\n"
        "        smooth: true\n"
        "        mipmap: true\n"
        "        asynchronous: true\n"
        "    }\n"
        "}\n"
    ));

    // 2. Stub QtMultimedia if not installed in Qt 6 dir
    const QString mmDir = stageRoot + QStringLiteral("/QtMultimedia");
    QDir().mkpath(mmDir);
    writeFileIfChanged(mmDir + QStringLiteral("/qmldir"), QByteArrayLiteral(
        "module QtMultimedia\n"
        "MediaPlayer 1.0 MediaPlayer.qml\n"
        "VideoOutput 1.0 VideoOutput.qml\n"
    ));
    writeFileIfChanged(mmDir + QStringLiteral("/MediaPlayer.qml"), QByteArrayLiteral(
        "import QtQuick\n"
        "QtObject {\n"
        "    enum Loops { Infinite = -1, Once = 1 }\n"
        "    property url source: \"\"\n"
        "    property QtObject audioOutput: null\n"
        "    property Item videoOutput: null\n"
        "    property int loops: 1\n"
        "    property int position: 0\n"
        "    function play() {}\n"
        "    function pause() {}\n"
        "    function stop() {}\n"
        "}\n"
    ));
    writeFileIfChanged(mmDir + QStringLiteral("/VideoOutput.qml"), QByteArrayLiteral(
        "import QtQuick\n"
        "Item {\n"
        "    property int fillMode: Image.PreserveAspectCrop\n"
        "}\n"
    ));
}

QList<StagedSingletonInfo> stageSekaipopTree(const QString& sekaipopDir, const QString& exeDir, const QString& stageRoot, const QString& stageQsDir) {
    // Fast fingerprint check FIRST — avoids running stageQuickshellHelperModules(),
    // ensureDirJunction() x3, and scanning 540+ QML files on every warm startup (< 2ms).
    static constexpr const char* kStageVersion = "v47-win-instant-boot-r3";
    QElapsedTimer stageTimer;
    stageTimer.start();

    const bool isDevWorkspace = QFileInfo::exists(QDir(sekaipopDir).filePath(QStringLiteral("../.git")));
    QString expectedHeader;
    if (!isDevWorkspace) {
        // Production install: stat only SekaiPopShell.exe and shell.qml (2 syscalls instead of 544)
        const QFileInfo exeFi(QCoreApplication::applicationFilePath());
        const QFileInfo shellFi(sekaipopDir + QStringLiteral("/shell.qml"));
        expectedHeader = QStringLiteral("%1|prod|%2|%3|%4|%5|%6")
            .arg(QLatin1String(kStageVersion), sekaipopDir, exeDir)
            .arg(exeFi.size())
            .arg(exeFi.lastModified().toMSecsSinceEpoch())
            .arg(shellFi.lastModified().toMSecsSinceEpoch());
    } else {
        qint64 maxMtimeMs = 0;
        int srcFileCount = 0;
        auto scanMtime = [&](const QString& subPath) {
            const QString srcRoot = subPath.isEmpty() ? sekaipopDir : (sekaipopDir + QLatin1Char('/') + subPath);
            QDirIterator it(srcRoot, QDir::Files, subPath.isEmpty() ? QDirIterator::NoIteratorFlags : QDirIterator::Subdirectories);
            while (it.hasNext()) {
                it.next();
                const QFileInfo fi = it.fileInfo();
                const QString ext = fi.suffix().toLower();
                if (ext != QStringLiteral("qml") && ext != QStringLiteral("js")) {
                    continue;
                }
                ++srcFileCount;
                const qint64 mt = fi.lastModified().toMSecsSinceEpoch();
                if (mt > maxMtimeMs) {
                    maxMtimeMs = mt;
                }
            }
        };
        scanMtime(QString());
        scanMtime(QStringLiteral("services"));
        scanMtime(QStringLiteral("modules"));

        expectedHeader = QStringLiteral("%1|%2|%3|%4|%5")
            .arg(QLatin1String(kStageVersion), sekaipopDir, exeDir)
            .arg(srcFileCount)
            .arg(maxMtimeMs);
    }

    const QString cacheFilePath = stageQsDir + QStringLiteral("/.stage_cache");

    {
        QFile cf(cacheFilePath);
        if (cf.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QStringList lines = QString::fromUtf8(cf.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            if (!lines.isEmpty() && lines.first().trimmed() == expectedHeader &&
                QFileInfo::exists(stageQsDir + QStringLiteral("/shell.qml")) &&
                QFileInfo::exists(stageQsDir + QStringLiteral("/DeferredWidgets.qml")) &&
                QFileInfo::exists(stageRoot + QStringLiteral("/Quickshell/Widgets/qmldir")) &&
                QFileInfo::exists(stageQsDir + QStringLiteral("/assets"))) {
                QList<StagedSingletonInfo> cached;
                bool ok = true;
                for (int i = 1; i < lines.size(); ++i) {
                    const QStringList parts = lines.at(i).split(QLatin1Char('\t'));
                    if (parts.size() != 3) {
                        ok = false;
                        break;
                    }
                    StagedSingletonInfo info;
                    info.name = parts.at(0);
                    info.stagedPath = parts.at(1);
                    info.priority = parts.at(2).toInt();
                    cached.append(info);
                }
                if (ok && !cached.isEmpty() && QFileInfo::exists(cached.first().stagedPath)) {
                    qInfo("[QsEngineSetup] Stage cache hit (%d singletons) in %lldms",
                          static_cast<int>(cached.size()), stageTimer.elapsed());
                    return cached;
                }
            }
        }
    }

    QDir().mkpath(stageQsDir);
    stageQuickshellHelperModules(stageRoot);
    ensureDirJunction(stageQsDir + QStringLiteral("/assets"), sekaipopDir + QStringLiteral("/assets"));
    ensureDirJunction(stageQsDir + QStringLiteral("/scripts"), sekaipopDir + QStringLiteral("/scripts"));
    ensureDirJunction(stageQsDir + QStringLiteral("/shaders"), exeDir + QStringLiteral("/shaders"));

    // Stage cache miss (first run or version upgrade): prune stale Qt QML bytecode cache
    // so obsolete .qmlc files from prior versions do not accumulate and slow cold NTFS reads.
    {
        const QString qmlCacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
            + QStringLiteral("/qmlcache");
        QDir qcd(qmlCacheDir);
        if (qcd.exists()) {
            const QFileInfoList stale = qcd.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
            if (stale.size() > 600) {
                for (const QFileInfo& sf : stale) {
                    QFile::remove(sf.absoluteFilePath());
                }
                qInfo("[QsEngineSetup] Pruned %d stale qmlcache entries during re-stage.",
                      static_cast<int>(stale.size()));
            }
        }
    }

    for (const QString& topFile : {QStringLiteral("VERSION"), QStringLiteral("CHANGELOG.md")}) {
        const QString src = sekaipopDir + QLatin1Char('/') + topFile;
        if (QFileInfo::exists(src)) {
            QFile f(src);
            if (f.open(QIODevice::ReadOnly)) {
                writeFileIfChanged(stageQsDir + QLatin1Char('/') + topFile, f.readAll());
            }
        }
    }

    QList<StagedSingletonInfo> singletons;
    QHash<QString, QStringList> dirRegularComponents;

    auto processDir = [&](const QString& subPath) {
        const QString srcRoot = subPath.isEmpty() ? sekaipopDir : (sekaipopDir + QLatin1Char('/') + subPath);
        QDirIterator it(srcRoot, QDir::Files, subPath.isEmpty() ? QDirIterator::NoIteratorFlags : QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString filePath = it.next();
            const QFileInfo fi(filePath);
            const QString ext = fi.suffix().toLower();
            if (ext != QStringLiteral("qml") && ext != QStringLiteral("js")) {
                continue;
            }
            const QString relPath = QDir(sekaipopDir).relativeFilePath(filePath).replace(QLatin1Char('\\'), QLatin1Char('/'));
            const QString dstPath = stageQsDir + QLatin1Char('/') + relPath;

            QFile f(filePath);
            if (!f.open(QIODevice::ReadOnly)) {
                continue;
            }
            QString text = QString::fromUtf8(f.readAll());

            bool isSingleton = false;
            if (ext == QStringLiteral("qml")) {
                const QStringList lines = text.split(QLatin1Char('\n'));
                for (int i = 0; i < qMin(10, static_cast<int>(lines.size())); ++i) {
                    if (lines.at(i).trimmed() == QStringLiteral("pragma Singleton")) {
                        isSingleton = true;
                        break;
                    }
                }

                if (isSingleton) {
                    text.replace(QStringLiteral("pragma Singleton"), QStringLiteral("// pragma Singleton"));
                    if (QFileInfo::exists(dstPath)) {
                        QFile::remove(dstPath);
                    }
                    const QString singletonDstPath = QFileInfo(dstPath).dir().filePath(
                        QStringLiteral("_qs_singleton_") + fi.fileName()
                    );
                    StagedSingletonInfo info;
                    info.name = fi.completeBaseName();
                    info.stagedPath = singletonDstPath;
                    info.priority = singletonPriority(info.name);
                    singletons.append(info);
                } else if (!subPath.isEmpty()) {
                    const QString relDir = QFileInfo(relPath).path().replace(QLatin1Char('\\'), QLatin1Char('/'));
                    dirRegularComponents[relDir].append(fi.completeBaseName());
                }

                // Windows font fallback in ThemeSet.qml
                if (fi.fileName() == QStringLiteral("ThemeSet.qml")) {
                    text.replace(
                        QStringLiteral("readonly property string fontFamily: \"Google Sans Flex\""),
                        QStringLiteral("readonly property string fontFamily: typeof ShellFontFamily !== \"undefined\" ? ShellFontFamily : \"Segoe UI Variable\"")
                    );
                    text.replace(
                        QStringLiteral("readonly property string fontStyleBold: \"ExtraBold\""),
                        QStringLiteral("readonly property string fontStyleBold: fontFamily === \"Google Sans Flex\" ? \"ExtraBold\" : \"\"")
                    );
                }

                // Allow EditMode.open() on Windows even when desktop apps are open,
                // keep full-screen widget windows by default (respecting perf.lowEnd so
                // widgets never resize/jump/stretch swapchains on Edit Desktop enter/exit),
                // and let EditTray/restack settle for 1 frame before enterAnim starts.
                if (fi.fileName() == QStringLiteral("EditMode.qml")) {
                    text.replace(
                        QStringLiteral("    function open(name) {\n"),
                        QStringLiteral("    function open(name) {\n"
                                       "        if (typeof QsDeferredBoot !== \"undefined\" && QsDeferredBoot && !QsDeferredBoot.tier2Ready)\n"
                                       "            QsDeferredBoot.ensureTier2Now()\n")
                    );
                    text.replace(
                        QStringLiteral("if (root.appsOn(name)) {"),
                        QStringLiteral("if (false && root.appsOn(name)) {")
                    );
                    text.replace(
                        QStringLiteral("        exitAnim.stop()\n        enterAnim.restart()"),
                        QStringLiteral("        exitAnim.stop()\n        enterAnim.stop()\n        enterPrep.restart()")
                    );
                    text.replace(
                        QStringLiteral("        enterAnim.stop()\n        exitAnim.restart()"),
                        QStringLiteral("        enterPrep.stop()\n        enterAnim.stop()\n        exitAnim.restart()")
                    );
                    text.replace(
                        QStringLiteral("    NumberAnimation {\n        id: enterAnim"),
                        QStringLiteral("    Timer {\n"
                                       "        id: enterPrep\n"
                                       "        interval: 16\n"
                                       "        onTriggered: {\n"
                                       "            if (root.active)\n"
                                       "                enterAnim.restart()\n"
                                       "        }\n"
                                       "    }\n\n"
                                       "    NumberAnimation {\n        id: enterAnim")
                    );
                    text.replace(
                        QStringLiteral("        duration: 560\n"
                                       "        easing.type: Easing.OutBack\n"
                                       "        easing.overshoot: 1.25"),
                        QStringLiteral("        duration: 360\n"
                                       "        easing.type: Easing.OutBack\n"
                                       "        easing.overshoot: 1.12")
                    );
                    text.replace(
                        QStringLiteral("        duration: 360\n"
                                       "        easing.type: Easing.InOutCubic"),
                        QStringLiteral("        duration: 260\n"
                                       "        easing.type: Easing.OutCubic")
                    );
                    text.replace(
                        QStringLiteral("    // ===== what each widget looks like =====\n"
                                       "    property var previewUrls: ({})\n"
                                       "    property var previewSizes: ({})"),
                        QStringLiteral("    // ===== what each widget looks like =====\n"
                                       "    property var previewUrls: ({})\n"
                                       "    property var previewSizes: ({})\n"
                                       "    readonly property string previewThemeKey: String(ThemeService.current || \"default\") + \"_\"\n"
                                       "        + (ThemeService.dark ? \"d\" : \"l\") + \"_\"\n"
                                       "        + String(Theme.accentMint).replace(\"#\", \"\") + \"_\"\n"
                                       "        + String(Theme.colBorder).replace(\"#\", \"\")\n"
                                       "    function loadCachedPreviews() {\n"
                                       "        const c = Quickshell.loadEditPreviewCache(root.previewThemeKey)\n"
                                       "        if (!c || !c.urls)\n"
                                       "            return\n"
                                       "        const u = Object.assign({}, root.previewUrls)\n"
                                       "        const z = Object.assign({}, root.previewSizes)\n"
                                       "        let changed = false\n"
                                       "        for (const k in c.urls) {\n"
                                       "            if (!u[k]) {\n"
                                       "                u[k] = c.urls[k]\n"
                                       "                z[k] = c.sizes[k]\n"
                                       "                changed = true\n"
                                       "            }\n"
                                       "        }\n"
                                       "        if (changed) {\n"
                                       "            root.previewUrls = u\n"
                                       "            root.previewSizes = z\n"
                                       "        }\n"
                                       "    }\n"
                                       "    onPreviewThemeKeyChanged: {\n"
                                       "        root.previewUrls = ({})\n"
                                       "        root.previewSizes = ({})\n"
                                       "        root.loadCachedPreviews()\n"
                                       "    }\n"
                                       "    Component.onCompleted: root.loadCachedPreviews()")
                    );
                }

                // Qt 6.8 QQuickRectangle drops gradient rendering when Rectangle.color is "transparent"
                // or gradient is assigned via a ternary returning null, causing gradient Pills (ActiveWindow,
                // MediaPill, UserCard) to reveal mt.ringWhite behind them (unreadable dark-on-dark in Darker mode).
                // Render fillGradient on a dedicated opaque fillGradLayer behind fill's content.
                if (fi.fileName() == QStringLiteral("Pill.qml")) {
                    text.replace(
                        QStringLiteral(
                            "        Rectangle {\n"
                            "            id: fill\n"
                            "            anchors.fill: face\n"
                            "            anchors.margins: root.capsule ? 0 : root.ringWidth\n"
                            "            radius: root.capsule ? Math.min(width, height) / 2 : Math.max(0, root.cornerRadius - root.ringWidth)\n"
                            "            color: root.bare ? \"transparent\"\n"
                            "                : (root.capsule\n"
                            "                    ? (root.capKeepsFill ? root.fillColor : mt.capChip)\n"
                            "                    : (root.capRim ? mt.capBody\n"
                            "                        : (root.fillGradient ? \"transparent\" : root.fillColor)))\n"
                            "            gradient: (root.capsule || root.capRim || root.bare) ? null : root.fillGradient"
                        ),
                        QStringLiteral(
                            "        Rectangle {\n"
                            "            id: fillGradLayer\n"
                            "            visible: root.fillGradient !== null && !root.capsule && !root.capRim && !root.bare\n"
                            "            anchors.fill: fill\n"
                            "            radius: fill.radius\n"
                            "            color: \"#ffffff\"\n"
                            "            gradient: root.fillGradient\n"
                            "            antialiasing: true\n"
                            "        }\n"
                            "\n"
                            "        Rectangle {\n"
                            "            id: fill\n"
                            "            anchors.fill: face\n"
                            "            anchors.margins: root.capsule ? 0 : root.ringWidth\n"
                            "            radius: root.capsule ? Math.min(width, height) / 2 : Math.max(0, root.cornerRadius - root.ringWidth)\n"
                            "            color: (root.bare || fillGradLayer.visible) ? \"transparent\"\n"
                            "                : (root.capsule\n"
                            "                    ? (root.capKeepsFill ? root.fillColor : mt.capChip)\n"
                            "                    : (root.capRim ? mt.capBody : root.fillColor))"
                        )
                    );
                }

                // Allow DiscordPill on flat horizontal bars to respect dynamic maxWidth from Status_Bar
                // so it elides cleanly instead of overlapping UtilButtons on narrow/DPI-scaled monitors.
                if (fi.fileName() == QStringLiteral("DiscordPill.qml")) {
                    text.replace(
                        QStringLiteral("    property bool vertical: false"),
                        QStringLiteral("    property bool vertical: false\n"
                                       "    property real maxWidth: 260")
                    );
                    text.replace(
                        QStringLiteral(
                            "            Text {\n"
                            "                anchors.verticalCenter: parent.verticalCenter\n"
                            "                width: Math.min(implicitWidth, 150)\n"
                            "                text: root.labelText\n"
                            "                elide: Text.ElideRight"
                        ),
                        QStringLiteral(
                            "            Text {\n"
                            "                anchors.verticalCenter: parent.verticalCenter\n"
                            "                readonly property real capTextW: Math.max(0, Math.min(150, root.maxWidth - (2 * pill.ringWidth + 14 + (root.shownAuth ? 18 : root.stackWidth) + (!root.shownAuth ? 32 : 7))))\n"
                            "                width: Math.min(implicitWidth, capTextW)\n"
                            "                visible: capTextW >= 18\n"
                            "                text: root.labelText\n"
                            "                elide: Text.ElideRight"
                        )
                    );
                }

                // Monitor 2 Bar parity on Windows DPI scaling (e.g. 1920x1080 @ 125% = 864 logical px):
                // lower vertical tighterV threshold from 880 to 700 so Weather, CPU/RAM, and plugins
                // stay visible identically across Monitor 1 (1.0x) and Monitor 2 (1.25x / 1.5x).
                // Also integrate with Ukikura Dynamic Island: when Ukikura is active at the top-center
                // notch of a monitor, smoothly split Top Bar center pills left/right around Ukikura.
                if (fi.fileName() == QStringLiteral("Status_Bar.qml")) {
                    text.replace(
                        QStringLiteral("readonly property bool tighterV: status_Bar.vertical && content.height < 880"),
                        QStringLiteral("readonly property bool tighterV: status_Bar.vertical && content.height < 700")
                    );
                    text.replace(
                        QStringLiteral("readonly property bool utilFlyout: status_Bar.vertical && (status_Bar.tightV || status_Bar.utilFlyoutAlways)"),
                        QStringLiteral("readonly property bool utilFlyout: status_Bar.vertical ? (status_Bar.tightV || status_Bar.utilFlyoutAlways) : status_Bar.tightH")
                    );
                    text.replace(
                        QStringLiteral(
                            "    property bool vertical: false\n"
                            "    property string barPosition: \"top\""
                        ),
                        QStringLiteral(
                            "    property bool vertical: false\n"
                            "    property string barPosition: \"top\"\n"
                            "    property real ukikuraGap: 0\n"
                            "    Behavior on ukikuraGap {\n"
                            "        NumberAnimation { duration: 220; easing.type: Easing.OutCubic }\n"
                            "    }\n"
                            "    readonly property bool ukikuraActive: !status_Bar.vertical && status_Bar.barPosition === \"top\" && status_Bar.ukikuraGap > 1\n"
                            "    readonly property var centerRightSet: {\n"
                            "        const res = ({})\n"
                            "        if (!status_Bar.ukikuraActive)\n"
                            "            return res\n"
                            "        const raw = status_Bar.layout[\"center\"] || []\n"
                            "        const active = []\n"
                            "        for (const id of raw) {\n"
                            "            if (!status_Bar.shows(id))\n"
                            "                continue\n"
                            "            if (id === \"discord\" && !(status_Bar.discordInCall || status_Bar.discordNeedsAuth || status_Bar.discordAuthorizing))\n"
                            "                continue\n"
                            "            if (id === \"privacy\" && !(status_Bar.privacyMic || status_Bar.privacyAudio || status_Bar.privacyScreen))\n"
                            "                continue\n"
                            "            if (id === \"updates\" && !(status_Bar.updatesCount > 0 || status_Bar.updatesShowZero || status_Bar.updatesLoading))\n"
                            "                continue\n"
                            "            if (id === \"battery\" && !status_Bar.batteryAvailable)\n"
                            "                continue\n"
                            "            if (id === \"keyboardLayout\" && status_Bar.keyboardLayoutCode === \"\")\n"
                            "                continue\n"
                            "            if (id === \"status\" && !(status_Bar.statusNetworkShown || status_Bar.statusBluetoothShown || status_Bar.statusSpeakerShown))\n"
                            "                continue\n"
                            "            if (id === \"tray\" && !(status_Bar.trayAvailable || (status_Bar.trayPinned && status_Bar.trayPinned.length > 0)))\n"
                            "                continue\n"
                            "            if (id === \"media\" && !status_Bar.mediaActive)\n"
                            "                continue\n"
                            "            active.push(id)\n"
                            "        }\n"
                            "        if (active.length <= 1)\n"
                            "            return res\n"
                            "        const est = {\n"
                            "            discord: 260, utilButtons: 250, media: 220, activeWindow: 180,\n"
                            "            workspaces: 170, resources: 132, date: 115, status: 108,\n"
                            "            weather: 88, battery: 86, clock: 76, tray: 64,\n"
                            "            privacy: 56, updates: 56, keyboardLayout: 54, notifications: 46\n"
                            "        }\n"
                            "        const gap = status_Bar.zoneGap\n"
                            "        const wts = active.map(id => est[id] || 96)\n"
                            "        let bestSplit = 1\n"
                            "        let bestDiff = 1e9\n"
                            "        for (let s = 1; s < active.length; ++s) {\n"
                            "            let lw = 0\n"
                            "            let rw = 0\n"
                            "            for (let i = 0; i < s; ++i) lw += wts[i] + (i > 0 ? gap : 0)\n"
                            "            for (let i = s; i < active.length; ++i) rw += wts[i] + (i > s ? gap : 0)\n"
                            "            const d = Math.abs(lw - rw)\n"
                            "            if (d < bestDiff) {\n"
                            "                bestDiff = d\n"
                            "                bestSplit = s\n"
                            "            }\n"
                            "        }\n"
                            "        for (let i = bestSplit; i < active.length; ++i) {\n"
                            "            res[active[i]] = true\n"
                            "            if (active[i] === \"tray\")\n"
                            "                res[\"trayButton\"] = true\n"
                            "        }\n"
                            "        return res\n"
                            "    }\n"
                            "    function zoneFixedWidth(zone, skipId) {\n"
                            "        let w = 0\n"
                            "        for (const id of (status_Bar.layout[zone] || [])) {\n"
                            "            if (id === \"media\" || id === \"discord\" || id === skipId)\n"
                            "                continue\n"
                            "            for (const it of status_Bar.itemsOf(id)) {\n"
                            "                if (it.visible)\n"
                            "                    w += status_Bar.slotWidth(it) + status_Bar.zoneGap\n"
                            "            }\n"
                            "        }\n"
                            "        return w > 0 ? Math.max(0, w - status_Bar.zoneGap) : 0\n"
                            "    }\n"
                            "    function centerSideWidth(onRight, includeDiscordEst) {\n"
                            "        let w = 0\n"
                            "        for (const id of (status_Bar.layout[\"center\"] || [])) {\n"
                            "            if (id === \"media\" || (!!status_Bar.centerRightSet[id]) !== onRight)\n"
                            "                continue\n"
                            "            if (id === \"discord\") {\n"
                            "                if (includeDiscordEst && status_Bar.shows(\"discord\") && (status_Bar.discordInCall || status_Bar.discordNeedsAuth || status_Bar.discordAuthorizing))\n"
                            "                    w += 235 + status_Bar.zoneGap\n"
                            "                continue\n"
                            "            }\n"
                            "            for (const it of status_Bar.itemsOf(id)) {\n"
                            "                if (it.visible)\n"
                            "                    w += status_Bar.slotWidth(it) + status_Bar.zoneGap\n"
                            "            }\n"
                            "        }\n"
                            "        return w > 0 ? Math.max(0, w - status_Bar.zoneGap) : 0\n"
                            "    }\n"
                            "    // On narrow/DPI-scaled horizontal Top Bars (e.g. 1536px @ 125% with Ukikura + Discord),\n"
                            "    // automatically fold UtilButtons into its single compact tools flyout button to prevent overlap.\n"
                            "    readonly property bool tightH: {\n"
                            "        if (status_Bar.vertical || !status_Bar.ukikuraActive || !status_Bar.shows(\"utilButtons\"))\n"
                            "            return false\n"
                            "        const uz = status_Bar.zoneMap[\"utilButtons\"]\n"
                            "        if (uz !== \"right\" && uz !== \"left\")\n"
                            "            return false\n"
                            "        const half = content.width / 2\n"
                            "        const onRight = (uz === \"right\")\n"
                            "        const otherSide = status_Bar.zoneFixedWidth(uz, \"utilButtons\")\n"
                            "        const expandedUtil = 264 + (otherSide > 0 ? status_Bar.zoneGap : 0)\n"
                            "        const centerNeed = status_Bar.centerSideWidth(onRight, true)\n"
                            "        const halfGap = Math.max(108, Math.min(205, status_Bar.ukikuraGap / 2))\n"
                            "        return (otherSide + expandedUtil + centerNeed + halfGap + 16) > half\n"
                            "    }\n"
                            "    readonly property real effectiveUkikuraGap: {\n"
                            "        if (!status_Bar.ukikuraActive)\n"
                            "            return 0\n"
                            "        const half = content.width / 2\n"
                            "        const rightFixed = status_Bar.zoneFixedWidth(\"right\", \"\")\n"
                            "        const leftFixed = status_Bar.zoneFixedWidth(\"left\", \"\")\n"
                            "        const rightSideRoom = half - rightFixed - status_Bar.centerSideWidth(true, true) - 10\n"
                            "        const leftSideRoom = half - leftFixed - status_Bar.centerSideWidth(false, true) - 10\n"
                            "        const safeRoom = 2 * Math.max(94, Math.min(rightSideRoom, leftSideRoom))\n"
                            "        const maxCap = Math.min(410, Math.min(content.width * 0.24, safeRoom))\n"
                            "        return Math.max(status_Bar.zoneGap, Math.min(status_Bar.ukikuraGap, maxCap))\n"
                            "    }\n"
                            "    readonly property real maxDiscordWidth: {\n"
                            "        if (status_Bar.vertical)\n"
                            "            return 260\n"
                            "        const half = content.width / 2\n"
                            "        const dz = status_Bar.zoneMap[\"discord\"]\n"
                            "        let room = 260\n"
                            "        if (status_Bar.ukikuraActive && dz === \"center\") {\n"
                            "            const onRight = !!status_Bar.centerRightSet[\"discord\"]\n"
                            "            const edgeFixed = status_Bar.zoneFixedWidth(onRight ? \"right\" : \"left\", \"\")\n"
                            "            const sibFixed = status_Bar.centerSideWidth(onRight, false)\n"
                            "            const sibGap = sibFixed > 0 ? status_Bar.zoneGap : 0\n"
                            "            room = half - status_Bar.effectiveUkikuraGap / 2 - edgeFixed - sibFixed - sibGap - 16\n"
                            "        } else if (dz === \"right\") {\n"
                            "            const edgeFixed = status_Bar.zoneFixedWidth(\"right\", \"\")\n"
                            "            const centerEdge = status_Bar.ukikuraActive\n"
                            "                ? (status_Bar.effectiveUkikuraGap / 2 + status_Bar.centerSideWidth(true, false))\n"
                            "                : (centerGroup.width / 2)\n"
                            "            room = half - centerEdge - edgeFixed - 18\n"
                            "        }\n"
                            "        return Math.max(96, Math.min(260, room))\n"
                            "    }"
                        )
                    );
                    text.replace(
                        QStringLiteral(
                            "            id: discordPill\n"
                            "            parent: status_Bar.zoneItem(\"discord\")"
                        ),
                        QStringLiteral(
                            "            id: discordPill\n"
                            "            parent: status_Bar.zoneItem(\"discord\")\n"
                            "            maxWidth: status_Bar.maxDiscordWidth"
                        )
                    );
                    text.replace(
                        QStringLiteral(
                            "    function zoneItem(key) {\n"
                            "        const z = status_Bar.zoneMap[key]\n"
                            "        return z === \"left\" ? leftGroup : (z === \"center\" ? centerGroup : rightGroup)\n"
                            "    }"
                        ),
                        QStringLiteral(
                            "    function zoneItem(key) {\n"
                            "        const z = status_Bar.zoneMap[key]\n"
                            "        if (z === \"left\") return leftGroup\n"
                            "        if (z === \"center\") return (status_Bar.ukikuraActive && status_Bar.centerRightSet[key]) ? centerRightGroup : centerGroup\n"
                            "        return rightGroup\n"
                            "    }"
                        )
                    );
                    text.replace(
                        QStringLiteral(
                            "        if (z === \"left\") {\n"
                            "            room = content.width - rightGroup.width - 12 - status_Bar.othersWidth(\"left\")\n"
                            "            if (centerGroup.width > 0)\n"
                            "                room = Math.min(room, half - centerGroup.width / 2 - 12 - status_Bar.othersWidth(\"left\"))\n"
                            "        } else if (z === \"right\") {\n"
                            "            room = content.width - leftGroup.width - 12 - status_Bar.othersWidth(\"right\")\n"
                            "            if (centerGroup.width > 0)\n"
                            "                room = Math.min(room, half - centerGroup.width / 2 - 12 - status_Bar.othersWidth(\"right\"))\n"
                            "        } else {\n"
                            "            room = 2 * (Math.min(half - leftGroup.width, half - rightGroup.width) - 12)\n"
                            "                - status_Bar.othersWidth(\"center\")\n"
                            "        }"
                        ),
                        QStringLiteral(
                            "        if (z === \"left\") {\n"
                            "            room = content.width - rightGroup.width - 12 - status_Bar.othersWidth(\"left\")\n"
                            "            if (status_Bar.ukikuraActive)\n"
                            "                room = Math.min(room, half - status_Bar.effectiveUkikuraGap / 2 - centerGroup.width - 12 - status_Bar.othersWidth(\"left\"))\n"
                            "            else if (centerGroup.width > 0)\n"
                            "                room = Math.min(room, half - centerGroup.width / 2 - 12 - status_Bar.othersWidth(\"left\"))\n"
                            "        } else if (z === \"right\") {\n"
                            "            room = content.width - leftGroup.width - 12 - status_Bar.othersWidth(\"right\")\n"
                            "            if (status_Bar.ukikuraActive)\n"
                            "                room = Math.min(room, half - status_Bar.effectiveUkikuraGap / 2 - centerRightGroup.width - 12 - status_Bar.othersWidth(\"right\"))\n"
                            "            else if (centerGroup.width > 0)\n"
                            "                room = Math.min(room, half - centerGroup.width / 2 - 12 - status_Bar.othersWidth(\"right\"))\n"
                            "        } else if (status_Bar.ukikuraActive) {\n"
                            "            const onRight = !!status_Bar.centerRightSet[\"media\"]\n"
                            "            const sideEdge = onRight ? rightGroup.width : leftGroup.width\n"
                            "            room = half - status_Bar.effectiveUkikuraGap / 2 - sideEdge - 14 - status_Bar.centerSideWidth(onRight, true)\n"
                            "        } else {\n"
                            "            room = 2 * (Math.min(half - leftGroup.width, half - rightGroup.width) - 12)\n"
                            "                - status_Bar.othersWidth(\"center\")\n"
                            "        }"
                        )
                    );
                    text.replace(
                        QStringLiteral(
                            "        GridLayout {\n"
                            "            id: centerGroup\n"
                            "            anchors.centerIn: parent\n"
                            "            visible: !status_Bar.vertical\n"
                            "                || content.height > leftGroup.height + rightGroup.height + centerGroup.implicitHeight + 24\n"
                            "            columns: status_Bar.vertical ? 1 : 100\n"
                            "            columnSpacing: status_Bar.zoneGap\n"
                            "            rowSpacing: status_Bar.vertical ? status_Bar.vGap : status_Bar.zoneGap\n"
                            "        }"
                        ),
                        QStringLiteral(
                            "        GridLayout {\n"
                            "            id: centerGroup\n"
                            "            x: status_Bar.ukikuraActive\n"
                            "                ? Math.max(0, parent.width / 2 - status_Bar.effectiveUkikuraGap / 2 - width)\n"
                            "                : (parent.width - width) / 2\n"
                            "            y: (parent.height - height) / 2\n"
                            "            visible: !status_Bar.vertical\n"
                            "                || content.height > leftGroup.height + rightGroup.height + centerGroup.implicitHeight + 8\n"
                            "            columns: status_Bar.vertical ? 1 : 100\n"
                            "            columnSpacing: status_Bar.zoneGap\n"
                            "            rowSpacing: status_Bar.vertical ? status_Bar.vGap : status_Bar.zoneGap\n"
                            "        }\n"
                            "\n"
                            "        GridLayout {\n"
                            "            id: centerRightGroup\n"
                            "            x: Math.min(Math.max(0, parent.width - width), parent.width / 2 + status_Bar.effectiveUkikuraGap / 2)\n"
                            "            y: (parent.height - height) / 2\n"
                            "            visible: status_Bar.ukikuraActive && children.length > 0\n"
                            "            columns: 100\n"
                            "            columnSpacing: status_Bar.zoneGap\n"
                            "            rowSpacing: status_Bar.zoneGap\n"
                            "        }"
                        )
                    );
                }

                // Native Windows Discord Voice bridge (via \\.\pipe\discord-ipc-0..9 inside QsProcess):
                // auto-activate so tray-minimized Discord is detected without waiting for a visible window,
                // and surface Discord when authorizing or clicking Open Discord.
                if (fi.fileName() == QStringLiteral("DiscordVoiceService.qml")) {
                    text.replace(
                        QStringLiteral("readonly property var clientClasses: [\"discord\", \"discord-canary\","),
                        QStringLiteral("readonly property var clientClasses: [\"discord\", \"discord.exe\", \"discord-canary\",")
                    );
                    text.replace(
                        QStringLiteral("    onClientSeenChanged: if (root.clientSeen) root.start(false)"),
                        QStringLiteral("    onClientSeenChanged: if (root.clientSeen) root.start(false)\n"
                                       "    Component.onCompleted: Qt.callLater(() => { root.clientSeen = true })")
                    );
                    text.replace(
                        QStringLiteral("    function authorizeAfterFocusRelease() { focusReleaseDelay.restart() }"),
                        QStringLiteral("    function authorizeAfterFocusRelease() { root.focusClient(); focusReleaseDelay.restart() }")
                    );
                    text.replace(
                        QStringLiteral("        if (address === \"\")\n            return false"),
                        QStringLiteral("        if (address === \"\") {\n"
                                       "            try {\n"
                                       "                const entry = DesktopEntries.heuristicLookup(\"discord\")\n"
                                       "                if (entry) { entry.execute(); return true }\n"
                                       "            } catch (e) {}\n"
                                       "            return false\n"
                                       "        }")
                    );
                }

                // Lazy-load the 5 secondary tabs in EditTray.qml (Bar, Dock, Views, Theme, Presets = ~134 KB QML)
                // so entering Edit Desktop only compiles/instantiates the active tab instead of all 6 boards at once.
                if (fi.fileName() == QStringLiteral("EditTray.qml")) {
                    text.replace(
                        QStringLiteral(
                            "            // ===== the bar tab =====\n"
                            "            EditBarLayout {\n"
                            "                id: barEditor\n"
                            "                visible: win.tab === \"bar\"\n"
                            "                anchors.left: tabs.right\n"
                            "                anchors.leftMargin: 14\n"
                            "                anchors.right: dividerRight.left\n"
                            "                anchors.rightMargin: 14\n"
                            "                anchors.top: parent.top\n"
                            "                anchors.topMargin: 14\n"
                            "                anchors.bottom: parent.bottom\n"
                            "                anchors.bottomMargin: 14\n"
                            "            }\n"
                            "\n"
                            "            // ===== the dock tab =====\n"
                            "            EditDockBoard {\n"
                            "                visible: win.tab === \"dock\"\n"
                            "                anchors.left: tabs.right\n"
                            "                anchors.leftMargin: 14\n"
                            "                anchors.right: dividerRight.left\n"
                            "                anchors.rightMargin: 14\n"
                            "                anchors.top: parent.top\n"
                            "                anchors.topMargin: 14\n"
                            "                anchors.bottom: parent.bottom\n"
                            "                anchors.bottomMargin: 14\n"
                            "            }\n"
                            "\n"
                            "            // ===== the views tab =====\n"
                            "            EditViewsBoard {\n"
                            "                visible: win.tab === \"views\"\n"
                            "                anchors.left: tabs.right\n"
                            "                anchors.leftMargin: 14\n"
                            "                anchors.right: dividerRight.left\n"
                            "                anchors.rightMargin: 14\n"
                            "                anchors.top: parent.top\n"
                            "                anchors.topMargin: 14\n"
                            "                anchors.bottom: parent.bottom\n"
                            "                anchors.bottomMargin: 14\n"
                            "            }\n"
                            "\n"
                            "            // ===== the theme tab =====\n"
                            "            EditThemeBoard {\n"
                            "                id: themeBoard\n"
                            "                visible: win.tab === \"theme\"\n"
                            "                anchors.left: tabs.right\n"
                            "                anchors.leftMargin: 14\n"
                            "                anchors.right: dividerRight.left\n"
                            "                anchors.rightMargin: 14\n"
                            "                anchors.top: parent.top\n"
                            "                anchors.bottom: parent.bottom\n"
                            "\n"
                            "                // The Matugen controls' hex field took the keyboard; give it back.\n"
                            "                onFinished: escKeys.forceActiveFocus()\n"
                            "            }\n"
                            "\n"
                            "            // ===== the presets tab =====\n"
                            "            EditPresetBoard {\n"
                            "                visible: win.tab === \"presets\"\n"
                            "                anchors.left: tabs.right\n"
                            "                anchors.leftMargin: 14\n"
                            "                anchors.right: dividerRight.left\n"
                            "                anchors.rightMargin: 14\n"
                            "                anchors.top: parent.top\n"
                            "                anchors.bottom: parent.bottom\n"
                            "\n"
                            "                // The name field took the keyboard; give it back so Esc\n"
                            "                // still leaves Edit Desktop.\n"
                            "                onFinished: escKeys.forceActiveFocus()\n"
                            "            }"
                        ),
                        QStringLiteral(
                            "            // ===== lazy-loaded secondary tabs =====\n"
                            "            Loader {\n"
                            "                id: barEditor\n"
                            "                property bool loaded: false\n"
                            "                active: loaded || win.tab === \"bar\"\n"
                            "                Component.onCompleted: if (win.tab === \"bar\") loaded = true\n"
                            "                Connections { target: win; function onTabChanged() { if (win.tab === \"bar\") barEditor.loaded = true } }\n"
                            "                visible: win.tab === \"bar\"\n"
                            "                source: active ? Qt.resolvedUrl(\"EditBarLayout.qml\") : \"\"\n"
                            "                anchors.left: tabs.right\n"
                            "                anchors.leftMargin: 14\n"
                            "                anchors.right: dividerRight.left\n"
                            "                anchors.rightMargin: 14\n"
                            "                anchors.top: parent.top\n"
                            "                anchors.topMargin: 14\n"
                            "                anchors.bottom: parent.bottom\n"
                            "                anchors.bottomMargin: 14\n"
                            "            }\n"
                            "\n"
                            "            Loader {\n"
                            "                id: dockEditor\n"
                            "                property bool loaded: false\n"
                            "                active: loaded || win.tab === \"dock\"\n"
                            "                Component.onCompleted: if (win.tab === \"dock\") loaded = true\n"
                            "                Connections { target: win; function onTabChanged() { if (win.tab === \"dock\") dockEditor.loaded = true } }\n"
                            "                visible: win.tab === \"dock\"\n"
                            "                source: active ? Qt.resolvedUrl(\"EditDockBoard.qml\") : \"\"\n"
                            "                anchors.left: tabs.right\n"
                            "                anchors.leftMargin: 14\n"
                            "                anchors.right: dividerRight.left\n"
                            "                anchors.rightMargin: 14\n"
                            "                anchors.top: parent.top\n"
                            "                anchors.topMargin: 14\n"
                            "                anchors.bottom: parent.bottom\n"
                            "                anchors.bottomMargin: 14\n"
                            "            }\n"
                            "\n"
                            "            Loader {\n"
                            "                id: viewsEditor\n"
                            "                property bool loaded: false\n"
                            "                active: loaded || win.tab === \"views\"\n"
                            "                Component.onCompleted: if (win.tab === \"views\") loaded = true\n"
                            "                Connections { target: win; function onTabChanged() { if (win.tab === \"views\") viewsEditor.loaded = true } }\n"
                            "                visible: win.tab === \"views\"\n"
                            "                source: active ? Qt.resolvedUrl(\"EditViewsBoard.qml\") : \"\"\n"
                            "                anchors.left: tabs.right\n"
                            "                anchors.leftMargin: 14\n"
                            "                anchors.right: dividerRight.left\n"
                            "                anchors.rightMargin: 14\n"
                            "                anchors.top: parent.top\n"
                            "                anchors.topMargin: 14\n"
                            "                anchors.bottom: parent.bottom\n"
                            "                anchors.bottomMargin: 14\n"
                            "            }\n"
                            "\n"
                            "            Loader {\n"
                            "                id: themeBoard\n"
                            "                property bool loaded: false\n"
                            "                active: loaded || win.tab === \"theme\"\n"
                            "                Component.onCompleted: if (win.tab === \"theme\") loaded = true\n"
                            "                Connections { target: win; function onTabChanged() { if (win.tab === \"theme\") themeBoard.loaded = true } }\n"
                            "                visible: win.tab === \"theme\"\n"
                            "                property bool tuning: false\n"
                            "                onTuningChanged: if (item && item.tuning !== tuning) item.tuning = tuning\n"
                            "                readonly property string screenName: (item && item.screenName) ? item.screenName : (win.screen ? win.screen.name : \"\")\n"
                            "                source: active ? Qt.resolvedUrl(\"EditThemeBoard.qml\") : \"\"\n"
                            "                onLoaded: if (item) item.tuning = tuning\n"
                            "                anchors.left: tabs.right\n"
                            "                anchors.leftMargin: 14\n"
                            "                anchors.right: dividerRight.left\n"
                            "                anchors.rightMargin: 14\n"
                            "                anchors.top: parent.top\n"
                            "                anchors.bottom: parent.bottom\n"
                            "                Connections {\n"
                            "                    target: themeBoard.item\n"
                            "                    ignoreUnknownSignals: true\n"
                            "                    function onFinished() { escKeys.forceActiveFocus() }\n"
                            "                    function onTuningChanged() { if (themeBoard.item) themeBoard.tuning = themeBoard.item.tuning }\n"
                            "                }\n"
                            "            }\n"
                            "\n"
                            "            Loader {\n"
                            "                id: presetBoard\n"
                            "                property bool loaded: false\n"
                            "                active: loaded || win.tab === \"presets\"\n"
                            "                Component.onCompleted: if (win.tab === \"presets\") loaded = true\n"
                            "                Connections { target: win; function onTabChanged() { if (win.tab === \"presets\") presetBoard.loaded = true } }\n"
                            "                visible: win.tab === \"presets\"\n"
                            "                source: active ? Qt.resolvedUrl(\"EditPresetBoard.qml\") : \"\"\n"
                            "                anchors.left: tabs.right\n"
                            "                anchors.leftMargin: 14\n"
                            "                anchors.right: dividerRight.left\n"
                            "                anchors.rightMargin: 14\n"
                            "                anchors.top: parent.top\n"
                            "                anchors.bottom: parent.bottom\n"
                            "                Connections {\n"
                            "                    target: presetBoard.item\n"
                            "                    ignoreUnknownSignals: true\n"
                            "                    function onFinished() { escKeys.forceActiveFocus() }\n"
                            "                }\n"
                            "            }"
                        )
                    );
                }

                // Lazy-load EditMatugenControls (35 KB + Canvas + infinite animations) only when tuning is opened.
                if (fi.fileName() == QStringLiteral("EditThemeBoard.qml")) {
                    text.replace(
                        QStringLiteral(
                            "    // The Matugen controls, in the row's place while tuning.\n"
                            "    EditMatugenControls {\n"
                            "        anchors.fill: parent\n"
                            "        visible: root.tuning\n"
                            "        onBack: root.tuning = false\n"
                            "        onFinished: root.finished()\n"
                            "    }"
                        ),
                        QStringLiteral(
                            "    // Lazy-loaded Matugen controls: zero QML/Canvas cost until tuning is opened.\n"
                            "    Loader {\n"
                            "        id: matugenLoader\n"
                            "        anchors.fill: parent\n"
                            "        active: root.tuning\n"
                            "        visible: root.tuning\n"
                            "        source: active ? Qt.resolvedUrl(\"EditMatugenControls.qml\") : \"\"\n"
                            "        Connections {\n"
                            "            target: matugenLoader.item\n"
                            "            ignoreUnknownSignals: true\n"
                            "            function onBack() { root.tuning = false }\n"
                            "            function onFinished() { root.finished() }\n"
                            "        }\n"
                            "    }"
                        )
                    );
                }

                // Replace double-FBO layer.enabled + MultiEffect rounded masks in EditPresetBoard with hardware RoundClip.
                if (fi.fileName() == QStringLiteral("EditPresetBoard.qml")) {
                    text.replace(
                        QStringLiteral(
                            "                    Item {\n"
                            "                        id: photoClip\n"
                            "                        x: frame.border.width\n"
                            "                        y: frame.border.width\n"
                            "                        width: frame.width - frame.border.width * 2\n"
                            "                        height: frame.height - frame.border.width * 2\n"
                            "\n"
                            "                        layer.enabled: true\n"
                            "                        layer.smooth: true\n"
                            "                        layer.textureSize: Qt.size(Math.max(1, Math.ceil(width * 2)),\n"
                            "                                                   Math.max(1, Math.ceil(height * 2)))\n"
                            "                        layer.effect: MultiEffect {\n"
                            "                            maskEnabled: true\n"
                            "                            maskSource: photoMask\n"
                            "                        }\n"
                            "\n"
                            "                        Image {\n"
                            "                            id: photo\n"
                            "                            anchors.fill: parent\n"
                            "                            source: tile.picture\n"
                            "                            fillMode: Image.PreserveAspectCrop\n"
                            "                            asynchronous: true\n"
                            "                            cache: false\n"
                            "                            smooth: true\n"
                            "                            sourceSize.width: 320\n"
                            "                            opacity: status === Image.Ready ? 1 : 0\n"
                            "\n"
                            "                            Behavior on opacity {\n"
                            "                                NumberAnimation {\n"
                            "                                    duration: 180\n"
                            "                                }\n"
                            "                            }\n"
                            "                        }\n"
                            "                    }\n"
                            "\n"
                            "                    Item {\n"
                            "                        id: photoMask\n"
                            "                        x: photoClip.x\n"
                            "                        y: photoClip.y\n"
                            "                        width: photoClip.width\n"
                            "                        height: photoClip.height\n"
                            "                        visible: false\n"
                            "                        layer.enabled: true\n"
                            "                        layer.smooth: true\n"
                            "                        layer.textureSize: Qt.size(Math.max(1, Math.ceil(width * 2)),\n"
                            "                                                   Math.max(1, Math.ceil(height * 2)))\n"
                            "\n"
                            "                        Rectangle {\n"
                            "                            anchors.fill: parent\n"
                            "                            radius: 7\n"
                            "                            color: \"black\"\n"
                            "                            antialiasing: true\n"
                            "                        }\n"
                            "                    }"
                        ),
                        QStringLiteral(
                            "                    RoundClip {\n"
                            "                        id: photoClip\n"
                            "                        x: frame.border.width\n"
                            "                        y: frame.border.width\n"
                            "                        width: frame.width - frame.border.width * 2\n"
                            "                        height: frame.height - frame.border.width * 2\n"
                            "                        radius: 7\n"
                            "\n"
                            "                        Image {\n"
                            "                            id: photo\n"
                            "                            anchors.fill: parent\n"
                            "                            source: tile.picture\n"
                            "                            fillMode: Image.PreserveAspectCrop\n"
                            "                            asynchronous: true\n"
                            "                            cache: false\n"
                            "                            smooth: true\n"
                            "                            sourceSize.width: 320\n"
                            "                            opacity: status === Image.Ready ? 1 : 0\n"
                            "\n"
                            "                            Behavior on opacity {\n"
                            "                                NumberAnimation {\n"
                            "                                    duration: 180\n"
                            "                                }\n"
                            "                            }\n"
                            "                        }\n"
                            "                    }"
                        )
                    );
                }

                // Optimize EditGuides grid Canvas: lazy paint on first drag/grid visibility
                // and batch ~20,000 individual Canvas draw calls into 14 batched paths.
                if (fi.fileName() == QStringLiteral("EditGuides.qml")) {
                    text.replace(
                        QStringLiteral(
                            "                    Canvas {\n"
                            "                        id: cv\n"
                            "                        anchors.fill: parent\n"
                            "\n"
                            "                        onPaint: {\n"
                            "                            const ctx = getContext(\"2d\")\n"
                            "                            ctx.reset()\n"
                            "                            const w = width\n"
                            "                            const h = height\n"
                            "                            if (w < 64 || h < 64)\n"
                            "                                return\n"
                            "\n"
                            "                            const c = EditGrid.cell\n"
                            "                            const navy = Qt.color(mt.colBorder)\n"
                            "                            const cyan = Qt.color(mt.accentCyan)\n"
                            "                            const mint = Qt.color(mt.accentMint)\n"
                            "                            const rgba = (col, a) => \"rgba(\" + Math.round(col.r * 255) + \",\"\n"
                            "                                + Math.round(col.g * 255) + \",\" + Math.round(col.b * 255) + \",\" + a + \")\"\n"
                            "\n"
                            "                            // hairlines: a navy shadow, then white on it, so they read on\n"
                            "                            // a light wallpaper and a dark one alike. Three weights: every\n"
                            "                            // cell (fine), every second one (medium, the old 40-px grid),\n"
                            "                            // every EditGrid.major-th (bar).\n"
                            "                            const maj = EditGrid.major\n"
                            "                            const tier = n => (n % maj === 0) ? 2 : ((n % 2 === 0) ? 1 : 0)\n"
                            "                            const navyA = [0.07, 0.11, 0.20]\n"
                            "                            const whiteA = [0.08, 0.13, 0.26]\n"
                            "                            const lw = [1, 1.5, 2]\n"
                            "                            for (let pass = 0; pass < 2; pass++) {\n"
                            "                                for (let i = 1; i * c < w - 1; i++) {\n"
                            "                                    const t = tier(i)\n"
                            "                                    ctx.strokeStyle = pass === 0 ? rgba(navy, navyA[t])\n"
                            "                                                                 : \"rgba(255,255,255,\" + whiteA[t] + \")\"\n"
                            "                                    ctx.lineWidth = lw[t]\n"
                            "                                    const x = i * c + (pass === 0 ? 1 : 0)\n"
                            "                                    ctx.beginPath()\n"
                            "                                    ctx.moveTo(x, 0)\n"
                            "                                    ctx.lineTo(x, h)\n"
                            "                                    ctx.stroke()\n"
                            "                                }\n"
                            "                                for (let j = 1; j * c < h - 1; j++) {\n"
                            "                                    const t = tier(j)\n"
                            "                                    ctx.strokeStyle = pass === 0 ? rgba(navy, navyA[t])\n"
                            "                                                                 : \"rgba(255,255,255,\" + whiteA[t] + \")\"\n"
                            "                                    ctx.lineWidth = lw[t]\n"
                            "                                    const y = j * c + (pass === 0 ? 1 : 0)\n"
                            "                                    ctx.beginPath()\n"
                            "                                    ctx.moveTo(0, y)\n"
                            "                                    ctx.lineTo(w, y)\n"
                            "                                    ctx.stroke()\n"
                            "                                }\n"
                            "                            }\n"
                            "\n"
                            "                            // diamonds on the crossings: a big one where two bars meet, a\n"
                            "                            // medium one where two medium lines do (the old grid's), and a\n"
                            "                            // small faint one on the new in-between crossings.\n"
                            "                            for (let i = 1; i * c < w - 1; i++) {\n"
                            "                                for (let j = 1; j * c < h - 1; j++) {\n"
                            "                                    const big = i % maj === 0 && j % maj === 0\n"
                            "                                    const mid = !big && i % 2 === 0 && j % 2 === 0\n"
                            "                                    const r = big ? 6 : (mid ? 3.4 : 1.9)\n"
                            "                                    const x = i * c\n"
                            "                                    const y = j * c\n"
                            "                                    ctx.fillStyle = rgba(navy, big ? 0.30 : (mid ? 0.30 : 0.16))\n"
                            "                                    ctx.beginPath()\n"
                            "                                    ctx.moveTo(x, y - r - 1.2)\n"
                            "                                    ctx.lineTo(x + r + 1.2, y)\n"
                            "                                    ctx.lineTo(x, y + r + 1.2)\n"
                            "                                    ctx.lineTo(x - r - 1.2, y)\n"
                            "                                    ctx.closePath()\n"
                            "                                    ctx.fill()\n"
                            "                                    ctx.fillStyle = rgba(((i + j) % 2 === 0) ? mint : cyan, big ? 0.9 : (mid ? 0.62 : 0.4))\n"
                            "                                    ctx.beginPath()\n"
                            "                                    ctx.moveTo(x, y - r)\n"
                            "                                    ctx.lineTo(x + r, y)\n"
                            "                                    ctx.lineTo(x, y + r)\n"
                            "                                    ctx.lineTo(x - r, y)\n"
                            "                                    ctx.closePath()\n"
                            "                                    ctx.fill()\n"
                            "                                }\n"
                            "                            }\n"
                            "                        }\n"
                            "\n"
                            "                        onWidthChanged: requestPaint()\n"
                            "                        onHeightChanged: requestPaint()\n"
                            "                        Component.onCompleted: requestPaint()\n"
                            "\n"
                            "                        Connections {\n"
                            "                            target: Theme\n"
                            "\n"
                            "                            function onColBorderChanged() { cv.requestPaint() }\n"
                            "                            function onAccentCyanChanged() { cv.requestPaint() }\n"
                            "                            function onAccentMintChanged() { cv.requestPaint() }\n"
                            "                        }\n"
                            "                    }"
                        ),
                        QStringLiteral(
                            "                    Canvas {\n"
                            "                        id: cv\n"
                            "                        anchors.fill: parent\n"
                            "                        property bool painted: false\n"
                            "\n"
                            "                        function ensurePaint() {\n"
                            "                            if (gridLayer.want > 0) {\n"
                            "                                painted = true\n"
                            "                                requestPaint()\n"
                            "                            }\n"
                            "                        }\n"
                            "\n"
                            "                        onPaint: {\n"
                            "                            const ctx = getContext(\"2d\")\n"
                            "                            ctx.reset()\n"
                            "                            if (!painted)\n"
                            "                                return\n"
                            "                            const w = width\n"
                            "                            const h = height\n"
                            "                            if (w < 64 || h < 64)\n"
                            "                                return\n"
                            "\n"
                            "                            const c = EditGrid.cell\n"
                            "                            const navy = Qt.color(mt.colBorder)\n"
                            "                            const cyan = Qt.color(mt.accentCyan)\n"
                            "                            const mint = Qt.color(mt.accentMint)\n"
                            "                            const rgba = (col, a) => \"rgba(\" + Math.round(col.r * 255) + \",\"\n"
                            "                                + Math.round(col.g * 255) + \",\" + Math.round(col.b * 255) + \",\" + a + \")\"\n"
                            "\n"
                            "                            const maj = EditGrid.major\n"
                            "                            const tier = n => (n % maj === 0) ? 2 : ((n % 2 === 0) ? 1 : 0)\n"
                            "                            const navyA = [0.07, 0.11, 0.20]\n"
                            "                            const whiteA = [0.08, 0.13, 0.26]\n"
                            "                            const lw = [1, 1.5, 2]\n"
                            "                            for (let pass = 0; pass < 2; pass++) {\n"
                            "                                const off = pass === 0 ? 1 : 0\n"
                            "                                for (let t = 0; t < 3; t++) {\n"
                            "                                    ctx.strokeStyle = pass === 0 ? rgba(navy, navyA[t])\n"
                            "                                                                 : \"rgba(255,255,255,\" + whiteA[t] + \")\"\n"
                            "                                    ctx.lineWidth = lw[t]\n"
                            "                                    ctx.beginPath()\n"
                            "                                    for (let i = 1; i * c < w - 1; i++) {\n"
                            "                                        if (tier(i) !== t) continue\n"
                            "                                        const x = i * c + off\n"
                            "                                        ctx.moveTo(x, 0)\n"
                            "                                        ctx.lineTo(x, h)\n"
                            "                                    }\n"
                            "                                    for (let j = 1; j * c < h - 1; j++) {\n"
                            "                                        if (tier(j) !== t) continue\n"
                            "                                        const y = j * c + off\n"
                            "                                        ctx.moveTo(0, y)\n"
                            "                                        ctx.lineTo(w, y)\n"
                            "                                    }\n"
                            "                                    ctx.stroke()\n"
                            "                                }\n"
                            "                            }\n"
                            "\n"
                            "                            const addDiamond = (x, y, rad) => {\n"
                            "                                ctx.moveTo(x, y - rad)\n"
                            "                                ctx.lineTo(x + rad, y)\n"
                            "                                ctx.lineTo(x, y + rad)\n"
                            "                                ctx.lineTo(x - rad, y)\n"
                            "                                ctx.closePath()\n"
                            "                            }\n"
                            "                            for (let grp = 0; grp < 2; grp++) {\n"
                            "                                ctx.fillStyle = rgba(navy, grp === 0 ? 0.30 : 0.16)\n"
                            "                                ctx.beginPath()\n"
                            "                                for (let i = 1; i * c < w - 1; i++) {\n"
                            "                                    for (let j = 1; j * c < h - 1; j++) {\n"
                            "                                        const big = i % maj === 0 && j % maj === 0\n"
                            "                                        const mid = !big && i % 2 === 0 && j % 2 === 0\n"
                            "                                        if ((grp === 0) !== (big || mid)) continue\n"
                            "                                        const r = (big ? 6 : (mid ? 3.4 : 1.9)) + 1.2\n"
                            "                                        addDiamond(i * c, j * c, r)\n"
                            "                                    }\n"
                            "                                }\n"
                            "                                ctx.fill()\n"
                            "                            }\n"
                            "                            const radii = [1.9, 3.4, 6]\n"
                            "                            const alphas = [0.4, 0.62, 0.9]\n"
                            "                            for (let colIdx = 0; colIdx < 2; colIdx++) {\n"
                            "                                const col = colIdx === 0 ? mint : cyan\n"
                            "                                for (let t = 0; t < 3; t++) {\n"
                            "                                    ctx.fillStyle = rgba(col, alphas[t])\n"
                            "                                    ctx.beginPath()\n"
                            "                                    const r = radii[t]\n"
                            "                                    for (let i = 1; i * c < w - 1; i++) {\n"
                            "                                        for (let j = 1; j * c < h - 1; j++) {\n"
                            "                                            if (((i + j) & 1) !== colIdx) continue\n"
                            "                                            const big = i % maj === 0 && j % maj === 0\n"
                            "                                            const mid = !big && i % 2 === 0 && j % 2 === 0\n"
                            "                                            const kind = big ? 2 : (mid ? 1 : 0)\n"
                            "                                            if (kind !== t) continue\n"
                            "                                            addDiamond(i * c, j * c, r)\n"
                            "                                        }\n"
                            "                                    }\n"
                            "                                    ctx.fill()\n"
                            "                                }\n"
                            "                            }\n"
                            "                        }\n"
                            "\n"
                            "                        onWidthChanged: if (painted) requestPaint()\n"
                            "                        onHeightChanged: if (painted) requestPaint()\n"
                            "                        Component.onCompleted: ensurePaint()\n"
                            "\n"
                            "                        Connections {\n"
                            "                            target: gridLayer\n"
                            "                            function onWantChanged() {\n"
                            "                                if (!cv.painted && gridLayer.want > 0)\n"
                            "                                    cv.ensurePaint()\n"
                            "                            }\n"
                            "                        }\n"
                            "\n"
                            "                        Connections {\n"
                            "                            target: Theme\n"
                            "\n"
                            "                            function onColBorderChanged() { if (cv.painted) cv.requestPaint() }\n"
                            "                            function onAccentCyanChanged() { if (cv.painted) cv.requestPaint() }\n"
                            "                            function onAccentMintChanged() { if (cv.painted) cv.requestPaint() }\n"
                            "                        }\n"
                            "                    }"
                        )
                    );
                }

                // Batch EditBackdrop staggered diamond field into 2 draw calls instead of ~600 per monitor.
                if (fi.fileName() == QStringLiteral("EditBackdrop.qml")) {
                    text.replace(
                        QStringLiteral(
                            "                let row = 0\n"
                            "                for (let y = step / 2; y < h + step; y += step * 0.75) {\n"
                            "                    const off = (row % 2) * step / 2\n"
                            "                    for (let x = off; x < w + step; x += step) {\n"
                            "                        ctx.globalAlpha = 0.07 + ((row * 7 + Math.round(x / step) * 3) % 5) * 0.012\n"
                            "                        ctx.fillStyle = ((row + Math.round(x / step)) % 2 === 0) ? mt.accentMint : mt.accentCyan\n"
                            "                        ctx.beginPath()\n"
                            "                        ctx.moveTo(x, y - r)\n"
                            "                        ctx.lineTo(x + r, y)\n"
                            "                        ctx.lineTo(x, y + r)\n"
                            "                        ctx.lineTo(x - r, y)\n"
                            "                        ctx.closePath()\n"
                            "                        ctx.fill()\n"
                            "                    }\n"
                            "                    row++\n"
                            "                }"
                        ),
                        QStringLiteral(
                            "                ctx.globalAlpha = 0.095\n"
                            "                for (let pass = 0; pass < 2; pass++) {\n"
                            "                    ctx.fillStyle = pass === 0 ? mt.accentMint : mt.accentCyan\n"
                            "                    ctx.beginPath()\n"
                            "                    let row = 0\n"
                            "                    for (let y = step / 2; y < h + step; y += step * 0.75) {\n"
                            "                        const off = (row % 2) * step / 2\n"
                            "                        let col = 0\n"
                            "                        for (let x = off; x < w + step; x += step) {\n"
                            "                            if (((row + col) & 1) === pass) {\n"
                            "                                ctx.moveTo(x, y - r)\n"
                            "                                ctx.lineTo(x + r, y)\n"
                            "                                ctx.lineTo(x, y + r)\n"
                            "                                ctx.lineTo(x - r, y)\n"
                            "                                ctx.closePath()\n"
                            "                            }\n"
                            "                            col++\n"
                            "                        }\n"
                            "                        row++\n"
                            "                    }\n"
                            "                    ctx.fill()\n"
                            "                }"
                        )
                    );
                }

                // Stagger EditPreview GPU readbacks after enterAnim finishes, cap to thumbnail size (<=220x140),
                // and capture once per Edit Desktop session instead of stalling D3D11 every 2.5s forever.
                if (fi.fileName() == QStringLiteral("EditPreview.qml")) {
                    text.replace(
                        QStringLiteral(
                            "    interval: 1000\n"
                            "    repeat: true\n"
                            "    running: root.wanted && root.target !== null && root.widgetId !== \"\"\n"
                            "    triggeredOnStart: true\n"
                            "\n"
                            "    onTriggered: root.snap()\n"
                            "\n"
                            "    onRunningChanged: {\n"
                            "        if (!root.running && !EditMode.active) {\n"
                            "            EditMode.setPreview(root.widgetId, \"\", 0, 0)\n"
                            "            root.shot = null\n"
                            "        }\n"
                            "    }\n"
                            "\n"
                            "    function snap() {\n"
                            "        const t = root.target\n"
                            "        if (!t || !t.visible || t.width < 2 || t.height < 2)\n"
                            "            return\n"
                            "        const k = Math.max(1, root.pixelRatio)\n"
                            "        t.grabToImage(function (result) {\n"
                            "            if (!result || !root.running)\n"
                            "                return\n"
                            "            root.shot = result\n"
                            "            EditMode.setPreview(root.widgetId, result.url.toString(), t.width, t.height)\n"
                            "        }, Qt.size(Math.max(1, Math.round(t.width * k)), Math.max(1, Math.round(t.height * k))))\n"
                            "    }"
                        ),
                        QStringLiteral(
                            "    property bool didInitialSnap: false\n"
                            "    property int initialDelay: EditMode.progress >= 0.92 ? 120 : 520\n"
                            "\n"
                            "    function computeDelay(alreadyOpen) {\n"
                            "        let h = 0\n"
                            "        for (let i = 0; i < root.widgetId.length; i++)\n"
                            "            h = (h * 31 + root.widgetId.charCodeAt(i)) & 0x7fffffff\n"
                            "        return alreadyOpen ? 100 : (460 + (h % 8) * 65)\n"
                            "    }\n"
                            "\n"
                            "    interval: root.didInitialSnap ? 12000 : root.initialDelay\n"
                            "    repeat: !root.didInitialSnap\n"
                            "    running: root.wanted && root.target !== null && root.widgetId !== \"\" && !root.didInitialSnap\n"
                            "    triggeredOnStart: false\n"
                            "\n"
                            "    onTriggered: root.snap()\n"
                            "\n"
                            "    onWantedChanged: {\n"
                            "        if (root.wanted) {\n"
                            "            root.didInitialSnap = false\n"
                            "            root.initialDelay = root.computeDelay(EditMode.progress >= 0.92)\n"
                            "        } else if (!EditMode.active) {\n"
                            "            root.didInitialSnap = false\n"
                            "            root.shot = null\n"
                            "        }\n"
                            "    }\n"
                            "\n"
                            "    function snap() {\n"
                            "        if (EditMode.progress < 0.92 || EditGrid.phase === \"drag\" || EditMode.dragId !== \"\") {\n"
                            "            root.initialDelay = 120\n"
                            "            return\n"
                            "        }\n"
                            "        const t = root.target\n"
                            "        if (!t || !t.visible || t.width < 2 || t.height < 2)\n"
                            "            return\n"
                            "        root.didInitialSnap = true\n"
                            "        const k = Math.max(1, root.pixelRatio)\n"
                            "        const capScale = Math.min(k, 220 / Math.max(1, t.width), 140 / Math.max(1, t.height))\n"
                            "        const gw = Math.max(2, Math.round(t.width * capScale))\n"
                            "        const gh = Math.max(2, Math.round(t.height * capScale))\n"
                            "        const tk = EditMode.previewThemeKey\n"
                            "        t.grabToImage(function (result) {\n"
                            "            if (!result || !EditMode.active)\n"
                            "                return\n"
                            "            root.shot = result\n"
                            "            const filePath = Quickshell.editPreviewFilePath(tk, root.widgetId)\n"
                            "            if (filePath && result.saveToFile(filePath)) {\n"
                            "                Quickshell.saveEditPreviewMeta(tk, root.widgetId, t.width, t.height)\n"
                            "                EditMode.setPreview(root.widgetId, \"\", 0, 0)\n"
                            "                EditMode.setPreview(root.widgetId, \"file:///\" + filePath, t.width, t.height)\n"
                            "            } else {\n"
                            "                EditMode.setPreview(root.widgetId, result.url.toString(), t.width, t.height)\n"
                            "            }\n"
                            "        }, Qt.size(gw, gh))\n"
                            "    }"
                        )
                    );
                }

                // Configurable UI graphics pipeline (720p / 1080p / 2K / 4K / 8K) in PhoneBody.qml
                if (fi.fileName() == QStringLiteral("PhoneBody.qml")) {
                    text.replace(
                        QStringLiteral("    property real superSample: 2"),
                        QStringLiteral(
                            "    readonly property string uiGfxMode: String(SettingsService.get(\"gfx.uiQuality\") || \"1080p\")\n"
                            "    property real superSample: uiGfxMode === \"8k\" ? 3.0 : (uiGfxMode === \"4k\" ? 2.5 : (uiGfxMode === \"1440p\" ? 2.0 : (uiGfxMode === \"720p\" ? 1.0 : 1.5)))"
                        )
                    );
                    text.replace(
                        QStringLiteral("    property int moveSamples: 2"),
                        QStringLiteral("    property int moveSamples: uiGfxMode === \"8k\" || uiGfxMode === \"4k\" ? 4 : (uiGfxMode === \"720p\" ? 0 : 2)")
                    );
                    text.replace(
                        QStringLiteral("    property int restSamples: 4"),
                        QStringLiteral("    property int restSamples: uiGfxMode === \"8k\" ? 8 : (uiGfxMode === \"4k\" || uiGfxMode === \"1440p\" ? 4 : (uiGfxMode === \"720p\" ? 0 : 2))")
                    );
                    text.replace(
                        QStringLiteral("    property real moveMegapixels: 8"),
                        QStringLiteral("    property real moveMegapixels: uiGfxMode === \"8k\" ? 16.0 : (uiGfxMode === \"4k\" ? 8.0 : (uiGfxMode === \"1440p\" ? 4.0 : (uiGfxMode === \"720p\" ? 1.1 : 2.2)))")
                    );
                    text.replace(
                        QStringLiteral("    property real turnMegapixels: 2.5"),
                        QStringLiteral("    property real turnMegapixels: uiGfxMode === \"8k\" ? 10.0 : (uiGfxMode === \"4k\" ? 5.0 : (uiGfxMode === \"1440p\" ? 2.8 : (uiGfxMode === \"720p\" ? 1.0 : 1.8)))")
                    );
                    text.replace(
                        QStringLiteral("    property real restMegapixels: 10"),
                        QStringLiteral("    property real restMegapixels: uiGfxMode === \"8k\" ? 24.0 : (uiGfxMode === \"4k\" ? 12.0 : (uiGfxMode === \"1440p\" ? 7.0 : (uiGfxMode === \"720p\" ? 1.6 : 4.5)))")
                    );
                    text.replace(
                        QStringLiteral("    property int coolDelay: 20000"),
                        QStringLiteral("    property int coolDelay: 18000")
                    );
                    text.replace(
                        QStringLiteral("    property bool warm: false\n\n    onOnScreenChanged: {\n        if (onScreen) {\n            coolTimer.stop()\n            warm = true"),
                        QStringLiteral("    property bool warm: false\n    property bool justWarmed: false\n\n    onOnScreenChanged: {\n        if (onScreen) {\n            coolTimer.stop()\n            justWarmed = !warm\n            warm = true")
                    );
                    text.replace(
                        QStringLiteral("    property int maxWallSlices: 200"),
                        QStringLiteral("    property int maxWallSlices: uiGfxMode === \"8k\" ? 220 : (uiGfxMode === \"4k\" ? 160 : (uiGfxMode === \"1440p\" ? 110 : (uiGfxMode === \"720p\" ? 36 : 72)))")
                    );
                    text.replace(
                        QStringLiteral("    readonly property int coarseSlices: Math.max(8, Math.round(thickness / 3))"),
                        QStringLiteral("    readonly property int coarseSlices: Math.max(8, Math.round(thickness / 5))")
                    );
                    text.replace(
                        QStringLiteral("        layer.mipmap: !root.turnLite"),
                        QStringLiteral("        layer.mipmap: !root.moving && !root.turnLite")
                    );
                }

                // Qt 6.8 compatibility: 'short' is a reserved JS keyword in Qt 6.8 QML parser
                if (fi.fileName() == QStringLiteral("TimerService.qml")) {
                    text.replace(QStringLiteral("function short("), QStringLiteral("function shortTime("));
                    text.replace(QStringLiteral("root.short("), QStringLiteral("root.shortTime("));
                }

                // Qt 6.8 compatibility: Shear transform was added in Qt 6.9; use equivalent Matrix4x4 shear
                if (fi.fileName() == QStringLiteral("PluginMeter.qml")) {
                    text.replace(
                        QStringLiteral("transform: Shear { xFactor: -0.25 }"),
                        QStringLiteral("transform: Matrix4x4 { matrix: Qt.matrix4x4(1, -0.25, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1) }")
                    );
                }

                // Windows drive letter safety for file:// URL encoding (don't turn "C:" into "C%3A")
                if (text.contains(QStringLiteral(".split(\"/\").map(encodeURIComponent).join(\"/\")"))) {
                    text.replace(
                        QStringLiteral(".split(\"/\").map(encodeURIComponent).join(\"/\")"),
                        QStringLiteral(".split(\"/\").map((p, i) => i === 1 && /^[A-Za-z]:$/.test(p) ? p : encodeURIComponent(p)).join(\"/\")")
                    );
                }

                // Ensure fallback wallpaper runs even if WallpaperService became ready before shell.qml finished loading,
                // completely disable Linux PolkitPrompt & custom Lockscreen on Windows, and URL-lazy-load heavy overlays,
                // desktop widgets, and QuickPanel so Wallpaper + Bar + Dock appear in <2s on startup.
                if (fi.fileName() == QStringLiteral("shell.qml")) {
                    // Strip eager module imports that are now loaded strictly on demand via LazyLoader.source
                    for (const char* unusedImp : {
                        "import qs.modules.sekai.cheatSheet\n",
                        "import qs.modules.sekai.keybinds\n",
                        "import qs.modules.sekai.desktopMenu\n",
                        "import qs.modules.sekai.dock\n",
                        "import qs.modules.sekai.editMode\n",
                        "import qs.modules.sekai.framing\n",
                        "import qs.modules.sekai.keyboard\n",
                        "import qs.modules.sekai.launcher\n",
                        "import qs.modules.sekai.lock\n",
                        "import qs.modules.sekai.palette\n",
                        "import qs.modules.sekai.polkit\n",
                        "import qs.modules.sekai.quickPanel\n",
                        "import qs.modules.sekai.regionSelector\n",
                        "import qs.modules.sekai.visualizer\n",
                        "import qs.modules.sekai.welcome\n"
                    }) {
                        text.remove(QString::fromLatin1(unusedImp));
                    }

                    text.replace(
                        QStringLiteral("        function onReadyChanged() {\n            shellRoot.applyFallbackWallpaper();\n        }"),
                        QStringLiteral("        function onReadyChanged() {\n            shellRoot.applyFallbackWallpaper();\n        }\n        Component.onCompleted: shellRoot.applyFallbackWallpaper()")
                    );
                    text.replace(
                        QStringLiteral("    PolkitPrompt {}"),
                        QStringLiteral("    // PolkitPrompt disabled on Windows (uses native UAC)")
                    );
                    text.replace(
                        QStringLiteral("            FramerWindow { modelData: framerLoader.modelData }"),
                        QStringLiteral("            source: Qt.resolvedUrl(\"modules/sekai/framing/FramerWindow.qml\")\n"
                                       "            initialProperties: ({ modelData: framerLoader.modelData })")
                    );
                    text.replace(
                        QStringLiteral("    WelcomeScreen {}"),
                        QStringLiteral("    LazyLoader { active: WelcomeService.active || WelcomeService.closing; source: Qt.resolvedUrl(\"modules/sekai/welcome/WelcomeScreen.qml\") }")
                    );
                    text.replace(
                        QStringLiteral("    RegionSelector {}"),
                        QStringLiteral("    LazyLoader { active: RegionService.open; source: Qt.resolvedUrl(\"modules/sekai/regionSelector/RegionSelector.qml\") }")
                    );
                    text.replace(
                        QStringLiteral("    RegionPreview {}"),
                        QStringLiteral("    LazyLoader { active: RegionService.previewShown; source: Qt.resolvedUrl(\"modules/sekai/regionSelector/RegionPreview.qml\") }")
                    );
                    text.replace(
                        QStringLiteral("    OnScreenKeyboard {}"),
                        QStringLiteral("    LazyLoader { active: KeyboardService.open; source: Qt.resolvedUrl(\"modules/sekai/keyboard/OnScreenKeyboard.qml\") }")
                    );
                    text.replace(
                        QStringLiteral("    Lockscreen {}"),
                        QStringLiteral("    // Custom Lockscreen/PAM/fprintd/WlSessionLock disabled on Windows; IPC routes to LockWorkStation()\n"
                                       "    IpcHandler { target: \"lock\"; function lock(): void { Quickshell.execDetached([\"win-lock\"]) } }")
                    );

                    // Defer heavy QuickPanel (147KB + QuickPanelFace 107KB + PhoneBody 59KB + SettingsStage/Face)
                    // behind a lightweight proxy so Wallpaper + Bar + Dock render immediately at boot.
                    // Any hotkey, click, notification, or IPC before background warm-up loads it synchronously.
                    text.replace(
                        QStringLiteral("    QuickPanel {\n"
                                       "        id: panel\n"
                                       "        coverCheat: cheatSheetEntry.shown\n"
                                       "        source: quickState\n"
                                       "        notificationSource: notificationState\n"
                                       "        barWindows: bars.instances\n"
                                       "        picker: wallpaperEntry\n"
                                       "        appSource: appState\n"
                                       "        launcher: launcherPhone\n"
                                       "    }"),
                        QStringLiteral(
                            "    Scope {\n"
                            "        id: panel\n"
                            "        property bool coverCheat: cheatSheetEntry.shown\n"
                            "        property var source: quickState\n"
                            "        property var notificationSource: notificationState\n"
                            "        property var barWindows: bars.instances\n"
                            "        property var picker: wallpaperEntry\n"
                            "        property var appSource: appState\n"
                            "        property var launcher: launcherPhone\n"
                            "        property bool wantLoad: false\n"
                            "        readonly property var real: panelLoader.item\n"
                            "        readonly property bool open: real ? real.open : false\n"
                            "        readonly property bool up: real ? real.up : false\n"
                            "        readonly property bool settingsOpen: real ? real.settingsOpen : false\n"
                            "        readonly property bool settingsShown: real ? real.settingsShown : false\n"
                            "        readonly property bool wallpapersOpen: real ? real.wallpapersOpen : false\n"
                            "        readonly property bool onScreen: real ? real.onScreen : false\n"
                            "        readonly property var targetScreen: real ? real.targetScreen : (Quickshell.screens.length > 0 ? Quickshell.screens[0] : null)\n"
                            "        signal keybindsOpenRequested(var screen)\n"
                            "\n"
                            "        function ensureLoaded() {\n"
                            "            if (!panel.wantLoad)\n"
                            "                panel.wantLoad = true\n"
                            "            return panelLoader.item\n"
                            "        }\n"
                            "        function focusedScreen() {\n"
                            "            const r = panel.ensureLoaded()\n"
                            "            return r ? r.focusedScreen() : (Quickshell.screens.length > 0 ? Quickshell.screens[0] : null)\n"
                            "        }\n"
                            "        function toggleOn(s) { const r = panel.ensureLoaded(); if (r) r.toggleOn(s) }\n"
                            "        function openOn(s, e) { const r = panel.ensureLoaded(); if (r) r.openOn(s, e) }\n"
                            "        function close() { if (panelLoader.item) panelLoader.item.close() }\n"
                            "        function goOn(s, n) { const r = panel.ensureLoaded(); if (r) r.goOn(s, n) }\n"
                            "        function back() { if (panelLoader.item) panelLoader.item.back() }\n"
                            "        function homeNow() { if (panelLoader.item) panelLoader.item.homeNow() }\n"
                            "        function openAppsOn(s) { const r = panel.ensureLoaded(); if (r) r.openAppsOn(s) }\n"
                            "        function toggleAppsOn(s) { const r = panel.ensureLoaded(); if (r) r.toggleAppsOn(s) }\n"
                            "        function openAppsMode(s, n, t) { const r = panel.ensureLoaded(); if (r) r.openAppsMode(s, n, t) }\n"
                            "        function openSettings(s) { const r = panel.ensureLoaded(); if (r) r.openSettings(s) }\n"
                            "        function openWallpapers(s, w) { const r = panel.ensureLoaded(); if (r) r.openWallpapers(s, w) }\n"
                            "        function toggleWallpapersOn(s, w) { const r = panel.ensureLoaded(); if (r) r.toggleWallpapersOn(s, w) }\n"
                            "        function alert() { const r = panel.ensureLoaded(); if (r) r.alert() }\n"
                            "\n"
                            "        LazyLoader {\n"
                            "            id: panelLoader\n"
                            "            active: panel.wantLoad\n"
                            "            source: Qt.resolvedUrl(\"modules/sekai/quickPanel/QuickPanel.qml\")\n"
                            "            initialProperties: ({\n"
                            "                source: panel.source,\n"
                            "                notificationSource: panel.notificationSource,\n"
                            "                barWindows: panel.barWindows,\n"
                            "                coverCheat: panel.coverCheat,\n"
                            "                picker: panel.picker,\n"
                            "                appSource: panel.appSource,\n"
                            "                launcher: panel.launcher\n"
                            "            })\n"
                            "        }\n"
                            "        Binding { when: panelLoader.item !== null; target: panelLoader.item; property: \"barWindows\"; value: panel.barWindows }\n"
                            "        Binding { when: panelLoader.item !== null; target: panelLoader.item; property: \"coverCheat\"; value: panel.coverCheat }\n"
                            "        Connections {\n"
                            "            target: panelLoader.item\n"
                            "            function onKeybindsOpenRequested(scr) { panel.keybindsOpenRequested(scr) }\n"
                            "        }\n"
                            "        Connections {\n"
                            "            target: panel.notificationSource\n"
                            "            enabled: panelLoader.item === null\n"
                            "            function onAlerted(entry) { panel.alert() }\n"
                            "        }\n"
                            "        Connections {\n"
                            "            target: PluginService\n"
                            "            enabled: panelLoader.item === null\n"
                            "            function onSettingsWanted(scr) { panel.openSettings(panel.focusedScreen()) }\n"
                            "        }\n"
                            "        Timer {\n"
                            "            id: panelWarmTimer\n"
                            "            interval: 2200\n"
                            "            running: !panel.wantLoad\n"
                            "            onTriggered: panel.wantLoad = true\n"
                            "        }\n"
                            "        IpcHandler {\n"
                            "            target: \"phone\"\n"
                            "            enabled: panelLoader.item === null\n"
                            "            function toggle(): void { panel.toggleOn(panel.focusedScreen()) }\n"
                            "            function show(): void { panel.openOn(panel.focusedScreen()) }\n"
                            "            function hide(): void { panel.close() }\n"
                            "            function go(name: string): void { panel.goOn(panel.focusedScreen(), name) }\n"
                            "            function back(): void { panel.back() }\n"
                            "            function home(): void { panel.homeNow() }\n"
                            "        }\n"
                            "        IpcHandler {\n"
                            "            target: \"quickpanel\"\n"
                            "            enabled: panelLoader.item === null\n"
                            "            function toggle(): void { panel.toggleOn(panel.focusedScreen()) }\n"
                            "            function show(): void { panel.openOn(panel.focusedScreen()) }\n"
                            "            function hide(): void { panel.close() }\n"
                            "            function keepAwake(): void { panel.source.toggleKeepAwake() }\n"
                            "        }\n"
                            "        IpcHandler {\n"
                            "            target: \"launcherphone\"\n"
                            "            enabled: panelLoader.item === null\n"
                            "            function toggle(): void { panel.toggleAppsOn(panel.focusedScreen()) }\n"
                            "            function show(): void { panel.openAppsOn(panel.focusedScreen()) }\n"
                            "            function hide(): void { panel.close() }\n"
                            "            function mode(name: string): void { panel.openAppsMode(panel.focusedScreen(), name, false) }\n"
                            "            function toggleMode(name: string): void { panel.openAppsMode(panel.focusedScreen(), name, true) }\n"
                            "        }\n"
                            "        IpcHandler {\n"
                            "            target: \"applauncher\"\n"
                            "            enabled: panelLoader.item === null\n"
                            "            function toggle(): void { panel.toggleAppsOn(panel.focusedScreen()) }\n"
                            "            function show(): void { panel.openAppsOn(panel.focusedScreen()) }\n"
                            "            function hide(): void { panel.close() }\n"
                            "        }\n"
                            "        IpcHandler {\n"
                            "            target: \"wallpaperpicker\"\n"
                            "            enabled: panelLoader.item === null\n"
                            "            function toggle(): void { panel.toggleWallpapersOn(panel.focusedScreen()) }\n"
                            "            function show(): void { panel.openWallpapers(panel.focusedScreen()) }\n"
                            "            function hide(): void { panel.close() }\n"
                            "            function togglelock(): void { panel.toggleWallpapersOn(panel.focusedScreen(), \"lock\") }\n"
                            "            function wallhaven(): void { WallhavenService.askedSource = \"wallhaven\"; panel.openWallpapers(panel.focusedScreen()) }\n"
                            "            function showlock(): void { panel.openWallpapers(panel.focusedScreen(), \"lock\") }\n"
                            "        }\n"
                            "        IpcHandler {\n"
                            "            target: \"settings\"\n"
                            "            enabled: panelLoader.item === null\n"
                            "            function toggle(): void { panel.openSettings(panel.focusedScreen()) }\n"
                            "            function show(): void { panel.openSettings(panel.focusedScreen()) }\n"
                            "            function hide(): void { panel.close() }\n"
                            "            function page(name: string): void { SettingsService.set(\"settings.page\", name); panel.openSettings(panel.focusedScreen()) }\n"
                            "        }\n"
                            "    }"
                        )
                    );

                    // URL-lazy-load Overview, CheatSheet, KeybindsPanel, PaletteTool (0 ms compile at boot)
                    text.replace(
                        QStringLiteral("    LazyLoader {\n"
                                       "        active: ComponentActivation.overviewActive\n"
                                       "\n"
                                       "        Overview {\n"
                                       "            id: overviewBoardItem\n"
                                       "            Component.onCompleted: overview.attach(overviewBoardItem)\n"
                                       "            Component.onDestruction: overview.detach(overviewBoardItem)\n"
                                       "        }\n"
                                       "    }"),
                        QStringLiteral("    LazyLoader {\n"
                                       "        active: ComponentActivation.overviewActive\n"
                                       "        source: Qt.resolvedUrl(\"modules/sekai/overview/Overview.qml\")\n"
                                       "        property var attachedItem: null\n"
                                       "        onItemChanged: {\n"
                                       "            if (attachedItem && attachedItem !== item) { overview.detach(attachedItem); attachedItem = null }\n"
                                       "            if (item) { attachedItem = item; overview.attach(item) }\n"
                                       "        }\n"
                                       "    }")
                    );
                    text.replace(
                        QStringLiteral("    LazyLoader {\n"
                                       "        active: ComponentActivation.cheatSheetActive\n"
                                       "\n"
                                       "        CheatSheet {\n"
                                       "            id: cheatSheetBoard\n"
                                       "            quickPanel: panel\n"
                                       "            Component.onCompleted: cheatSheetEntry.attach(cheatSheetBoard)\n"
                                       "            Component.onDestruction: cheatSheetEntry.detach(cheatSheetBoard)\n"
                                       "        }\n"
                                       "    }"),
                        QStringLiteral("    LazyLoader {\n"
                                       "        active: ComponentActivation.cheatSheetActive\n"
                                       "        source: Qt.resolvedUrl(\"modules/sekai/cheatSheet/CheatSheet.qml\")\n"
                                       "        initialProperties: ({ quickPanel: panel })\n"
                                       "        property var attachedItem: null\n"
                                       "        onItemChanged: {\n"
                                       "            if (attachedItem && attachedItem !== item) { cheatSheetEntry.detach(attachedItem); attachedItem = null }\n"
                                       "            if (item) { attachedItem = item; cheatSheetEntry.attach(item) }\n"
                                       "        }\n"
                                       "    }")
                    );
                    text.replace(
                        QStringLiteral("    LazyLoader {\n"
                                       "        active: ComponentActivation.keybindsActive\n"
                                       "\n"
                                       "        KeybindsPanel {\n"
                                       "            id: keybindsSlab\n"
                                       "            quickPanel: panel\n"
                                       "            Component.onCompleted: keybindsEntry.attach(keybindsSlab)\n"
                                       "            Component.onDestruction: keybindsEntry.detach(keybindsSlab)\n"
                                       "        }\n"
                                       "    }"),
                        QStringLiteral("    LazyLoader {\n"
                                       "        active: ComponentActivation.keybindsActive\n"
                                       "        source: Qt.resolvedUrl(\"modules/sekai/keybinds/KeybindsPanel.qml\")\n"
                                       "        initialProperties: ({ quickPanel: panel })\n"
                                       "        property var attachedItem: null\n"
                                       "        onItemChanged: {\n"
                                       "            if (attachedItem && attachedItem !== item) { keybindsEntry.detach(attachedItem); attachedItem = null }\n"
                                       "            if (item) { attachedItem = item; keybindsEntry.attach(item) }\n"
                                       "        }\n"
                                       "    }")
                    );
                    text.replace(
                        QStringLiteral("    LazyLoader {\n"
                                       "        active: ComponentActivation.paletteToolActive\n"
                                       "\n"
                                       "        PaletteTool {\n"
                                       "            id: paletteToolBoard\n"
                                       "            Component.onCompleted: paletteToolEntry.attach(paletteToolBoard)\n"
                                       "            Component.onDestruction: paletteToolEntry.detach(paletteToolBoard)\n"
                                       "        }\n"
                                       "    }"),
                        QStringLiteral("    LazyLoader {\n"
                                       "        active: ComponentActivation.paletteToolActive\n"
                                       "        source: Qt.resolvedUrl(\"modules/sekai/palette/PaletteTool.qml\")\n"
                                       "        property var attachedItem: null\n"
                                       "        onItemChanged: {\n"
                                       "            if (attachedItem && attachedItem !== item) { paletteToolEntry.detach(attachedItem); attachedItem = null }\n"
                                       "            if (item) { attachedItem = item; paletteToolEntry.attach(item) }\n"
                                       "        }\n"
                                       "    }")
                    );

                    // Defer Dock (158KB + 5 popup cards) until Wallpaper + Bar have painted their first frame
                    text.replace(
                        QStringLiteral(
                            "    Variants {\n"
                            "        id: docks\n"
                            "        model: {"
                        ),
                        QStringLiteral(
                            "    property bool dockReady: false\n"
                            "    Timer {\n"
                            "        id: dockReadyTimer\n"
                            "        interval: 120\n"
                            "        running: !shellRoot.dockReady && SettingsService.loaded && BarPositionService.loaded && GameModeService.loaded\n"
                            "        onTriggered: shellRoot.dockReady = true\n"
                            "    }\n"
                            "    Variants {\n"
                            "        id: docks\n"
                            "        model: {\n"
                            "            if (!shellRoot.dockReady)\n"
                            "                return []"
                        )
                    );
                    text.replace(
                        QStringLiteral(
                            "        Dock {\n"
                            "            notificationSource: notificationState\n"
                            "            launcher: panel\n"
                            "            overviewBoard: overview\n"
                            "        }"
                        ),
                        QStringLiteral(
                            "        delegate: LazyLoader {\n"
                            "            id: dockItemLoader\n"
                            "            required property var modelData\n"
                            "            active: true\n"
                            "            source: Qt.resolvedUrl(\"modules/sekai/dock/Dock.qml\")\n"
                            "            initialProperties: ({\n"
                            "                modelData: dockItemLoader.modelData,\n"
                            "                notificationSource: notificationState,\n"
                            "                launcher: panel,\n"
                            "                overviewBoard: overview\n"
                            "            })\n"
                            "        }"
                        )
                    );
                    text.replace(
                        QStringLiteral("            for (const d of docks.instances)\n                d.peek()"),
                        QStringLiteral("            for (const d of docks.instances) {\n                const w = d && d.item ? d.item : d\n                if (w && w.peek) w.peek()\n            }")
                    );
                    text.replace(
                        QStringLiteral("            for (const d of docks.instances) {\n                if (d.toggleFiles())\n                    break\n            }"),
                        QStringLiteral("            for (const d of docks.instances) {\n                const w = d && d.item ? d.item : d\n                if (w && w.toggleFiles && w.toggleFiles())\n                    break\n            }")
                    );

                    // Stagger desktop widgets after Wallpaper + Bar + Dock present their first frame
                    text.replace(
                        QStringLiteral("    property int wakeStep: 99"),
                        QStringLiteral("    property int wakeStep: -1\n"
                                       "    Timer {\n"
                                       "        id: startupWakeTimer\n"
                                       "        interval: 260\n"
                                       "        running: shellRoot.dockReady\n"
                                       "        onTriggered: {\n"
                                       "            if (!shellRoot.desktopAsleep && shellRoot.wakeStep < 0) {\n"
                                       "                shellRoot.wakeStep = 0\n"
                                       "                wakeTimer.restart()\n"
                                       "            }\n"
                                       "        }\n"
                                       "    }")
                    );
                    text.replace(
                        QStringLiteral("    Timer {\n        id: wakeTimer\n        interval: 90"),
                        QStringLiteral("    Timer {\n        id: wakeTimer\n        interval: 65")
                    );

                    // URL-lazy-load all 11 desktop widgets (+ clones) and Edit Desktop overlays
                    text.replace(
                        QStringLiteral("        TodoWidget {}"),
                        QStringLiteral("        source: Qt.resolvedUrl(\"modules/sekai/desktopWidgets/TodoWidget.qml\")")
                    );
                    text.replace(
                        QStringLiteral("        ClockWidget {}"),
                        QStringLiteral("        source: Qt.resolvedUrl(\"modules/sekai/desktopWidgets/ClockWidget.qml\")")
                    );
                    text.replace(
                        QStringLiteral("            ClockWidget { clone: true; monitor: clClockWidget.modelData }"),
                        QStringLiteral("            source: Qt.resolvedUrl(\"modules/sekai/desktopWidgets/ClockWidget.qml\")\n"
                                       "            initialProperties: ({ clone: true, monitor: clClockWidget.modelData })")
                    );
                    text.replace(
                        QStringLiteral("        VisualizerWidget {}"),
                        QStringLiteral("        source: Qt.resolvedUrl(\"modules/sekai/visualizer/VisualizerWidget.qml\")")
                    );
                    text.replace(
                        QStringLiteral("        UserCardWidget {}"),
                        QStringLiteral("        source: Qt.resolvedUrl(\"modules/sekai/desktopWidgets/UserCardWidget.qml\")")
                    );
                    text.replace(
                        QStringLiteral("            UserCardWidget { clone: true; monitor: clUserCardWidget.modelData }"),
                        QStringLiteral("            source: Qt.resolvedUrl(\"modules/sekai/desktopWidgets/UserCardWidget.qml\")\n"
                                       "            initialProperties: ({ clone: true, monitor: clUserCardWidget.modelData })")
                    );
                    text.replace(
                        QStringLiteral("        WeatherCardWidget {}"),
                        QStringLiteral("        source: Qt.resolvedUrl(\"modules/sekai/desktopWidgets/WeatherCardWidget.qml\")")
                    );
                    text.replace(
                        QStringLiteral("            WeatherCardWidget { clone: true; monitor: clWeatherCardWidget.modelData }"),
                        QStringLiteral("            source: Qt.resolvedUrl(\"modules/sekai/desktopWidgets/WeatherCardWidget.qml\")\n"
                                       "            initialProperties: ({ clone: true, monitor: clWeatherCardWidget.modelData })")
                    );
                    text.replace(
                        QStringLiteral("                ImageWidget {}"),
                        QStringLiteral("                delegate: LazyLoader {\n"
                                       "                    id: imgWidgetItemLoader\n"
                                       "                    required property string modelData\n"
                                       "                    active: true\n"
                                       "                    source: Qt.resolvedUrl(\"modules/sekai/desktopWidgets/ImageWidget.qml\")\n"
                                       "                    initialProperties: ({ modelData: imgWidgetItemLoader.modelData })\n"
                                       "                }")
                    );
                    text.replace(
                        QStringLiteral("        StickyNotesWidget { pinnedLayer: false }"),
                        QStringLiteral("        source: Qt.resolvedUrl(\"modules/sekai/desktopWidgets/StickyNotesWidget.qml\")\n"
                                       "        initialProperties: ({ pinnedLayer: false })")
                    );
                    text.replace(
                        QStringLiteral("        StickyNotesWidget { pinnedLayer: true }"),
                        QStringLiteral("        source: Qt.resolvedUrl(\"modules/sekai/desktopWidgets/StickyNotesWidget.qml\")\n"
                                       "        initialProperties: ({ pinnedLayer: true })")
                    );
                    text.replace(
                        QStringLiteral("        CalendarCardWidget {}"),
                        QStringLiteral("        source: Qt.resolvedUrl(\"modules/sekai/desktopWidgets/CalendarCardWidget.qml\")")
                    );
                    text.replace(
                        QStringLiteral("            CalendarCardWidget { clone: true; monitor: clCalendarCardWidget.modelData }"),
                        QStringLiteral("            source: Qt.resolvedUrl(\"modules/sekai/desktopWidgets/CalendarCardWidget.qml\")\n"
                                       "            initialProperties: ({ clone: true, monitor: clCalendarCardWidget.modelData })")
                    );
                    text.replace(
                        QStringLiteral("        PaletteWidget {}"),
                        QStringLiteral("        source: Qt.resolvedUrl(\"modules/sekai/desktopWidgets/PaletteWidget.qml\")")
                    );
                    text.replace(
                        QStringLiteral("            PaletteWidget { clone: true; monitor: clPaletteWidget.modelData }"),
                        QStringLiteral("            source: Qt.resolvedUrl(\"modules/sekai/desktopWidgets/PaletteWidget.qml\")\n"
                                       "            initialProperties: ({ clone: true, monitor: clPaletteWidget.modelData })")
                    );
                    text.replace(
                        QStringLiteral("        LyricsWidget {}"),
                        QStringLiteral("        source: Qt.resolvedUrl(\"modules/sekai/desktopWidgets/LyricsWidget.qml\")")
                    );
                    text.replace(
                        QStringLiteral("            LyricsWidget { clone: true; monitor: clLyricsWidget.modelData }"),
                        QStringLiteral("            source: Qt.resolvedUrl(\"modules/sekai/desktopWidgets/LyricsWidget.qml\")\n"
                                       "            initialProperties: ({ clone: true, monitor: clLyricsWidget.modelData })")
                    );
                    text.replace(
                        QStringLiteral("                PluginWidgetHost { pluginId: plg.modelData }"),
                        QStringLiteral("                source: Qt.resolvedUrl(\"modules/sekai/desktopWidgets/PluginWidgetHost.qml\")\n"
                                       "                initialProperties: ({ pluginId: plg.modelData })")
                    );
                    text.replace(
                        QStringLiteral("                    PluginWidgetHost { pluginId: plg.modelData; clone: true; monitor: clPluginWidget.modelData }"),
                        QStringLiteral("                    source: Qt.resolvedUrl(\"modules/sekai/desktopWidgets/PluginWidgetHost.qml\")\n"
                                       "                    initialProperties: ({ pluginId: plg.modelData, clone: true, monitor: clPluginWidget.modelData })")
                    );
                    text.replace(
                        QStringLiteral("        LyricsSources {}"),
                        QStringLiteral("        source: Qt.resolvedUrl(\"modules/sekai/desktopWidgets/LyricsSources.qml\")")
                    );
                    text.replace(
                        QStringLiteral("    DesktopMenu {}\n    EditTray {}\n    // A widget pulled up off the shelf follows the pointer in a window of its own.\n    EditDragGhost {}"),
                        QStringLiteral("    LazyLoader { active: EditMode.menuOpen; source: Qt.resolvedUrl(\"modules/sekai/desktopMenu/DesktopMenu.qml\") }\n"
                                       "    LazyLoader { id: editTrayLoader; property bool warmed: false; active: warmed || EditMode.screenName !== \"\"; source: Qt.resolvedUrl(\"modules/sekai/editMode/EditTray.qml\") }\n"
                                       "    Connections { target: EditMode; function onScreenNameChanged() { if (EditMode.screenName !== \"\") editTrayLoader.warmed = true } }\n"
                                       "    LazyLoader { active: EditMode.dragId !== \"\"; source: Qt.resolvedUrl(\"modules/sekai/editMode/EditDragGhost.qml\") }")
                    );
                    text.replace(
                        QStringLiteral("    LazyLoader {\n        active: EditMode.screenName !== \"\"\n\n        Scope {\n            Variants {\n                model: Quickshell.screens\n                EditGuides {}\n            }\n        }\n    }"),
                        QStringLiteral("    LazyLoader {\n"
                                       "        active: EditMode.screenName !== \"\" && (EditGrid.phase !== \"idle\" || EditGrid.gridAlwaysOn)\n"
                                       "        Scope {\n"
                                       "            Variants {\n"
                                       "                model: Quickshell.screens\n"
                                       "                delegate: LazyLoader {\n"
                                       "                    id: editGuideItemLoader\n"
                                       "                    required property var modelData\n"
                                       "                    active: true\n"
                                       "                    source: Qt.resolvedUrl(\"modules/sekai/editMode/EditGuides.qml\")\n"
                                       "                    initialProperties: ({ modelData: editGuideItemLoader.modelData })\n"
                                       "                }\n"
                                       "            }\n"
                                       "        }\n"
                                       "    }")
                    );
                    // Staggered 1-at-a-time background preview capture for DISABLED desktop widgets
                    // that are not yet cached on disk for the current theme. Runs only AFTER enterAnim
                    // finishes so entering Edit Desktop stays instant (0 extra swapchains on open),
                    // saves each thumbnail PNG to disk cache, and immediately unloads the temporary widget.
                    text.replace(
                        QStringLiteral("    LazyLoader { id: editTrayLoader; property bool warmed: false; active: warmed || EditMode.screenName !== \"\"; source: Qt.resolvedUrl(\"modules/sekai/editMode/EditTray.qml\") }"),
                        QStringLiteral("    property string previewCaptureId: \"\"\n"
                                       "    property var previewTried: ({})\n"
                                       "    property double previewCaptureStartedAt: 0\n"
                                       "    function stepPreviewCapture() {\n"
                                       "        if (!EditMode.active || EditMode.progress < 0.92 || EditMode.currentTab !== \"widgets\" || EditGrid.phase === \"drag\") {\n"
                                       "            if (shellRoot.previewCaptureId !== \"\")\n"
                                       "                shellRoot.previewCaptureId = \"\"\n"
                                       "            return\n"
                                       "        }\n"
                                       "        const cur = shellRoot.previewCaptureId\n"
                                       "        if (cur !== \"\") {\n"
                                       "            const done = !!EditMode.previewUrls[cur] || EditMode.widgetShown(cur)\n"
                                       "            const timedOut = (Date.now() - shellRoot.previewCaptureStartedAt) > 2200\n"
                                       "            if (!done && !timedOut)\n"
                                       "                return\n"
                                       "            const t = Object.assign({}, shellRoot.previewTried)\n"
                                       "            t[cur] = true\n"
                                       "            shellRoot.previewTried = t\n"
                                       "            shellRoot.previewCaptureId = \"\"\n"
                                       "            return\n"
                                       "        }\n"
                                       "        const ids = [\"weathercard\", \"todo\", \"palettecard\", \"lyrics\", \"clock\", \"usercard\", \"calendarcard\", \"visualizer\"]\n"
                                       "        for (let i = 0; i < ids.length; i++) {\n"
                                       "            const id = ids[i]\n"
                                       "            if (!EditMode.widgetShown(id) && !EditMode.previewUrls[id] && !shellRoot.previewTried[id]) {\n"
                                       "                shellRoot.previewCaptureStartedAt = Date.now()\n"
                                       "                shellRoot.previewCaptureId = id\n"
                                       "                return\n"
                                       "            }\n"
                                       "        }\n"
                                       "    }\n"
                                       "    Timer {\n"
                                       "        id: previewQueueTimer\n"
                                       "        interval: 150\n"
                                       "        repeat: true\n"
                                       "        running: EditMode.active && EditMode.currentTab === \"widgets\"\n"
                                       "        onTriggered: shellRoot.stepPreviewCapture()\n"
                                       "    }\n"
                                       "    Connections {\n"
                                       "        target: EditMode\n"
                                       "        function onActiveChanged() {\n"
                                       "            if (!EditMode.active) {\n"
                                       "                shellRoot.previewCaptureId = \"\"\n"
                                       "                shellRoot.previewTried = ({})\n"
                                       "            } else {\n"
                                       "                EditMode.loadCachedPreviews()\n"
                                       "            }\n"
                                       "        }\n"
                                       "        function onPreviewThemeKeyChanged() {\n"
                                       "            shellRoot.previewCaptureId = \"\"\n"
                                       "            shellRoot.previewTried = ({})\n"
                                       "        }\n"
                                       "        function onPreviewUrlsChanged() {\n"
                                       "            if (shellRoot.previewCaptureId !== \"\" && EditMode.previewUrls[shellRoot.previewCaptureId])\n"
                                       "                Qt.callLater(shellRoot.stepPreviewCapture)\n"
                                       "        }\n"
                                       "    }\n"
                                       "    LazyLoader { id: editTrayLoader; property bool warmed: false; active: warmed || EditMode.screenName !== \"\"; source: Qt.resolvedUrl(\"modules/sekai/editMode/EditTray.qml\") }")
                    );
                    text.replace(
                        QStringLiteral("((TodoService.loaded && TodoService.shown && !todoGate.gone) || EditMode.active || shellRoot.todoResetRequested)"),
                        QStringLiteral("((TodoService.loaded && TodoService.shown && !todoGate.gone) || shellRoot.previewCaptureId === \"todo\" || shellRoot.todoResetRequested)")
                    );
                    text.replace(
                        QStringLiteral("((ClockService.loaded && ClockService.shown && !clockGate.gone) || EditMode.active || shellRoot.clockResetRequested)"),
                        QStringLiteral("((ClockService.loaded && ClockService.shown && !clockGate.gone) || shellRoot.previewCaptureId === \"clock\" || shellRoot.clockResetRequested)")
                    );
                    text.replace(
                        QStringLiteral("((VisualizerService.loaded && VisualizerService.shown && !visualizerGate.gone) || EditMode.active || shellRoot.visualizerResetRequested)"),
                        QStringLiteral("((VisualizerService.loaded && VisualizerService.shown && !visualizerGate.gone) || shellRoot.previewCaptureId === \"visualizer\" || shellRoot.visualizerResetRequested)")
                    );
                    text.replace(
                        QStringLiteral("((UserCardService.loaded && UserCardService.shown && !usercardGate.gone) || EditMode.active || shellRoot.userCardResetRequested)"),
                        QStringLiteral("((UserCardService.loaded && UserCardService.shown && !usercardGate.gone) || shellRoot.previewCaptureId === \"usercard\" || shellRoot.userCardResetRequested)")
                    );
                    text.replace(
                        QStringLiteral("((WeatherCardService.loaded && WeatherCardService.shown && !weathercardGate.gone) || EditMode.active || shellRoot.weatherCardResetRequested)"),
                        QStringLiteral("((WeatherCardService.loaded && WeatherCardService.shown && !weathercardGate.gone) || shellRoot.previewCaptureId === \"weathercard\" || shellRoot.weatherCardResetRequested)")
                    );
                    text.replace(
                        QStringLiteral("((CalendarCardService.loaded && CalendarCardService.shown && !calendarcardGate.gone) || EditMode.active || shellRoot.calendarCardResetRequested)"),
                        QStringLiteral("((CalendarCardService.loaded && CalendarCardService.shown && !calendarcardGate.gone) || shellRoot.previewCaptureId === \"calendarcard\" || shellRoot.calendarCardResetRequested)")
                    );
                    text.replace(
                        QStringLiteral("((PaletteService.loaded && PaletteService.shown && !palettecardGate.gone) || EditMode.active || shellRoot.paletteCardResetRequested)"),
                        QStringLiteral("((PaletteService.loaded && PaletteService.shown && !palettecardGate.gone) || shellRoot.previewCaptureId === \"palettecard\" || shellRoot.paletteCardResetRequested)")
                    );
                    text.replace(
                        QStringLiteral("((LyricsService.loaded && LyricsService.shown && !lyricsGate.gone) || EditMode.active || shellRoot.lyricsResetRequested)"),
                        QStringLiteral("((LyricsService.loaded && LyricsService.shown && !lyricsGate.gone) || shellRoot.previewCaptureId === \"lyrics\" || shellRoot.lyricsResetRequested)")
                    );
                    text.replace(
                        QStringLiteral("((plg.st.shown && !pluginGate.gone) || EditMode.active)"),
                        QStringLiteral("(plg.st.shown && !pluginGate.gone)")
                    );

                    // Replace legacy duplicate corner phone (LauncherPhone) with a lightweight shim
                    // forwarding wallpaperphone IPC to the unified QuickPanel phone (saves ~25-40MB RAM).
                    text.replace(
                        QStringLiteral("    LauncherPhone {\n"
                                       "        id: launcherPhone\n"
                                       "        source: appState\n"
                                       "        quickPanel: panel\n"
                                       "        overviewBoard: overview\n"
                                       "    }"),
                        QStringLiteral("    QtObject {\n"
                                       "        id: launcherPhone\n"
                                       "        function close() {}\n"
                                       "    }\n"
                                       "    IpcHandler {\n"
                                       "        target: \"wallpaperphone\"\n"
                                       "        function toggle(): void { panel.toggleWallpapersOn(panel.focusedScreen(), \"desktop\") }\n"
                                       "        function show(): void { panel.openWallpapers(panel.focusedScreen(), \"desktop\") }\n"
                                       "        function hide(): void { panel.close() }\n"
                                       "    }")
                    );

                    // Stage-0 Ultra-Fast Boot: strip non-core eager imports, defer OSD/Notification/Reload
                    // popups and Linux-only services until Tier-2 singletons finish right after Wallpaper + Bar paint,
                    // avoid FolderListModel directory scans when a wallpaper is already saved, and extract the
                    // entire Desktop Widgets + Edit Desktop block into lazy-loaded DeferredWidgets.qml.
                    for (const char* stage0Strip : {
                        "import qs.modules.sekai.desktopWidgets\n",
                        "import qs.modules.sekai.notificationPopup\n",
                        "import qs.modules.sekai.onScreenDisplay\n"
                    }) {
                        text.remove(QString::fromLatin1(stage0Strip));
                    }
                    text.replace(
                        QStringLiteral("    settings.watchFiles: !UpdateService.updating"),
                        QStringLiteral("    settings.watchFiles: !(UpdateService && UpdateService.updating)")
                    );
                    text.replace(
                        QStringLiteral("    readonly property bool focusModeOn: FocusService.enabled"),
                        QStringLiteral("    readonly property bool focusModeOn: QsDeferredBoot.tier2Ready && !!FocusService && FocusService.enabled")
                    );
                    text.replace(
                        QStringLiteral("active: RegionService.open"),
                        QStringLiteral("active: QsDeferredBoot.tier2Ready && !!RegionService && RegionService.open")
                    );
                    text.replace(
                        QStringLiteral("active: RegionService.previewShown"),
                        QStringLiteral("active: QsDeferredBoot.tier2Ready && !!RegionService && RegionService.previewShown")
                    );
                    text.replace(
                        QStringLiteral("active: KeyboardService.open"),
                        QStringLiteral("active: QsDeferredBoot.tier2Ready && !!KeyboardService && KeyboardService.open")
                    );
                    text.replace(
                        QStringLiteral("    readonly property bool appThemingOn: AppThemeService.ready"),
                        QStringLiteral("    readonly property bool appThemingOn: QsDeferredBoot.tier2Ready && !!AppThemeService && AppThemeService.ready")
                    );
                    text.replace(
                        QStringLiteral("    readonly property bool idleTimesReady: IdleService.ready"),
                        QStringLiteral("    readonly property bool idleTimesReady: QsDeferredBoot.tier2Ready && !!IdleService && IdleService.ready")
                    );
                    text.replace(
                        QStringLiteral("    readonly property bool appFontsReady: AppFontService.ready"),
                        QStringLiteral("    readonly property bool appFontsReady: QsDeferredBoot.tier2Ready && !!AppFontService && AppFontService.ready")
                    );
                    text.replace(
                        QStringLiteral("        folder: WallpaperService.directoryUrl"),
                        QStringLiteral("        folder: (WallpaperService.ready && WallpaperService.defaultPath.length === 0) ? WallpaperService.directoryUrl : \"\"")
                    );
                    text.replace(
                        QStringLiteral("            active: PanelImages.framing !== \"\""),
                        QStringLiteral("            active: QsDeferredBoot.tier2Ready && !!PanelImages && PanelImages.framing !== \"\"")
                    );
                    text.replace(
                        QStringLiteral("active: WelcomeService.active || WelcomeService.closing"),
                        QStringLiteral("active: QsDeferredBoot.tier2Ready && !!WelcomeService && (WelcomeService.active || WelcomeService.closing)")
                    );
                    text.replace(
                        QStringLiteral("active: ComponentActivation.overviewActive"),
                        QStringLiteral("active: QsDeferredBoot.tier2Ready && !!ComponentActivation && ComponentActivation.overviewActive")
                    );
                    text.replace(
                        QStringLiteral("active: ComponentActivation.cheatSheetActive"),
                        QStringLiteral("active: QsDeferredBoot.tier2Ready && !!ComponentActivation && ComponentActivation.cheatSheetActive")
                    );
                    text.replace(
                        QStringLiteral("active: ComponentActivation.keybindsActive"),
                        QStringLiteral("active: QsDeferredBoot.tier2Ready && !!ComponentActivation && ComponentActivation.keybindsActive")
                    );
                    text.replace(
                        QStringLiteral("active: ComponentActivation.paletteToolActive"),
                        QStringLiteral("active: QsDeferredBoot.tier2Ready && !!ComponentActivation && ComponentActivation.paletteToolActive")
                    );
                    text.replace(
                        QStringLiteral("    OSDPopup {}"),
                        QStringLiteral("    LazyLoader { active: QsDeferredBoot.tier2Ready; source: Qt.resolvedUrl(\"modules/sekai/onScreenDisplay/OSDPopup.qml\") }")
                    );
                    text.replace(
                        QStringLiteral("    ReloadPopup {}"),
                        QStringLiteral("    LazyLoader { active: QsDeferredBoot.tier2Ready; source: Qt.resolvedUrl(\"ReloadPopup.qml\") }")
                    );
                    text.replace(
                        QStringLiteral("    NotificationPopup {\n        source: notificationState\n    }"),
                        QStringLiteral("    LazyLoader {\n"
                                       "        active: QsDeferredBoot.tier2Ready && notificationState.real !== null\n"
                                       "        source: Qt.resolvedUrl(\"modules/sekai/notificationPopup/NotificationPopup.qml\")\n"
                                       "        initialProperties: ({ source: notificationState.real })\n"
                                       "    }")
                    );
                    text.replace(
                        QStringLiteral("    BatteryAlertService {}"),
                        QStringLiteral("    LazyLoader { active: QsDeferredBoot.tier2Ready; source: Qt.resolvedUrl(\"services/BatteryAlertService.qml\") }")
                    );
                    // Defer heavy non-Stage-0 services (QuickSource 78KB + NotificationSource 27KB +
                    // AppSource 13KB + TraySource 3KB = 121KB QML + JS) until right after Wallpaper + Bar paint.
                    text.replace(
                        QStringLiteral("    TraySource {\n        id: trayState\n    }"),
                        QStringLiteral("    Scope {\n"
                                       "        id: trayState\n"
                                       "        readonly property var real: trayStateLoader.item\n"
                                       "        readonly property var unpinnedItems: real ? real.unpinnedItems : []\n"
                                       "        readonly property var pinnedItems: real ? real.pinnedItems : []\n"
                                       "        readonly property bool unpinnedAttention: real ? real.unpinnedAttention : false\n"
                                       "        LazyLoader {\n"
                                       "            id: trayStateLoader\n"
                                       "            active: QsDeferredBoot.tier2Ready\n"
                                       "            source: Qt.resolvedUrl(\"services/TraySource.qml\")\n"
                                       "        }\n"
                                       "    }")
                    );
                    text.replace(
                        QStringLiteral("    QuickSource {\n"
                                       "        id: quickState\n"
                                       "        // The Settings show some of these tiles too.\n"
                                       "        active: panel.up || panel.settingsShown\n"
                                       "        targetScreen: panel.targetScreen\n"
                                       "    }"),
                        QStringLiteral("    Scope {\n"
                                       "        id: quickState\n"
                                       "        property bool wantLoad: false\n"
                                       "        readonly property var real: quickStateLoader.item\n"
                                       "        readonly property bool notificationsOn: real ? real.notificationsOn : SettingsService.flag(\"notify.on\")\n"
                                       "        function ensureReal() {\n"
                                       "            if (!wantLoad) wantLoad = true\n"
                                       "            return quickStateLoader.item\n"
                                       "        }\n"
                                       "        function toggleKeepAwake() {\n"
                                       "            const r = ensureReal()\n"
                                       "            if (r) r.toggleKeepAwake()\n"
                                       "        }\n"
                                       "        LazyLoader {\n"
                                       "            id: quickStateLoader\n"
                                       "            active: QsDeferredBoot.tier2Ready || quickState.wantLoad\n"
                                       "            source: Qt.resolvedUrl(\"services/QuickSource.qml\")\n"
                                       "            onItemChanged: {\n"
                                       "                if (!item) return\n"
                                       "                item.active = Qt.binding(() => panel.up || panel.settingsShown)\n"
                                       "                item.targetScreen = Qt.binding(() => panel.targetScreen)\n"
                                       "            }\n"
                                       "        }\n"
                                       "    }")
                    );
                    text.replace(
                        QStringLiteral("    NotificationSource {\n"
                                       "        id: notificationState\n"
                                       "        doNotDisturb: !quickState.notificationsOn\n"
                                       "        quietWhenFullscreen: SettingsService.flag(\"notify.quietFullscreen\")\n"
                                       "        alertStyle: SettingsService.get(\"notify.style\")\n"
                                       "        popupsSuppressed: panel.up\n"
                                       "    }"),
                        QStringLiteral("    Scope {\n"
                                       "        id: notificationState\n"
                                       "        property bool wantLoad: false\n"
                                       "        readonly property var real: notifStateLoader.item\n"
                                       "        readonly property int count: real ? real.count : 0\n"
                                       "        signal alerted(var entry)\n"
                                       "        function ensureReal() {\n"
                                       "            if (!wantLoad) wantLoad = true\n"
                                       "            return notifStateLoader.item\n"
                                       "        }\n"
                                       "        LazyLoader {\n"
                                       "            id: notifStateLoader\n"
                                       "            active: QsDeferredBoot.tier2Ready || notificationState.wantLoad\n"
                                       "            source: Qt.resolvedUrl(\"services/NotificationSource.qml\")\n"
                                       "            onItemChanged: {\n"
                                       "                if (!item) return\n"
                                       "                item.doNotDisturb = Qt.binding(() => !quickState.notificationsOn)\n"
                                       "                item.quietWhenFullscreen = Qt.binding(() => SettingsService.flag(\"notify.quietFullscreen\"))\n"
                                       "                item.alertStyle = Qt.binding(() => SettingsService.get(\"notify.style\"))\n"
                                       "                item.popupsSuppressed = Qt.binding(() => panel.up)\n"
                                       "            }\n"
                                       "        }\n"
                                       "        Connections {\n"
                                       "            target: notifStateLoader.item\n"
                                       "            function onAlerted(entry) { notificationState.alerted(entry) }\n"
                                       "        }\n"
                                       "    }")
                    );
                    text.replace(
                        QStringLiteral("    AppSource {\n        id: appState\n    }"),
                        QStringLiteral("    Scope {\n"
                                       "        id: appState\n"
                                       "        property bool wantLoad: false\n"
                                       "        readonly property var real: appStateLoader.item\n"
                                       "        function ensureReal() {\n"
                                       "            if (!wantLoad) wantLoad = true\n"
                                       "            return appStateLoader.item\n"
                                       "        }\n"
                                       "        LazyLoader {\n"
                                       "            id: appStateLoader\n"
                                       "            active: QsDeferredBoot.tier2Ready || appState.wantLoad\n"
                                       "            source: Qt.resolvedUrl(\"services/AppSource.qml\")\n"
                                       "        }\n"
                                       "    }")
                    );
                    text.replace(
                        QStringLiteral("        function ensureLoaded() {\n"
                                       "            if (!panel.wantLoad)\n"
                                       "                panel.wantLoad = true\n"
                                       "            return panelLoader.item\n"
                                       "        }"),
                        QStringLiteral("        function ensureLoaded() {\n"
                                       "            if (typeof QsDeferredBoot !== \"undefined\" && QsDeferredBoot && !QsDeferredBoot.tier2Ready)\n"
                                       "                QsDeferredBoot.ensureTier2Now()\n"
                                       "            if (quickState.ensureReal) quickState.ensureReal()\n"
                                       "            if (notificationState.ensureReal) notificationState.ensureReal()\n"
                                       "            if (appState.ensureReal) appState.ensureReal()\n"
                                       "            if (!panel.wantLoad)\n"
                                       "                panel.wantLoad = true\n"
                                       "            return panelLoader.item\n"
                                       "        }")
                    );
                    text.replace(
                        QStringLiteral("            initialProperties: ({\n"
                                       "                source: panel.source,\n"
                                       "                notificationSource: panel.notificationSource,\n"
                                       "                barWindows: panel.barWindows,\n"
                                       "                coverCheat: panel.coverCheat,\n"
                                       "                picker: panel.picker,\n"
                                       "                appSource: panel.appSource,\n"
                                       "                launcher: panel.launcher\n"
                                       "            })"),
                        QStringLiteral("            initialProperties: ({\n"
                                       "                source: (panel.source && panel.source.real) ? panel.source.real : panel.source,\n"
                                       "                notificationSource: (panel.notificationSource && panel.notificationSource.real) ? panel.notificationSource.real : panel.notificationSource,\n"
                                       "                barWindows: panel.barWindows,\n"
                                       "                coverCheat: panel.coverCheat,\n"
                                       "                picker: panel.picker,\n"
                                       "                appSource: (panel.appSource && panel.appSource.real) ? panel.appSource.real : panel.appSource,\n"
                                       "                launcher: panel.launcher\n"
                                       "            })")
                    );
                    text.replace(
                        QStringLiteral("            id: panelWarmTimer\n"
                                       "            interval: 2200\n"
                                       "            running: !panel.wantLoad\n"
                                       "            onTriggered: panel.wantLoad = true"),
                        QStringLiteral("            id: panelWarmTimer\n"
                                       "            interval: 2200\n"
                                       "            running: !panel.wantLoad && QsDeferredBoot.tier2Ready\n"
                                       "            onTriggered: panel.ensureLoaded()")
                    );
                    text.replace(
                        QStringLiteral("                notificationSource: notificationState,\n"
                                       "                launcher: panel,"),
                        QStringLiteral("                notificationSource: notificationState.real ? notificationState.real : notificationState,\n"
                                       "                launcher: panel,")
                    );
                    text.replace(
                        QStringLiteral("        id: dockReadyTimer\n"
                                       "        interval: 120\n"
                                       "        running: !shellRoot.dockReady && SettingsService.loaded && BarPositionService.loaded && GameModeService.loaded"),
                        QStringLiteral("        id: dockReadyTimer\n"
                                       "        interval: 16\n"
                                       "        running: !shellRoot.dockReady && QsDeferredBoot.tier2Ready && notificationState.real !== null && SettingsService.loaded && BarPositionService.loaded && GameModeService.loaded")
                    );
                    text.replace(
                        QStringLiteral("        id: startupWakeTimer\n"
                                       "        interval: 260"),
                        QStringLiteral("        id: startupWakeTimer\n"
                                       "        interval: 80")
                    );

                    // Split the entire Desktop Widgets + Edit Desktop section into DeferredWidgets.qml
                    // so engine.load(shell.qml) never compiles 650+ lines of FocusGates, widget Loaders,
                    // WidgetClones Variants, or widget IPC handlers before Wallpaper + Bar render.
                    const QString splitMarker = QStringLiteral("    readonly property bool widgetsAllowed:");
                    const int splitIdx = text.indexOf(splitMarker);
                    if (splitIdx > 0) {
                        const QString deferredTail = text.mid(splitIdx);
                        const QString deferredQml =
                            QStringLiteral("import Quickshell\n"
                                           "import Quickshell.Io\n"
                                           "import QtQuick\n"
                                           "import qs.services\n"
                                           "import qs.modules.common\n"
                                           "import qs.modules.sekai.desktopWidgets\n"
                                           "\n"
                                           "Scope {\n"
                                           "    id: shellRoot\n"
                                           "    property bool dockReady: false\n"
                                           "\n") + deferredTail;
                        writeFileIfChanged(stageQsDir + QStringLiteral("/DeferredWidgets.qml"), deferredQml.toUtf8());

                        text = text.left(splitIdx) + QStringLiteral(
                            "    LazyLoader {\n"
                            "        id: deferredWidgetsLoader\n"
                            "        active: QsDeferredBoot.tier2Ready\n"
                            "        source: Qt.resolvedUrl(\"DeferredWidgets.qml\")\n"
                            "        initialProperties: ({ dockReady: shellRoot.dockReady })\n"
                            "    }\n"
                            "    Binding {\n"
                            "        when: deferredWidgetsLoader.item !== null\n"
                            "        target: deferredWidgetsLoader.item\n"
                            "        property: \"dockReady\"\n"
                            "        value: shellRoot.dockReady\n"
                            "    }\n"
                            "}\n");
                    }
                }

                // Disable custom LockState state machine completely on Windows; LockState.lock() ONLY calls LockWorkStation()
                if (fi.fileName() == QStringLiteral("LockState.qml")) {
                    text.replace(
                        QStringLiteral("    function lock() {\n"
                                       "        if (root.phase === \"unlocking\") {\n"
                                       "            root.relockPending = true\n"
                                       "            return\n"
                                       "        }\n"
                                       "        if (root.phase !== \"idle\")\n"
                                       "            return\n"
                                       "        root.lockedAt = new Date()\n"
                                       "        // With the \"Random\" transition: pick this lock's style now.\n"
                                       "        WallpaperService.rollLockStyle()\n"
                                       "        root.phase = \"locking\"\n"
                                       "    }"),
                        QStringLiteral("    function lock() {\n"
                                       "        root.locked = false\n"
                                       "        root.phase = \"idle\"\n"
                                       "        Quickshell.execDetached([\"win-lock\"])\n"
                                       "    }")
                    );
                    text.replace(
                        QStringLiteral("        root.locked = true\n        root.phase = \"locked\""),
                        QStringLiteral("        root.locked = false\n        root.phase = \"idle\"")
                    );
                }

                // Windows native Lock/Sleep/Hibernate in QuickSource.qml (never trap user in Wayland Lockscreen)
                if (fi.fileName() == QStringLiteral("QuickSource.qml")) {
                    text.replace(
                        QStringLiteral("    function lock() {\n        LockState.lock()\n    }"),
                        QStringLiteral("    function lock() {\n        Quickshell.execDetached([\"win-lock\"])\n    }")
                    );
                    text.replace(
                        QStringLiteral("    function sleep() {\n        root.lockThen(\"sleep\")\n    }"),
                        QStringLiteral("    function sleep() {\n        sleepAction.running = true\n    }")
                    );
                    text.replace(
                        QStringLiteral("    function hibernate() {\n        root.lockThen(\"hibernate\")\n    }"),
                        QStringLiteral("    function hibernate() {\n        hibernateAction.running = true\n    }")
                    );
                    text.replace(
                        QStringLiteral("DesktopPresetService.noteQuickTiles(root.tiles, root.tileColumns)"),
                        QStringLiteral("if (DesktopPresetService) DesktopPresetService.noteQuickTiles(root.tiles, root.tileColumns)")
                    );
                }

                // Lazy-load BarTip's PopupWindow AND strip Linux-only hyprctl cursorpos Process/Timer/MouseArea
                // (eliminates 40 QQuickWindows + 40 QsProcess + 40 StdioCollector + 40 Timers + 40 MouseAreas at boot).
                if (fi.fileName() == QStringLiteral("BarTip.qml")) {
                    text.replace(
                        QStringLiteral(
                            "    MouseArea {\n"
                            "        width: root.host ? root.host.width : 0\n"
                            "        height: root.host ? root.host.height : 0\n"
                            "        hoverEnabled: true\n"
                            "        acceptedButtons: Qt.NoButton\n"
                            "        onPositionChanged: root.lastMove = Date.now()\n"
                            "    }\n"
                            "\n"
                            "    Timer {\n"
                            "        interval: 70\n"
                            "        repeat: true\n"
                            "        running: root.shown && root.text !== \"\"\n"
                            "        onTriggered: {\n"
                            "            if (!cursorProbe.running)\n"
                            "                cursorProbe.running = true\n"
                            "        }\n"
                            "    }\n"
                            "\n"
                            "    Process {\n"
                            "        id: cursorProbe\n"
                            "        command: [\"hyprctl\", \"-j\", \"cursorpos\"]\n"
                            "        stdout: StdioCollector {\n"
                            "            onStreamFinished: root.checkPointer(text)\n"
                            "        }\n"
                            "    }"
                        ),
                        QStringLiteral("    // Linux hyprctl cursorpos probe stripped on Windows (Qt hover state is authoritative)")
                    );
                    text.replace(
                        QStringLiteral(
                            "    PopupWindow {\n"
                            "        id: popup\n"
                            "        visible: root.fade > 0.01 && root.barWindow !== null && root.text !== \"\""
                        ),
                        QStringLiteral(
                            "    LazyLoader {\n"
                            "        id: popupLoader\n"
                            "        active: root.fade > 0.01 && root.barWindow !== null && root.text !== \"\"\n"
                            "        PopupWindow {\n"
                            "            id: popup\n"
                            "            visible: root.fade > 0.01 && root.barWindow !== null && root.text !== \"\""
                        )
                    );
                    text.replace(
                        QStringLiteral(
                            "                Text {\n"
                            "                    id: label\n"
                            "                    anchors.centerIn: parent\n"
                            "                    text: root.text\n"
                            "                    color: mt.textWhite\n"
                            "                    font.family: mt.fontFamily\n"
                            "                    font.pixelSize: 11\n"
                            "                    font.weight: Font.DemiBold\n"
                            "                }\n"
                            "            }\n"
                            "        }\n"
                            "    }\n"
                            "}"
                        ),
                        QStringLiteral(
                            "                Text {\n"
                            "                    id: label\n"
                            "                    anchors.centerIn: parent\n"
                            "                    text: root.text\n"
                            "                    color: mt.textWhite\n"
                            "                    font.family: mt.fontFamily\n"
                            "                    font.pixelSize: 11\n"
                            "                    font.weight: Font.DemiBold\n"
                            "                }\n"
                            "            }\n"
                            "        }\n"
                            "        }\n"
                            "    }\n"
                            "}"
                        )
                    );
                }

                // Tier-2 safe guards for EditStage, UtilKeys, UtilButtons, DiscordPill, BarLayoutService, ThemeService
                if (fi.fileName() == QStringLiteral("EditStage.qml")) {
                    text.replace(
                        QStringLiteral("readonly property bool editing: EditMode.active && EditMode.editing(root.screenName)"),
                        QStringLiteral("readonly property bool editing: !!EditMode && EditMode.active && EditMode.editing(root.screenName)")
                    );
                    text.replace(
                        QStringLiteral("x: EditMode.xFor(root.screenName, root.refWidth, root.refHeight, root.uiScale)"),
                        QStringLiteral("x: (EditMode ? EditMode.xFor(root.screenName, root.refWidth, root.refHeight, root.uiScale) : 0)")
                    );
                    text.replace(
                        QStringLiteral("y: EditMode.yFor(root.screenName, root.refWidth, root.refHeight, root.uiScale)"),
                        QStringLiteral("y: (EditMode ? EditMode.yFor(root.screenName, root.refWidth, root.refHeight, root.uiScale) : 0)")
                    );
                    text.replace(
                        QStringLiteral("scale: EditMode.scaleFor(root.screenName, root.refWidth, root.refHeight, root.uiScale)"),
                        QStringLiteral("scale: EditMode ? EditMode.scaleFor(root.screenName, root.refWidth, root.refHeight, root.uiScale) : 1")
                    );
                }
                if (fi.fileName() == QStringLiteral("UtilKeys.qml")) {
                    text.replace(
                        QStringLiteral("active: KeyboardService.running"),
                        QStringLiteral("active: !!KeyboardService && KeyboardService.running")
                    );
                    text.replace(
                        QStringLiteral("onClicked: KeyboardService.toggle()"),
                        QStringLiteral("onClicked: { if (QsDeferredBoot && !QsDeferredBoot.tier2Ready) QsDeferredBoot.ensureTier2Now(); if (KeyboardService) KeyboardService.toggle() }")
                    );
                }
                if (fi.fileName() == QStringLiteral("UtilButtons.qml")) {
                    text.replace(
                        QStringLiteral("readonly property bool recording: recorder.running || RegionService.recording"),
                        QStringLiteral("readonly property bool recording: recorder.running || (!!RegionService && RegionService.recording)")
                    );
                    text.replace(
                        QStringLiteral("if (RegionService.recording)\n                RegionService.stopRecording()\n            else\n                RegionService.show(\"record\", \"\")"),
                        QStringLiteral("if (QsDeferredBoot && !QsDeferredBoot.tier2Ready) QsDeferredBoot.ensureTier2Now()\n            if (RegionService) { if (RegionService.recording) RegionService.stopRecording(); else RegionService.show(\"record\", \"\") }")
                    );
                    text.replace(
                        QStringLiteral("RegionService.show(\"copy\", \"\")"),
                        QStringLiteral("{ if (QsDeferredBoot && !QsDeferredBoot.tier2Ready) QsDeferredBoot.ensureTier2Now(); if (RegionService) RegionService.show(\"copy\", \"\") }")
                    );
                }
                if (fi.fileName() == QStringLiteral("DiscordPill.qml")) {
                    text.replace(
                        QStringLiteral("source: DiscordVoiceService.avatarUrl(av.user, 64)"),
                        QStringLiteral("source: DiscordVoiceService ? DiscordVoiceService.avatarUrl(av.user, 64) : \"\"")
                    );
                }
                if (fi.fileName() == QStringLiteral("BarLayoutService.qml")) {
                    text.replace(
                        QStringLiteral("readonly property var modules: root.builtinModules.concat(PluginService.barModules)"),
                        QStringLiteral("readonly property var modules: root.builtinModules.concat(PluginService ? PluginService.barModules : [])")
                    );
                    text.replace(
                        QStringLiteral("const st = PluginService.stateFor(id)"),
                        QStringLiteral("const st = PluginService ? PluginService.stateFor(id) : null")
                    );
                    text.replace(
                        QStringLiteral("const missing = PluginService.barModules.map("),
                        QStringLiteral("const missing = (PluginService ? PluginService.barModules : []).map(")
                    );
                    text.replace(
                        QStringLiteral("for (const m of PluginService.barModules)"),
                        QStringLiteral("for (const m of (PluginService ? PluginService.barModules : []))")
                    );
                }
                if (fi.fileName() == QStringLiteral("ThemeService.qml")) {
                    text.replace(
                        QStringLiteral("HookService.emit("),
                        QStringLiteral("if (HookService) HookService.emit(")
                    );
                }

                // Windows defaults in SettingsService.qml:
                // - Screenshot/Snip defaults to Windows Snipping Tool instead of Linux grim/slurp overlay
                // - Desktop Focus Mode defaults OFF on Windows so desktop widgets stay visible behind
                //   normal windows after placing them in Edit Desktop (user can still enable it in Settings)
                if (fi.fileName() == QStringLiteral("SettingsService.qml")) {
                    text.replace(
                        QStringLiteral("\"region.own\": true,"),
                        QStringLiteral("\"region.own\": false,")
                    );
                    text.replace(
                        QStringLiteral("\"focus.enabled\": true,"),
                        QStringLiteral("\"focus.enabled\": false,")
                    );
                    text.replace(
                        QStringLiteral("\"wallpaper.mpvpaper\": true,"),
                        QStringLiteral(
                            "\"wallpaper.mpvpaper\": false,\n"
                            "        \"gfx.uiQuality\": \"1080p\",\n"
                            "        \"gfx.uiFps\": 60,\n"
                            "        \"wallpaper.photoResolution\": \"1440p\",\n"
                            "        \"wallpaper.videoResolution\": \"1080p\",\n"
                            "        \"wallpaper.videoFps\": 60,\n"
                            "        \"wallpaper.videoFilter\": \"fast\","
                        )
                    );
                }

                // Replace Linux QtMultimedia VideoWallpaper.qml with native Windows Media Foundation VideoWallpaperSurface
                // wired live to user-configurable resolution (720p..8K), frame rate (15..200 FPS), and scaling filter.
                if (fi.fileName() == QStringLiteral("VideoWallpaper.qml")) {
                    text = QStringLiteral(
                        "import QtQuick\n"
                        "import Quickshell\n"
                        "import qs.services\n"
                        "\n"
                        "Item {\n"
                        "    id: root\n"
                        "\n"
                        "    property string path: \"\"\n"
                        "    property string screenName: \"\"\n"
                        "    property bool paused: false\n"
                        "    property bool restartOnPause: false\n"
                        "    property bool hidden: false\n"
                        "\n"
                        "    readonly property string resMode: String(SettingsService.get(\"wallpaper.videoResolution\") || \"1080p\")\n"
                        "    readonly property int resW: resMode === \"8k\" ? 7680\n"
                        "                              : resMode === \"4k\" ? 3840\n"
                        "                              : resMode === \"1440p\" ? 2560\n"
                        "                              : resMode === \"720p\" ? 1280\n"
                        "                              : 1920\n"
                        "    readonly property int resH: resMode === \"8k\" ? 4320\n"
                        "                              : resMode === \"4k\" ? 2160\n"
                        "                              : resMode === \"1440p\" ? 1440\n"
                        "                              : resMode === \"720p\" ? 720\n"
                        "                              : 1080\n"
                        "\n"
                        "    VideoWallpaperSurface {\n"
                        "        id: surface\n"
                        "        anchors.fill: parent\n"
                        "        path: root.path\n"
                        "        paused: root.paused || root.hidden\n"
                        "        restartOnPause: root.restartOnPause\n"
                        "        maxWidth: root.resW\n"
                        "        maxHeight: root.resH\n"
                        "        targetFps: Math.max(15, Math.min(200, Number(SettingsService.get(\"wallpaper.videoFps\")) || 60))\n"
                        "        smoothScaling: SettingsService.get(\"wallpaper.videoFilter\") === \"smooth\"\n"
                        "    }\n"
                        "    readonly property alias playing: surface.playing\n"
                        "}\n"
                    );
                }

                // Always use built-in native Windows VideoWallpaperSurface instead of Linux mpvpaper,
                // ensure 3-slash file:/// URLs work for both "C:/..." and "/C:/..." paths,
                // and allow live redecode both upwards (to 4K/8K) and downwards (to 720p/1080p).
                if (fi.fileName() == QStringLiteral("WallpaperLayer.qml")) {
                    text.replace(
                        QStringLiteral("    readonly property var hyprMonitor: win.screen ? Hyprland.monitorFor(win.screen) : null"),
                        QStringLiteral("    readonly property var hyprMonitor: win.screen ? (Hyprland.monitors.values.find(m => m.name === win.screen.name) ?? Hyprland.monitorFor(win.screen)) : null")
                    );
                    text.replace(
                        QStringLiteral("obj.visible = Qt.binding(() => obj === win.currentSlot && win.previousSlot === null);"),
                        QStringLiteral("obj.visible = Qt.binding(() => obj === win.currentSlot && win.previousSlot === null && !(videoLayer.active && videoLayer.item && videoLayer.item.playing));")
                    );
                    text.replace(
                        QStringLiteral("readonly property bool useMpvpaper: SettingsService.flag(\"wallpaper.mpvpaper\") && !win.mpvpaperFailed"),
                        QStringLiteral("readonly property bool useMpvpaper: false")
                    );
                    text.replace(
                        QStringLiteral("source: image ? (\"file://\" + image) : \"\""),
                        QStringLiteral("source: image ? ((image.indexOf(\"/\") === 0 ? \"file://\" : \"file:///\") + image) : \"\"")
                    );
                    text.replace(
                        QStringLiteral("if (cur.sourceSize.width >= want.width && cur.sourceSize.height >= want.height)\n            return;"),
                        QStringLiteral("if (Math.abs(cur.sourceSize.width - want.width) <= 2 && Math.abs(cur.sourceSize.height - want.height) <= 2)\n            return;")
                    );
                    text.replace(
                        QStringLiteral("source: \"file://\" + win.shownImage,"),
                        QStringLiteral("source: (win.shownImage.indexOf(\"/\") === 0 ? \"file://\" : \"file:///\") + win.shownImage,")
                    );
                    // URL-lazy-load EditBackdrop & EditFrame so WallpaperLayer never compiles Edit Desktop chrome at boot.
                    text.remove(QStringLiteral("import qs.modules.sekai.editMode\n"));
                    text.replace(
                        QStringLiteral("    Loader {\n"
                                       "        anchors.fill: parent\n"
                                       "        active: EditMode.editing(win.screenName)\n"
                                       "\n"
                                       "        sourceComponent: EditBackdrop {\n"
                                       "            screenObj: win.screen\n"
                                       "            uiScale: win.uiScale\n"
                                       "        }\n"
                                       "    }"),
                        QStringLiteral("    Loader {\n"
                                       "        anchors.fill: parent\n"
                                       "        active: EditMode.editing(win.screenName)\n"
                                       "        source: active ? Qt.resolvedUrl(\"../editMode/EditBackdrop.qml\") : \"\"\n"
                                       "        onLoaded: {\n"
                                       "            item.screenObj = Qt.binding(() => win.screen)\n"
                                       "            item.uiScale = Qt.binding(() => win.uiScale)\n"
                                       "        }\n"
                                       "    }")
                    );
                    text.replace(
                        QStringLiteral("    Loader {\n"
                                       "        anchors.fill: parent\n"
                                       "        active: EditMode.editing(win.screenName)\n"
                                       "\n"
                                       "        sourceComponent: EditFrame {\n"
                                       "            screenObj: win.screen\n"
                                       "            uiScale: win.uiScale\n"
                                       "        }\n"
                                       "    }"),
                        QStringLiteral("    Loader {\n"
                                       "        anchors.fill: parent\n"
                                       "        active: EditMode.editing(win.screenName)\n"
                                       "        source: active ? Qt.resolvedUrl(\"../editMode/EditFrame.qml\") : \"\"\n"
                                       "        onLoaded: {\n"
                                       "            item.screenObj = Qt.binding(() => win.screen)\n"
                                       "            item.uiScale = Qt.binding(() => win.uiScale)\n"
                                       "        }\n"
                                       "    }")
                    );
                    text.replace(
                        QStringLiteral("readonly property bool focusReduced: FocusService.effectsReducedOn(win.screenName)"),
                        QStringLiteral("readonly property bool focusReduced: !!FocusService && FocusService.effectsReducedOn(win.screenName)")
                    );
                    text.replace(
                        QStringLiteral("|| (win.useMpvpaper && LockState.desktopAsleep)"),
                        QStringLiteral("|| (win.useMpvpaper && !!LockState && LockState.desktopAsleep)")
                    );
                    text.replace(
                        QStringLiteral("readonly property bool videoEditing: EditMode.editing(win.screenName)"),
                        QStringLiteral("readonly property bool videoEditing: !!EditMode && EditMode.editing(win.screenName)")
                    );
                    text.replace(
                        QStringLiteral("readonly property bool videoPausedBase: LockState.phase !== \"idle\" || win.videoCovered"),
                        QStringLiteral("readonly property bool videoPausedBase: (!!LockState && LockState.phase !== \"idle\") || win.videoCovered")
                    );
                    text.replace(
                        QStringLiteral("x: EditMode.xFor(win.screenName, win.width, win.height, win.uiScale)"),
                        QStringLiteral("x: EditMode ? EditMode.xFor(win.screenName, win.width, win.height, win.uiScale) : 0")
                    );
                    text.replace(
                        QStringLiteral("y: EditMode.yFor(win.screenName, win.width, win.height, win.uiScale)"),
                        QStringLiteral("y: EditMode ? EditMode.yFor(win.screenName, win.width, win.height, win.uiScale) : 0")
                    );
                    text.replace(
                        QStringLiteral("scale: EditMode.scaleFor(win.screenName, win.width, win.height, win.uiScale)"),
                        QStringLiteral("scale: EditMode ? EditMode.scaleFor(win.screenName, win.width, win.height, win.uiScale) : 1")
                    );
                    text.replace(
                        QStringLiteral("enabled: EditMode.active"),
                        QStringLiteral("enabled: !!EditMode && EditMode.active")
                    );
                    text.replace(
                        QStringLiteral("enabled: !EditMode.active"),
                        QStringLiteral("enabled: !EditMode || !EditMode.active")
                    );
                    text.replace(
                        QStringLiteral("onClicked: EditMode.select(\"\")"),
                        QStringLiteral("onClicked: { if (EditMode) EditMode.select(\"\") }")
                    );
                    text.replace(
                        QStringLiteral("onClicked: mouse => EditMode.openMenu(win.screenName, mouse.x, mouse.y)"),
                        QStringLiteral("onClicked: mouse => { if (QsDeferredBoot && !QsDeferredBoot.tier2Ready) QsDeferredBoot.ensureTier2Now(); if (EditMode) EditMode.openMenu(win.screenName, mouse.x, mouse.y) }")
                    );
                    text.replace(
                        QStringLiteral("active: EditMode.editing(win.screenName)"),
                        QStringLiteral("active: !!EditMode && EditMode.editing(win.screenName)")
                    );
                }

                // Bulletproof case-insensitive Windows thumbnail lookup (FolderListModel emits lowercase
                // "c:/..." on Windows while QFileInfo emits "C:/..." or "/C:/..."), fast scan startup,
                // and always emits valid 3-slash file:/// URLs.
                if (fi.fileName() == QStringLiteral("WallpaperService.qml")) {
                    text.replace(
                        QStringLiteral("HookService.emit("),
                        QStringLiteral("if (HookService) HookService.emit(")
                    );
                    text.replace(
                        QStringLiteral(
                            "    function thumbUrl(path) {\n"
                            "        if (!path)\n"
                            "            return \"\";\n"
                            "        const t = SettingsService.flag(\"wallpaper.thumbs\") ? root.thumbs[path] : undefined;\n"
                            "        if (t)\n"
                            "            return \"file://\" + t;\n"
                            "        return root.isVideo(path) ? \"\" : \"file://\" + path;\n"
                            "    }"
                        ),
                        QStringLiteral(
                            "    function thumbKey(path) {\n"
                            "        if (!path)\n"
                            "            return \"\";\n"
                            "        return String(path).replace(/\\\\/g, \"/\").replace(/^\\/([A-Za-z]:)/, \"$1\").toLowerCase();\n"
                            "    }\n"
                            "\n"
                            "    function thumbUrl(path) {\n"
                            "        if (!path)\n"
                            "            return \"\";\n"
                            "        const k = root.thumbKey(path);\n"
                            "        const t = SettingsService.flag(\"wallpaper.thumbs\") ? (root.thumbs[k] || root.thumbs[path]) : undefined;\n"
                            "        if (t)\n"
                            "            return (t.indexOf(\"/\") === 0 ? \"file://\" : \"file:///\") + t;\n"
                            "        return root.isVideo(path) ? \"\" : ((path.indexOf(\"/\") === 0 ? \"file://\" : \"file:///\") + path);\n"
                            "    }"
                        )
                    );
                    text.replace(
                        QStringLiteral(
                            "    Timer {\n"
                            "        id: scanDebounce\n"
                            "        interval: 700\n"
                            "        onTriggered: root.scanThumbs()\n"
                            "    }"
                        ),
                        QStringLiteral(
                            "    Timer {\n"
                            "        id: scanDebounce\n"
                            "        interval: 100\n"
                            "        onTriggered: root.scanThumbs()\n"
                            "    }\n"
                            "\n"
                            "    Component.onCompleted: scanDebounce.restart()"
                        )
                    );
                    text.replace(
                        QStringLiteral(
                            "                    if (p[2] === \"1\")\n"
                            "                        next[p[0]] = p[1];"
                        ),
                        QStringLiteral(
                            "                    if (p[2] === \"1\")\n"
                            "                        next[root.thumbKey(p[0])] = p[1];"
                        )
                    );
                    text.replace(
                        QStringLiteral(
                            "                    next[p[1]] = p[2];"
                        ),
                        QStringLiteral(
                            "                    next[root.thumbKey(p[1])] = p[2];"
                        )
                    );
                }

                // Update Desktop Settings with Graphics & FPS controls (UI, Photo Wallpaper, Video Wallpaper up to 200 FPS & 8K)
                if (fi.fileName() == QStringLiteral("SettingsPageDesktop.qml")) {
                    text.replace(
                        QStringLiteral("    SectionCard {\n        title: \"Wallpaper\""),
                        QStringLiteral(
                            "    SectionCard {\n"
                            "        title: \"Graphics & FPS\"\n"
                            "\n"
                            "        SettingChoice {\n"
                            "            label: \"UI graphics quality\"\n"
                            "            sublabel: \"Offscreen supersampling, multisample anti-aliasing and 2.5D phone slices (up to 8K)\"\n"
                            "            current: String(SettingsService.get(\"gfx.uiQuality\") || \"1080p\")\n"
                            "            options: [\n"
                            "                { id: \"720p\", label: \"720p Low\", icon: \"battery_saver\" },\n"
                            "                { id: \"1080p\", label: \"1080p FHD\", icon: \"desktop_windows\" },\n"
                            "                { id: \"1440p\", label: \"2K QHD\", icon: \"high_quality\" },\n"
                            "                { id: \"4k\", label: \"4K UHD\", icon: \"4k\" },\n"
                            "                { id: \"8k\", label: \"8K Ultra\", icon: \"auto_awesome\" }\n"
                            "            ]\n"
                            "            onPicked: id => SettingsService.set(\"gfx.uiQuality\", id)\n"
                            "        }\n"
                            "\n"
                            "        SettingChoice {\n"
                            "            label: \"UI frame rate preset\"\n"
                            "            sublabel: \"Refresh target for visualizer, lyrics and shell animations (up to 200 FPS)\"\n"
                            "            current: String(SettingsService.get(\"gfx.uiFps\") || 60)\n"
                            "            options: [\n"
                            "                { id: \"30\", label: \"30 FPS\", icon: \"battery_saver\" },\n"
                            "                { id: \"60\", label: \"60 FPS\", icon: \"speed\" },\n"
                            "                { id: \"120\", label: \"120 FPS\", icon: \"bolt\" },\n"
                            "                { id: \"144\", label: \"144 FPS\", icon: \"bolt\" },\n"
                            "                { id: \"200\", label: \"200 FPS\", icon: \"rocket_launch\" }\n"
                            "            ]\n"
                            "            onPicked: id => SettingsService.set(\"gfx.uiFps\", Number(id))\n"
                            "        }\n"
                            "\n"
                            "        SettingSlider {\n"
                            "            width: page.halfWidth\n"
                            "            icon: \"speed\"\n"
                            "            label: \"UI max frame rate\"\n"
                            "            from: 15\n"
                            "            to: 200\n"
                            "            step: 5\n"
                            "            suffix: \" FPS\"\n"
                            "            value: Number(SettingsService.get(\"gfx.uiFps\")) || 60\n"
                            "            onMoved: v => SettingsService.set(\"gfx.uiFps\", Math.round(v))\n"
                            "        }\n"
                            "\n"
                            "        SettingChoice {\n"
                            "            label: \"Photo wallpaper graphics\"\n"
                            "            sublabel: \"Decode resolution for image wallpapers (720p saves RAM; 4K/8K keeps full detail)\"\n"
                            "            current: String(SettingsService.get(\"wallpaper.photoResolution\") || \"1440p\")\n"
                            "            options: [\n"
                            "                { id: \"screen\", label: \"Auto\", icon: \"desktop_windows\" },\n"
                            "                { id: \"720p\", label: \"720p\", icon: \"battery_saver\" },\n"
                            "                { id: \"1080p\", label: \"1080p\", icon: \"desktop_windows\" },\n"
                            "                { id: \"1440p\", label: \"2K\", icon: \"high_quality\" },\n"
                            "                { id: \"4k\", label: \"4K\", icon: \"4k\" },\n"
                            "                { id: \"8k\", label: \"8K\", icon: \"auto_awesome\" }\n"
                            "            ]\n"
                            "            onPicked: id => SettingsService.set(\"wallpaper.photoResolution\", id)\n"
                            "        }\n"
                            "\n"
                            "        SettingChoice {\n"
                            "            label: \"Video wallpaper graphics\"\n"
                            "            sublabel: \"Render resolution for live video wallpapers (720p/1080p uses less CPU & RAM; up to 8K)\"\n"
                            "            current: String(SettingsService.get(\"wallpaper.videoResolution\") || \"1080p\")\n"
                            "            options: [\n"
                            "                { id: \"720p\", label: \"720p\", icon: \"battery_saver\" },\n"
                            "                { id: \"1080p\", label: \"1080p\", icon: \"desktop_windows\" },\n"
                            "                { id: \"1440p\", label: \"2K\", icon: \"high_quality\" },\n"
                            "                { id: \"4k\", label: \"4K\", icon: \"4k\" },\n"
                            "                { id: \"8k\", label: \"8K\", icon: \"auto_awesome\" }\n"
                            "            ]\n"
                            "            onPicked: id => SettingsService.set(\"wallpaper.videoResolution\", id)\n"
                            "        }\n"
                            "\n"
                            "        SettingChoice {\n"
                            "            label: \"Video wallpaper FPS preset\"\n"
                            "            sublabel: \"24/30 FPS cuts CPU load in half; up to 200 FPS for high-framerate videos\"\n"
                            "            current: String(SettingsService.get(\"wallpaper.videoFps\") || 60)\n"
                            "            options: [\n"
                            "                { id: \"24\", label: \"24 FPS\", icon: \"movie\" },\n"
                            "                { id: \"30\", label: \"30 FPS\", icon: \"battery_saver\" },\n"
                            "                { id: \"60\", label: \"60 FPS\", icon: \"speed\" },\n"
                            "                { id: \"120\", label: \"120 FPS\", icon: \"bolt\" },\n"
                            "                { id: \"144\", label: \"144 FPS\", icon: \"bolt\" },\n"
                            "                { id: \"200\", label: \"200 FPS\", icon: \"rocket_launch\" }\n"
                            "            ]\n"
                            "            onPicked: id => SettingsService.set(\"wallpaper.videoFps\", Number(id))\n"
                            "        }\n"
                            "\n"
                            "        SettingSlider {\n"
                            "            width: page.halfWidth\n"
                            "            icon: \"speed\"\n"
                            "            label: \"Video wallpaper max FPS\"\n"
                            "            from: 15\n"
                            "            to: 200\n"
                            "            step: 5\n"
                            "            suffix: \" FPS\"\n"
                            "            value: Number(SettingsService.get(\"wallpaper.videoFps\")) || 60\n"
                            "            onMoved: v => SettingsService.set(\"wallpaper.videoFps\", Math.round(v))\n"
                            "        }\n"
                            "\n"
                            "        SettingChoice {\n"
                            "            label: \"Video scaling filter\"\n"
                            "            sublabel: \"Fast uses direct single-pass sampling (lowest CPU); Smooth uses bilinear filtering\"\n"
                            "            current: String(SettingsService.get(\"wallpaper.videoFilter\") || \"fast\")\n"
                            "            options: [\n"
                            "                { id: \"fast\", label: \"Fast (Low CPU)\", icon: \"bolt\" },\n"
                            "                { id: \"smooth\", label: \"Smooth (HQ)\", icon: \"high_quality\" }\n"
                            "            ]\n"
                            "            onPicked: id => SettingsService.set(\"wallpaper.videoFilter\", id)\n"
                            "        }\n"
                            "    }\n"
                            "\n"
                            "    SectionCard {\n"
                            "        title: \"Wallpaper\""
                        )
                    );
                    text.replace(
                        QStringLiteral(
                            "            detail: !SettingsService.flag(\"wallpaper.video\") ? \"First frame only\"\n"
                            "                : SettingsService.flag(\"wallpaper.mpvpaper\") ? \"Through mpvpaper\" : \"Needs qt6-multimedia\""
                        ),
                        QStringLiteral(
                            "            detail: !SettingsService.flag(\"wallpaper.video\") ? \"First frame only\"\n"
                            "                : (\"Windows Media Foundation \\u00b7 \" + String(SettingsService.get(\"wallpaper.videoResolution\") || \"1080p\").toUpperCase() + \" @ \" + (SettingsService.get(\"wallpaper.videoFps\") || 60) + \" FPS\")"
                        )
                    );
                    text.replace(
                        QStringLiteral(
                            "        SettingToggle {\n"
                            "            icon: \"speed\"\n"
                            "            label: \"Use mpvpaper for video\""
                        ),
                        QStringLiteral(
                            "        SettingToggle {\n"
                            "            visible: false\n"
                            "            icon: \"speed\"\n"
                            "            label: \"Use mpvpaper for video\""
                        )
                    );
                }

                // Also expose Graphics & FPS quick controls on Settings > Quick (01 Quick)
                if (fi.fileName() == QStringLiteral("SettingsPageQuick.qml")) {
                    text.replace(
                        QStringLiteral("    SectionCard {\n        title: \"Shell\""),
                        QStringLiteral(
                            "    SectionCard {\n"
                            "        title: \"Graphics & FPS\"\n"
                            "\n"
                            "        SettingChoice {\n"
                            "            label: \"UI graphics\"\n"
                            "            sublabel: \"Phone & panel render quality up to 8K\"\n"
                            "            current: String(SettingsService.get(\"gfx.uiQuality\") || \"1080p\")\n"
                            "            options: [\n"
                            "                { id: \"720p\", label: \"720p\", icon: \"battery_saver\" },\n"
                            "                { id: \"1080p\", label: \"1080p\", icon: \"desktop_windows\" },\n"
                            "                { id: \"1440p\", label: \"2K\", icon: \"high_quality\" },\n"
                            "                { id: \"4k\", label: \"4K\", icon: \"4k\" },\n"
                            "                { id: \"8k\", label: \"8K\", icon: \"auto_awesome\" }\n"
                            "            ]\n"
                            "            onPicked: id => SettingsService.set(\"gfx.uiQuality\", id)\n"
                            "        }\n"
                            "\n"
                            "        SettingChoice {\n"
                            "            label: \"UI frame rate\"\n"
                            "            sublabel: \"Visualizer, lyrics & animation refresh rate up to 200 FPS\"\n"
                            "            current: String(SettingsService.get(\"gfx.uiFps\") || 60)\n"
                            "            options: [\n"
                            "                { id: \"30\", label: \"30 FPS\", icon: \"battery_saver\" },\n"
                            "                { id: \"60\", label: \"60 FPS\", icon: \"speed\" },\n"
                            "                { id: \"120\", label: \"120 FPS\", icon: \"bolt\" },\n"
                            "                { id: \"144\", label: \"144 FPS\", icon: \"bolt\" },\n"
                            "                { id: \"200\", label: \"200 FPS\", icon: \"rocket_launch\" }\n"
                            "            ]\n"
                            "            onPicked: id => SettingsService.set(\"gfx.uiFps\", Number(id))\n"
                            "        }\n"
                            "\n"
                            "        SettingChoice {\n"
                            "            label: \"Photo wallpaper\"\n"
                            "            sublabel: \"Decode resolution for photo wallpapers up to 8K\"\n"
                            "            current: String(SettingsService.get(\"wallpaper.photoResolution\") || \"1440p\")\n"
                            "            options: [\n"
                            "                { id: \"screen\", label: \"Auto\", icon: \"desktop_windows\" },\n"
                            "                { id: \"720p\", label: \"720p\", icon: \"battery_saver\" },\n"
                            "                { id: \"1080p\", label: \"1080p\", icon: \"desktop_windows\" },\n"
                            "                { id: \"1440p\", label: \"2K\", icon: \"high_quality\" },\n"
                            "                { id: \"4k\", label: \"4K\", icon: \"4k\" },\n"
                            "                { id: \"8k\", label: \"8K\", icon: \"auto_awesome\" }\n"
                            "            ]\n"
                            "            onPicked: id => SettingsService.set(\"wallpaper.photoResolution\", id)\n"
                            "        }\n"
                            "\n"
                            "        SettingChoice {\n"
                            "            label: \"Video wallpaper\"\n"
                            "            sublabel: \"Render resolution for video wallpapers up to 8K\"\n"
                            "            current: String(SettingsService.get(\"wallpaper.videoResolution\") || \"1080p\")\n"
                            "            options: [\n"
                            "                { id: \"720p\", label: \"720p\", icon: \"battery_saver\" },\n"
                            "                { id: \"1080p\", label: \"1080p\", icon: \"desktop_windows\" },\n"
                            "                { id: \"1440p\", label: \"2K\", icon: \"high_quality\" },\n"
                            "                { id: \"4k\", label: \"4K\", icon: \"4k\" },\n"
                            "                { id: \"8k\", label: \"8K\", icon: \"auto_awesome\" }\n"
                            "            ]\n"
                            "            onPicked: id => SettingsService.set(\"wallpaper.videoResolution\", id)\n"
                            "        }\n"
                            "\n"
                            "        SettingChoice {\n"
                            "            label: \"Video FPS\"\n"
                            "            sublabel: \"Frame rate cap for video wallpapers (24/30 saves CPU; up to 200 FPS)\"\n"
                            "            current: String(SettingsService.get(\"wallpaper.videoFps\") || 60)\n"
                            "            options: [\n"
                            "                { id: \"24\", label: \"24 FPS\", icon: \"movie\" },\n"
                            "                { id: \"30\", label: \"30 FPS\", icon: \"battery_saver\" },\n"
                            "                { id: \"60\", label: \"60 FPS\", icon: \"speed\" },\n"
                            "                { id: \"120\", label: \"120 FPS\", icon: \"bolt\" },\n"
                            "                { id: \"144\", label: \"144 FPS\", icon: \"bolt\" },\n"
                            "                { id: \"200\", label: \"200 FPS\", icon: \"rocket_launch\" }\n"
                            "            ]\n"
                            "            onPicked: id => SettingsService.set(\"wallpaper.videoFps\", Number(id))\n"
                            "        }\n"
                            "    }\n"
                            "\n"
                            "    SectionCard {\n"
                            "        title: \"Shell\""
                        )
                    );
                }

                // Fix null-player guard in MediaPopup.qml
                if (fi.fileName() == QStringLiteral("MediaPopup.qml")) {
                    text.replace(
                        QStringLiteral("        if (root.hasPlayer)\n            root.p.positionChanged()"),
                        QStringLiteral("        if (root.hasPlayer && root.p)\n            root.p.positionChanged()")
                    );
                    text.replace(
                        QStringLiteral("        onTriggered: root.p.positionChanged()"),
                        QStringLiteral("        onTriggered: { if (root.p) root.p.positionChanged() }")
                    );
                }

                // Emergency recovery items in Desktop Right-Click Menu (pure text + Solar icons)
                if (fi.fileName() == QStringLiteral("DesktopMenu.qml")) {
                    text.replace(
                        QStringLiteral("                    MaterialIcon {\n                        anchors.centerIn: parent\n                        name: \"palette\"\n                        size: 18\n                        weight: 600\n                        color: mt.ink\n                    }\n                }"),
                        QStringLiteral(
                            "                    MaterialIcon {\n"
                            "                        anchors.centerIn: parent\n"
                            "                        name: \"palette\"\n"
                            "                        size: 18\n"
                            "                        weight: 600\n"
                            "                        color: mt.ink\n"
                            "                    }\n"
                            "                }\n"
                            "                TrayMenuRow {\n"
                            "                    width: col.width\n"
                            "                    separator: true\n"
                            "                }\n"
                            "                TrayMenuRow {\n"
                            "                    width: col.width\n"
                            "                    text: \"Toggle Win Taskbar\"\n"
                            "                    onClicked: {\n"
                            "                        EditMode.closeMenu()\n"
                            "                        win.ipc(\"shell\", \"toggletaskbar\")\n"
                            "                    }\n"
                            "                    MaterialIcon {\n"
                            "                        anchors.centerIn: parent\n"
                            "                        name: \"desktop_windows\"\n"
                            "                        size: 18\n"
                            "                        weight: 600\n"
                            "                        color: mt.ink\n"
                            "                    }\n"
                            "                }\n"
                            "                TrayMenuRow {\n"
                            "                    width: col.width\n"
                            "                    text: \"Restart SekaiPop\"\n"
                            "                    onClicked: {\n"
                            "                        EditMode.closeMenu()\n"
                            "                        win.ipc(\"shell\", \"restart\")\n"
                            "                    }\n"
                            "                    MaterialIcon {\n"
                            "                        anchors.centerIn: parent\n"
                            "                        name: \"restart_alt\"\n"
                            "                        size: 18\n"
                            "                        weight: 600\n"
                            "                        color: mt.ink\n"
                            "                    }\n"
                            "                }\n"
                            "                TrayMenuRow {\n"
                            "                    width: col.width\n"
                            "                    text: \"Exit SekaiPop\"\n"
                            "                    onClicked: {\n"
                            "                        EditMode.closeMenu()\n"
                            "                        win.ipc(\"shell\", \"exit\")\n"
                            "                    }\n"
                            "                    MaterialIcon {\n"
                            "                        anchors.centerIn: parent\n"
                            "                        name: \"close\"\n"
                            "                        size: 18\n"
                            "                        weight: 600\n"
                            "                        color: mt.ink\n"
                            "                    }\n"
                            "                }"
                        )
                    );
                }

                // Emergency Exit SekaiPop button in Quick Panel Power Menu rail
                if (fi.fileName() == QStringLiteral("PowerMenu.qml")) {
                    text.replace(
                        QStringLiteral("            case 7: root.systemMonitorRequested(); break"),
                        QStringLiteral("            case 7: root.systemMonitorRequested(); break\n            case 8: root.exitShellRequested(); break")
                    );
                    text.replace(
                        QStringLiteral("    signal systemMonitorRequested()"),
                        QStringLiteral("    signal systemMonitorRequested()\n    signal exitShellRequested()")
                    );
                    text.replace(
                        QStringLiteral("        \"monitor_heart\"\n    ]"),
                        QStringLiteral("        \"monitor_heart\",\n        \"close\"\n    ]")
                    );
                    text.replace(
                        QStringLiteral("        \"System monitor\"\n    ]"),
                        QStringLiteral("        \"System monitor\",\n        \"Exit SekaiPop\"\n    ]")
                    );
                }
                if (fi.fileName() == QStringLiteral("VisualizerService.qml")) {
                    // On Windows, WASAPI loopback works for all desktop audio (browsers, games,
                    // Voicemeeter, Discord, Spotify) even without SMTC, so default alwaysListen to true,
                    // and default perf to Balanced (1 = ~35fps) to cut GPU/CPU overhead nearly in half.
                    text.replace(
                        QStringLiteral("    property bool alwaysListen: false"),
                        QStringLiteral("    property bool alwaysListen: true")
                    );
                    text.replace(
                        QStringLiteral("    property int perf: 0"),
                        QStringLiteral("    property int perf: 1")
                    );
                }

                if (fi.fileName() == QStringLiteral("ShaderVisualizerEngine.qml") ||
                    fi.fileName() == QStringLiteral("VisualizerEngine.qml")) {
                    // Break QSG_RENDER_LOOP=basic hidden-window FrameAnimation deadlock:
                    // VisualizerWidget stays visible:false until fade > 0.05, and FrameAnimation
                    // does not tick reliably inside a hidden window until it is mapped.
                    text.replace(
                        QStringLiteral("                root.lastSignal = Date.now();\n                ticker.running = true;"),
                        QStringLiteral("                root.lastSignal = Date.now();\n                if (root.fade < 0.08) root.fade = 0.08;\n                ticker.running = true;")
                    );
                }

                if (fi.fileName() == QStringLiteral("ShaderVisualizerLayer.qml")) {
                    // ribbons.frag only reads b0..b12, not p0..p12; skip 13 wasted QML property writes per frame.
                    text.replace(
                        QStringLiteral("                for (let k = 0; k < 13; k++) {\n"
                                       "                    const i = k * 4;\n"
                                       "                    root[`b${k}`] = Qt.vector4d(s[i], s[i + 1], s[i + 2], s[i + 3]);\n"
                                       "                    root[`p${k}`] = Qt.vector4d(p[i], p[i + 1], p[i + 2], p[i + 3]);\n"
                                       "                }"),
                        QStringLiteral("                for (let k = 0; k < 13; k++) {\n"
                                       "                    const i = k * 4;\n"
                                       "                    root[`b${k}`] = Qt.vector4d(s[i], s[i + 1], s[i + 2], s[i + 3]);\n"
                                       "                }")
                    );
                }

                if (fi.fileName() == QStringLiteral("VisualizerWidget.qml")) {
                    // Band visualizers never pop/scale, so avoid 0.12*1920 = 230px of empty swapchain waste.
                    text.replace(
                        QStringLiteral("    readonly property real boxPad: Math.ceil((0.12 * Math.max(card.width, card.height) + 16) * win.uiScale)"),
                        QStringLiteral("    readonly property real boxPad: Math.ceil(((win.isBand ? 0 : 0.12 * Math.max(card.width, card.height)) + 16) * win.uiScale)")
                    );
                    // Balanced performance mode at ~60fps (16ms gap) stays silky smooth with swapInterval=0,
                    // while pausing/throttling the full-screen 1080p demo shader during Edit Desktop so
                    // Intel iGPUs stay responsive under QSG_RENDER_LOOP=basic.
                    text.replace(
                        QStringLiteral("        frameGap: gate.dimmed ? 0.1 : (VisualizerService.perf >= 2 ? 0.05 : (VisualizerService.perf === 1 ? 0.028 : 0))"),
                        QStringLiteral("        frameGap: (EditMode.progress > 0.01 && EditMode.progress < 0.99) ? 1.0 : (EditMode.active ? 0.125 : (gate.dimmed ? 0.1 : (VisualizerService.perf >= 2 ? 0.05 : (VisualizerService.perf === 1 ? 0.016 : 0))))")
                    );
                    text.replace(
                        QStringLiteral("        frameGap: gate.dimmed ? Math.max(0.1, VisualizerService.frameGap) : VisualizerService.frameGap"),
                        QStringLiteral("        frameGap: (EditMode.progress > 0.01 && EditMode.progress < 0.99) ? 1.0 : (EditMode.active ? 0.125 : (gate.dimmed ? Math.max(0.1, VisualizerService.frameGap) : VisualizerService.frameGap))")
                    );
                    // Only keep the 512x512 cover FBO live when actually rendering the Ring style.
                    text.replace(
                        QStringLiteral("                                        id: coverTexture\n"
                                       "                                        sourceItem: coverItem\n"
                                       "                                        hideSource: true"),
                        QStringLiteral("                                        id: coverTexture\n"
                                       "                                        sourceItem: coverItem\n"
                                       "                                        live: win.isRing\n"
                                       "                                        hideSource: true")
                    );
                    // Let the plain-QML 'bars' style animate in Edit Desktop demo mode too.
                    text.replace(
                        QStringLiteral("                                            points: pcEngine.raw"),
                        QStringLiteral("                                            points: pcEngine.demo ? pcEngine.synth(pcEngine.time) : pcEngine.raw")
                    );
                    // Make Visualizer themePalette purely follow the active theme/wallpaper across all 3 layers
                    // (replacing the hardcoded irisColorPink front wave) and have Cover mode fall back to the active
                    // theme instead of hardcoded Sekai colors when no cover art is present.
                    text.replace(
                        QStringLiteral(
                            "    readonly property var themePalette: {\n"
                            "        const A = mt.accentCyan, B = mt.accentMint, P = mt.irisColorPink\n"
                            "        switch (win.style) {\n"
                            "            case \"ring\": return [A, B, Qt.lighter(A, 1.25)]\n"
                            "            case \"dots\": return [win.neutralInk, A, P]\n"
                            "            default: return [A, B, P]\n"
                            "        }\n"
                            "    }"
                        ),
                        QStringLiteral(
                            "    readonly property var themePalette: {\n"
                            "        const A = mt.accentCyan, B = mt.accentMint\n"
                            "        const C = Qt.lighter(mt.phoneWallMid, 1.25)\n"
                            "        switch (win.style) {\n"
                            "            case \"ring\": return [A, B, Qt.lighter(A, 1.25)]\n"
                            "            case \"dots\": return [win.neutralInk, A, B]\n"
                            "            default: return [A, B, C]\n"
                            "        }\n"
                            "    }"
                        )
                    );
                    text.replace(
                        QStringLiteral(
                            "    readonly property var shapeColors: {\n"
                            "        const mode = VisualizerService.colorMode\n"
                            "        if (mode === 2) return win.themePalette\n"
                            "        if (mode === 0 && cover.palette) {\n"
                            "            const c = cover.palette\n"
                            "            return win.style === \"dots\" ? [win.neutralInk, c[0], c[1]] : c\n"
                            "        }\n"
                            "        return win.attrPalette\n"
                            "    }"
                        ),
                        QStringLiteral(
                            "    readonly property var shapeColors: {\n"
                            "        const mode = VisualizerService.colorMode\n"
                            "        if (mode === 2) return win.themePalette\n"
                            "        if (mode === 0) {\n"
                            "            if (cover.palette) {\n"
                            "                const c = cover.palette\n"
                            "                return win.style === \"dots\" ? [win.neutralInk, c[0], c[1]] : c\n"
                            "            }\n"
                            "            return win.themePalette\n"
                            "        }\n"
                            "        return win.attrPalette\n"
                            "    }"
                        )
                    );
                }

                if (fi.fileName() == QStringLiteral("Bar.qml")) {
                    // Ukikura Dynamic Island integration: expose per-monitor top notch gap to Status_Bar
                    // and punch the notch cutout out of Bar's input mask so hover/click/drop go to Ukikura.
                    text.replace(
                        QStringLiteral(
                            "    mask: Region {\n"
                            "        item: barMask\n"
                            "        regions: [ Region { item: hotCut; intersection: Intersection.Subtract } ]\n"
                            "    }\n"
                            "    Item {\n"
                            "        id: hotCut"
                        ),
                        QStringLiteral(
                            "    readonly property var ukikuraInfo: (!win.vertical && win.barPosition === \"top\" && !win.editing && win.screen && Quickshell.ukikuraNotches)\n"
                            "        ? (Quickshell.ukikuraNotches[win.screen.name] || null) : null\n"
                            "    readonly property real ukikuraGap: {\n"
                            "        const info = win.ukikuraInfo\n"
                            "        if (!info)\n"
                            "            return 0\n"
                            "        const sc = Number(info.scale) || 1.0\n"
                            "        const musicWide = !!info.featureMusic && !info.minimal && mediaSource.present && mediaSource.playing\n"
                            "        const baseDip = info.minimal ? 188 : (musicWide ? 392 : 216)\n"
                            "        return Math.round((baseDip * sc) / Math.max(0.5, win.uiScale))\n"
                            "    }\n"
                            "    mask: Region {\n"
                            "        item: barMask\n"
                            "        regions: [\n"
                            "            Region { item: hotCut; intersection: Intersection.Subtract },\n"
                            "            Region { item: ukikuraCut; intersection: Intersection.Subtract }\n"
                            "        ]\n"
                            "    }\n"
                            "    Item {\n"
                            "        id: ukikuraCut\n"
                            "        readonly property bool on: win.barPosition === \"top\" && !win.passive && win.ukikuraGap > 1\n"
                            "        width: on ? Math.min(win.width, Math.round((statusBar ? statusBar.effectiveUkikuraGap : win.ukikuraGap) * win.uiScale)) : 0\n"
                            "        height: on ? win.stripSize : 0\n"
                            "        x: (win.width - width) / 2\n"
                            "        y: 0\n"
                            "    }\n"
                            "    Item {\n"
                            "        id: hotCut"
                        )
                    );
                    text.replace(
                        QStringLiteral(
                            "            Status_Bar {\n"
                            "                id: statusBar\n"
                            "                vertical: win.vertical\n"
                            "                barPosition: win.barPosition"
                        ),
                        QStringLiteral(
                            "            Status_Bar {\n"
                            "                id: statusBar\n"
                            "                vertical: win.vertical\n"
                            "                barPosition: win.barPosition\n"
                            "                ukikuraGap: win.ukikuraGap"
                        )
                    );
                    // Preserve upstream growsFromOrigin (compactWindow on top/left only; full-screen
                    // transparent click-through on right/bottom) so right/bottom bars never flash on
                    // the opposite edge when expanding/shrinking for Edit Desktop.
                    text.replace(
                        QStringLiteral("                mediaPoints: PcCavaService.points"),
                        QStringLiteral("                mediaPoints: win.vertical ? [] : PcCavaService.points")
                    );
                    // Defer all 8 heavy Bar popups to URL-based LazyLoader so Bar.qml compiles fast at boot.
                    text.remove(QStringLiteral("import qs.modules.sekai.equalizer\n"));
                    text.remove(QStringLiteral("import qs.modules.sekai.mediaControls\n"));
                    // Defer TrayPopups (26KB + TrayMenu 9KB + 2 PopupWindows per monitor) until tray is actually opened/clicked
                    text.replace(
                        QStringLiteral(
                            "    TrayPopups {\n"
                            "        id: trayPopups\n"
                            "        parentWindow: win\n"
                            "        source: win.traySource\n"
                            "        toggleAnchor: statusBar.trayToggle\n"
                            "        open: win.trayOpen\n"
                            "        onCloseRequested: win.trayOpen = false\n"
                            "    }"
                        ),
                        QStringLiteral(
                            "    Scope {\n"
                            "        id: trayPopups\n"
                            "        property bool wantLoad: false\n"
                            "        readonly property var real: trayPopupsLoader.item\n"
                            "        readonly property bool showing: real ? real.showing : false\n"
                            "        readonly property var menuItem: real ? real.menuItem : null\n"
                            "        readonly property var leavingItem: real ? real.leavingItem : null\n"
                            "        Connections {\n"
                            "            target: win\n"
                            "            function onTrayOpenChanged() { if (win.trayOpen) trayPopups.wantLoad = true }\n"
                            "        }\n"
                            "        function ensureReal() {\n"
                            "            if (!wantLoad) wantLoad = true\n"
                            "            return trayPopupsLoader.item\n"
                            "        }\n"
                            "        function handleClick(item, button, icon, fromPop) {\n"
                            "            const r = ensureReal()\n"
                            "            if (r) r.handleClick(item, button, icon, fromPop)\n"
                            "        }\n"
                            "        function handleScroll(item, delta, horizontal) {\n"
                            "            const r = ensureReal()\n"
                            "            if (r) r.handleScroll(item, delta, horizontal)\n"
                            "        }\n"
                            "        LazyLoader {\n"
                            "            id: trayPopupsLoader\n"
                            "            active: trayPopups.wantLoad\n"
                            "            source: Qt.resolvedUrl(\"TrayPopups.qml\")\n"
                            "            onItemChanged: {\n"
                            "                if (!item) return\n"
                            "                item.parentWindow = win\n"
                            "                item.source = Qt.binding(() => (win.traySource && win.traySource.real) ? win.traySource.real : win.traySource)\n"
                            "                item.toggleAnchor = Qt.binding(() => statusBar.trayToggle)\n"
                            "                item.open = Qt.binding(() => win.trayOpen)\n"
                            "                item.closeRequested.connect(() => { win.trayOpen = false })\n"
                            "            }\n"
                            "        }\n"
                            "    }"
                        )
                    );
                    text.replace(
                        QStringLiteral(
                            "        MediaPopup {\n"
                            "            // Starts closed and opens a beat later, so its open-handlers (focus grab,\n"
                            "            // refresh, ...) run exactly as they do when the button is clicked.\n"
                            "            property bool ready: false\n"
                            "            Component.onCompleted: Qt.callLater(() => ready = true)\n"
                            "            open: mediaPopupLazy.want && ready\n"
                            "            parentWindow: win\n"
                            "            anchorItem: statusBar.mediaToggle\n"
                            "            source: mediaSource\n"
                            "            onCloseRequested: win.mediaPopupOpen = false\n"
                            "            onEqRequested: win.openEqPopup()\n"
                            "        }"
                        ),
                        QStringLiteral(
                            "        source: Qt.resolvedUrl(\"../mediaControls/MediaPopup.qml\")\n"
                            "        onItemChanged: {\n"
                            "            if (!item) return\n"
                            "            item.parentWindow = win\n"
                            "            item.anchorItem = Qt.binding(() => statusBar.mediaToggle)\n"
                            "            item.source = mediaSource\n"
                            "            item.closeRequested.connect(() => { win.mediaPopupOpen = false })\n"
                            "            item.eqRequested.connect(() => { win.openEqPopup() })\n"
                            "            Qt.callLater(() => { if (item) item.open = Qt.binding(() => mediaPopupLazy.want) })\n"
                            "        }"
                        )
                    );
                    text.replace(
                        QStringLiteral(
                            "        EqualizerPopup {\n"
                            "            property bool ready: false\n"
                            "            Component.onCompleted: Qt.callLater(() => ready = true)\n"
                            "            open: eqPopupLazy.want && ready\n"
                            "            parentWindow: win\n"
                            "            anchorItem: statusBar.mediaToggle\n"
                            "            source: mediaSource\n"
                            "            onCloseRequested: {\n"
                            "                EqualizerService.discardIfPending()\n"
                            "                win.eqPopupOpen = false\n"
                            "            }\n"
                            "            onTurntableRequested: win.backToTurntable()\n"
                            "        }"
                        ),
                        QStringLiteral(
                            "        source: Qt.resolvedUrl(\"../equalizer/EqualizerPopup.qml\")\n"
                            "        onItemChanged: {\n"
                            "            if (!item) return\n"
                            "            item.parentWindow = win\n"
                            "            item.anchorItem = Qt.binding(() => statusBar.mediaToggle)\n"
                            "            item.source = mediaSource\n"
                            "            item.closeRequested.connect(() => { EqualizerService.discardIfPending(); win.eqPopupOpen = false })\n"
                            "            item.turntableRequested.connect(() => { win.backToTurntable() })\n"
                            "            Qt.callLater(() => { if (item) item.open = Qt.binding(() => eqPopupLazy.want) })\n"
                            "        }"
                        )
                    );
                    text.replace(
                        QStringLiteral(
                            "        DiscordPopup {\n"
                            "            // Starts closed and opens a beat later, so its open-handlers (focus grab,\n"
                            "            // refresh, ...) run exactly as they do when the button is clicked.\n"
                            "            property bool ready: false\n"
                            "            Component.onCompleted: Qt.callLater(() => ready = true)\n"
                            "            open: discordPopupLazy.want && ready\n"
                            "            parentWindow: win\n"
                            "            anchorItem: statusBar.discordToggle\n"
                            "            onCloseRequested: win.discordPopupOpen = false\n"
                            "\n"
                            "            channelName: DiscordVoiceService.channel ? (DiscordVoiceService.channel.name || \"\") : \"\"\n"
                            "            backendLabel: DiscordVoiceService.backendLabel\n"
                            "            guildName: DiscordVoiceService.channel ? (DiscordVoiceService.channel.guild_name || \"\") : \"\"\n"
                            "            guildIconUrl: DiscordVoiceService.channel ? (DiscordVoiceService.channel.guild_icon_url || \"\") : \"\"\n"
                            "            participants: DiscordVoiceService.participants\n"
                            "            selfId: DiscordVoiceService.selfId\n"
                            "            muted: DiscordVoiceService.muted\n"
                            "            deafened: DiscordVoiceService.deafened\n"
                            "            hint: SettingsService.get(\"discord.leftClick\") === \"panel\"\n"
                            "            ? \"Right-click the pill to deafen \u00b7 middle-click to mute\"\n"
                            "            : \"Left-click the pill to mute \u00b7 right-click to deafen\"\n"
                            "            onMuteRequested: DiscordVoiceService.setMuted(!DiscordVoiceService.muted)\n"
                            "            onDeafenRequested: DiscordVoiceService.setDeafened(!DiscordVoiceService.deafened)\n"
                            "            onOpenClientRequested: {\n"
                            "                if (DiscordVoiceService.focusClient())\n"
                            "                    win.discordPopupOpen = false\n"
                            "            }\n"
                            "        }"
                        ),
                        QStringLiteral(
                            "        source: Qt.resolvedUrl(\"DiscordPopup.qml\")\n"
                            "        onItemChanged: {\n"
                            "            if (!item) return\n"
                            "            item.parentWindow = win\n"
                            "            item.anchorItem = Qt.binding(() => statusBar.discordToggle)\n"
                            "            item.channelName = Qt.binding(() => DiscordVoiceService.channel ? (DiscordVoiceService.channel.name || \"\") : \"\")\n"
                            "            item.backendLabel = Qt.binding(() => DiscordVoiceService.backendLabel)\n"
                            "            item.guildName = Qt.binding(() => DiscordVoiceService.channel ? (DiscordVoiceService.channel.guild_name || \"\") : \"\")\n"
                            "            item.guildIconUrl = Qt.binding(() => DiscordVoiceService.channel ? (DiscordVoiceService.channel.guild_icon_url || \"\") : \"\")\n"
                            "            item.participants = Qt.binding(() => DiscordVoiceService.participants)\n"
                            "            item.selfId = Qt.binding(() => DiscordVoiceService.selfId)\n"
                            "            item.muted = Qt.binding(() => DiscordVoiceService.muted)\n"
                            "            item.deafened = Qt.binding(() => DiscordVoiceService.deafened)\n"
                            "            item.hint = Qt.binding(() => SettingsService.get(\"discord.leftClick\") === \"panel\" ? \"Right-click the pill to deafen \\u00b7 middle-click to mute\" : \"Left-click the pill to mute \\u00b7 right-click to deafen\")\n"
                            "            item.closeRequested.connect(() => { win.discordPopupOpen = false })\n"
                            "            item.muteRequested.connect(() => { DiscordVoiceService.setMuted(!DiscordVoiceService.muted) })\n"
                            "            item.deafenRequested.connect(() => { DiscordVoiceService.setDeafened(!DiscordVoiceService.deafened) })\n"
                            "            item.openClientRequested.connect(() => { if (DiscordVoiceService.focusClient()) win.discordPopupOpen = false })\n"
                            "            Qt.callLater(() => { if (item) item.open = Qt.binding(() => discordPopupLazy.want) })\n"
                            "        }"
                        )
                    );
                    text.replace(
                        QStringLiteral(
                            "        BatteryPopup {\n"
                            "            // Starts closed and opens a beat later, so its open-handlers (focus grab,\n"
                            "            // refresh, ...) run exactly as they do when the button is clicked.\n"
                            "            property bool ready: false\n"
                            "            Component.onCompleted: Qt.callLater(() => ready = true)\n"
                            "            open: batteryPopupLazy.want && ready\n"
                            "            parentWindow: win\n"
                            "            anchorItem: statusBar.batteryToggle\n"
                            "            onCloseRequested: win.batteryPopupOpen = false\n"
                            "\n"
                            "            percentage: batterySource.percentage\n"
                            "            statusLabel: batterySource.statusLabel\n"
                            "            timeRemaining: batterySource.timeRemaining\n"
                            "        }"
                        ),
                        QStringLiteral(
                            "        source: Qt.resolvedUrl(\"BatteryPopup.qml\")\n"
                            "        onItemChanged: {\n"
                            "            if (!item) return\n"
                            "            item.parentWindow = win\n"
                            "            item.anchorItem = Qt.binding(() => statusBar.batteryToggle)\n"
                            "            item.percentage = Qt.binding(() => batterySource.percentage)\n"
                            "            item.statusLabel = Qt.binding(() => batterySource.statusLabel)\n"
                            "            item.timeRemaining = Qt.binding(() => batterySource.timeRemaining)\n"
                            "            item.closeRequested.connect(() => { win.batteryPopupOpen = false })\n"
                            "            Qt.callLater(() => { if (item) item.open = Qt.binding(() => batteryPopupLazy.want) })\n"
                            "        }"
                        )
                    );
                    text.replace(
                        QStringLiteral(
                            "        UtilPopup {\n"
                            "            property bool ready: false\n"
                            "            Component.onCompleted: Qt.callLater(() => ready = true)\n"
                            "            open: utilPopupLazy.want && ready\n"
                            "            parentWindow: win\n"
                            "            anchorItem: statusBar.utilToggle\n"
                            "            source: statusBar.utilSource\n"
                            "            onCloseRequested: win.utilPopupOpen = false\n"
                            "        }"
                        ),
                        QStringLiteral(
                            "        source: Qt.resolvedUrl(\"UtilPopup.qml\")\n"
                            "        onItemChanged: {\n"
                            "            if (!item) return\n"
                            "            item.parentWindow = win\n"
                            "            item.anchorItem = Qt.binding(() => statusBar.utilToggle)\n"
                            "            item.source = Qt.binding(() => statusBar.utilSource)\n"
                            "            item.closeRequested.connect(() => { win.utilPopupOpen = false })\n"
                            "            Qt.callLater(() => { if (item) item.open = Qt.binding(() => utilPopupLazy.want) })\n"
                            "        }"
                        )
                    );
                    text.replace(
                        QStringLiteral(
                            "        CalendarPopup {\n"
                            "            // Starts closed and opens a beat later, so its open-handlers (focus grab,\n"
                            "            // refresh, ...) run exactly as they do when the button is clicked.\n"
                            "            property bool ready: false\n"
                            "            Component.onCompleted: Qt.callLater(() => ready = true)\n"
                            "            open: calendarPopupLazy.want && ready\n"
                            "            parentWindow: win\n"
                            "            anchorItem: statusBar.dateToggle\n"
                            "            today: clock.date\n"
                            "            onCloseRequested: win.calendarPopupOpen = false\n"
                            "        }"
                        ),
                        QStringLiteral(
                            "        source: Qt.resolvedUrl(\"CalendarPopup.qml\")\n"
                            "        onItemChanged: {\n"
                            "            if (!item) return\n"
                            "            item.parentWindow = win\n"
                            "            item.anchorItem = Qt.binding(() => statusBar.dateToggle)\n"
                            "            item.today = Qt.binding(() => clock.date)\n"
                            "            item.closeRequested.connect(() => { win.calendarPopupOpen = false })\n"
                            "            Qt.callLater(() => { if (item) item.open = Qt.binding(() => calendarPopupLazy.want) })\n"
                            "        }"
                        )
                    );
                    text.replace(
                        QStringLiteral(
                            "        WeatherPopup {\n"
                            "            // Starts closed and opens a beat later, so its open-handlers (focus grab,\n"
                            "            // refresh, ...) run exactly as they do when the button is clicked.\n"
                            "            property bool ready: false\n"
                            "            Component.onCompleted: Qt.callLater(() => ready = true)\n"
                            "            open: weatherPopupLazy.want && ready\n"
                            "            parentWindow: win\n"
                            "            anchorItem: statusBar.weatherToggle\n"
                            "            source: weatherSource\n"
                            "            onCloseRequested: win.weatherPopupOpen = false\n"
                            "        }"
                        ),
                        QStringLiteral(
                            "        source: Qt.resolvedUrl(\"WeatherPopup.qml\")\n"
                            "        onItemChanged: {\n"
                            "            if (!item) return\n"
                            "            item.parentWindow = win\n"
                            "            item.anchorItem = Qt.binding(() => statusBar.weatherToggle)\n"
                            "            item.source = weatherSource\n"
                            "            item.closeRequested.connect(() => { win.weatherPopupOpen = false })\n"
                            "            Qt.callLater(() => { if (item) item.open = Qt.binding(() => weatherPopupLazy.want) })\n"
                            "        }"
                        )
                    );
                    text.replace(
                        QStringLiteral(
                            "        ResourcePopup {\n"
                            "            // Starts closed and opens a beat later, so its open-handlers (focus grab,\n"
                            "            // refresh, ...) run exactly as they do when the button is clicked.\n"
                            "            property bool ready: false\n"
                            "            Component.onCompleted: Qt.callLater(() => ready = true)\n"
                            "            open: resourcePopupLazy.want && ready\n"
                            "            parentWindow: win\n"
                            "            anchorItem: statusBar.resourceToggle\n"
                            "            onCloseRequested: win.resourcePopupOpen = false\n"
                            "\n"
                            "            cpuPercent: resourceSource.cpuPercent\n"
                            "            memPercent: resourceSource.memPercent\n"
                            "            memUsedLabel: resourceSource.memUsedLabel\n"
                            "            memTotalLabel: resourceSource.memTotalLabel\n"
                            "        }"
                        ),
                        QStringLiteral(
                            "        source: Qt.resolvedUrl(\"ResourcePopup.qml\")\n"
                            "        onItemChanged: {\n"
                            "            if (!item) return\n"
                            "            item.parentWindow = win\n"
                            "            item.anchorItem = Qt.binding(() => statusBar.resourceToggle)\n"
                            "            item.cpuPercent = Qt.binding(() => resourceSource.cpuPercent)\n"
                            "            item.memPercent = Qt.binding(() => resourceSource.memPercent)\n"
                            "            item.memUsedLabel = Qt.binding(() => resourceSource.memUsedLabel)\n"
                            "            item.memTotalLabel = Qt.binding(() => resourceSource.memTotalLabel)\n"
                            "            item.closeRequested.connect(() => { win.resourcePopupOpen = false })\n"
                            "            Qt.callLater(() => { if (item) item.open = Qt.binding(() => resourcePopupLazy.want) })\n"
                            "        }"
                        )
                    );
                    // Tier-2 safe null guards for deferred Bar singletons (applied AFTER popup replacements)
                    text.replace(
                        QStringLiteral("EqualizerService.discardIfPending()"),
                        QStringLiteral("if (EqualizerService) EqualizerService.discardIfPending()")
                    );
                    text.replace(
                        QStringLiteral("return IconCursorService.path(entry.icon, \"application-x-executable\")"),
                        QStringLiteral("return IconCursorService ? IconCursorService.path(entry.icon, \"application-x-executable\") : \"\"")
                    );
                    text.replace(
                        QStringLiteral("readonly property bool editing: win.screen !== null && EditMode.editing(win.screen.name)"),
                        QStringLiteral("readonly property bool editing: win.screen !== null && !!EditMode && EditMode.editing(win.screen.name)")
                    );
                    text.replace(
                        QStringLiteral("HyprlandSettings.num(\"general.gaps_out\")"),
                        QStringLiteral("(HyprlandSettings ? HyprlandSettings.num(\"general.gaps_out\") : 0)")
                    );
                    text.replace(
                        QStringLiteral("    readonly property var pluginEntries: PluginService.barPlugins\n"
                                       "        .map(f => ({ key: \"plugin:\" + f.plugin.id, st: PluginService.states[f.plugin.id] ?? null }))"),
                        QStringLiteral("    readonly property var pluginEntries: (PluginService ? PluginService.barPlugins : [])\n"
                                       "        .map(f => ({ key: \"plugin:\" + f.plugin.id, st: PluginService.states[f.plugin.id] ?? null }))")
                    );
                    text.replace(
                        QStringLiteral("discordPopupOpen = DiscordVoiceService.inVoice && !discordPopupOpen"),
                        QStringLiteral("discordPopupOpen = !!DiscordVoiceService && DiscordVoiceService.inVoice && !discordPopupOpen")
                    );
                    text.replace(
                        QStringLiteral("property bool want: win.discordPopupOpen && DiscordVoiceService.inVoice"),
                        QStringLiteral("property bool want: win.discordPopupOpen && !!DiscordVoiceService && DiscordVoiceService.inVoice")
                    );
                    text.replace(
                        QStringLiteral("readonly property int activeWorkspace: NiriWorkspaces.running ? NiriWorkspaces.activeOn(screen?.name ?? \"\") : monitor?.activeWorkspace?.id ?? Hyprland.focusedWorkspace?.id ?? 1"),
                        QStringLiteral("readonly property int activeWorkspace: (NiriWorkspaces && NiriWorkspaces.running) ? NiriWorkspaces.activeOn(screen?.name ?? \"\") : monitor?.activeWorkspace?.id ?? Hyprland.focusedWorkspace?.id ?? 1")
                    );
                    text.replace(
                        QStringLiteral("readonly property var occupiedWorkspaces: NiriWorkspaces.running ? NiriWorkspaces.occupiedOn(screen?.name ?? \"\") : Hyprland.workspaces.values"),
                        QStringLiteral("readonly property var occupiedWorkspaces: (NiriWorkspaces && NiriWorkspaces.running) ? NiriWorkspaces.occupiedOn(screen?.name ?? \"\") : Hyprland.workspaces.values")
                    );
                    text.replace(
                        QStringLiteral("duration: PresetMotion.outMs"),
                        QStringLiteral("duration: PresetMotion ? PresetMotion.outMs : 220")
                    );
                    text.replace(
                        QStringLiteral("duration: PresetMotion.inMs"),
                        QStringLiteral("duration: PresetMotion ? PresetMotion.inMs : 280")
                    );
                    text.replace(
                        QStringLiteral("if (PresetMotion.barBusy) {"),
                        QStringLiteral("if (PresetMotion && PresetMotion.barBusy) {")
                    );
                    text.replace(
                        QStringLiteral("mediaVisualizerStyle: VisualizerService.pillStyle"),
                        QStringLiteral("mediaVisualizerStyle: VisualizerService ? VisualizerService.pillStyle : \"none\"")
                    );
                    text.replace(
                        QStringLiteral("mediaPoints: win.vertical ? [] : PcCavaService.points"),
                        QStringLiteral("mediaPoints: (win.vertical || !PcCavaService) ? [] : PcCavaService.points")
                    );
                    text.replace(
                        QStringLiteral("keyboardLayoutCode: KeyboardLayoutService.code"),
                        QStringLiteral("keyboardLayoutCode: KeyboardLayoutService ? KeyboardLayoutService.code : \"\"")
                    );
                    text.replace(
                        QStringLiteral("onKeyboardLayoutClicked: KeyboardLayoutService.cycle()"),
                        QStringLiteral("onKeyboardLayoutClicked: { if (KeyboardLayoutService) KeyboardLayoutService.cycle() }")
                    );
                    text.replace(
                        QStringLiteral("privacyMic: PrivacyService.micLive\n"
                                       "                privacyAudio: PrivacyService.audioLive\n"
                                       "                privacyScreen: PrivacyService.screenLive"),
                        QStringLiteral("privacyMic: !!PrivacyService && PrivacyService.micLive\n"
                                       "                privacyAudio: !!PrivacyService && PrivacyService.audioLive\n"
                                       "                privacyScreen: !!PrivacyService && PrivacyService.screenLive")
                    );
                    text.replace(
                        QStringLiteral("if (PrivacyService.screenLive)"),
                        QStringLiteral("if (PrivacyService && PrivacyService.screenLive)")
                    );
                    text.replace(
                        QStringLiteral("if (PrivacyService.micLive)"),
                        QStringLiteral("if (PrivacyService && PrivacyService.micLive)")
                    );
                    text.replace(
                        QStringLiteral("if (PrivacyService.audioLive)"),
                        QStringLiteral("if (PrivacyService && PrivacyService.audioLive)")
                    );
                    text.replace(
                        QStringLiteral(
                            "                discordInCall: DiscordVoiceService.inVoice\n"
                            "                discordNeedsAuth: DiscordVoiceService.needsAuth\n"
                            "                discordAuthorizing: DiscordVoiceService.status === \"authorizing\"\n"
                            "                discordChannel: DiscordVoiceService.channel ? (DiscordVoiceService.channel.name || \"\") : \"\"\n"
                            "                discordParticipants: DiscordVoiceService.participants\n"
                            "                discordMuted: DiscordVoiceService.muted\n"
                            "                discordDeafened: DiscordVoiceService.deafened\n"
                            "                onDiscordMuteRequested: DiscordVoiceService.setMuted(!DiscordVoiceService.muted)\n"
                            "                onDiscordDeafenRequested: DiscordVoiceService.setDeafened(!DiscordVoiceService.deafened)\n"
                            "                onDiscordAuthorizeRequested: DiscordVoiceService.authorizeAfterFocusRelease()"
                        ),
                        QStringLiteral(
                            "                discordInCall: !!DiscordVoiceService && DiscordVoiceService.inVoice\n"
                            "                discordNeedsAuth: !!DiscordVoiceService && DiscordVoiceService.needsAuth\n"
                            "                discordAuthorizing: !!DiscordVoiceService && DiscordVoiceService.status === \"authorizing\"\n"
                            "                discordChannel: (DiscordVoiceService && DiscordVoiceService.channel) ? (DiscordVoiceService.channel.name || \"\") : \"\"\n"
                            "                discordParticipants: DiscordVoiceService ? DiscordVoiceService.participants : []\n"
                            "                discordMuted: !!DiscordVoiceService && DiscordVoiceService.muted\n"
                            "                discordDeafened: !!DiscordVoiceService && DiscordVoiceService.deafened\n"
                            "                onDiscordMuteRequested: { if (DiscordVoiceService) DiscordVoiceService.setMuted(!DiscordVoiceService.muted) }\n"
                            "                onDiscordDeafenRequested: { if (DiscordVoiceService) DiscordVoiceService.setDeafened(!DiscordVoiceService.deafened) }\n"
                            "                onDiscordAuthorizeRequested: { if (DiscordVoiceService) DiscordVoiceService.authorizeAfterFocusRelease() }"
                        )
                    );
                    text.replace(
                        QStringLiteral(
                            "                updatesCount: PackageUpdatesService.total\n"
                            "                updatesNudging: PackageUpdatesService.nudging\n"
                            "                updatesShowZero: SettingsService.flag(\"updates.showZero\")\n"
                            "                updatesBusy: PackageUpdatesService.status === \"checking\" || PackageUpdatesService.upgrading\n"
                            "                updatesLoading: PackageUpdatesService.loadingBadge\n"
                            "                updatesTip: PackageUpdatesService.breakdown\n"
                            "                + (PackageUpdatesService.total > 0 ? \" — click to update, right-click to recheck\" : \" — right-click to recheck\")\n"
                            "                onUpdatesClicked: PackageUpdatesService.runUpdate()\n"
                            "                onUpdatesRefreshRequested: PackageUpdatesService.refresh(true)"
                        ),
                        QStringLiteral(
                            "                updatesCount: PackageUpdatesService ? PackageUpdatesService.total : 0\n"
                            "                updatesNudging: !!PackageUpdatesService && PackageUpdatesService.nudging\n"
                            "                updatesShowZero: SettingsService.flag(\"updates.showZero\")\n"
                            "                updatesBusy: !!PackageUpdatesService && (PackageUpdatesService.status === \"checking\" || PackageUpdatesService.upgrading)\n"
                            "                updatesLoading: !!PackageUpdatesService && PackageUpdatesService.loadingBadge\n"
                            "                updatesTip: PackageUpdatesService ? (PackageUpdatesService.breakdown + (PackageUpdatesService.total > 0 ? \" — click to update, right-click to recheck\" : \" — right-click to recheck\")) : \"\"\n"
                            "                onUpdatesClicked: { if (PackageUpdatesService) PackageUpdatesService.runUpdate() }\n"
                            "                onUpdatesRefreshRequested: { if (PackageUpdatesService) PackageUpdatesService.refresh(true) }"
                        )
                    );
                    text.replace(
                        QStringLiteral("if (NiriWorkspaces.running) {"),
                        QStringLiteral("if (NiriWorkspaces && NiriWorkspaces.running) {")
                    );
                }

                // Defer all 6 Settings pages until Settings is actually opened (0 ms at boot & QuickPanel warm-up).
                if (fi.fileName() == QStringLiteral("SettingsFace.qml")) {
                    text.replace(
                        QStringLiteral("    property var visited: [true, false, false, false, false, false]"),
                        QStringLiteral(
                            "    readonly property var pageUrls: [\n"
                            "        Qt.resolvedUrl(\"SettingsPageQuick.qml\"),\n"
                            "        Qt.resolvedUrl(\"SettingsPageGeneral.qml\"),\n"
                            "        Qt.resolvedUrl(\"SettingsPageBar.qml\"),\n"
                            "        Qt.resolvedUrl(\"SettingsPageDesktop.qml\"),\n"
                            "        Qt.resolvedUrl(\"SettingsPageHyprland.qml\"),\n"
                            "        Qt.resolvedUrl(\"SettingsPageAbout.qml\")\n"
                            "    ]\n"
                            "    property var visited: [false, false, false, false, false, false]\n"
                            "    property bool restoredOnce: false"
                        )
                    );
                    text.replace(
                        QStringLiteral(
                            "    Component.onCompleted: {\n"
                            "        if (SettingsService.loaded)\n"
                            "            face.restorePage()\n"
                            "    }\n"
                            "\n"
                            "    Connections {\n"
                            "        target: SettingsService\n"
                            "        function onLoadedChanged() {\n"
                            "            if (SettingsService.loaded)\n"
                            "                face.restorePage()\n"
                            "        }\n"
                            "    }"
                        ),
                        QStringLiteral(
                            "    Component.onCompleted: {\n"
                            "        if (face.active && !face.restoredOnce && SettingsService.loaded) {\n"
                            "            face.restoredOnce = true\n"
                            "            face.restorePage()\n"
                            "        }\n"
                            "    }\n"
                            "\n"
                            "    Connections {\n"
                            "        target: SettingsService\n"
                            "        function onLoadedChanged() {\n"
                            "            if (face.active && !face.restoredOnce && SettingsService.loaded) {\n"
                            "                face.restoredOnce = true\n"
                            "                face.restorePage()\n"
                            "            }\n"
                            "        }\n"
                            "    }"
                        )
                    );
                    text.replace(
                        QStringLiteral(
                            "    onActiveChanged: {\n"
                            "        if (active && hub)\n"
                            "            face.scrollPagesToTop()\n"
                            "        if (!active)\n"
                            "            search.clear()\n"
                            "    }"
                        ),
                        QStringLiteral(
                            "    onActiveChanged: {\n"
                            "        if (active) {\n"
                            "            if (!face.restoredOnce && SettingsService.loaded) {\n"
                            "                face.restoredOnce = true\n"
                            "                face.restorePage()\n"
                            "            }\n"
                            "            if (!face.visited[face.pageIndex])\n"
                            "                face.visit(face.pageIndex)\n"
                            "            if (hub)\n"
                            "                face.scrollPagesToTop()\n"
                            "        } else {\n"
                            "            search.clear()\n"
                            "        }\n"
                            "    }"
                        )
                    );
                    text.replace(
                        QStringLiteral(
                            "                onLoaded: {\n"
                            "                    if (face.searching)\n"
                            "                        searchTimer.restart()\n"
                            "                }\n"
                            "                sourceComponent: slot.index === 0 ? quickPage\n"
                            "                    : slot.index === 1 ? generalPage\n"
                            "                    : slot.index === 2 ? barPage\n"
                            "                    : slot.index === 3 ? desktopPage\n"
                            "                    : slot.index === 4 ? hyprlandPage : aboutPage"
                        ),
                        QStringLiteral(
                            "                source: active ? face.pageUrls[slot.index] : \"\"\n"
                            "                onLoaded: {\n"
                            "                    if (item) {\n"
                            "                        item.host = face\n"
                            "                        item.width = Qt.binding(() => pageArea.width)\n"
                            "                        item.height = Qt.binding(() => pageArea.height)\n"
                            "                    }\n"
                            "                    if (face.searching)\n"
                            "                        searchTimer.restart()\n"
                            "                }"
                        )
                    );
                    const int compIdx = text.indexOf(QStringLiteral("    Component {\n        id: quickPage"));
                    if (compIdx > 0) {
                        text = text.left(compIdx) + QStringLiteral("}\n");
                    }
                }

                // Defer WallpaperFace (69KB + WallpaperWheel + WallpaperGrid + WallpaperViewer) until Wallpapers opens.
                if (fi.fileName() == QStringLiteral("SettingsStage.qml")) {
                    text.remove(QStringLiteral("import qs.modules.sekai.wallpaperSelector\n"));
                    text.replace(
                        QStringLiteral(
                            "                onLoaded: {\n"
                            "                    root.setTarget(root.initialTarget)\n"
                            "                    root.wallpapersLoaded()\n"
                            "                }\n"
                            "\n"
                            "                sourceComponent: Component {\n"
                            "                    WallpaperFace {\n"
                            "                        screenName: root.screenName\n"
                            "                        clockText: root.clockText\n"
                            "                        dateText: root.dateText\n"
                            "                        screenInset: phone.screenInset\n"
                            "                        cornerRadius: root.cornerRadius\n"
                            "                        renderScale: phone.renderScale\n"
                            "\n"
                            "                        onCloseRequested: root.wallpapersCloseRequested()\n"
                            "                    }\n"
                            "                }"
                        ),
                        QStringLiteral(
                            "                source: active ? Qt.resolvedUrl(\"../wallpaperSelector/WallpaperFace.qml\") : \"\"\n"
                            "                onLoaded: {\n"
                            "                    if (item) {\n"
                            "                        item.screenName = Qt.binding(() => root.screenName)\n"
                            "                        item.clockText = Qt.binding(() => root.clockText)\n"
                            "                        item.dateText = Qt.binding(() => root.dateText)\n"
                            "                        item.screenInset = Qt.binding(() => phone.screenInset)\n"
                            "                        item.cornerRadius = Qt.binding(() => root.cornerRadius)\n"
                            "                        item.renderScale = Qt.binding(() => phone.renderScale)\n"
                            "                        item.closeRequested.connect(() => root.wallpapersCloseRequested())\n"
                            "                    }\n"
                            "                    root.setTarget(root.initialTarget)\n"
                            "                    root.wallpapersLoaded()\n"
                            "                }"
                        )
                    );
                }

                if (fi.fileName() == QStringLiteral("PcCavaService.qml")) {
                    // A vertical standing bar never shows the media pill visualizer (MediaPill.showViz is false),
                    // so don't run 60 FPS WASAPI loopback & QML array updates when the bar is vertical.
                    text.replace(
                        QStringLiteral("    readonly property bool pillWanted: VisualizerService.loaded && VisualizerService.pillStyle !== \"none\" && root.anyPlaying"),
                        QStringLiteral("    readonly property bool pillWanted: VisualizerService.loaded && VisualizerService.pillStyle !== \"none\" && !BarPositionService.vertical && SettingsService.flag(\"bar.visible\") && SettingsService.flag(\"bar.media\") && root.anyPlaying")
                    );
                }

                if (fi.fileName() == QStringLiteral("FocusService.qml")) {
                    // Match each window's actual monitor on multi-monitor setups so Desktop Focus Mode
                    // and per-monitor widget suspension work on every monitor.
                    text.replace(
                        QStringLiteral("                monitor: ws && ws.monitor ? ws.monitor.name : \"\","),
                        QStringLiteral("                monitor: t.monitor ? t.monitor.name : (ws && ws.monitor ? ws.monitor.name : \"\"),")
                    );
                }

                if (fi.fileName() == QStringLiteral("Dock.qml")) {
                    // Cap normal-desktop Dock swapchain height to 560 design px (full height only in Edit Desktop)
                    // to cut Dock GPU/RAM swapchain usage nearly in half while keeping full room for all popups/cards.
                    text.replace(
                        QStringLiteral("    readonly property real windowAcross: dockWindow.screen\n"
                                       "        ? Math.max(dockWindow.stripSize + Math.round(dockWindow.innerGap * dockWindow.uiScale),\n"
                                       "                   Math.round(dockWindow.vertical ? dockWindow.screen.width : dockWindow.screen.height)\n"
                                       "                       - dockWindow.lift + Math.round(dockWindow.innerGap * dockWindow.uiScale))\n"
                                       "        : dockWindow.stripSize"),
                        QStringLiteral("    readonly property real fullWindowAcross: dockWindow.screen\n"
                                       "        ? Math.max(dockWindow.stripSize + Math.round(dockWindow.innerGap * dockWindow.uiScale),\n"
                                       "                   Math.round(dockWindow.vertical ? dockWindow.screen.width : dockWindow.screen.height)\n"
                                       "                       - dockWindow.lift + Math.round(dockWindow.innerGap * dockWindow.uiScale))\n"
                                       "        : dockWindow.stripSize\n"
                                       "    readonly property real windowAcross: (dockWindow.editing || EditMode.screenName !== \"\")\n"
                                       "        ? dockWindow.fullWindowAcross\n"
                                       "        : Math.min(dockWindow.fullWindowAcross, Math.round(560 * dockWindow.uiScale))")
                    );
                }

                if (fi.fileName() == QStringLiteral("QuickPanel.qml")) {
                    // Do not eagerly pre-decode wallpaper gallery thumbnails in RAM when App Launcher opens.
                    text.replace(
                        QStringLiteral("    property bool wallpapersWarm: true"),
                        QStringLiteral("    property bool wallpapersWarm: false")
                    );
                    // On cold open (FBOs not allocated yet), let frame 0 map/warm offscreen before enterAnim starts.
                    text.replace(
                        QStringLiteral("        if (up) {\n            exitDelay.stop()\n            exitAnim.stop()"),
                        QStringLiteral("        if (up) {\n            enterPrep.stop()\n            exitDelay.stop()\n            exitAnim.stop()")
                    );
                    text.replace(
                        QStringLiteral("            if (stage.lift > stage.enterFrom)\n                stage.lift = stage.enterFrom\n            enterAnim.restart()\n        } else {\n            enterAnim.stop()"),
                        QStringLiteral("            if (stage.lift > stage.enterFrom)\n                stage.lift = stage.enterFrom\n            if (!phone.warm || phone.justWarmed) {\n                phone.justWarmed = false\n                enterPrep.restart()\n            } else {\n                enterAnim.restart()\n            }\n        } else {\n            enterPrep.stop()\n            enterAnim.stop()")
                    );
                    text.replace(
                        QStringLiteral("    Timer {\n        id: exitDelay"),
                        QStringLiteral("    Timer {\n"
                                       "        id: enterPrep\n"
                                       "        interval: 24\n"
                                       "        onTriggered: {\n"
                                       "            if (root.up)\n"
                                       "                enterAnim.restart()\n"
                                       "        }\n"
                                       "    }\n\n"
                                       "    Timer {\n        id: exitDelay")
                    );
                    text.replace(
                        QStringLiteral("                    moving: enterAnim.running || exitAnim.running"),
                        QStringLiteral("                    moving: enterPrep.running || enterAnim.running || exitAnim.running")
                    );
                    text.replace(
                        QStringLiteral("                        onSystemMonitorRequested: root.source.openSystemMonitor()"),
                        QStringLiteral("                        onSystemMonitorRequested: root.source.openSystemMonitor()\n                        onExitShellRequested: Quickshell.execDetached([\"qs\", \"-c\", \"sekaipop\", \"ipc\", \"call\", \"shell\", \"exit\"])")
                    );
                    // Ensure the bottom-centre Hot Edge strip sits on Overlay layer above Bar/Dock and
                    // uses alpha=1/255 (visually invisible, 100% solid for Win32 DWM hover hit-testing).
                    text.replace(
                        QStringLiteral("            visible: root.hotEdge\n            color: \"transparent\"\n            exclusionMode: ExclusionMode.Ignore"),
                        QStringLiteral("            visible: root.hotEdge\n            WlrLayershell.namespace: \"sekaipop-hotedge\"\n            WlrLayershell.layer: WlrLayer.Overlay\n            color: Qt.rgba(0, 0, 0, 1 / 255.0)\n            exclusionMode: ExclusionMode.Ignore")
                    );
                }

                // Integrate Ukikura multi-source lyrics engine (YouTube CC, Forced Alignment, Romaji, Kanji & Sub Indo)
                if (fi.fileName() == QStringLiteral("LyricsService.qml")) {
                    text.replace(
                        QStringLiteral("    property string source: \"\""),
                        QStringLiteral("    property string source: \"\"\n    property string ukikuraSource: \"\"")
                    );
                    text.replace(
                        QStringLiteral("    readonly property int cacheRev: 2"),
                        QStringLiteral("    readonly property int cacheRev: 3")
                    );
                    text.replace(
                        QStringLiteral("        root.source = \"\"\n        root.sourceKind = \"\""),
                        QStringLiteral("        root.source = \"\"\n        root.ukikuraSource = \"\"\n        root.sourceKind = \"\"")
                    );
                    text.replace(
                        QStringLiteral(
                            "    readonly property var providers: [\n"
                            "        { id: \"boidu\", name: \"Boidu\", hint: \"Better Lyrics: word by word, Apple Music's timing\" },"
                        ),
                        QStringLiteral(
                            "    readonly property var providers: [\n"
                            "        { id: \"ukikura\", name: \"Ukikura\", hint: \"YouTube CC, Forced Alignment, Romaji & Sub Indo\" },\n"
                            "        { id: \"boidu\", name: \"Boidu\", hint: \"Better Lyrics: word by word, Apple Music's timing\" },"
                        )
                    );
                    text.replace(
                        QStringLiteral(
                            "    readonly property var aliases: ({\n"
                            "        betterlyrics: \"boidu\", better: \"boidu\","
                        ),
                        QStringLiteral(
                            "    readonly property var aliases: ({\n"
                            "        kura: \"ukikura\", nusa: \"ukikura\",\n"
                            "        betterlyrics: \"boidu\", better: \"boidu\","
                        )
                    );
                    text.replace(
                        QStringLiteral(
                            "    function ask(id, ctx, cb) {\n"
                            "        if (id === \"boidu\")\n"
                            "            root.pBoidu(ctx, cb)"
                        ),
                        QStringLiteral(
                            "    function ask(id, ctx, cb) {\n"
                            "        if (id === \"ukikura\")\n"
                            "            root.pUkikura(ctx, cb)\n"
                            "        else if (id === \"boidu\")\n"
                            "            root.pBoidu(ctx, cb)"
                        )
                    );
                    text.replace(
                        QStringLiteral(
                            "                if (!ctx.done)\n"
                            "                    ctx.results.push({ id: id, kind: res.kind === \"word\" ? \"word\" : \"synced\", text: res.text, lines: lines, duration: res.duration || 0, trusted: res.trusted === true })"
                        ),
                        QStringLiteral(
                            "                if (!ctx.done)\n"
                            "                    ctx.results.push({ id: id, kind: res.kind === \"word\" ? \"word\" : \"synced\", text: res.text, lines: lines, duration: res.duration || 0, trusted: res.trusted === true, extras: res.extras || [], ukikuraSource: res.ukikuraSource || \"\" })"
                        )
                    );
                    text.replace(
                        QStringLiteral(
                            "    function choose(ctx) {\n"
                            "        return LM.chooseLyrics(ctx.results, root.providerIds, ctx.length, root.preferred, root.wordsOn)\n"
                            "    }\n"
                            "\n"
                            "    function progress(ctx, usable) {\n"
                            "        const hasWord = ctx.results.some(r => r.kind === \"word\")\n"
                            "        const hasSynced = ctx.results.some(r => r.kind !== \"word\")\n"
                            "        if (ctx.pending <= 0 || (hasWord && hasSynced && root.preferred === \"auto\")) {\n"
                            "            root.finalize(ctx)\n"
                            "            return\n"
                            "        }\n"
                            "        if (ctx.results.length === 0)\n"
                            "            return\n"
                            "        if (!ctx.shown) {\n"
                            "            const ch = root.choose(ctx)\n"
                            "            if (ch) {\n"
                            "                ctx.shown = true\n"
                            "                ctx.shownId = ch.id\n"
                            "                ctx.shownText = ch.text\n"
                            "                root.apply(ctx, { synced: ch.text, duration: root.durationOf(ctx, ch.id) }, ch.id, false)\n"
                            "            }\n"
                            "        }\n"
                            "        if (!ctx.waiting) {\n"
                            "            ctx.waiting = true\n"
                            "            root.run([\"sleep\", \"8\"], () => root.finalize(ctx))\n"
                            "        }\n"
                            "    }"
                        ),
                        QStringLiteral(
                            "    function choose(ctx) {\n"
                            "        const uk = ctx.results.find(r => r.id === \"ukikura\" && r.lines && r.lines.length > 0)\n"
                            "        if (uk && (root.preferred === \"auto\" || root.preferred === \"ukikura\"))\n"
                            "            return { id: \"ukikura\", kind: uk.kind, text: uk.text, extras: uk.extras || [], ukikuraSource: uk.ukikuraSource || \"\" }\n"
                            "        const picked = LM.chooseLyrics(ctx.results, root.providerIds, ctx.length, root.preferred, root.wordsOn)\n"
                            "        if (!picked)\n"
                            "            return null\n"
                            "        const raw = ctx.results.find(r => r.id === picked.id)\n"
                            "        return { id: picked.id, kind: picked.kind, text: picked.text, extras: raw ? (raw.extras || []) : [], ukikuraSource: raw ? (raw.ukikuraSource || \"\") : \"\" }\n"
                            "    }\n"
                            "\n"
                            "    function progress(ctx, usable) {\n"
                            "        const ukDone = root.sourceStates[\"ukikura\"] !== \"loading\"\n"
                            "        const hasUk = ctx.results.some(r => r.id === \"ukikura\")\n"
                            "        const hasWord = ctx.results.some(r => r.kind === \"word\")\n"
                            "        const hasSynced = ctx.results.some(r => r.kind !== \"word\")\n"
                            "        if (ctx.pending <= 0 || (root.preferred === \"auto\" && hasUk) || (ukDone && hasWord && hasSynced && root.preferred === \"auto\")) {\n"
                            "            root.finalize(ctx)\n"
                            "            return\n"
                            "        }\n"
                            "        if (ctx.results.length === 0)\n"
                            "            return\n"
                            "        if (!ctx.shown && (ukDone || root.preferred !== \"auto\")) {\n"
                            "            const ch = root.choose(ctx)\n"
                            "            if (ch) {\n"
                            "                ctx.shown = true\n"
                            "                ctx.shownId = ch.id\n"
                            "                ctx.shownText = ch.text\n"
                            "                root.apply(ctx, { synced: ch.text, duration: root.durationOf(ctx, ch.id), extras: ch.extras, ukikuraSource: ch.ukikuraSource }, ch.id, false)\n"
                            "            }\n"
                            "        }\n"
                            "        if (!ctx.waiting) {\n"
                            "            ctx.waiting = true\n"
                            "            root.run([\"sleep\", ukDone ? \"8\" : \"22\"], () => root.finalize(ctx))\n"
                            "        }\n"
                            "    }"
                        )
                    );
                    text.replace(
                        QStringLiteral(
                            "        if (ctx.shown === true && ctx.shownId === ch.id && ctx.shownText === ch.text) {\n"
                            "            root.keep(ctx, ch.text, dur, words, ch.id)\n"
                            "            return\n"
                            "        }\n"
                            "        root.apply(ctx, { synced: ch.text, duration: dur, words: words, provider: ch.id }, ch.id, true)\n"
                            "    }\n"
                            "\n"
                            "    function keep(ctx, synced, duration, words, provider) {\n"
                            "        const json = JSON.stringify({ synced: synced, duration: duration || 0, words: words, provider: provider || \"\", rev: root.cacheRev })\n"
                            "        // one argument to a program is 128 kB at most\n"
                            "        if (json.length < 120000)\n"
                            "            root.runScript([\"put\", ctx.hash, json], () => {})\n"
                            "    }"
                        ),
                        QStringLiteral(
                            "        if (ctx.shown === true && ctx.shownId === ch.id && ctx.shownText === ch.text) {\n"
                            "            root.keep(ctx, ch.text, dur, words, ch.id, ch.extras || [], ch.ukikuraSource || \"\")\n"
                            "            return\n"
                            "        }\n"
                            "        root.apply(ctx, { synced: ch.text, duration: dur, words: words, provider: ch.id, extras: ch.extras || [], ukikuraSource: ch.ukikuraSource || \"\" }, ch.id, true)\n"
                            "    }\n"
                            "\n"
                            "    function keep(ctx, synced, duration, words, provider, extras, ukikuraSource) {\n"
                            "        let payload = { synced: synced, duration: duration || 0, words: words, provider: provider || \"\", rev: root.cacheRev, extras: Array.isArray(extras) ? extras : [], ukikuraSource: ukikuraSource || \"\" }\n"
                            "        let json = JSON.stringify(payload)\n"
                            "        if (json.length >= 120000) {\n"
                            "            payload.extras = []\n"
                            "            json = JSON.stringify(payload)\n"
                            "        }\n"
                            "        if (json.length < 120000)\n"
                            "            root.runScript([\"put\", ctx.hash, json], () => {})\n"
                            "    }"
                        )
                    );
                    text.replace(
                        QStringLiteral(
                            "        const lines = LM.foldEchoes(LM.parseLrc(got.synced || \"\"))\n"
                            "        // (also catches a notice kept in the cache or a .lrc file from before it was ignored:\n"
                            "        // false sends the song to the providers again, and it ends up as an instrumental)\n"
                            "        if (lines.length === 0 || LM.isNoLyricsNotice(lines))\n"
                            "            return false\n"
                            "        root.lines = lines\n"
                            "        root.versionDiff = (got.duration > 0 && ctx.length > 0) ? got.duration - ctx.length : 0\n"
                            "        root.status = \"ready\"\n"
                            "        root.source = (source === \"cache\" && got.provider) ? got.provider : source\n"
                            "        root.sourceKind = lines.some(l => l.marks && l.marks.length > 0) ? \"word\" : \"synced\"\n"
                            "        if (store)\n"
                            "            root.keep(ctx, got.synced, got.duration, got.words || \"\", got.provider || source)\n"
                            "        // Kept from before there were several providers (or before word timing): the lyrics are\n"
                            "        // on, and the others are asked in the background for a better one.\n"
                            "        // (and a song kept as line-synced by an older list of providers is asked once more:\n"
                            "        // one that was added since may have it word by word)\n"
                            "        // Here: every play of a song whose kept lyrics are not word by word asks again (the kept\n"
                            "        // ones stay on screen meanwhile), so a source that has it word by word, Boidu above all,\n"
                            "        // is found once it can answer. A kept word-by-word text is final.\n"
                            "        if (source === \"cache\" && got.words !== \"yes\") {\n"
                            "            ctx.quiet = true\n"
                            "            ctx.cached = got\n"
                            "            root.gather(ctx)\n"
                            "        }\n"
                            "        return true"
                        ),
                        QStringLiteral(
                            "        const lines = LM.foldEchoes(LM.parseLrc(got.synced || \"\"))\n"
                            "        if (lines.length === 0 || LM.isNoLyricsNotice(lines))\n"
                            "            return false\n"
                            "        const exList = Array.isArray(got.extras) ? got.extras : []\n"
                            "        if (exList.length > 0) {\n"
                            "            let ei = 0\n"
                            "            for (let li = 0; li < lines.length; li++) {\n"
                            "                const l = lines[li]\n"
                            "                if (!l || l.text === \"\")\n"
                            "                    continue\n"
                            "                while (ei + 1 < exList.length && exList[ei].t < l.t - 0.3)\n"
                            "                    ei++\n"
                            "                let best = -1\n"
                            "                let bestDiff = 0.35\n"
                            "                for (let k = Math.max(0, ei - 1); k < Math.min(exList.length, ei + 4); k++) {\n"
                            "                    const diff = Math.abs(exList[k].t - l.t)\n"
                            "                    if (diff < bestDiff) {\n"
                            "                        bestDiff = diff\n"
                            "                        best = k\n"
                            "                    }\n"
                            "                }\n"
                            "                if (best >= 0) {\n"
                            "                    if (exList[best].sub) l.subText = exList[best].sub\n"
                            "                    if (exList[best].trans) l.transText = exList[best].trans\n"
                            "                }\n"
                            "            }\n"
                            "        }\n"
                            "        root.lines = lines\n"
                            "        root.versionDiff = (got.duration > 0 && ctx.length > 0) ? got.duration - ctx.length : 0\n"
                            "        root.status = \"ready\"\n"
                            "        root.source = (source === \"cache\" && got.provider) ? got.provider : source\n"
                            "        root.ukikuraSource = got.ukikuraSource || \"\"\n"
                            "        root.sourceKind = lines.some(l => l.marks && l.marks.length > 0) ? \"word\" : \"synced\"\n"
                            "        if (store)\n"
                            "            root.keep(ctx, got.synced, got.duration, got.words || \"\", got.provider || source, exList, root.ukikuraSource)\n"
                            "        const cachedUkikuraFresh = (source === \"cache\" && got.provider === \"ukikura\" && (got.rev || 0) >= root.cacheRev)\n"
                            "        if (source === \"cache\" && !cachedUkikuraFresh && got.words !== \"yes\") {\n"
                            "            ctx.quiet = true\n"
                            "            ctx.cached = got\n"
                            "            root.gather(ctx)\n"
                            "        }\n"
                            "        return true"
                        )
                    );
                    text.replace(
                        QStringLiteral(
                            "            provider: typeof o.provider === \"string\" ? o.provider : \"\",\n"
                            "            rev: typeof o.rev === \"number\" ? o.rev : 0"
                        ),
                        QStringLiteral(
                            "            provider: typeof o.provider === \"string\" ? o.provider : \"\",\n"
                            "            rev: typeof o.rev === \"number\" ? o.rev : 0,\n"
                            "            extras: Array.isArray(o.extras) ? o.extras : [],\n"
                            "            ukikuraSource: typeof o.ukikuraSource === \"string\" ? o.ukikuraSource : \"\""
                        )
                    );
                    text.replace(
                        QStringLiteral("    function run(command, cb) {"),
                        QStringLiteral(
                            "    // ----- Ukikura Multi-Source Synced Lyrics Engine -----\n"
                            "    function ukikuraStamp(sec) {\n"
                            "        const ms = Math.max(0, Math.round(Number(sec || 0) * 1000))\n"
                            "        const m = Math.floor(ms / 60000)\n"
                            "        const s = Math.floor((ms % 60000) / 1000)\n"
                            "        const f = ms % 1000\n"
                            "        return (m < 10 ? \"0\" : \"\") + m + \":\" + (s < 10 ? \"0\" : \"\") + s + \".\" + (f < 100 ? (f < 10 ? \"00\" : \"0\") : \"\") + f\n"
                            "    }\n"
                            "\n"
                            "    function ukikuraFormat(rawLines, ctx, trackDuration) {\n"
                            "        if (!Array.isArray(rawLines) || rawLines.length === 0)\n"
                            "            return null\n"
                            "        const titleArtist = (ctx.title || \"\") + \" \" + (ctx.artist || \"\")\n"
                            "        const songHasChinese = /[\\u4e00-\\u9fa5]/.test(titleArtist)\n"
                            "        const lrcRows = []\n"
                            "        const extras = []\n"
                            "        for (let i = 0; i < rawLines.length; i++) {\n"
                            "            const l = rawLines[i]\n"
                            "            if (!l || typeof l.time !== \"number\" || !isFinite(l.time))\n"
                            "                continue\n"
                            "            const rawVal = String(l.rawText || l.text || \"\").trim()\n"
                            "            const hasJpVocal = /[\\u3040-\\u30ff\\u3400-\\u4dbf\\u4e00-\\u9fff]/.test(rawVal) || Boolean(l.roma && l.jp)\n"
                            "            const hasJpSub = Boolean(l.jpText && /[\\u3040-\\u30ff\\u3400-\\u4dbf\\u4e00-\\u9fff]/.test(l.jpText))\n"
                            "            let romaVal = String(l.romaText || l.roma || \"\").trim()\n"
                            "            if (!romaVal && typeof l.text === \"string\" && l.text.indexOf(\" \\u00b7 \") >= 0)\n"
                            "                romaVal = l.text.split(\" \\u00b7 \")[0].trim()\n"
                            "            let mainText = \"\"\n"
                            "            let subText = \"\"\n"
                            "            let transText = String(l.transText || \"\").trim()\n"
                            "            if (hasJpVocal && romaVal) {\n"
                            "                let jpScript = String(l.jp || l.rawText || \"\").trim()\n"
                            "                if (!jpScript && typeof l.text === \"string\" && l.text.indexOf(\" \\u00b7 \") >= 0)\n"
                            "                    jpScript = l.text.split(\" \\u00b7 \").slice(1).join(\" \\u00b7 \").trim()\n"
                            "                mainText = romaVal\n"
                            "                subText = jpScript || rawVal\n"
                            "            } else if (hasJpSub) {\n"
                            "                mainText = rawVal\n"
                            "                const jp = String(l.jpText || \"\").trim()\n"
                            "                if (romaVal && jp && romaVal.toLowerCase() !== jp.toLowerCase())\n"
                            "                    subText = romaVal.toLowerCase() === mainText.toLowerCase() ? jp : (romaVal + \" \\u00b7 \" + jp)\n"
                            "                else\n"
                            "                    subText = (romaVal && romaVal.toLowerCase() !== mainText.toLowerCase()) ? romaVal : jp\n"
                            "            } else {\n"
                            "                if (typeof l.text === \"string\" && l.text.indexOf(\" \\u00b7 \") >= 0) {\n"
                            "                    const parts = l.text.split(\" \\u00b7 \")\n"
                            "                    mainText = parts[0].trim()\n"
                            "                    subText = parts.slice(1).join(\" \\u00b7 \").trim()\n"
                            "                } else {\n"
                            "                    mainText = rawVal || String(l.text || \"\").trim()\n"
                            "                }\n"
                            "            }\n"
                            "            if (subText && subText.toLowerCase() === mainText.toLowerCase())\n"
                            "                subText = \"\"\n"
                            "            if (transText && transText.toLowerCase() === mainText.toLowerCase())\n"
                            "                transText = \"\"\n"
                            "            if (transText && subText && transText.toLowerCase() === subText.toLowerCase())\n"
                            "                transText = \"\"\n"
                            "            if (transText && /[\\u4e00-\\u9fa5]/.test(transText) && !songHasChinese)\n"
                            "                transText = \"\"\n"
                            "            mainText = mainText.replace(/[\\r\\n]+/g, \" \").trim()\n"
                            "            if (mainText === \"\")\n"
                            "                continue\n"
                            "            const tSec = Math.max(0, l.time)\n"
                            "            lrcRows.push(\"[\" + root.ukikuraStamp(tSec) + \"]\" + mainText)\n"
                            "            if (subText !== \"\" || transText !== \"\")\n"
                            "                extras.push({ t: tSec, sub: subText.replace(/[\\r\\n]+/g, \" \"), trans: transText.replace(/[\\r\\n]+/g, \" \") })\n"
                            "            const nextT = (i + 1 < rawLines.length && typeof rawLines[i + 1].time === \"number\") ? rawLines[i + 1].time : Infinity\n"
                            "            if (typeof l.endTime === \"number\" && l.endTime > tSec + 0.3 && nextT - l.endTime > 0.6)\n"
                            "                lrcRows.push(\"[\" + root.ukikuraStamp(l.endTime) + \"]\")\n"
                            "        }\n"
                            "        if (lrcRows.length === 0)\n"
                            "            return null\n"
                            "        return { lrc: lrcRows.join(\"\\n\"), extras: extras, duration: Number(trackDuration) || 0 }\n"
                            "    }\n"
                            "\n"
                            "    Component {\n"
                            "        id: ukikuraRunner\n"
                            "        QtObject {\n"
                            "            id: ujob\n"
                            "            property var command: []\n"
                            "            property var onFast: null\n"
                            "            property var onFinal: null\n"
                            "            property bool done: false\n"
                            "            property var lastFast: null\n"
                            "            property bool gotAnswer: false\n"
                            "            function finish(res) {\n"
                            "                if (ujob.done) return\n"
                            "                ujob.done = true\n"
                            "                ulimit.stop()\n"
                            "                ujob.onFinal(res)\n"
                            "                ujob.destroy()\n"
                            "            }\n"
                            "            property Process proc: Process {\n"
                            "                command: ujob.command\n"
                            "                stdout: SplitParser {\n"
                            "                    onRead: data => {\n"
                            "                        const s = String(data || \"\").trim()\n"
                            "                        if (s.indexOf(\"FAST:\") === 0) {\n"
                            "                            ujob.gotAnswer = true\n"
                            "                            const obj = root.parseJson(s.slice(5))\n"
                            "                            if (obj) {\n"
                            "                                ujob.lastFast = obj\n"
                            "                                if (ujob.onFast) ujob.onFast(obj)\n"
                            "                            }\n"
                            "                        } else if (s.indexOf(\"FINAL:\") === 0) {\n"
                            "                            ujob.gotAnswer = true\n"
                            "                            const obj = root.parseJson(s.slice(6))\n"
                            "                            ujob.finish({ ok: true, obj: obj })\n"
                            "                        } else if (s.indexOf(\"EMPTY:\") === 0) {\n"
                            "                            ujob.gotAnswer = true\n"
                            "                        }\n"
                            "                    }\n"
                            "                }\n"
                            "                onExited: ujob.finish({ ok: ujob.lastFast !== null, obj: ujob.lastFast, answered: ujob.gotAnswer })\n"
                            "            }\n"
                            "            property Timer ulimit: Timer {\n"
                            "                interval: 23000\n"
                            "                onTriggered: {\n"
                            "                    ujob.proc.running = false\n"
                            "                    ujob.finish({ ok: ujob.lastFast !== null, obj: ujob.lastFast, answered: ujob.gotAnswer })\n"
                            "                }\n"
                            "            }\n"
                            "            Component.onCompleted: {\n"
                            "                ujob.proc.running = true\n"
                            "                ujob.ulimit.start()\n"
                            "            }\n"
                            "        }\n"
                            "    }\n"
                            "\n"
                            "    function pUkikura(ctx, cb) {\n"
                            "        console.log(\"[lyrics] ukikura fetch: \" + ctx.title + \" | \" + ctx.artist + \" (\" + Math.round(ctx.length) + \"s)\")\n"
                            "        ukikuraRunner.createObject(root, {\n"
                            "            command: [\"ukikura-lyrics\", ctx.title, ctx.artist, String(Math.round(ctx.length || 0))],\n"
                            "            onFast: obj => {\n"
                            "                if (ctx.serial !== root.serial || ctx.done)\n"
                            "                    return\n"
                            "                const fmt = root.ukikuraFormat(obj.lines, ctx, obj.trackDuration)\n"
                            "                if (!fmt)\n"
                            "                    return\n"
                            "                ctx.answered = true\n"
                            "                if (root.preferred === \"auto\" || root.preferred === \"ukikura\") {\n"
                            "                    ctx.shown = true\n"
                            "                    ctx.shownId = \"ukikura\"\n"
                            "                    ctx.shownText = fmt.lrc\n"
                            "                    root.apply(ctx, { synced: fmt.lrc, duration: fmt.duration, extras: fmt.extras, provider: \"ukikura\", ukikuraSource: obj.source || \"Ukikura\" }, \"ukikura\", false)\n"
                            "                }\n"
                            "            },\n"
                            "            onFinal: res => {\n"
                            "                if (ctx.serial !== root.serial)\n"
                            "                    return\n"
                            "                const obj = res && res.ok ? res.obj : null\n"
                            "                const fmt = obj ? root.ukikuraFormat(obj.lines, ctx, obj.trackDuration) : null\n"
                            "                if (fmt) {\n"
                            "                    console.log(\"[lyrics] ukikura ready (\" + (obj.source || \"Synced\") + \", \" + obj.lines.length + \" lines, \" + fmt.extras.length + \" sub/trans)\")\n"
                            "                    cb({ state: \"found\", kind: \"synced\", text: fmt.lrc, extras: fmt.extras, duration: fmt.duration, ukikuraSource: obj.source || \"Ukikura\", trusted: true })\n"
                            "                } else {\n"
                            "                    cb({ state: (res && res.answered) ? \"none\" : \"offline\" })\n"
                            "                }\n"
                            "            }\n"
                            "        })\n"
                            "    }\n"
                            "\n"
                            "    function run(command, cb) {"
                        )
                    );
                }

                // Render Ukikura 3-tier hierarchy (Romaji main + original Kanji/Kana sub + Indonesian translation) in LyricsLine
                if (fi.fileName() == QStringLiteral("LyricsLine.qml")) {
                    text.replace(
                        QStringLiteral("    property string text: \"\""),
                        QStringLiteral("    property string text: \"\"\n    property string subText: \"\"\n    property string transText: \"\"")
                    );
                    text.replace(
                        QStringLiteral("        // ===== the bouncing marker ====="),
                        QStringLiteral(
                            "        // ===== Ukikura sub-lyric (Kanji/Kana) & Indonesian translation =====\n"
                            "        Column {\n"
                            "            id: subStack\n"
                            "            readonly property real subVis: Math.max(0, Math.min(1, 1 - Math.abs(line.d) * 1.45))\n"
                            "            visible: !line.blank && !line.still && (line.subText !== \"\" || line.transText !== \"\") && subVis > 0.02\n"
                            "            opacity: subVis * (line.playing ? 0.96 : 0.72)\n"
                            "            anchors.horizontalCenter: parent.horizontalCenter\n"
                            "            y: line.pad + row.implicitHeight + 1\n"
                            "            spacing: 0\n"
                            "\n"
                            "            LyricsWord {\n"
                            "                visible: line.subText !== \"\"\n"
                            "                anchors.horizontalCenter: parent.horizontalCenter\n"
                            "                text: line.subText\n"
                            "                pixelSize: Math.max(13, Math.round(line.px * 0.44))\n"
                            "                fillColor: \"#C9FFF0\"\n"
                            "                withFill: false\n"
                            "                scale: LM.fit(implicitWidth, line.maxW)\n"
                            "                transformOrigin: Item.Top\n"
                            "            }\n"
                            "\n"
                            "            LyricsWord {\n"
                            "                visible: line.transText !== \"\"\n"
                            "                anchors.horizontalCenter: parent.horizontalCenter\n"
                            "                text: line.transText\n"
                            "                pixelSize: Math.max(12, Math.round(line.px * 0.38))\n"
                            "                fillColor: \"#FFE694\"\n"
                            "                withFill: false\n"
                            "                scale: LM.fit(implicitWidth, line.maxW)\n"
                            "                transformOrigin: Item.Top\n"
                            "            }\n"
                            "        }\n"
                            "\n"
                            "        // ===== the bouncing marker ====="
                        )
                    );
                }

                // Wire subText/transText and comfortable 3-tier vertical spacing into LyricsWidget
                if (fi.fileName() == QStringLiteral("LyricsWidget.qml")) {
                    text.replace(
                        QStringLiteral("    readonly property real playerLead: 0.2"),
                        QStringLiteral("    readonly property real playerLead: 0.14")
                    );
                    text.replace(
                        QStringLiteral(
                            "                readonly property real midY: 112\n"
                            "                width: 720\n"
                            "                height: 244"
                        ),
                        QStringLiteral(
                            "                readonly property real midY: 116\n"
                            "                width: 740\n"
                            "                height: 292"
                        )
                    );
                    text.replace(
                        QStringLiteral("                            text: win.lines[slot.index] ? win.lines[slot.index].text : \"\""),
                        QStringLiteral(
                            "                            text: win.lines[slot.index] ? win.lines[slot.index].text : \"\"\n"
                            "                            subText: (win.lines[slot.index] && win.lines[slot.index].subText) ? win.lines[slot.index].subText : \"\"\n"
                            "                            transText: (win.lines[slot.index] && win.lines[slot.index].transText) ? win.lines[slot.index].transText : \"\""
                        )
                    );
                    text.replace(
                        QStringLiteral(
                            "    FrameAnimation {\n"
                            "        running: win.live && win.animating && (win.demo || win.playing)\n"
                            "        onTriggered: win.now = Date.now()\n"
                            "    }"
                        ),
                        QStringLiteral(
                            "    Timer {\n"
                            "        interval: Math.max(5, Math.round(1000 / Math.max(15, Math.min(200, Number(SettingsService.get(\"gfx.uiFps\")) || 60))))\n"
                            "        repeat: true\n"
                            "        running: win.live && win.animating && (win.demo || win.playing)\n"
                            "        onTriggered: win.now = Date.now()\n"
                            "    }"
                        )
                    );
                }

                // Replace unthrottled SwapInterval=0 FrameAnimation spin-loops in Visualizer engines
                // with paced Timers driven by gfx.uiFps (saves ~80% of 1 CPU core when visualizer is active).
                if (fi.fileName() == QStringLiteral("ShaderVisualizerEngine.qml")) {
                    text.replace(
                        QStringLiteral("import QtQuick"),
                        QStringLiteral("import QtQuick\nimport qs.services")
                    );
                    text.replace(
                        QStringLiteral(
                            "    property real lastSignal: 0\n"
                            "    property real bassAverage: 0\n"
                            "    readonly property var peakHold: new Array(maxBands).fill(0)\n"
                            "\n"
                            "    onRawChanged: {\n"
                            "        if (!root.active || root.demo) return;\n"
                            "        if (root.raw.length > 0) root.bandCount = Math.min(root.raw.length, root.maxBands);\n"
                            "        for (let i = 0; i < root.raw.length; i++) {\n"
                            "            if (root.raw[i] > 0) {\n"
                            "                root.lastSignal = Date.now();\n"
                            "                if (root.fade < 0.08) root.fade = 0.08;\n"
                            "                ticker.running = true;\n"
                            "                return;\n"
                            "            }\n"
                            "        }\n"
                            "    }"
                        ),
                        QStringLiteral(
                            "    property real lastSignal: 0\n"
                            "    property real bassAverage: 0\n"
                            "    property bool activeMusic: false\n"
                            "    readonly property var peakHold: new Array(maxBands).fill(0)\n"
                            "\n"
                            "    onRawChanged: {\n"
                            "        if (!root.active || root.demo) return;\n"
                            "        const len = root.raw.length;\n"
                            "        if (len > 0) root.bandCount = Math.min(len, root.maxBands);\n"
                            "        let peak = 0;\n"
                            "        for (let i = 0; i < len; i++) {\n"
                            "            const v = root.raw[i];\n"
                            "            if (v > peak) peak = v;\n"
                            "        }\n"
                            "        root.activeMusic = peak > 145;\n"
                            "        if (peak > 0) {\n"
                            "            root.lastSignal = Date.now();\n"
                            "            if (root.fade < 0.08) root.fade = 0.08;\n"
                            "            ticker.running = true;\n"
                            "        }\n"
                            "    }"
                        )
                    );
                    text.replace(
                        QStringLiteral(
                            "    FrameAnimation {\n"
                            "        id: ticker\n"
                            "\n"
                            "        property real acc: 0\n"
                            "\n"
                            "        running: false\n"
                            "        onTriggered: {\n"
                            "            acc += frameTime\n"
                            "            if (acc < root.frameGap)\n"
                            "                return\n"
                            "            root.step(Math.min(acc, 0.1))\n"
                            "            acc = 0\n"
                            "        }\n"
                            "    }"
                        ),
                        QStringLiteral(
                            "    Timer {\n"
                            "        id: ticker\n"
                            "        property double lastMs: 0\n"
                            "        readonly property int activeInterval: Math.max(5, Math.round(Math.max(root.frameGap * 1000, 1000 / Math.max(15, Math.min(200, Number(SettingsService.get(\"gfx.uiFps\")) || 60)))))\n"
                            "        interval: (root.demo || root.activeMusic) ? activeInterval : Math.max(activeInterval, 62)\n"
                            "        repeat: true\n"
                            "        running: false\n"
                            "        onRunningChanged: lastMs = Date.now()\n"
                            "        onTriggered: {\n"
                            "            const now = Date.now()\n"
                            "            const dt = lastMs > 0 ? Math.max(0.004, Math.min(0.1, (now - lastMs) / 1000)) : (interval / 1000)\n"
                            "            lastMs = now\n"
                            "            root.step(dt)\n"
                            "        }\n"
                            "    }"
                        )
                    );
                }

                if (fi.fileName() == QStringLiteral("VisualizerEngine.qml")) {
                    text.replace(
                        QStringLiteral("import QtQuick"),
                        QStringLiteral("import QtQuick\nimport qs.services")
                    );
                    text.replace(
                        QStringLiteral(
                            "    FrameAnimation {\n"
                            "        id: ticker\n"
                            "\n"
                            "        property real acc: 0\n"
                            "\n"
                            "        running: false\n"
                            "        onTriggered: {\n"
                            "            acc += frameTime\n"
                            "            if (acc < root.frameGap)\n"
                            "                return\n"
                            "            root.step(Math.min(acc, 0.1))\n"
                            "            acc = 0\n"
                            "        }\n"
                            "    }"
                        ),
                        QStringLiteral(
                            "    Timer {\n"
                            "        id: ticker\n"
                            "        property double lastMs: 0\n"
                            "        interval: Math.max(5, Math.round(Math.max(root.frameGap * 1000, 1000 / Math.max(15, Math.min(200, Number(SettingsService.get(\"gfx.uiFps\")) || 60)))))\n"
                            "        repeat: true\n"
                            "        running: false\n"
                            "        onRunningChanged: lastMs = Date.now()\n"
                            "        onTriggered: {\n"
                            "            const now = Date.now()\n"
                            "            const dt = lastMs > 0 ? Math.max(0.004, Math.min(0.1, (now - lastMs) / 1000)) : (interval / 1000)\n"
                            "            lastMs = now\n"
                            "            root.step(dt)\n"
                            "        }\n"
                            "    }"
                        )
                    );
                }

                if (fi.fileName() == QStringLiteral("SekaiVisualizer.qml")) {
                    text.replace(
                        QStringLiteral(
                            "    FrameAnimation {\n"
                            "        property real acc: 0\n"
                            "\n"
                            "        running: root.visible && root.active && (root.awake || root.pointerDown || root.engine.fade > 0.003)\n"
                            "        onTriggered: {\n"
                            "            acc += frameTime\n"
                            "            if (acc < VisualizerService.frameGap)\n"
                            "                return\n"
                            "            root.advance(Math.min(0.05, acc))\n"
                            "            acc = 0\n"
                            "        }\n"
                            "    }"
                        ),
                        QStringLiteral(
                            "    Timer {\n"
                            "        property double lastMs: 0\n"
                            "        interval: Math.max(5, Math.round(Math.max(VisualizerService.frameGap * 1000, 1000 / Math.max(15, Math.min(200, Number(SettingsService.get(\"gfx.uiFps\")) || 60)))))\n"
                            "        repeat: true\n"
                            "        running: root.visible && root.active && (root.awake || root.pointerDown || root.engine.fade > 0.003)\n"
                            "        onRunningChanged: lastMs = Date.now()\n"
                            "        onTriggered: {\n"
                            "            const now = Date.now()\n"
                            "            const dt = lastMs > 0 ? Math.max(0.004, Math.min(0.05, (now - lastMs) / 1000)) : (interval / 1000)\n"
                            "            lastMs = now\n"
                            "            root.advance(dt)\n"
                            "        }\n"
                            "    }"
                        )
                    );
                }

                // Configurable WallpaperLayer photo decode sourceSize (Auto / 720p / 1080p / 2K / 4K / 8K)
                if (fi.fileName() == QStringLiteral("WallpaperLayer.qml")) {
                    text.replace(
                        QStringLiteral("    color: \"transparent\" // shown before the first image finishes decoding"),
                        QStringLiteral("    color: \"#000000\" // opaque native FLIP_DISCARD swapchain for low CPU/DWM overhead")
                    );
                    text.replace(
                        QStringLiteral(
                            "    readonly property real outputPixelRatio: {\n"
                            "        if (!win.screen)\n"
                            "            return 1;\n"
                            "        const logicalW = Math.max(1, width);\n"
                            "        const logicalH = Math.max(1, height);\n"
                            "        const geometry = win.screen.geometry;\n"
                            "        const geometryRatio = geometry\n"
                            "            ? Math.max(geometry.width / logicalW, geometry.height / logicalH)\n"
                            "            : 1;\n"
                            "        return Math.max(1, Math.min(win.screen.devicePixelRatio || 1, geometryRatio || 1));\n"
                            "    }\n"
                            "    readonly property size sourceSize: Qt.size(\n"
                            "        Math.ceil(width * win.parallaxZoom * win.outputPixelRatio),\n"
                            "        Math.ceil(height * win.parallaxZoom * win.outputPixelRatio))"
                        ),
                        QStringLiteral(
                            "    readonly property real effW: width > 1 ? width : (win.screen ? Math.max(1, win.screen.width) : 1920)\n"
                            "    readonly property real effH: height > 1 ? height : (win.screen ? Math.max(1, win.screen.height) : 1080)\n"
                            "    readonly property real outputPixelRatio: {\n"
                            "        if (!win.screen)\n"
                            "            return 1;\n"
                            "        const logicalW = Math.max(1, win.effW);\n"
                            "        const logicalH = Math.max(1, win.effH);\n"
                            "        const geometry = win.screen.geometry;\n"
                            "        const geometryRatio = geometry\n"
                            "            ? Math.max(geometry.width / logicalW, geometry.height / logicalH)\n"
                            "            : 1;\n"
                            "        return Math.max(1, Math.min(win.screen.devicePixelRatio || 1, geometryRatio || 1));\n"
                            "    }\n"
                            "    readonly property string photoResMode: String(SettingsService.get(\"wallpaper.photoResolution\") || \"1440p\")\n"
                            "    readonly property int photoBaseW: photoResMode === \"8k\" ? 7680\n"
                            "                                    : photoResMode === \"4k\" ? 3840\n"
                            "                                    : photoResMode === \"1440p\" ? 2560\n"
                            "                                    : photoResMode === \"1080p\" ? 1920\n"
                            "                                    : photoResMode === \"720p\" ? 1280\n"
                            "                                    : Math.ceil(win.effW * win.outputPixelRatio)\n"
                            "    readonly property int photoBaseH: photoResMode === \"8k\" ? 4320\n"
                            "                                    : photoResMode === \"4k\" ? 2160\n"
                            "                                    : photoResMode === \"1440p\" ? 1440\n"
                            "                                    : photoResMode === \"1080p\" ? 1080\n"
                            "                                    : photoResMode === \"720p\" ? 720\n"
                            "                                    : Math.ceil(win.effH * win.outputPixelRatio)\n"
                            "    readonly property size sourceSize: Qt.size(\n"
                            "        Math.min(7680, Math.ceil(win.photoBaseW * win.parallaxZoom)),\n"
                            "        Math.min(4320, Math.ceil(win.photoBaseH * win.parallaxZoom)))"
                        )
                    );
                }

                // Prevent ClockWidget's 1Hz BeatDiamond from running a 360ms 60 FPS spring animation
                // every single second across 100+ text glyph nodes (cuts sekaipop-clock from 48 to 2 frames/2s).
                if (fi.fileName() == QStringLiteral("ClockWidget.qml")) {
                    text.replace(
                        QStringLiteral(
                            "        scale: bd.pop\n"
                            "        color: bd.lit ? bd.litColor : bd.offColor\n"
                            "        border.width: 2\n"
                            "        border.color: bd.lineColor\n"
                            "\n"
                            "        onLitChanged: {\n"
                            "            if (bd.lit)\n"
                            "                beatPop.restart()\n"
                            "        }\n"
                            "\n"
                            "        NumberAnimation {\n"
                            "            id: beatPop\n"
                            "            target: bd\n"
                            "            property: \"pop\"\n"
                            "            from: 1.7\n"
                            "            to: 1\n"
                            "            duration: 360\n"
                            "            easing.type: Easing.OutBack\n"
                            "            easing.overshoot: 2.2\n"
                            "        }"
                        ),
                        QStringLiteral(
                            "        scale: bd.lit ? 1.15 : 1.0\n"
                            "        color: bd.lit ? bd.litColor : bd.offColor\n"
                            "        border.width: 2\n"
                            "        border.color: bd.lineColor"
                        )
                    );
                }

                // Zero-allocation O(n) prefix-sum smoothing for TideVisualizer (cuts JS CPU ~4x).
                if (fi.fileName() == QStringLiteral("TideVisualizer.qml")) {
                    text.replace(
                        QStringLiteral("    readonly property int cols: root.lite ? 44 : 80"),
                        QStringLiteral("    readonly property int cols: root.lite ? 40 : 52")
                    );
                    text.replace(
                        QStringLiteral(
                            "    readonly property var spec: [\n"
                            "        { base: 0.24, amp: 0.55, rad: 9, pass: 4, rate: 2.5, speed: 0.55, len: 3.4 },\n"
                            "        { base: 0.16, amp: 0.78, rad: 6, pass: 3, rate: 5.0, speed: 0.85, len: 4.6 },\n"
                            "        { base: 0.08, amp: 1.0, rad: 4, pass: 3, rate: 11.0, speed: 1.2, len: 6.2 }\n"
                            "    ]"
                        ),
                        QStringLiteral(
                            "    readonly property var spec: [\n"
                            "        { base: 0.24, amp: 0.55, rad: 6, pass: 2, rate: 2.5, speed: 0.55, len: 3.4 },\n"
                            "        { base: 0.16, amp: 0.78, rad: 4, pass: 2, rate: 5.0, speed: 0.85, len: 4.6 },\n"
                            "        { base: 0.08, amp: 1.0, rad: 3, pass: 2, rate: 11.0, speed: 1.2, len: 6.2 }\n"
                            "    ]"
                        )
                    );
                    text.replace(
                        QStringLiteral(
                            "    property var st: [[], [], []]\n"
                            "    property real lastT: 0\n"
                            "    property real bassSmooth: 0\n"
                            "\n"
                            "    // One pass of a [1 4 6 4 1] / 16 kernel (the ends stay put): a few of these in a row\n"
                            "    // round every bump off the way a gaussian blur would.\n"
                            "    function blur(a) {\n"
                            "        const n = a.length\n"
                            "        const out = new Array(n)\n"
                            "        for (let i = 0; i < n; i++) {\n"
                            "            const m2 = a[Math.max(0, i - 2)], m1 = a[Math.max(0, i - 1)]\n"
                            "            const p1 = a[Math.min(n - 1, i + 1)], p2 = a[Math.min(n - 1, i + 2)]\n"
                            "            out[i] = (m2 + 4 * m1 + 6 * a[i] + 4 * p1 + p2) / 16\n"
                            "        }\n"
                            "        return out\n"
                            "    }\n"
                            "\n"
                            "    visible: engine.fade > 0.003\n"
                            "    opacity: Math.min(1, engine.fade * 2)\n"
                            "    // rises into place as the music starts\n"
                            "    transform: Translate { y: (1 - Math.min(1, root.engine.fade)) * root.height * 0.18 }\n"
                            "\n"
                            "    function resetState() {\n"
                            "        const n = root.cols + 1\n"
                            "        root.st = [new Array(n).fill(0), new Array(n).fill(0), new Array(n).fill(0)]\n"
                            "    }\n"
                            "\n"
                            "    onColsChanged: root.resetState()\n"
                            "    Component.onCompleted: root.resetState()\n"
                            "\n"
                            "    function rebuild() {\n"
                            "        const e = root.engine\n"
                            "        const W = root.width, H = root.height\n"
                            "        if (W <= 0 || H <= 0) return\n"
                            "        const n = root.cols\n"
                            "        if (root.st[0].length !== n + 1) root.resetState()\n"
                            "        const nb = e.bandCount > 1 ? e.bandCount : 50\n"
                            "        let dt = e.time - root.lastT\n"
                            "        root.lastT = e.time\n"
                            "        if (dt <= 0 || dt > 0.2) dt = 0.03\n"
                            "        const lv = e.levels, pk = e.peaks\n"
                            "        const span = W + 2 * root.pad\n"
                            "        // the beat's lift is eased too, so it swells instead of jumping\n"
                            "        root.bassSmooth += (e.bass - root.bassSmooth) * Math.min(1, dt * 8)\n"
                            "        const ampMul = 1 + 0.35 * root.bassSmooth\n"
                            "\n"
                            "        // the spectrum folded round the middle, the highs lifted a little\n"
                            "        const raw = new Array(n + 1)\n"
                            "        let mean = 0\n"
                            "        for (let c = 0; c <= n; c++) {\n"
                            "            const d = Math.abs(c / n - 0.5) * 2\n"
                            "            const f = d * (nb - 1) * 0.8\n"
                            "            const i0 = Math.floor(f)\n"
                            "            const i1 = Math.min(nb - 1, i0 + 1)\n"
                            "            const t = f - i0\n"
                            "            raw[c] = Math.pow(Math.min(1, (lv[i0] * (1 - t) + lv[i1] * t) * (1 + d * 1.1)), 0.8)\n"
                            "            mean += raw[c] / (n + 1)\n"
                            "        }\n"
                            "        // how big the rolling swells are: a little always, more with the music\n"
                            "        const swellAmp = (0.02 + 0.05 * Math.min(1, mean * 2.5)) * H * e.fade\n"
                            "\n"
                            "        const xs = new Array(n + 1)\n"
                            "        for (let c = 0; c <= n; c++) xs[c] = -root.pad + c / n * span\n"
                            "\n"
                            "        let tops = null\n"
                            "        for (let k = 0; k < 3; k++) {\n"
                            "            const L = root.spec[k]\n"
                            "            const s = root.st[k]\n"
                            "            const a = Math.min(1, dt * L.rate)\n"
                            "            const ys = new Array(n + 1)\n"
                            "            for (let c = 0; c <= n; c++) {\n"
                            "                let sum = 0, cnt = 0\n"
                            "                for (let j = -L.rad; j <= L.rad; j++) {\n"
                            "                    const cc = c + j\n"
                            "                    if (cc < 0 || cc > n) continue\n"
                            "                    sum += raw[cc]\n"
                            "                    cnt++\n"
                            "                }\n"
                            "                s[c] += (sum / cnt - s[c]) * a\n"
                            "            }\n"
                            "            let v = s\n"
                            "            for (let q = 0; q < L.pass; q++) v = root.blur(v)\n"
                            "            for (let c = 0; c <= n; c++) {\n"
                            "                const u = xs[c] / W\n"
                            "                // two swells rolling along, the second one slower and shorter\n"
                            "                const swell = swellAmp * (Math.sin(u * L.len - e.time * L.speed + k * 2.1)\n"
                            "                    + 0.5 * Math.sin(u * L.len * 1.9 + e.time * L.speed * 0.6 + k))\n"
                            "                ys[c] = Math.max(6, H - (L.base * H + v[c] * L.amp * H * 0.68 * ampMul + swell))\n"
                            "            }\n"
                            "            shaderLayer.setSheet(k, ys)\n"
                            "            if (k === 2) tops = ys\n"
                            "        }\n"
                            "\n"
                            "        // the pins: a diamond held a little above the front crest by the lane's falling peak\n"
                            "        const front = root.spec[2]\n"
                            "        const marks = []\n"
                            "        for (let m = 0; m < root.laneX.length; m++) {\n"
                            "            const xf = root.laneX[m]\n"
                            "            const d = Math.abs(xf - 0.5) * 2\n"
                            "            const f = d * (nb - 1) * 0.8\n"
                            "            const p = Math.min(1, Math.max(pk[Math.floor(f)], pk[Math.min(nb - 1, Math.ceil(f))]) * (1 + d * 1.1))\n"
                            "            const col = Math.max(0, Math.min(n, Math.round((xf * W + root.pad) / span * n)))\n"
                            "            const crest = tops[col]\n"
                            "            const lift = 16 + Math.min(34, p * H * 0.16)\n"
                            "            marks.push({ x: xf * W, y: Math.max(10, crest - lift), cy: crest, a: Math.min(1, p * 4) * e.fade, s: p })\n"
                            "        }\n"
                            "\n"
                            "        shaderLayer.setPins(marks)\n"
                            "    }"
                        ),
                        QStringLiteral(
                            "    property var st: [new Float64Array(0), new Float64Array(0), new Float64Array(0)]\n"
                            "    property var _raw: new Float64Array(0)\n"
                            "    property var _ps: new Float64Array(0)\n"
                            "    property var _u: new Float64Array(0)\n"
                            "    property var _bA: new Float64Array(0)\n"
                            "    property var _bB: new Float64Array(0)\n"
                            "    property var _ys: new Float64Array(0)\n"
                            "    property var _marks: []\n"
                            "    property real _cachedW: -1\n"
                            "    property real lastT: 0\n"
                            "    property real bassSmooth: 0\n"
                            "\n"
                            "    function blurInto(src, dst, len) {\n"
                            "        const last = len - 1\n"
                            "        for (let i = 0; i < len; i++) {\n"
                            "            const m2 = src[i > 1 ? i - 2 : 0], m1 = src[i > 0 ? i - 1 : 0]\n"
                            "            const p1 = src[i < last ? i + 1 : last], p2 = src[i < last - 1 ? i + 2 : last]\n"
                            "            dst[i] = (m2 + 4 * m1 + 6 * src[i] + 4 * p1 + p2) * 0.0625\n"
                            "        }\n"
                            "    }\n"
                            "\n"
                            "    visible: engine.fade > 0.003\n"
                            "    opacity: Math.min(1, engine.fade * 2)\n"
                            "    transform: Translate { y: (1 - Math.min(1, root.engine.fade)) * root.height * 0.18 }\n"
                            "\n"
                            "    function resetState() {\n"
                            "        const n = root.cols + 1\n"
                            "        root.st = [new Float64Array(n), new Float64Array(n), new Float64Array(n)]\n"
                            "        root._raw = new Float64Array(n)\n"
                            "        root._ps = new Float64Array(n + 1)\n"
                            "        root._u = new Float64Array(n)\n"
                            "        root._bA = new Float64Array(n)\n"
                            "        root._bB = new Float64Array(n)\n"
                            "        root._ys = new Float64Array(n)\n"
                            "        root._cachedW = -1\n"
                            "        const ms = new Array(root.laneX.length)\n"
                            "        for (let m = 0; m < root.laneX.length; m++) ms[m] = { x: 0, y: 0, cy: 0, a: 0, s: 0 }\n"
                            "        root._marks = ms\n"
                            "    }\n"
                            "\n"
                            "    onColsChanged: root.resetState()\n"
                            "    Component.onCompleted: root.resetState()\n"
                            "\n"
                            "    function rebuild() {\n"
                            "        const e = root.engine\n"
                            "        const W = root.width, H = root.height\n"
                            "        if (W <= 0 || H <= 0) return\n"
                            "        const n = root.cols\n"
                            "        const len = n + 1\n"
                            "        if (root.st[0].length !== len) root.resetState()\n"
                            "        const nb = e.bandCount > 1 ? e.bandCount : 50\n"
                            "        let dt = e.time - root.lastT\n"
                            "        root.lastT = e.time\n"
                            "        if (dt <= 0 || dt > 0.2) dt = 0.03\n"
                            "        const lv = e.levels, pk = e.peaks\n"
                            "        const span = W + 2 * root.pad\n"
                            "        root.bassSmooth += (e.bass - root.bassSmooth) * Math.min(1, dt * 8)\n"
                            "        const ampMul = 1 + 0.35 * root.bassSmooth\n"
                            "\n"
                            "        const raw = root._raw\n"
                            "        const ps = root._ps\n"
                            "        ps[0] = 0\n"
                            "        const invN = 1 / n\n"
                            "        const bandScale = (nb - 1) * 0.8\n"
                            "        for (let c = 0; c <= n; c++) {\n"
                            "            const d = Math.abs(c * invN - 0.5) * 2\n"
                            "            const f = d * bandScale\n"
                            "            const i0 = f | 0\n"
                            "            const i1 = i0 + 1 < nb ? i0 + 1 : nb - 1\n"
                            "            const t = f - i0\n"
                            "            let v = (lv[i0] * (1 - t) + lv[i1] * t) * (1 + d * 1.1)\n"
                            "            if (v > 1) v = 1\n"
                            "            const rv = v > 0 ? Math.pow(v, 0.8) : 0\n"
                            "            raw[c] = rv\n"
                            "            ps[c + 1] = ps[c] + rv\n"
                            "        }\n"
                            "        const mean = ps[len] / len\n"
                            "        const swellAmp = (0.02 + 0.05 * Math.min(1, mean * 2.5)) * H * e.fade\n"
                            "\n"
                            "        const uArr = root._u\n"
                            "        if (root._cachedW !== W) {\n"
                            "            root._cachedW = W\n"
                            "            const invW = 1 / W\n"
                            "            for (let c = 0; c <= n; c++) uArr[c] = (-root.pad + c * invN * span) * invW\n"
                            "        }\n"
                            "\n"
                            "        const bA = root._bA\n"
                            "        const bB = root._bB\n"
                            "        const ys = root._ys\n"
                            "        const etime = e.time\n"
                            "        for (let k = 0; k < 3; k++) {\n"
                            "            const L = root.spec[k]\n"
                            "            const s = root.st[k]\n"
                            "            const a = Math.min(1, dt * L.rate)\n"
                            "            const rad = L.rad\n"
                            "            for (let c = 0; c <= n; c++) {\n"
                            "                const lo = c > rad ? c - rad : 0\n"
                            "                const hi = c + rad < n ? c + rad : n\n"
                            "                const avg = (ps[hi + 1] - ps[lo]) / (hi - lo + 1)\n"
                            "                s[c] += (avg - s[c]) * a\n"
                            "            }\n"
                            "            root.blurInto(s, bA, len)\n"
                            "            root.blurInto(bA, bB, len)\n"
                            "            const baseH = L.base * H\n"
                            "            const ampH = L.amp * H * 0.68 * ampMul\n"
                            "            const lLen = L.len, lLen2 = lLen * 1.9\n"
                            "            const ph1 = -etime * L.speed + k * 2.1\n"
                            "            const ph2 = etime * L.speed * 0.6 + k\n"
                            "            for (let c = 0; c <= n; c++) {\n"
                            "                const u = uArr[c]\n"
                            "                const swell = swellAmp * (Math.sin(u * lLen + ph1) + 0.5 * Math.sin(u * lLen2 + ph2))\n"
                            "                const yv = H - (baseH + bB[c] * ampH + swell)\n"
                            "                ys[c] = yv > 6 ? yv : 6\n"
                            "            }\n"
                            "            shaderLayer.setSheet(k, ys)\n"
                            "        }\n"
                            "\n"
                            "        const marks = root._marks\n"
                            "        const pad = root.pad\n"
                            "        const invSpanN = n / span\n"
                            "        for (let m = 0; m < root.laneX.length; m++) {\n"
                            "            const xf = root.laneX[m]\n"
                            "            const d = Math.abs(xf - 0.5) * 2\n"
                            "            const f = d * bandScale\n"
                            "            const i0 = f | 0\n"
                            "            const i1 = i0 + 1 < nb ? i0 + 1 : nb - 1\n"
                            "            let p = (pk[i0] > pk[i1] ? pk[i0] : pk[i1]) * (1 + d * 1.1)\n"
                            "            if (p > 1) p = 1\n"
                            "            let col = Math.round((xf * W + pad) * invSpanN)\n"
                            "            if (col < 0) col = 0; else if (col > n) col = n\n"
                            "            const crest = ys[col]\n"
                            "            const lift = 16 + Math.min(34, p * H * 0.16)\n"
                            "            const mk = marks[m]\n"
                            "            mk.x = xf * W\n"
                            "            mk.y = crest - lift > 10 ? crest - lift : 10\n"
                            "            mk.cy = crest\n"
                            "            mk.a = Math.min(1, p * 4) * e.fade\n"
                            "            mk.s = p\n"
                            "        }\n"
                            "\n"
                            "        shaderLayer.setPins(marks)\n"
                            "    }"
                        )
                    );
                }

                // Direct uniform assignment without dynamic string lookup in TideShaderLayer.
                if (fi.fileName() == QStringLiteral("TideShaderLayer.qml")) {
                    text.replace(
                        QStringLiteral(
                            "    // ys: the crest heights (item pixels) of sheet k, columns + 1 of them.\n"
                            "    function setSheet(k, ys) {\n"
                            "        const n = ys.length;\n"
                            "        for (let j = 0; j < 21; j++) {\n"
                            "            const i = j * 4;\n"
                            "            root[\"h\" + k + \"_\" + j] = Qt.vector4d(\n"
                            "                i < n ? ys[i] : 0, i + 1 < n ? ys[i + 1] : 0,\n"
                            "                i + 2 < n ? ys[i + 2] : 0, i + 3 < n ? ys[i + 3] : 0);\n"
                            "        }\n"
                            "    }\n"
                            "\n"
                            "    // marks: [{ x, y, cy, a, s }] as TideVisualizer.rebuild() makes them (nine of them).\n"
                            "    function setPins(marks) {\n"
                            "        const s = [];\n"
                            "        for (let m = 0; m < 9; m++) {\n"
                            "            const k = marks[m];\n"
                            "            root[\"pin\" + m] = k ? Qt.vector4d(k.x, k.y, k.cy, k.a) : Qt.vector4d(0, 0, 0, 0);\n"
                            "            s.push(k ? k.s : 0);\n"
                            "        }\n"
                            "        root.pinS0 = Qt.vector4d(s[0], s[1], s[2], s[3]);\n"
                            "        root.pinS1 = Qt.vector4d(s[4], s[5], s[6], s[7]);\n"
                            "        root.pinS2 = Qt.vector4d(s[8], 0, 0, 0);\n"
                            "    }"
                        ),
                        QStringLiteral(
                            "    function setSheet(k, ys) {\n"
                            "        const n = ys.length;\n"
                            "        const v = (idx) => idx < n ? ys[idx] : 0;\n"
                            "        if (k === 0) {\n"
                            "            h0_0 = Qt.vector4d(v(0),v(1),v(2),v(3)); h0_1 = Qt.vector4d(v(4),v(5),v(6),v(7));\n"
                            "            h0_2 = Qt.vector4d(v(8),v(9),v(10),v(11)); h0_3 = Qt.vector4d(v(12),v(13),v(14),v(15));\n"
                            "            h0_4 = Qt.vector4d(v(16),v(17),v(18),v(19)); h0_5 = Qt.vector4d(v(20),v(21),v(22),v(23));\n"
                            "            h0_6 = Qt.vector4d(v(24),v(25),v(26),v(27)); h0_7 = Qt.vector4d(v(28),v(29),v(30),v(31));\n"
                            "            h0_8 = Qt.vector4d(v(32),v(33),v(34),v(35)); h0_9 = Qt.vector4d(v(36),v(37),v(38),v(39));\n"
                            "            h0_10 = Qt.vector4d(v(40),v(41),v(42),v(43)); h0_11 = Qt.vector4d(v(44),v(45),v(46),v(47));\n"
                            "            h0_12 = Qt.vector4d(v(48),v(49),v(50),v(51)); h0_13 = Qt.vector4d(v(52),v(53),v(54),v(55));\n"
                            "            if (n > 56) { h0_14 = Qt.vector4d(v(56),v(57),v(58),v(59)); h0_15 = Qt.vector4d(v(60),v(61),v(62),v(63));\n"
                            "            h0_16 = Qt.vector4d(v(64),v(65),v(66),v(67)); h0_17 = Qt.vector4d(v(68),v(69),v(70),v(71));\n"
                            "            h0_18 = Qt.vector4d(v(72),v(73),v(74),v(75)); h0_19 = Qt.vector4d(v(76),v(77),v(78),v(79)); h0_20 = Qt.vector4d(v(80),0,0,0); }\n"
                            "        } else if (k === 1) {\n"
                            "            h1_0 = Qt.vector4d(v(0),v(1),v(2),v(3)); h1_1 = Qt.vector4d(v(4),v(5),v(6),v(7));\n"
                            "            h1_2 = Qt.vector4d(v(8),v(9),v(10),v(11)); h1_3 = Qt.vector4d(v(12),v(13),v(14),v(15));\n"
                            "            h1_4 = Qt.vector4d(v(16),v(17),v(18),v(19)); h1_5 = Qt.vector4d(v(20),v(21),v(22),v(23));\n"
                            "            h1_6 = Qt.vector4d(v(24),v(25),v(26),v(27)); h1_7 = Qt.vector4d(v(28),v(29),v(30),v(31));\n"
                            "            h1_8 = Qt.vector4d(v(32),v(33),v(34),v(35)); h1_9 = Qt.vector4d(v(36),v(37),v(38),v(39));\n"
                            "            h1_10 = Qt.vector4d(v(40),v(41),v(42),v(43)); h1_11 = Qt.vector4d(v(44),v(45),v(46),v(47));\n"
                            "            h1_12 = Qt.vector4d(v(48),v(49),v(50),v(51)); h1_13 = Qt.vector4d(v(52),v(53),v(54),v(55));\n"
                            "            if (n > 56) { h1_14 = Qt.vector4d(v(56),v(57),v(58),v(59)); h1_15 = Qt.vector4d(v(60),v(61),v(62),v(63));\n"
                            "            h1_16 = Qt.vector4d(v(64),v(65),v(66),v(67)); h1_17 = Qt.vector4d(v(68),v(69),v(70),v(71));\n"
                            "            h1_18 = Qt.vector4d(v(72),v(73),v(74),v(75)); h1_19 = Qt.vector4d(v(76),v(77),v(78),v(79)); h1_20 = Qt.vector4d(v(80),0,0,0); }\n"
                            "        } else {\n"
                            "            h2_0 = Qt.vector4d(v(0),v(1),v(2),v(3)); h2_1 = Qt.vector4d(v(4),v(5),v(6),v(7));\n"
                            "            h2_2 = Qt.vector4d(v(8),v(9),v(10),v(11)); h2_3 = Qt.vector4d(v(12),v(13),v(14),v(15));\n"
                            "            h2_4 = Qt.vector4d(v(16),v(17),v(18),v(19)); h2_5 = Qt.vector4d(v(20),v(21),v(22),v(23));\n"
                            "            h2_6 = Qt.vector4d(v(24),v(25),v(26),v(27)); h2_7 = Qt.vector4d(v(28),v(29),v(30),v(31));\n"
                            "            h2_8 = Qt.vector4d(v(32),v(33),v(34),v(35)); h2_9 = Qt.vector4d(v(36),v(37),v(38),v(39));\n"
                            "            h2_10 = Qt.vector4d(v(40),v(41),v(42),v(43)); h2_11 = Qt.vector4d(v(44),v(45),v(46),v(47));\n"
                            "            h2_12 = Qt.vector4d(v(48),v(49),v(50),v(51)); h2_13 = Qt.vector4d(v(52),v(53),v(54),v(55));\n"
                            "            if (n > 56) { h2_14 = Qt.vector4d(v(56),v(57),v(58),v(59)); h2_15 = Qt.vector4d(v(60),v(61),v(62),v(63));\n"
                            "            h2_16 = Qt.vector4d(v(64),v(65),v(66),v(67)); h2_17 = Qt.vector4d(v(68),v(69),v(70),v(71));\n"
                            "            h2_18 = Qt.vector4d(v(72),v(73),v(74),v(75)); h2_19 = Qt.vector4d(v(76),v(77),v(78),v(79)); h2_20 = Qt.vector4d(v(80),0,0,0); }\n"
                            "        }\n"
                            "    }\n"
                            "\n"
                            "    function setPins(marks) {\n"
                            "        const m0 = marks[0], m1 = marks[1], m2 = marks[2], m3 = marks[3], m4 = marks[4];\n"
                            "        const m5 = marks[5], m6 = marks[6], m7 = marks[7], m8 = marks[8];\n"
                            "        pin0 = m0 ? Qt.vector4d(m0.x, m0.y, m0.cy, m0.a) : Qt.vector4d(0,0,0,0);\n"
                            "        pin1 = m1 ? Qt.vector4d(m1.x, m1.y, m1.cy, m1.a) : Qt.vector4d(0,0,0,0);\n"
                            "        pin2 = m2 ? Qt.vector4d(m2.x, m2.y, m2.cy, m2.a) : Qt.vector4d(0,0,0,0);\n"
                            "        pin3 = m3 ? Qt.vector4d(m3.x, m3.y, m3.cy, m3.a) : Qt.vector4d(0,0,0,0);\n"
                            "        pin4 = m4 ? Qt.vector4d(m4.x, m4.y, m4.cy, m4.a) : Qt.vector4d(0,0,0,0);\n"
                            "        pin5 = m5 ? Qt.vector4d(m5.x, m5.y, m5.cy, m5.a) : Qt.vector4d(0,0,0,0);\n"
                            "        pin6 = m6 ? Qt.vector4d(m6.x, m6.y, m6.cy, m6.a) : Qt.vector4d(0,0,0,0);\n"
                            "        pin7 = m7 ? Qt.vector4d(m7.x, m7.y, m7.cy, m7.a) : Qt.vector4d(0,0,0,0);\n"
                            "        pin8 = m8 ? Qt.vector4d(m8.x, m8.y, m8.cy, m8.a) : Qt.vector4d(0,0,0,0);\n"
                            "        pinS0 = Qt.vector4d(m0 ? m0.s : 0, m1 ? m1.s : 0, m2 ? m2.s : 0, m3 ? m3.s : 0);\n"
                            "        pinS1 = Qt.vector4d(m4 ? m4.s : 0, m5 ? m5.s : 0, m6 ? m6.s : 0, m7 ? m7.s : 0);\n"
                            "        pinS2 = Qt.vector4d(m8 ? m8.s : 0, 0, 0, 0);\n"
                            "    }"
                        )
                    );
                }

                // Show the bottom Dock on every monitor by default on Windows multi-monitor setups
                // (user can still toggle off anytime in Settings -> Desktop -> Dock -> Dock on every monitor).
                if (fi.fileName() == QStringLiteral("SettingsService.qml")) {
                    text.replace(
                        QStringLiteral("\"dock.perMonitor\": false,"),
                        QStringLiteral("\"dock.perMonitor\": true,")
                    );
                }

                // Match running Windows apps by each toplevel's actual monitor on multi-monitor docks.
                if (fi.fileName() == QStringLiteral("Dock.qml")) {
                    text.replace(
                        QStringLiteral("const onHere = here !== \"\" && t.workspace?.monitor?.name === here"),
                        QStringLiteral("const onHere = here !== \"\" && (t.monitor?.name || t.workspace?.monitor?.name) === here")
                    );
                    // Eliminate width and height binding loop warnings on Dock tooltip hint text
                    text.replace(
                        QStringLiteral("                width: Math.max(flagTitle.width, flagHint.width) + 46"),
                        QStringLiteral("                width: Math.max(flagTitle.width, flagHint.visible ? flagHint.implicitWidth : 0) + 46")
                    );
                    text.replace(
                        QStringLiteral("                            visible: flag.hint !== \"\"\n"
                                       "                            width: visible ? implicitWidth : 0\n"
                                       "                            height: visible ? implicitHeight : 0"),
                        QStringLiteral("                            visible: flag.hint !== \"\"")
                    );
                }

                // Scale EqualizerPopup Canvas supersampling with UI graphics quality (720p..8K).
                if (fi.fileName() == QStringLiteral("EqualizerPopup.qml")) {
                    text.replace(
                        QStringLiteral("    readonly property real ssaa: 2"),
                        QStringLiteral("    readonly property real ssaa: SettingsService.get(\"gfx.uiQuality\") === \"8k\" ? 2.5 : (SettingsService.get(\"gfx.uiQuality\") === \"4k\" ? 2.0 : (SettingsService.get(\"gfx.uiQuality\") === \"1440p\" ? 1.5 : (SettingsService.get(\"gfx.uiQuality\") === \"720p\" ? 1.0 : 1.25)))")
                    );
                }

                // Add live Ukikura Lyrics strip + Desktop Lyrics toggle + Source picker right inside MediaPopup
                if (fi.fileName() == QStringLiteral("MediaPopup.qml")) {
                    text.replace(
                        QStringLiteral("import qs.modules.sekai.visualizer"),
                        QStringLiteral("import qs.modules.sekai.visualizer\nimport \"../../common/functions/LyricsMath.js\" as LM")
                    );
                    text.replace(
                        QStringLiteral(
                            "        const matches = root.ownerInfo.matches\n"
                            "        if (matches.length === 0) {\n"
                            "            if (root.p.canRaise)\n"
                            "                root.p.raise()\n"
                            "            return\n"
                            "        }"
                        ),
                        QStringLiteral(
                            "        const matches = root.ownerInfo.matches\n"
                            "        if (matches.length === 0) {\n"
                            "            root.p.raise()\n"
                            "            return\n"
                            "        }"
                        )
                    );
                    text.replace(
                        QStringLiteral(
                            "    // A fresh reading whenever the card opens, the song changes or another player is\n"
                            "    // picked (a paused player would otherwise show an old time).\n"
                            "    function refreshPosition() {\n"
                            "        if (root.hasPlayer && root.p)\n"
                            "            root.p.positionChanged()\n"
                            "    }\n"
                            "    onTrackKeyChanged: {"
                        ),
                        QStringLiteral(
                            "    property real liveLyricPos: 0\n"
                            "    readonly property int liveLyricIdx: LyricsService.lines.length > 0 ? LM.lineIndexAt(LyricsService.lines, root.liveLyricPos + 0.14 + LyricsService.syncTotal / 1000) : -1\n"
                            "    readonly property var liveLyricObj: (root.liveLyricIdx >= 0 && root.liveLyricIdx < LyricsService.lines.length) ? LyricsService.lines[root.liveLyricIdx] : null\n"
                            "    readonly property string liveLyricMain: (root.liveLyricObj && root.liveLyricObj.text) ? root.liveLyricObj.text : \"\"\n"
                            "    readonly property string liveLyricSub: {\n"
                            "        if (!root.liveLyricObj) return \"\"\n"
                            "        if (root.liveLyricObj.transText && root.liveLyricObj.transText !== \"\") return root.liveLyricObj.transText\n"
                            "        if (root.liveLyricObj.subText && root.liveLyricObj.subText !== \"\") return root.liveLyricObj.subText\n"
                            "        return \"\"\n"
                            "    }\n"
                            "\n"
                            "    Timer {\n"
                            "        interval: 120\n"
                            "        repeat: true\n"
                            "        running: root.open && root.hasPlayer && root.playing && !root.scrubbing\n"
                            "        onTriggered: {\n"
                            "            if (root.p) {\n"
                            "                root.p.positionChanged()\n"
                            "                root.liveLyricPos = root.p.position\n"
                            "            }\n"
                            "        }\n"
                            "    }\n"
                            "\n"
                            "    function requestPopupLyrics() {\n"
                            "        if (!root.open || !root.hasTrack)\n"
                            "            return\n"
                            "        LyricsService.request(root.artistText, root.titleText, root.p ? String(root.p.trackAlbum ?? \"\") : \"\", root.length, \"\")\n"
                            "    }\n"
                            "\n"
                            "    function refreshPosition() {\n"
                            "        if (root.hasPlayer && root.p) {\n"
                            "            root.p.positionChanged()\n"
                            "            root.liveLyricPos = root.p.position\n"
                            "        }\n"
                            "    }\n"
                            "    onTrackKeyChanged: {"
                        )
                    );
                    text.replace(
                        QStringLiteral(
                            "    onTrackKeyChanged: {\n"
                            "        root.settleFrac = -1\n"
                            "        root.refreshPosition()\n"
                            "    }\n"
                            "    onPChanged: {\n"
                            "        root.settleFrac = -1\n"
                            "        root.refreshPosition()\n"
                            "    }\n"
                            "\n"
                            "    onOpenChanged: {\n"
                            "        if (open) {\n"
                            "            root.picked = null\n"
                            "            root.settleFrac = -1\n"
                            "            root.scrubbing = false\n"
                            "            root.refreshPosition()\n"
                            "        }\n"
                            "    }"
                        ),
                        QStringLiteral(
                            "    onTrackKeyChanged: {\n"
                            "        root.settleFrac = -1\n"
                            "        root.refreshPosition()\n"
                            "        root.requestPopupLyrics()\n"
                            "    }\n"
                            "    onPChanged: {\n"
                            "        root.settleFrac = -1\n"
                            "        root.refreshPosition()\n"
                            "        root.requestPopupLyrics()\n"
                            "    }\n"
                            "\n"
                            "    onOpenChanged: {\n"
                            "        if (open) {\n"
                            "            root.picked = null\n"
                            "            root.settleFrac = -1\n"
                            "            root.scrubbing = false\n"
                            "            root.refreshPosition()\n"
                            "            root.requestPopupLyrics()\n"
                            "        }\n"
                            "    }"
                        )
                    );
                    text.replace(
                        QStringLiteral(
                            "            MediaKey {\n"
                            "                y: Math.round((keys.height - height) / 2)\n"
                            "                compact: true\n"
                            "                icon: \"open_in_new\"\n"
                            "                available: root.hasPlayer\n"
                            "                onClicked: root.openPlayerWindow()\n"
                            "            }\n"
                            "        }\n"
                            "    }\n"
                            "}"
                        ),
                        QStringLiteral(
                            "            MediaKey {\n"
                            "                y: Math.round((keys.height - height) / 2)\n"
                            "                compact: true\n"
                            "                icon: \"open_in_new\"\n"
                            "                available: root.hasPlayer\n"
                            "                onClicked: root.openPlayerWindow()\n"
                            "            }\n"
                            "        }\n"
                            "\n"
                            "        // ===== live lyrics bar (Ukikura synced + toggle desktop lyrics) =====\n"
                            "        Pill {\n"
                            "            id: lyricsStrip\n"
                            "            width: stack.width\n"
                            "            height: root.liveLyricSub !== \"\" ? 54 : 42\n"
                            "            cornerRadius: 14\n"
                            "            ringWidth: 4\n"
                            "            fillColor: lyricsStripArea.containsMouse ? mt.surfaceHover : mt.surface\n"
                            "            pressed: lyricsStripArea.pressed\n"
                            "\n"
                            "            Row {\n"
                            "                anchors.fill: parent\n"
                            "                anchors.leftMargin: 10\n"
                            "                anchors.rightMargin: 8\n"
                            "                spacing: 8\n"
                            "\n"
                            "                MaterialIcon {\n"
                            "                    anchors.verticalCenter: parent.verticalCenter\n"
                            "                    name: \"lyrics\"\n"
                            "                    size: 18\n"
                            "                    color: LyricsService.shown ? mt.accentCyan : mt.inkSub\n"
                            "                }\n"
                            "\n"
                            "                Column {\n"
                            "                    anchors.verticalCenter: parent.verticalCenter\n"
                            "                    width: Math.max(80, parent.width - 114)\n"
                            "                    spacing: 1\n"
                            "\n"
                            "                    Text {\n"
                            "                        width: parent.width\n"
                            "                        elide: Text.ElideRight\n"
                            "                        text: {\n"
                            "                            if (root.liveLyricMain !== \"\") return root.liveLyricMain\n"
                            "                            if (LyricsService.status === \"loading\") return \"Looking for lyrics...\"\n"
                            "                            if (LyricsService.status === \"ready\" && LyricsService.lines.length > 0) return \"Instrumental / Intro\"\n"
                            "                            if (LyricsService.status === \"instrumental\") return \"Instrumental\"\n"
                            "                            return LyricsService.shown ? \"Desktop Lyrics Active\" : \"Tap to enable Desktop Lyrics\"\n"
                            "                        }\n"
                            "                        color: root.liveLyricMain !== \"\" ? mt.ink : mt.inkSub\n"
                            "                        font.family: mt.fontFamily\n"
                            "                        font.pixelSize: 12\n"
                            "                        font.weight: Font.ExtraBold\n"
                            "                        font.styleName: mt.fontStyleBold\n"
                            "                    }\n"
                            "\n"
                            "                    Text {\n"
                            "                        visible: root.liveLyricSub !== \"\"\n"
                            "                        width: parent.width\n"
                            "                        elide: Text.ElideRight\n"
                            "                        text: root.liveLyricSub\n"
                            "                        color: mt.inkSub\n"
                            "                        font.family: mt.fontFamily\n"
                            "                        font.pixelSize: 10\n"
                            "                        font.weight: Font.DemiBold\n"
                            "                    }\n"
                            "                }\n"
                            "\n"
                            "                Rectangle {\n"
                            "                    anchors.verticalCenter: parent.verticalCenter\n"
                            "                    width: 44\n"
                            "                    height: 22\n"
                            "                    radius: 8\n"
                            "                    color: LyricsService.shown ? mt.accentCyan : mt.surfaceDivider\n"
                            "                    border.width: 1.5\n"
                            "                    border.color: mt.line\n"
                            "\n"
                            "                    Text {\n"
                            "                        anchors.centerIn: parent\n"
                            "                        text: LyricsService.shown ? \"ON\" : \"OFF\"\n"
                            "                        color: LyricsService.shown ? mt.colBorder : mt.inkSub\n"
                            "                        font.family: mt.fontFamily\n"
                            "                        font.pixelSize: 10\n"
                            "                        font.weight: Font.ExtraBold\n"
                            "                        font.letterSpacing: 0.8\n"
                            "                    }\n"
                            "                }\n"
                            "\n"
                            "                Rectangle {\n"
                            "                    id: srcBtn\n"
                            "                    anchors.verticalCenter: parent.verticalCenter\n"
                            "                    width: 24\n"
                            "                    height: 24\n"
                            "                    radius: 8\n"
                            "                    color: srcBtnArea.containsMouse ? mt.surfaceDivider : \"transparent\"\n"
                            "                    z: 3\n"
                            "\n"
                            "                    MaterialIcon {\n"
                            "                        anchors.centerIn: parent\n"
                            "                        name: \"tune\"\n"
                            "                        size: 16\n"
                            "                        color: mt.inkSub\n"
                            "                    }\n"
                            "\n"
                            "                    MouseArea {\n"
                            "                        id: srcBtnArea\n"
                            "                        anchors.fill: parent\n"
                            "                        hoverEnabled: true\n"
                            "                        cursorShape: Qt.PointingHandCursor\n"
                            "                        onClicked: mouse => {\n"
                            "                            mouse.accepted = true\n"
                            "                            LyricsService.togglePicker(root.screen ? root.screen.name : \"\", -1, -1)\n"
                            "                        }\n"
                            "                    }\n"
                            "                }\n"
                            "            }\n"
                            "\n"
                            "            MouseArea {\n"
                            "                id: lyricsStripArea\n"
                            "                anchors.fill: parent\n"
                            "                anchors.rightMargin: 34\n"
                            "                hoverEnabled: true\n"
                            "                cursorShape: Qt.PointingHandCursor\n"
                            "                onClicked: {\n"
                            "                    LyricsService.setShown(!LyricsService.shown)\n"
                            "                    if (LyricsService.shown)\n"
                            "                        root.requestPopupLyrics()\n"
                            "                }\n"
                            "            }\n"
                            "        }\n"
                            "    }\n"
                            "}"
                        )
                    );
                }
            }

            if (ext == QStringLiteral("qml") && fi.fileName() == QStringLiteral("QuickPanel.qml")) {
                text.replace(
                    QStringLiteral(
                        "        function wallhaven(): void {\n"
                        "            WallhavenService.askedSource = \"wallhaven\"\n"
                        "            root.openWallpapers(root.focusedScreen())\n"
                        "        }"
                    ),
                    QStringLiteral(
                        "        function wallhaven(): void {\n"
                        "            WallhavenService.askedSource = \"wallhaven\"\n"
                        "            root.openWallpapers(root.focusedScreen())\n"
                        "        }\n"
                        "        function live(): void {\n"
                        "            WallhavenService.askedSource = \"live\"\n"
                        "            root.openWallpapers(root.focusedScreen())\n"
                        "        }"
                    )
                );
            }

            if (ext == QStringLiteral("qml") && fi.fileName() == QStringLiteral("WallpaperSourceSwitch.qml")) {
                // Add 3rd tab "Live video" beside "My folder" and "Wallhaven"
                text.replace(
                    QStringLiteral(
                        "    readonly property var choices: [\n"
                        "        { id: \"folder\", label: \"My folder\", icon: \"folder\" },\n"
                        "        { id: \"wallhaven\", label: \"Wallhaven\", icon: \"travel_explore\" }\n"
                        "    ]\n"
                        "    readonly property int currentIndex: root.current === \"wallhaven\" ? 1 : 0\n"
                        "\n"
                        "    // icon 20 + gap 7 + the label, 16 each side\n"
                        "    readonly property real slot: Math.ceil(Math.max(m0.width, m1.width) + 20 + 7 + 32)\n"
                        "\n"
                        "    width: 2 * root.slot + 6"
                    ),
                    QStringLiteral(
                        "    readonly property var choices: [\n"
                        "        { id: \"folder\", label: \"My folder\", icon: \"folder\" },\n"
                        "        { id: \"wallhaven\", label: \"Wallhaven\", icon: \"travel_explore\" },\n"
                        "        { id: \"live\", label: \"Live video\", icon: \"movie\" }\n"
                        "    ]\n"
                        "    readonly property int currentIndex: root.current === \"live\" ? 2 : (root.current === \"wallhaven\" ? 1 : 0)\n"
                        "\n"
                        "    // icon 20 + gap 7 + the label, 12 each side\n"
                        "    readonly property real slot: Math.ceil(Math.max(m0.width, m1.width, m2.width) + 20 + 7 + 24)\n"
                        "\n"
                        "    width: 3 * root.slot + 6"
                    )
                );
                text.replace(
                    QStringLiteral(
                        "    TextMetrics {\n"
                        "        id: m1\n"
                        "        text: root.choices[1].label\n"
                        "        font: m0.font\n"
                        "    }"
                    ),
                    QStringLiteral(
                        "    TextMetrics {\n"
                        "        id: m1\n"
                        "        text: root.choices[1].label\n"
                        "        font: m0.font\n"
                        "    }\n"
                        "    TextMetrics {\n"
                        "        id: m2\n"
                        "        text: root.choices[2].label\n"
                        "        font: m0.font\n"
                        "    }"
                    )
                );
            }

            if (ext == QStringLiteral("js") && fi.fileName() == QStringLiteral("WallhavenMath.js")) {
                // Support MotionBGs live video items alongside Wallhaven images
                text.replace(
                    QStringLiteral(
                        "        items.push({\n"
                        "            id: d.id,\n"
                        "            page: typeof d.url === \"string\" ? d.url : \"https://wallhaven.cc/w/\" + d.id,\n"
                        "            full: d.path,\n"
                        "            thumb: d.thumbs.large || d.thumbs.original || d.thumbs.small || \"\",\n"
                        "            small: d.thumbs.small || d.thumbs.large || \"\",\n"
                        "            width: Number(d.dimension_x) || 0,\n"
                        "            height: Number(d.dimension_y) || 0,\n"
                        "            resolution: typeof d.resolution === \"string\" ? d.resolution : \"\",\n"
                        "            size: Number(d.file_size) || 0,\n"
                        "            type: extOf(d.path),\n"
                        "            category: typeof d.category === \"string\" ? d.category : \"\",\n"
                        "            views: Number(d.views) || 0,\n"
                        "            favourites: Number(d.favorites) || 0,\n"
                        "            colours: Array.isArray(d.colors) ? d.colors.filter(function (c) { return /^#[0-9a-fA-F]{6}$/.test(c) }) : []\n"
                        "        })"
                    ),
                    QStringLiteral(
                        "        items.push({\n"
                        "            id: d.id,\n"
                        "            title: typeof d.title === \"string\" ? d.title : \"\",\n"
                        "            page: typeof d.url === \"string\" ? d.url : \"https://wallhaven.cc/w/\" + d.id,\n"
                        "            full: d.path,\n"
                        "            preview: typeof d.preview === \"string\" ? d.preview : d.path,\n"
                        "            thumb: d.thumbs.large || d.thumbs.original || d.thumbs.small || \"\",\n"
                        "            small: d.thumbs.small || d.thumbs.large || \"\",\n"
                        "            width: Number(d.dimension_x) || 0,\n"
                        "            height: Number(d.dimension_y) || 0,\n"
                        "            resolution: typeof d.resolution === \"string\" ? d.resolution : \"\",\n"
                        "            size: Number(d.file_size) || 0,\n"
                        "            type: d.file_type === \"mp4\" ? \"mp4\" : extOf(d.path),\n"
                        "            category: typeof d.category === \"string\" ? d.category : \"\",\n"
                        "            views: Number(d.views) || 0,\n"
                        "            favourites: Number(d.favorites) || 0,\n"
                        "            isVideo: !!d.is_video,\n"
                        "            provider: typeof d.provider === \"string\" ? d.provider : \"wallhaven\",\n"
                        "            colours: Array.isArray(d.colors) ? d.colors.filter(function (c) { return /^#[0-9a-fA-F]{6}$/.test(c) }) : []\n"
                        "        })"
                    )
                );
                text.replace(
                    QStringLiteral(
                        "function fileName(item) {\n"
                        "    return \"wallhaven-\" + item.id + \".\" + (item.type || extOf(item.full))\n"
                        "}"
                    ),
                    QStringLiteral(
                        "function fileName(item) {\n"
                        "    if (item && (item.isVideo || item.provider === \"motionbgs\"))\n"
                        "        return \"motionbgs-\" + String(item.id || \"\").replace(/^mb-/, \"\") + \".mp4\"\n"
                        "    return \"wallhaven-\" + item.id + \".\" + (item.type || extOf(item.full))\n"
                        "}"
                    )
                );
                text.replace(
                    QStringLiteral(
                        "function idOf(name) {\n"
                        "    var m = String(name || \"\").match(/(?:^|\\/)wallhaven-([a-z0-9]+)\\.[a-z0-9]+$/)\n"
                        "    return m ? m[1] : \"\"\n"
                        "}"
                    ),
                    QStringLiteral(
                        "function idOf(name) {\n"
                        "    var s = String(name || \"\")\n"
                        "    var mb = s.match(/(?:^|\\/)motionbgs-([0-9]+)\\.[a-z0-9]+$/i)\n"
                        "    if (mb) return \"mb-\" + mb[1]\n"
                        "    var m = s.match(/(?:^|\\/)wallhaven-([a-z0-9]+)\\.[a-z0-9]+$/i)\n"
                        "    return m ? m[1] : \"\"\n"
                        "}"
                    )
                );
            }

            if (ext == QStringLiteral("qml") && fi.fileName() == QStringLiteral("WallhavenService.qml")) {
                // Dual online provider: "wallhaven" (static images) + "live" (MotionBGs 1080p MP4 live wallpapers)
                // with per-provider RAM cache, immediate cancellation on tab/query switch, and silent live preload.
                text.replace(
                    QStringLiteral("    property string seed: \"\""),
                    QStringLiteral(
                        "    property string seed: \"\"\n"
                        "    property string provider: \"wallhaven\"   // \"wallhaven\" | \"live\"\n"
                        "    property string liveCategory: \"anime\"\n"
                        "    property var providerCache: ({})\n"
                        "    property bool livePreloaded: false\n"
                        "    readonly property var liveCategoryLabels: ({\n"
                        "        \"anime\": \"Anime\", \"girl\": \"Anime Girl\", \"anime-nature\": \"Anime Nature\",\n"
                        "        \"games\": \"Games\", \"nature\": \"Nature\", \"superhero\": \"Superhero\", \"4k\": \"All 4K\"\n"
                        "    })\n"
                        "    readonly property string liveCategoryLabel: root.liveCategoryLabels[root.liveCategory] || \"Anime\"\n"
                        "\n"
                        "    function preloadLive() {\n"
                        "        if (root.livePreloaded)\n"
                        "            return\n"
                        "        root.livePreloaded = true\n"
                        "        const url = \"https://motionbgs.com/api/sekaipop?q=&cat=\" + encodeURIComponent(root.liveCategory) + \"&page=1\"\n"
                        "        Quickshell.execDetached([\"sh\", root.script, \"preload\", url])\n"
                        "    }\n"
                        "\n"
                        "    function setProvider(p) {\n"
                        "        const next = (p === \"live\") ? \"live\" : \"wallhaven\"\n"
                        "        if (root.provider === next)\n"
                        "            return\n"
                        "        if (root.items.length > 0 && root.page > 0) {\n"
                        "            const pc = Object.assign({}, root.providerCache)\n"
                        "            pc[root.provider] = {\n"
                        "                items: root.items, page: root.page, lastPage: root.lastPage,\n"
                        "                total: root.total, query: root.query, seed: root.seed\n"
                        "            }\n"
                        "            root.providerCache = pc\n"
                        "        }\n"
                        "        limitTimer.stop()\n"
                        "        if (searcher.running)\n"
                        "            searcher.running = false\n"
                        "        root.provider = next\n"
                        "        root.error = \"\"\n"
                        "        root.loading = false\n"
                        "        const saved = root.providerCache[next]\n"
                        "        if (saved && saved.items && saved.items.length > 0) {\n"
                        "            root.items = saved.items\n"
                        "            root.page = saved.page\n"
                        "            root.lastPage = saved.lastPage\n"
                        "            root.total = saved.total\n"
                        "            root.query = saved.query || \"\"\n"
                        "            root.seed = saved.seed || \"\"\n"
                        "        } else {\n"
                        "            root.items = []\n"
                        "            root.page = 0\n"
                        "            root.lastPage = 0\n"
                        "            root.total = 0\n"
                        "            root.query = \"\"\n"
                        "            root.seed = \"\"\n"
                        "        }\n"
                        "    }\n"
                        "\n"
                        "    function setLiveCategory(cat) {\n"
                        "        if (!cat || cat === root.liveCategory)\n"
                        "            return\n"
                        "        root.liveCategory = cat\n"
                        "        root.again()\n"
                        "    }"
                    )
                );
                text.replace(
                    QStringLiteral(
                        "        root.page = 0\n"
                        "        root.lastPage = 0\n"
                        "        root.total = 0\n"
                        "        root.request(1)"
                    ),
                    QStringLiteral(
                        "        limitTimer.stop()\n"
                        "        if (searcher.running)\n"
                        "            searcher.running = false\n"
                        "        root.page = 0\n"
                        "        root.lastPage = 0\n"
                        "        root.total = 0\n"
                        "        root.request(1)"
                    )
                );
                text.replace(
                    QStringLiteral("    readonly property string description: WH.describe(Object.assign({ query: root.query }, root.opts))"),
                    QStringLiteral(
                        "    readonly property string description: root.provider === \"live\"\n"
                        "        ? (root.query.trim() !== \"\" ? (\"\\u201c\" + root.query.trim() + \"\\u201d \\u00b7 Live videos\")\n"
                        "           : (root.liveCategoryLabel + \" \\u00b7 Live videos (MotionBGs)\"))\n"
                        "        : WH.describe(Object.assign({ query: root.query }, root.opts))"
                    )
                );
                text.replace(
                    QStringLiteral(
                        "        searcher.forPage = root.wantPage\n"
                        "        searcher.forQuery = root.query\n"
                        "        searcher.exec([\"sh\", root.script, \"search\", WH.searchUrl(o)])"
                    ),
                    QStringLiteral(
                        "        searcher.forPage = root.wantPage\n"
                        "        searcher.forQuery = root.query\n"
                        "        searcher.forProvider = root.provider\n"
                        "        const url = root.provider === \"live\"\n"
                        "            ? (\"https://motionbgs.com/api/sekaipop?q=\" + encodeURIComponent(root.query)\n"
                        "               + \"&cat=\" + encodeURIComponent(root.liveCategory) + \"&page=\" + root.wantPage)\n"
                        "            : WH.searchUrl(o)\n"
                        "        searcher.exec([\"sh\", root.script, \"search\", url])"
                    )
                );
                text.replace(
                    QStringLiteral(
                        "        property int forPage: 0\n"
                        "        property string forQuery: \"\""
                    ),
                    QStringLiteral(
                        "        property int forPage: 0\n"
                        "        property string forQuery: \"\"\n"
                        "        property string forProvider: \"wallhaven\""
                    )
                );
                text.replace(
                    QStringLiteral("            if (searcher.forQuery !== root.query || searcher.forPage !== root.wantPage)"),
                    QStringLiteral("            if (searcher.forProvider !== root.provider || searcher.forQuery !== root.query || searcher.forPage !== root.wantPage)")
                );
            }

            if (ext == QStringLiteral("qml") && fi.fileName() == QStringLiteral("WallpaperFace.qml")) {
                // Wire "Live video" online tab seamlessly into WallpaperFace.qml
                text.replace(
                    QStringLiteral("    readonly property bool viewIsVideo: face.viewPath !== \"\" && WallpaperService.isVideo(face.viewPath)"),
                    QStringLiteral("    readonly property bool viewIsVideo: (face.viewWh && !!face.viewWh.isVideo) || (face.viewPath !== \"\" && WallpaperService.isVideo(face.viewPath))")
                );
                text.replace(
                    QStringLiteral("    readonly property bool onWallhaven: face.source === \"wallhaven\""),
                    QStringLiteral(
                        "    readonly property bool onLive: face.source === \"live\"\n"
                        "    readonly property bool onWallhaven: face.source === \"wallhaven\" || face.source === \"live\"\n"
                        "    readonly property var liveCategoryOptions: [\n"
                        "        { mode: \"anime\",        label: \"Anime (Live MP4)\",        short: \"Anime\" },\n"
                        "        { mode: \"girl\",         label: \"Anime Girl (Live MP4)\",   short: \"Anime Girl\" },\n"
                        "        { mode: \"anime-nature\", label: \"Anime Nature (Live MP4)\", short: \"Anime Nature\" },\n"
                        "        { mode: \"games\",        label: \"Games (Live MP4)\",        short: \"Games\" },\n"
                        "        { mode: \"nature\",       label: \"Nature (Live MP4)\",       short: \"Nature\" },\n"
                        "        { mode: \"superhero\",    label: \"Superhero (Live MP4)\",    short: \"Superhero\" },\n"
                        "        { mode: \"4k\",           label: \"All 4K Live Videos\",      short: \"All 4K\" }\n"
                        "    ]"
                    )
                );
                text.replace(
                    QStringLiteral(
                        "        if (face.onWallhaven && (WallhavenService.page === 0 || WallhavenService.query !== \"\"))\n"
                        "            WallhavenService.search(\"\", face.screenPxW, face.screenPxH)"
                    ),
                    QStringLiteral(
                        "        if (face.onWallhaven) {\n"
                        "            WallhavenService.setProvider(face.onLive ? \"live\" : \"wallhaven\")\n"
                        "            if (WallhavenService.page === 0 || WallhavenService.query !== \"\")\n"
                        "                WallhavenService.search(\"\", face.screenPxW, face.screenPxH)\n"
                        "        }"
                    )
                );
                text.replace(
                    QStringLiteral("                fileName: \"#\" + it.id,"),
                    QStringLiteral("                fileName: (it.title && it.title !== \"\") ? it.title : (\"#\" + it.id),")
                );
                text.replace(
                    QStringLiteral("            face.setSource(face.onWallhaven ? \"folder\" : \"wallhaven\")"),
                    QStringLiteral("            face.setSource(face.source === \"folder\" ? \"wallhaven\" : (face.source === \"wallhaven\" ? \"live\" : \"folder\"))")
                );
                text.replace(
                    QStringLiteral("            readonly property bool video: WallpaperService.isVideo(cell.path)"),
                    QStringLiteral("            readonly property bool video: (cell.wh && !!cell.wh.isVideo) || WallpaperService.isVideo(cell.path)")
                );
                text.replace(
                    QStringLiteral(
                        "    Component.onCompleted: {\n"
                        "        if (files.status === FolderListModel.Ready)\n"
                        "            face.refresh()\n"
                        "    }"
                    ),
                    QStringLiteral(
                        "    Component.onCompleted: {\n"
                        "        if (files.status === FolderListModel.Ready)\n"
                        "            face.refresh()\n"
                        "        WallhavenService.preloadLive()\n"
                        "    }"
                    )
                );
                text.replace(
                    QStringLiteral("                        source: cell.wh ? cell.wh.thumb : WallpaperService.thumbUrl(cell.path)"),
                    QStringLiteral("                        source: !grid.visible ? \"\" : (cell.wh ? cell.wh.thumb : WallpaperService.thumbUrl(cell.path))")
                );
                text.replace(
                    QStringLiteral(
                        "            text: !face.onWallhaven ? face.subtitle\n"
                        "                : (WallhavenService.loading && WallhavenService.page === 0 ? \"Searching Wallhaven\\u2026\"\n"
                        "                : (WallhavenService.error !== \"\" ? \"Wallhaven\" : WallhavenService.description))"
                    ),
                    QStringLiteral(
                        "            text: !face.onWallhaven ? face.subtitle\n"
                        "                : (WallhavenService.loading && WallhavenService.page === 0 ? (face.onLive ? \"Loading live videos\\u2026\" : \"Searching Wallhaven\\u2026\")\n"
                        "                : (WallhavenService.error !== \"\" ? (face.onLive ? \"Live video\" : \"Wallhaven\") : WallhavenService.description))"
                    )
                );
                text.replace(
                    QStringLiteral("                placeholder: face.onWallhaven ? \"Search Wallhaven\" : \"Search wallpapers\""),
                    QStringLiteral("                placeholder: face.onLive ? \"Search live videos\" : (face.onWallhaven ? \"Search Wallhaven\" : \"Search wallpapers\")")
                );
                text.replace(
                    QStringLiteral("                label: face.onWallhaven ? WallhavenService.sortLabel : face.sortShort"),
                    QStringLiteral("                label: face.onLive ? WallhavenService.liveCategoryLabel : (face.onWallhaven ? WallhavenService.sortLabel : face.sortShort)")
                );
                text.replace(
                    QStringLiteral(
                        "            WallhavenFilterChip {\n"
                        "                id: filterChip\n"
                        "                visible: face.onWallhaven"
                    ),
                    QStringLiteral(
                        "            WallhavenFilterChip {\n"
                        "                id: filterChip\n"
                        "                visible: face.source === \"wallhaven\""
                    )
                );
                text.replace(
                    QStringLiteral(
                        "        GallerySortCard {\n"
                        "            x: controls.x + sortChip.x + sortChip.width - width\n"
                        "            y: face.toolbarH + 6\n"
                        "            shown: face.sortOpen && !face.onWallhaven\n"
                        "            sortMode: face.sortMode\n"
                        "            options: face.sortOptions\n"
                        "            onSelected: mode => {\n"
                        "                face.setSort(mode)\n"
                        "                face.sortOpen = false\n"
                        "            }\n"
                        "        }"
                    ),
                    QStringLiteral(
                        "        GallerySortCard {\n"
                        "            x: controls.x + sortChip.x + sortChip.width - width\n"
                        "            y: face.toolbarH + 6\n"
                        "            shown: face.sortOpen && (!face.onWallhaven || face.onLive)\n"
                        "            sortMode: face.onLive ? WallhavenService.liveCategory : face.sortMode\n"
                        "            options: face.onLive ? face.liveCategoryOptions : face.sortOptions\n"
                        "            onSelected: mode => {\n"
                        "                if (face.onLive)\n"
                        "                    WallhavenService.setLiveCategory(mode)\n"
                        "                else\n"
                        "                    face.setSort(mode)\n"
                        "                face.sortOpen = false\n"
                        "            }\n"
                        "        }"
                    )
                );
                text.replace(
                    QStringLiteral("            shown: face.sortOpen && face.onWallhaven"),
                    QStringLiteral("            shown: face.sortOpen && face.source === \"wallhaven\"")
                );
                text.replace(
                    QStringLiteral("            shown: face.filtersOpen && face.onWallhaven"),
                    QStringLiteral("            shown: face.filtersOpen && face.source === \"wallhaven\"")
                );
                text.replace(
                    QStringLiteral(
                        "                    source: face.viewPath === \"\" ? (face.viewWh ? face.viewWh.full : \"\")\n"
                        "                            : (face.viewIsVideo ? WallpaperService.thumbUrl(face.viewPath) : \"file://\" + face.viewPath)"
                    ),
                    QStringLiteral(
                        "                    source: (face.viewWh && face.viewWh.isVideo) ? (face.viewWh.preview || face.viewWh.thumb)\n"
                        "                            : (face.viewPath === \"\" ? (face.viewWh ? face.viewWh.full : \"\")\n"
                        "                            : (face.viewIsVideo ? WallpaperService.thumbUrl(face.viewPath)\n"
                        "                            : ((face.viewPath.indexOf(\"/\") === 0 ? \"file://\" : \"file:///\") + face.viewPath)))"
                    )
                );
                text.replace(
                    QStringLiteral(
                        "                        MaterialIcon {\n"
                        "                            anchors.verticalCenter: parent.verticalCenter\n"
                        "                            name: \"travel_explore\"\n"
                        "                            size: 16\n"
                        "                            color: mt.pInk\n"
                        "                        }\n"
                        "                        Text {\n"
                        "                            anchors.verticalCenter: parent.verticalCenter\n"
                        "                            text: \"Wallhaven\""
                    ),
                    QStringLiteral(
                        "                        MaterialIcon {\n"
                        "                            anchors.verticalCenter: parent.verticalCenter\n"
                        "                            name: (face.viewWh && face.viewWh.isVideo) ? \"movie\" : \"travel_explore\"\n"
                        "                            size: 16\n"
                        "                            color: mt.pInk\n"
                        "                        }\n"
                        "                        Text {\n"
                        "                            anchors.verticalCenter: parent.verticalCenter\n"
                        "                            text: (face.viewWh && face.viewWh.isVideo) ? \"Live video\" : \"Wallhaven\""
                    )
                );
                text.replace(
                    QStringLiteral(
                        "                visible: face.viewWh !== null\n"
                        "                text: \"WALLHAVEN\""
                    ),
                    QStringLiteral(
                        "                visible: face.viewWh !== null\n"
                        "                text: (face.viewWh && face.viewWh.isVideo) ? \"LIVE VIDEO\" : \"WALLHAVEN\""
                    )
                );
                text.replace(
                    QStringLiteral(
                        "                        face.viewWh.type.toUpperCase(),\n"
                        "                        WallhavenService.humanSize(face.viewWh.size)"
                    ),
                    QStringLiteral(
                        "                        face.viewWh.type.toUpperCase(),\n"
                        "                        face.viewWh.size > 0 ? WallhavenService.humanSize(face.viewWh.size) : \"\""
                    )
                );
                text.replace(
                    QStringLiteral(
                        "            Row {\n"
                        "                visible: face.viewWh !== null\n"
                        "                spacing: 14"
                    ),
                    QStringLiteral(
                        "            Row {\n"
                        "                visible: face.viewWh !== null && (face.viewWh.views > 0 || face.viewWh.favourites > 0)\n"
                        "                spacing: 14"
                    )
                );
                text.replace(
                    QStringLiteral(
                        "            label: viewerArea.whDownloading\n"
                        "                ? \"Downloading \\u00b7 \" + WallhavenService.humanSize(WallhavenService.fetched) + \" of \" + WallhavenService.humanSize(face.viewWh.size)\n"
                        "                : \"Set as Desktop\""
                    ),
                    QStringLiteral(
                        "            label: viewerArea.whDownloading\n"
                        "                ? (face.viewWh.size > 0\n"
                        "                    ? \"Downloading \\u00b7 \" + WallhavenService.humanSize(WallhavenService.fetched) + \" of \" + WallhavenService.humanSize(face.viewWh.size)\n"
                        "                    : \"Downloading \\u00b7 \" + WallhavenService.humanSize(WallhavenService.fetched))\n"
                        "                : \"Set as Desktop\""
                    )
                );
                text.replace(
                    QStringLiteral("        readonly property bool whDownloading: face.viewWh !== null && WallhavenService.fetching === face.viewWh.id"),
                    QStringLiteral("        readonly property bool whDownloading: WallhavenService.fetching !== \"\" && face.viewWh !== null && face.viewWh.id !== \"\" && WallhavenService.fetching === face.viewWh.id")
                );
                text.replace(
                    QStringLiteral(
                        "            WallhavenChip {\n"
                        "                icon: \"open_in_new\"\n"
                        "                text: \"wallhaven.cc\""
                    ),
                    QStringLiteral(
                        "            WallhavenChip {\n"
                        "                icon: \"open_in_new\"\n"
                        "                text: (face.viewWh && face.viewWh.isVideo) ? \"motionbgs.com\" : \"wallhaven.cc\""
                    )
                );
            }

            if (ext == QStringLiteral("qml") && fi.fileName() == QStringLiteral("WallpaperWheel.qml")) {
                text.replace(
                    QStringLiteral("const first = root.count > 0 ? String(root.items[0].fileName || \"\") : \"\""),
                    QStringLiteral("const first = (root.count > 0 && root.items && root.items[0]) ? String(root.items[0].fileName || \"\") : \"\"")
                );
                text.replace(
                    QStringLiteral("    readonly property string focusPath: root.centerIndex >= 0 ? (root.items[root.centerIndex].filePath || \"\") : \"\""),
                    QStringLiteral("    readonly property string focusPath: (root.centerIndex >= 0 && root.centerIndex < root.count && root.items[root.centerIndex]) ? (root.items[root.centerIndex].filePath || \"\") : \"\"")
                );
                text.replace(
                    QStringLiteral("    readonly property string focusName: root.centerIndex >= 0 ? root.baseName(root.items[root.centerIndex].fileName || \"\") : \"\""),
                    QStringLiteral("    readonly property string focusName: (root.centerIndex >= 0 && root.centerIndex < root.count && root.items[root.centerIndex]) ? root.baseName(root.items[root.centerIndex].fileName || \"\") : \"\"")
                );
                text.replace(
                    QStringLiteral("    readonly property bool shownOk: root.shownIndex >= 0 && root.shownIndex < root.count"),
                    QStringLiteral("    readonly property bool shownOk: root.shownIndex >= 0 && root.shownIndex < root.count && !!root.items[root.shownIndex]")
                );
                text.replace(
                    QStringLiteral("readonly property bool applied: gon.path !== \"\" && gon.path === root.currentPath"),
                    QStringLiteral("readonly property bool applied: gon.path !== \"\" && WallpaperService.thumbKey(gon.path) === WallpaperService.thumbKey(root.currentPath)")
                );
                text.replace(
                    QStringLiteral("readonly property bool other: root.otherPath !== \"\" && gon.path === root.otherPath"),
                    QStringLiteral("readonly property bool other: root.otherPath !== \"\" && WallpaperService.thumbKey(gon.path) === WallpaperService.thumbKey(root.otherPath)")
                );
                text.replace(
                    QStringLiteral("readonly property bool video: gon.path !== \"\" && WallpaperService.isVideo(gon.path)"),
                    QStringLiteral("readonly property bool video: (gon.entry && gon.entry.wh && !!gon.entry.wh.isVideo) || (gon.path !== \"\" && WallpaperService.isVideo(gon.path))")
                );
                text.replace(
                    QStringLiteral("                        source: !gon.valid ? \"\" : (gon.entry && gon.entry.wh ? gon.entry.wh.thumb : WallpaperService.thumbUrl(gon.path))"),
                    QStringLiteral("                        source: (!root.visible || !gon.visible || !gon.valid) ? \"\" : (gon.entry && gon.entry.wh ? gon.entry.wh.thumb : WallpaperService.thumbUrl(gon.path))")
                );
                text.replace(
                    QStringLiteral("                        sourceSize.width: Math.ceil((root.cardW - 22) * root.renderScale * 2)"),
                    QStringLiteral("                        sourceSize.width: Math.min(720, Math.ceil((root.cardW - 22) * root.renderScale * 2))")
                );
                text.replace(
                    QStringLiteral("                        source: gon.sharpOn && gon.valid && gon.path !== \"\" ? \"file://\" + gon.path : \"\""),
                    QStringLiteral("                        source: (root.visible && gon.visible && gon.sharpOn && gon.valid && gon.path !== \"\") ? ((gon.path.indexOf(\"/\") === 0 ? \"file://\" : \"file:///\") + gon.path) : \"\"")
                );
                text.replace(
                    QStringLiteral("    readonly property string shownName: root.shownOk ? root.baseName(root.items[root.shownIndex].fileName || \"\") : \"\""),
                    QStringLiteral(
                        "    readonly property string shownName: root.shownOk ? root.baseName(root.items[root.shownIndex].fileName || \"\") : \"\"\n"
                        "    readonly property var shownWh: root.shownOk ? (root.items[root.shownIndex].wh || null) : null\n"
                        "    readonly property bool shownDownloading: WallhavenService.fetching !== \"\" && root.shownWh !== null && root.shownWh.id !== \"\" && WallhavenService.fetching === root.shownWh.id"
                    )
                );
                text.replace(
                    QStringLiteral(
                        "            WallpaperActionKey {\n"
                        "                capW: 320\n"
                        "                capH: 64\n"
                        "                icon: \"desktop_windows\"\n"
                        "                label: \"Set as Desktop\"\n"
                        "                done: root.justSet === \"desktop\"\n"
                        "                onClicked: all => root.setDesktopRequested(root.shownPath, all)\n"
                        "            }"
                    ),
                    QStringLiteral(
                        "            WallpaperActionKey {\n"
                        "                capW: 320\n"
                        "                capH: 64\n"
                        "                icon: root.shownDownloading ? \"download\" : \"desktop_windows\"\n"
                        "                label: root.shownDownloading\n"
                        "                    ? (\"Downloading \\u00b7 \" + WallhavenService.humanSize(WallhavenService.fetched))\n"
                        "                    : \"Set as Desktop\"\n"
                        "                progress: root.shownDownloading ? WallhavenService.progress : -1\n"
                        "                done: root.justSet === \"desktop\"\n"
                        "                onClicked: all => root.setDesktopRequested(root.shownPath, all)\n"
                        "            }"
                    )
                );
            }

            if (ext == QStringLiteral("js") && fi.fileName() == QStringLiteral("LyricsMath.js")) {
                // Give 3-tier Ukikura lyrics (Romaji + Kanji + Sub Indo) clean room above the upcoming line
                text.replace(
                    QStringLiteral("    { d: 1, y: 68, s: 0.66, o: 0.55, r: -2.4 },\n    { d: 2, y: 122, s: 0.46, o: 0, r: -4.8 }"),
                    QStringLiteral("    { d: 1, y: 88, s: 0.66, o: 0.55, r: -2.4 },\n    { d: 2, y: 146, s: 0.46, o: 0, r: -4.8 }")
                );
            }

            const QString targetWritePath = isSingleton ? singletons.last().stagedPath : dstPath;
            writeFileIfChanged(targetWritePath, text.toUtf8());
        }
    };

    processDir(QString());
    processDir(QStringLiteral("services"));
    processDir(QStringLiteral("modules"));

    // Write qmldir for every module directory
    for (auto it = dirRegularComponents.begin(); it != dirRegularComponents.end(); ++it) {
        const QString relDir = it.key();
        QStringList types = it.value();
        types.sort();
        types.removeDuplicates();

        QString moduleName = QStringLiteral("qs.") + QString(relDir).replace(QLatin1Char('/'), QLatin1Char('.'));
        QString qmldirText = QStringLiteral("module ") + moduleName + QLatin1Char('\n');
        for (const QString& t : types) {
            qmldirText += QStringLiteral("%1 1.0 %1.qml\n").arg(t);
        }
        writeFileIfChanged(stageQsDir + QLatin1Char('/') + relDir + QStringLiteral("/qmldir"), qmldirText.toUtf8());
    }

    std::sort(singletons.begin(), singletons.end(), [](const StagedSingletonInfo& a, const StagedSingletonInfo& b) {
        if (a.priority != b.priority) {
            return a.priority < b.priority;
        }
        return a.name < b.name;
    });

    QString cacheOut = expectedHeader + QLatin1Char('\n');
    for (const StagedSingletonInfo& s : singletons) {
        cacheOut += QStringLiteral("%1\t%2\t%3\n").arg(s.name, s.stagedPath).arg(s.priority);
    }
    writeFileIfChanged(cacheFilePath, cacheOut.toUtf8());
    qInfo("[QsEngineSetup] Re-staged sekaipop tree (%d singletons) in %lldms",
          static_cast<int>(singletons.size()), stageTimer.elapsed());

    return singletons;
}

} // namespace

// ============================================================================
// QsDeferredBoot (Micro-Batched Tier-2 Singleton Initialization Post-UI-Ready)
// ============================================================================

QsDeferredBoot::QsDeferredBoot(QQmlApplicationEngine* engine, QList<StagedSingletonEntry> tier2, QObject* parent)
    : QObject(parent)
    , m_engine(engine)
    , m_tier2(std::move(tier2)) {}

void QsDeferredBoot::initOne(const StagedSingletonEntry& info) {
    if (!m_engine) {
        return;
    }
    QQmlContext* rootCtx = m_engine->rootContext();
    auto* comp = new QQmlComponent(m_engine, QUrl::fromLocalFile(info.stagedPath), m_engine);
    if (comp->isError()) {
        for (const QQmlError& err : comp->errors()) {
            qWarning("[QsEngineSetup] Tier-2 singleton compile error (%s): %s",
                     qPrintable(info.name), qPrintable(err.toString()));
        }
        return;
    }
    QObject* obj = comp->beginCreate(rootCtx);
    if (!obj) {
        qWarning("[QsEngineSetup] Failed beginCreate for Tier-2 singleton: %s", qPrintable(info.name));
        return;
    }
    obj->setParent(m_engine);
    rootCtx->setContextProperty(info.name, obj);
    comp->completeCreate();
    if (info.name == QStringLiteral("EditMode")) {
        if (auto* wm = QsWindowManager::instance()) {
            wm->setEditModeObject(obj);
        }
    }
}

void QsDeferredBoot::finalizeTier2() {
    if (m_tier2Ready) {
        return;
    }
    m_tier2Ready = true;
    qInfo("[QsEngineSetup] Deferred Tier-2 singletons (%d) ready in %lldms.",
          static_cast<int>(m_tier2.size()), m_timer.isValid() ? m_timer.elapsed() : 0LL);
    Q_EMIT tier2ReadyChanged();
}

void QsDeferredBoot::scheduleNextBatch() {
    if (m_tier2Ready || !m_engine) {
        return;
    }
    constexpr int kBatchSize = 5;
    int ran = 0;
    while (m_index < m_tier2.size() && ran < kBatchSize) {
        initOne(m_tier2.at(m_index++));
        ++ran;
    }
    if (m_index >= m_tier2.size()) {
        finalizeTier2();
        return;
    }
    QTimer::singleShot(0, this, [this]() {
        scheduleNextBatch();
    });
}

void QsDeferredBoot::startAsync() {
    if (m_started || m_tier2Ready) {
        return;
    }
    m_started = true;
    m_timer.start();
    if (m_tier2.isEmpty()) {
        finalizeTier2();
        return;
    }
    QTimer::singleShot(0, this, [this]() {
        scheduleNextBatch();
    });
}

void QsDeferredBoot::ensureTier2Now() {
    if (m_tier2Ready || !m_engine) {
        return;
    }
    if (!m_started) {
        m_started = true;
        m_timer.start();
    }
    while (m_index < m_tier2.size()) {
        initOne(m_tier2.at(m_index++));
    }
    finalizeTier2();
}

namespace {

void registerQuickshellTypes(QQmlApplicationEngine& engine, const QsRuntimeDeps& deps, const QString& stageRoot, const QString& stageQsDir, const QString& shadersDir) {
    engine.setNetworkAccessManagerFactory(new QsNamFactory());
    engine.addUrlInterceptor(new QsUrlInterceptor(shadersDir));
    engine.addImportPath(stageRoot);

    auto* qsGlobal = new QsGlobal(stageQsDir, deps.appSource, deps.windowTracker, &engine);
    auto* qsDesktopEntries = new QsDesktopEntries(deps.appSource, deps.windowTracker, &engine);
    auto* qsHyprland = new QsHyprland(deps.windowTracker, &engine);
    auto* qsToplevelMgr = new QsToplevelManager(qsHyprland, &engine);
    auto* qsMpris = new QsMpris(deps.mediaSource, deps.quickSource, &engine);
    auto* qsPipewire = new QsPipewire(deps.quickSource, &engine);
    auto* qsUPower = new QsUPower(deps.batterySource, &engine);
    auto* qsPowerProfiles = new QsPowerProfiles(&engine);
    auto* qsSysTray = new QsSystemTray(&engine);

    // Module: Quickshell
    qmlRegisterSingletonInstance("Quickshell", 1, 0, "Quickshell", qsGlobal);
    qmlRegisterSingletonInstance("Quickshell", 1, 0, "DesktopEntries", qsDesktopEntries);
    qmlRegisterUncreatableType<QsWindow>("Quickshell", 1, 0, "QsWindow", QStringLiteral("Attached property"));
    qmlRegisterUncreatableType<ExclusionMode>("Quickshell", 1, 0, "ExclusionMode", QStringLiteral("Enum"));
    qmlRegisterUncreatableType<Edges>("Quickshell", 1, 0, "Edges", QStringLiteral("Enum"));
    qmlRegisterUncreatableType<Intersection>("Quickshell", 1, 0, "Intersection", QStringLiteral("Enum"));
    qmlRegisterUncreatableType<PopupAdjustment>("Quickshell", 1, 0, "PopupAdjustment", QStringLiteral("Enum"));
    qmlRegisterUncreatableType<QsMenuButtonType>("Quickshell", 1, 0, "QsMenuButtonType", QStringLiteral("Enum"));
    qmlRegisterType<QsShellRoot>("Quickshell", 1, 0, "ShellRoot");
    qmlRegisterType<QsSingleton>("Quickshell", 1, 0, "Singleton");
    qmlRegisterType<QsScope>("Quickshell", 1, 0, "Scope");
    qmlRegisterType<QsVariants>("Quickshell", 1, 0, "Variants");
    qmlRegisterType<QsLazyLoader>("Quickshell", 1, 0, "LazyLoader");
    qmlRegisterType<QsSystemClock>("Quickshell", 1, 0, "SystemClock");
    qmlRegisterType<QsIpcHandler>("Quickshell", 1, 0, "IpcHandler");
    qmlRegisterType<QsScriptModel>("Quickshell", 1, 0, "ScriptModel");
    qmlRegisterType<QsColorQuantizer>("Quickshell", 1, 0, "ColorQuantizer");
    qmlRegisterType<QsMenuOpener>("Quickshell", 1, 0, "QsMenuOpener");
    qmlRegisterType<QsPanelWindow>("Quickshell", 1, 0, "PanelWindow");
    qmlRegisterType<QsPopupWindow>("Quickshell", 1, 0, "PopupWindow");
    qmlRegisterType<QsFloatingWindow>("Quickshell", 1, 0, "FloatingWindow");
    qmlRegisterType<QsRegion>("Quickshell", 1, 0, "Region");
    qmlRegisterType<QsClippingRectangle>("Quickshell", 1, 0, "QsClippingRectangle");
    qmlRegisterType<Services::VideoWallpaperItem>("Quickshell", 1, 0, "VideoWallpaperSurface");

    // Module: Quickshell.Io
    qmlRegisterType<QsProcess>("Quickshell.Io", 1, 0, "Process");
    qmlRegisterType<QsStdioCollector>("Quickshell.Io", 1, 0, "StdioCollector");
    qmlRegisterType<QsSplitParser>("Quickshell.Io", 1, 0, "SplitParser");
    qmlRegisterType<QsFileView>("Quickshell.Io", 1, 0, "FileView");
    qmlRegisterType<QsSocket>("Quickshell.Io", 1, 0, "Socket");

    // Module: Quickshell.Wayland
    qmlRegisterUncreatableType<WlrLayershell>("Quickshell.Wayland", 1, 0, "WlrLayershell", QStringLiteral("Attached property"));
    qmlRegisterUncreatableType<WlrLayer>("Quickshell.Wayland", 1, 0, "WlrLayer", QStringLiteral("Enum"));
    qmlRegisterUncreatableType<WlrKeyboardFocus>("Quickshell.Wayland", 1, 0, "WlrKeyboardFocus", QStringLiteral("Enum"));
    qmlRegisterType<WlSessionLock>("Quickshell.Wayland", 1, 0, "WlSessionLock");
    qmlRegisterType<WlSessionLockSurface>("Quickshell.Wayland", 1, 0, "WlSessionLockSurface");
    qmlRegisterType<QsScreencopyView>("Quickshell.Wayland", 1, 0, "ScreencopyView");
    qmlRegisterSingletonInstance("Quickshell.Wayland", 1, 0, "ToplevelManager", qsToplevelMgr);

    // Module: Quickshell.Hyprland
    qmlRegisterSingletonInstance("Quickshell.Hyprland", 1, 0, "Hyprland", qsHyprland);
    qmlRegisterType<QsHyprlandFocusGrab>("Quickshell.Hyprland", 1, 0, "HyprlandFocusGrab");
    qmlRegisterType<QsGlobalShortcut>("Quickshell.Hyprland", 1, 0, "GlobalShortcut");

    // Module: Quickshell.Services.Mpris
    qmlRegisterSingletonInstance("Quickshell.Services.Mpris", 1, 0, "Mpris", qsMpris);
    qmlRegisterUncreatableType<MprisPlaybackState>("Quickshell.Services.Mpris", 1, 0, "MprisPlaybackState", QStringLiteral("Enum"));
    qmlRegisterUncreatableType<QsMprisPlayer>("Quickshell.Services.Mpris", 1, 0, "MprisPlayer", QStringLiteral("Managed by Mpris"));

    // Module: Quickshell.Services.Pipewire
    qmlRegisterSingletonInstance("Quickshell.Services.Pipewire", 1, 0, "Pipewire", qsPipewire);
    qmlRegisterType<QsPwObjectTracker>("Quickshell.Services.Pipewire", 1, 0, "PwObjectTracker");

    // Module: Quickshell.Services.UPower
    qmlRegisterSingletonInstance("Quickshell.Services.UPower", 1, 0, "UPower", qsUPower);
    qmlRegisterUncreatableType<UPowerDeviceState>("Quickshell.Services.UPower", 1, 0, "UPowerDeviceState", QStringLiteral("Enum"));
    qmlRegisterSingletonInstance("Quickshell.Services.UPower", 1, 0, "PowerProfiles", qsPowerProfiles);
    qmlRegisterUncreatableType<PowerProfile>("Quickshell.Services.UPower", 1, 0, "PowerProfile", QStringLiteral("Enum"));

    // Module: Quickshell.Services.Notifications
    qmlRegisterType<QsNotificationServer>("Quickshell.Services.Notifications", 1, 0, "NotificationServer");

    // Module: Quickshell.Services.SystemTray
    qmlRegisterSingletonInstance("Quickshell.Services.SystemTray", 1, 0, "SystemTray", qsSysTray);
    qmlRegisterUncreatableType<QsSystemTrayStatus>("Quickshell.Services.SystemTray", 1, 0, "Status", QStringLiteral("Enum"));

    // Module: Quickshell.Services.Polkit
    qmlRegisterType<QsPolkitAgent>("Quickshell.Services.Polkit", 1, 0, "PolkitAgent");

    // Module: Quickshell.Services.Pam
    qmlRegisterType<QsPamContext>("Quickshell.Services.Pam", 1, 0, "PamContext");
    qmlRegisterUncreatableType<QsPamResult>("Quickshell.Services.Pam", 1, 0, "PamResult", QStringLiteral("Enum"));

    QQmlContext* rootCtx = engine.rootContext();
    rootCtx->setContextProperty(QStringLiteral("Quickshell"), qsGlobal);
    rootCtx->setContextProperty(QStringLiteral("DesktopEntries"), qsDesktopEntries);
    rootCtx->setContextProperty(QStringLiteral("Hyprland"), qsHyprland);
    rootCtx->setContextProperty(QStringLiteral("Mpris"), qsMpris);
    rootCtx->setContextProperty(QStringLiteral("Pipewire"), qsPipewire);
    rootCtx->setContextProperty(QStringLiteral("UPower"), qsUPower);
    rootCtx->setContextProperty(QStringLiteral("PowerProfiles"), qsPowerProfiles);
    rootCtx->setContextProperty(QStringLiteral("SystemTray"), qsSysTray);
}

} // namespace

QUrl setupQuickshellEngine(QQmlApplicationEngine& engine, const QsRuntimeDeps& deps, QsDeferredBoot** outDeferred) {
    const QString sekaipopDir = QDir(deps.baseDir).filePath(QStringLiteral("sekaipop"));
    const QString localDataRoot = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    const QString stageRoot = !localDataRoot.isEmpty()
        ? QDir(localDataRoot).filePath(QStringLiteral("SekaiPop/qs_stage"))
        : QDir(deps.exeDir).filePath(QStringLiteral("qs_stage"));
    const QString stageQsDir = QDir(stageRoot).filePath(QStringLiteral("qs"));
    const QString shadersDir = QDir(deps.exeDir).filePath(QStringLiteral("shaders"));

    QsWindowManager::instance()->init(deps.wallpaperHost, deps.appBarManager);
    QsProcess::initBackends(deps.spectrumSource, deps.quickSource, deps.statusIconsService, deps.windowTracker, deps.mediaSource);

    const QList<StagedSingletonInfo> singletons = stageSekaipopTree(sekaipopDir, deps.exeDir, stageRoot, stageQsDir);

    registerQuickshellTypes(engine, deps, stageRoot, stageQsDir, shadersDir);

    QQmlContext* rootCtx = engine.rootContext();
    QList<StagedSingletonInfo> tier1;
    QList<StagedSingletonInfo> tier2;
    tier1.reserve(30);
    tier2.reserve(singletons.size());
    for (const StagedSingletonInfo& info : singletons) {
        rootCtx->setContextProperty(info.name, static_cast<QObject*>(nullptr));
        if (isTier1Singleton(info.name)) {
            tier1.append(info);
        } else {
            tier2.append(info);
        }
    }

    auto* deferred = new QsDeferredBoot(&engine, std::move(tier2), &engine);
    rootCtx->setContextProperty(QStringLiteral("QsDeferredBoot"), deferred);
    if (outDeferred) {
        *outDeferred = deferred;
    }

    // Initialize ONLY Tier-1 singletons required for Wallpaper + Bar before engine.load(shell.qml).
    QElapsedTimer singletonsTimer;
    singletonsTimer.start();
    int initializedCount = 0;
    for (const StagedSingletonInfo& info : tier1) {
        QElapsedTimer oneTimer;
        oneTimer.start();
        auto* comp = new QQmlComponent(&engine, QUrl::fromLocalFile(info.stagedPath), &engine);
        if (comp->isError()) {
            for (const QQmlError& err : comp->errors()) {
                qWarning("[QsEngineSetup] Singleton compile error (%s): %s",
                         qPrintable(info.name), qPrintable(err.toString()));
            }
            continue;
        }
        QObject* obj = comp->beginCreate(rootCtx);
        if (!obj) {
            qWarning("[QsEngineSetup] Failed beginCreate for singleton: %s", qPrintable(info.name));
            continue;
        }
        obj->setParent(&engine);
        rootCtx->setContextProperty(info.name, obj);
        comp->completeCreate();
        ++initializedCount;

        const qint64 oneMs = oneTimer.elapsed();
        if (oneMs >= 30) {
            qInfo("[QsEngineSetup] Slow Tier-1 singleton %s: %lldms", qPrintable(info.name), oneMs);
        }

        if (info.name == QStringLiteral("EditMode")) {
            QsWindowManager::instance()->setEditModeObject(obj);
        }
    }

    // Pre-settle fast local C++ state-load QTimer::singleShot(0) callbacks (SettingsService,
    // BarPositionService, BarStyleService, BarLayoutService, GameModeService, WallpaperService,
    // ThemeService, ShellScale) BEFORE shell.qml loads so Wallpaper + Bar instantiate with their
    // final saved configuration on Frame 0 (eliminating post-load waiting & double-layouts).
    QElapsedTimer settleTimer;
    settleTimer.start();
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 25);
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 25);

    qInfo("[QsEngineSetup] Initialized %d Tier-1 singletons in %lldms (pre-settle=%lldms, %d Tier-2 deferred).",
          initializedCount, singletonsTimer.elapsed(), settleTimer.elapsed(),
          static_cast<int>(singletons.size()) - initializedCount);
    return QUrl::fromLocalFile(QDir(stageQsDir).filePath(QStringLiteral("shell.qml")));
}

void prewarmQuickshellCache(QQmlApplicationEngine& engine, const QsRuntimeDeps& deps) {
    QElapsedTimer total;
    total.start();
    const QString sekaipopDir = QDir(deps.baseDir).filePath(QStringLiteral("sekaipop"));
    const QString localDataRoot = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    const QString stageRoot = !localDataRoot.isEmpty()
        ? QDir(localDataRoot).filePath(QStringLiteral("SekaiPop/qs_stage"))
        : QDir(deps.exeDir).filePath(QStringLiteral("qs_stage"));
    const QString stageQsDir = QDir(stageRoot).filePath(QStringLiteral("qs"));
    const QString shadersDir = QDir(deps.exeDir).filePath(QStringLiteral("shaders"));

    const QList<StagedSingletonInfo> singletons = stageSekaipopTree(sekaipopDir, deps.exeDir, stageRoot, stageQsDir);
    registerQuickshellTypes(engine, deps, stageRoot, stageQsDir, shadersDir);

    QQmlContext* rootCtx = engine.rootContext();
    for (const StagedSingletonInfo& info : singletons) {
        rootCtx->setContextProperty(info.name, static_cast<QObject*>(nullptr));
    }
    auto* dummyDeferred = new QsDeferredBoot(&engine, {}, &engine);
    rootCtx->setContextProperty(QStringLiteral("QsDeferredBoot"), dummyDeferred);

    // Compile-only (no create(), no windows, no D3D11) to generate hot .qmlc bytecode on disk.
    int compiled = 0;
    for (const StagedSingletonInfo& info : singletons) {
        QQmlComponent comp(&engine, QUrl::fromLocalFile(info.stagedPath));
        if (!comp.isError()) {
            ++compiled;
        }
    }
    const QStringList coreRelPaths = {
        QStringLiteral("shell.qml"),
        QStringLiteral("DeferredWidgets.qml"),
        QStringLiteral("modules/sekai/background/WallpaperLayer.qml"),
        QStringLiteral("modules/sekai/bar/Bar.qml"),
        QStringLiteral("modules/sekai/bar/Status_Bar.qml"),
        QStringLiteral("modules/sekai/dock/Dock.qml")
    };
    for (const QString& rel : coreRelPaths) {
        const QString p = QDir(stageQsDir).filePath(rel);
        if (QFileInfo::exists(p)) {
            QQmlComponent comp(&engine, QUrl::fromLocalFile(p));
            if (!comp.isError()) {
                ++compiled;
            }
        }
    }
    qInfo("[Prewarm] Staged tree and pre-compiled %d QML units (.qmlc hot cache) in %lldms.",
          compiled, total.elapsed());
}

} // namespace SekaiPop::Quickshell
