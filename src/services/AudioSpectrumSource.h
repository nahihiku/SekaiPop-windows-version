#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

namespace SekaiPop::Services {

class SettingsService;

/**
 * Captures live desktop speaker output via Windows WASAPI Loopback
 * (AUDCLNT_STREAMFLAGS_LOOPBACK) and feeds 50 smoothed frequency bands,
 * peaks, bass energy, and timing to TideVisualizer and MediaPill.
 */
class AudioSpectrumSource final : public QObject {
    Q_OBJECT

    Q_PROPERTY(QVariantList levels READ levels NOTIFY updated)
    Q_PROPERTY(QVariantList peaks READ peaks NOTIFY updated)
    Q_PROPERTY(int bandCount READ bandCount CONSTANT)
    Q_PROPERTY(qreal bass READ bass NOTIFY updated)
    Q_PROPERTY(qreal fade READ fade NOTIFY updated)
    Q_PROPERTY(qreal time READ time NOTIFY updated)
    Q_PROPERTY(bool activeAudio READ activeAudio NOTIFY updated)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)

public:
    static constexpr int kBands = 50;

    explicit AudioSpectrumSource(SettingsService* settings, QObject* parent = nullptr);
    ~AudioSpectrumSource() override;

    QVariantList levels() const { return m_levelsList; }
    QVariantList peaks() const { return m_peaksList; }
    int bandCount() const { return kBands; }
    qreal bass() const { return m_bass; }
    qreal fade() const { return m_fade; }
    qreal time() const { return m_time; }
    bool activeAudio() const { return m_activeAudio; }
    bool enabled() const { return m_enabled; }

    void setEnabled(bool enabled);
    void setDesktopOccluded(bool occluded);
    void setTargetFps(int fps);

Q_SIGNALS:
    void updated();
    void enabledChanged();

private:
    void captureThreadLoop();
    void tickFrame();

    SettingsService* m_settings = nullptr;
    QTimer m_frameTimer;
    QElapsedTimer m_clock;

    std::atomic<bool> m_stop{ false };
    std::atomic<bool> m_captureEnabled{ false };
    std::atomic<bool> m_occluded{ false };
    std::atomic<int> m_targetFps{ 60 };
    std::thread m_captureThread;

    std::mutex m_bandMutex;
    std::vector<double> m_rawBands;
    std::atomic<bool> m_hasSignal{ false };

    std::vector<double> m_smooth;
    std::vector<double> m_peaks;
    QVariantList m_levelsList;
    QVariantList m_peaksList;

    qreal m_bass = 0.0;
    qreal m_fade = 0.0;
    qreal m_time = 0.0;
    qint64 m_lastMs = 0;
    bool m_activeAudio = false;
    bool m_enabled = false;
};

} // namespace SekaiPop::Services
