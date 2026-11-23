#include "WeatherSource.h"
#include "SettingsService.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace SekaiPop::Services {

namespace {

struct WmoDesc {
    const char* label;
    const char* dayIcon;
    const char* nightIcon;
};

WmoDesc describeWmo(int code) {
    if (code == 0) return { "Clear sky", "clear_day", "bedtime" };
    if (code == 1 || code == 2) return { "Partly cloudy", "partly_cloudy_day", "partly_cloudy_night" };
    if (code == 3) return { "Overcast", "cloud", "cloud" };
    if (code == 45 || code == 48) return { "Foggy", "foggy", "foggy" };
    if (code >= 51 && code <= 57) return { "Drizzle", "rainy_light", "rainy_light" };
    if (code >= 61 && code <= 67) return { "Rain", "rainy", "rainy" };
    if (code >= 71 && code <= 77) return { "Snow", "weather_snowy", "weather_snowy" };
    if (code >= 80 && code <= 82) return { "Showers", "rainy_heavy", "rainy_heavy" };
    if (code >= 85 && code <= 86) return { "Snow showers", "weather_snowy", "weather_snowy" };
    if (code >= 95) return { "Thunderstorm", "thunderstorm", "thunderstorm" };
    return { "Cloudy", "cloud", "cloud" };
}

} // namespace

WeatherSource::WeatherSource(SettingsService* settings, QObject* parent)
    : QObject(parent)
    , m_settings(settings) {
    const QString dir = SettingsService::stateDir();
    QDir().mkpath(dir);
    m_cachePath = QDir(dir).filePath(QStringLiteral("weather-cache.json"));

    loadCache();

    m_timer.setInterval(20 * 60 * 1000);
    connect(&m_timer, &QTimer::timeout, this, &WeatherSource::refresh);
    m_timer.start();

    QTimer::singleShot(1200, this, &WeatherSource::refreshIfStale);
}

void WeatherSource::loadCache() {
    QFile f(m_cachePath);
    if (!f.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) {
        return;
    }
    const QJsonObject root = doc.object();
    m_lastFetchMs = static_cast<qint64>(root.value(QStringLiteral("fetchedMs")).toDouble(0));
    m_lat = root.value(QStringLiteral("lat")).toDouble(m_lat);
    m_lon = root.value(QStringLiteral("lon")).toDouble(m_lon);
    const QString city = root.value(QStringLiteral("city")).toString();
    const QJsonObject payload = root.value(QStringLiteral("payload")).toObject();
    if (!payload.isEmpty()) {
        applyWeatherJson(QJsonDocument(payload).toJson(QJsonDocument::Compact), city);
    }
}

void WeatherSource::saveCache() const {
    // Saved inside applyWeatherJson via explicit payload.
}

void WeatherSource::refreshIfStale() {
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (m_temperatureText.isEmpty() || (nowMs - m_lastFetchMs) > 15 * 60 * 1000) {
        refresh();
    }
}

void WeatherSource::refresh() {
    if (m_loading) {
        return;
    }
    m_loading = true;
    Q_EMIT changed();

    const QString configuredCity = m_settings ? m_settings->get(QStringLiteral("weather.city")).toString().trimmed() : QString();
    if (!configuredCity.isEmpty() && configuredCity != m_cityName) {
        const QUrl geoUrl(QStringLiteral("https://geocoding-api.open-meteo.com/v1/search?name=%1&count=1&format=json")
            .arg(QString::fromUtf8(QUrl::toPercentEncoding(configuredCity))));
        QNetworkRequest req(geoUrl);
        req.setTransferTimeout(6000);
        QNetworkReply* reply = m_net.get(req);
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            reply->deleteLater();
            if (reply->error() == QNetworkReply::NoError) {
                const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
                const QJsonArray results = obj.value(QStringLiteral("results")).toArray();
                if (!results.isEmpty()) {
                    const QJsonObject r = results.first().toObject();
                    m_lat = r.value(QStringLiteral("latitude")).toDouble(m_lat);
                    m_lon = r.value(QStringLiteral("longitude")).toDouble(m_lon);
                    fetchForecast(m_lat, m_lon, r.value(QStringLiteral("name")).toString());
                    return;
                }
            }
            fetchCoordinatesThenWeather();
        });
        return;
    }

    fetchCoordinatesThenWeather();
}

