#include "BatterySource.h"

#include <windows.h>

namespace SekaiPop::Services {

BatterySource::BatterySource(QObject* parent)
    : QObject(parent) {
    m_timer.setInterval(5000);
    connect(&m_timer, &QTimer::timeout, this, &BatterySource::refresh);
    refresh();
    m_timer.start();
}

void BatterySource::refresh() {
    SYSTEM_POWER_STATUS sps = {};
    if (!GetSystemPowerStatus(&sps)) {
        return;
    }

    const bool hasBattery = (sps.BatteryFlag & 128) == 0 && sps.BatteryFlag != 255 && sps.BatteryLifePercent <= 100;
    const int pct = hasBattery ? static_cast<int>(sps.BatteryLifePercent) : 0;
    const bool plugged = sps.ACLineStatus == 1;
    const bool charging = hasBattery && ((sps.BatteryFlag & 8) != 0 || (plugged && pct < 100));

    QString rem;
    if (hasBattery && !charging && sps.BatteryLifeTime != BATTERY_LIFE_UNKNOWN && sps.BatteryLifeTime > 0) {
        const quint32 mins = sps.BatteryLifeTime / 60;
        const quint32 h = mins / 60;
        const quint32 m = mins % 60;
        rem = (h > 0 ? QString::number(h) + QStringLiteral("h ") : QString()) + QString::number(m) + QStringLiteral("m");
    }

    QString label;
    if (hasBattery) {
        if (pct >= 100 && plugged) label = QStringLiteral("Fully charged");
        else if (charging) label = QStringLiteral("Charging");
        else if (plugged) label = QStringLiteral("Plugged in");
        else label = QStringLiteral("On battery");
    }

    if (hasBattery != m_available || pct != m_percentage || charging != m_isCharging ||
        plugged != m_isPluggedIn || rem != m_timeRemaining || label != m_statusLabel) {
        m_available = hasBattery;
        m_percentage = pct;
        m_isCharging = charging;
        m_isPluggedIn = plugged;
        m_timeRemaining = rem;
        m_statusLabel = label;
        Q_EMIT changed();
    }
}

} // namespace SekaiPop::Services
