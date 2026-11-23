#pragma once

#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantMap>

namespace SekaiPop::Services {

/**
 * Pure C++ JSON settings store persisted under %LOCALAPPDATA%/SekaiPop/settings.json.
 * Replaces Linux shell-script state files (settings-state.sh) with native Qt I/O.
 */
class SettingsService final : public QObject {
    Q_OBJECT

    Q_PROPERTY(QVariantMap values READ values NOTIFY changed)

public:
    explicit SettingsService(QObject* parent = nullptr);

    QVariantMap values() const { return m_values; }

    Q_INVOKABLE QVariant get(const QString& key) const;
    Q_INVOKABLE bool flag(const QString& key) const;
    Q_INVOKABLE void set(const QString& key, const QVariant& value);
    Q_INVOKABLE void reset();

    static QString stateDir();

Q_SIGNALS:
    void changed();
    void keyChanged(const QString& key);

private:
    void initDefaults();
    void load();
    void save() const;

    QVariantMap m_defaults;
    QVariantMap m_overrides;
    QVariantMap m_values;
    QString m_filePath;
};

} // namespace SekaiPop::Services
