#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace SekaiPop::Services {

class SettingsService;

/**
 * Provides SekaiPop unit palettes (Virtual Singer, Leo/need, MORE MORE JUMP!,
 * Vivid BAD SQUAD, Wonderlands x Showtime, Nightcord at 25:00, Monochrome) and
 * Light / Dark / Darker surface modes without Linux matugen/GTK/KDE side-effects.
 */
class ThemeService final : public QObject {
    Q_OBJECT

    Q_PROPERTY(QVariantList baseGroups READ baseGroups CONSTANT)
    Q_PROPERTY(QVariantList groups READ groups NOTIFY changed)
    Q_PROPERTY(QString group READ group NOTIFY changed)
    Q_PROPERTY(QString mode READ mode NOTIFY changed)
    Q_PROPERTY(bool dark READ dark NOTIFY changed)
    Q_PROPERTY(bool darker READ darker NOTIFY changed)
    Q_PROPERTY(QVariantMap palette READ palette NOTIFY changed)

public:
    explicit ThemeService(SettingsService* settings, QObject* parent = nullptr);

    QVariantList baseGroups() const { return m_baseGroups; }
    QVariantList groups() const { return m_allGroups; }
    QString group() const { return m_group; }
    QString mode() const { return m_mode; }
    bool dark() const { return m_mode == QStringLiteral("dark") || m_mode == QStringLiteral("darker"); }
    bool darker() const { return m_mode == QStringLiteral("darker"); }
    QVariantMap palette() const { return m_palette; }

    Q_INVOKABLE void setTheme(const QString& id);
    Q_INVOKABLE void setMode(const QString& mode);
    Q_INVOKABLE void toggleDark();
    Q_INVOKABLE void cycleTheme();
    Q_INVOKABLE void cycleMode();
    Q_INVOKABLE bool hasScreenTheme(const QString& screenName) const;
    Q_INVOKABLE QVariantMap paletteFor(const QString& screenName) const;
    Q_INVOKABLE void updateFromWallpaper(const QString& wallpaperPath);

Q_SIGNALS:
    void changed();

private:
    void initGroups();
    void updateActivePalette();
    QVariantMap computeWallpaperPalette(const QString& wallpaperPath) const;

    SettingsService* m_settings = nullptr;
    QVariantList m_baseGroups;
    QVariantList m_allGroups;
    QVariantMap m_wallpaperPalette;
    QString m_group = QStringLiteral("vs");
    QString m_mode = QStringLiteral("light");
    QVariantMap m_palette;
};

} // namespace SekaiPop::Services
