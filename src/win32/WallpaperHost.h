#pragma once

#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QSet>
#include <QWindow>
#include <windows.h>

namespace SekaiPop::Win32 {

/**
 * Keeps desktop wallpaper QQuickWindows pinned permanently behind every regular
 * application window via Win32 HWND_BOTTOM + WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE.
 * Works identically whether explorer.exe is running or terminated manually.
 */
class WallpaperHost final : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT

public:
    explicit WallpaperHost(QObject* parent = nullptr);
    ~WallpaperHost() override;

    Q_INVOKABLE void registerWallpaperWindow(QWindow* window);
    Q_INVOKABLE void unregisterWallpaperWindow(QWindow* window);
    Q_INVOKABLE void registerVisualizerWindow(QWindow* window, bool focusable);
    Q_INVOKABLE void registerWidgetWindow(QWindow* window, bool clickThrough, bool focusable);
    Q_INVOKABLE void registerBottomDockWindow(QWindow* window, bool focusable);
    Q_INVOKABLE void repinAll();
    void setEditModeSuspended(bool suspended);
    bool isEditModeSuspended() const { return m_editModeSuspended; }

    /**
     * Optional helper for environments running explorer.exe: spawns/locates the
     * desktop WorkerW handle behind SHELLDLL_DefView if present.
     */
    static HWND findExplorerWorkerW();

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

private:
    void applyBottomStyle(HWND hwnd);
    void applyWidgetStyle(HWND hwnd, bool clickThrough, bool focusable);
    void repinWallpapersBottom();

    QSet<HWND> m_wallpaperHwnds;
    QSet<HWND> m_visualizerHwnds;
    QSet<HWND> m_widgetHwnds;
    QSet<HWND> m_bottomDockHwnds;
    QSet<HWND> m_clickThroughWidgets;
    QSet<HWND> m_focusableWidgets;
    bool m_repinning = false;
    bool m_editModeSuspended = false;
};

} // namespace SekaiPop::Win32
