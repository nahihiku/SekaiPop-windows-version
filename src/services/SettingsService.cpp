#include "SettingsService.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace SekaiPop::Services {

SettingsService::SettingsService(QObject* parent)
    : QObject(parent) {
    initDefaults();
    const QString dir = stateDir();
    QDir().mkpath(dir);
    m_filePath = QDir(dir).filePath(QStringLiteral("settings.json"));
    load();
}

QString SettingsService::stateDir() {
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (base.isEmpty()) {
        base = QDir::homePath() + QStringLiteral("/AppData/Local/SekaiPop");
    }
    return base;
}

void SettingsService::initDefaults() {
    m_defaults = {
        // Bar
        { QStringLiteral("bar.visible"), true },
        { QStringLiteral("bar.hideMode"), QStringLiteral("off") },
        { QStringLiteral("bar.position"), QStringLiteral("top") },
        { QStringLiteral("bar.style"), QStringLiteral("float") },
        { QStringLiteral("bar.workspaceCount"), 5 },
        { QStringLiteral("bar.scrollHints"), true },
        { QStringLiteral("bar.workspaces"), true },
        { QStringLiteral("bar.activeWindow"), true },
        { QStringLiteral("bar.weather"), true },
        { QStringLiteral("bar.media"), true },
        { QStringLiteral("bar.utilButtons"), true },
        { QStringLiteral("bar.resources"), true },
        { QStringLiteral("bar.status"), true },
        { QStringLiteral("bar.battery"), true },
        { QStringLiteral("bar.date"), true },
        { QStringLiteral("bar.clock"), true },

        // Dock
        { QStringLiteral("dock.visible"), true },
        { QStringLiteral("dock.style"), QStringLiteral("float") },
        { QStringLiteral("dock.position"), QStringLiteral("bottom") },
        { QStringLiteral("dock.magnify"), true },
        { QStringLiteral("dock.autoHide"), false },
        { QStringLiteral("dock.media"), true },
        { QStringLiteral("dock.hideWindowsTaskbar"), true },

        // Desktop Widgets & Audio Visualizer
        { QStringLiteral("widgets.todo"), true },
        { QStringLiteral("widgets.clock"), true },
        { QStringLiteral("widgets.clockDesign"), 2 },
        { QStringLiteral("widgets.clock24h"), false },
        { QStringLiteral("widgets.userCard"), true },
        { QStringLiteral("widgets.weatherCard"), false },
        { QStringLiteral("widgets.visualizer"), true },
        { QStringLiteral("widgets.visualizerAmbient"), true },
        { QStringLiteral("widgets.locked"), false },

        // Weather & Notifications
        { QStringLiteral("weather.city"), QString() },
        { QStringLiteral("weather.view"), QStringLiteral("hourly") },
        { QStringLiteral("notify.dnd"), false },
        { QStringLiteral("quick.nightLight"), false },
        { QStringLiteral("calendar.marks"), QString() },

        // General & Appearance
        { QStringLiteral("time.use24h"), false },
        { QStringLiteral("theme.perMonitor"), false },
        { QStringLiteral("theme.group"), QStringLiteral("vs") },
        { QStringLiteral("theme.mode"), QStringLiteral("light") },
        { QStringLiteral("appearance.iconStyle"), QStringLiteral("outline") },

        // Wallpaper & Shader Transitions
        { QStringLiteral("wallpaper.enabled"), true },
        { QStringLiteral("wallpaper.perMonitor"), false },
        { QStringLiteral("wallpaper.defaultPath"), QString() },
        { QStringLiteral("wallpaper.style"), QStringLiteral("sekaiRing") },
        { QStringLiteral("wallpaper.themedTransition"), true },
        { QStringLiteral("wallpaper.parallax"), false },
        { QStringLiteral("wallpaper.parallaxAmount"), 8 },
        { QStringLiteral("wallpaper.parallaxWorkspaces"), 5 },

        // Launcher & Phone
        { QStringLiteral("launcher.order"), QStringLiteral("az") },
        { QStringLiteral("launcher.webSuggestions"), true },
        { QStringLiteral("phone.wallpaper"), QStringLiteral("desktop") },
        { QStringLiteral("quick.flat"), false },
        { QStringLiteral("quick.phoneShadow"), true },
        { QStringLiteral("panel.peek"), 76 },
        { QStringLiteral("scale.default"), 1.0 }
    };
    m_values = m_defaults;
}

QVariant SettingsService::get(const QString& key) const {
    return m_values.value(key, m_defaults.value(key));
}

bool SettingsService::flag(const QString& key) const {
    return get(key).toBool();
}

void SettingsService::set(const QString& key, const QVariant& value) {
    const QVariant defVal = m_defaults.value(key);
    if (value == defVal) {
        m_overrides.remove(key);
    } else {
        m_overrides.insert(key, value);
    }

    if (m_values.value(key) == value) {
        save();
        return;
    }

    m_values.insert(key, value);
    save();
    Q_EMIT keyChanged(key);
    Q_EMIT changed();
}

void SettingsService::reset() {
    m_overrides.clear();
    m_values = m_defaults;
    save();
    Q_EMIT changed();
}

void SettingsService::load() {
    QFile f(m_filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) {
        return;
    }
    const QJsonObject obj = doc.object();
    m_overrides.clear();
    m_values = m_defaults;
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        const QVariant v = it.value().toVariant();
        m_overrides.insert(it.key(), v);
        m_values.insert(it.key(), v);
    }
}

void SettingsService::save() const {
    QFile f(m_filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return;
    }
    const QJsonObject obj = QJsonObject::fromVariantMap(m_overrides);
    f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
}

} // namespace SekaiPop::Services
