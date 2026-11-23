#pragma once

#include "QsTypes.h"

class QQmlEngine;

#include <QColor>
#include <QObject>
#include <QPointer>
#include <QQmlListProperty>
#include <QQmlParserStatus>
#include <QQuickGraphicsDevice>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSet>
#include <QTimer>

namespace SekaiPop::Win32 {
class AppBarManager;
class WallpaperHost;
}

namespace SekaiPop::Quickshell {

class QsAnchors : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool top READ top WRITE setTop NOTIFY changed)
    Q_PROPERTY(bool bottom READ bottom WRITE setBottom NOTIFY changed)
    Q_PROPERTY(bool left READ left WRITE setLeft NOTIFY changed)
    Q_PROPERTY(bool right READ right WRITE setRight NOTIFY changed)

public:
    using QObject::QObject;

    bool top() const { return m_top; }
    void setTop(bool v) { if (m_top != v) { m_top = v; Q_EMIT changed(); } }

    bool bottom() const { return m_bottom; }
    void setBottom(bool v) { if (m_bottom != v) { m_bottom = v; Q_EMIT changed(); } }

    bool left() const { return m_left; }
    void setLeft(bool v) { if (m_left != v) { m_left = v; Q_EMIT changed(); } }

    bool right() const { return m_right; }
    void setRight(bool v) { if (m_right != v) { m_right = v; Q_EMIT changed(); } }

Q_SIGNALS:
    void changed();

private:
    bool m_top = false;
    bool m_bottom = false;
    bool m_left = false;
    bool m_right = false;
};

class QsMargins : public QObject {
    Q_OBJECT
    Q_PROPERTY(qreal top READ top WRITE setTop NOTIFY changed)
    Q_PROPERTY(qreal bottom READ bottom WRITE setBottom NOTIFY changed)
    Q_PROPERTY(qreal left READ left WRITE setLeft NOTIFY changed)
    Q_PROPERTY(qreal right READ right WRITE setRight NOTIFY changed)

public:
    using QObject::QObject;

    qreal top() const { return m_top; }
    void setTop(qreal v) { if (!qFuzzyCompare(m_top, v)) { m_top = v; Q_EMIT changed(); } }

    qreal bottom() const { return m_bottom; }
    void setBottom(qreal v) { if (!qFuzzyCompare(m_bottom, v)) { m_bottom = v; Q_EMIT changed(); } }

    qreal left() const { return m_left; }
    void setLeft(qreal v) { if (!qFuzzyCompare(m_left, v)) { m_left = v; Q_EMIT changed(); } }

    qreal right() const { return m_right; }
    void setRight(qreal v) { if (!qFuzzyCompare(m_right, v)) { m_right = v; Q_EMIT changed(); } }

Q_SIGNALS:
    void changed();

private:
    qreal m_top = 0;
    qreal m_bottom = 0;
    qreal m_left = 0;
    qreal m_right = 0;
};

class QsRegion : public QObject {
    Q_OBJECT
    Q_PROPERTY(QQuickItem* item READ item WRITE setItem NOTIFY regionUpdated)
    Q_PROPERTY(int intersection READ intersection WRITE setIntersection NOTIFY regionUpdated)
    Q_PROPERTY(qreal x READ x WRITE setX NOTIFY regionUpdated)
    Q_PROPERTY(qreal y READ y WRITE setY NOTIFY regionUpdated)
    Q_PROPERTY(qreal width READ width WRITE setWidth NOTIFY regionUpdated)
    Q_PROPERTY(qreal height READ height WRITE setHeight NOTIFY regionUpdated)
    Q_PROPERTY(qreal radius READ radius WRITE setRadius NOTIFY regionUpdated)
    Q_PROPERTY(QQmlListProperty<SekaiPop::Quickshell::QsRegion> regions READ regions)
    Q_CLASSINFO("DefaultProperty", "regions")

public:
    explicit QsRegion(QObject* parent = nullptr) : QObject(parent) {}

    QQuickItem* item() const { return m_item; }
    void setItem(QQuickItem* it);

    int intersection() const { return m_intersection; }
    void setIntersection(int i) {
        if (m_intersection != i) {
            m_intersection = i;
            Q_EMIT regionUpdated();
        }
    }

    qreal x() const { return m_x; }
    void setX(qreal v) { if (!qFuzzyCompare(m_x, v)) { m_x = v; Q_EMIT regionUpdated(); } }

