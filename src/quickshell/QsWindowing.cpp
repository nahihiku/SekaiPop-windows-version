#include "QsWindowing.h"
#include "../win32/AppBarManager.h"
#include "../win32/BootCurtainCache.h"
#include "../win32/WallpaperHost.h"
#include "../win32/Win32Utils.h"

#include <QCoreApplication>
#include <QCursor>
#include <QGuiApplication>
#include <QPixmapCache>
#include <QQmlEngine>
#include <QSGFlatColorMaterial>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QtQuick/private/qquickclipnode_p.h>
#include <QtQuick/private/qquickitem_p.h>
#include <rhi/qrhi.h>
#include <rhi/qrhi_platform.h>
#include <cmath>
#include <dxgi1_6.h>
#include <vector>
#include <windows.h>

namespace SekaiPop::Quickshell {

// ============================================================================
// QsRegion
// ============================================================================

void QsRegion::setItem(QQuickItem* it) {
    if (m_item == it) {
        return;
    }
    if (m_item) {
        disconnect(m_item, nullptr, this, nullptr);
    }
    m_item = it;
    if (m_item) {
        auto notify = [this]() {
            Q_EMIT regionUpdated();
            if (auto* mgr = QsWindowManager::instance()) {
                mgr->markHitTestDirty();
            }
        };
        connect(m_item, &QQuickItem::xChanged, this, notify);
        connect(m_item, &QQuickItem::yChanged, this, notify);
        connect(m_item, &QQuickItem::widthChanged, this, notify);
        connect(m_item, &QQuickItem::heightChanged, this, notify);
        connect(m_item, &QQuickItem::visibleChanged, this, notify);
    }
    Q_EMIT regionUpdated();
    if (auto* mgr = QsWindowManager::instance()) {
        mgr->markHitTestDirty();
    }
}

QQmlListProperty<QsRegion> QsRegion::regions() {
    return QQmlListProperty<QsRegion>(this, &m_regions);
}

QRectF QsRegion::ownRect() const {
    if (m_item) {
        if (!m_item->isVisible()) {
            return QRectF();
        }
        const qreal w = m_item->width();
        const qreal h = m_item->height();
        if (w <= 1.0 && h <= 1.0) {
            return QRectF();
        }
        return m_item->mapRectToScene(QRectF(0.0, 0.0, w, h));
    }
    if (m_width > 1.0 || m_height > 1.0) {
        return QRectF(m_x, m_y, m_width, m_height);
    }
    return QRectF();
}

bool QsRegion::isEmpty() const {
    if (!ownRect().isEmpty()) {
        return false;
    }
    for (const QsRegion* child : std::as_const(m_regions)) {
        if (!child) {
            continue;
        }
        if ((child->intersection() == Intersection::Combine || child->intersection() == Intersection::Xor) &&
            !child->isEmpty()) {
            return false;
        }
    }
    return true;
}

bool QsRegion::coversWholeRect(const QSizeF& winSize) const {
    if (winSize.width() <= 1.0 || winSize.height() <= 1.0) {
        return false;
    }
    const QRectF r = ownRect();
    if (!r.isValid() || r.left() > 2.0 || r.top() > 2.0 ||
        r.right() < winSize.width() - 2.0 || r.bottom() < winSize.height() - 2.0) {
        return false;
    }
    for (const QsRegion* child : std::as_const(m_regions)) {
        if (!child) continue;
        if (child->intersection() == Intersection::Subtract ||
            child->intersection() == Intersection::Intersect ||
            child->intersection() == Intersection::Xor) {
            if (!child->ownRect().isEmpty()) {
                return false;
            }
        }
    }
    return true;
}

bool QsRegion::containsPoint(const QPointF& windowPt) const {
    const QRectF r = ownRect();
    bool hit = r.isValid() && !r.isEmpty() && r.contains(windowPt);

    for (const QsRegion* child : std::as_const(m_regions)) {
        if (!child) {
            continue;
        }
        const bool childHit = child->containsPoint(windowPt);
        if (child->intersection() == Intersection::Subtract) {
            if (childHit) {
                hit = false;
            }
        } else if (child->intersection() == Intersection::Intersect) {
            hit = hit && childHit;
        } else if (child->intersection() == Intersection::Xor) {
            hit = (hit != childHit);
        } else {
            if (childHit) {
                hit = true;
            }
        }
    }
    return hit;
}

// ============================================================================
// WlrLayershellAttached
// ============================================================================

WlrLayershellAttached::WlrLayershellAttached(QObject* parent)
    : QObject(parent)
    , m_anchors(new QsAnchors(this))
    , m_margins(new QsMargins(this)) {
    if (auto* pw = qobject_cast<QsPanelWindow*>(parent)) {
        connect(this, &WlrLayershellAttached::layerChanged, pw, [pw]() {
            if (auto* mgr = QsWindowManager::instance()) {
                mgr->invalidatePanelStack(pw);
            }
            pw->scheduleSync();
        });
        connect(this, &WlrLayershellAttached::keyboardFocusChanged, pw, &QsPanelWindow::scheduleSync);
        connect(this, &WlrLayershellAttached::exclusiveZoneChanged, pw, &QsPanelWindow::scheduleSync);
    }
}

void WlrLayershellAttached::setLayer(int l) {
    if (m_layer != l) {
        m_layer = l;
        Q_EMIT layerChanged();
    }
}

void WlrLayershellAttached::setNs(const QString& n) {
    if (m_ns != n) {
        m_ns = n;
        Q_EMIT nsChanged();
    }
}

void WlrLayershellAttached::setKeyboardFocus(int f) {
    if (m_keyboardFocus != f) {
        m_keyboardFocus = f;
        Q_EMIT keyboardFocusChanged();
    }
}

void WlrLayershellAttached::setExclusiveZone(qreal z) {
    if (!qFuzzyCompare(m_exclusiveZone, z)) {
        m_exclusiveZone = z;
        Q_EMIT exclusiveZoneChanged();
    }
}

// ============================================================================
// QsPanelWindow
// ============================================================================

QsPanelWindow::QsPanelWindow(QWindow* parent)
    : QQuickWindow(parent)
    , m_anchors(new QsAnchors(this))
    , m_margins(new QsMargins(this)) {
    const QQuickGraphicsDevice sharedDev = QsWindowManager::sharedGraphicsDevice();
    if (!sharedDev.isNull()) {
        setGraphicsDevice(sharedDev);
    }
    setColor(Qt::transparent);
    setFlags(Qt::FramelessWindowHint | Qt::Tool);
    setPersistentSceneGraph(false);

    m_releaseTimer.setSingleShot(true);
    m_releaseTimer.setInterval(18000);
    connect(&m_releaseTimer, &QTimer::timeout, this, [this]() {
        if (m_qsVisible || QWindow::isVisible()) {
            return;
        }
        WlrLayershellAttached* ls = layerShell();
        const QString ns = ls ? ls->ns() : QString();
        const bool isAlwaysVisibleCore = (ns == QStringLiteral("sekaipop-wallpaper") ||
                                          ns == QStringLiteral("sekaipop-dock") ||
                                          ns == QStringLiteral("sekaipop-hotedge") ||
                                          ns == QStringLiteral("sekaipop-clock") ||
                                          ns == QStringLiteral("sekaipop-visualizer") ||
                                          m_registeredAsBar);
        if (!isAlwaysVisibleCore) {
            setPersistentSceneGraph(false);
            releaseResources();
            if (auto* mgr = QsWindowManager::instance()) {
                mgr->scheduleMemoryTrim(1500);
            }
        }
    });

    if (QsGlobal* g = QsGlobal::instance()) {
        m_qsScreen = g->primaryQsScreen();
        if (m_qsScreen && m_qsScreen->qscreen()) {
            QWindow::setScreen(m_qsScreen->qscreen());
        }
    }

    connect(m_anchors, &QsAnchors::changed, this, &QsPanelWindow::scheduleSync);
    connect(m_margins, &QsMargins::changed, this, &QsPanelWindow::scheduleSync);
    connect(this, &QWindow::visibleChanged, this, [this](bool) {
        scheduleSync();
    });

    if (auto* mgr = QsWindowManager::instance()) {
        mgr->registerPanel(this);
    }
    scheduleSync();
}

QsPanelWindow::~QsPanelWindow() {
    if (auto* mgr = QsWindowManager::instance()) {
        mgr->unregisterPanel(this);
    }
}

void QsPanelWindow::setQsVisible(bool v) {
    if (m_qsVisible != v) {
        m_qsVisible = v;
        Q_EMIT qsVisibleChanged();
    }
    if (!v && QWindow::isVisible()) {
        QQuickWindow::setVisible(false);
    }
    if (v) {
        m_releaseTimer.stop();
        setPersistentSceneGraph(true);
    } else {
        m_releaseTimer.start();
    }
    if (auto* mgr = QsWindowManager::instance()) {
        mgr->invalidatePanelStack(this);
        mgr->markHitTestDirty();
    }
    scheduleSync();
}

QVariant QsPanelWindow::screenVariant() const {
    if (m_qsScreen) {
        return QVariant::fromValue(static_cast<QObject*>(m_qsScreen.data()));
    }
    if (QsGlobal* g = QsGlobal::instance()) {
        if (QsScreen* p = g->primaryQsScreen()) {
            return QVariant::fromValue(static_cast<QObject*>(p));
        }
    }
    return QVariant();
}

void QsPanelWindow::setScreenVariant(const QVariant& v) {
    QsScreen* next = nullptr;
    if (QObject* obj = v.value<QObject*>()) {
        next = qobject_cast<QsScreen*>(obj);
        if (!next) {
            if (auto* rawScr = qobject_cast<QScreen*>(obj)) {
                if (QsGlobal* g = QsGlobal::instance()) {
                    next = g->screenFor(rawScr);
                }
            }
        }
    }
    if (!next && QsGlobal::instance()) {
        next = QsGlobal::instance()->primaryQsScreen();
    }
    if (m_qsScreen == next) {
        return;
    }
    if (m_qsScreen) {
        disconnect(m_qsScreen, &QsScreen::geometryChanged, this, &QsPanelWindow::scheduleSync);
    }
    m_qsScreen = next;
    if (m_qsScreen) {
        connect(m_qsScreen, &QsScreen::geometryChanged, this, &QsPanelWindow::scheduleSync);
        if (m_qsScreen->qscreen()) {
            QWindow::setScreen(m_qsScreen->qscreen());
        }
    }
    Q_EMIT qsScreenChanged();
    scheduleSync();
}

void QsPanelWindow::setImplicitWidth(qreal w) {
    if (!qFuzzyCompare(m_implicitWidth, w)) {
        m_implicitWidth = w;
        Q_EMIT implicitWidthChanged();
        scheduleSync();
    }
}

void QsPanelWindow::setImplicitHeight(qreal h) {
    if (!qFuzzyCompare(m_implicitHeight, h)) {
        m_implicitHeight = h;
        Q_EMIT implicitHeightChanged();
        scheduleSync();
    }
}

void QsPanelWindow::setExclusionMode(int m) {
    if (m_exclusionMode != m) {
        m_exclusionMode = m;
        Q_EMIT exclusionModeChanged();
        scheduleSync();
    }
}

void QsPanelWindow::setExclusiveZone(qreal z) {
    // Quickshell's setter for exclusiveZone also forces exclusionMode to Normal
    if (m_exclusionMode != ExclusionMode::Normal) {
        m_exclusionMode = ExclusionMode::Normal;
        Q_EMIT exclusionModeChanged();
    }
    if (!qFuzzyCompare(m_exclusiveZone, z)) {
        m_exclusiveZone = z;
        Q_EMIT exclusiveZoneChanged();
        scheduleSync();
    }
}

void QsPanelWindow::setAboveWindows(bool a) {
    if (m_aboveWindows != a) {
        m_aboveWindows = a;
        Q_EMIT aboveWindowsChanged();
        scheduleSync();
    }
}

void QsPanelWindow::setFocusable(bool f) {
    if (m_focusable != f) {
        m_focusable = f;
        Q_EMIT focusableChanged();
        scheduleSync();
    }
}

void QsPanelWindow::setMask(QsRegion* r) {
    if (m_mask != r) {
        m_mask = r;
        Q_EMIT maskChanged();
        if (auto* mgr = QsWindowManager::instance()) {
            mgr->invalidatePanelStack(this);
            mgr->markHitTestDirty();
        }
    }
}

WlrLayershellAttached* QsPanelWindow::layerShell() const {
    return qobject_cast<WlrLayershellAttached*>(
        qmlAttachedPropertiesObject<WlrLayershell>(this, true));
}

void QsPanelWindow::scheduleSync() {
    if (m_syncPending) {
        return;
    }
    m_syncPending = true;
    QTimer::singleShot(0, this, [this]() {
        m_syncPending = false;
        syncNow();
    });
}

void QsPanelWindow::syncNow() {
    auto* mgr = QsWindowManager::instance();

    // Never allocate Win32 HWNDs or swapchains for hidden PanelWindows.
    if (!m_qsVisible) {
        m_lastKbFocus = -1;
        if (QWindow::isVisible()) {
            QQuickWindow::setVisible(false);
        }
        if (mgr) {
            mgr->invalidatePanelStack(this);
        }
        if (m_registeredAsBar && m_lastBarZone != 0) {
            if (mgr && mgr->appBarManager()) {
                mgr->appBarManager()->configureBarWindow(this, m_lastBarEdge, 0, true);
                m_lastBarZone = 0;
            }
        }
        return;
    }

    QsScreen* scr = m_qsScreen;
    if (!scr && QsGlobal::instance()) {
        scr = QsGlobal::instance()->primaryQsScreen();
    }
    if (!scr) {
        return;
    }

    int rx = scr->x();
    int ry = scr->y();
    int rw = scr->width();
    int rh = scr->height();

    const bool aTop = m_anchors->top();
    const bool aBottom = m_anchors->bottom();
    const bool aLeft = m_anchors->left();
    const bool aRight = m_anchors->right();

    const int mTop = qRound(m_margins->top());
    const int mBottom = qRound(m_margins->bottom());
    const int mLeft = qRound(m_margins->left());
    const int mRight = qRound(m_margins->right());

    const int impW = qRound(m_implicitWidth);
    const int impH = qRound(m_implicitHeight);

    WlrLayershellAttached* ls = layerShell();
    const QString ns = ls ? ls->ns() : QString();
    const int kbFocus = ls ? ls->keyboardFocus() : WlrKeyboardFocus::None;

    // Determine anchored edge for Normal exclusionMode windows (Bar / pinned Dock)
    QString anchoredEdge;
    if (aTop && !aBottom && aLeft && aRight) anchoredEdge = QStringLiteral("top");
    else if (aBottom && !aTop && aLeft && aRight) anchoredEdge = QStringLiteral("bottom");
    else if (aLeft && !aRight && aTop && aBottom) anchoredEdge = QStringLiteral("left");
    else if (aRight && !aLeft && aTop && aBottom) anchoredEdge = QStringLiteral("right");

    // If this is a pinned Dock (ExclusionMode::Normal) on the same edge as the Bar,
    // inset the reference rect by the Bar's exclusiveZone.
    if (ns == QStringLiteral("sekaipop-dock") && m_exclusionMode == ExclusionMode::Normal && !anchoredEdge.isEmpty() && mgr) {
        const int barRes = mgr->barReserveOnEdge(scr, anchoredEdge);
        if (barRes > 0) {
            if (anchoredEdge == QStringLiteral("top")) { ry += barRes; rh -= barRes; }
            else if (anchoredEdge == QStringLiteral("bottom")) { rh -= barRes; }
            else if (anchoredEdge == QStringLiteral("left")) { rx += barRes; rw -= barRes; }
            else if (anchoredEdge == QStringLiteral("right")) { rw -= barRes; }
        }
    }

    int targetX = rx;
    int targetW = rw;
    if (aLeft && aRight) {
        targetX = rx + mLeft;
        targetW = qMax(1, rw - mLeft - mRight);
    } else if (aLeft && !aRight) {
        targetW = impW > 0 ? impW : rw;
        targetX = rx + mLeft;
    } else if (!aLeft && aRight) {
        targetW = impW > 0 ? impW : rw;
        targetX = rx + rw - mRight - targetW;
    } else {
        targetW = impW > 0 ? impW : rw;
        targetX = rx + (rw - targetW) / 2 + mLeft - mRight;
    }

    int targetY = ry;
    int targetH = rh;
    if (aTop && aBottom) {
        targetY = ry + mTop;
        targetH = qMax(1, rh - mTop - mBottom);
    } else if (aTop && !aBottom) {
        targetH = impH > 0 ? impH : rh;
        targetY = ry + mTop;
    } else if (!aTop && aBottom) {
        targetH = impH > 0 ? impH : rh;
        targetY = ry + rh - mBottom - targetH;
    } else {
        targetH = impH > 0 ? impH : rh;
        targetY = ry + (rh - targetH) / 2 + mTop - mBottom;
    }

    const bool wasVisible = QWindow::isVisible();
    if (!wasVisible && ns == QStringLiteral("sekaipop-wallpaper")) {
        // WallpaperLayer fills the monitor opaquely (including EditBackdrop in Edit Desktop).
        // Using an opaque surface (alphaBufferSize=0) lets Windows DWM use a fast native
        // FLIP_DISCARD HWND swapchain instead of DirectComposition alpha-blended composition.
        QSurfaceFormat sf = format();
        if (sf.alphaBufferSize() != 0) {
            sf.setAlphaBufferSize(0);
            setFormat(sf);
        }
        setColor(Qt::black);
    }
    const QRect newGeom(targetX, targetY, qMax(1, targetW), qMax(1, targetH));
    if (geometry() != newGeom) {
        setGeometry(newGeom);
    }

    if (!wasVisible) {
        QQuickWindow::setVisible(true);
        if (mgr) {
            mgr->invalidatePanelStack(this);
        }
    }

    // Reserve workarea for Bar when visible and exclusionMode == Normal
    if (ns != QStringLiteral("sekaipop-dock") && !anchoredEdge.isEmpty() && m_exclusionMode == ExclusionMode::Normal) {
        const int zone = qMax(0, qRound(m_exclusiveZone));
        if (m_lastBarEdge != anchoredEdge || m_lastBarZone != zone) {
            if (mgr && mgr->appBarManager()) {
                mgr->appBarManager()->configureBarWindow(this, anchoredEdge, zone, zone <= 0);
                m_registeredAsBar = true;
                m_lastBarEdge = anchoredEdge;
                m_lastBarZone = zone;
            }
        }
    }

    HWND hwnd = reinterpret_cast<HWND>(winId());
    if (!hwnd) {
        return;
    }

    const bool wantFocus = m_focusable || kbFocus == WlrKeyboardFocus::OnDemand || kbFocus == WlrKeyboardFocus::Exclusive;
    const bool needLayered = (m_mask != nullptr) || m_isClickThrough;
    const LONG_PTR curExStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    LONG_PTR exStyle = curExStyle;
    exStyle |= WS_EX_TOOLWINDOW;
    if (needLayered) {
        exStyle |= WS_EX_LAYERED;
    } else {
        exStyle &= ~(WS_EX_LAYERED | WS_EX_TRANSPARENT);
    }
    exStyle &= ~WS_EX_APPWINDOW;
    if (wantFocus) {
        exStyle &= ~WS_EX_NOACTIVATE;
    } else {
        exStyle |= WS_EX_NOACTIVATE;
    }
    const bool styleChanged = (exStyle != curExStyle);
    if (styleChanged) {
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle);
        if (((exStyle ^ curExStyle) & WS_EX_LAYERED) != 0) {
            SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }
    }

