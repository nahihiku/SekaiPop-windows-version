#pragma once

#include <QAbstractNativeEventFilter>
#include <QHash>
#include <QObject>
#include <QRect>
#include <QSet>
#include <QString>
#include <QWindow>
#include <windows.h>

namespace SekaiPop::Win32 {

/**
 * Reserves desktop screen space along Top, Right, Bottom, or Left edges for
 * SekaiPop Bar windows.
 *
 * Dual-mode Windows reservation:
 *   1. Uses SHAppBarMessage (ABM_NEW/QUERYPOS/SETPOS) when explorer.exe's
 *      Shell_TrayWnd is active.
 *   2. Falls back to SystemParametersInfoW(SPI_SETWORKAREA) per monitor when
 *      explorer.exe has been stopped manually, restoring saved work areas
 *      automatically on shutdown or Explorer restoration.
 */
class AppBarManager final : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT

public:
    enum class Edge {
        Top,
        Right,
        Bottom,
        Left
    };
    Q_ENUM(Edge)

    explicit AppBarManager(QObject* parent = nullptr);
    ~AppBarManager() override;

    Q_INVOKABLE void configureBarWindow(QWindow* window, const QString& edgeName, int thicknessPx, bool autoHide);
    Q_INVOKABLE void unregisterBarWindow(QWindow* window);
    Q_INVOKABLE void setTaskbarHiddenMode(bool hidden);
    Q_INVOKABLE void reapplyAllReservations();
    Q_INVOKABLE void ensureWorkAreasValid();
    Q_INVOKABLE void restoreAllWorkAreas();
    void setFullscreenMonitors(const QSet<quintptr>& monitors);
    bool isMonitorFullscreen(HMONITOR hmon) const;
    void setEditModeSuspended(bool suspended);
    bool isEditModeSuspended() const { return m_editModeSuspended; }

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

    static Edge parseEdge(const QString& edgeName);

private:
    struct BarEntry {
        HWND hwnd = nullptr;
        QWindow* window = nullptr;
        HMONITOR monitor = nullptr;
        Edge edge = Edge::Top;
        int thicknessPx = 63;
        bool autoHide = false;
        bool registeredAppBar = false;
        bool positioned = false;
        bool hiddenForFullscreen = false;
        UINT callbackMsg = 0;
        RECT lastBounds = {};
    };

    void applyBarStyle(HWND hwnd);
    void applyReservation(BarEntry& entry);
    void removeAppBar(BarEntry& entry);
    void positionBarWindow(const BarEntry& entry, const RECT& rectPx);
    RECT computeReservedRect(const RECT& monitorRect, Edge edge, int thicknessPx) const;

    QHash<HWND, BarEntry> m_bars;
    QHash<HMONITOR, RECT> m_savedWorkAreas;
    QSet<quintptr> m_fullscreenMonitors;
    UINT m_nextCallbackMsg = WM_APP + 200;
    bool m_inApplyReservation = false;
    bool m_taskbarHiddenMode = true;
    bool m_editModeSuspended = false;
};

} // namespace SekaiPop::Win32