void WeatherSource::fetchCoordinatesThenWeather() {
    if (m_lastFetchMs > 0 && !m_cityName.isEmpty() && m_cityName != QStringLiteral("Local")) {
        fetchForecast(m_lat, m_lon, m_cityName);
        return;
    }

    QNetworkRequest req(QUrl(QStringLiteral("https://ipwho.is/")));
    req.setTransferTimeout(5000);
    QNetworkReply* reply = m_net.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        QString city = m_cityName;
        if (reply->error() == QNetworkReply::NoError) {
            const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
            if (obj.value(QStringLiteral("success")).toBool(true)) {
                m_lat = obj.value(QStringLiteral("latitude")).toDouble(m_lat);
                m_lon = obj.value(QStringLiteral("longitude")).toDouble(m_lon);
                const QString c = obj.value(QStringLiteral("city")).toString().trimmed();
                if (!c.isEmpty()) {
                    city = c;
                }
            }
        }
        fetchForecast(m_lat, m_lon, city);
    });
}

void WeatherSource::fetchForecast(double lat, double lon, const QString& city) {
    const QString urlStr = QStringLiteral(
        "https://api.open-meteo.com/v1/forecast?latitude=%1&longitude=%2"
        "&current=temperature_2m,relative_humidity_2m,apparent_temperature,is_day,weather_code,surface_pressure,wind_speed_10m,wind_direction_10m"
        "&hourly=temperature_2m,precipitation_probability,weather_code"
        "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,uv_index_max"
        "&forecast_days=7&timezone=auto")
        .arg(lat, 0, 'f', 4)
        .arg(lon, 0, 'f', 4);

    QNetworkRequest req{ QUrl(urlStr) };
    req.setTransferTimeout(7000);
    QNetworkReply* reply = m_net.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, lat, lon, city]() {
        reply->deleteLater();
        m_loading = false;
        if (reply->error() == QNetworkReply::NoError) {
            const QByteArray raw = reply->readAll();
            const QJsonDocument doc = QJsonDocument::fromJson(raw);
            if (doc.isObject() && doc.object().contains(QStringLiteral("current"))) {
                m_lastFetchMs = QDateTime::currentMSecsSinceEpoch();
                applyWeatherJson(raw, city);

                QFile f(m_cachePath);
                if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                    QJsonObject cache;
                    cache.insert(QStringLiteral("fetchedMs"), static_cast<double>(m_lastFetchMs));
                    cache.insert(QStringLiteral("lat"), lat);
                    cache.insert(QStringLiteral("lon"), lon);
                    cache.insert(QStringLiteral("city"), city);
                    cache.insert(QStringLiteral("payload"), doc.object());
                    f.write(QJsonDocument(cache).toJson(QJsonDocument::Compact));
                }
                return;
            }
        }
        if (m_temperatureText.isEmpty()) {
            m_temperatureText = QStringLiteral("--");
            m_conditionText = QStringLiteral("Offline");
        }
        Q_EMIT changed();
    });
}