    qreal y() const { return m_y; }
    void setY(qreal v) { if (!qFuzzyCompare(m_y, v)) { m_y = v; Q_EMIT regionUpdated(); } }

    qreal width() const { return m_width; }
    void setWidth(qreal v) { if (!qFuzzyCompare(m_width, v)) { m_width = v; Q_EMIT regionUpdated(); } }

    qreal height() const { return m_height; }
    void setHeight(qreal v) { if (!qFuzzyCompare(m_height, v)) { m_height = v; Q_EMIT regionUpdated(); } }

    qreal radius() const { return m_radius; }
    void setRadius(qreal v) { if (!qFuzzyCompare(m_radius, v)) { m_radius = v; Q_EMIT regionUpdated(); } }

    QQmlListProperty<QsRegion> regions();
    const QList<QsRegion*>& childRegions() const { return m_regions; }

    QRectF ownRect() const;
    bool containsPoint(const QPointF& windowPt) const;
    bool coversWholeRect(const QSizeF& winSize) const;

    Q_INVOKABLE void changed() { Q_EMIT regionUpdated(); }

Q_SIGNALS:
    void regionUpdated();

private:
    QPointer<QQuickItem> m_item;
    int m_intersection = Intersection::Combine;
    qreal m_x = 0;
    qreal m_y = 0;
    qreal m_width = 0;
    qreal m_height = 0;
    qreal m_radius = 0;
    QList<QsRegion*> m_regions;
};

class WlrLayer : public QObject {
    Q_OBJECT
public:
    enum Layer {
        Background = 0,
        Bottom = 1,
        Top = 2,
        Overlay = 3
    };
    Q_ENUM(Layer)
    using QObject::QObject;
};

class WlrKeyboardFocus : public QObject {
    Q_OBJECT
public:
    enum FocusMode {
        None = 0,
        Exclusive = 1,
        OnDemand = 2
    };
    Q_ENUM(FocusMode)
    using QObject::QObject;
};

class WlrLayershellAttached : public QObject {
    Q_OBJECT
    Q_PROPERTY(int layer READ layer WRITE setLayer NOTIFY layerChanged)
    Q_PROPERTY(QString namespace READ ns WRITE setNs NOTIFY nsChanged)
    Q_PROPERTY(int keyboardFocus READ keyboardFocus WRITE setKeyboardFocus NOTIFY keyboardFocusChanged)
    Q_PROPERTY(qreal exclusiveZone READ exclusiveZone WRITE setExclusiveZone NOTIFY exclusiveZoneChanged)
    Q_PROPERTY(QsAnchors* anchors READ anchors CONSTANT)
    Q_PROPERTY(QsMargins* margins READ margins CONSTANT)

public:
    explicit WlrLayershellAttached(QObject* parent = nullptr);

    int layer() const { return m_layer; }
    void setLayer(int l);

    QString ns() const { return m_ns; }
    void setNs(const QString& n);

    int keyboardFocus() const { return m_keyboardFocus; }
    void setKeyboardFocus(int f);

    qreal exclusiveZone() const { return m_exclusiveZone; }
    void setExclusiveZone(qreal z);

    QsAnchors* anchors() const { return m_anchors; }
    QsMargins* margins() const { return m_margins; }

Q_SIGNALS:
    void layerChanged();
    void nsChanged();
    void keyboardFocusChanged();
    void exclusiveZoneChanged();

private:
    int m_layer = WlrLayer::Top;
    QString m_ns;
    int m_keyboardFocus = WlrKeyboardFocus::None;
    qreal m_exclusiveZone = 0;
    QsAnchors* m_anchors = nullptr;
    QsMargins* m_margins = nullptr;
};

class WlrLayershell : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    static WlrLayershellAttached* qmlAttachedProperties(QObject* object) {
        return new WlrLayershellAttached(object);
    }
};

