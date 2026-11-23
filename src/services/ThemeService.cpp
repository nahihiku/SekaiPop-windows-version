#include "ThemeService.h"
#include "SettingsService.h"

#include <QColor>
#include <QFileInfo>
#include <QImage>
#include <algorithm>
#include <cmath>

namespace SekaiPop::Services {

namespace {

QVariantMap makeGroup(const QString& id, const QString& name, const QVariantMap& colors) {
    return {
        { QStringLiteral("id"), id },
        { QStringLiteral("name"), name },
        { QStringLiteral("c"), colors }
    };
}

QVariantMap defaultMikuWallpaperPalette() {
    return {
        { QStringLiteral("border"), QStringLiteral("#1b4554") },
        { QStringLiteral("A"), QStringLiteral("#88d2f0") },
        { QStringLiteral("B"), QStringLiteral("#b5e8f2") },
        { QStringLiteral("deep"), QStringLiteral("#0f262f") },
        { QStringLiteral("rimHi"), QStringLiteral("#72a3b5") },
        { QStringLiteral("bezel"), QStringLiteral("#153642") },
        { QStringLiteral("wallLight"), QStringLiteral("#92becf") },
        { QStringLiteral("wallMid"), QStringLiteral("#2b677c") },
        { QStringLiteral("wallDark"), QStringLiteral("#183c49") },
        { QStringLiteral("hardware"), QStringLiteral("#0a1b22") },
        { QStringLiteral("divider"), QStringLiteral("#dfecee") },
        { QStringLiteral("lens"), QStringLiteral("#266c8a") },
        { QStringLiteral("glint"), QStringLiteral("#a3dff5") },
        { QStringLiteral("hoverTint"), QStringLiteral("#ecf8fc") },
        { QStringLiteral("boardBg"), QStringLiteral("#f3fafc") },
        { QStringLiteral("boardDot"), QStringLiteral("#cbe1ea") },
        { QStringLiteral("lockMint"), QStringLiteral("#7fe8d8") },
        { QStringLiteral("lockPink"), QStringLiteral("#ff75ac") }
    };
}

QString hexOf(const QColor& c) {
    return c.name(QColor::HexRgb);
}

QColor mixColor(const QColor& a, const QColor& b, double t) {
    const double k = std::clamp(t, 0.0, 1.0);
    return QColor::fromRgbF(
        a.redF() + (b.redF() - a.redF()) * k,
        a.greenF() + (b.greenF() - a.greenF()) * k,
        a.blueF() + (b.blueF() - a.blueF()) * k
    );
}

} // namespace

ThemeService::ThemeService(SettingsService* settings, QObject* parent)
    : QObject(parent)
    , m_settings(settings) {
    if (m_settings) {
        m_wallpaperPalette = computeWallpaperPalette(
            m_settings->get(QStringLiteral("wallpaper.defaultPath")).toString()
        );
        m_group = m_settings->get(QStringLiteral("theme.group")).toString();
        m_mode = m_settings->get(QStringLiteral("theme.mode")).toString();
        connect(m_settings, &SettingsService::changed, this, [this]() {
            updateFromWallpaper(m_settings->get(QStringLiteral("wallpaper.defaultPath")).toString());
        });
    } else {
        m_wallpaperPalette = defaultMikuWallpaperPalette();
    }
    initGroups();
    updateActivePalette();
}

void ThemeService::initGroups() {
    m_baseGroups = {
        makeGroup(QStringLiteral("vs"), QStringLiteral("Virtual Singer"), {
            { QStringLiteral("border"), QStringLiteral("#214858") },
            { QStringLiteral("A"), QStringLiteral("#04feee") },
            { QStringLiteral("B"), QStringLiteral("#00ffcb") },
            { QStringLiteral("deep"), QStringLiteral("#132832") },
            { QStringLiteral("rimHi"), QStringLiteral("#79a6b8") },
            { QStringLiteral("bezel"), QStringLiteral("#183946") },
            { QStringLiteral("wallLight"), QStringLiteral("#96c1d1") },
            { QStringLiteral("wallMid"), QStringLiteral("#2d6980") },
            { QStringLiteral("wallDark"), QStringLiteral("#1a3f4d") },
            { QStringLiteral("hardware"), QStringLiteral("#0c1d24") },
            { QStringLiteral("divider"), QStringLiteral("#e1ecee") },
            { QStringLiteral("lens"), QStringLiteral("#286e8c") },
            { QStringLiteral("glint"), QStringLiteral("#9eddf5") },
            { QStringLiteral("hoverTint"), QStringLiteral("#d9fff6") },
            { QStringLiteral("boardBg"), QStringLiteral("#f3fbf9") },
            { QStringLiteral("boardDot"), QStringLiteral("#cfe6e1") },
            { QStringLiteral("lockMint"), QStringLiteral("#7fe8d8") },
            { QStringLiteral("lockPink"), QStringLiteral("#ff75ac") }
        }),
        makeGroup(QStringLiteral("leoneed"), QStringLiteral("Leo/need"), {
            { QStringLiteral("border"), QStringLiteral("#2d3775") },
            { QStringLiteral("A"), QStringLiteral("#8ea3ff") },
            { QStringLiteral("B"), QStringLiteral("#9fd8ff") },
            { QStringLiteral("deep"), QStringLiteral("#181e3f") },
            { QStringLiteral("rimHi"), QStringLiteral("#808bce") },
            { QStringLiteral("bezel"), QStringLiteral("#2a3265") },
            { QStringLiteral("wallLight"), QStringLiteral("#a8b0de") },
            { QStringLiteral("wallMid"), QStringLiteral("#4958b8") },
            { QStringLiteral("wallDark"), QStringLiteral("#313a75") },
            { QStringLiteral("hardware"), QStringLiteral("#12162e") },
            { QStringLiteral("divider"), QStringLiteral("#dee1f2") },
            { QStringLiteral("lens"), QStringLiteral("#5968b3") },
            { QStringLiteral("glint"), QStringLiteral("#c1ccff") },
            { QStringLiteral("hoverTint"), QStringLiteral("#f1f9ff") },
            { QStringLiteral("boardBg"), QStringLiteral("#f3f8fb") },
            { QStringLiteral("boardDot"), QStringLiteral("#cfdde6") }
        }),
        makeGroup(QStringLiteral("mmj"), QStringLiteral("MORE MORE JUMP!"), {
            { QStringLiteral("border"), QStringLiteral("#39662d") },
            { QStringLiteral("A"), QStringLiteral("#a4ee55") },
            { QStringLiteral("B"), QStringLiteral("#7fe9a8") },
            { QStringLiteral("deep"), QStringLiteral("#223c1b") },
            { QStringLiteral("rimHi"), QStringLiteral("#93c985") },
            { QStringLiteral("bezel"), QStringLiteral("#38622d") },
            { QStringLiteral("wallLight"), QStringLiteral("#b6daac") },
            { QStringLiteral("wallMid"), QStringLiteral("#64b150") },
            { QStringLiteral("wallDark"), QStringLiteral("#427135") },
            { QStringLiteral("hardware"), QStringLiteral("#192c14") },
            { QStringLiteral("divider"), QStringLiteral("#e3f1df") },
            { QStringLiteral("lens"), QStringLiteral("#69a33f") },
            { QStringLiteral("glint"), QStringLiteral("#cdf6a2") },
            { QStringLiteral("hoverTint"), QStringLiteral("#ecfcf2") },
            { QStringLiteral("boardBg"), QStringLiteral("#f3fbf6") },
            { QStringLiteral("boardDot"), QStringLiteral("#cfe6d8") }
        }),
        makeGroup(QStringLiteral("vbs"), QStringLiteral("Vivid BAD SQUAD"), {
            { QStringLiteral("border"), QStringLiteral("#561938") },
            { QStringLiteral("A"), QStringLiteral("#ff5c9a") },
            { QStringLiteral("B"), QStringLiteral("#ff96c0") },
            { QStringLiteral("deep"), QStringLiteral("#43142c") },
            { QStringLiteral("rimHi"), QStringLiteral("#d777a8") },
            { QStringLiteral("bezel"), QStringLiteral("#6d2248") },
            { QStringLiteral("wallLight"), QStringLiteral("#e4a2c4") },
            { QStringLiteral("wallMid"), QStringLiteral("#c53c82") },
            { QStringLiteral("wallDark"), QStringLiteral("#7d2954") },
            { QStringLiteral("hardware"), QStringLiteral("#320e20") },
            { QStringLiteral("divider"), QStringLiteral("#f5dbe8") },
            { QStringLiteral("lens"), QStringLiteral("#a23764") },
            { QStringLiteral("glint"), QStringLiteral("#ffa5c7") },
            { QStringLiteral("hoverTint"), QStringLiteral("#ffeff6") },
            { QStringLiteral("boardBg"), QStringLiteral("#fbf3f6") },
            { QStringLiteral("boardDot"), QStringLiteral("#e6cfd8") }
        }),
        makeGroup(QStringLiteral("wxs"), QStringLiteral("Wonderlands x Showtime"), {
            { QStringLiteral("border"), QStringLiteral("#6a3c11") },
            { QStringLiteral("A"), QStringLiteral("#ffa51f") },
            { QStringLiteral("B"), QStringLiteral("#ffd54a") },
            { QStringLiteral("deep"), QStringLiteral("#4b2a0c") },
            { QStringLiteral("rimHi"), QStringLiteral("#e7a567") },
            { QStringLiteral("bezel"), QStringLiteral("#784617") },
            { QStringLiteral("wallLight"), QStringLiteral("#eec298") },
            { QStringLiteral("wallMid"), QStringLiteral("#db7d26") },
            { QStringLiteral("wallDark"), QStringLiteral("#8b511b") },
            { QStringLiteral("hardware"), QStringLiteral("#371f09") },
            { QStringLiteral("divider"), QStringLiteral("#f9e7d7") },
            { QStringLiteral("lens"), QStringLiteral("#ad6b17") },
            { QStringLiteral("glint"), QStringLiteral("#ffce84") },
            { QStringLiteral("hoverTint"), QStringLiteral("#fff9e4") },
            { QStringLiteral("boardBg"), QStringLiteral("#fbf9f3") },
            { QStringLiteral("boardDot"), QStringLiteral("#e6e1cf") }
        }),
        makeGroup(QStringLiteral("nightcord"), QStringLiteral("Nightcord at 25:00"), {
            { QStringLiteral("border"), QStringLiteral("#43285f") },
            { QStringLiteral("A"), QStringLiteral("#c08cf2") },
            { QStringLiteral("B"), QStringLiteral("#e2b5ff") },
            { QStringLiteral("deep"), QStringLiteral("#2b1a3d") },
            { QStringLiteral("rimHi"), QStringLiteral("#a683cb") },
            { QStringLiteral("bezel"), QStringLiteral("#472c63") },
            { QStringLiteral("wallLight"), QStringLiteral("#c3abdb") },
            { QStringLiteral("wallMid"), QStringLiteral("#804eb3") },
            { QStringLiteral("wallDark"), QStringLiteral("#523472") },
            { QStringLiteral("hardware"), QStringLiteral("#20132d") },
            { QStringLiteral("divider"), QStringLiteral("#e8dff1") },
            { QStringLiteral("lens"), QStringLiteral("#7b55a1") },
            { QStringLiteral("glint"), QStringLiteral("#dcc0f8") },
            { QStringLiteral("hoverTint"), QStringLiteral("#fbf4ff") },
            { QStringLiteral("boardBg"), QStringLiteral("#f8f3fb") },
            { QStringLiteral("boardDot"), QStringLiteral("#ddcfe6") }
        })
    };

    m_allGroups = m_baseGroups;
    m_allGroups.push_back(makeGroup(QStringLiteral("mono"), QStringLiteral("Monochrome"), {
        { QStringLiteral("mono"), true },
        { QStringLiteral("border"), QStringLiteral("#444444") },
        { QStringLiteral("A"), QStringLiteral("#f4f4f4") },
        { QStringLiteral("B"), QStringLiteral("#bdbdbd") },
        { QStringLiteral("deep"), QStringLiteral("#1a1a1a") },
        { QStringLiteral("rimHi"), QStringLiteral("#9a9a9a") },
        { QStringLiteral("bezel"), QStringLiteral("#333333") },
        { QStringLiteral("wallLight"), QStringLiteral("#bcbcbc") },
        { QStringLiteral("wallMid"), QStringLiteral("#757575") },
        { QStringLiteral("wallDark"), QStringLiteral("#4a4a4a") },
        { QStringLiteral("hardware"), QStringLiteral("#121212") },
        { QStringLiteral("divider"), QStringLiteral("#ececec") },
        { QStringLiteral("lens"), QStringLiteral("#6a6a6a") },
        { QStringLiteral("glint"), QStringLiteral("#d0d0d0") },
        { QStringLiteral("hoverTint"), QStringLiteral("#e9e9e9") },
        { QStringLiteral("boardBg"), QStringLiteral("#f4f4f4") },
        { QStringLiteral("boardDot"), QStringLiteral("#d4d4d4") },
        { QStringLiteral("lockMint"), QStringLiteral("#e6e6e6") },
        { QStringLiteral("lockPink"), QStringLiteral("#9e9e9e") }
    }));
    m_allGroups.push_back(makeGroup(
        QStringLiteral("matugen"),
        QStringLiteral("Wallpaper"),
        m_wallpaperPalette.isEmpty() ? defaultMikuWallpaperPalette() : m_wallpaperPalette
    ));
}

QVariantMap ThemeService::computeWallpaperPalette(const QString& wallpaperPath) const {
    if (wallpaperPath.isEmpty() || !QFileInfo::exists(wallpaperPath)) {
        return defaultMikuWallpaperPalette();
    }
    if (QFileInfo(wallpaperPath).fileName().compare(QStringLiteral("default-wallpaper.png"), Qt::CaseInsensitive) == 0) {
        return defaultMikuWallpaperPalette();
    }
    QImage img(wallpaperPath);
    if (img.isNull()) {
        return defaultMikuWallpaperPalette();
    }
    const QImage small = img.scaled(32, 32, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                            .convertToFormat(QImage::Format_RGB32);
    double sumSin = 0.0;
    double sumCos = 0.0;
    double weightSum = 0.0;
    for (int y = 0; y < small.height(); ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(small.constScanLine(y));
        for (int x = 0; x < small.width(); ++x) {
            const QColor c(row[x]);
            float h = 0.0f, s = 0.0f, l = 0.0f;
            c.getHslF(&h, &s, &l);
            if (h < 0.0f || s < 0.08f || l < 0.08f || l > 0.94f) {
                continue;
            }
            const double w = static_cast<double>(s) * (1.0 - std::abs(static_cast<double>(l) - 0.52));
            const double rad = static_cast<double>(h) * 2.0 * 3.14159265358979323846;
            sumCos += std::cos(rad) * w;
            sumSin += std::sin(rad) * w;
            weightSum += w;
        }
    }
    if (weightSum < 1e-4) {
        return defaultMikuWallpaperPalette();
    }
    double hue = std::atan2(sumSin, sumCos) / (2.0 * 3.14159265358979323846);
    if (hue < 0.0) {
        hue += 1.0;
    }
    const double hue2 = std::fmod(hue + 0.06, 1.0);
    const QColor border = QColor::fromHslF(static_cast<float>(hue), 0.52f, 0.22f);
    const QColor accA   = QColor::fromHslF(static_cast<float>(hue), 0.76f, 0.73f);
    const QColor accB   = QColor::fromHslF(static_cast<float>(hue2), 0.72f, 0.82f);
    const QColor deep   = QColor::fromHslF(static_cast<float>(hue), 0.50f, 0.12f);
    const QColor white(255, 255, 255);

    return {
        { QStringLiteral("border"), hexOf(border) },
        { QStringLiteral("A"), hexOf(accA) },
        { QStringLiteral("B"), hexOf(accB) },
        { QStringLiteral("deep"), hexOf(deep) },
        { QStringLiteral("rimHi"), hexOf(QColor::fromHslF(static_cast<float>(hue), 0.34f, 0.58f)) },
        { QStringLiteral("bezel"), hexOf(QColor::fromHslF(static_cast<float>(hue), 0.52f, 0.17f)) },
        { QStringLiteral("wallLight"), hexOf(QColor::fromHslF(static_cast<float>(hue), 0.38f, 0.70f)) },
        { QStringLiteral("wallMid"), hexOf(QColor::fromHslF(static_cast<float>(hue), 0.48f, 0.34f)) },
        { QStringLiteral("wallDark"), hexOf(QColor::fromHslF(static_cast<float>(hue), 0.50f, 0.20f)) },
        { QStringLiteral("hardware"), hexOf(QColor::fromHslF(static_cast<float>(hue), 0.52f, 0.09f)) },
        { QStringLiteral("divider"), hexOf(mixColor(border, white, 0.90)) },
        { QStringLiteral("lens"), hexOf(QColor::fromHslF(static_cast<float>(hue), 0.56f, 0.36f)) },
        { QStringLiteral("glint"), hexOf(QColor::fromHslF(static_cast<float>(hue), 0.76f, 0.80f)) },
        { QStringLiteral("hoverTint"), hexOf(mixColor(accA, white, 0.88)) },
        { QStringLiteral("boardBg"), hexOf(mixColor(accA, white, 0.94)) },
        { QStringLiteral("boardDot"), hexOf(mixColor(mixColor(accA, border, 0.35), white, 0.78)) },
        { QStringLiteral("lockMint"), hexOf(accB) },
        { QStringLiteral("lockPink"), QStringLiteral("#ff75ac") }
    };
}

void ThemeService::updateFromWallpaper(const QString& wallpaperPath) {
    m_wallpaperPalette = computeWallpaperPalette(wallpaperPath);
    initGroups();
    if (m_group == QStringLiteral("matugen")) {
        updateActivePalette();
    }
    Q_EMIT changed();
}

void ThemeService::updateActivePalette() {
    for (const QVariant& item : m_allGroups) {
        const QVariantMap m = item.toMap();
        if (m.value(QStringLiteral("id")).toString() == m_group) {
            m_palette = m.value(QStringLiteral("c")).toMap();
            return;
        }
    }
    m_group = QStringLiteral("vs");
    m_palette = m_baseGroups.first().toMap().value(QStringLiteral("c")).toMap();
}

void ThemeService::setTheme(const QString& id) {
    bool found = false;
    for (const QVariant& item : m_allGroups) {
        if (item.toMap().value(QStringLiteral("id")).toString() == id) {
            found = true;
            break;
        }
    }
    if (!found || m_group == id) {
        return;
    }
    m_group = id;
    updateActivePalette();
    if (m_settings) {
        m_settings->set(QStringLiteral("theme.group"), m_group);
    }
    Q_EMIT changed();
}

void ThemeService::setMode(const QString& mode) {
    const QString m = mode.toLower();
    if (m != QStringLiteral("light") && m != QStringLiteral("dark") && m != QStringLiteral("darker")) {
        return;
    }
    if (m_mode == m) {
        return;
    }
    m_mode = m;
    if (m_settings) {
        m_settings->set(QStringLiteral("theme.mode"), m_mode);
    }
    Q_EMIT changed();
}

void ThemeService::toggleDark() {
    setMode(dark() ? QStringLiteral("light") : QStringLiteral("dark"));
}

void ThemeService::cycleTheme() {
    int idx = 0;
    for (int i = 0; i < m_allGroups.size(); ++i) {
        if (m_allGroups.at(i).toMap().value(QStringLiteral("id")).toString() == m_group) {
            idx = i;
            break;
        }
    }
    const int next = (idx + 1) % m_allGroups.size();
    setTheme(m_allGroups.at(next).toMap().value(QStringLiteral("id")).toString());
}

void ThemeService::cycleMode() {
    if (m_mode == QStringLiteral("light")) setMode(QStringLiteral("dark"));
    else if (m_mode == QStringLiteral("dark")) setMode(QStringLiteral("darker"));
    else setMode(QStringLiteral("light"));
}

bool ThemeService::hasScreenTheme(const QString&) const {
    return false;
}

QVariantMap ThemeService::paletteFor(const QString&) const {
    return m_palette;
}

} // namespace SekaiPop::Services
