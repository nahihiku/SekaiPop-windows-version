#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <windows.h>

namespace SekaiPop::Services {

/**
 * Polls Windows CPU and RAM usage via GetSystemTimes and GlobalMemoryStatusEx
 * (replaces Linux /proc/stat and /proc/meminfo reads).
 */
class ResourceSource final : public QObject {
    Q_OBJECT

    Q_PROPERTY(qreal cpuPercent READ cpuPercent NOTIFY changed)
    Q_PROPERTY(qreal memPercent READ memPercent NOTIFY changed)
    Q_PROPERTY(QString memUsedLabel READ memUsedLabel NOTIFY changed)
    Q_PROPERTY(QString memTotalLabel READ memTotalLabel NOTIFY changed)
    Q_PROPERTY(qreal diskPercent READ diskPercent NOTIFY changed)
    Q_PROPERTY(QString diskUsedLabel READ diskUsedLabel NOTIFY changed)
    Q_PROPERTY(QString diskTotalLabel READ diskTotalLabel NOTIFY changed)
    Q_PROPERTY(QString uptimeLabel READ uptimeLabel NOTIFY changed)
    Q_PROPERTY(bool wanted READ wanted WRITE setWanted NOTIFY wantedChanged)

public:
    explicit ResourceSource(QObject* parent = nullptr);

    qreal cpuPercent() const { return m_cpuPercent; }
    qreal memPercent() const { return m_memPercent; }
    QString memUsedLabel() const { return m_memUsedLabel; }
    QString memTotalLabel() const { return m_memTotalLabel; }
    qreal diskPercent() const { return m_diskPercent; }
    QString diskUsedLabel() const { return m_diskUsedLabel; }
    QString diskTotalLabel() const { return m_diskTotalLabel; }
    QString uptimeLabel() const { return m_uptimeLabel; }
    bool wanted() const { return m_wanted; }

    void setWanted(bool wanted);
    Q_INVOKABLE void sampleNow();

Q_SIGNALS:
    void changed();
    void wantedChanged();

private:
    static quint64 fileTimeToUInt64(const FILETIME& ft);

    QTimer m_timer;
    bool m_wanted = true;
    bool m_hasPrevCpu = false;
    quint64 m_prevIdle = 0;
    quint64 m_prevKernel = 0;
    quint64 m_prevUser = 0;

    qreal m_cpuPercent = 0.0;
    qreal m_memPercent = 0.0;
    QString m_memUsedLabel = QStringLiteral("--");
    QString m_memTotalLabel = QStringLiteral("--");
    qreal m_diskPercent = 0.0;
    QString m_diskUsedLabel = QStringLiteral("--");
    QString m_diskTotalLabel = QStringLiteral("--");
    QString m_uptimeLabel = QStringLiteral("0m");
};

} // namespace SekaiPop::Services
