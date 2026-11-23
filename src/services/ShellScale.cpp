#include "ShellScale.h"
#include "SettingsService.h"

namespace SekaiPop::Services {

ShellScale::ShellScale(SettingsService* settings, QObject* parent)
    : QObject(parent)
    , m_settings(settings) {
    if (m_settings) {
        const qreal s = m_settings->get(QStringLiteral("scale.default")).toDouble();
        if (s >= 0.5 && s <= 3.0) {
            m_defaultScale = s;
        }
    }
}

void ShellScale::setDefaultScale(qreal scale) {
    const qreal clamped = qBound<qreal>(0.5, scale, 3.0);
    if (qFuzzyCompare(m_defaultScale, clamped)) {
        return;
    }
    m_defaultScale = clamped;
    if (m_settings) {
        m_settings->set(QStringLiteral("scale.default"), clamped);
    }
    Q_EMIT changed();
}

qreal ShellScale::forScreen(const QVariant&) const {
    return m_defaultScale;
}

} // namespace SekaiPop::Services
