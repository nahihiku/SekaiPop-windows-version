#include "QsTypes.h"
#include "QsIo.h"
#include "../services/AppSource.h"
#include "../win32/WinIconProvider.h"
#include "../win32/WindowTracker.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJSValue>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaMethod>
#include <QMetaObject>
#include <QQmlContext>
#include <QQmlEngine>
#include <QRegularExpression>
#include <QStandardPaths>
#include <algorithm>
#include <windows.h>

namespace SekaiPop::Quickshell {

QString normalizeWinPath(const QString& raw) {
    QString s = raw.trimmed();
    if (s.isEmpty()) {
        return s;
    }
    if (s.startsWith(QStringLiteral("file:///"), Qt::CaseInsensitive)) {
        s = s.mid(8);
    } else if (s.startsWith(QStringLiteral("file://"), Qt::CaseInsensitive)) {
        s = s.mid(7);
    }
    if (s.contains(QLatin1Char('%'))) {
        s = QUrl::fromPercentEncoding(s.toUtf8());
    }
    s.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (s.size() >= 3 && s.at(0) == QLatin1Char('/') && s.at(1).isLetter() && s.at(2) == QLatin1Char(':')) {
        s.remove(0, 1);
    }
    if (s.size() >= 2 && s.at(0).isLetter() && s.at(1) == QLatin1Char('/')) {
        // Handle file://D:/foo where QUrl stripped ':' from host "d"
        const QString driveCandidate = s.left(1).toUpper() + QStringLiteral(":/") + s.mid(2);
        if (QFileInfo::exists(driveCandidate) || QFileInfo::exists(QFileInfo(driveCandidate).absolutePath())) {
            s = driveCandidate;
        }
    }
    return s;
}

static QString toLeadingSlashPath(const QString& winPath) {
    QString p = normalizeWinPath(winPath);
    if (p.size() >= 2 && p.at(0).isLetter() && p.at(1) == QLatin1Char(':')) {
        return QLatin1Char('/') + p;
    }
    return p;
}

// ============================================================================
// QsScreen
// ============================================================================

QsScreen::QsScreen(QScreen* screen, QObject* parent)
    : QObject(parent)
    , m_screen(screen) {
    if (m_screen) {
        connect(m_screen, &QScreen::geometryChanged, this, &QsScreen::geometryChanged);
        connect(m_screen, &QScreen::physicalDotsPerInchChanged, this, &QsScreen::geometryChanged);
    }
}

QString QsScreen::name() const {
    return m_screen ? m_screen->name() : QStringLiteral("Primary");
}

QString QsScreen::model() const {
    return m_screen ? m_screen->model() : QStringLiteral("Generic PnP Monitor");
}

int QsScreen::x() const {
    return m_screen ? m_screen->geometry().x() : 0;
}

int QsScreen::y() const {
    return m_screen ? m_screen->geometry().y() : 0;
}

int QsScreen::width() const {
    return m_screen ? m_screen->geometry().width() : 1920;
}

int QsScreen::height() const {
    return m_screen ? m_screen->geometry().height() : 1080;
}

qreal QsScreen::devicePixelRatio() const {
    return m_screen ? m_screen->devicePixelRatio() : 1.0;
}

// ============================================================================
// QsGlobal
// ============================================================================

QsGlobal* QsGlobal::s_instance = nullptr;

QsGlobal::QsGlobal(const QString& shellDir, Services::AppSource* appSource, Win32::WindowTracker* tracker, QObject* parent)
    : QObject(parent)
    , m_shellDir(toLeadingSlashPath(shellDir))
    , m_appSource(appSource)
    , m_windowTracker(tracker) {
    s_instance = this;

    QString localAppData = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (localAppData.isEmpty()) {
        localAppData = QDir::homePath() + QStringLiteral("/AppData/Local");
    }
    const QString rootData = QDir(localAppData).filePath(QStringLiteral("SekaiPop"));
    const QString stateDir = QDir(rootData).filePath(QStringLiteral("state"));
    const QString configDir = QDir(rootData).filePath(QStringLiteral("config"));
    const QString cacheDir = QDir(rootData).filePath(QStringLiteral("cache"));
    const QString runtimeDir = QDir(rootData).filePath(QStringLiteral("runtime"));

    QDir().mkpath(stateDir + QStringLiteral("/sekaipop"));
    QDir().mkpath(configDir + QStringLiteral("/sekaipop"));
    QDir().mkpath(cacheDir + QStringLiteral("/sekaipop/cover-art"));
    QDir().mkpath(runtimeDir);

    m_homeDir = toLeadingSlashPath(QDir::homePath());
    m_stateHome = toLeadingSlashPath(stateDir);
    m_configHome = toLeadingSlashPath(configDir);
    m_cacheHome = toLeadingSlashPath(cacheDir);
    m_runtimeDir = toLeadingSlashPath(runtimeDir);

    rebuildScreens();
    if (auto* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
        connect(app, &QGuiApplication::screenAdded, this, [this](QScreen*) { rebuildScreens(); });
        connect(app, &QGuiApplication::screenRemoved, this, [this](QScreen*) { rebuildScreens(); });
        if (QClipboard* cb = QGuiApplication::clipboard()) {
            connect(cb, &QClipboard::dataChanged, this, &QsGlobal::clipboardTextChanged);
        }
    }

    m_ukikuraTimer.setInterval(250);
    connect(&m_ukikuraTimer, &QTimer::timeout, this, &QsGlobal::pollUkikuraNotches);
    m_ukikuraTimer.start();
    pollUkikuraNotches();
}

void QsGlobal::pollUkikuraNotches() {
    // Detect live Ukikura Dynamic Island windows ("UkikuraNativeClass") per monitor
    // so SekaiPop's Top Bar cleanly splits its center pills around the Ukikura notch.
    QVariantMap next;
    HWND cur = nullptr;
    bool foundAny = false;
    QList<HWND> ukiHwnds;
    while ((cur = FindWindowExW(nullptr, cur, L"UkikuraNativeClass", nullptr)) != nullptr) {
        if (!IsWindowVisible(cur)) {
            continue;
        }
        RECT rc = {};
        if (!GetWindowRect(cur, &rc) || (rc.right - rc.left) <= 0 || (rc.bottom - rc.top) <= 0) {
            continue;
        }
        ukiHwnds.append(cur);
        foundAny = true;
    }

    if (foundAny) {
        // Refresh Ukikura user config (notchScale, featureMusic, minimalNotch) only when modified.
        QString localAppData = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
        if (localAppData.isEmpty()) {
            localAppData = QDir::homePath() + QStringLiteral("/AppData/Local");
        }
        const QString cfgPath = QDir(localAppData).filePath(QStringLiteral("Ukikura/data/config.json"));
        const QFileInfo fi(cfgPath);
        if (fi.exists()) {
            const qint64 mt = fi.lastModified().toMSecsSinceEpoch();
            if (mt != m_ukikuraCfgMtime) {
                m_ukikuraCfgMtime = mt;
                QFile f(cfgPath);
                if (f.open(QIODevice::ReadOnly)) {
                    const QJsonDocument d = QJsonDocument::fromJson(f.readAll());
                    if (d.isObject()) {
                        const QJsonObject o = d.object();
                        const int sc = o.value(QStringLiteral("notchScale")).toInt(0);
                        switch (sc) {
                        case 2: m_ukikuraScaleMul = 1.15; break;
                        case 3: m_ukikuraScaleMul = 1.30; break;
                        case 4: m_ukikuraScaleMul = 1.50; break;
                        default: m_ukikuraScaleMul = 1.0; break;
                        }
                        m_ukikuraFeatureMusic = o.value(QStringLiteral("featureMusic")).toBool(true);
                        m_ukikuraMinimal = o.value(QStringLiteral("minimalNotch")).toBool(false);
                    }
                }
            }
        }

        // Map each QsScreen by both GDI device name (MONITORINFOEXW::szDevice) and HMONITOR fallback
        QHash<QString, QString> devToScreen;
        QHash<HMONITOR, QString> monToScreen;
        for (QsScreen* s : std::as_const(m_screenObjs)) {
            if (!s || !s->qscreen()) {
                continue;
            }
            const QString sName = s->name();
            devToScreen.insert(sName.toLower(), sName);
            const QRect g = s->qscreen()->geometry();
            const qreal dpr = qMax<qreal>(0.5, s->qscreen()->devicePixelRatio());
            const POINT pt{
                qRound(g.x() + (g.width() * 0.5) * dpr),
                qRound(g.y() + (g.height() * 0.5) * dpr)
            };
            if (HMONITOR hm = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST)) {
                monToScreen.insert(hm, sName);
            }
        }

        for (HWND hwnd : ukiHwnds) {
            RECT rc = {};
            if (!GetWindowRect(hwnd, &rc)) {
                continue;
            }
            HMONITOR hm = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            if (!hm) {
                continue;
            }
            MONITORINFOEXW mi = {};
            mi.cbSize = sizeof(mi);
            if (!GetMonitorInfoW(hm, &mi) || rc.top > mi.rcMonitor.top + 80) {
                continue;
            }
            const QString devKey = QString::fromWCharArray(mi.szDevice).toLower();
            QString scrName = devToScreen.value(devKey);
            if (scrName.isEmpty()) {
                scrName = monToScreen.value(hm);
            }
            if (!scrName.isEmpty()) {
                next.insert(scrName, QVariantMap{
                    {QStringLiteral("scale"), m_ukikuraScaleMul},
                    {QStringLiteral("featureMusic"), m_ukikuraFeatureMusic},
                    {QStringLiteral("minimal"), m_ukikuraMinimal}
                });
            }
        }
    }

