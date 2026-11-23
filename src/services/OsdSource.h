#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

namespace SekaiPop::Services {

/**
 * Lightweight On-Screen Display state controller for volume, mic, brightness,
 * and Night Light adjustments.
 */
class OsdSource final : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool open READ open NOTIFY changed)
    Q_PROPERTY(QString kind READ kind NOTIFY changed)
    Q_PROPERTY(QString icon READ icon NOTIFY changed)
    Q_PROPERTY(QString label READ label NOTIFY changed)
    Q_PROPERTY(qreal value READ value NOTIFY changed)
    Q_PROPERTY(bool muted READ muted NOTIFY changed)

public:
    explicit OsdSource(QObject* parent = nullptr);

    bool open() const { return m_open; }
    QString kind() const { return m_kind; }
    QString icon() const { return m_icon; }
    QString label() const { return m_label; }
    qreal value() const { return m_value; }
    bool muted() const { return m_muted; }

    Q_INVOKABLE void show(const QString& kind, const QString& icon, const QString& label, qreal value, bool muted = false);
    Q_INVOKABLE void hide();

Q_SIGNALS:
    void changed();

private:
    QTimer m_hideTimer;
    bool m_open = false;
    QString m_kind = QStringLiteral("volume");
    QString m_icon = QStringLiteral("volume_up");
    QString m_label = QStringLiteral("Volume");
    qreal m_value = 0.0;
    bool m_muted = false;
};

} // namespace SekaiPop::Services