    if (mgr) {
        if (!wasVisible || styleChanged || !mgr->isPanelStacked(this)) {
            mgr->scheduleRestack();
        }
        mgr->markHitTestDirty();
    }

    const int layer = ls ? ls->layer() : WlrLayer::Top;
    const bool kbChanged = (m_lastKbFocus != kbFocus);
    m_lastKbFocus = kbFocus;
    const bool shouldActivate =
        (kbFocus == WlrKeyboardFocus::Exclusive && (!wasVisible || kbChanged)) ||
        (kbFocus == WlrKeyboardFocus::OnDemand && layer == WlrLayer::Overlay && (!wasVisible || kbChanged || styleChanged));
    if (shouldActivate) {
        Win32::focusWindowForce(hwnd);
        requestActivate();
    }
}

void QsPanelWindow::setClickThrough(bool transparent) {
    if (m_isClickThrough == transparent) {
        return;
    }
    HWND hwnd = reinterpret_cast<HWND>(winId());
    if (!hwnd) {
        return;
    }
    m_isClickThrough = transparent;
    const LONG_PTR curExStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    LONG_PTR exStyle = curExStyle;
    if (transparent || m_mask != nullptr) {
        exStyle |= WS_EX_LAYERED;
    } else {
        exStyle &= ~WS_EX_LAYERED;
    }
    if (transparent) {
        exStyle |= WS_EX_TRANSPARENT;
    } else {
        exStyle &= ~WS_EX_TRANSPARENT;
    }
    if (exStyle != curExStyle) {
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle);
        // Only flush frame state when WS_EX_LAYERED itself changes; toggling WS_EX_TRANSPARENT
        // takes effect immediately in win32k hit-testing without expensive WM_NCCALCSIZE storms.
        if (((exStyle ^ curExStyle) & WS_EX_LAYERED) != 0) {
            SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }
    }
}

