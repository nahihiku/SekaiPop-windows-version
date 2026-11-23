#pragma once

#include <QImage>
#include <QQuickItem>
#include <QString>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>

namespace SekaiPop::Services {

struct SharedGpuFrame {
    void* sharedHandle = nullptr; // Win32 HANDLE from IDXGIResource::GetSharedHandle
    int width = 0;
    int height = 0;
    int slot = 0;
    uint64_t poolId = 0;
    std::shared_ptr<void> poolKeepAlive; // keeps producer ID3D11Texture2D alive while consumer opens handle
};

/**
 * Native Windows Media Foundation video wallpaper surface rendered directly
 * into Qt Quick's Direct3D 11 scene graph via QSGSimpleTextureNode.
 *
 * - Decodes video silently on a single background worker thread (audio disabled).
 * - Paces frames accurately using Media Foundation presentation timestamps.
 * - Sleeps at 0% CPU when paused or hidden (Edit Desktop, Lockscreen, fullscreen).
 * - Caps staged frame size to 1920x1080 and keeps only a single double-buffer
 *   to prevent RAM spikes on multi-monitor / 4K video wallpapers.
 */
class VideoWallpaperItem : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged)
    Q_PROPERTY(bool restartOnPause READ restartOnPause WRITE setRestartOnPause NOTIFY restartOnPauseChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(int maxWidth READ maxWidth WRITE setMaxWidth NOTIFY maxWidthChanged)
    Q_PROPERTY(int maxHeight READ maxHeight WRITE setMaxHeight NOTIFY maxHeightChanged)
    Q_PROPERTY(int targetFps READ targetFps WRITE setTargetFps NOTIFY targetFpsChanged)
    Q_PROPERTY(bool smoothScaling READ smoothScaling WRITE setSmoothScaling NOTIFY smoothScalingChanged)

public:
    explicit VideoWallpaperItem(QQuickItem* parent = nullptr);
    ~VideoWallpaperItem() override;

    QString path() const { return m_path; }
    void setPath(const QString& path);

    bool paused() const { return m_paused.load(std::memory_order_acquire); }
    void setPaused(bool paused);

    bool restartOnPause() const { return m_restartOnPause.load(std::memory_order_acquire); }
    void setRestartOnPause(bool restart);

    bool playing() const { return m_playing.load(std::memory_order_acquire); }

    int maxWidth() const { return m_maxWidth.load(std::memory_order_acquire); }
    void setMaxWidth(int w);

    int maxHeight() const { return m_maxHeight.load(std::memory_order_acquire); }
    void setMaxHeight(int h);

    int targetFps() const { return m_targetFps.load(std::memory_order_acquire); }
    void setTargetFps(int fps);

    bool smoothScaling() const { return m_smoothScaling.load(std::memory_order_acquire); }
    void setSmoothScaling(bool smooth);

    bool isItemVisible() const { return m_itemVisible.load(std::memory_order_acquire); }
    void deliverSharedFrame(const QImage& frame);
    void deliverSharedGpuFrame(const SharedGpuFrame& gpuFrame);
    void notifyStreamPlaying(bool playing);
    void notifyStreamFailed();

    /**
     * Extract a crisp video frame (seeking to ~1s with fallback to 0s) as JPEG
     * using Windows Media Foundation. Returns true on success.
     */
    static bool extractFrameToJpeg(const QString& videoPath, const QString& outputPath, int maxWidth, int maxHeight, int quality = 85);

Q_SIGNALS:
    void pathChanged();
    void pausedChanged();
    void restartOnPauseChanged();
    void playingChanged();
    void maxWidthChanged();
    void maxHeightChanged();
    void targetFpsChanged();
    void smoothScalingChanged();
    void failed();

protected:
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* updatePaintNodeData) override;
    void itemChange(ItemChange change, const ItemChangeData& value) override;
    void releaseResources() override;

private Q_SLOTS:
    void onFrameAvailable();

private:
    void attachToSharedStream();
    void detachFromSharedStream();
    void notifyStreamControlChanged();
    void setPlayingState(bool playing);

    static QString normalizeMediaPath(const QString& raw);

    QString m_path;
    std::atomic<bool> m_paused{false};
    std::atomic<bool> m_restartOnPause{false};
    std::atomic<bool> m_playing{false};
    std::atomic<int> m_maxWidth{1920};
    std::atomic<int> m_maxHeight{1080};
    std::atomic<int> m_targetFps{60};
    std::atomic<bool> m_smoothScaling{false};
    std::atomic<bool> m_itemVisible{true};
    std::atomic<bool> m_updateScheduled{false};

    std::mutex m_frameMutex;
    QImage m_pendingFrame;
    SharedGpuFrame m_pendingGpuFrame;
    bool m_hasNewFrame{false};
    bool m_hasNewGpuFrame{false};
    bool m_clearTexture{false};
};

} // namespace SekaiPop::Services
