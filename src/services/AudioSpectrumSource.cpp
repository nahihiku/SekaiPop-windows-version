#include "AudioSpectrumSource.h"
#include "SettingsService.h"

#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <cmath>
#include <cstring>

namespace SekaiPop::Services {

namespace {

constexpr GUID kClsidMMDevEnum = { 0xbcde0395, 0xe52f, 0x467c, { 0x8e, 0x3d, 0xc4, 0x57, 0x92, 0x91, 0x69, 0x2e } };
constexpr GUID kIidIMMDevEnum = { 0xa95664d2, 0x9614, 0x4f35, { 0xa7, 0x46, 0xde, 0x8d, 0xb6, 0x36, 0x17, 0xe6 } };
constexpr GUID kIidIAudioClient = { 0x1cb9ad4c, 0xdbfa, 0x4c32, { 0xb1, 0x78, 0xc2, 0xf5, 0x68, 0xa7, 0x03, 0xb2 } };
constexpr GUID kIidIAudioCaptureClient = { 0xc8adbd64, 0xe71e, 0x48a0, { 0xa4, 0xde, 0x18, 0x5c, 0x39, 0x5c, 0xd3, 0x17 } };
constexpr GUID kSubTypeFloat = { 0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
constexpr GUID kSubTypePcm = { 0x00000001, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };

constexpr double kPi = 3.14159265358979323846;
constexpr int kWindowSamples = 512;

struct GoertzelCache {
    int sampleRate = 0;
    float hann[kWindowSamples] = {};
    float coeff[AudioSpectrumSource::kBands] = {};
    float scale[AudioSpectrumSource::kBands] = {};
    float windowed[kWindowSamples] = {};
    float ordered[kWindowSamples] = {};

    void ensureInit(int sr) {
        if (sampleRate == sr) {
            return;
        }
        sampleRate = sr;
        for (int i = 0; i < kWindowSamples; ++i) {
            hann[i] = static_cast<float>(0.5 * (1.0 - std::cos((2.0 * kPi * i) / (kWindowSamples - 1))));
        }
        constexpr double minFreq = 40.0;
        const double maxFreq = std::min(14000.0, sr * 0.45);
        const double logMin = std::log(minFreq);
        const double logMax = std::log(maxFreq);
        for (int b = 0; b < AudioSpectrumSource::kBands; ++b) {
            const double frac = static_cast<double>(b) / (AudioSpectrumSource::kBands - 1);
            const double freq = std::exp(logMin + frac * (logMax - logMin));
            const double omega = (2.0 * kPi * freq) / sr;
            coeff[b] = static_cast<float>(2.0 * std::cos(omega));
            const double tilt = 0.85 + 0.65 * frac;
            scale[b] = static_cast<float>(11.0 * tilt / (kWindowSamples * 0.25));
        }
    }
};

void computeGoertzelBands(const float* samples, int n, int sampleRate, GoertzelCache& cache, std::vector<double>& outBands) {
    outBands.assign(AudioSpectrumSource::kBands, 0.0);
    if (!samples || n != kWindowSamples || sampleRate <= 0) {
        return;
    }

    // Check RMS energy first to stay near zero on true digital silence.
    float sumSq = 0.0f;
    for (int i = 0; i < kWindowSamples; ++i) {
        const float s = samples[i];
        sumSq += s * s;
    }
    const float rms = std::sqrt(sumSq / static_cast<float>(kWindowSamples));
    if (rms < 0.00012f) {
        return;
    }

    cache.ensureInit(sampleRate);

    // Gentle automatic gain lift for low system volumes (e.g. 15-25% Windows volume / Voicemeeter).
    const float agc = std::min(6.0f, std::max(1.0f, 0.14f / std::max(0.015f, rms)));
    for (int i = 0; i < kWindowSamples; ++i) {
        cache.windowed[i] = samples[i] * cache.hann[i] * agc;
    }

    constexpr float invLog12 = 1.0f / 2.48490665f; // 1 / ln(1 + 11)
    for (int b = 0; b < AudioSpectrumSource::kBands; ++b) {
        const float c = cache.coeff[b];
        float q1 = 0.0f;
        float q2 = 0.0f;
        for (int i = 0; i < kWindowSamples; ++i) {
            const float q0 = c * q1 - q2 + cache.windowed[i];
            q2 = q1;
            q1 = q0;
        }
        const float magSq = std::max(0.0f, q1 * q1 + q2 * q2 - q1 * q2 * c);
        const float val = std::log1p(std::sqrt(magSq) * cache.scale[b]) * invLog12;
        outBands[b] = static_cast<double>(std::min(1.0f, std::max(0.0f, val)));
    }
}

} // namespace

AudioSpectrumSource::AudioSpectrumSource(SettingsService* settings, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_rawBands(kBands, 0.0)
    , m_smooth(kBands, 0.0)
    , m_peaks(kBands, 0.0) {
    m_levelsList.reserve(kBands);
    m_peaksList.reserve(kBands);
    for (int i = 0; i < kBands; ++i) {
        m_levelsList.push_back(0.0);
        m_peaksList.push_back(0.0);
    }

    m_enabled = false;
    m_captureEnabled.store(false);

    m_clock.start();
    m_lastMs = m_clock.elapsed();

    m_frameTimer.setTimerType(Qt::PreciseTimer);
    m_frameTimer.setInterval(16);
    connect(&m_frameTimer, &QTimer::timeout, this, &AudioSpectrumSource::tickFrame);
}

AudioSpectrumSource::~AudioSpectrumSource() {
    m_stop.store(true);
    m_captureEnabled.store(false);
    if (m_captureThread.joinable()) {
        m_captureThread.join();
    }
}

void AudioSpectrumSource::setEnabled(bool enabled) {
    if (m_enabled == enabled) {
        return;
    }
    m_enabled = enabled;
    m_captureEnabled.store(enabled);
    if (m_enabled) {
        m_lastMs = m_clock.elapsed();
        if (!m_captureThread.joinable()) {
            m_stop.store(false);
            m_captureThread = std::thread([this]() { captureThreadLoop(); });
        }
        if (!m_occluded.load()) {
            m_frameTimer.start();
        }
    } else {
        m_frameTimer.stop();
        m_stop.store(true);
        if (m_captureThread.joinable()) {
            m_captureThread.join();
        }
        m_stop.store(false);
        m_fade = 0.0;
        Q_EMIT updated();
    }
    Q_EMIT enabledChanged();
}

void AudioSpectrumSource::setDesktopOccluded(bool occluded) {
    if (m_occluded.load() == occluded) {
        return;
    }
    m_occluded.store(occluded);
    if (!m_enabled) {
        return;
    }
    if (occluded) {
        m_frameTimer.stop();
    } else {
        m_lastMs = m_clock.elapsed();
        const int fps = qBound(15, m_targetFps.load(std::memory_order_acquire), 200);
        const int activeInterval = qBound(5, qRound(1000.0 / static_cast<double>(fps)), 66);
        m_frameTimer.setInterval(m_hasSignal.load() ? activeInterval : 33);
        m_frameTimer.start();
    }
}

void AudioSpectrumSource::setTargetFps(int fps) {
    const int clamped = qBound(15, fps, 200);
    m_targetFps.store(clamped, std::memory_order_release);
}

void AudioSpectrumSource::captureThreadLoop() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    while (!m_stop.load()) {
        if (!m_captureEnabled.load()) {
            Sleep(150);
            continue;
        }

        IMMDeviceEnumerator* enumerator = nullptr;
        IMMDevice* device = nullptr;
        IAudioClient* audioClient = nullptr;
        IAudioCaptureClient* captureClient = nullptr;
        WAVEFORMATEX* pwfx = nullptr;

        HRESULT hr = CoCreateInstance(kClsidMMDevEnum, nullptr, CLSCTX_ALL, kIidIMMDevEnum, reinterpret_cast<void**>(&enumerator));
        if (SUCCEEDED(hr) && enumerator) {
            hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
        }
        if (SUCCEEDED(hr) && device) {
            hr = device->Activate(kIidIAudioClient, CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&audioClient));
        }
        if (SUCCEEDED(hr) && audioClient) {
            hr = audioClient->GetMixFormat(&pwfx);
        }
        if (SUCCEEDED(hr) && audioClient && pwfx) {
            hr = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 400000, 0, pwfx, nullptr);
        }
        if (SUCCEEDED(hr) && audioClient) {
            hr = audioClient->GetService(kIidIAudioCaptureClient, reinterpret_cast<void**>(&captureClient));
        }

