#pragma once

#include <QAbstractListModel>
#include <QColor>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlListProperty>
#include <QQmlParserStatus>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

namespace SekaiPop::Services {
class AppSource;
}
namespace SekaiPop::Win32 {
class WindowTracker;
}

namespace SekaiPop::Quickshell {

QString normalizeWinPath(const QString& raw);

class QsScreen : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString name READ name NOTIFY geometryChanged)
    Q_PROPERTY(QString model READ model NOTIFY geometryChanged)
    Q_PROPERTY(int x READ x NOTIFY geometryChanged)
    Q_PROPERTY(int y READ y NOTIFY geometryChanged)
    Q_PROPERTY(int virtualX READ x NOTIFY geometryChanged)
    Q_PROPERTY(int virtualY READ y NOTIFY geometryChanged)
    Q_PROPERTY(int width READ width NOTIFY geometryChanged)
    Q_PROPERTY(int height READ height NOTIFY geometryChanged)
    Q_PROPERTY(qreal devicePixelRatio READ devicePixelRatio NOTIFY geometryChanged)
    Q_PROPERTY(qreal scale READ devicePixelRatio NOTIFY geometryChanged)

public:
    explicit QsScreen(QScreen* screen, QObject* parent = nullptr);

    QScreen* qscreen() const { return m_screen; }
    QString name() const;
    QString model() const;
    int x() const;
    int y() const;
    int width() const;
    int height() const;
    qreal devicePixelRatio() const;

Q_SIGNALS:
    void geometryChanged();

private:
    QPointer<QScreen> m_screen;
};

class QsGlobal : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList screens READ screens NOTIFY screensChanged)
    Q_PROPERTY(QString shellDir READ shellDir CONSTANT)
    Q_PROPERTY(QString workingDirectory READ shellDir CONSTANT)
    Q_PROPERTY(bool inhibitReloadPopup READ inhibitReloadPopup WRITE setInhibitReloadPopup NOTIFY inhibitReloadPopupChanged)
    Q_PROPERTY(QString clipboardText READ clipboardText WRITE setClipboardText NOTIFY clipboardTextChanged)
    Q_PROPERTY(QVariantMap ukikuraNotches READ ukikuraNotches NOTIFY ukikuraNotchesChanged)

public:
    explicit QsGlobal(const QString& shellDir, Services::AppSource* appSource, Win32::WindowTracker* tracker = nullptr, QObject* parent = nullptr);
    static QsGlobal* instance();

    QVariantList screens() const { return m_screens; }
    QsScreen* screenFor(QScreen* s) const;
    QsScreen* screenByName(const QString& name) const;
    QsScreen* primaryQsScreen() const;

    QString shellDir() const { return m_shellDir; }
    QString stateHome() const { return m_stateHome; }
    QString configHome() const { return m_configHome; }
    QString cacheHome() const { return m_cacheHome; }
    QString runtimeDir() const { return m_runtimeDir; }

    bool inhibitReloadPopup() const { return m_inhibitReloadPopup; }
    void setInhibitReloadPopup(bool v);

    QString clipboardText() const;
    void setClipboardText(const QString& text);
    QVariantMap ukikuraNotches() const { return m_ukikuraNotches; }

    Q_INVOKABLE QString env(const QString& name) const;
    Q_INVOKABLE void execDetached(const QVariant& command);
    Q_INVOKABLE QString iconPath(const QString& icon, const QVariant& fallback = QVariant()) const;
    Q_INVOKABLE void reload(bool hard = false);
    Q_INVOKABLE QString shellPath(const QString& rel) const;
    Q_INVOKABLE QString cachePath(const QString& rel) const;
    Q_INVOKABLE QString editPreviewFilePath(const QString& themeKey, const QString& widgetId) const;
    Q_INVOKABLE void saveEditPreviewMeta(const QString& themeKey, const QString& widgetId, qreal width, qreal height) const;
    Q_INVOKABLE QVariantMap loadEditPreviewCache(const QString& themeKey) const;

Q_SIGNALS:
    void screensChanged();
    void inhibitReloadPopupChanged();
    void clipboardTextChanged();
    void ukikuraNotchesChanged();
    void reloadCompleted();
    void reloadFailed(const QString& errorString);

