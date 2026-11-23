#include "ResourceSource.h"

namespace SekaiPop::Services {

ResourceSource::ResourceSource(QObject* parent)
    : QObject(parent) {
    m_timer.setInterval(2000);
    connect(&m_timer, &QTimer::timeout, this, &ResourceSource::sampleNow);
    if (m_wanted) {
        sampleNow();
        m_timer.start();
    }
}

quint64 ResourceSource::fileTimeToUInt64(const FILETIME& ft) {
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    return uli.QuadPart;
}

void ResourceSource::setWanted(bool wanted) {
    if (m_wanted == wanted) {
        return;
    }
    m_wanted = wanted;
    if (m_wanted) {
        m_hasPrevCpu = false;
        sampleNow();
        m_timer.start();
    } else {
        m_timer.stop();
        m_hasPrevCpu = false;
    }
    Q_EMIT wantedChanged();
}

void ResourceSource::sampleNow() {
    FILETIME ftIdle = {}, ftKernel = {}, ftUser = {};
    if (GetSystemTimes(&ftIdle, &ftKernel, &ftUser)) {
        const quint64 idle = fileTimeToUInt64(ftIdle);
        const quint64 kernel = fileTimeToUInt64(ftKernel);
        const quint64 user = fileTimeToUInt64(ftUser);

        if (m_hasPrevCpu) {
            const quint64 dIdle = idle - m_prevIdle;
            const quint64 dTotal = (kernel - m_prevKernel) + (user - m_prevUser);
            if (dTotal > 0) {
                const qreal busy = static_cast<qreal>(dTotal - dIdle) / static_cast<qreal>(dTotal);
                m_cpuPercent = qBound<qreal>(0.0, qRound(busy * 100.0), 100.0);
            }
        }
        m_prevIdle = idle;
        m_prevKernel = kernel;
        m_prevUser = user;
        m_hasPrevCpu = true;
    }

    MEMORYSTATUSEX mem = {};
    mem.dwLength = sizeof(mem);
    if (GlobalMemoryStatusEx(&mem) && mem.ullTotalPhys > 0) {
        const quint64 used = mem.ullTotalPhys - mem.ullAvailPhys;
        const qreal gb = 1024.0 * 1024.0 * 1024.0;
        m_memPercent = qBound<qreal>(0.0, qRound(static_cast<qreal>(used) * 100.0 / static_cast<qreal>(mem.ullTotalPhys)), 100.0);
        m_memUsedLabel = QString::number(static_cast<qreal>(used) / gb, 'f', 1);
        m_memTotalLabel = QString::number(static_cast<qreal>(mem.ullTotalPhys) / gb, 'f', 1);
    }

    ULARGE_INTEGER freeBytes = {};
    ULARGE_INTEGER totalBytes = {};
    if (GetDiskFreeSpaceExW(L"C:\\", &freeBytes, &totalBytes, nullptr) && totalBytes.QuadPart > 0) {
        const quint64 usedDisk = totalBytes.QuadPart - freeBytes.QuadPart;
        const qreal gb = 1024.0 * 1024.0 * 1024.0;
        m_diskPercent = qBound<qreal>(0.0, qRound(static_cast<qreal>(usedDisk) * 100.0 / static_cast<qreal>(totalBytes.QuadPart)), 100.0);
        m_diskUsedLabel = QString::number(static_cast<qreal>(usedDisk) / gb, 'f', 0);
        m_diskTotalLabel = QString::number(static_cast<qreal>(totalBytes.QuadPart) / gb, 'f', 0);
    }

    const quint64 mins = GetTickCount64() / 60000ULL;
    const quint64 hrs = mins / 60;
    const quint64 days = hrs / 24;
    if (days > 0) {
        m_uptimeLabel = QStringLiteral("%1d %2h").arg(days).arg(hrs % 24);
    } else if (hrs > 0) {
        m_uptimeLabel = QStringLiteral("%1h %2m").arg(hrs).arg(mins % 60);
    } else {
        m_uptimeLabel = QStringLiteral("%1m").arg(mins);
    }

    Q_EMIT changed();
}

} // namespace SekaiPop::Services
