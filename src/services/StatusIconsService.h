#pragma once

#include <QObject>
#include <QString>

namespace SekaiPop::Services {

class QuickSource;

/**
 * Provides StatusPill properties (network + speaker icons, states, and tooltip text)
 * driven live by QuickSource's Win32 CoreAudio / WlanApi / IPHelper polling.
 */
class StatusIconsService final : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool networkShown READ networkShown NOTIFY changed)
    Q_PROPERTY(QString networkIcon READ networkIcon NOTIFY changed)
    Q_PROPERTY(QString networkState READ networkState NOTIFY changed)

    Q_PROPERTY(bool bluetoothShown READ bluetoothShown NOTIFY changed)
    Q_PROPERTY(QString bluetoothIcon READ bluetoothIcon NOTIFY changed)
    Q_PROPERTY(QString bluetoothState READ bluetoothState NOTIFY changed)

    Q_PROPERTY(bool speakerShown READ speakerShown NOTIFY changed)
    Q_PROPERTY(QString speakerIcon READ speakerIcon NOTIFY changed)
    Q_PROPERTY(QString speakerState READ speakerState NOTIFY changed)

    Q_PROPERTY(QString detailText READ detailText NOTIFY changed)

public:
    explicit StatusIconsService(QuickSource* quickSource, QObject* parent = nullptr);

    bool networkShown() const { return m_quick != nullptr; }
    QString networkIcon() const;
    QString networkState() const;

    bool bluetoothShown() const;
    QString bluetoothIcon() const;
    QString bluetoothState() const;

    bool speakerShown() const;
    QString speakerIcon() const;
    QString speakerState() const;

    QString detailText() const;

Q_SIGNALS:
    void changed();

private:
    QuickSource* m_quick = nullptr;
};

} // namespace SekaiPop::Services
