#pragma once

#include <QAbstractNativeEventFilter>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVector>
#include <windows.h>

struct IVirtualDesktopManager;

namespace SekaiPop::Win32 {

/**
 * Tracks foreground and top-level Windows application windows in real time
 * using SetWinEventHook + EnumWindows + DwmGetWindowAttribute(DWMWA_CLOAKED).
 */
class WindowTracker final : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString activeApp READ activeApp NOTIFY activeWindowChanged)
    Q_PROPERTY(QString activeTitle READ activeTitle NOTIFY activeWindowChanged)
    Q_PROPERTY(QString activeIcon READ activeIcon NOTIFY activeWindowChanged)
    Q_PROPERTY(qulonglong activeHwnd READ activeHwnd NOTIFY activeWindowChanged)
    Q_PROPERTY(bool hasActiveWindow READ hasActiveWindow NOTIFY activeWindowChanged)
    Q_PROPERTY(QVariantList openWindows READ openWindows NOTIFY openWindowsChanged)
    Q_PROPERTY(int activeWorkspace READ activeWorkspace NOTIFY activeWorkspaceChanged)
    Q_PROPERTY(QVariantList occupiedWorkspaces READ occupiedWorkspaces NOTIFY openWindowsChanged)

public:
    explicit WindowTracker(QObject* parent = nullptr);
    ~WindowTracker() override;

    QString activeApp() const { return m_activeApp; }
    QString activeTitle() const { return m_activeTitle; }
    QString activeIcon() const { return m_activeIcon; }
    qulonglong activeHwnd() const { return static_cast<qulonglong>(m_activeHwnd); }
    bool hasActiveWindow() const { return m_activeHwnd != 0; }
    QVariantList openWindows() const { return m_openWindows; }
    int activeWorkspace() const { return m_activeWorkspace; }
    QVariantList occupiedWorkspaces() const;
    bool primaryOccluded() const { return m_primaryOccluded; }
    QSet<quintptr> fullscreenMonitors() const { return m_fullscreenMonitors; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void focusWindow(qulonglong hwnd);
    Q_INVOKABLE void minimizeWindow(qulonglong hwnd);
    Q_INVOKABLE void closeWindow(qulonglong hwnd);
    Q_INVOKABLE void switchWorkspace(int workspace);
    Q_INVOKABLE void moveWindowToWorkspace(qulonglong hwnd, int workspace);
    Q_INVOKABLE void restoreAllWorkspaces();

Q_SIGNALS:
    void activeWindowChanged();
    void openWindowsChanged();
    void activeWorkspaceChanged();
    void primaryOccludedChanged();
    void fullscreenMonitorsChanged();
    void windowGeometryChanged();

private:
    static void CALLBACK winEventProc(
        HWINEVENTHOOK hook,
        DWORD event,
        HWND hwnd,
        LONG idObject,
        LONG idChild,
        DWORD idEventThread,
        DWORD dwmsEventTime
    );

    void scheduleRefresh();
    void scheduleNameRefresh();
    void scheduleGeometryRefresh();
    void pollFullscreenMonitors();
    bool isTrackableAppWindow(HWND hwnd) const;
    QString resolveAppNameForHwnd(HWND hwnd) const;

    static WindowTracker* s_instance;

    QVector<HWINEVENTHOOK> m_hooks;
    QTimer m_debounce;
    QTimer m_nameDebounce;
    QTimer m_geomDebounce;
    QTimer m_reconcileTimer;
    QTimer m_fsPollTimer;
    int m_reconcileTicks = 0;
    DWORD m_ownPid = 0;
    IVirtualDesktopManager* m_vdm = nullptr;
    int m_lastLoggedCount = -1;

    quintptr m_activeHwnd = 0;
    QString m_activeApp;
    QString m_activeTitle;
    QString m_activeIcon;
    QVariantList m_openWindows;
    int m_activeWorkspace = 1;
    bool m_primaryOccluded = false;
    QSet<quintptr> m_fullscreenMonitors;
    QHash<quintptr, int> m_windowWorkspace;
    QHash<int, quintptr> m_lastFocusedByWs;
    QSet<quintptr> m_minimizedByWs;
};

} // namespace SekaiPop::Win32