    if (next != m_ukikuraNotches) {
        m_ukikuraNotches = next;
        qInfo("[Ukikura] Active top-notch monitors: %s",
              m_ukikuraNotches.isEmpty() ? "none" : qPrintable(m_ukikuraNotches.keys().join(QStringLiteral(", "))));
        Q_EMIT ukikuraNotchesChanged();
    }
}

QsGlobal* QsGlobal::instance() {
    return s_instance;
}

void QsGlobal::rebuildScreens() {
    qDeleteAll(m_screenObjs);
    m_screenObjs.clear();
    m_screens.clear();

    const QList<QScreen*> rawScreens = QGuiApplication::screens();
    for (QScreen* s : rawScreens) {
        auto* wrapper = new QsScreen(s, this);
        m_screenObjs.append(wrapper);
        m_screens.append(QVariant::fromValue(static_cast<QObject*>(wrapper)));
    }
    Q_EMIT screensChanged();
}

QsScreen* QsGlobal::screenFor(QScreen* s) const {
    for (QsScreen* w : m_screenObjs) {
        if (w->qscreen() == s) {
            return w;
        }
    }
    return m_screenObjs.isEmpty() ? nullptr : m_screenObjs.first();
}

QsScreen* QsGlobal::screenByName(const QString& name) const {
    for (QsScreen* w : m_screenObjs) {
        if (w->name().compare(name, Qt::CaseInsensitive) == 0) {
            return w;
        }
    }
    return m_screenObjs.isEmpty() ? nullptr : m_screenObjs.first();
}

QsScreen* QsGlobal::primaryQsScreen() const {
    if (QScreen* p = QGuiApplication::primaryScreen()) {
        if (QsScreen* w = screenFor(p)) {
            return w;
        }
    }
    return m_screenObjs.isEmpty() ? nullptr : m_screenObjs.first();
}

void QsGlobal::setInhibitReloadPopup(bool v) {
    if (m_inhibitReloadPopup != v) {
        m_inhibitReloadPopup = v;
        Q_EMIT inhibitReloadPopupChanged();
    }
}

