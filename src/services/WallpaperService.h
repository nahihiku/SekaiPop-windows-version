#pragma once

#include <QObject>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace SekaiPop::Services {

class SettingsService;

/**
 * Manages Windows desktop wallpaper paths and SekaiPop shader transition parameters.
 */
class WallpaperService final : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(bool perMonitor READ perMonitor NOTIFY changed)
    Q_PROPERTY(QString defaultPath READ defaultPath NOTIFY changed)
    Q_PROPERTY(QString transitionStyle READ transitionStyle NOTIFY changed)
    Q_PROPERTY(QStringList availableStyles READ availableStyles CONSTANT)
    Q_PROPERTY(QVariantList entries READ entries NOTIFY changed)

    Q_PROPERTY(int ringVariant READ ringVariant CONSTANT)
    Q_PROPERTY(qreal ringDistortion READ ringDistortion CONSTANT)
    Q_PROPERTY(qreal ringSpeed READ ringSpeed CONSTANT)
    Q_PROPERTY(qreal ringWidth READ ringWidth CONSTANT)
    Q_PROPERTY(qreal ringColorIntensity READ ringColorIntensity CONSTANT)
    Q_PROPERTY(QPointF ringCenter READ ringCenter CONSTANT)
    Q_PROPERTY(int ringDuration READ ringDuration CONSTANT)
    Q_PROPERTY(int transitionDuration READ transitionDuration CONSTANT)

    Q_PROPERTY(qreal starColorIntensity READ starColorIntensity CONSTANT)
    Q_PROPERTY(QPointF starCenter READ starCenter CONSTANT)
    Q_PROPERTY(qreal starThickness READ starThickness CONSTANT)
    Q_PROPERTY(qreal starPointLength READ starPointLength CONSTANT)
    Q_PROPERTY(qreal starCurvature READ starCurvature CONSTANT)
    Q_PROPERTY(qreal starElastic READ starElastic CONSTANT)
    Q_PROPERTY(qreal starSpin READ starSpin CONSTANT)

    Q_PROPERTY(QPointF blocksDirection READ blocksDirection CONSTANT)
    Q_PROPERTY(qreal blocksBandThickness READ blocksBandThickness CONSTANT)
    Q_PROPERTY(qreal blocksBlockSize READ blocksBlockSize CONSTANT)
    Q_PROPERTY(qreal blocksStagger READ blocksStagger CONSTANT)

public:
    explicit WallpaperService(SettingsService* settings, const QString& appBaseDir, QObject* parent = nullptr);

    bool ready() const { return m_ready; }
    bool perMonitor() const;
    QString defaultPath() const { return m_defaultPath; }
    QString transitionStyle() const { return m_transitionStyle; }
    QStringList availableStyles() const;
    QVariantList entries() const { return m_entries; }
    Q_INVOKABLE void refreshEntries();

    int ringVariant() const { return 0; }
    qreal ringDistortion() const { return 1.0; }
    qreal ringSpeed() const { return 1.0; }
    qreal ringWidth() const { return 0.44; }
    qreal ringColorIntensity() const { return 1.0; }
    QPointF ringCenter() const { return QPointF(0.5, 0.5); }
    int ringDuration() const { return 1650; }
    int transitionDuration() const { return 550; }

    qreal starColorIntensity() const { return 1.0; }
    QPointF starCenter() const { return QPointF(0.5, 0.5); }
    qreal starThickness() const { return 0.22; }
    qreal starPointLength() const { return 1.0; }
    qreal starCurvature() const { return 0.35; }
    qreal starElastic() const { return 0.12; }
    qreal starSpin() const { return 1.0; }

    QPointF blocksDirection() const { return QPointF(1.0, 0.6); }
    qreal blocksBandThickness() const { return 0.38; }
    qreal blocksBlockSize() const { return 24.0; }
    qreal blocksStagger() const { return 0.45; }

    Q_INVOKABLE QString pathFor(const QString& screenName) const;
    Q_INVOKABLE QString imageFor(const QString& path) const;
    Q_INVOKABLE bool videoFor(const QString& screenName) const;
    Q_INVOKABLE QString styleForSwitch() const;

    Q_INVOKABLE void setWallpaper(const QString& path, const QString& screenName = {});
    Q_INVOKABLE void setTransitionStyle(const QString& style);
    Q_INVOKABLE void pickWallpaperDialog(const QString& screenName = {});
    Q_INVOKABLE void nextInFolder(const QString& screenName = {});

Q_SIGNALS:
    void changed();

private:
    SettingsService* m_settings = nullptr;
    QString m_defaultPath;
    QString m_bundledDefault;
    QString m_transitionStyle = QStringLiteral("sekaiRing");
    QVariantMap m_screenPaths;
    QVariantList m_entries;
    bool m_ready = false;
    mutable int m_randomCounter = 0;
};

} // namespace SekaiPop::Services