class QsPanelWindow : public QQuickWindow {
    Q_OBJECT
    Q_PROPERTY(bool visible READ qsVisible WRITE setQsVisible NOTIFY qsVisibleChanged)
    Q_PROPERTY(QVariant screen READ screenVariant WRITE setScreenVariant NOTIFY qsScreenChanged)
    Q_PROPERTY(QsAnchors* anchors READ anchors CONSTANT)
    Q_PROPERTY(QsMargins* margins READ margins CONSTANT)
    Q_PROPERTY(qreal implicitWidth READ implicitWidth WRITE setImplicitWidth NOTIFY implicitWidthChanged)
    Q_PROPERTY(qreal implicitHeight READ implicitHeight WRITE setImplicitHeight NOTIFY implicitHeightChanged)
    Q_PROPERTY(int exclusionMode READ exclusionMode WRITE setExclusionMode NOTIFY exclusionModeChanged)
    Q_PROPERTY(qreal exclusiveZone READ exclusiveZone WRITE setExclusiveZone NOTIFY exclusiveZoneChanged)
    Q_PROPERTY(bool aboveWindows READ aboveWindows WRITE setAboveWindows NOTIFY aboveWindowsChanged)
    Q_PROPERTY(bool focusable READ focusable WRITE setFocusable NOTIFY focusableChanged)
    Q_PROPERTY(QsRegion* mask READ mask WRITE setMask NOTIFY maskChanged)

public:
    explicit QsPanelWindow(QWindow* parent = nullptr);
    ~QsPanelWindow() override;

    bool qsVisible() const { return m_qsVisible; }
    void setQsVisible(bool v);

    QVariant screenVariant() const;
    void setScreenVariant(const QVariant& v);
    QsScreen* qsScreen() const { return m_qsScreen; }

    QsAnchors* anchors() const { return m_anchors; }
    QsMargins* margins() const { return m_margins; }

    qreal implicitWidth() const { return m_implicitWidth; }
    void setImplicitWidth(qreal w);

    qreal implicitHeight() const { return m_implicitHeight; }
    void setImplicitHeight(qreal h);

    int exclusionMode() const { return m_exclusionMode; }
    void setExclusionMode(int m);

    qreal exclusiveZone() const { return m_exclusiveZone; }
    void setExclusiveZone(qreal z);

    bool aboveWindows() const { return m_aboveWindows; }
    void setAboveWindows(bool a);

    bool focusable() const { return m_focusable; }
    void setFocusable(bool f);

    QsRegion* mask() const { return m_mask; }
    void setMask(QsRegion* r);

    WlrLayershellAttached* layerShell() const;
    bool isClickThrough() const { return m_isClickThrough; }
    void scheduleSync();
    void syncNow();
    void updateHitTest(const QPoint& globalLogicalCursor);

Q_SIGNALS:
    void qsVisibleChanged();
    void qsScreenChanged();
    void implicitWidthChanged();
    void implicitHeightChanged();
    void exclusionModeChanged();
    void exclusiveZoneChanged();
    void aboveWindowsChanged();
    void focusableChanged();
    void maskChanged();

private:
    void setClickThrough(bool transparent);

    QPointer<QsScreen> m_qsScreen;
    QsAnchors* m_anchors = nullptr;
    QsMargins* m_margins = nullptr;
    qreal m_implicitWidth = 0;
    qreal m_implicitHeight = 0;
    int m_exclusionMode = ExclusionMode::Auto;
    qreal m_exclusiveZone = 0;
    bool m_aboveWindows = true;
    bool m_focusable = false;
    QPointer<QsRegion> m_mask;
    bool m_qsVisible = true;
    bool m_syncPending = false;
    bool m_isClickThrough = false;
    bool m_registeredAsBar = false;
    QString m_lastBarEdge;
    int m_lastBarZone = -1;
    int m_lastKbFocus = -1;
    QTimer m_releaseTimer;
};

class QsPopupAnchorRect : public QObject {
    Q_OBJECT
    Q_PROPERTY(qreal x READ x WRITE setX NOTIFY changed)
    Q_PROPERTY(qreal y READ y WRITE setY NOTIFY changed)
    Q_PROPERTY(qreal width READ width WRITE setWidth NOTIFY changed)
    Q_PROPERTY(qreal height READ height WRITE setHeight NOTIFY changed)

public:
    using QObject::QObject;
    qreal x() const { return m_x; }
    void setX(qreal v) { if (!qFuzzyCompare(m_x, v)) { m_x = v; Q_EMIT changed(); } }
    qreal y() const { return m_y; }
    void setY(qreal v) { if (!qFuzzyCompare(m_y, v)) { m_y = v; Q_EMIT changed(); } }
    qreal width() const { return m_w; }
    void setWidth(qreal v) { if (!qFuzzyCompare(m_w, v)) { m_w = v; Q_EMIT changed(); } }
    qreal height() const { return m_h; }
    void setHeight(qreal v) { if (!qFuzzyCompare(m_h, v)) { m_h = v; Q_EMIT changed(); } }

Q_SIGNALS:
    void changed();

private:
    qreal m_x = 0;
    qreal m_y = 0;
    qreal m_w = 0;
    qreal m_h = 0;
};