void QsPanelWindow::updateHitTest(const QPoint& globalPhysicalCursor) {
    if (!isVisible()) {
        return;
    }
    if (!m_mask) {
        setClickThrough(false);
        return;
    }
    if (mouseGrabberItem() != nullptr) {
        setClickThrough(false);
        return;
    }
    if (m_mask->coversWholeRect(QSizeF(width(), height()))) {
        setClickThrough(false);
        return;
    }
    HWND hwnd = reinterpret_cast<HWND>(winId());
    if (!hwnd) {
        return;
    }
    RECT wr = {};
    if (!GetWindowRect(hwnd, &wr)) {
        return;
    }
    const bool wasClickThrough = m_isClickThrough;
    if (globalPhysicalCursor.x() < wr.left || globalPhysicalCursor.x() >= wr.right ||
        globalPhysicalCursor.y() < wr.top || globalPhysicalCursor.y() >= wr.bottom) {
        setClickThrough(true);
        if (!wasClickThrough) {
            PostMessageW(hwnd, WM_MOUSELEAVE, 0, 0);
        }
        return;
    }
    const qreal dpr = qMax<qreal>(0.5, devicePixelRatio());
    const QPointF localPt(
        static_cast<qreal>(globalPhysicalCursor.x() - wr.left) / dpr,
        static_cast<qreal>(globalPhysicalCursor.y() - wr.top) / dpr
    );
    const bool hit = m_mask->containsPoint(localPt);
    setClickThrough(!hit);
    if (wasClickThrough && hit) {
        const bool anyBtnDown =
            ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0) ||
            ((GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0) ||
            ((GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0);
        const POINT gPt{ globalPhysicalCursor.x(), globalPhysicalCursor.y() };
        if (!anyBtnDown && WindowFromPoint(gPt) == hwnd) {
            POINT cpt = gPt;
            ScreenToClient(hwnd, &cpt);
            PostMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(cpt.x, cpt.y));
        }
    } else if (!wasClickThrough && !hit) {
        PostMessageW(hwnd, WM_MOUSELEAVE, 0, 0);
    }
}

// ============================================================================
// QsPopupAnchor & QsPopupWindow
// ============================================================================

QsPopupAnchor::QsPopupAnchor(QObject* parent)
    : QObject(parent) {}

QsPopupWindow::QsPopupWindow(QWindow* parent)
    : QQuickWindow(parent)
    , m_anchor(new QsPopupAnchor(this)) {
    const QQuickGraphicsDevice sharedDev = QsWindowManager::sharedGraphicsDevice();
    if (!sharedDev.isNull()) {
        setGraphicsDevice(sharedDev);
    }
    setColor(Qt::transparent);
    setFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint);
    setPersistentSceneGraph(false);

    m_releaseTimer.setSingleShot(true);
    m_releaseTimer.setInterval(12000);
    connect(&m_releaseTimer, &QTimer::timeout, this, [this]() {
        if (isVisible()) {
            return;
        }
        setPersistentSceneGraph(false);
        releaseResources();
        if (auto* mgr = QsWindowManager::instance()) {
            mgr->scheduleMemoryTrim(1500);
        }
    });

    connect(m_anchor, &QsPopupAnchor::changed, this, &QsPopupWindow::syncPlacement);
    connect(this, &QWindow::visibleChanged, this, [this](bool vis) {
        if (vis) {
            m_releaseTimer.stop();
            setPersistentSceneGraph(true);
            syncPlacement();
            syncMaskStyles();
            HWND hwnd = reinterpret_cast<HWND>(winId());
            if (hwnd) {
                SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
            }
        } else {
            m_releaseTimer.start();
        }
    });
}

void QsPopupWindow::setImplicitWidth(qreal w) {
    if (!qFuzzyCompare(m_implicitWidth, w)) {
        m_implicitWidth = w;
        Q_EMIT implicitWidthChanged();
        syncPlacement();
    }
}

void QsPopupWindow::setImplicitHeight(qreal h) {
    if (!qFuzzyCompare(m_implicitHeight, h)) {
        m_implicitHeight = h;
        Q_EMIT implicitHeightChanged();
        syncPlacement();
    }
}

void QsPopupWindow::setMask(QsRegion* r) {
    if (m_mask != r) {
        if (m_mask) {
            disconnect(m_mask, nullptr, this, nullptr);
        }
        m_mask = r;
        if (m_mask) {
            connect(m_mask, &QsRegion::regionUpdated, this, &QsPopupWindow::syncMaskStyles);
        }
        Q_EMIT maskChanged();
        syncMaskStyles();
    }
}