QString QsGlobal::clipboardText() const {
    if (QClipboard* cb = QGuiApplication::clipboard()) {
        return cb->text();
    }
    return QString();
}

void QsGlobal::setClipboardText(const QString& text) {
    if (QClipboard* cb = QGuiApplication::clipboard()) {
        cb->setText(text);
    }
}

QString QsGlobal::env(const QString& name) const {
    if (name == QStringLiteral("HOME")) {
        return m_homeDir;
    }
    if (name == QStringLiteral("USER") || name == QStringLiteral("USERNAME")) {
        return qEnvironmentVariable("USERNAME", QStringLiteral("user"));
    }
    if (name == QStringLiteral("SHELL")) {
        return QStringLiteral("pwsh");
    }
    if (name == QStringLiteral("XDG_CURRENT_DESKTOP") || name == QStringLiteral("DESKTOP_SESSION")) {
        return QStringLiteral("Win32 11");
    }
    if (name == QStringLiteral("XDG_STATE_HOME")) {
        return m_stateHome;
    }
    if (name == QStringLiteral("XDG_CONFIG_HOME")) {
        return m_configHome;
    }
    if (name == QStringLiteral("XDG_CACHE_HOME")) {
        return m_cacheHome;
    }
    if (name == QStringLiteral("XDG_RUNTIME_DIR")) {
        return m_runtimeDir;
    }
    return qEnvironmentVariable(name.toUtf8().constData());
}

void QsGlobal::execDetached(const QVariant& command) {
    QStringList args;
    if (command.typeId() == QMetaType::QStringList) {
        args = command.toStringList();
    } else if (command.canConvert<QVariantList>()) {
        for (const QVariant& v : command.toList()) {
            args.append(v.toString());
        }
    } else if (command.typeId() == QMetaType::QString) {
        args.append(command.toString());
    }
    if (!args.isEmpty()) {
        QsProcess::executeDetachedCommand(args);
    }
}

QString QsGlobal::iconPath(const QString& icon, const QVariant& fallback) const {
    auto resolveSingle = [this](const QString& rawName) -> QString {
        const QString trimmed = rawName.trimmed();
        if (trimmed.isEmpty()) {
            return QString();
        }
        if (trimmed.startsWith(QStringLiteral("image://")) || trimmed.startsWith(QStringLiteral("file://"))) {
            return trimmed;
        }
        const QString norm = normalizeWinPath(trimmed);
        if (QFileInfo::exists(norm)) {
            return QUrl::fromLocalFile(norm).toString();
        }
        const QString lower = trimmed.toLower();
        static const QHash<QString, QString> builtinIcons = {
            {QStringLiteral("folder"), QStringLiteral("folder.svg")},
            {QStringLiteral("inode-directory"), QStringLiteral("folder.svg")},
            {QStringLiteral("user-home"), QStringLiteral("home-2.svg")},
            {QStringLiteral("user-desktop"), QStringLiteral("monitor.svg")},
            {QStringLiteral("folder-download"), QStringLiteral("download.svg")},
            {QStringLiteral("folder-documents"), QStringLiteral("document-text.svg")},
            {QStringLiteral("folder-pictures"), QStringLiteral("gallery.svg")},
            {QStringLiteral("folder-music"), QStringLiteral("music-note.svg")},
            {QStringLiteral("folder-videos"), QStringLiteral("video-frame.svg")},
            {QStringLiteral("text-x-generic"), QStringLiteral("document-text.svg")},
            {QStringLiteral("text-plain"), QStringLiteral("document-text.svg")},
            {QStringLiteral("application-pdf"), QStringLiteral("document-text.svg")},
            {QStringLiteral("image-x-generic"), QStringLiteral("gallery.svg")},
            {QStringLiteral("audio-x-generic"), QStringLiteral("music-note.svg")},
            {QStringLiteral("video-x-generic"), QStringLiteral("video-frame.svg")},
            {QStringLiteral("package-x-generic"), QStringLiteral("box.svg")},
            {QStringLiteral("application-x-executable"), QStringLiteral("window-frame.svg")},
            {QStringLiteral("utilities-terminal"), QStringLiteral("programming.svg")},
            {QStringLiteral("system-file-manager"), QStringLiteral("folder.svg")},
            {QStringLiteral("web-browser"), QStringLiteral("global.svg")},
            {QStringLiteral("preferences-system"), QStringLiteral("settings.svg")}
        };
        if (builtinIcons.contains(lower)) {
            const QString svgPath = normalizeWinPath(m_shellDir + QStringLiteral("/assets/icons/outline/") + builtinIcons.value(lower));
            if (QFileInfo::exists(svgPath)) {
                return QUrl::fromLocalFile(svgPath).toString();
            }
        }
        // Check if assets/icons/outline/<name>.svg exists directly
        QString directSvg = normalizeWinPath(m_shellDir + QStringLiteral("/assets/icons/outline/") + lower + QStringLiteral(".svg"));
        if (QFileInfo::exists(directSvg)) {
            return QUrl::fromLocalFile(directSvg).toString();
        }
        // Pass 1: Exact match against live open windows (most accurate HWND/EXE icon)
        if (m_windowTracker) {
            for (const QVariant& wv : m_windowTracker->openWindows()) {
                const QVariantMap wm = wv.toMap();
                const QString appName = wm.value(QStringLiteral("app")).toString().toLower();
                const QString exeStem = QFileInfo(wm.value(QStringLiteral("exe")).toString()).completeBaseName().toLower();
                const QString iconUrl = wm.value(QStringLiteral("icon")).toString();
                if (!iconUrl.isEmpty() && (appName == lower || (!exeStem.isEmpty() && exeStem == lower))) {
                    return iconUrl;
                }
            }
        }
        // Pass 2: Exact match against Start Menu / UWP AppSource
        if (m_appSource) {
            const QVariantList ordered = m_appSource->ordered();
            for (const QVariant& v : ordered) {
                const QVariantMap row = v.toMap();
                const QVariantMap app = row.value(QStringLiteral("app")).toMap();
                const QString id = row.value(QStringLiteral("key")).toString().toLower();
                const QString name = row.value(QStringLiteral("name")).toString().toLower();
                const QString exeStem = QFileInfo(app.value(QStringLiteral("target")).toString()).completeBaseName().toLower();
                const QString iconUrl = row.value(QStringLiteral("icon")).toString();
                if (!iconUrl.isEmpty() && (iconUrl == trimmed || id == lower || name == lower || (!exeStem.isEmpty() && exeStem == lower))) {
                    return iconUrl;
                }
            }
            // Pass 3: Substring match against AppSource
            for (const QVariant& v : ordered) {
                const QVariantMap row = v.toMap();
                const QString id = row.value(QStringLiteral("key")).toString().toLower();
                const QString name = row.value(QStringLiteral("name")).toString().toLower();
                const QString iconUrl = row.value(QStringLiteral("icon")).toString();
                if (!iconUrl.isEmpty() && (id.contains(lower) || name.contains(lower))) {
                    return iconUrl;
                }
            }
        }
        // Pass 4: Substring match against live open windows
        if (m_windowTracker) {
            for (const QVariant& wv : m_windowTracker->openWindows()) {
                const QVariantMap wm = wv.toMap();
                const QString appName = wm.value(QStringLiteral("app")).toString().toLower();
                const QString iconUrl = wm.value(QStringLiteral("icon")).toString();
                if (!iconUrl.isEmpty() && !appName.isEmpty() && (appName.contains(lower) || lower.contains(appName))) {
                    return iconUrl;
                }
            }
        }
        return QString();
    };

    const QString primary = resolveSingle(icon);
    if (!primary.isEmpty()) {
        return primary;
    }
    if (fallback.isValid() && fallback.typeId() == QMetaType::Bool && fallback.toBool()) {
        return QString();
    }
    if (fallback.isValid() && fallback.typeId() == QMetaType::QString) {
        const QString fbResolved = resolveSingle(fallback.toString());
        if (!fbResolved.isEmpty()) {
            return fbResolved;
        }
    }
    if (!icon.trimmed().isEmpty()) {
        return Win32::WinIconProvider::fallbackIconUrl(icon.trimmed());
    }
    const QString genericFallback = normalizeWinPath(m_shellDir + QStringLiteral("/assets/icons/outline/window-frame.svg"));
    if (QFileInfo::exists(genericFallback)) {
        return QUrl::fromLocalFile(genericFallback).toString();
    }
    return Win32::WinIconProvider::fallbackIconUrl(QStringLiteral("App"));
}