private:
    void rebuildScreens();
    void pollUkikuraNotches();

    static QsGlobal* s_instance;
    QString m_shellDir;
    QString m_homeDir;
    QString m_stateHome;
    QString m_configHome;
    QString m_cacheHome;
    QString m_runtimeDir;
    Services::AppSource* m_appSource = nullptr;
    Win32::WindowTracker* m_windowTracker = nullptr;
    QList<QsScreen*> m_screenObjs;
    QVariantList m_screens;
    QVariantMap m_ukikuraNotches;
    QTimer m_ukikuraTimer;
    qint64 m_ukikuraCfgMtime = -1;
    qreal m_ukikuraScaleMul = 1.0;
    bool m_ukikuraFeatureMusic = true;
    bool m_ukikuraMinimal = false;
    bool m_inhibitReloadPopup = false;
};

class QsWindowAttached : public QObject {
    Q_OBJECT
    Q_PROPERTY(QQuickWindow* window READ window NOTIFY windowChanged)

public:
    explicit QsWindowAttached(QObject* object);
    QQuickWindow* window() const;

Q_SIGNALS:
    void windowChanged();

private:
    void attachToHierarchy();
    QPointer<QQuickItem> m_item;
    QPointer<QQuickWindow> m_directWindow;
};

class QsWindow : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    static QsWindowAttached* qmlAttachedProperties(QObject* object) {
        return new QsWindowAttached(object);
    }
};

class QsShellSettings : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool watchFiles READ watchFiles WRITE setWatchFiles NOTIFY watchFilesChanged)
public:
    using QObject::QObject;
    bool watchFiles() const { return m_watchFiles; }
    void setWatchFiles(bool v) {
        if (m_watchFiles != v) {
            m_watchFiles = v;
            Q_EMIT watchFilesChanged();
        }
    }
Q_SIGNALS:
    void watchFilesChanged();
private:
    bool m_watchFiles = true;
};

class QsShellRoot : public QObject {
    Q_OBJECT
    Q_PROPERTY(QsShellSettings* settings READ settings CONSTANT)
    Q_PROPERTY(QQmlListProperty<QObject> data READ data)
    Q_CLASSINFO("DefaultProperty", "data")

public:
    explicit QsShellRoot(QObject* parent = nullptr);
    QsShellSettings* settings() const { return m_settings; }
    QQmlListProperty<QObject> data();

private:
    QsShellSettings* m_settings = nullptr;
    QList<QObject*> m_data;
};

class QsScope : public QObject {
    Q_OBJECT
    Q_PROPERTY(QQmlListProperty<QObject> data READ data)
    Q_CLASSINFO("DefaultProperty", "data")

public:
    explicit QsScope(QObject* parent = nullptr) : QObject(parent) {}
    QQmlListProperty<QObject> data();

private:
    QList<QObject*> m_data;
};

class QsSingleton : public QsScope {
    Q_OBJECT
public:
    using QsScope::QsScope;
};

class QsVariants : public QObject, public QQmlParserStatus {
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)
    Q_PROPERTY(QVariant model READ model WRITE setModel NOTIFY modelChanged)
    Q_PROPERTY(QQmlComponent* delegate READ delegate WRITE setDelegate NOTIFY delegateChanged)
    Q_PROPERTY(QVariantList instances READ instances NOTIFY instancesChanged)
    Q_CLASSINFO("DefaultProperty", "delegate")

public:
    explicit QsVariants(QObject* parent = nullptr) : QObject(parent) {}
    ~QsVariants() override;

    QVariant model() const { return m_model; }
    void setModel(const QVariant& m);

    QQmlComponent* delegate() const { return m_delegate; }
    void setDelegate(QQmlComponent* d);

    QVariantList instances() const { return m_instances; }

    void classBegin() override {}
    void componentComplete() override;

Q_SIGNALS:
    void modelChanged();
    void delegateChanged();
    void instancesChanged();

private:
    void rebuild(bool forceRecreate = false);

    QVariant m_model;
    QPointer<QQmlComponent> m_delegate;
    QVariantList m_items;
    QList<QObject*> m_created;
    QVariantList m_instances;
    bool m_complete = false;
};