void QsPopupWindow::syncMaskStyles() {
    if (!handle()) {
        return;
    }
    HWND hwnd = reinterpret_cast<HWND>(winId());
    if (!hwnd) {
        return;
    }
    const bool clickThrough = (m_mask != nullptr && m_mask->isEmpty());
    const LONG_PTR curEx = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    LONG_PTR exStyle = curEx;
    exStyle |= WS_EX_TOOLWINDOW;
    exStyle &= ~WS_EX_APPWINDOW;
    if (clickThrough) {
        exStyle |= WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE;
    } else {
        exStyle &= ~WS_EX_TRANSPARENT;
    }
    if (exStyle != curEx) {
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle);
        if (((exStyle ^ curEx) & WS_EX_LAYERED) != 0) {
            SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }
    }
}

void QsPopupWindow::syncPlacement() {
    if (!isVisible()) {
        return;
    }
    const int w = qMax(1, qRound(m_implicitWidth));
    const int h = qMax(1, qRound(m_implicitHeight));

    int baseX = 0;
    int baseY = 0;
    QScreen* scr = screen();
    if (auto* parentWin = qobject_cast<QWindow*>(m_anchor->window())) {
        baseX = parentWin->x();
        baseY = parentWin->y();
        if (parentWin->screen()) {
            scr = parentWin->screen();
            setScreen(scr);
        }
    }

    const QRectF r = m_anchor->rect();
    const int edges = m_anchor->edges();
    const int grav = m_anchor->gravity();

    // Compute anchor point (ax, ay) on anchor.rect according to xdg-positioner / Quickshell edges.
    qreal ax = r.x() + r.width() * 0.5;
    const bool eLeft = (edges & Edges::Left) != 0;
    const bool eRight = (edges & Edges::Right) != 0;
    if (eLeft && !eRight) {
        ax = r.x();
    } else if (eRight && !eLeft) {
        ax = r.x() + r.width();
    }

    qreal ay = r.y() + r.height() * 0.5;
    const bool eTop = (edges & Edges::Top) != 0;
    const bool eBottom = (edges & Edges::Bottom) != 0;
    if (eTop && !eBottom) {
        ay = r.y();
    } else if (eBottom && !eTop) {
        ay = r.y() + r.height();
    }

    // Place popup relative to (ax, ay) according to gravity (center on an axis when neither flag is set).
    const bool gLeft = (grav & Edges::Left) != 0;
    const bool gRight = (grav & Edges::Right) != 0;
    int localLeft = qRound(ax - w * 0.5);
    if (gLeft && !gRight) {
        localLeft = qRound(ax) - w;
    } else if (gRight && !gLeft) {
        localLeft = qRound(ax);
    }

    const bool gTop = (grav & Edges::Top) != 0;
    const bool gBottom = (grav & Edges::Bottom) != 0;
    int localTop = qRound(ay - h * 0.5);
    if (gTop && !gBottom) {
        localTop = qRound(ay) - h;
    } else if (gBottom && !gTop) {
        localTop = qRound(ay);
    }

    int targetX = baseX + localLeft;
    int targetY = baseY + localTop;

    const int adj = m_anchor->adjustment();
    if (scr && adj != PopupAdjustment::None) {
        const QRect sg = scr->geometry();
        const int pad = 4;
        if ((adj & PopupAdjustment::SlideX) != 0) {
            const int minX = sg.x() + pad;
            const int maxX = qMax(minX, sg.x() + sg.width() - w - pad);
            targetX = qBound(minX, targetX, maxX);
        }
        if ((adj & PopupAdjustment::SlideY) != 0) {
            const int minY = sg.y() + pad;
            const int maxY = qMax(minY, sg.y() + sg.height() - h - pad);
            targetY = qBound(minY, targetY, maxY);
        }
    }

    const QRect target(targetX, targetY, w, h);
    if (geometry() != target) {
        setGeometry(target);
    }
}

// ============================================================================
// QsFloatingWindow & WlSessionLockSurface
// ============================================================================

QsFloatingWindow::QsFloatingWindow(QWindow* parent)
    : QQuickWindow(parent) {
    const QQuickGraphicsDevice sharedDev = QsWindowManager::sharedGraphicsDevice();
    if (!sharedDev.isNull()) {
        setGraphicsDevice(sharedDev);
    }
    resize(qRound(m_implicitWidth), qRound(m_implicitHeight));
}

void QsFloatingWindow::setImplicitWidth(qreal w) {
    if (!qFuzzyCompare(m_implicitWidth, w)) {
        m_implicitWidth = w;
        setWidth(qMax(1, qRound(w)));
        Q_EMIT implicitWidthChanged();
    }
}

void QsFloatingWindow::setImplicitHeight(qreal h) {
    if (!qFuzzyCompare(m_implicitHeight, h)) {
        m_implicitHeight = h;
        setHeight(qMax(1, qRound(h)));
        Q_EMIT implicitHeightChanged();
    }
}

QVariant QsFloatingWindow::screenVariant() const {
    if (m_qsScreen) {
        return QVariant::fromValue(static_cast<QObject*>(m_qsScreen.data()));
    }
    return QVariant();
}

void QsFloatingWindow::setScreenVariant(const QVariant& v) {
    if (QObject* obj = v.value<QObject*>()) {
        if (auto* s = qobject_cast<QsScreen*>(obj)) {
            m_qsScreen = s;
            if (s->qscreen()) {
                QWindow::setScreen(s->qscreen());
            }
            Q_EMIT qsScreenChanged();
        }
    }
}

WlSessionLockSurface::WlSessionLockSurface(QWindow* parent)
    : QsPanelWindow(parent) {
    anchors()->setTop(true);
    anchors()->setBottom(true);
    anchors()->setLeft(true);
    anchors()->setRight(true);
    setExclusionMode(ExclusionMode::Ignore);
    layerShell()->setLayer(WlrLayer::Overlay);
    layerShell()->setKeyboardFocus(WlrKeyboardFocus::Exclusive);
}

// ============================================================================
// QsClippingRectangle (Zero-FBO hardware stencil rounded clip)
// ============================================================================

QsClippingRectangle::QsClippingRectangle(QQuickItem* parent)
    : QQuickItem(parent) {
    setFlag(ItemHasContents, true);
    setClip(true);
}

void QsClippingRectangle::setRadius(qreal r) {
    if (!qFuzzyCompare(m_radius, r)) {
        m_radius = r;
        Q_EMIT radiusChanged();
        if (!m_hasTopLeft) Q_EMIT topLeftRadiusChanged();
        if (!m_hasTopRight) Q_EMIT topRightRadiusChanged();
        if (!m_hasBottomLeft) Q_EMIT bottomLeftRadiusChanged();
        if (!m_hasBottomRight) Q_EMIT bottomRightRadiusChanged();
        update();
    }
}

void QsClippingRectangle::setTopLeftRadius(qreal r) {
    if (!m_hasTopLeft || !qFuzzyCompare(m_topLeftRadius, r)) {
        m_hasTopLeft = true;
        m_topLeftRadius = r;
        Q_EMIT topLeftRadiusChanged();
        update();
    }
}

void QsClippingRectangle::setTopRightRadius(qreal r) {
    if (!m_hasTopRight || !qFuzzyCompare(m_topRightRadius, r)) {
        m_hasTopRight = true;
        m_topRightRadius = r;
        Q_EMIT topRightRadiusChanged();
        update();
    }
}

void QsClippingRectangle::setBottomLeftRadius(qreal r) {
    if (!m_hasBottomLeft || !qFuzzyCompare(m_bottomLeftRadius, r)) {
        m_hasBottomLeft = true;
        m_bottomLeftRadius = r;
        Q_EMIT bottomLeftRadiusChanged();
        update();
    }
}

void QsClippingRectangle::setBottomRightRadius(qreal r) {
    if (!m_hasBottomRight || !qFuzzyCompare(m_bottomRightRadius, r)) {
        m_hasBottomRight = true;
        m_bottomRightRadius = r;
        Q_EMIT bottomRightRadiusChanged();
        update();
    }
}

void QsClippingRectangle::setColor(const QColor& c) {
    if (m_color != c) {
        m_color = c;
        Q_EMIT colorChanged();
        update();
    }
}

void QsClippingRectangle::setContentInsideBorder(bool b) {
    if (m_contentInsideBorder != b) {
        m_contentInsideBorder = b;
        Q_EMIT contentInsideBorderChanged();
        update();
    }
}

void QsClippingRectangle::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) {
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        update();
    }
}