        if (SUCCEEDED(hr) && audioClient && captureClient && pwfx) {
            bool isFloat = (pwfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT);
            bool isPcm16 = (pwfx->wFormatTag == WAVE_FORMAT_PCM && pwfx->wBitsPerSample == 16);
            if (pwfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE && pwfx->cbSize >= 22) {
                const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(pwfx);
                isFloat = std::memcmp(&ext->SubFormat, &kSubTypeFloat, sizeof(GUID)) == 0;
                isPcm16 = std::memcmp(&ext->SubFormat, &kSubTypePcm, sizeof(GUID)) == 0 && pwfx->wBitsPerSample == 16;
            }

            if (SUCCEEDED(audioClient->Start())) {
                LPWSTR currentDeviceId = nullptr;
                device->GetId(&currentDeviceId);
                qInfo("[AudioSpectrumSource] WASAPI loopback started: ch=%d rate=%lu float=%d pcm16=%d",
                      static_cast<int>(pwfx->nChannels),
                      static_cast<unsigned long>(pwfx->nSamplesPerSec),
                      isFloat ? 1 : 0,
                      isPcm16 ? 1 : 0);

                std::vector<float> ring(kWindowSamples, 0.0f);
                GoertzelCache goertzelCache;
                int writePos = 0;
                int silentLoops = 0;
                int deviceCheckTicks = 0;
                ULONGLONG lastSignalTickMs = 0;
                std::vector<double> bands(kBands, 0.0);

                while (!m_stop.load() && m_captureEnabled.load()) {
                    if (m_occluded.load()) {
                        // Desktop visualizer is completely covered by a maximized/fullscreen window:
                        // flush stale loopback packets occasionally without running Goertzel DFT.
                        UINT32 drainLen = 0;
                        while (SUCCEEDED(captureClient->GetNextPacketSize(&drainLen)) && drainLen > 0) {
                            BYTE* dummy = nullptr;
                            UINT32 frames = 0;
                            DWORD flags = 0;
                            if (FAILED(captureClient->GetBuffer(&dummy, &frames, &flags, nullptr, nullptr))) {
                                break;
                            }
                            captureClient->ReleaseBuffer(frames);
                        }
                        Sleep(200);
                        continue;
                    }
                    Sleep(m_hasSignal.load() ? 18 : 80);

                    // Reconnect automatically if the user switches default output device (e.g. Speakers <-> Bluetooth <-> Voicemeeter).
                    if (++deviceCheckTicks >= 120) {
                        deviceCheckTicks = 0;
                        IMMDevice* curDev = nullptr;
                        if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &curDev)) && curDev) {
                            LPWSTR newId = nullptr;
                            if (SUCCEEDED(curDev->GetId(&newId)) && newId) {
                                const bool changed = (!currentDeviceId || wcscmp(currentDeviceId, newId) != 0);
                                CoTaskMemFree(newId);
                                if (changed) {
                                    curDev->Release();
                                    qInfo("[AudioSpectrumSource] Default audio endpoint changed; reconnecting WASAPI loopback");
                                    break;
                                }
                            }
                            curDev->Release();
                        }
                    }