class QsPopupAnchor : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject* window READ window WRITE setWindow NOTIFY windowChanged)
    Q_PROPERTY(QQuickItem* item READ item WRITE setItem NOTIFY itemChanged)
    Q_PROPERTY(QRectF rect READ rect WRITE setRect NOTIFY rectChanged)
    Q_PROPERTY(int edges READ edges WRITE setEdges NOTIFY edgesChanged)
    Q_PROPERTY(int gravity READ gravity WRITE setGravity NOTIFY gravityChanged)
    Q_PROPERTY(int adjustment READ adjustment WRITE setAdjustment NOTIFY adjustmentChanged)

public:
    explicit QsPopupAnchor(QObject* parent = nullptr);

    QObject* window() const { return m_window; }
    void setWindow(QObject* w) { if (m_window != w) { m_window = w; Q_EMIT windowChanged(); Q_EMIT changed(); } }

    QQuickItem* item() const { return m_item; }
    void setItem(QQuickItem* it) { if (m_item != it) { m_item = it; Q_EMIT itemChanged(); Q_EMIT changed(); } }

    QRectF rect() const { return m_rect; }
    void setRect(const QRectF& r) { if (m_rect != r) { m_rect = r; Q_EMIT rectChanged(); Q_EMIT changed(); } }

    int edges() const { return m_edges; }
    void setEdges(int e) { if (m_edges != e) { m_edges = e; Q_EMIT edgesChanged(); Q_EMIT changed(); } }

    int gravity() const { return m_gravity; }
    void setGravity(int g) { if (m_gravity != g) { m_gravity = g; Q_EMIT gravityChanged(); Q_EMIT changed(); } }

    int adjustment() const { return m_adjustment; }
    void setAdjustment(int a) { if (m_adjustment != a) { m_adjustment = a; Q_EMIT adjustmentChanged(); Q_EMIT changed(); } }

    Q_INVOKABLE void updateAnchor() { Q_EMIT changed(); }

Q_SIGNALS:
    void windowChanged();
    void itemChanged();
    void rectChanged();
    void edgesChanged();
    void gravityChanged();
    void adjustmentChanged();
    void changed();

private:
    QPointer<QObject> m_window;
    QPointer<QQuickItem> m_item;
    QRectF m_rect;
    int m_edges = Edges::Bottom | Edges::Right;
    int m_gravity = Edges::Bottom | Edges::Right;
    int m_adjustment = PopupAdjustment::None;
};

class QsPopupWindow : public QQuickWindow {
    Q_OBJECT
    Q_PROPERTY(QsPopupAnchor* anchor READ anchor CONSTANT)
    Q_PROPERTY(qreal implicitWidth READ implicitWidth WRITE setImplicitWidth NOTIFY implicitWidthChanged)
    Q_PROPERTY(qreal implicitHeight READ implicitHeight WRITE setImplicitHeight NOTIFY implicitHeightChanged)
    Q_PROPERTY(QsRegion* mask READ mask WRITE setMask NOTIFY maskChanged)

public:
    explicit QsPopupWindow(QWindow* parent = nullptr);

    QsPopupAnchor* anchor() const { return m_anchor; }

    qreal implicitWidth() const { return m_implicitWidth; }
    void setImplicitWidth(qreal w);

    qreal implicitHeight() const { return m_implicitHeight; }
    void setImplicitHeight(qreal h);

    QsRegion* mask() const { return m_mask; }
    void setMask(QsRegion* r);

Q_SIGNALS:
    void implicitWidthChanged();
    void implicitHeightChanged();
    void maskChanged();

private:
    void syncPlacement();

    QsPopupAnchor* m_anchor = nullptr;
    qreal m_implicitWidth = 200;
    qreal m_implicitHeight = 200;
    QPointer<QsRegion> m_mask;
    QTimer m_releaseTimer;
};

class QsFloatingWindow : public QQuickWindow {
    Q_OBJECT
    Q_PROPERTY(qreal implicitWidth READ implicitWidth WRITE setImplicitWidth NOTIFY implicitWidthChanged)
    Q_PROPERTY(qreal implicitHeight READ implicitHeight WRITE setImplicitHeight NOTIFY implicitHeightChanged)
    Q_PROPERTY(QSize minimumSize READ minimumSize WRITE setMinimumSize)
    Q_PROPERTY(QSize maximumSize READ maximumSize WRITE setMaximumSize)
    Q_PROPERTY(QVariant screen READ screenVariant WRITE setScreenVariant NOTIFY qsScreenChanged)

public:
    explicit QsFloatingWindow(QWindow* parent = nullptr);

