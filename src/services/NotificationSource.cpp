#include "NotificationSource.h"
#include "SettingsService.h"

#include <QDateTime>

namespace SekaiPop::Services {

NotificationSource::NotificationSource(SettingsService* settings, QObject* parent)
    : QObject(parent)
    , m_settings(settings) {
    if (m_settings) {
        connect(m_settings, &SettingsService::keyChanged, this, [this](const QString& key) {
            if (key == QStringLiteral("notify.dnd")) {
                Q_EMIT dndChanged();
            }
        });
    }

    push(
        QStringLiteral("SekaiPop Shell"),
        QStringLiteral("Windows Shell Active"),
        QStringLiteral("Alt+Space: Apps | Alt+N: Quick Panel | Ctrl+Alt+Shift+E: Restore Explorer"),
        QStringLiteral("auto_awesome")
    );
}

bool NotificationSource::dnd() const {
    return m_settings && m_settings->flag(QStringLiteral("notify.dnd"));
}

void NotificationSource::setDnd(bool value) {
    if (m_settings) {
        m_settings->set(QStringLiteral("notify.dnd"), value);
    }
    Q_EMIT dndChanged();
}

void NotificationSource::toggleDnd() {
    setDnd(!dnd());
}

void NotificationSource::push(const QString& appName, const QString& summary, const QString& body, const QString& icon) {
    if (summary.trimmed().isEmpty()) {
        return;
    }
    QVariantMap row{
        { QStringLiteral("id"), m_nextId++ },
        { QStringLiteral("appName"), appName },
        { QStringLiteral("summary"), summary },
        { QStringLiteral("body"), body },
        { QStringLiteral("icon"), icon },
        { QStringLiteral("time"), QDateTime::currentDateTime().toString(QStringLiteral("hh:mm AP")) }
    };
    m_list.push_front(row);
    if (m_list.size() > 25) {
        m_list.removeLast();
    }
    Q_EMIT changed();
}

void NotificationSource::dismiss(int id) {
    for (int i = 0; i < m_list.size(); ++i) {
        if (m_list.at(i).toMap().value(QStringLiteral("id")).toInt() == id) {
            m_list.removeAt(i);
            Q_EMIT changed();
            return;
        }
    }
}

void NotificationSource::clearAll() {
    if (!m_list.isEmpty()) {
        m_list.clear();
        Q_EMIT changed();
    }
}

} // namespace SekaiPop::Services