void QsGlobal::reload(bool hard) {
    Q_UNUSED(hard);
    Q_EMIT reloadCompleted();
}

QString QsGlobal::shellPath(const QString& rel) const {
    return m_shellDir + QLatin1Char('/') + rel;
}

QString QsGlobal::cachePath(const QString& rel) const {
    return m_cacheHome + QStringLiteral("/sekaipop/") + rel;
}

namespace {
QString sanitizeCacheToken(const QString& raw) {
    QString out;
    out.reserve(raw.size());
    for (const QChar& ch : raw) {
        if (ch.isLetterOrNumber() || ch == QLatin1Char('_') || ch == QLatin1Char('-')) {
            out.append(ch);
        } else {
            out.append(QLatin1Char('_'));
        }
    }
    return out.isEmpty() ? QStringLiteral("default") : out;
}
} // namespace

QString QsGlobal::editPreviewFilePath(const QString& themeKey, const QString& widgetId) const {
    const QString dirPath = normalizeWinPath(m_cacheHome) + QStringLiteral("/sekaipop/edit-previews");
    QDir().mkpath(dirPath);
    return dirPath + QLatin1Char('/') + sanitizeCacheToken(themeKey) + QLatin1Char('_')
        + sanitizeCacheToken(widgetId) + QStringLiteral(".png");
}

void QsGlobal::saveEditPreviewMeta(const QString& themeKey, const QString& widgetId, qreal width, qreal height) const {
    const QString dirPath = normalizeWinPath(m_cacheHome) + QStringLiteral("/sekaipop/edit-previews");
    QDir().mkpath(dirPath);
    const QString idxPath = dirPath + QStringLiteral("/index.json");
    QJsonObject root;
    {
        QFile rf(idxPath);
        if (rf.open(QIODevice::ReadOnly)) {
            const QJsonDocument d = QJsonDocument::fromJson(rf.readAll());
            if (d.isObject()) {
                root = d.object();
            }
        }
    }
    const QString safeTheme = sanitizeCacheToken(themeKey);
    QJsonObject themeObj = root.value(safeTheme).toObject();
    themeObj.insert(widgetId, QJsonObject{
        {QStringLiteral("w"), width},
        {QStringLiteral("h"), height}
    });
    root.insert(safeTheme, themeObj);
    QFile wf(idxPath);
    if (wf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        wf.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    }
}

QVariantMap QsGlobal::loadEditPreviewCache(const QString& themeKey) const {
    QVariantMap urls;
    QVariantMap sizes;
    const QString dirPath = normalizeWinPath(m_cacheHome) + QStringLiteral("/sekaipop/edit-previews");
    const QString idxPath = dirPath + QStringLiteral("/index.json");
    QFile rf(idxPath);
    if (rf.open(QIODevice::ReadOnly)) {
        const QJsonDocument d = QJsonDocument::fromJson(rf.readAll());
        if (d.isObject()) {
            const QString safeTheme = sanitizeCacheToken(themeKey);
            const QJsonObject themeObj = d.object().value(safeTheme).toObject();
            for (auto it = themeObj.constBegin(); it != themeObj.constEnd(); ++it) {
                const QString wid = it.key();
                const QJsonObject meta = it.value().toObject();
                const qreal w = meta.value(QStringLiteral("w")).toDouble();
                const qreal h = meta.value(QStringLiteral("h")).toDouble();
                const QString pngPath = dirPath + QLatin1Char('/') + safeTheme + QLatin1Char('_')
                    + sanitizeCacheToken(wid) + QStringLiteral(".png");
                if (w > 1.0 && h > 1.0 && QFileInfo::exists(pngPath) && QFileInfo(pngPath).size() > 64) {
                    urls.insert(wid, QUrl::fromLocalFile(pngPath).toString());
                    sizes.insert(wid, QVariantMap{{QStringLiteral("w"), w}, {QStringLiteral("h"), h}});
                }
            }
        }
    }
    return QVariantMap{
        {QStringLiteral("urls"), urls},
        {QStringLiteral("sizes"), sizes}
    };
}