                    UINT32 packetLen = 0;
                    if (FAILED(captureClient->GetNextPacketSize(&packetLen))) {
                        break;
                    }
                    bool gotAudio = false;
                    while (packetLen > 0) {
                        BYTE* data = nullptr;
                        UINT32 numFrames = 0;
                        DWORD flags = 0;
                        if (FAILED(captureClient->GetBuffer(&data, &numFrames, &flags, nullptr, nullptr))) {
                            break;
                        }
                        const int ch = std::max<int>(1, pwfx->nChannels);
                        if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0 || !data) {
                            for (UINT32 f = 0; f < numFrames; ++f) {
                                ring[writePos] = 0.0f;
                                writePos = (writePos + 1) % kWindowSamples;
                            }
                        } else if (isFloat) {
                            gotAudio = true;
                            const float* flt = reinterpret_cast<const float*>(data);
                            // Normalize by active channels so 8-channel virtual devices (e.g. Voicemeeter 7.1)
                            // playing stereo on ch 0..1 do not lose 75% amplitude.
                            int activeCh = 0;
                            for (int c = 0; c < ch; ++c) {
                                float peakC = 0.0f;
                                const UINT32 step = std::max<UINT32>(1, numFrames / 16);
                                for (UINT32 f = 0; f < numFrames; f += step) {
                                    peakC = std::max(peakC, std::fabs(flt[f * ch + c]));
                                }
                                if (peakC > 1e-5f) {
                                    ++activeCh;
                                }
                            }
                            const float denom = static_cast<float>(std::max(1, activeCh > 0 ? activeCh : std::min(ch, 2)));
                            for (UINT32 f = 0; f < numFrames; ++f) {
                                float mono = 0.0f;
                                for (int c = 0; c < ch; ++c) {
                                    mono += flt[f * ch + c];
                                }
                                ring[writePos] = mono / denom;
                                writePos = (writePos + 1) % kWindowSamples;
                            }
                        } else if (isPcm16) {
                            gotAudio = true;
                            const short* pcm = reinterpret_cast<const short*>(data);
                            const float denom = static_cast<float>(std::min(ch, 2));
                            for (UINT32 f = 0; f < numFrames; ++f) {
                                float mono = 0.0f;
                                for (int c = 0; c < ch; ++c) {
                                    mono += static_cast<float>(pcm[f * ch + c]) / 32768.0f;
                                }
                                ring[writePos] = mono / denom;
                                writePos = (writePos + 1) % kWindowSamples;
                            }
                        }
                        captureClient->ReleaseBuffer(numFrames);
                        if (FAILED(captureClient->GetNextPacketSize(&packetLen))) {
                            break;
                        }
                    }

