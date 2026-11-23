#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

namespace SekaiPop::Services {

/**
 * Windows native battery status backend using GetSystemPowerStatus
 * (replaces Linux Quickshell.Services.UPower).
 */
class BatterySource final : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(int percentage READ percentage NOTIFY changed)
    Q_PROPERTY(bool isCharging READ isCharging NOTIFY changed)
    Q_PROPERTY(bool isPluggedIn READ isPluggedIn NOTIFY changed)
    Q_PROPERTY(bool isLow READ isLow NOTIFY changed)
    Q_PROPERTY(QString timeRemaining READ timeRemaining NOTIFY changed)
    Q_PROPERTY(QString statusLabel READ statusLabel NOTIFY changed)

public:
    explicit BatterySource(QObject* parent = nullptr);

    bool available() const { return m_available; }
    int percentage() const { return m_percentage; }
    bool isCharging() const { return m_isCharging; }
    bool isPluggedIn() const { return m_isPluggedIn; }
    bool isLow() const { return m_available && m_percentage <= 20 && !m_isCharging; }
    QString timeRemaining() const { return m_timeRemaining; }
    QString statusLabel() const { return m_statusLabel; }

    Q_INVOKABLE void refresh();

Q_SIGNALS:
    void changed();

private:
    QTimer m_timer;
    bool m_available = false;
    int m_percentage = 0;
    bool m_isCharging = false;
    bool m_isPluggedIn = false;
    QString m_timeRemaining;
    QString m_statusLabel;
};

} // namespace SekaiPop::Services
