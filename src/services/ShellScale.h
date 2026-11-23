#pragma once

#include <QObject>
#include <QVariant>

namespace SekaiPop::Services {

class SettingsService;

/**
 * Per-monitor UI scale helper (replaces Linux ShellScale.qml).
 */
class ShellScale final : public QObject {
    Q_OBJECT

    Q_PROPERTY(qreal defaultScale READ defaultScale WRITE setDefaultScale NOTIFY changed)

public:
    explicit ShellScale(SettingsService* settings, QObject* parent = nullptr);

    qreal defaultScale() const { return m_defaultScale; }
    void setDefaultScale(qreal scale);

    Q_INVOKABLE qreal forScreen(const QVariant& screen = {}) const;

Q_SIGNALS:
    void changed();

private:
    SettingsService* m_settings = nullptr;
    qreal m_defaultScale = 1.0;
};

} // namespace SekaiPop::Services