class QsLazyLoader : public QObject, public QQmlParserStatus {
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(bool activeAsync READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(QVariantMap initialProperties READ initialProperties WRITE setInitialProperties NOTIFY initialPropertiesChanged)
    Q_PROPERTY(QQmlComponent* component READ sourceComponent WRITE setSourceComponent NOTIFY sourceComponentChanged)
    Q_PROPERTY(QQmlComponent* sourceComponent READ sourceComponent WRITE setSourceComponent NOTIFY sourceComponentChanged)
    Q_PROPERTY(QObject* item READ item NOTIFY itemChanged)
    Q_CLASSINFO("DefaultProperty", "sourceComponent")

public:
    explicit QsLazyLoader(QObject* parent = nullptr) : QObject(parent) {}
    ~QsLazyLoader() override;

    bool active() const { return m_active; }
    void setActive(bool a);

    bool loading() const { return false; }

    QUrl source() const { return m_source; }
    void setSource(const QUrl& url);

    QVariantMap initialProperties() const { return m_initialProperties; }
    void setInitialProperties(const QVariantMap& props);

    QQmlComponent* sourceComponent() const { return m_component; }
    void setSourceComponent(QQmlComponent* c);

    QObject* item() const { return m_item; }

    void classBegin() override {}
    void componentComplete() override;

Q_SIGNALS:
    void activeChanged();
    void loadingChanged();
    void sourceChanged();
    void initialPropertiesChanged();
    void sourceComponentChanged();
    void itemChanged();

private:
    void updateInstance();

    bool m_active = false;
    bool m_complete = false;
    QUrl m_source;
    QVariantMap m_initialProperties;
    QPointer<QQmlComponent> m_component;
    QPointer<QQmlComponent> m_ownedComponent;
    QPointer<QObject> m_item;
};

class QsSystemClock : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(int precision READ precision WRITE setPrecision NOTIFY precisionChanged)
    Q_PROPERTY(QDateTime date READ date NOTIFY dateChanged)
    Q_PROPERTY(int hours READ hours NOTIFY dateChanged)
    Q_PROPERTY(int minutes READ minutes NOTIFY dateChanged)
    Q_PROPERTY(int seconds READ seconds NOTIFY dateChanged)

public:
    enum Precision {
        Hours = 0,
        Minutes = 1,
        Seconds = 2
    };
    Q_ENUM(Precision)

    explicit QsSystemClock(QObject* parent = nullptr);

    bool enabled() const { return m_enabled; }
    void setEnabled(bool e);

    int precision() const { return m_precision; }
    void setPrecision(int p);

    QDateTime date() const { return m_date; }
    int hours() const { return m_date.time().hour(); }
    int minutes() const { return m_date.time().minute(); }
    int seconds() const { return m_date.time().second(); }

Q_SIGNALS:
    void enabledChanged();
    void precisionChanged();
    void dateChanged();

private:
    void tick();
    void updateTimer();

    bool m_enabled = true;
    int m_precision = Seconds;
    QDateTime m_date;
    QTimer m_timer;
};

class QsIpcHandler : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString target READ target WRITE setTarget NOTIFY targetChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)

public:
    explicit QsIpcHandler(QObject* parent = nullptr);
    ~QsIpcHandler() override;

    QString target() const { return m_target; }
    void setTarget(const QString& t);

    bool enabled() const { return m_enabled; }
    void setEnabled(bool e);

    static QString dispatchCall(const QString& target, const QString& func, const QStringList& args = {});

Q_SIGNALS:
    void targetChanged();
    void enabledChanged();

private:
    QString m_target;
    bool m_enabled = true;
};

class QsShellEmergencyIpc : public QsIpcHandler {
    Q_OBJECT
public:
    explicit QsShellEmergencyIpc(QObject* parent = nullptr) : QsIpcHandler(parent) {
        setTarget(QStringLiteral("shell"));
    }

    Q_INVOKABLE void exit() { Q_EMIT exitRequested(); }
    Q_INVOKABLE void restart() { Q_EMIT restartRequested(); }
    Q_INVOKABLE void toggletaskbar() { Q_EMIT toggleTaskbarRequested(); }
    Q_INVOKABLE void restoretaskbar() { Q_EMIT restoreTaskbarRequested(); }

Q_SIGNALS:
    void exitRequested();
    void restartRequested();
    void toggleTaskbarRequested();
    void restoreTaskbarRequested();
};

class QsScriptModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QVariantList values READ values WRITE setValues NOTIFY valuesChanged)
    Q_PROPERTY(QString objectProp READ objectProp WRITE setObjectProp NOTIFY objectPropChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY valuesChanged)

public:
    explicit QsScriptModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}

    QVariantList values() const { return m_values; }
    void setValues(const QVariantList& v);

    QString objectProp() const { return m_objectProp; }
    void setObjectProp(const QString& p);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE int indexOf(const QVariant& item) const { return m_values.indexOf(item); }

Q_SIGNALS:
    void valuesChanged();
    void objectPropChanged();

private:
    QVariantList m_values;
    QString m_objectProp;
};

class QsColorQuantizer : public QObject {
    Q_OBJECT
    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(int depth READ depth WRITE setDepth NOTIFY depthChanged)
    Q_PROPERTY(int rescaleSize READ rescaleSize WRITE setRescaleSize NOTIFY rescaleSizeChanged)
    Q_PROPERTY(QVariantList colors READ colors NOTIFY colorsChanged)

public:
    explicit QsColorQuantizer(QObject* parent = nullptr);

    QUrl source() const { return m_source; }
    void setSource(const QUrl& u);

    int depth() const { return m_depth; }
    void setDepth(int d);

    int rescaleSize() const { return m_rescaleSize; }
    void setRescaleSize(int s);

    QVariantList colors() const { return m_colors; }

Q_SIGNALS:
    void sourceChanged();
    void depthChanged();
    void rescaleSizeChanged();
    void colorsChanged();

private:
    void quantize();

    QUrl m_source;
    int m_depth = 3;
    int m_rescaleSize = 64;
    QVariantList m_colors;
};

class QsObjectModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QVariantList values READ values NOTIFY valuesChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY valuesChanged)

public:
    explicit QsObjectModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}

    QVariantList values() const { return m_values; }
    void setValues(const QVariantList& v);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

Q_SIGNALS:
    void valuesChanged();

private:
    QVariantList m_values;
};

class QsMenuButtonType : public QObject {
    Q_OBJECT
public:
    enum Type {
        None = 0,
        CheckBox = 1,
        RadioButton = 2
    };
    Q_ENUM(Type)
    using QObject::QObject;
};

class QsMenuOpener : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariant menu READ menu WRITE setMenu NOTIFY menuChanged)
    Q_PROPERTY(QsObjectModel* children READ children CONSTANT)

public:
    explicit QsMenuOpener(QObject* parent = nullptr)
        : QObject(parent), m_children(new QsObjectModel(this)) {}
    QVariant menu() const { return m_menu; }
    void setMenu(const QVariant& m) {
        if (m_menu != m) {
            m_menu = m;
            Q_EMIT menuChanged();
        }
    }
    QsObjectModel* children() const { return m_children; }

Q_SIGNALS:
    void menuChanged();

private:
    QVariant m_menu;
    QsObjectModel* m_children = nullptr;
};

class ExclusionMode : public QObject {
    Q_OBJECT
public:
    enum Mode {
        Normal = 0,
        Ignore = 1,
        Auto = 2
    };
    Q_ENUM(Mode)
    using QObject::QObject;
};

class Edges : public QObject {
    Q_OBJECT
public:
    enum Edge {
        None = 0,
        Top = 1,
        Bottom = 2,
        Left = 4,
        Right = 8
    };
    Q_ENUM(Edge)
    using QObject::QObject;
};

class Intersection : public QObject {
    Q_OBJECT
public:
    enum Mode {
        Combine = 0,
        Subtract = 1,
        Intersect = 2,
        Xor = 3
    };
    Q_ENUM(Mode)
    using QObject::QObject;
};

class PopupAdjustment : public QObject {
    Q_OBJECT
public:
    enum Adjustment {
        None = 0,
        SlideX = 1,
        SlideY = 2,
        Slide = 3,
        FlipX = 4,
        FlipY = 8,
        Flip = 12,
        ResizeX = 16,
        ResizeY = 32,
        Resize = 48,
        All = 63
    };
    Q_ENUM(Adjustment)
    using QObject::QObject;
};

} // namespace SekaiPop::Quickshell

QML_DECLARE_TYPEINFO(SekaiPop::Quickshell::QsWindow, QML_HAS_ATTACHED_PROPERTIES)