QSGNode* QsClippingRectangle::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) {
    const qreal w = width();
    const qreal h = height();

    if (w > 0.0 && h > 0.0) {
        if (auto* priv = QQuickItemPrivate::get(this)) {
            if (QQuickDefaultClipNode* cn = priv->clipNode()) {
                const qreal maxR = qMin(w, h) * 0.5;
                const qreal tl = qBound<qreal>(0.0, topLeftRadius(), maxR);
                const qreal tr = qBound<qreal>(0.0, topRightRadius(), maxR);
                const qreal bl = qBound<qreal>(0.0, bottomLeftRadius(), maxR);
                const qreal br = qBound<qreal>(0.0, bottomRightRadius(), maxR);

                const bool uniform = qFuzzyCompare(tl + 1.0, tr + 1.0)
                    && qFuzzyCompare(tr + 1.0, bl + 1.0)
                    && qFuzzyCompare(bl + 1.0, br + 1.0);

                if (uniform) {
                    cn->setRect(QRectF(0.0, 0.0, w, h));
                    cn->setRadius(tl);
                    cn->update();
                } else {
                    constexpr int kSegs = 8;
                    constexpr double kPi = 3.14159265358979323846;
                    std::vector<QPointF> perim;
                    perim.reserve(4 * (kSegs + 1));

                    auto addCorner = [&](qreal cx, qreal cy, qreal r, double startA, double endA, qreal flatX, qreal flatY) {
                        if (r <= 0.5) {
                            perim.emplace_back(flatX, flatY);
                            return;
                        }
                        for (int i = 0; i <= kSegs; ++i) {
                            const double a = startA + (endA - startA) * (static_cast<double>(i) / kSegs);
                            perim.emplace_back(cx + std::cos(a) * r, cy + std::sin(a) * r);
                        }
                    };

                    addCorner(tl, tl, tl, kPi, 1.5 * kPi, 0.0, 0.0);
                    addCorner(w - tr, tr, tr, 1.5 * kPi, 2.0 * kPi, w, 0.0);
                    addCorner(w - br, h - br, br, 0.0, 0.5 * kPi, w, h);
                    addCorner(bl, h - bl, bl, 0.5 * kPi, kPi, 0.0, h);

                    if (QSGGeometry* g = cn->geometry()) {
                        const int triCount = static_cast<int>(perim.size());
                        g->setDrawingMode(QSGGeometry::DrawTriangles);
                        g->allocate(triCount * 3);
                        QSGGeometry::Point2D* v = g->vertexDataAsPoint2D();
                        const float cx = static_cast<float>(w * 0.5);
                        const float cy = static_cast<float>(h * 0.5);
                        for (int i = 0; i < triCount; ++i) {
                            const QPointF& p0 = perim[static_cast<size_t>(i)];
                            const QPointF& p1 = perim[static_cast<size_t>((i + 1) % triCount)];
                            v[i * 3 + 0].set(cx, cy);
                            v[i * 3 + 1].set(static_cast<float>(p0.x()), static_cast<float>(p0.y()));
                            v[i * 3 + 2].set(static_cast<float>(p1.x()), static_cast<float>(p1.y()));
                        }
                        cn->setClipRect(QRectF(0.0, 0.0, w, h));
                        cn->setIsRectangular(false);
                        cn->markDirty(QSGNode::DirtyGeometry);
                    }
                }
            }
        }
    }

    if (m_color.alpha() == 0 || w <= 0.0 || h <= 0.0) {
        delete oldNode;
        return nullptr;
    }

    auto* node = static_cast<QSGGeometryNode*>(oldNode);
    if (!node) {
        node = new QSGGeometryNode();
        auto* geom = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 4);
        geom->setDrawingMode(QSGGeometry::DrawTriangleStrip);
        node->setGeometry(geom);
        node->setFlag(QSGNode::OwnsGeometry);

        auto* mat = new QSGFlatColorMaterial();
        node->setMaterial(mat);
        node->setFlag(QSGNode::OwnsMaterial);
    }

    QSGGeometry::updateRectGeometry(node->geometry(), QRectF(0.0, 0.0, w, h));
    node->markDirty(QSGNode::DirtyGeometry);

    auto* mat = static_cast<QSGFlatColorMaterial*>(node->material());
    if (mat->color() != m_color) {
        mat->setColor(m_color);
        node->markDirty(QSGNode::DirtyMaterial);
    }
    return node;
}

// ============================================================================
// QsWindowManager
// ============================================================================

namespace {
QPointer<QsWindowManager> s_windowMgr;
bool s_windowMgrShuttingDown = false;
QRhi* s_sharedRhi = nullptr;
QQuickGraphicsDevice s_sharedDevice;

bool selectLowPowerGpuLuid(quint32& outLow, qint32& outHigh, QString& outName) {
    HMODULE dxgiMod = LoadLibraryW(L"dxgi.dll");
    if (!dxgiMod) {
        return false;
    }
    using CreateFactory1Fn = HRESULT(WINAPI*)(REFIID, void**);
    auto createFactory1 = reinterpret_cast<CreateFactory1Fn>(GetProcAddress(dxgiMod, "CreateDXGIFactory1"));
    if (!createFactory1) {
        FreeLibrary(dxgiMod);
        return false;
    }

    IDXGIFactory1* factory1 = nullptr;
    bool found = false;
    if (SUCCEEDED(createFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory1))) && factory1) {
        IDXGIFactory6* factory6 = nullptr;
        if (SUCCEEDED(factory1->QueryInterface(__uuidof(IDXGIFactory6), reinterpret_cast<void**>(&factory6))) && factory6) {
            // Prefer low-power integrated GPU (e.g. Intel UHD / AMD Radeon iGPU) so the desktop
            // shell avoids loading ~200MB of discrete NVIDIA driver heaps & keeps dGPU asleep.
            IDXGIAdapter1* fallbackHw = nullptr;
            DXGI_ADAPTER_DESC1 fallbackDesc = {};
            for (UINT i = 0;; ++i) {
                IDXGIAdapter1* adapter = nullptr;
                if (FAILED(factory6->EnumAdapterByGpuPreference(
                        i, DXGI_GPU_PREFERENCE_MINIMUM_POWER, __uuidof(IDXGIAdapter1),
                        reinterpret_cast<void**>(&adapter))) || !adapter) {
                    break;
                }
                DXGI_ADAPTER_DESC1 desc = {};
                if (SUCCEEDED(adapter->GetDesc1(&desc))) {
                    const bool isSoftware = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
                    const bool isKnownGpuVendor = (desc.VendorId == 0x8086 || desc.VendorId == 0x1002 ||
                                                   desc.VendorId == 0x10DE || desc.VendorId == 0x5143);
                    if (!isSoftware && isKnownGpuVendor) {
                        outLow = static_cast<quint32>(desc.AdapterLuid.LowPart);
                        outHigh = static_cast<qint32>(desc.AdapterLuid.HighPart);
                        outName = QString::fromWCharArray(desc.Description);
                        found = true;
                        adapter->Release();
                        break;
                    }
                    if (!isSoftware && !fallbackHw && desc.DedicatedVideoMemory > 0) {
                        fallbackHw = adapter;
                        fallbackDesc = desc;
                        continue;
                    }
                }
                adapter->Release();
            }
            if (!found && fallbackHw) {
                outLow = static_cast<quint32>(fallbackDesc.AdapterLuid.LowPart);
                outHigh = static_cast<qint32>(fallbackDesc.AdapterLuid.HighPart);
                outName = QString::fromWCharArray(fallbackDesc.Description);
                found = true;
            }
            if (fallbackHw) {
                fallbackHw->Release();
            }
            factory6->Release();
        }
        factory1->Release();
    }
    FreeLibrary(dxgiMod);
    return found;
}
}

void QsWindowManager::initSharedGraphics() {
    if (s_sharedRhi) {
        return;
    }
    const QString reqBackend = qEnvironmentVariable("QSG_RHI_BACKEND").trimmed().toLower();
    if (!reqBackend.isEmpty() && reqBackend != QStringLiteral("d3d11")) {
        return;
    }
    QRhiD3D11InitParams params;
    const QString gpuPref = qEnvironmentVariable("SEKAIPOP_GPU").trimmed().toLower();
    if (gpuPref != QStringLiteral("high")) {
        quint32 luidLow = 0;
        qint32 luidHigh = 0;
        QString gpuName;
        if (selectLowPowerGpuLuid(luidLow, luidHigh, gpuName)) {
            QRhiD3D11NativeHandles nativeHandles;
            nativeHandles.adapterLuidLow = luidLow;
            nativeHandles.adapterLuidHigh = luidHigh;
            s_sharedRhi = QRhi::create(QRhi::D3D11, &params, {}, &nativeHandles);
            if (s_sharedRhi) {
                s_sharedDevice = QQuickGraphicsDevice::fromRhi(s_sharedRhi);
                qInfo("[QsWindowManager] Shared D3D11 QRhi initialized on low-power GPU '%s' (LUID=%u:%d): %p",
                      qPrintable(gpuName), luidLow, luidHigh, static_cast<void*>(s_sharedRhi));
                return;
            }
        }
    }
    s_sharedRhi = QRhi::create(QRhi::D3D11, &params);
    if (s_sharedRhi) {
        s_sharedDevice = QQuickGraphicsDevice::fromRhi(s_sharedRhi);
        qInfo("[QsWindowManager] Shared D3D11 QRhi initialized: %p", static_cast<void*>(s_sharedRhi));
    }
}