// ============================================================================
// QsWindowAttached
// ============================================================================

QsWindowAttached::QsWindowAttached(QObject* object)
    : QObject(object) {
    attachToHierarchy();
}

void QsWindowAttached::attachToHierarchy() {
    QObject* cur = parent();
    while (cur) {
        if (auto* win = qobject_cast<QQuickWindow*>(cur)) {
            m_directWindow = win;
            return;
        }
        if (auto* item = qobject_cast<QQuickItem*>(cur)) {
            m_item = item;
            connect(item, &QQuickItem::windowChanged, this, [this](QQuickWindow*) {
                Q_EMIT windowChanged();
            });
            return;
        }
        cur = cur->parent();
    }
}

QQuickWindow* QsWindowAttached::window() const {
    if (m_directWindow) {
        return m_directWindow;
    }
    if (m_item) {
        return m_item->window();
    }
    QObject* cur = parent();
    while (cur) {
        if (auto* win = qobject_cast<QQuickWindow*>(cur)) {
            return win;
        }
        if (auto* item = qobject_cast<QQuickItem*>(cur)) {
            return item->window();
        }
        cur = cur->parent();
    }
    return nullptr;
}

// ============================================================================
// QsShellRoot & QsScope
// ============================================================================

QsShellRoot::QsShellRoot(QObject* parent)
    : QObject(parent)
    , m_settings(new QsShellSettings(this)) {}

QQmlListProperty<QObject> QsShellRoot::data() {
    return QQmlListProperty<QObject>(this, &m_data);
}

QQmlListProperty<QObject> QsScope::data() {
    return QQmlListProperty<QObject>(this, &m_data);
}

// ============================================================================
// QsVariants
// ============================================================================

namespace {

QVariant normalizeVariantValue(const QVariant& raw) {
    QVariant v = raw;
    if (v.typeId() == qMetaTypeId<QJSValue>()) {
        v = v.value<QJSValue>().toVariant();
    }
    if (v.typeId() == QMetaType::QVariantList) {
        const QVariantList src = v.toList();
        QVariantList out;
        out.reserve(src.size());
        for (const QVariant& elem : src) {
            out.append(normalizeVariantValue(elem));
        }
        return out;
    }
    if (v.typeId() == QMetaType::QVariantMap) {
        const QVariantMap src = v.toMap();
        QVariantMap out;
        for (auto it = src.cbegin(); it != src.cend(); ++it) {
            out.insert(it.key(), normalizeVariantValue(it.value()));
        }
        return out;
    }
    return v;
}

bool variantDeepEqual(const QVariant& a, const QVariant& b) {
    if (!a.isValid() && !b.isValid()) {
        return true;
    }
    if (a.isValid() != b.isValid()) {
        return false;
    }
    const bool aObj = (a.metaType().flags() & QMetaType::PointerToQObject) != 0;
    const bool bObj = (b.metaType().flags() & QMetaType::PointerToQObject) != 0;
    if (aObj || bObj) {
        if (!aObj || !bObj) {
            return false;
        }
        return a.value<QObject*>() == b.value<QObject*>();
    }
    if (a.typeId() == QMetaType::QVariantMap && b.typeId() == QMetaType::QVariantMap) {
        const QVariantMap ma = a.toMap();
        const QVariantMap mb = b.toMap();
        if (ma.size() != mb.size()) {
            return false;
        }
        for (auto it = ma.cbegin(); it != ma.cend(); ++it) {
            auto jt = mb.constFind(it.key());
            if (jt == mb.cend() || !variantDeepEqual(it.value(), jt.value())) {
                return false;
            }
        }
        return true;
    }
    if (a.typeId() == QMetaType::QVariantList && b.typeId() == QMetaType::QVariantList) {
        const QVariantList la = a.toList();
        const QVariantList lb = b.toList();
        if (la.size() != lb.size()) {
            return false;
        }
        for (int i = 0; i < la.size(); ++i) {
            if (!variantDeepEqual(la.at(i), lb.at(i))) {
                return false;
            }
        }
        return true;
    }
    return a == b;
}

QVariantList extractVariantItems(const QVariant& model) {
    const QVariant norm = normalizeVariantValue(model);
    QVariantList items;
    if (norm.typeId() == QMetaType::QVariantList) {
        items = norm.toList();
    } else if (norm.typeId() == QMetaType::QStringList) {
        for (const QString& s : norm.toStringList()) {
            items.append(s);
        }
    } else if (norm.canConvert<QVariantList>()) {
        const QVariantList rawList = norm.toList();
        items.reserve(rawList.size());
        for (const QVariant& elem : rawList) {
            items.append(normalizeVariantValue(elem));
        }
    } else if (norm.typeId() == QMetaType::Int) {
        const int n = norm.toInt();
        items.reserve(qMax(0, n));
        for (int i = 0; i < n; ++i) {
            items.append(i);
        }
    }
    return items;
}

} // namespace

QsVariants::~QsVariants() {
    qDeleteAll(m_created);
    m_created.clear();
    m_items.clear();
}