    qreal implicitWidth() const { return m_implicitWidth; }
    void setImplicitWidth(qreal w);

    qreal implicitHeight() const { return m_implicitHeight; }
    void setImplicitHeight(qreal h);

    QVariant screenVariant() const;
    void setScreenVariant(const QVariant& v);

Q_SIGNALS:
    void implicitWidthChanged();
    void implicitHeightChanged();
    void qsScreenChanged();

private:
    QPointer<QsScreen> m_qsScreen;
    qreal m_implicitWidth = 640;
    qreal m_implicitHeight = 480;
};

class WlSessionLockSurface : public QsPanelWindow {
    Q_OBJECT
public:
    explicit WlSessionLockSurface(QWindow* parent = nullptr);
};

class WlSessionLock : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool locked READ locked WRITE setLocked NOTIFY lockedChanged)
    Q_PROPERTY(bool secure READ locked NOTIFY lockedChanged)
    Q_PROPERTY(QQmlComponent* surface READ surface WRITE setSurface NOTIFY surfaceChanged)
    Q_CLASSINFO("DefaultProperty", "surface")

public:
    using QObject::QObject;

    bool locked() const { return false; }
    void setLocked(bool) {
        // Custom Wayland lockscreen is permanently disabled on Windows; native LockWorkStation() is used instead.
        m_locked = false;
    }

    QQmlComponent* surface() const { return m_surface; }
    void setSurface(QQmlComponent* s) {
        if (m_surface != s) {
            m_surface = s;
            Q_EMIT surfaceChanged();
        }
    }

Q_SIGNALS:
    void lockedChanged();
    void surfaceChanged();

private:
    bool m_locked = false;
    QPointer<QQmlComponent> m_surface;
};

class QsScreencopyView : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QVariant captureSource READ captureSource WRITE setCaptureSource NOTIFY captureSourceChanged)
    Q_PROPERTY(bool live READ live WRITE setLive NOTIFY liveChanged)
    Q_PROPERTY(bool paintCursors READ paintCursors WRITE setPaintCursors NOTIFY paintCursorsChanged)
    Q_PROPERTY(bool hasContent READ hasContent NOTIFY hasContentChanged)
    Q_PROPERTY(QSizeF sourceSize READ sourceSize NOTIFY sourceSizeChanged)

public:
    explicit QsScreencopyView(QQuickItem* parent = nullptr) : QQuickItem(parent) {}

    QVariant captureSource() const { return m_captureSource; }
    void setCaptureSource(const QVariant& s) {
        if (m_captureSource != s) {
            m_captureSource = s;
            Q_EMIT captureSourceChanged();
        }
    }

    bool live() const { return m_live; }
    void setLive(bool l) { if (m_live != l) { m_live = l; Q_EMIT liveChanged(); } }

    bool paintCursors() const { return m_paintCursors; }
    void setPaintCursors(bool p) { if (m_paintCursors != p) { m_paintCursors = p; Q_EMIT paintCursorsChanged(); } }

    bool hasContent() const { return false; }
    QSizeF sourceSize() const { return QSizeF(320, 180); }

Q_SIGNALS:
    void captureSourceChanged();
    void liveChanged();
    void paintCursorsChanged();
    void hasContentChanged();
    void sourceSizeChanged();

private:
    QVariant m_captureSource;
    bool m_live = false;
    bool m_paintCursors = false;
};

class QsClippingRectangle : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(qreal radius READ radius WRITE setRadius NOTIFY radiusChanged)
    Q_PROPERTY(qreal topLeftRadius READ topLeftRadius WRITE setTopLeftRadius NOTIFY topLeftRadiusChanged)
    Q_PROPERTY(qreal topRightRadius READ topRightRadius WRITE setTopRightRadius NOTIFY topRightRadiusChanged)
    Q_PROPERTY(qreal bottomLeftRadius READ bottomLeftRadius WRITE setBottomLeftRadius NOTIFY bottomLeftRadiusChanged)
    Q_PROPERTY(qreal bottomRightRadius READ bottomRightRadius WRITE setBottomRightRadius NOTIFY bottomRightRadiusChanged)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)
    Q_PROPERTY(bool contentInsideBorder READ contentInsideBorder WRITE setContentInsideBorder NOTIFY contentInsideBorderChanged)

