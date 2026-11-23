#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace SekaiPop::Services {

class SettingsService;

/**
 * Windows native App Launcher backend replacing Linux .desktop DesktopEntries.
 * Scans Start Menu shortcuts (.lnk via IShellLinkW) and UWP/Store packaged apps
 * (FOLDERID_AppsFolder) asynchronously and resolves icons via WinIconProvider.
 */
class AppSource final : public QObject {
    Q_OBJECT

    Q_PROPERTY(QVariantList ordered READ ordered NOTIFY entriesChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(int count READ count NOTIFY entriesChanged)

public:
    explicit AppSource(SettingsService* settings, QObject* parent = nullptr);

    QVariantList ordered() const { return m_ordered; }
    bool scanning() const { return m_scanning; }
    int count() const { return m_ordered.size(); }

    Q_INVOKABLE void sync();
    Q_INVOKABLE void rescan();
    Q_INVOKABLE QVariantList rankEntries(const QString& query) const;
    Q_INVOKABLE QString iconFor(const QString& iconUrl) const;
    Q_INVOKABLE void launch(const QVariantMap& app);

Q_SIGNALS:
    void entriesChanged();
    void scanningChanged();

private:
    struct AppEntry {
        QString key;
        QString name;
        QString comment;
        QString target;
        QString args;
        QString workDir;
        QString iconUrl;
        bool isUwp = false;
        QString nameLower;
        QString commentLower;
    };

    void rebuildOrdered();
    void loadUsage();
    void saveUsage() const;

    static QList<AppEntry> scanWindowsApps();

    SettingsService* m_settings = nullptr;
    QList<AppEntry> m_entries;
    QVariantList m_ordered;
    QHash<QString, int> m_usage;
    QString m_usagePath;
    bool m_scanning = false;
};

} // namespace SekaiPop::Services