                    if (gotAudio) {
                        silentLoops = 0;
                        const int tail = kWindowSamples - writePos;
                        std::memcpy(goertzelCache.ordered, ring.data() + writePos, static_cast<size_t>(tail) * sizeof(float));
                        if (writePos > 0) {
                            std::memcpy(goertzelCache.ordered + tail, ring.data(), static_cast<size_t>(writePos) * sizeof(float));
                        }
                        computeGoertzelBands(goertzelCache.ordered, kWindowSamples, static_cast<int>(pwfx->nSamplesPerSec), goertzelCache, bands);
                        double peak = 0.0;
                        for (double b : bands) {
                            if (b > peak) peak = b;
                        }
                        const ULONGLONG nowTick = GetTickCount64();
                        if (peak > 0.005) {
                            lastSignalTickMs = nowTick;
                            m_hasSignal.store(true);
                        } else if (lastSignalTickMs == 0 || (nowTick - lastSignalTickMs) > 650) {
                            m_hasSignal.store(false);
                        }
                        std::lock_guard<std::mutex> lock(m_bandMutex);
                        m_rawBands = bands;
                    } else {
                        ++silentLoops;
                        const ULONGLONG nowTick = GetTickCount64();
                        if (silentLoops > 18 && (lastSignalTickMs == 0 || (nowTick - lastSignalTickMs) > 650)) {
                            m_hasSignal.store(false);
                            std::lock_guard<std::mutex> lock(m_bandMutex);
                            std::fill(m_rawBands.begin(), m_rawBands.end(), 0.0);
                        }
                    }
                }
                if (currentDeviceId) {
                    CoTaskMemFree(currentDeviceId);
                }
                audioClient->Stop();
            }
        } else {
            qWarning("[AudioSpectrumSource] WASAPI loopback init failed: hr=0x%08lX", static_cast<unsigned long>(hr));
        }

        if (pwfx) CoTaskMemFree(pwfx);
        if (captureClient) captureClient->Release();
        if (audioClient) audioClient->Release();
        if (device) device->Release();
        if (enumerator) enumerator->Release();

        if (!m_stop.load()) {
            Sleep(500);
        }
    }

    CoUninitialize();
}

