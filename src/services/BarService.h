#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace SekaiPop::Services {

class SettingsService;

/**
 * Unified bar configuration service for Windows (replaces BarPositionService,
 * BarStyleService, and BarLayoutService without shell-script persistence).
 */
class BarService final : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString position READ position NOTIFY changed)
    Q_PROPERTY(bool vertical READ vertical NOTIFY changed)
    Q_PROPERTY(int thickness READ thickness NOTIFY changed)
    Q_PROPERTY(int reserved READ reserved NOTIFY changed)
    Q_PROPERTY(QString style READ style NOTIFY changed)
    Q_PROPERTY(int styleIndex READ styleIndex NOTIFY changed)
    Q_PROPERTY(bool autoHide READ autoHide NOTIFY changed)
    Q_PROPERTY(int workspaceCount READ workspaceCount NOTIFY changed)
    Q_PROPERTY(QVariantMap layout READ layout CONSTANT)
    Q_PROPERTY(QStringList positionNames READ positionNames CONSTANT)
    Q_PROPERTY(QStringList styleNames READ styleNames CONSTANT)

public:
    explicit BarService(SettingsService* settings, QObject* parent = nullptr);

    QString position() const { return m_position; }
    bool vertical() const { return m_position == QStringLiteral("left") || m_position == QStringLiteral("right"); }
    int thickness() const { return thicknessFor(m_position); }
    int reserved() const;
    QString style() const { return m_style; }
    int styleIndex() const;
    bool autoHide() const;
    int workspaceCount() const;
    QVariantMap layout() const { return m_layout; }
    QStringList positionNames() const;
    QStringList styleNames() const;

    Q_INVOKABLE int thicknessFor(const QString& pos) const;
    Q_INVOKABLE int stripSizeFor(const QString& pos, qreal uiScale) const;
    Q_INVOKABLE void setPosition(const QString& pos);
    Q_INVOKABLE void cyclePosition();
    Q_INVOKABLE void setStyle(const QString& style);
    Q_INVOKABLE void cycleStyle();
    Q_INVOKABLE void toggleAutoHide();

Q_SIGNALS:
    void changed();

private:
    SettingsService* m_settings = nullptr;
    QString m_position = QStringLiteral("top");
    QString m_style = QStringLiteral("transparent");
    QVariantMap m_layout;
};

} // namespace SekaiPop::Services
