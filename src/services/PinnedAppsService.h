#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace SekaiPop::Services {

class AppSource;

/**
 * Manages Dock pinned applications persisted at
 * %LOCALAPPDATA%\SekaiPop\SekaiPopShell\dock.json.
 */
class PinnedAppsService final : public QObject {
    Q_OBJECT

    Q_PROPERTY(QVariantList pinned READ pinned NOTIFY changed)

public:
    explicit PinnedAppsService(AppSource* appSource, QObject* parent = nullptr);

    QVariantList pinned() const { return m_pinned; }

    Q_INVOKABLE bool isPinned(const QString& keyOrTarget) const;
    Q_INVOKABLE void pinApp(const QVariantMap& app);
    Q_INVOKABLE void unpin(const QString& keyOrTarget);
    Q_INVOKABLE void movePin(int from, int to);
    Q_INVOKABLE void launchItem(const QVariantMap& item);

Q_SIGNALS:
    void changed();

private:
    void load();
    void save() const;
    void seedDefaultsIfEmpty();

    AppSource* m_appSource = nullptr;
    QString m_filePath;
    QVariantList m_pinned;
    bool m_seeded = false;
};

} // namespace SekaiPop::Services