void AudioSpectrumSource::tickFrame() {
    if (m_occluded.load()) {
        return;
    }
    const qint64 nowMs = m_clock.elapsed();
    const double dt = qBound(0.005, static_cast<double>(nowMs - m_lastMs) / 1000.0, 0.1);
    m_lastMs = nowMs;
    m_time += dt;

    std::vector<double> target(kBands, 0.0);
    {
        std::lock_guard<std::mutex> lock(m_bandMutex);
        target = m_rawBands;
    }

    const bool liveAudio = m_hasSignal.load();
    m_activeAudio = liveAudio;
    // Configurable frame pacing up to 200 Hz for active music/audio, ~16 Hz for gentle ambient breathing.
    const int fps = qBound(15, m_targetFps.load(std::memory_order_acquire), 200);
    const int activeInterval = qBound(5, qRound(1000.0 / static_cast<double>(fps)), 66);
    const int desiredInterval = liveAudio ? activeInterval : 60;
    if (m_frameTimer.interval() != desiredInterval) {
        m_frameTimer.setInterval(desiredInterval);
    }
    const bool ambientOn = !m_settings || m_settings->flag(QStringLiteral("widgets.visualizerAmbient"));

    const double fadeTarget = (liveAudio || ambientOn) ? 1.0 : 0.0;
    const double fadeRate = fadeTarget > m_fade ? 6.0 : 2.5;
    m_fade = qBound<qreal>(0.0, m_fade + (fadeTarget - m_fade) * std::min(1.0, fadeRate * dt), 1.0);

    double bassSum = 0.0;
    for (int i = 0; i < kBands; ++i) {
        double want = target[i];
        if (!liveAudio && ambientOn) {
            // Subtle breathing ambient swell when silent so desktop waves match Image 2 gracefully.
            const double x = static_cast<double>(i) / (kBands - 1);
            want = 0.06 + 0.06 * std::sin(m_time * 1.15 + x * 4.2) * std::cos(m_time * 0.55 - x * 2.1);
            want = qBound(0.0, want, 0.25);
        }

        const double rate = want > m_smooth[i] ? 22.0 : 7.5;
        m_smooth[i] += (want - m_smooth[i]) * std::min(1.0, rate * dt);

        if (m_smooth[i] >= m_peaks[i]) {
            m_peaks[i] = m_smooth[i];
        } else {
            m_peaks[i] = std::max(m_smooth[i], m_peaks[i] - 0.35 * dt);
        }

        if (i < 8) {
            bassSum += m_smooth[i];
        }
        m_levelsList[i] = m_smooth[i];
        m_peaksList[i] = m_peaks[i];
    }

    m_bass = qBound<qreal>(0.0, bassSum / 8.0, 1.0);
    Q_EMIT updated();
}

} // namespace SekaiPop::Services