public:
    explicit QsClippingRectangle(QQuickItem* parent = nullptr);

    qreal radius() const { return m_radius; }
    void setRadius(qreal r);

    qreal topLeftRadius() const { return m_hasTopLeft ? m_topLeftRadius : m_radius; }
    void setTopLeftRadius(qreal r);

    qreal topRightRadius() const { return m_hasTopRight ? m_topRightRadius : m_radius; }
    void setTopRightRadius(qreal r);

    qreal bottomLeftRadius() const { return m_hasBottomLeft ? m_bottomLeftRadius : m_radius; }
    void setBottomLeftRadius(qreal r);

    qreal bottomRightRadius() const { return m_hasBottomRight ? m_bottomRightRadius : m_radius; }
    void setBottomRightRadius(qreal r);

    QColor color() const { return m_color; }
    void setColor(const QColor& c);

    bool contentInsideBorder() const { return m_contentInsideBorder; }
    void setContentInsideBorder(bool b);

Q_SIGNALS:
    void radiusChanged();
    void topLeftRadiusChanged();
    void topRightRadiusChanged();
    void bottomLeftRadiusChanged();
    void bottomRightRadiusChanged();
    void colorChanged();
    void contentInsideBorderChanged();

protected:
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* data) override;

private:
    qreal m_radius = 0.0;
    qreal m_topLeftRadius = 0.0;
    qreal m_topRightRadius = 0.0;
    qreal m_bottomLeftRadius = 0.0;
    qreal m_bottomRightRadius = 0.0;
    bool m_hasTopLeft = false;
    bool m_hasTopRight = false;
    bool m_hasBottomLeft = false;
    bool m_hasBottomRight = false;
    QColor m_color = Qt::transparent;
    bool m_contentInsideBorder = false;
};

class QsWindowManager : public QObject {
    Q_OBJECT
public:
    static QsWindowManager* instance();
    static void initSharedGraphics();
    static void releaseSharedGraphics();
    static QQuickGraphicsDevice sharedGraphicsDevice();
    static void shutdown();
    void init(Win32::WallpaperHost* wallpaperHost, Win32::AppBarManager* appBarManager);
    void setQmlEngine(QQmlEngine* engine);
    void setEditModeObject(QObject* editModeSingleton);
    void scheduleMemoryTrim(int delayMs = 2500);

    Win32::WallpaperHost* wallpaperHost() const { return m_wallpaperHost; }
    Win32::AppBarManager* appBarManager() const { return m_appBarManager; }

    void registerPanel(QsPanelWindow* win);
    void unregisterPanel(QsPanelWindow* win);
    void invalidatePanelStack(QsPanelWindow* win);
    bool isPanelStacked(QsPanelWindow* win) const { return m_normalStacked.contains(win); }
    void markHitTestDirty();

    bool isEditModeActive() const;
    int barReserveOnEdge(QsScreen* screen, const QString& edge) const;
    void setFullscreenMonitors(const QSet<quintptr>& monitors);

public Q_SLOTS:
    void scheduleRestack();
    void restackAll();

Q_SIGNALS:
    void uiReady();

private:
    explicit QsWindowManager(QObject* parent = nullptr);
    void onEditModeChanged();
    void pollHitTests();
    void performMemoryTrim();
    bool isPanelOnFullscreenMonitor(QsPanelWindow* w) const;

    Win32::WallpaperHost* m_wallpaperHost = nullptr;
    Win32::AppBarManager* m_appBarManager = nullptr;
    QPointer<QQmlEngine> m_qmlEngine;
    QPointer<QObject> m_editModeObj;
    QList<QsPanelWindow*> m_panels;
    QSet<QsPanelWindow*> m_normalStacked;
    QSet<quintptr> m_fullscreenMonitors;
    QSet<QsPanelWindow*> m_fullscreenHiddenPanels;
    QTimer m_hitTestTimer;
    QTimer m_trimTimer;
    QPoint m_lastCursorPos;
    int m_stationaryHitTicks = 0;
    bool m_hasCursorPos = false;
    bool m_hitTestDirty = true;
    bool m_hitTestQueued = false;
    bool m_restackPending = false;
    bool m_wasEditMode = false;
    bool m_editModeActive = false;
    bool m_uiReadyEmitted = false;
    bool m_wallpaperSeen = false;
    bool m_focusConnected = false;
};

} // namespace SekaiPop::Quickshell

QML_DECLARE_TYPEINFO(SekaiPop::Quickshell::WlrLayershell, QML_HAS_ATTACHED_PROPERTIES)