void QsWindowManager::releaseSharedGraphics() {
    s_sharedDevice = QQuickGraphicsDevice();
    if (s_sharedRhi) {
        delete s_sharedRhi;
        s_sharedRhi = nullptr;
    }
}

QQuickGraphicsDevice QsWindowManager::sharedGraphicsDevice() {
    return s_sharedDevice;
}

QsWindowManager::QsWindowManager(QObject* parent)
    : QObject(parent) {
    // Adaptive cursor hit-test check: 16ms (60Hz) while pointer is moving,
    // relaxing to 50ms (20Hz) when pointer is stationary to minimize idle Main Thread wakeups.
    m_hitTestTimer.setInterval(16);
    connect(&m_hitTestTimer, &QTimer::timeout, this, &QsWindowManager::pollHitTests);
    m_hitTestTimer.start();

    m_trimTimer.setSingleShot(true);
    connect(&m_trimTimer, &QTimer::timeout, this, &QsWindowManager::performMemoryTrim);

    m_curtainCaptureTimer.setSingleShot(true);
    connect(&m_curtainCaptureTimer, &QTimer::timeout, this, [this]() {
        if (s_windowMgrShuttingDown || !m_allowCurtainCapture || isEditModeActive()) {
            return;
        }
        Win32::BootCurtainCache::captureFromPanels(m_panels, isEditModeActive());
    });
}

void QsWindowManager::scheduleBootCurtainCapture(int delayMs) {
    if (s_windowMgrShuttingDown || !m_allowCurtainCapture) {
        return;
    }
    m_curtainCaptureTimer.start(qMax(250, delayMs));
}

void QsWindowManager::maybeEmitCurtainDrop() {
    if (s_windowMgrShuttingDown || m_curtainDropEmitted) {
        return;
    }
    if (m_wallpaperFrameSwapped && m_barFrameSwapped) {
        m_curtainDropEmitted = true;
        Q_EMIT curtainDropReady();
    }
}

void QsWindowManager::setQmlEngine(QQmlEngine* engine) {
    m_qmlEngine = engine;
}

void QsWindowManager::scheduleMemoryTrim(int delayMs) {
    if (s_windowMgrShuttingDown) {
        return;
    }
    m_trimTimer.start(qMax(500, delayMs));
}

void QsWindowManager::performMemoryTrim() {
    if (s_windowMgrShuttingDown || isEditModeActive()) {
        return;
    }
    for (QsPanelWindow* w : std::as_const(m_panels)) {
        if (!w || !w->isVisible()) {
            continue;
        }
        WlrLayershellAttached* ls = w->layerShell();
        if (ls && ls->layer() == WlrLayer::Overlay && ls->ns() != QStringLiteral("sekaipop-hotedge")) {
            return;
        }
    }
    // Event-driven idle memory trim (runs ONLY once after startup settle or after a heavy
    // overlay/popup finishes releasing its scene graph, never on a blind recurring loop).
    if (m_qmlEngine) {
        m_qmlEngine->collectGarbage();
    }
    QPixmapCache::clear();
    SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
}

QsWindowManager* QsWindowManager::instance() {
    if (s_windowMgrShuttingDown) {
        return nullptr;
    }
    if (!s_windowMgr) {
        QCoreApplication* app = QCoreApplication::instance();
        if (!app) {
            return nullptr;
        }
        s_windowMgr = new QsWindowManager(app);
    }
    return s_windowMgr;
}

void QsWindowManager::shutdown() {
    s_windowMgrShuttingDown = true;
    if (s_windowMgr) {
        s_windowMgr->m_hitTestTimer.stop();
        s_windowMgr->m_trimTimer.stop();
        s_windowMgr->m_curtainCaptureTimer.stop();
        s_windowMgr->m_qmlEngine = nullptr;
        s_windowMgr->m_wallpaperHost = nullptr;
        s_windowMgr->m_appBarManager = nullptr;
        s_windowMgr->m_editModeObj = nullptr;
        s_windowMgr->m_panels.clear();
        s_windowMgr->m_normalStacked.clear();
    }
}

void QsWindowManager::init(Win32::WallpaperHost* wallpaperHost, Win32::AppBarManager* appBarManager) {
    m_wallpaperHost = wallpaperHost;
    m_appBarManager = appBarManager;
}

void QsWindowManager::setEditModeObject(QObject* editModeSingleton) {
    m_editModeObj = editModeSingleton;
    if (m_editModeObj) {
        const QMetaObject* mo = m_editModeObj->metaObject();
        const int sigIdx = mo->indexOfSignal("screenNameChanged()");
        const int activeIdx = mo->indexOfSignal("activeChanged()");
        const int targetIdx = mo->indexOfSignal("editTargetChanged()");
        if (sigIdx >= 0) {
            connect(m_editModeObj, SIGNAL(screenNameChanged()), this, SLOT(scheduleRestack()));
        }
        if (activeIdx >= 0) {
            connect(m_editModeObj, SIGNAL(activeChanged()), this, SLOT(scheduleRestack()));
        }
        if (targetIdx >= 0) {
            connect(m_editModeObj, SIGNAL(editTargetChanged()), this, SLOT(scheduleRestack()));
        }
        if (!m_focusConnected) {
            if (auto* guiApp = qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
                m_focusConnected = true;
                connect(guiApp, &QGuiApplication::focusWindowChanged, this, [this](QWindow*) {
                    if (!s_windowMgrShuttingDown && isEditModeActive()) {
                        scheduleRestack();
                    }
                });
            }
        }
        onEditModeChanged();
    }
}

void QsWindowManager::onEditModeChanged() {
    if (!m_editModeObj) {
        m_editModeActive = false;
        return;
    }
    const QString sn = m_editModeObj->property("screenName").toString();
    const bool act = m_editModeObj->property("active").toBool();
    m_editModeActive = act || !sn.isEmpty();
}

void QsWindowManager::registerPanel(QsPanelWindow* win) {
    if (!m_panels.contains(win)) {
        m_panels.append(win);
        m_hitTestDirty = true;
        if (!m_curtainDropEmitted && win) {
            QPointer<QsPanelWindow> wp(win);
            connect(win, &QQuickWindow::frameSwapped, this, [this, wp]() {
                if (s_windowMgrShuttingDown || m_curtainDropEmitted || !wp || !wp->isVisible()) {
                    return;
                }
                WlrLayershellAttached* ls = wp->layerShell();
                const int layer = ls ? ls->layer() : WlrLayer::Top;
                const QString ns = ls ? ls->ns() : QString();
                if (layer == WlrLayer::Background && wp->mask() == nullptr) {
                    m_wallpaperFrameSwapped = true;
                } else if (layer == WlrLayer::Top && ns != QStringLiteral("sekaipop-dock") &&
                           ns != QStringLiteral("sekaipop-hotedge")) {
                    m_barFrameSwapped = true;
                }
                maybeEmitCurtainDrop();
            }, Qt::QueuedConnection);
        }
    }
}

void QsWindowManager::unregisterPanel(QsPanelWindow* win) {
    m_panels.removeAll(win);
    m_normalStacked.remove(win);
    m_fullscreenHiddenPanels.remove(win);
    m_hitTestDirty = true;
}

bool QsWindowManager::isPanelOnFullscreenMonitor(QsPanelWindow* w) const {
    if (!w || m_fullscreenMonitors.isEmpty()) {
        return false;
    }
    HWND hwnd = w->handle() ? reinterpret_cast<HWND>(w->winId()) : nullptr;
    HMONITOR hmon = hwnd ? MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST) : nullptr;
    if (!hmon && w->screen()) {
        const QRect g = w->screen()->geometry();
        const qreal dpr = qMax<qreal>(0.5, w->screen()->devicePixelRatio());
        const POINT pt{
            qRound(g.x() + (g.width() * 0.5) * dpr),
            qRound(g.y() + (g.height() * 0.5) * dpr)
        };
        hmon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    }
    return hmon && m_fullscreenMonitors.contains(reinterpret_cast<quintptr>(hmon));
}

void QsWindowManager::setFullscreenMonitors(const QSet<quintptr>& monitors) {
    if (s_windowMgrShuttingDown || m_fullscreenMonitors == monitors) {
        return;
    }
    m_fullscreenMonitors = monitors;
    if (isEditModeActive()) {
        return;
    }
    for (QsPanelWindow* w : std::as_const(m_panels)) {
        if (!w || !w->isVisible() || !w->handle()) {
            continue;
        }
        WlrLayershellAttached* ls = w->layerShell();
        const int layer = ls ? ls->layer() : WlrLayer::Top;
        const QString ns = ls ? ls->ns() : QString();
        const bool isAutoHideFsTarget =
            (ns == QStringLiteral("sekaipop-hotedge")) ||
            (ns == QStringLiteral("sekaipop-dock") && layer == WlrLayer::Top);
        if (!isAutoHideFsTarget) {
            continue;
        }
        HWND hwnd = reinterpret_cast<HWND>(w->winId());
        if (!hwnd || !IsWindow(hwnd)) {
            continue;
        }
        const bool fs = isPanelOnFullscreenMonitor(w);
        if (fs && !m_fullscreenHiddenPanels.contains(w)) {
            m_fullscreenHiddenPanels.insert(w);
            ShowWindow(hwnd, SW_HIDE);
        } else if (!fs && m_fullscreenHiddenPanels.contains(w)) {
            m_fullscreenHiddenPanels.remove(w);
            ShowWindow(hwnd, SW_SHOWNOACTIVATE);
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        }
    }
}