void QsVariants::setModel(const QVariant& m) {
    const QVariantList nextItems = extractVariantItems(m);
    if (m_complete && nextItems.size() == m_items.size()) {
        bool same = true;
        for (int i = 0; i < nextItems.size(); ++i) {
            if (!variantDeepEqual(m_items.at(i), nextItems.at(i))) {
                same = false;
                break;
            }
        }
        if (same) {
            return;
        }
    }
    m_model = m;
    Q_EMIT modelChanged();
    if (m_complete) {
        rebuild(false);
    }
}

void QsVariants::setDelegate(QQmlComponent* d) {
    if (m_delegate == d) {
        return;
    }
    m_delegate = d;
    Q_EMIT delegateChanged();
    if (m_complete) {
        rebuild(true);
    }
}

void QsVariants::componentComplete() {
    m_complete = true;
    rebuild(true);
}

void QsVariants::rebuild(bool forceRecreate) {
    if (!m_delegate) {
        for (QObject* obj : std::as_const(m_created)) {
            if (obj) {
                obj->deleteLater();
            }
        }
        m_created.clear();
        m_items.clear();
        m_instances.clear();
        Q_EMIT instancesChanged();
        return;
    }

    const QVariantList items = extractVariantItems(m_model);

    if (!forceRecreate && items.size() == m_items.size() && m_created.size() == m_items.size()) {
        bool allSame = true;
        for (int i = 0; i < items.size(); ++i) {
            if (!variantDeepEqual(m_items.at(i), items.at(i))) {
                allSame = false;
                break;
            }
        }
        if (allSame) {
            return;
        }
    }

    const QVariantList oldItems = m_items;
    const QList<QObject*> oldCreated = m_created;
    QList<bool> reused(oldCreated.size(), false);

    m_items.clear();
    m_created.clear();
    m_instances.clear();
    m_items.reserve(items.size());
    m_created.reserve(items.size());
    m_instances.reserve(items.size());

    QQmlContext* baseCtx = m_delegate ? m_delegate->creationContext() : nullptr;
    if (!baseCtx) {
        baseCtx = qmlContext(this);
    }

    for (int i = 0; i < items.size(); ++i) {
        const QVariant& item = items.at(i);

        QObject* existingObj = nullptr;
        if (!forceRecreate) {
            for (int j = 0; j < oldCreated.size(); ++j) {
                if (!reused.at(j) && oldCreated.at(j) && j < oldItems.size() &&
                    variantDeepEqual(oldItems.at(j), item)) {
                    reused[j] = true;
                    existingObj = oldCreated.at(j);
                    if (QQmlContext* ctx = qmlContext(existingObj)) {
                        ctx->setContextProperty(QStringLiteral("index"), i);
                    }
                    break;
                }
            }
        }

        if (existingObj) {
            m_items.append(item);
            m_created.append(existingObj);
            m_instances.append(QVariant::fromValue(existingObj));
            continue;
        }

        auto* childCtx = new QQmlContext(baseCtx, this);
        childCtx->setContextProperty(QStringLiteral("modelData"), item);
        childCtx->setContextProperty(QStringLiteral("index"), i);

        QObject* obj = m_delegate->beginCreate(childCtx);
        if (!obj) {
            delete childCtx;
            continue;
        }
        childCtx->setParent(obj);
        if (!obj->parent()) {
            obj->setParent(this);
        }

        QVariantMap initProps;
        if (obj->metaObject()->indexOfProperty("modelData") >= 0) {
            initProps.insert(QStringLiteral("modelData"), item);
        }
        if (obj->metaObject()->indexOfProperty("index") >= 0) {
            initProps.insert(QStringLiteral("index"), i);
        }
        if (item.typeId() == QMetaType::QVariantMap) {
            const QVariantMap map = item.toMap();
            for (auto it = map.begin(); it != map.end(); ++it) {
                if (obj->metaObject()->indexOfProperty(it.key().toUtf8().constData()) >= 0 &&
                    it.key() != QStringLiteral("position")) {
                    initProps.insert(it.key(), it.value());
                }
            }
        }
        if (!initProps.isEmpty()) {
            m_delegate->setInitialProperties(obj, initProps);
        }
        m_delegate->completeCreate();

        m_items.append(item);
        m_created.append(obj);
        m_instances.append(QVariant::fromValue(obj));
    }

    for (int j = 0; j < oldCreated.size(); ++j) {
        if (!reused.at(j) && oldCreated.at(j)) {
            oldCreated.at(j)->deleteLater();
        }
    }

    Q_EMIT instancesChanged();
}

// ============================================================================
// QsLazyLoader
// ============================================================================

QsLazyLoader::~QsLazyLoader() {
    if (m_item) {
        delete m_item;
        m_item = nullptr;
    }
}

void QsLazyLoader::setActive(bool a) {
    if (m_active == a) {
        return;
    }
    m_active = a;
    Q_EMIT activeChanged();
    if (m_complete) {
        updateInstance();
    }
}

void QsLazyLoader::setSource(const QUrl& url) {
    if (m_source == url) {
        return;
    }
    m_source = url;
    if (m_item) {
        QObject* old = m_item;
        m_item = nullptr;
        Q_EMIT itemChanged();
        old->deleteLater();
    }
    if (m_ownedComponent) {
        m_ownedComponent->deleteLater();
        m_ownedComponent = nullptr;
    }
    Q_EMIT sourceChanged();
    if (m_complete) {
        updateInstance();
    }
}

void QsLazyLoader::setInitialProperties(const QVariantMap& props) {
    if (m_initialProperties == props) {
        return;
    }
    m_initialProperties = props;
    Q_EMIT initialPropertiesChanged();
}

void QsLazyLoader::setSourceComponent(QQmlComponent* c) {
    if (m_component == c) {
        return;
    }
    m_component = c;
    Q_EMIT sourceComponentChanged();
    if (m_complete) {
        updateInstance();
    }
}

void QsLazyLoader::componentComplete() {
    m_complete = true;
    updateInstance();
}

