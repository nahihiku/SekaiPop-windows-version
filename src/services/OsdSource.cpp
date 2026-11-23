#include "OsdSource.h"

namespace SekaiPop::Services {

OsdSource::OsdSource(QObject* parent)
    : QObject(parent) {
    m_hideTimer.setSingleShot(true);
    m_hideTimer.setInterval(1650);
    connect(&m_hideTimer, &QTimer::timeout, this, &OsdSource::hide);
}

void OsdSource::show(const QString& kind, const QString& icon, const QString& label, qreal value, bool muted) {
    m_kind = kind;
    m_icon = icon;
    m_label = label;
    m_value = qBound<qreal>(0.0, value, 100.0);
    m_muted = muted;
    m_open = true;
    m_hideTimer.start();
    Q_EMIT changed();
}

void OsdSource::hide() {
    if (!m_open) {
        return;
    }
    m_open = false;
    m_hideTimer.stop();
    Q_EMIT changed();
}

} // namespace SekaiPop::Services
