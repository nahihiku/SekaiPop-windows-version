#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>

namespace SekaiPop::Services {

class SettingsService;

/**
 * Fetches live weather and forecast from Open-Meteo (with IP geolocation fallback)
 * and caches readings to %LOCALAPPDATA%\SekaiPop\SekaiPopShell\weather-cache.json.
 */
class WeatherSource final : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString temperatureText READ temperatureText NOTIFY changed)
    Q_PROPERTY(QString iconName READ iconName NOTIFY changed)
    Q_PROPERTY(QString cityName READ cityName NOTIFY changed)
    Q_PROPERTY(QString conditionText READ conditionText NOTIFY changed)
    Q_PROPERTY(QString feelsLikeText READ feelsLikeText NOTIFY changed)
    Q_PROPERTY(int humidity READ humidity NOTIFY changed)
    Q_PROPERTY(QString windSpeedText READ windSpeedText NOTIFY changed)
    Q_PROPERTY(int windDeg READ windDeg NOTIFY changed)
    Q_PROPERTY(QString pressureText READ pressureText NOTIFY changed)
    Q_PROPERTY(qreal uvIndex READ uvIndex NOTIFY changed)
    Q_PROPERTY(QVariantList hourly READ hourly NOTIFY changed)
    Q_PROPERTY(QVariantList forecast READ forecast NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)

public:
    explicit WeatherSource(SettingsService* settings, QObject* parent = nullptr);

    QString temperatureText() const { return m_temperatureText; }
    QString iconName() const { return m_iconName; }
    QString cityName() const { return m_cityName; }
    QString conditionText() const { return m_conditionText; }
    QString feelsLikeText() const { return m_feelsLikeText; }
    int humidity() const { return m_humidity; }
    QString windSpeedText() const { return m_windSpeedText; }
    int windDeg() const { return m_windDeg; }
    QString pressureText() const { return m_pressureText; }
    qreal uvIndex() const { return m_uvIndex; }
    QVariantList hourly() const { return m_hourly; }
    QVariantList forecast() const { return m_forecast; }
    bool loading() const { return m_loading; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void refreshIfStale();

Q_SIGNALS:
    void changed();

private:
    void loadCache();
    void saveCache() const;
    void fetchCoordinatesThenWeather();
    void fetchForecast(double lat, double lon, const QString& city);
    void applyWeatherJson(const QByteArray& raw, const QString& city);

    SettingsService* m_settings = nullptr;
    QNetworkAccessManager m_net;
    QTimer m_timer;

    QString m_cachePath;
    qint64 m_lastFetchMs = 0;
    bool m_loading = false;

    double m_lat = -6.2088;
    double m_lon = 106.8456;
    QString m_cityName = QStringLiteral("Local");
    QString m_temperatureText;
    QString m_iconName = QStringLiteral("partly_cloudy_day");
    QString m_conditionText = QStringLiteral("Loading");
    QString m_feelsLikeText = QStringLiteral("--");
    int m_humidity = 0;
    QString m_windSpeedText = QStringLiteral("--");
    int m_windDeg = 0;
    QString m_pressureText = QStringLiteral("--");
    qreal m_uvIndex = 0.0;
    QVariantList m_hourly;
    QVariantList m_forecast;
};

} // namespace SekaiPop::Services
