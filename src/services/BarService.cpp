#include "BarService.h"
#include "SettingsService.h"

namespace SekaiPop::Services {

BarService::BarService(SettingsService* settings, QObject* parent)
    : QObject(parent)
    , m_settings(settings) {
    if (m_settings) {
        const QString pos = m_settings->get(QStringLiteral("bar.position")).toString();
        if (positionNames().contains(pos)) {
            m_position = pos;
        }
        const QString st = m_settings->get(QStringLiteral("bar.style")).toString();
        if (styleNames().contains(st)) {
            m_style = st;
        }
        connect(m_settings, &SettingsService::keyChanged, this, [this](const QString& key) {
            if (key == QStringLiteral("bar.hideMode") || key == QStringLiteral("bar.workspaceCount")) {
                Q_EMIT changed();
            }
        });
    }

    m_layout = {
        { QStringLiteral("left"), QStringList{ QStringLiteral("workspaces"), QStringLiteral("activeWindow"), QStringLiteral("weather"), QStringLiteral("media") } },
        { QStringLiteral("center"), QStringList{ QStringLiteral("status"), QStringLiteral("date"), QStringLiteral("clock") } },
        { QStringLiteral("right"), QStringList{ QStringLiteral("resources"), QStringLiteral("battery"), QStringLiteral("utilButtons") } }
    };
}

QStringList BarService::positionNames() const {
    return { QStringLiteral("top"), QStringLiteral("right"), QStringLiteral("bottom"), QStringLiteral("left") };
}

QStringList BarService::styleNames() const {
    return { QStringLiteral("float"), QStringLiteral("straight"), QStringLiteral("transparent"), QStringLiteral("capsule") };
}

int BarService::thicknessFor(const QString& pos) const {
    return (pos == QStringLiteral("left") || pos == QStringLiteral("right")) ? 74 : 63;
}

int BarService::stripSizeFor(const QString& pos, qreal uiScale) const {
    const bool vert = (pos == QStringLiteral("left") || pos == QStringLiteral("right"));
    const int base = (styleIndex() == 3)
        ? (vert ? 72 : 55) // capsule bar dimensions from ThemeSet.qml
        : thicknessFor(pos);
    return qRound(base * uiScale);
}

int BarService::reserved() const {
    return autoHide() ? 0 : thickness();
}

int BarService::styleIndex() const {
    const int idx = styleNames().indexOf(m_style);
    return idx >= 0 ? idx : 2;
}

bool BarService::autoHide() const {
    return m_settings && m_settings->get(QStringLiteral("bar.hideMode")).toString() == QStringLiteral("auto");
}

int BarService::workspaceCount() const {
    return m_settings ? qBound(1, m_settings->get(QStringLiteral("bar.workspaceCount")).toInt(), 10) : 5;
}

void BarService::setPosition(const QString& pos) {
    if (!positionNames().contains(pos) || m_position == pos) {
        return;
    }
    m_position = pos;
    if (m_settings) {
        m_settings->set(QStringLiteral("bar.position"), pos);
    }
    Q_EMIT changed();
}

void BarService::cyclePosition() {
    const QStringList names = positionNames();
    const int next = (names.indexOf(m_position) + 1) % names.size();
    setPosition(names.at(next));
}

void BarService::setStyle(const QString& style) {
    if (!styleNames().contains(style) || m_style == style) {
        return;
    }
    m_style = style;
    if (m_settings) {
        m_settings->set(QStringLiteral("bar.style"), style);
    }
    Q_EMIT changed();
}

void BarService::cycleStyle() {
    const QStringList names = styleNames();
    const int next = (names.indexOf(m_style) + 1) % names.size();
    setStyle(names.at(next));
}

void BarService::toggleAutoHide() {
    if (!m_settings) {
        return;
    }
    m_settings->set(QStringLiteral("bar.hideMode"), autoHide() ? QStringLiteral("off") : QStringLiteral("auto"));
}

} // namespace SekaiPop::Services
