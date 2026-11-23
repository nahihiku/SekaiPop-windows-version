#include "PinnedAppsService.h"
#include "AppSource.h"
#include "SettingsService.h"
#include "../win32/Win32Utils.h"
#include "../win32/WinIconProvider.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace SekaiPop::Services {

PinnedAppsService::PinnedAppsService(AppSource* appSource, QObject* parent)
    : QObject(parent)
    , m_appSource(appSource) {
    const QString dir = SettingsService::stateDir();
    QDir().mkpath(dir);
    m_filePath = QDir(dir).filePath(QStringLiteral("dock.json"));
    load();

    if (m_appSource) {
        connect(m_appSource, &AppSource::entriesChanged, this, &PinnedAppsService::seedDefaultsIfEmpty);
        seedDefaultsIfEmpty();
    }
}

void PinnedAppsService::seedDefaultsIfEmpty() {
    if (m_seeded || !m_pinned.isEmpty() || !m_appSource) {
        return;
    }
    const QVariantList entries = m_appSource->ordered();
    if (entries.isEmpty()) {
        return;
    }
    m_seeded = true;

    // 1. Always add Windows File Explorer directly first.
    QVariantMap explorerMap{
        { QStringLiteral("key"), QStringLiteral("win:explorer") },
        { QStringLiteral("name"), QStringLiteral("File Explorer") },
        { QStringLiteral("target"), QStringLiteral("explorer.exe") },
        { QStringLiteral("args"), QString() },
        { QStringLiteral("icon"), Win32::WinIconProvider::appIconUrl(QStringLiteral("C:/Windows/explorer.exe")) }
    };
    m_pinned.push_back(explorerMap);

    // 2. Pick useful everyday apps installed on this machine.
    const QList<QStringList> wantedGroups = {
        { QStringLiteral("windows terminal"), QStringLiteral("powershell"), QStringLiteral("command prompt") },
        { QStringLiteral("brave"), QStringLiteral("google chrome"), QStringLiteral("microsoft edge"), QStringLiteral("firefox") },
        { QStringLiteral("visual studio code"), QStringLiteral("notepad") },
        { QStringLiteral("calculator") },
        { QStringLiteral("settings") }
    };

    for (const QStringList& candidates : wantedGroups) {
        for (const QString& cand : candidates) {
            bool matched = false;
            for (const QVariant& rowVar : entries) {
                const QVariantMap row = rowVar.toMap();
                const QString nm = row.value(QStringLiteral("name")).toString().toLower();
                if (nm == cand || nm.contains(cand)) {
                    const QVariantMap app = row.value(QStringLiteral("app")).toMap();
                    m_pinned.push_back(QVariantMap{
                        { QStringLiteral("key"), app.value(QStringLiteral("key")) },
                        { QStringLiteral("name"), app.value(QStringLiteral("name")) },
                        { QStringLiteral("target"), app.value(QStringLiteral("target")) },
                        { QStringLiteral("args"), app.value(QStringLiteral("args")) },
                        { QStringLiteral("icon"), app.value(QStringLiteral("icon")) }
                    });
                    matched = true;
                    break;
                }
            }
            if (matched) {
                break;
            }
        }
    }

    save();
    Q_EMIT changed();
}

bool PinnedAppsService::isPinned(const QString& keyOrTarget) const {
    if (keyOrTarget.isEmpty()) {
        return false;
    }
    const QString low = keyOrTarget.toLower();
    for (const QVariant& v : m_pinned) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("key")).toString().toLower() == low ||
            m.value(QStringLiteral("target")).toString().toLower() == low ||
            m.value(QStringLiteral("name")).toString().toLower() == low) {
            return true;
        }
    }
    return false;
}

void PinnedAppsService::pinApp(const QVariantMap& app) {
    const QString key = app.value(QStringLiteral("key")).toString();
    const QString target = app.value(QStringLiteral("target")).toString();
    const QString name = app.value(QStringLiteral("name")).toString();
    if (target.isEmpty() && name.isEmpty()) {
        return;
    }
    if (isPinned(!key.isEmpty() ? key : (!target.isEmpty() ? target : name))) {
        return;
    }
    m_pinned.push_back(QVariantMap{
        { QStringLiteral("key"), key.isEmpty() ? target.toLower() : key },
        { QStringLiteral("name"), name },
        { QStringLiteral("target"), target },
        { QStringLiteral("args"), app.value(QStringLiteral("args")).toString() },
        { QStringLiteral("icon"), app.value(QStringLiteral("icon")).toString() }
    });
    m_seeded = true;
    save();
    Q_EMIT changed();
}

void PinnedAppsService::unpin(const QString& keyOrTarget) {
    const QString low = keyOrTarget.toLower();
    for (int i = 0; i < m_pinned.size(); ++i) {
        const QVariantMap m = m_pinned.at(i).toMap();
        if (m.value(QStringLiteral("key")).toString().toLower() == low ||
            m.value(QStringLiteral("target")).toString().toLower() == low ||
            m.value(QStringLiteral("name")).toString().toLower() == low) {
            m_pinned.removeAt(i);
            m_seeded = true;
            save();
            Q_EMIT changed();
            return;
        }
    }
}

void PinnedAppsService::movePin(int from, int to) {
    if (from < 0 || from >= m_pinned.size() || to < 0 || to >= m_pinned.size() || from == to) {
        return;
    }
    m_pinned.move(from, to);
    save();
    Q_EMIT changed();
}

void PinnedAppsService::launchItem(const QVariantMap& item) {
    const QString target = item.value(QStringLiteral("target")).toString();
    const QString args = item.value(QStringLiteral("args")).toString();
    if (!target.isEmpty()) {
        Win32::launchShellTarget(target, args, {}, SW_SHOWNORMAL);
    }
}

void PinnedAppsService::load() {
    QFile f(m_filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isArray()) {
        return;
    }
    m_pinned.clear();
    for (const QJsonValue& val : doc.array()) {
        const QJsonObject obj = val.toObject();
        m_pinned.push_back(obj.toVariantMap());
    }
    if (!m_pinned.isEmpty()) {
        m_seeded = true;
    }
}

void PinnedAppsService::save() const {
    QFile f(m_filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return;
    }
    QJsonArray arr;
    for (const QVariant& v : m_pinned) {
        arr.append(QJsonObject::fromVariantMap(v.toMap()));
    }
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
}

} // namespace SekaiPop::Services