void QsLazyLoader::updateInstance() {
    if (!m_active || (!m_component && m_source.isEmpty())) {
        if (m_item) {
            QObject* old = m_item;
            m_item = nullptr;
            Q_EMIT itemChanged();
            old->deleteLater();
        }
        return;
    }
    if (m_item) {
        return;
    }

    QQmlComponent* comp = m_component;
    if (!comp && !m_source.isEmpty()) {
        if (!m_ownedComponent) {
            QQmlContext* ctx = qmlContext(this);
            QQmlEngine* eng = ctx ? ctx->engine() : qmlEngine(this);
            if (!eng) {
                return;
            }
            const QUrl resolved = (ctx && m_source.isRelative()) ? ctx->resolvedUrl(m_source) : m_source;
            m_ownedComponent = new QQmlComponent(eng, resolved, QQmlComponent::PreferSynchronous, this);
            if (m_ownedComponent->isError()) {
                for (const QQmlError& err : m_ownedComponent->errors()) {
                    qWarning("[QsLazyLoader] Compile error (%s): %s",
                             qPrintable(resolved.toString()), qPrintable(err.toString()));
                }
                m_ownedComponent->deleteLater();
                m_ownedComponent = nullptr;
                return;
            }
        }
        comp = m_ownedComponent;
    }
    if (!comp) {
        return;
    }

    QQmlContext* ctx = comp->creationContext();
    if (!ctx) {
        ctx = qmlContext(this);
    }

    QObject* created = nullptr;
    if (!m_initialProperties.isEmpty()) {
        created = comp->beginCreate(ctx);
        if (created) {
            if (!created->parent()) {
                created->setParent(this);
            }
            comp->setInitialProperties(created, m_initialProperties);
            comp->completeCreate();
        }
    } else {
        created = comp->create(ctx);
        if (created && !created->parent()) {
            created->setParent(this);
        }
    }

    if (comp->isError()) {
        for (const QQmlError& err : comp->errors()) {
            qWarning("[QsLazyLoader] Create error (%s): %s",
                     qPrintable(comp->url().toString()), qPrintable(err.toString()));
        }
    }

    if (created) {
        m_item = created;
        Q_EMIT itemChanged();
    }
}

// ============================================================================
// QsSystemClock
// ============================================================================

QsSystemClock::QsSystemClock(QObject* parent)
    : QObject(parent)
    , m_date(QDateTime::currentDateTime()) {
    connect(&m_timer, &QTimer::timeout, this, &QsSystemClock::tick);
    updateTimer();
}

void QsSystemClock::setEnabled(bool e) {
    if (m_enabled == e) {
        return;
    }
    m_enabled = e;
    Q_EMIT enabledChanged();
    updateTimer();
}

void QsSystemClock::setPrecision(int p) {
    if (m_precision == p) {
        return;
    }
    m_precision = p;
    Q_EMIT precisionChanged();
    updateTimer();
}

void QsSystemClock::tick() {
    m_date = QDateTime::currentDateTime();
    Q_EMIT dateChanged();
}

void QsSystemClock::updateTimer() {
    if (!m_enabled) {
        m_timer.stop();
        return;
    }
    m_date = QDateTime::currentDateTime();
    Q_EMIT dateChanged();
    m_timer.setInterval(1000);
    m_timer.start();
}

// ============================================================================
// QsIpcHandler
// ============================================================================

static QList<QsIpcHandler*>& ipcHandlers() {
    static QList<QsIpcHandler*> s_list;
    return s_list;
}

QsIpcHandler::QsIpcHandler(QObject* parent)
    : QObject(parent) {
    ipcHandlers().append(this);
}

QsIpcHandler::~QsIpcHandler() {
    ipcHandlers().removeAll(this);
}

void QsIpcHandler::setTarget(const QString& t) {
    if (m_target != t) {
        m_target = t;
        Q_EMIT targetChanged();
    }
}

void QsIpcHandler::setEnabled(bool e) {
    if (m_enabled != e) {
        m_enabled = e;
        Q_EMIT enabledChanged();
    }
}

QString QsIpcHandler::dispatchCall(const QString& target, const QString& func, const QStringList& args) {
    for (QsIpcHandler* h : std::as_const(ipcHandlers())) {
        if (!h || !h->enabled() || h->target().compare(target, Qt::CaseInsensitive) != 0) {
            continue;
        }
        const QMetaObject* mo = h->metaObject();
        for (int i = 0; i < mo->methodCount(); ++i) {
            const QMetaMethod mm = mo->method(i);
            if (QString::fromLatin1(mm.name()) != func) {
                continue;
            }
            const int paramCount = mm.parameterCount();
            QVariant rawArgs[4];
            QVariant typedArgs[4];
            auto prepArg = [&](int idx) -> QGenericArgument {
                if (idx >= paramCount) {
                    return QGenericArgument();
                }
                const QString s = (idx < args.size()) ? args.at(idx) : QString();
                QVariant val(s);
                if (s == QStringLiteral("true")) {
                    val = QVariant(true);
                } else if (s == QStringLiteral("false")) {
                    val = QVariant(false);
                }
                const int pType = mm.parameterType(idx);
                if (pType == QMetaType::QVariant) {
                    rawArgs[idx] = val;
                    return QGenericArgument("QVariant", &rawArgs[idx]);
                }
                typedArgs[idx] = val;
                if (pType != QMetaType::UnknownType && typedArgs[idx].typeId() != pType) {
                    typedArgs[idx].convert(QMetaType(pType));
                }
                return QGenericArgument(mm.parameterTypeName(idx).constData(), typedArgs[idx].constData());
            };

            const QGenericArgument g0 = prepArg(0);
            const QGenericArgument g1 = prepArg(1);
            const QGenericArgument g2 = prepArg(2);
            const QGenericArgument g3 = prepArg(3);

            if (paramCount == 0) {
                mm.invoke(h, Qt::DirectConnection);
            } else if (paramCount == 1) {
                mm.invoke(h, Qt::DirectConnection, g0);
            } else if (paramCount == 2) {
                mm.invoke(h, Qt::DirectConnection, g0, g1);
            } else if (paramCount == 3) {
                mm.invoke(h, Qt::DirectConnection, g0, g1, g2);
            } else if (paramCount >= 4) {
                mm.invoke(h, Qt::DirectConnection, g0, g1, g2, g3);
            }
            return QStringLiteral("ok");
        }
    }
    return QString();
}