void QsWindowManager::invalidatePanelStack(QsPanelWindow* win) {
    m_normalStacked.remove(win);
}

void QsWindowManager::markHitTestDirty() {
    m_hitTestDirty = true;
    if (!m_hitTestQueued && !s_windowMgrShuttingDown) {
        m_hitTestQueued = true;
        QTimer::singleShot(0, this, [this]() {
            m_hitTestQueued = false;
            if (!s_windowMgrShuttingDown && m_hitTestDirty) {
                pollHitTests();
            }
        });
    }
}

bool QsWindowManager::isEditModeActive() const {
    if (!m_editModeObj) {
        return false;
    }
    const QString sn = m_editModeObj->property("screenName").toString();
    const bool act = m_editModeObj->property("active").toBool();
    return act || !sn.isEmpty();
}

int QsWindowManager::barReserveOnEdge(QsScreen* screen, const QString& edge) const {
    for (QsPanelWindow* w : m_panels) {
        if (!w || !w->isVisible() || w->qsScreen() != screen) {
            continue;
        }
        WlrLayershellAttached* ls = w->layerShell();
        if (ls && ls->ns() == QStringLiteral("sekaipop-dock")) {
            continue;
        }
        if (w->exclusionMode() == ExclusionMode::Normal && w->exclusiveZone() > 0) {
            const bool aTop = w->anchors()->top();
            const bool aBottom = w->anchors()->bottom();
            const bool aLeft = w->anchors()->left();
            const bool aRight = w->anchors()->right();
            QString wEdge;
            if (aTop && !aBottom && aLeft && aRight) wEdge = QStringLiteral("top");
            else if (aBottom && !aTop && aLeft && aRight) wEdge = QStringLiteral("bottom");
            else if (aLeft && !aRight && aTop && aBottom) wEdge = QStringLiteral("left");
            else if (aRight && !aLeft && aTop && aBottom) wEdge = QStringLiteral("right");
            if (wEdge == edge) {
                return qRound(w->exclusiveZone());
            }
        }
    }
    return 0;
}

void QsWindowManager::scheduleRestack() {
    if (s_windowMgrShuttingDown || m_restackPending) {
        return;
    }
    m_restackPending = true;
    QTimer::singleShot(0, this, [this]() {
        m_restackPending = false;
        if (!s_windowMgrShuttingDown) {
            restackAll();
        }
    });
}

