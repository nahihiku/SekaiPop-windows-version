#include "QsServices.h"
#include "QsWindowing.h"
#include "../services/AppSource.h"
#include "../services/BatterySource.h"
#include "../services/MediaSource.h"
#include "../services/QuickSource.h"
#include "../win32/Win32Utils.h"
#include "../win32/WinIconProvider.h"
#include "../win32/WindowTracker.h"

#include <QCursor>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <cmath>
#include <windows.h>

namespace SekaiPop::Quickshell {

// ============================================================================
// QsUrlInterceptor
// ============================================================================

QUrl QsUrlInterceptor::intercept(const QUrl& url, DataType type) {
    Q_UNUSED(type);
    if (!url.isValid()) {
        return url;
    }

    const QString raw = url.toString();
    if (!raw.startsWith(QStringLiteral("file://"), Qt::CaseInsensitive)) {
        return url;
    }

    QString local = normalizeWinPath(raw);
    if (local.isEmpty()) {
        return url;
    }

    // Redirect any shader .qsb lookup to our Qt 6.8.3 compiled shaders folder
    if (local.endsWith(QStringLiteral(".qsb"), Qt::CaseInsensitive)) {
        const int idx = local.lastIndexOf(QStringLiteral("/shaders/"), -1, Qt::CaseInsensitive);
        if (idx >= 0) {
            const QString rel = local.mid(idx + 9); // after "/shaders/"
            const QString candidate = QDir(m_shadersDir).filePath(rel);
            if (QFileInfo::exists(candidate)) {
                return QUrl::fromLocalFile(candidate);
            }
        }
    }

    // Redirect missing Solar icon aliases cleanly
    if (!QFileInfo::exists(local) && local.contains(QStringLiteral("/assets/icons/"), Qt::CaseInsensitive)) {
        static const QHash<QString, QString> iconAliases = {
            {QStringLiteral("/air.svg"), QStringLiteral("/wind.svg")},
            {QStringLiteral("/sunny.svg"), QStringLiteral("/sun.svg")},
            {QStringLiteral("/clear-day.svg"), QStringLiteral("/sun.svg")},
            {QStringLiteral("/partly-cloudy-day.svg"), QStringLiteral("/cloud-sun-2.svg")},
            {QStringLiteral("/thunderstorm.svg"), QStringLiteral("/cloud-storm.svg")},
            {QStringLiteral("/lock-clock.svg"), QStringLiteral("/lock.svg")},
            {QStringLiteral("/fingerprint.svg"), QStringLiteral("/shield-keyhole.svg")},
            {QStringLiteral("/battery-alert.svg"), QStringLiteral("/battery-low.svg")},
            {QStringLiteral("/comments-disabled.svg"), QStringLiteral("/chat-line.svg")},
            {QStringLiteral("/auto-mode.svg"), QStringLiteral("/refresh-circle.svg")},
            {QStringLiteral("/123.svg"), QStringLiteral("/hashtag.svg")},
            {QStringLiteral("/campaign.svg"), QStringLiteral("/bell.svg")},
            {QStringLiteral("/gpp-maybe.svg"), QStringLiteral("/shield-warning.svg")},
            {QStringLiteral("/verified.svg"), QStringLiteral("/verified-check.svg")},
            {QStringLiteral("/storefront.svg"), QStringLiteral("/shop.svg")}
        };
        for (auto it = iconAliases.constBegin(); it != iconAliases.constEnd(); ++it) {
            if (local.endsWith(it.key(), Qt::CaseInsensitive)) {
                const QString alt = local.left(local.size() - it.key().size()) + it.value();
                if (QFileInfo::exists(alt)) {
                    return QUrl::fromLocalFile(alt);
                }
            }
        }
    }

    return QUrl::fromLocalFile(local);
}

// ============================================================================
// QsDesktopEntry & QsDesktopEntries
// ============================================================================

QsDesktopEntry::QsDesktopEntry(
    const QString& id,
    const QString& name,
    const QString& genericName,
    const QString& icon,
    const QString& comment,
    const QStringList& keywords,
    const QVariantMap& appMap,
    Services::AppSource* appSource,
    QObject* parent
)
    : QObject(parent)
    , m_id(id)
    , m_name(name)
    , m_genericName(genericName)
    , m_icon(icon)
    , m_comment(comment)
    , m_keywords(keywords)
    , m_appMap(appMap)
    , m_appSource(appSource) {}

void QsDesktopEntry::execute() {
    if (m_appSource && !m_appMap.isEmpty()) {
        m_appSource->launch(m_appMap);
        return;
    }
    const QString target = m_appMap.value(QStringLiteral("target")).toString();
    if (!target.isEmpty()) {
        Win32::launchShellTarget(target);
    }
}

QsDesktopEntries::QsDesktopEntries(Services::AppSource* appSource, Win32::WindowTracker* tracker, QObject* parent)
    : QObject(parent)
    , m_appSource(appSource)
    , m_windowTracker(tracker)
    , m_applications(new QsObjectModel(this)) {
    rebuild();
    if (m_appSource) {
        connect(m_appSource, &Services::AppSource::entriesChanged, this, &QsDesktopEntries::rebuild);
    }
}

void QsDesktopEntries::rebuild() {
    const QList<QsDesktopEntry*> oldEntries = m_entryObjs;
    m_entryObjs.clear();
    const QList<QsDesktopEntry*> oldDynamic = m_dynamicRunningEntries.values();
    m_dynamicRunningEntries.clear();
    QVariantList entries;

    if (!m_appSource) {
        m_applications->setValues(entries);
        Q_EMIT applicationsChanged();
        for (QsDesktopEntry* e : oldEntries) e->deleteLater();
        for (QsDesktopEntry* e : oldDynamic) e->deleteLater();
        return;
    }

    const QVariantList list = m_appSource->ordered();
    for (const QVariant& v : list) {
        const QVariantMap row = v.toMap();
        const QVariantMap appMap = row.value(QStringLiteral("app")).toMap();
        const QString id = row.value(QStringLiteral("key")).toString();
        const QString name = row.value(QStringLiteral("name")).toString();
        const QString generic = appMap.value(QStringLiteral("comment")).toString();
        const QString icon = row.value(QStringLiteral("icon")).toString();
        const QString comment = appMap.value(QStringLiteral("comment")).toString();
        const QStringList kws = name.split(QLatin1Char(' '), Qt::SkipEmptyParts);

        auto* entry = new QsDesktopEntry(id, name, generic, icon, comment, kws, appMap, m_appSource, this);
        m_entryObjs.append(entry);
        entries.append(QVariant::fromValue(static_cast<QObject*>(entry)));
    }
    m_applications->setValues(entries);
    Q_EMIT applicationsChanged();
    for (QsDesktopEntry* e : oldEntries) e->deleteLater();
    for (QsDesktopEntry* e : oldDynamic) e->deleteLater();
}

QObject* QsDesktopEntries::byId(const QString& id) const {
    const QString clean = id.trimmed().toLower();
    for (QsDesktopEntry* e : m_entryObjs) {
        if (e->id().toLower() == clean) {
            return e;
        }
    }
    return heuristicLookup(id);
}

QObject* QsDesktopEntries::heuristicLookup(const QString& query) const {
    QString q = query.trimmed().toLower();
    if (q.endsWith(QStringLiteral(".desktop"))) {
        q.chop(8);
    }
    if (q.endsWith(QStringLiteral(".exe"))) {
        q.chop(4);
    }
    if (q.isEmpty()) {
        return nullptr;
    }

    static const QHash<QString, QString> winAliases = {
        {QStringLiteral("code"), QStringLiteral("visual studio code")},
        {QStringLiteral("msedge"), QStringLiteral("microsoft edge")},
        {QStringLiteral("chrome"), QStringLiteral("google chrome")},
        {QStringLiteral("explorer"), QStringLiteral("file explorer")},
        {QStringLiteral("windowsterminal"), QStringLiteral("terminal")},
        {QStringLiteral("wt"), QStringLiteral("terminal")},
        {QStringLiteral("pwsh"), QStringLiteral("powershell")},
        {QStringLiteral("winword"), QStringLiteral("word")},
        {QStringLiteral("excel"), QStringLiteral("excel")},
        {QStringLiteral("powerpnt"), QStringLiteral("powerpoint")}
    };
    const QString alias = winAliases.value(q);

    for (QsDesktopEntry* e : m_entryObjs) {
        const QString ename = e->name().toLower();
        const QString eid = e->id().toLower();
        if (eid == q || ename == q || (!alias.isEmpty() && ename == alias)) {
            return e;
        }
    }
    for (QsDesktopEntry* e : m_entryObjs) {
        const QString ename = e->name().toLower();
        const QString eid = e->id().toLower();
        if (eid.contains(q) || ename.contains(q) || (!alias.isEmpty() && ename.contains(alias))) {
            return e;
        }
    }

    // Fall back to live running window info from WindowTracker (real HWND/EXE icon + ShellExecuteW launch!)
    if (m_windowTracker) {
        for (const QVariant& wv : m_windowTracker->openWindows()) {
            const QVariantMap wm = wv.toMap();
            const QString appName = wm.value(QStringLiteral("app")).toString();
            const QString appLow = appName.toLower();
            const QString exePath = wm.value(QStringLiteral("exe")).toString();
            const QString exeStem = QFileInfo(exePath).completeBaseName().toLower();
            if (appLow == q || (!exeStem.isEmpty() && exeStem == q) ||
                (!appLow.isEmpty() && (appLow.contains(q) || q.contains(appLow)))) {
                const QString cacheKey = !exePath.isEmpty() ? exePath.toLower() : appLow;
                if (auto* existing = m_dynamicRunningEntries.value(cacheKey, nullptr)) {
                    return existing;
                }
                QString iconUrl = wm.value(QStringLiteral("icon")).toString();
                if (iconUrl.isEmpty() && !exePath.isEmpty()) {
                    iconUrl = Win32::WinIconProvider::appIconUrl(exePath);
                }
                QVariantMap dynApp;
                dynApp.insert(QStringLiteral("key"), cacheKey);
                dynApp.insert(QStringLiteral("name"), appName);
                dynApp.insert(QStringLiteral("target"), exePath);
                dynApp.insert(QStringLiteral("icon"), iconUrl);
                auto* dynEntry = new QsDesktopEntry(
                    appName, appName, QString(), iconUrl, QString(), {appName}, dynApp, m_appSource,
                    const_cast<QsDesktopEntries*>(this)
                );
                m_dynamicRunningEntries.insert(cacheKey, dynEntry);
                return dynEntry;
            }
        }
    }
    return nullptr;
}

// ============================================================================
// QsHyprlandWorkspace, QsHyprlandMonitor, QsHyprlandToplevel, QsHyprland
// ============================================================================

QsHyprlandWorkspace::QsHyprlandWorkspace(int id, QsHyprlandMonitor* mon, QObject* parent, const QString& customName)
    : QObject(parent)
    , m_id(id)
    , m_customName(customName)
    , m_monitor(mon)
    , m_toplevels(new QsObjectModel(this)) {
    setWindowCount(0, false);
}

QObject* QsHyprlandWorkspace::monitor() const {
    return m_monitor;
}

void QsHyprlandWorkspace::setId(int id) {
    if (m_id != id) {
        m_id = id;
        m_ipc[QStringLiteral("id")] = m_id;
        m_ipc[QStringLiteral("name")] = name();
        Q_EMIT changed();
    }
}

void QsHyprlandWorkspace::setWindowCount(int count, bool hasFullscreen) {
    const QVariantMap nextIpc{
        {QStringLiteral("id"), m_id},
        {QStringLiteral("name"), name()},
        {QStringLiteral("windows"), count},
        {QStringLiteral("hasfullscreen"), hasFullscreen}
    };
    if (m_ipc != nextIpc) {
        m_ipc = nextIpc;
        Q_EMIT changed();
    }
}

QsHyprlandMonitor::QsHyprlandMonitor(int id, QsScreen* screen, QObject* parent)
    : QObject(parent)
    , m_id(id)
    , m_screen(screen)
    , m_monitorWorkspace(new QsHyprlandWorkspace(1, this, this)) {
    m_activeWorkspace = m_monitorWorkspace;
    if (m_screen) {
        connect(m_screen, &QsScreen::geometryChanged, this, &QsHyprlandMonitor::changed);
    }
}

void QsHyprlandMonitor::setActiveWorkspace(QsHyprlandWorkspace* ws) {
    if (m_activeWorkspace != ws) {
        m_activeWorkspace = ws;
        Q_EMIT changed();
    }
}

QVariantMap QsHyprlandMonitor::lastIpcObject() const {
    return QVariantMap{
        {QStringLiteral("id"), m_id},
        {QStringLiteral("name"), name()},
        {QStringLiteral("width"), width()},
        {QStringLiteral("height"), height()},
        {QStringLiteral("x"), x()},
        {QStringLiteral("y"), y()},
        {QStringLiteral("scale"), 1.0},
        {QStringLiteral("focused"), true}
    };
}

QsHyprlandToplevel::QsHyprlandToplevel(
    const QString& address,
    const QString& title,
    const QString& appClass,
    bool activated,
    bool minimized,
    bool maximized,
    QsHyprlandWorkspace* ws,
    QsHyprlandMonitor* mon,
    QObject* parent
)
    : QObject(parent)
    , m_address(address)
    , m_title(title)
    , m_class(appClass)
    , m_activated(activated)
    , m_minimized(minimized)
    , m_maximized(maximized)
    , m_workspace(ws)
    , m_monitor(mon) {
    m_ipc = QVariantMap{
        {QStringLiteral("address"), m_address},
        {QStringLiteral("title"), m_title},
        {QStringLiteral("class"), m_class},
        {QStringLiteral("initialClass"), m_class},
        {QStringLiteral("mapped"), true},
        {QStringLiteral("hidden"), m_minimized},
        {QStringLiteral("pinned"), false},
        {QStringLiteral("floating"), !m_maximized},
        {QStringLiteral("fullscreen"), m_maximized ? 1 : 0},
        {QStringLiteral("workspace"), QVariantMap{{QStringLiteral("id"), ws ? ws->id() : 1}, {QStringLiteral("name"), ws ? ws->name() : QStringLiteral("1")}}}
    };
}

QsHyprland::QsHyprland(Win32::WindowTracker* tracker, QObject* parent)
    : QObject(parent)
    , m_tracker(tracker)
    , m_monitors(new QsObjectModel(this))
    , m_workspaces(new QsObjectModel(this))
    , m_toplevels(new QsObjectModel(this)) {
    m_syncTimer.setSingleShot(true);
    m_syncTimer.setInterval(0);
    connect(&m_syncTimer, &QTimer::timeout, this, [this]() {
        const QStringList events = std::move(m_pendingRawEvents);
        m_pendingRawEvents.clear();
        syncFromTracker();
        for (const QString& ev : events) {
            Q_EMIT rawEvent(QVariantMap{{QStringLiteral("name"), ev}});
        }
    });

    syncFromTracker();
    if (m_tracker) {
        connect(m_tracker, &Win32::WindowTracker::activeWindowChanged, this, [this]() {
            scheduleSyncFromTracker(QStringLiteral("activewindow"));
        });
        connect(m_tracker, &Win32::WindowTracker::activeWorkspaceChanged, this, [this]() {
            scheduleSyncFromTracker(QStringLiteral("workspace"));
        });
        connect(m_tracker, &Win32::WindowTracker::openWindowsChanged, this, [this]() {
            scheduleSyncFromTracker(QStringLiteral("openwindow"));
        });
        connect(m_tracker, &Win32::WindowTracker::fullscreenMonitorsChanged, this, [this]() {
            scheduleSyncFromTracker(QStringLiteral("fullscreen"));
        });
    }
    if (QsGlobal* g = QsGlobal::instance()) {
        connect(g, &QsGlobal::screensChanged, this, [this]() { scheduleSyncFromTracker(); });
    }
}

void QsHyprland::scheduleSyncFromTracker(const QString& pendingEvent) {
    if (!pendingEvent.isEmpty() && !m_pendingRawEvents.contains(pendingEvent)) {
        m_pendingRawEvents.append(pendingEvent);
    }
    if (!m_syncTimer.isActive()) {
        m_syncTimer.start();
    }
}

QObject* QsHyprland::focusedMonitor() const {
    return m_monitorObjs.isEmpty() ? nullptr : m_monitorObjs.first();
}

QObject* QsHyprland::focusedWorkspace() const {
    if (auto* mon = qobject_cast<QsHyprlandMonitor*>(focusedMonitor())) {
        return mon->activeWorkspace();
    }
    return m_workspaceObjs.isEmpty() ? nullptr : m_workspaceObjs.first();
}

QObject* QsHyprland::monitorFor(QObject* screenObj) const {
    if (!screenObj) {
        return focusedMonitor();
    }
    if (auto* qs = qobject_cast<QsScreen*>(screenObj)) {
        for (QsHyprlandMonitor* m : m_monitorObjs) {
            if (m->qsScreen() == qs || m->name() == qs->name()) {
                return m;
            }
        }
    }
    return focusedMonitor();
}

void QsHyprland::dispatch(const QString& cmd) {
    const QString trimmed = cmd.trimmed();
    if (!m_tracker) {
        return;
    }

    // Handle window-addressed dispatches first (focus, close, minimize to special:minimized)
    static const QRegularExpression addrRe(QStringLiteral("address\\s*[:=]\\s*['\"]?(0x[0-9a-fA-F]+|[0-9a-fA-F]+)"));
    const QRegularExpressionMatch addrMatch = addrRe.match(trimmed);
    if (addrMatch.hasMatch()) {
        QString hex = addrMatch.captured(1);
        if (hex.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
            hex = hex.mid(2);
        }
        bool ok = false;
        const quintptr val = hex.toULongLong(&ok, 16);
        if (ok && val != 0) {
            HWND hwnd = reinterpret_cast<HWND>(val);
            if (trimmed.contains(QStringLiteral("close"), Qt::CaseInsensitive)) {
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
                QTimer::singleShot(120, m_tracker, &Win32::WindowTracker::refresh);
            } else if (trimmed.contains(QStringLiteral("special:dockmin"), Qt::CaseInsensitive) ||
                       trimmed.contains(QStringLiteral("special:minimized"), Qt::CaseInsensitive) ||
                       trimmed.contains(QStringLiteral("minimized"), Qt::CaseInsensitive)) {
                m_tracker->minimizeWindow(static_cast<qulonglong>(val));
                QTimer::singleShot(60, m_tracker, &Win32::WindowTracker::refresh);
            } else {
                m_tracker->focusWindow(static_cast<qulonglong>(val));
                QTimer::singleShot(60, m_tracker, &Win32::WindowTracker::refresh);
                QTimer::singleShot(280, m_tracker, &Win32::WindowTracker::refresh);
            }
            return;
        }
    }

    // Handle workspace dispatches: "workspace 2", "workspace r+1", "workspace r-1", or Lua "hl.dsp.focus({ workspace = ... })"
    static const QRegularExpression wsNumRe(QStringLiteral("(?:workspace\\s*=?\\s*['\"]?)(\\d+)"));
    const QRegularExpressionMatch numMatch = wsNumRe.match(trimmed);
    if (numMatch.hasMatch()) {
        const int targetWs = numMatch.captured(1).toInt();
        if (targetWs >= 1 && targetWs <= 10) {
            m_tracker->switchWorkspace(targetWs);
            return;
        }
    }
    if (trimmed.contains(QStringLiteral("r+1")) || trimmed.contains(QStringLiteral("e+1")) || trimmed.contains(QStringLiteral("+1"))) {
        m_tracker->switchWorkspace(qBound(1, m_tracker->activeWorkspace() + 1, 10));
        return;
    }
    if (trimmed.contains(QStringLiteral("r-1")) || trimmed.contains(QStringLiteral("e-1")) || trimmed.contains(QStringLiteral("-1"))) {
        m_tracker->switchWorkspace(qBound(1, m_tracker->activeWorkspace() - 1, 10));
        return;
    }
}

void QsHyprland::syncFromTracker() {
    const QList<QsHyprlandToplevel*> oldToplevels = m_toplevelObjs;
    m_toplevelObjs.clear();
    m_activeToplevel = nullptr;

    const QList<QsHyprlandWorkspace*> oldWorkspaces = m_workspaceObjs;
    m_workspaceObjs.clear();

    auto deferDeleteOld = [&]() {
        for (auto* t : oldToplevels) t->deleteLater();
        for (auto* w : oldWorkspaces) w->deleteLater();
    };

    QVariantList monitorsList;
    QVariantList workspacesList;
    QVariantList toplevelsList;

    QsGlobal* g = QsGlobal::instance();
    if (!g) {
        deferDeleteOld();
        return;
    }

    // Keep QsHyprlandMonitor instances persistent across syncs so QML bindings to
    // Hyprland.monitorFor(screen) in WallpaperLayer.qml and Bar.qml never go stale/null.
    QList<QsHyprlandMonitor*> nextMonitors;
    int monId = 0;
    for (const QVariant& sv : g->screens()) {
        if (auto* qs = qobject_cast<QsScreen*>(sv.value<QObject*>())) {
            QsHyprlandMonitor* existingMon = nullptr;
            for (QsHyprlandMonitor* m : m_monitorObjs) {
                if (m && (m->qsScreen() == qs || m->name() == qs->name())) {
                    existingMon = m;
                    break;
                }
            }
            if (!existingMon) {
                existingMon = new QsHyprlandMonitor(monId, qs, this);
            }
            ++monId;
            nextMonitors.append(existingMon);
            monitorsList.append(QVariant::fromValue(static_cast<QObject*>(existingMon)));
        }
    }
    for (QsHyprlandMonitor* oldM : m_monitorObjs) {
        if (!nextMonitors.contains(oldM)) {
            oldM->deleteLater();
        }
    }
    const bool monitorsChanged = (m_monitorObjs != nextMonitors);
    m_monitorObjs = nextMonitors;

    QsHyprlandMonitor* primaryMon = m_monitorObjs.isEmpty() ? nullptr : m_monitorObjs.first();
    const int activeWsId = m_tracker ? m_tracker->activeWorkspace() : 1;
    const QVariantList occupied = m_tracker ? m_tracker->occupiedWorkspaces() : QVariantList{1};
    const QVariantList openWins = m_tracker ? m_tracker->openWindows() : QVariantList{};

    QHash<int, QsHyprlandWorkspace*> wsById;
    for (int id = 1; id <= 10; ++id) {
        if (id == activeWsId || occupied.contains(id)) {
            auto* ws = new QsHyprlandWorkspace(id, primaryMon, this);
            wsById.insert(id, ws);
            m_workspaceObjs.append(ws);
            workspacesList.append(QVariant::fromValue(static_cast<QObject*>(ws)));
        }
    }

    QsHyprlandWorkspace* activeWsObj = wsById.value(activeWsId, m_workspaceObjs.isEmpty() ? nullptr : m_workspaceObjs.first());

    // Virtual special workspace for minimized Windows apps ("special:dockmin")
    auto* minimizedWs = new QsHyprlandWorkspace(-99, primaryMon, this, QStringLiteral("special:dockmin"));
    m_workspaceObjs.append(minimizedWs);
    wsById.insert(-99, minimizedWs);

    auto monitorForName = [&](const QString& screenName) -> QsHyprlandMonitor* {
        if (!screenName.isEmpty()) {
            for (QsHyprlandMonitor* m : m_monitorObjs) {
                if (m && m->name().compare(screenName, Qt::CaseInsensitive) == 0) {
                    return m;
                }
            }
        }
        return primaryMon;
    };

    QHash<int, int> winCountPerWs;
    QHash<int, QVariantList> winsPerWs;
    QHash<QsHyprlandMonitor*, int> winCountPerMon;
    QHash<QsHyprlandMonitor*, bool> occludedPerMon;
    QHash<QsHyprlandMonitor*, QVariantList> winsPerMon;
    for (const QVariant& wv : openWins) {
        const QVariantMap wm = wv.toMap();
        const qulonglong hwndVal = wm.value(QStringLiteral("hwnd")).toULongLong();
        const QString title = wm.value(QStringLiteral("title")).toString();
        const QString app = wm.value(QStringLiteral("app")).toString();
        const QString scrName = wm.value(QStringLiteral("screen")).toString();
        const bool isMin = wm.value(QStringLiteral("minimized")).toBool();
        const bool isMax = wm.value(QStringLiteral("maximized")).toBool();
        const int wsId = isMin ? -99 : wm.value(QStringLiteral("workspace"), activeWsId).toInt();
        const bool isAct = !isMin && wm.value(QStringLiteral("active")).toBool();

        QsHyprlandMonitor* winMon = monitorForName(scrName);
        QsHyprlandWorkspace* wsObj = wsById.value(wsId, activeWsObj);
        winCountPerWs[wsId] = winCountPerWs.value(wsId, 0) + 1;
        if (!isMin && wsId == activeWsId && winMon) {
            winCountPerMon[winMon] = winCountPerMon.value(winMon, 0) + 1;
            if (isMax) {
                occludedPerMon[winMon] = true;
            }
        }

        // Format HWND as true hexadecimal ("0x...") so Hyprland.dispatch address parsing matches exact HWND!
        const QString addr = QStringLiteral("0x") + QString::number(hwndVal, 16);
        auto* tl = new QsHyprlandToplevel(addr, title, app, isAct, isMin, isMax, wsObj, winMon, this);
        m_toplevelObjs.append(tl);
        const QVariant tlVar = QVariant::fromValue(static_cast<QObject*>(tl));
        toplevelsList.append(tlVar);
        winsPerWs[wsObj ? wsObj->id() : activeWsId].append(tlVar);
        if (!isMin && wsId == activeWsId && winMon) {
            winsPerMon[winMon].append(tlVar);
        }
        if (isAct && !m_activeToplevel) {
            m_activeToplevel = tl;
        }
    }

    const bool anyFullscreen = m_tracker && !m_tracker->fullscreenMonitors().isEmpty();
    bool anyMaximized = false;
    for (bool occ : occludedPerMon) {
        if (occ) {
            anyMaximized = true;
            break;
        }
    }
    for (auto it = wsById.begin(); it != wsById.end(); ++it) {
        const bool wsHasFs = (anyFullscreen || anyMaximized) && (it.key() == activeWsId);
        it.value()->setWindowCount(winCountPerWs.value(it.key(), 0), wsHasFs);
        it.value()->toplevels()->setValues(winsPerWs.value(it.key()));
    }

    // Update each monitor's own persistent activeWorkspace with per-monitor window count and
    // per-monitor occlusion (fullscreen or maximized window covering that monitor's desktop).
    for (QsHyprlandMonitor* mon : m_monitorObjs) {
        if (!mon) continue;
        if (QsHyprlandWorkspace* monWs = mon->monitorWorkspace()) {
            monWs->setId(activeWsId);
            const bool monCovered = occludedPerMon.value(mon, false);
            monWs->setWindowCount(winCountPerMon.value(mon, 0), monCovered);
            monWs->toplevels()->setValues(winsPerMon.value(mon));
            mon->setActiveWorkspace(monWs);
        }
    }

    if (monitorsChanged || m_monitors->values().isEmpty()) {
        m_monitors->setValues(monitorsList);
    }
    m_workspaces->setValues(workspacesList);
    m_toplevels->setValues(toplevelsList);
    Q_EMIT stateChanged();
    deferDeleteOld();
}

// ============================================================================
// QsHyprlandFocusGrab
// ============================================================================

QsHyprlandFocusGrab::QsHyprlandFocusGrab(QObject* parent)
    : QObject(parent) {
    m_pollTimer.setInterval(12);
    connect(&m_pollTimer, &QTimer::timeout, this, &QsHyprlandFocusGrab::checkGrab);
}

void QsHyprlandFocusGrab::setWindows(const QVariantList& w) {
    if (m_windows != w) {
        m_windows = w;
        Q_EMIT windowsChanged();
    }
}

void QsHyprlandFocusGrab::setActive(bool a) {
    if (m_active == a) {
        return;
    }
    m_active = a;
    if (m_active) {
        m_wasPressed = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0 ||
                       (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
        m_pollTimer.start();
    } else {
        m_pollTimer.stop();
    }
    Q_EMIT activeChanged();
}

void QsHyprlandFocusGrab::checkGrab() {
    if (!m_active) {
        return;
    }
    const bool pressed = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0 ||
                         (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0 ||
                         (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;
    if (!pressed) {
        m_wasPressed = false;
        return;
    }
    if (m_wasPressed) {
        return;
    }
    m_wasPressed = true;

    POINT pt = {};
    if (!GetCursorPos(&pt)) {
        return;
    }
    for (const QVariant& v : std::as_const(m_windows)) {
        auto* win = qobject_cast<QWindow*>(v.value<QObject*>());
        if (!win || !win->isVisible()) {
            continue;
        }
        HWND hwnd = reinterpret_cast<HWND>(win->winId());
        if (!hwnd) {
            continue;
        }
        RECT wr = {};
        if (!GetWindowRect(hwnd, &wr)) {
            continue;
        }
        if (pt.x < wr.left || pt.x >= wr.right || pt.y < wr.top || pt.y >= wr.bottom) {
            continue;
        }
        if (auto* pw = qobject_cast<QsPanelWindow*>(win)) {
            if (pw->mask()) {
                const qreal dpr = qMax<qreal>(0.5, pw->devicePixelRatio());
                const QPointF localPt(
                    static_cast<qreal>(pt.x - wr.left) / dpr,
                    static_cast<qreal>(pt.y - wr.top) / dpr
                );
                if (pw->mask()->containsPoint(localPt)) {
                    return; // Inside mask of a grabbed PanelWindow
                }
                continue;
            }
        }
        return; // Inside a grabbed PopupWindow or unmasked window
    }

    // Clicked outside all grabbed windows
    m_active = false;
    m_pollTimer.stop();
    Q_EMIT activeChanged();
    Q_EMIT cleared();
}

// ============================================================================
// QsToplevelManager
// ============================================================================

QsToplevelManager::QsToplevelManager(QsHyprland* hypr, QObject* parent)
    : QObject(parent)
    , m_hypr(hypr) {
    if (m_hypr) {
        connect(m_hypr, &QsHyprland::stateChanged, this, &QsToplevelManager::toplevelsChanged);
    }
}

QsObjectModel* QsToplevelManager::toplevels() const {
    return m_hypr ? m_hypr->toplevels() : nullptr;
}

QObject* QsToplevelManager::activeToplevel() const {
    return m_hypr ? m_hypr->activeToplevel() : nullptr;
}

// ============================================================================
// QsMprisPlayer & QsMpris (Multi-Session Live SMTC + Ukikura Timeline)
// ============================================================================

QsMprisPlayer::QsMprisPlayer(
    const QString& aumid,
    Services::MediaSource* mediaSource,
    Services::QuickSource* quickSource,
    QObject* parent
)
    : QObject(parent)
    , m_aumid(aumid)
    , m_media(mediaSource)
    , m_quick(quickSource) {
    m_info.aumid = aumid;
    if (m_quick) {
        connect(m_quick, &Services::QuickSource::changed, this, &QsMprisPlayer::changed);
    }
}

void QsMprisPlayer::updateFromSession(const Services::MediaSessionInfo& info) {
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const bool trackChangedNow = (m_info.title != info.title) || (m_info.artist != info.artist) ||
                                 (m_info.album != info.album);
    const bool metaChangedNow = trackChangedNow ||
                                (m_info.artUrl != info.artUrl) ||
                                (m_info.identity != info.identity) ||
                                (m_info.desktopEntry != info.desktopEntry) ||
                                (m_info.present != info.present) ||
                                (std::abs(m_info.durationSec - info.durationSec) > 1.5);

    const bool keepOptimisticSeek = !trackChangedNow && (nowMs < m_optimisticSeekUntilMs) &&
                                    (std::abs(info.currentPositionSec() - m_info.currentPositionSec()) > 1.5);

    const double prevPos = m_info.currentPositionSec();
    Services::MediaSessionInfo nextInfo = info;
    if (keepOptimisticSeek) {
        nextInfo.basePositionSec = m_info.basePositionSec;
        nextInfo.lastUpdatedEpochMs = m_info.lastUpdatedEpochMs;
    } else if (m_optimisticSeekUntilMs > 0 && nowMs >= m_optimisticSeekUntilMs) {
        m_optimisticSeekUntilMs = 0;
    }

    m_info = nextInfo;
    const int newState = playbackState();
    const bool stateChangedNow = (newState != m_lastPlaybackState);
    if (stateChangedNow) {
        m_lastPlaybackState = newState;
        Q_EMIT playbackStateChanged();
    }

    if (metaChangedNow || stateChangedNow) {
        Q_EMIT changed();
    }
    if (trackChangedNow) {
        Q_EMIT trackChanged();
    }

    const double newPos = m_info.currentPositionSec();
    if (trackChangedNow || stateChangedNow || std::abs(newPos - prevPos) > 0.9) {
        Q_EMIT positionChanged();
    }
}

int QsMprisPlayer::playbackState() const {
    if (!m_info.present) {
        return MprisPlaybackState::Stopped;
    }
    return m_info.playing ? MprisPlaybackState::Playing : MprisPlaybackState::Paused;
}

bool QsMprisPlayer::isPlaying() const {
    return m_info.present && m_info.playing;
}

QString QsMprisPlayer::trackTitle() const {
    return m_info.title;
}

QString QsMprisPlayer::trackArtist() const {
    return m_info.artist;
}

QString QsMprisPlayer::trackAlbum() const {
    return m_info.album;
}

QString QsMprisPlayer::trackArtUrl() const {
    return m_info.artUrl;
}

QString QsMprisPlayer::identity() const {
    return !m_info.identity.isEmpty() ? m_info.identity : QStringLiteral("Media");
}

QString QsMprisPlayer::desktopEntry() const {
    return !m_info.desktopEntry.isEmpty() ? m_info.desktopEntry : QStringLiteral("media");
}

QString QsMprisPlayer::dbusName() const {
    return QStringLiteral("org.mpris.MediaPlayer2.") + desktopEntry();
}

qreal QsMprisPlayer::position() const {
    return m_info.currentPositionSec();
}

void QsMprisPlayer::setPosition(qreal p) {
    const double maxLen = m_info.durationSec > 0.0 ? m_info.durationSec : 86400.0;
    const double clamped = qBound(0.0, static_cast<double>(p), maxLen);
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    m_info.basePositionSec = clamped;
    m_info.lastUpdatedEpochMs = nowMs;
    m_optimisticSeekUntilMs = nowMs + 450;
    Q_EMIT positionChanged();
    if (m_media) {
        m_media->seek(clamped, m_aumid);
    }
}

qreal QsMprisPlayer::length() const {
    return m_info.durationSec;
}

bool QsMprisPlayer::positionSupported() const {
    return m_info.durationSec > 0.0 || m_info.basePositionSec > 0.0;
}

bool QsMprisPlayer::lengthSupported() const {
    return m_info.durationSec > 0.0;
}

bool QsMprisPlayer::canSeek() const {
    return m_info.durationSec > 0.0;
}

qreal QsMprisPlayer::volume() const {
    if (!m_quick) return 1.0;
    return qBound(0.0, m_quick->speakerVolume() / 100.0, 1.0);
}

void QsMprisPlayer::setVolume(qreal v) {
    if (!m_quick) return;
    const int pct = qBound(0, qRound(v * 100.0), 100);
    m_quick->setSpeakerVolume(pct);
    Q_EMIT changed();
}

QVariantMap QsMprisPlayer::metadata() const {
    QVariantMap md;
    if (!m_info.title.isEmpty()) md.insert(QStringLiteral("xesam:title"), m_info.title);
    if (!m_info.artist.isEmpty()) md.insert(QStringLiteral("xesam:artist"), QStringList{ m_info.artist });
    if (!m_info.album.isEmpty()) md.insert(QStringLiteral("xesam:album"), m_info.album);
    if (!m_info.artUrl.isEmpty()) md.insert(QStringLiteral("mpris:artUrl"), m_info.artUrl);
    if (m_info.durationSec > 0.0) md.insert(QStringLiteral("mpris:length"), static_cast<qint64>(std::llround(m_info.durationSec * 1000000.0)));
    return md;
}

void QsMprisPlayer::togglePlaying() {
    if (m_media) m_media->togglePlayPause(m_aumid);
}

void QsMprisPlayer::play() {
    if (m_media) m_media->play(m_aumid);
}

void QsMprisPlayer::pause() {
    if (m_media) m_media->pause(m_aumid);
}

void QsMprisPlayer::stop() {
    if (m_media) m_media->stop(m_aumid);
}

void QsMprisPlayer::next() {
    if (m_media) {
        m_info.basePositionSec = 0.0;
        m_info.lastUpdatedEpochMs = QDateTime::currentMSecsSinceEpoch();
        m_media->next(m_aumid);
    }
}

void QsMprisPlayer::previous() {
    if (m_media) {
        m_info.basePositionSec = 0.0;
        m_info.lastUpdatedEpochMs = QDateTime::currentMSecsSinceEpoch();
        m_media->previous(m_aumid);
    }
}

void QsMprisPlayer::raise() {
    if (m_media) {
        m_media->raisePlayer(m_aumid, m_info.title);
    }
}

QsMpris::QsMpris(
    Services::MediaSource* mediaSource,
    Services::QuickSource* quickSource,
    QObject* parent
)
    : QObject(parent)
    , m_media(mediaSource)
    , m_quick(quickSource)
    , m_players(new QsObjectModel(this)) {
    if (m_media) {
        connect(m_media, &Services::MediaSource::sessionsChanged, this, &QsMpris::syncSessions);
        syncSessions();
    }
}

void QsMpris::syncSessions() {
    if (!m_media) {
        return;
    }

    const QVector<Services::MediaSessionInfo> live = m_media->sessions();
    QSet<QString> seenKeys;
    QStringList newOrder;
    QVariantList modelVals;
    newOrder.reserve(live.size());
    modelVals.reserve(live.size());

    for (const auto& info : live) {
        const QString key = info.aumid.toLower();
        seenKeys.insert(key);
        QsMprisPlayer* pl = m_playerMap.value(key, nullptr);
        if (!pl) {
            pl = new QsMprisPlayer(info.aumid, m_media, m_quick, this);
            m_playerMap.insert(key, pl);
        }
        pl->updateFromSession(info);
        newOrder.push_back(key);
        modelVals.push_back(QVariant::fromValue(static_cast<QObject*>(pl)));
    }

    // Remove players whose SMTC sessions closed.
    QList<QString> staleKeys;
    for (auto it = m_playerMap.constBegin(); it != m_playerMap.constEnd(); ++it) {
        if (!seenKeys.contains(it.key())) {
            staleKeys.push_back(it.key());
        }
    }
    const bool listChanged = (newOrder != m_orderedKeys) || !staleKeys.isEmpty();
    if (listChanged) {
        m_orderedKeys = newOrder;
        m_players->setValues(modelVals);
        for (const QString& stale : staleKeys) {
            if (QsMprisPlayer* old = m_playerMap.take(stale)) {
                old->deleteLater();
            }
        }
        Q_EMIT playersChanged();
    }
}

// ============================================================================
// QsPwAudio, QsPwNode, QsPipewire
// ============================================================================

QsPwAudio::QsPwAudio(Services::QuickSource* qs, bool isSink, QObject* parent)
    : QObject(parent)
    , m_qs(qs)
    , m_isSink(isSink) {
    m_lastVol = volume();
    m_lastMuted = muted();
    if (m_qs) {
        connect(m_qs, &Services::QuickSource::changed, this, [this]() {
            const qreal curVol = volume();
            const bool curMuted = muted();
            if (!qFuzzyCompare(curVol, m_lastVol)) {
                m_lastVol = curVol;
                Q_EMIT volumeChanged();
            }
            if (curMuted != m_lastMuted) {
                m_lastMuted = curMuted;
                Q_EMIT mutedChanged();
            }
        });
    }
}

qreal QsPwAudio::volume() const {
    if (!m_qs) return 0.72;
    return (m_isSink ? m_qs->speakerVolume() : m_qs->microphoneVolume()) / 100.0;
}

void QsPwAudio::setVolume(qreal v) {
    if (!m_qs) return;
    const int pct = qBound(0, qRound(v * 100.0), 100);
    if (m_isSink) {
        m_qs->setSpeakerVolume(pct);
    } else {
        m_qs->setMicrophoneVolume(pct);
    }
}

bool QsPwAudio::muted() const {
    if (!m_qs) return false;
    return m_isSink ? !m_qs->speakerOn() : !m_qs->microphoneOn();
}

void QsPwAudio::setMuted(bool m) {
    if (!m_qs) return;
    if (m_isSink) {
        if (m_qs->speakerOn() == m) m_qs->toggleSpeaker();
    } else {
        if (m_qs->microphoneOn() == m) m_qs->toggleMicrophone();
    }
}

QsPwNode::QsPwNode(int id, const QString& name, bool isSink, Services::QuickSource* qs, QObject* parent)
    : QObject(parent)
    , m_id(id)
    , m_name(name)
    , m_isSink(isSink)
    , m_audio(new QsPwAudio(qs, isSink, this)) {}

QsPipewire::QsPipewire(Services::QuickSource* qs, QObject* parent)
    : QObject(parent)
    , m_sink(new QsPwNode(1, QStringLiteral("Windows Speaker"), true, qs, this))
    , m_source(new QsPwNode(2, QStringLiteral("Windows Microphone"), false, qs, this))
    , m_nodes(new QsObjectModel(this))
    , m_links(new QsObjectModel(this)) {
    m_nodes->setValues({
        QVariant::fromValue(static_cast<QObject*>(m_sink)),
        QVariant::fromValue(static_cast<QObject*>(m_source))
    });
}

// ============================================================================
// QsUPowerDevice & QsUPower
// ============================================================================

QsUPowerDevice::QsUPowerDevice(Services::BatterySource* bat, QObject* parent)
    : QObject(parent)
    , m_bat(bat) {
    if (m_bat) {
        connect(m_bat, &Services::BatterySource::changed, this, &QsUPowerDevice::changed);
    }
}

bool QsUPowerDevice::isLaptopBattery() const {
    return m_bat ? m_bat->available() : true;
}

qreal QsUPowerDevice::percentage() const {
    return m_bat ? (m_bat->percentage() / 100.0) : 0.85;
}

int QsUPowerDevice::state() const {
    if (!m_bat) return UPowerDeviceState::Discharging;
    if (m_bat->isCharging()) return UPowerDeviceState::Charging;
    if (m_bat->percentage() >= 100) return UPowerDeviceState::FullyCharged;
    return UPowerDeviceState::Discharging;
}

QsUPower::QsUPower(Services::BatterySource* bat, QObject* parent)
    : QObject(parent)
    , m_device(new QsUPowerDevice(bat, this))
    , m_devices(new QsObjectModel(this))
    , m_bat(bat) {
    m_devices->setValues({QVariant::fromValue(static_cast<QObject*>(m_device))});
    if (m_bat) {
        connect(m_bat, &Services::BatterySource::changed, this, &QsUPower::changed);
    }
}

bool QsUPower::onBattery() const {
    return m_bat ? !m_bat->isCharging() : false;
}

} // namespace SekaiPop::Quickshell