// ============================================================================
// QsScriptModel
// ============================================================================

void QsScriptModel::setValues(const QVariantList& v) {
    if (m_values == v) {
        return;
    }
    beginResetModel();
    m_values = v;
    endResetModel();
    Q_EMIT valuesChanged();
}

void QsScriptModel::setObjectProp(const QString& p) {
    if (m_objectProp != p) {
        m_objectProp = p;
        Q_EMIT objectPropChanged();
    }
}

int QsScriptModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(m_values.size());
}

QVariant QsScriptModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_values.size()) {
        return QVariant();
    }
    if (role == Qt::UserRole + 1 || role == Qt::DisplayRole) {
        return m_values.at(index.row());
    }
    return QVariant();
}

QHash<int, QByteArray> QsScriptModel::roleNames() const {
    return {
        {Qt::UserRole + 1, QByteArrayLiteral("modelData")}
    };
}

// ============================================================================
// QsColorQuantizer
// ============================================================================

QsColorQuantizer::QsColorQuantizer(QObject* parent)
    : QObject(parent) {}

void QsColorQuantizer::setSource(const QUrl& u) {
    if (m_source == u) {
        return;
    }
    m_source = u;
    Q_EMIT sourceChanged();
    quantize();
}

void QsColorQuantizer::setDepth(int d) {
    if (m_depth == d) {
        return;
    }
    m_depth = qBound(1, d, 5);
    Q_EMIT depthChanged();
    quantize();
}

void QsColorQuantizer::setRescaleSize(int s) {
    if (m_rescaleSize == s) {
        return;
    }
    m_rescaleSize = qBound(8, s, 256);
    Q_EMIT rescaleSizeChanged();
    quantize();
}

void QsColorQuantizer::quantize() {
    if (m_source.isEmpty()) {
        if (!m_colors.isEmpty()) {
            m_colors.clear();
            Q_EMIT colorsChanged();
        }
        return;
    }

    QString localPath = normalizeWinPath(m_source.toString());
    if (localPath.isEmpty() || !QFileInfo::exists(localPath)) {
        localPath = normalizeWinPath(m_source.toLocalFile());
    }

    QImage img(localPath);
    if (img.isNull()) {
        return;
    }

    const int sz = qMax(8, m_rescaleSize);
    img = img.scaled(sz, sz, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
             .convertToFormat(QImage::Format_RGB32);

    struct Rgb {
        int r, g, b;
    };
    std::vector<Rgb> pixels;
    pixels.reserve(sz * sz);
    for (int y = 0; y < img.height(); ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            pixels.push_back({qRed(line[x]), qGreen(line[x]), qBlue(line[x])});
        }
    }

    std::vector<std::vector<Rgb>> buckets;
    buckets.push_back(std::move(pixels));

    for (int d = 0; d < m_depth; ++d) {
        std::vector<std::vector<Rgb>> next;
        next.reserve(buckets.size() * 2);
        for (auto& b : buckets) {
            if (b.size() < 2) {
                next.push_back(std::move(b));
                continue;
            }
            int minR = 255, maxR = 0, minG = 255, maxG = 0, minB = 255, maxB = 0;
            for (const auto& p : b) {
                minR = std::min(minR, p.r); maxR = std::max(maxR, p.r);
                minG = std::min(minG, p.g); maxG = std::max(maxG, p.g);
                minB = std::min(minB, p.b); maxB = std::max(maxB, p.b);
            }
            const int rSpan = maxR - minR;
            const int gSpan = maxG - minG;
            const int bSpan = maxB - minB;
            if (rSpan >= gSpan && rSpan >= bSpan) {
                std::sort(b.begin(), b.end(), [](const Rgb& a, const Rgb& c) { return a.r < c.r; });
            } else if (gSpan >= rSpan && gSpan >= bSpan) {
                std::sort(b.begin(), b.end(), [](const Rgb& a, const Rgb& c) { return a.g < c.g; });
            } else {
                std::sort(b.begin(), b.end(), [](const Rgb& a, const Rgb& c) { return a.b < c.b; });
            }
            const size_t mid = b.size() / 2;
            next.emplace_back(b.begin(), b.begin() + mid);
            next.emplace_back(b.begin() + mid, b.end());
        }
        buckets = std::move(next);
    }

    QVariantList result;
    for (const auto& b : buckets) {
        if (b.empty()) {
            continue;
        }
        qint64 sr = 0, sg = 0, sb = 0;
        for (const auto& p : b) {
            sr += p.r;
            sg += p.g;
            sb += p.b;
        }
        const int n = static_cast<int>(b.size());
        result.append(QColor(static_cast<int>(sr / n), static_cast<int>(sg / n), static_cast<int>(sb / n)));
    }

    m_colors = result;
    Q_EMIT colorsChanged();
}

// ============================================================================
// QsObjectModel
// ============================================================================

void QsObjectModel::setValues(const QVariantList& v) {
    if (m_values == v) {
        return;
    }
    beginResetModel();
    m_values = v;
    endResetModel();
    Q_EMIT valuesChanged();
}

int QsObjectModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(m_values.size());
}

QVariant QsObjectModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_values.size()) {
        return QVariant();
    }
    if (role == Qt::DisplayRole || role == Qt::UserRole) {
        return m_values.at(index.row());
    }
    return QVariant();
}

QHash<int, QByteArray> QsObjectModel::roleNames() const {
    return {
        {Qt::DisplayRole, QByteArrayLiteral("display")},
        {Qt::UserRole, QByteArrayLiteral("modelData")}
    };
}

} // namespace SekaiPop::Quickshell
