#include "StatusIconsService.h"
#include "QuickSource.h"

#include <QStringList>

namespace SekaiPop::Services {

StatusIconsService::StatusIconsService(QuickSource* quickSource, QObject* parent)
    : QObject(parent)
    , m_quick(quickSource) {
    if (m_quick) {
        connect(m_quick, &QuickSource::changed, this, &StatusIconsService::changed);
    }
}

QString StatusIconsService::networkIcon() const {
    if (!m_quick) {
        return QStringLiteral("wifi_off");
    }
    if (!m_quick->wifiName().isEmpty()) {
        return QStringLiteral("wifi");
    }
    if (!m_quick->ethernetName().isEmpty()) {
        return QStringLiteral("settings_ethernet");
    }
    return m_quick->wifiOn() ? QStringLiteral("wifi_find") : QStringLiteral("wifi_off");
}

QString StatusIconsService::networkState() const {
    if (!m_quick) {
        return QStringLiteral("off");
    }
    if (!m_quick->wifiName().isEmpty() || !m_quick->ethernetName().isEmpty()) {
        return QStringLiteral("on");
    }
    return m_quick->wifiOn() ? QStringLiteral("idle") : QStringLiteral("off");
}

bool StatusIconsService::bluetoothShown() const {
    return m_quick && m_quick->bluetoothAvailable();
}

QString StatusIconsService::bluetoothIcon() const {
    if (!m_quick || !m_quick->bluetoothOn()) {
        return QStringLiteral("bluetooth_disabled");
    }
    return m_quick->bluetoothConnected() ? QStringLiteral("bluetooth_connected") : QStringLiteral("bluetooth");
}

QString StatusIconsService::bluetoothState() const {
    if (!m_quick || !m_quick->bluetoothOn()) {
        return QStringLiteral("off");
    }
    return m_quick->bluetoothConnected() ? QStringLiteral("on") : QStringLiteral("idle");
}

bool StatusIconsService::speakerShown() const {
    return m_quick && m_quick->speakerAvailable();
}

QString StatusIconsService::speakerIcon() const {
    if (!m_quick || !m_quick->speakerOn()) {
        return QStringLiteral("volume_off");
    }
    const qreal v = m_quick->speakerVolume();
    if (v <= 0) return QStringLiteral("volume_mute");
    if (v < 45) return QStringLiteral("volume_down");
    return QStringLiteral("volume_up");
}

QString StatusIconsService::speakerState() const {
    return (m_quick && m_quick->speakerOn()) ? QStringLiteral("on") : QStringLiteral("off");
}

QString StatusIconsService::detailText() const {
    if (!m_quick) {
        return {};
    }
    QStringList lines;
    if (m_quick->networkKind() == QStringLiteral("ethernet")) {
        lines << (QStringLiteral("Ethernet: ") + m_quick->ethernetName());
    } else if (!m_quick->wifiName().isEmpty()) {
        lines << (QStringLiteral("Wi-Fi: ") + m_quick->wifiName());
    } else {
        lines << (m_quick->wifiOn() ? QStringLiteral("Wi-Fi: Not connected") : QStringLiteral("Wi-Fi: OFF"));
    }

    if (m_quick->bluetoothAvailable()) {
        lines << (QStringLiteral("Bluetooth: ") + m_quick->bluetoothDetail());
    }

    if (m_quick->speakerAvailable()) {
        lines << (m_quick->speakerOn()
            ? QStringLiteral("Speaker: %1%").arg(qRound(m_quick->speakerVolume()))
            : QStringLiteral("Speaker: Muted"));
    }
    return lines.join(QLatin1Char('\n'));
}

} // namespace SekaiPop::Services