void WeatherSource::applyWeatherJson(const QByteArray& raw, const QString& city) {
    const QJsonObject root = QJsonDocument::fromJson(raw).object();
    const QJsonObject cur = root.value(QStringLiteral("current")).toObject();
    if (cur.isEmpty()) {
        return;
    }

    if (!city.isEmpty()) {
        m_cityName = city;
    }

    const int temp = qRound(cur.value(QStringLiteral("temperature_2m")).toDouble(0.0));
    const int feels = qRound(cur.value(QStringLiteral("apparent_temperature")).toDouble(temp));
    const int code = cur.value(QStringLiteral("weather_code")).toInt(0);
    const bool isDay = cur.value(QStringLiteral("is_day")).toInt(1) != 0;
    const WmoDesc desc = describeWmo(code);

    m_temperatureText = QStringLiteral("%1\u00B0C").arg(temp);
    m_feelsLikeText = QStringLiteral("%1\u00B0C").arg(feels);
    m_iconName = QString::fromLatin1(isDay ? desc.dayIcon : desc.nightIcon);
    m_conditionText = QString::fromLatin1(desc.label);
    m_humidity = qBound(0, cur.value(QStringLiteral("relative_humidity_2m")).toInt(0), 100);
    m_windSpeedText = QStringLiteral("%1 km/h").arg(qRound(cur.value(QStringLiteral("wind_speed_10m")).toDouble(0.0)));
    m_windDeg = cur.value(QStringLiteral("wind_direction_10m")).toInt(0);
    m_pressureText = QStringLiteral("%1 hPa").arg(qRound(cur.value(QStringLiteral("surface_pressure")).toDouble(1013.0)));

    const QJsonObject daily = root.value(QStringLiteral("daily")).toObject();
    const QJsonArray dDates = daily.value(QStringLiteral("time")).toArray();
    const QJsonArray dCodes = daily.value(QStringLiteral("weather_code")).toArray();
    const QJsonArray dMax = daily.value(QStringLiteral("temperature_2m_max")).toArray();
    const QJsonArray dMin = daily.value(QStringLiteral("temperature_2m_min")).toArray();
    const QJsonArray dRain = daily.value(QStringLiteral("precipitation_probability_max")).toArray();
    const QJsonArray dUv = daily.value(QStringLiteral("uv_index_max")).toArray();

    m_uvIndex = dUv.isEmpty() ? 0.0 : dUv.first().toDouble(0.0);

    QVariantList nextForecast;
    for (int i = 0; i < dDates.size() && i < 7; ++i) {
        const QDate dt = QDate::fromString(dDates.at(i).toString(), Qt::ISODate);
        const WmoDesc dDesc = describeWmo(dCodes.at(i).toInt(0));
        nextForecast.push_back(QVariantMap{
            { QStringLiteral("day"), i == 0 ? QStringLiteral("Today") : dt.toString(QStringLiteral("ddd")) },
            { QStringLiteral("icon"), QString::fromLatin1(dDesc.dayIcon) },
            { QStringLiteral("label"), QString::fromLatin1(dDesc.label) },
            { QStringLiteral("high"), qRound(dMax.at(i).toDouble(0.0)) },
            { QStringLiteral("low"), qRound(dMin.at(i).toDouble(0.0)) },
            { QStringLiteral("rain"), dRain.at(i).toInt(0) }
        });
    }
    m_forecast = std::move(nextForecast);

    const QJsonObject hourlyObj = root.value(QStringLiteral("hourly")).toObject();
    const QJsonArray hTimes = hourlyObj.value(QStringLiteral("time")).toArray();
    const QJsonArray hTemps = hourlyObj.value(QStringLiteral("temperature_2m")).toArray();
    const QJsonArray hRain = hourlyObj.value(QStringLiteral("precipitation_probability")).toArray();
    const QJsonArray hCodes = hourlyObj.value(QStringLiteral("weather_code")).toArray();

    const QString nowPrefix = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-ddTHH"));
    int startIdx = 0;
    for (int i = 0; i < hTimes.size(); ++i) {
        if (hTimes.at(i).toString() >= nowPrefix) {
            startIdx = i;
            break;
        }
    }

    QVariantList nextHourly;
    for (int i = startIdx; i < hTimes.size() && nextHourly.size() < 8; i += 2) {
        const QString ts = hTimes.at(i).toString();
        const int hr = ts.mid(11, 2).toInt();
        const WmoDesc hDesc = describeWmo(hCodes.at(i).toInt(0));
        nextHourly.push_back(QVariantMap{
            { QStringLiteral("time"), QStringLiteral("%1:00").arg(hr, 2, 10, QLatin1Char('0')) },
            { QStringLiteral("temp"), qRound(hTemps.at(i).toDouble(0.0)) },
            { QStringLiteral("rain"), hRain.at(i).toInt(0) },
            { QStringLiteral("icon"), QString::fromLatin1(hr >= 6 && hr < 19 ? hDesc.dayIcon : hDesc.nightIcon) }
        });
    }
    m_hourly = std::move(nextHourly);

    Q_EMIT changed();
}

} // namespace SekaiPop::Services