void QsWindowManager::restackAll() {
    if (s_windowMgrShuttingDown) {
        return;
    }
    const bool editing = isEditModeActive();
    m_editModeActive = editing;

    if (editing) {
        m_wasEditMode = true;
        m_normalStacked.clear();
        if (m_wallpaperHost) {
            m_wallpaperHost->setEditModeSuspended(true);
        }
        if (m_appBarManager) {
            m_appBarManager->setEditModeSuspended(true);
        }

        auto rankOf = [](QsPanelWindow* w) -> int {
            WlrLayershellAttached* ls = w->layerShell();
            const int layer = ls ? ls->layer() : WlrLayer::Top;
            const QString ns = ls ? ls->ns() : QString();
            if (layer == WlrLayer::Background && w->mask() == nullptr) {
                return 0; // WallpaperLayer (EditBackdrop + framed desktop card)
            }
            if (layer == WlrLayer::Background && ns == QStringLiteral("sekaipop-visualizer")) {
                return 1; // Audio Visualizer strictly behind other desktop widgets & Dock
            }
            if (layer == WlrLayer::Background) {
                return 2; // Desktop Widgets at rest
            }
            if (layer == WlrLayer::Bottom && ns != QStringLiteral("sekaipop-dock")) {
                return 3; // Selected / raised Desktop Widget
            }
            if (layer == WlrLayer::Top || ns == QStringLiteral("sekaipop-dock") ||
                ns == QStringLiteral("sekaipop-hotedge")) {
                return 4; // Bar, Dock & HotEdge (strictly below EditTray)
            }
            if (ns == QStringLiteral("sekaipop-edit-guides")) {
                return 5; // Alignment grid & snap guides above desktop card, below shelf
            }
            if (ns == QStringLiteral("sekaipop-edit-drag")) {
                return 7; // Dragged widget ghost always above EditTray
            }
            return 6; // Overlay (EditTray / sekaipop-edit-drawer, DesktopMenu, etc.)
        };

        QList<QsPanelWindow*> vis;
        for (QsPanelWindow* w : m_panels) {
            if (w && w->isVisible() && w->handle() && w->winId()) {
                vis.append(w);
            }
        }
        std::stable_sort(vis.begin(), vis.end(), [&](QsPanelWindow* a, QsPanelWindow* b) {
            return rankOf(a) < rankOf(b);
        });

        // Collect HWNDs top-to-bottom (highest rank first -> WallpaperLayer last).
        QList<HWND> topDown;
        topDown.reserve(vis.size());
        bool allTopmost = true;
        for (int i = vis.size() - 1; i >= 0; --i) {
            HWND hwnd = reinterpret_cast<HWND>(vis.at(i)->winId());
            if (!hwnd || !IsWindow(hwnd)) {
                continue;
            }
            if ((GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) == 0) {
                allTopmost = false;
            }
            topDown.append(hwnd);
        }
        if (topDown.isEmpty()) {
            return;
        }

        // Fast-path: if every Edit Desktop panel is already WS_EX_TOPMOST and in exact
        // top-to-bottom rank order, skip SetWindowPos completely (zero flicker).
        if (allTopmost) {
            QHash<HWND, int> zIndex;
            zIndex.reserve(topDown.size());
            int remaining = topDown.size();
            for (HWND h : topDown) {
                zIndex.insert(h, -1);
            }
            int pos = 0;
            for (HWND cur = GetTopWindow(nullptr); cur && remaining > 0; cur = GetWindow(cur, GW_HWNDNEXT), ++pos) {
                auto it = zIndex.find(cur);
                if (it != zIndex.end() && it.value() < 0) {
                    it.value() = pos;
                    --remaining;
                }
            }
            bool ordered = (remaining == 0);
            for (int i = 1; ordered && i < topDown.size(); ++i) {
                if (zIndex.value(topDown.at(i - 1)) >= zIndex.value(topDown.at(i))) {
                    ordered = false;
                }
            }
            if (ordered) {
                return;
            }
        }

        // Step 1: Promote any panel not yet in the WS_EX_TOPMOST band (ascending rank 0..7)
        // so Windows reliably grants WS_EX_TOPMOST without flashing WallpaperLayer above EditTray.
        if (!allTopmost) {
            for (int i = topDown.size() - 1; i >= 0; --i) {
                HWND hwnd = topDown.at(i);
                if ((GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) == 0) {
                    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                }
            }
        }

        // Step 2: Commit exact top-to-bottom rank order atomically in a single DWM frame.
        HDWP hdwp = BeginDeferWindowPos(static_cast<int>(topDown.size()));
        HWND insertAfter = HWND_TOPMOST;
        for (HWND hwnd : topDown) {
            if (hdwp) {
                hdwp = DeferWindowPos(hdwp, hwnd, insertAfter, 0, 0, 0, 0,
                                      SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            } else {
                SetWindowPos(hwnd, insertAfter, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            }
            insertAfter = hwnd;
        }
        if (hdwp) {
            EndDeferWindowPos(hdwp);
        }
        return;
    }

    const bool leavingEdit = m_wasEditMode;
    if (leavingEdit) {
        m_wasEditMode = false;
        m_normalStacked.clear();
        // Strip WS_EX_TOPMOST from all Background/Bottom panels while WallpaperHost's
        // WM_WINDOWPOSCHANGING hook is still suspended so Windows cleanly demotes them
        // out of the topmost band before repinAll() places them behind normal apps.
        for (QsPanelWindow* w : m_panels) {
            if (!w || !w->isVisible() || !w->handle() || !w->winId()) {
                continue;
            }
            WlrLayershellAttached* ls = w->layerShell();
            const int layer = ls ? ls->layer() : WlrLayer::Top;
            if (layer == WlrLayer::Background || layer == WlrLayer::Bottom) {
                HWND hwnd = reinterpret_cast<HWND>(w->winId());
                if (hwnd && IsWindow(hwnd)) {
                    SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
                                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                }
            }
        }
        if (m_wallpaperHost) {
            m_wallpaperHost->setEditModeSuspended(false);
        }
        if (m_appBarManager) {
            m_appBarManager->setEditModeSuspended(false);
        }
    }

    // Normal desktop mode: strict z-order:
    //   Wallpaper -> Widgets -> Unpinned Dock (WlrLayer::Bottom)
    //   -> Normal/Maximized Apps ->
    //   Pinned/AutoHide Dock (WlrLayer::Top) -> Bar (WlrLayer::Top) -> Overlay
    QList<QsPanelWindow*> allTopDocks;
    QList<QsPanelWindow*> allBars;
    QList<QsPanelWindow*> allOverlays;
    bool needTopmostRestack = leavingEdit;
    bool bottomOrderChanged = false;

    for (QsPanelWindow* w : m_panels) {
        if (!w || !w->isVisible() || !w->winId()) {
            continue;
        }
        WlrLayershellAttached* ls = w->layerShell();
        const int layer = ls ? ls->layer() : WlrLayer::Top;
        const QString ns = ls ? ls->ns() : QString();

        if (layer == WlrLayer::Overlay) {
            if (m_wallpaperHost) {
                m_wallpaperHost->unregisterWallpaperWindow(w);
            }
            allOverlays.append(w);
            if (!m_normalStacked.contains(w)) {
                needTopmostRestack = true;
            }
            continue;
        }
        if (ns == QStringLiteral("sekaipop-dock")) {
            if (layer == WlrLayer::Top) {
                if (m_wallpaperHost) {
                    m_wallpaperHost->unregisterWallpaperWindow(w);
                }
                allTopDocks.append(w);
                if (!m_normalStacked.contains(w)) {
                    needTopmostRestack = true;
                }
                continue;
            }
            // Unpinned Dock (WlrLayer::Bottom): sits behind normal/maximized windows,
            // strictly above Desktop Widgets & Wallpaper.
            if (m_normalStacked.contains(w)) {
                continue;
            }
            if (m_wallpaperHost) {
                const bool focusable = (ls && ls->keyboardFocus() != WlrKeyboardFocus::None);
                m_wallpaperHost->registerBottomDockWindow(w, focusable);
            }
            m_normalStacked.insert(w);
            bottomOrderChanged = true;
            continue;
        }
        if (layer == WlrLayer::Top) {
            if (m_wallpaperHost) {
                m_wallpaperHost->unregisterWallpaperWindow(w);
            }
            allBars.append(w);
            if (!m_normalStacked.contains(w)) {
                needTopmostRestack = true;
            }
            continue;
        }
        if (m_normalStacked.contains(w)) {
            continue;
        }
        HWND hwnd = reinterpret_cast<HWND>(w->winId());
        if (layer == WlrLayer::Background && w->mask() == nullptr) {
            SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            if (m_wallpaperHost) {
                m_wallpaperHost->registerWallpaperWindow(w);
            }
        } else if (ns == QStringLiteral("sekaipop-visualizer")) {
            SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            if (m_wallpaperHost) {
                const bool focusable = (ls && ls->keyboardFocus() != WlrKeyboardFocus::None);
                m_wallpaperHost->registerVisualizerWindow(w, focusable);
            }
        } else if (layer == WlrLayer::Background || layer == WlrLayer::Bottom) {
            SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            if (m_wallpaperHost) {
                const bool focusable = (ls && ls->keyboardFocus() != WlrKeyboardFocus::None);
                m_wallpaperHost->registerWidgetWindow(w, false, focusable);
            }
        }
        m_normalStacked.insert(w);
        bottomOrderChanged = true;
    }

    if (needTopmostRestack) {
        for (QsPanelWindow* w : allTopDocks) {
            HWND hwnd = reinterpret_cast<HWND>(w->winId());
            if (!isPanelOnFullscreenMonitor(w)) {
                SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            }
            m_normalStacked.insert(w);
        }
        for (QsPanelWindow* w : allBars) {
            HWND hwnd = reinterpret_cast<HWND>(w->winId());
            if (!isPanelOnFullscreenMonitor(w)) {
                SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                // Keep live Ukikura Dynamic Island ("UkikuraNativeClass") visually and interactively
                // above SekaiPop's Top Bar on the same monitor while Bar remains above normal apps.
                HMONITOR barMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
                HWND uki = nullptr;
                while ((uki = FindWindowExW(nullptr, uki, L"UkikuraNativeClass", nullptr)) != nullptr) {
                    if (IsWindowVisible(uki) &&
                        (GetWindowLongPtrW(uki, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0 &&
                        MonitorFromWindow(uki, MONITOR_DEFAULTTONEAREST) == barMon) {
                        SetWindowPos(hwnd, uki, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                        break;
                    }
                }
            }
            m_normalStacked.insert(w);
        }
        for (QsPanelWindow* w : allOverlays) {
            HWND hwnd = reinterpret_cast<HWND>(w->winId());
            WlrLayershellAttached* ls = w->layerShell();
            const bool isHotEdge = ls && ls->ns() == QStringLiteral("sekaipop-hotedge");
            if (!isHotEdge || !isPanelOnFullscreenMonitor(w)) {
                SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            }
            m_normalStacked.insert(w);
        }
    }

    if ((leavingEdit || bottomOrderChanged) && m_wallpaperHost) {
        m_wallpaperHost->repinAll();
        markHitTestDirty();
        if (leavingEdit) {
            QTimer::singleShot(180, this, [this]() {
                if (!s_windowMgrShuttingDown && !isEditModeActive() && m_wallpaperHost) {
                    m_wallpaperHost->repinAll();
                    markHitTestDirty();
                }
            });
            scheduleBootCurtainCapture(1800);
        }
    }

    if (!m_uiReadyEmitted) {
        bool hasWallpaper = false;
        bool hasBar = !allBars.isEmpty();
        for (QsPanelWindow* w : m_panels) {
            if (!w || !w->isVisible() || !w->winId()) {
                continue;
            }
            WlrLayershellAttached* ls = w->layerShell();
            const int layer = ls ? ls->layer() : WlrLayer::Top;
            const QString ns = ls ? ls->ns() : QString();
            if (layer == WlrLayer::Background && w->mask() == nullptr) {
                hasWallpaper = true;
            } else if (layer == WlrLayer::Top && ns != QStringLiteral("sekaipop-dock")) {
                hasBar = true;
            }
        }
        if (hasWallpaper && hasBar) {
            m_uiReadyEmitted = true;
            Q_EMIT uiReady();
            scheduleBootCurtainCapture(2800);
            QTimer::singleShot(350, this, [this]() {
                if (!s_windowMgrShuttingDown && !m_curtainDropEmitted) {
                    m_curtainDropEmitted = true;
                    Q_EMIT curtainDropReady();
                }
            });
        } else if (hasWallpaper && !m_wallpaperSeen) {
            m_wallpaperSeen = true;
            // Fallback in case the user disabled the Bar (bar.visible = false)
            QTimer::singleShot(400, this, [this]() {
                if (!s_windowMgrShuttingDown && !m_uiReadyEmitted) {
                    m_uiReadyEmitted = true;
                    Q_EMIT uiReady();
                    scheduleBootCurtainCapture(2600);
                    if (!m_curtainDropEmitted) {
                        m_curtainDropEmitted = true;
                        Q_EMIT curtainDropReady();
                    }
                }
            });
        }
    }
}

void QsWindowManager::pollHitTests() {
    if (s_windowMgrShuttingDown) {
        return;
    }
    POINT pt = {};
    if (!GetCursorPos(&pt)) {
        if (m_hitTestDirty) {
            m_hitTestDirty = false;
            const QPoint offscreen(-32768, -32768);
            for (QsPanelWindow* w : std::as_const(m_panels)) {
                if (w && w->isVisible() && (w->mask() != nullptr || w->isClickThrough())) {
                    w->updateHitTest(offscreen);
                }
            }
        }
        return;
    }
    const QPoint cursor(pt.x, pt.y);
    if (m_hasCursorPos && cursor == m_lastCursorPos && !m_hitTestDirty) {
        if (++m_stationaryHitTicks >= 18 && m_hitTestTimer.interval() != 50) {
            m_hitTestTimer.setInterval(50);
        }
        return;
    }
    m_stationaryHitTicks = 0;
    if (m_hitTestTimer.interval() != 16) {
        m_hitTestTimer.setInterval(16);
    }
    m_lastCursorPos = cursor;
    m_hasCursorPos = true;
    m_hitTestDirty = false;

    for (QsPanelWindow* w : std::as_const(m_panels)) {
        if (w && w->isVisible() && (w->mask() != nullptr || w->isClickThrough())) {
            w->updateHitTest(cursor);
        }
    }
}

} // namespace SekaiPop::Quickshell
