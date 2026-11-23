#include "MatugenEngine.h"

#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUrl>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace SekaiPop::Services {

namespace {

constexpr double kPi = 3.14159265358979323846;

inline double clamp01(double v) {
    return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
}

inline double clampRange(double v, double lo, double hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

inline double normHue(double h) {
    double r = std::fmod(h, 360.0);
    if (r < 0.0) r += 360.0;
    return r;
}

inline double hueDiff(double a, double b) {
    const double d = std::fabs(normHue(a) - normHue(b));
    return d > 180.0 ? 360.0 - d : d;
}

inline double srgbToLinear(double c) {
    return c <= 0.04045 ? (c / 12.92) : std::pow((c + 0.055) / 1.055, 2.4);
}

inline double linearToSrgb(double c) {
    if (c <= 0.0) return 0.0;
    if (c >= 1.0) return 1.0;
    return c <= 0.0031308 ? (12.92 * c) : (1.055 * std::pow(c, 1.0 / 2.4) - 0.055);
}

struct Oklch {
    double L = 0.0; // 0..1
    double C = 0.0; // 0..~0.37
    double H = 0.0; // 0..360 deg
};

struct LinRgb {
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
};

Oklch rgbToOklch(int r8, int g8, int b8) {
    const double r = srgbToLinear(r8 / 255.0);
    const double g = srgbToLinear(g8 / 255.0);
    const double b = srgbToLinear(b8 / 255.0);

    const double l = 0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b;
    const double m = 0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b;
    const double s = 0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b;

    const double l_ = std::cbrt(std::max(0.0, l));
    const double m_ = std::cbrt(std::max(0.0, m));
    const double s_ = std::cbrt(std::max(0.0, s));

    const double L = 0.2104542553 * l_ + 0.7936177850 * m_ - 0.0040720468 * s_;
    const double A = 1.9779984951 * l_ - 2.4285922050 * m_ + 0.4505937099 * s_;
    const double B = 0.0259040371 * l_ + 0.7827717662 * m_ - 0.8086757660 * s_;

    const double C = std::sqrt(A * A + B * B);
    const double H = C < 1e-6 ? 0.0 : normHue(std::atan2(B, A) * 180.0 / kPi);
    return {clamp01(L), C, H};
}

LinRgb oklchToLinRgb(double L, double C, double H) {
    const double rad = H * kPi / 180.0;
    const double A = C * std::cos(rad);
    const double B = C * std::sin(rad);

    const double l_ = L + 0.3963377774 * A + 0.2158037573 * B;
    const double m_ = L - 0.1055613458 * A - 0.0638541728 * B;
    const double s_ = L - 0.0894841775 * A - 1.2914855480 * B;

    const double l = l_ * l_ * l_;
    const double m = m_ * m_ * m_;
    const double s = s_ * s_ * s_;

    return {
        +4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
        -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
        -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s
    };
}

inline bool inGamut(const LinRgb& c) {
    constexpr double eps = 1e-4;
    return c.r >= -eps && c.r <= 1.0 + eps &&
           c.g >= -eps && c.g <= 1.0 + eps &&
           c.b >= -eps && c.b <= 1.0 + eps;
}

double maxChromaInGamut(double L, double H, double cap) {
    if (cap <= 0.0 || L <= 0.001 || L >= 0.999) {
        return 0.0;
    }
    if (inGamut(oklchToLinRgb(L, cap, H))) {
        return cap;
    }
    double lo = 0.0;
    double hi = cap;
    for (int i = 0; i < 18; ++i) {
        const double mid = 0.5 * (lo + hi);
        if (inGamut(oklchToLinRgb(L, mid, H))) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return lo;
}

// Convert Material tone T (0..100, CIE L* luminance scale) to perceptual OKLab L (0..1).
double toneToOklabL(double tone) {
    const double t = clampRange(tone, 0.0, 100.0);
    double y = 0.0;
    if (t <= 8.0) {
        y = t / 903.2962962962963;
    } else {
        const double f = (t + 16.0) / 116.0;
        y = f * f * f;
    }
    return clamp01(std::cbrt(std::max(0.0, y)));
}

QString rgbToHex(int r, int g, int b) {
    return QStringLiteral("#%1%2%3")
        .arg(qBound(0, r, 255), 2, 16, QLatin1Char('0'))
        .arg(qBound(0, g, 255), 2, 16, QLatin1Char('0'))
        .arg(qBound(0, b, 255), 2, 16, QLatin1Char('0'));
}

QString toneHex(double hueDeg, double desiredChroma, double tone, bool preserveAccentVibrancy = false) {
    double L = toneToOklabL(tone);
    const double H = normHue(hueDeg);
    double C = std::max(0.0, desiredChroma);

    // For vivid bright accents (dark.primary / dark.tertiary around tone 78-84),
    // deep hues like red/blue clip chroma early at L=0.83. Relax L slightly if needed
    // so vibrant wallpapers yield vivid accents instead of washed-out pastels.
    if (preserveAccentVibrancy && C >= 0.11 && L > 0.72) {
        double maxC = maxChromaInGamut(L, H, C);
        if (maxC < std::min(C, 0.125)) {
            double bestL = L;
            double bestC = maxC;
            for (int step = 1; step <= 6; ++step) {
                const double candL = L - step * 0.014;
                if (candL < 0.70) break;
                const double candC = maxChromaInGamut(candL, H, C);
                if (candC > bestC + 0.008) {
                    bestC = candC;
                    bestL = candL;
                }
                if (bestC >= std::min(C, 0.145)) break;
            }
            L = bestL;
            C = bestC;
        } else {
            C = maxC;
        }
    } else {
        C = maxChromaInGamut(L, H, C);
    }

    const LinRgb lin = oklchToLinRgb(L, C, H);
    const int r = qRound(linearToSrgb(lin.r) * 255.0);
    const int g = qRound(linearToSrgb(lin.g) * 255.0);
    const int b = qRound(linearToSrgb(lin.b) * 255.0);
    return rgbToHex(r, g, b);
}

QString cleanImagePath(const QString& raw) {
    QString s = raw.trimmed();
    if (s.startsWith(QStringLiteral("file:///"), Qt::CaseInsensitive)) {
        s = s.mid(8);
    } else if (s.startsWith(QStringLiteral("file://"), Qt::CaseInsensitive)) {
        s = s.mid(7);
    }
    s = QUrl::fromPercentEncoding(s.toUtf8());
    s.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (s.size() >= 3 && s.at(0) == QLatin1Char('/') && s.at(1).isLetter() && s.at(2) == QLatin1Char(':')) {
        s.remove(0, 1);
    }
    if (s.size() >= 2 && s.at(0).isLetter() && s.at(1) == QLatin1Char('/')) {
        const QString cand = s.left(1).toUpper() + QStringLiteral(":/") + s.mid(2);
        if (QFileInfo::exists(cand)) {
            s = cand;
        }
    }
    return s;
}

QImage loadSmallWallpaper(const QString& path) {
    const QString clean = cleanImagePath(path);
    if (clean.isEmpty() || !QFileInfo::exists(clean)) {
        return {};
    }
    QImageReader reader(clean);
    reader.setAutoTransform(true);
    const QSize orig = reader.size();
    constexpr int kMaxDim = 128;
    if (orig.isValid() && (orig.width() > kMaxDim || orig.height() > kMaxDim)) {
        QSize scaled = orig;
        scaled.scale(kMaxDim, kMaxDim, Qt::KeepAspectRatio);
        if (scaled.width() < 1) scaled.setWidth(1);
        if (scaled.height() < 1) scaled.setHeight(1);
        reader.setScaledSize(scaled);
    }
    QImage img = reader.read();
    if (img.isNull()) {
        return {};
    }
    if (img.width() > kMaxDim || img.height() > kMaxDim) {
        img = img.scaled(kMaxDim, kMaxDim, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return img.convertToFormat(QImage::Format_ARGB32);
}

struct ColorCluster {
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
    uint32_t count = 0;
    Oklch lch;
    double score = 0.0;
};

std::vector<ColorCluster> quantizeAndScore(const QImage& img) {
    // 5-bit per channel histogram (32 x 32 x 32 = 32768 bins) for fast, exact population counting.
    constexpr int kBins = 32 * 32 * 32;
    std::vector<uint32_t> counts(kBins, 0);
    std::vector<uint64_t> sumR(kBins, 0);
    std::vector<uint64_t> sumG(kBins, 0);
    std::vector<uint64_t> sumB(kBins, 0);

    uint64_t totalOpaque = 0;
    for (int y = 0; y < img.height(); ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const QRgb px = row[x];
            if (qAlpha(px) < 128) {
                continue;
            }
            const int r = qRed(px);
            const int g = qGreen(px);
            const int b = qBlue(px);
            const int idx = ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3);
            counts[idx] += 1;
            sumR[idx] += static_cast<uint64_t>(r);
            sumG[idx] += static_cast<uint64_t>(g);
            sumB[idx] += static_cast<uint64_t>(b);
            totalOpaque += 1;
        }
    }

    if (totalOpaque == 0) {
        return {};
    }

    std::vector<ColorCluster> clusters;
    clusters.reserve(256);
    std::array<double, 360> huePop = {};
    double chromaticTotal = 0.0;

    for (int i = 0; i < kBins; ++i) {
        const uint32_t c = counts[i];
        if (c == 0) continue;
        ColorCluster cl;
        cl.r = static_cast<double>(sumR[i]) / c;
        cl.g = static_cast<double>(sumG[i]) / c;
        cl.b = static_cast<double>(sumB[i]) / c;
        cl.count = c;
        cl.lch = rgbToOklch(qRound(cl.r), qRound(cl.g), qRound(cl.b));
        if (cl.lch.C >= 0.032 && cl.lch.L >= 0.10 && cl.lch.L <= 0.94) {
            const int hBin = static_cast<int>(std::floor(cl.lch.H)) % 360;
            huePop[hBin] += static_cast<double>(c);
            chromaticTotal += static_cast<double>(c);
        }
        clusters.push_back(cl);
    }

    // Material Color Utilities excited hue proportions (+/- 15 degree window).
    std::array<double, 360> excited = {};
    if (chromaticTotal > 0.0) {
        for (int h = 0; h < 360; ++h) {
            const double p = huePop[h] / chromaticTotal;
            if (p <= 0.0) continue;
            for (int off = -14; off <= 15; ++off) {
                const int b = (h + off + 360) % 360;
                excited[b] += p;
            }
        }
    }

    // Score each cluster: combine hue proportion (70%) + vibrancy/chroma (30%),
    // slightly preferring mid-tone vivid colors over near-black or near-white pixels.
    std::vector<ColorCluster> chromatic;
    std::vector<ColorCluster> fallback;
    chromatic.reserve(clusters.size());
    fallback.reserve(clusters.size());

    for (auto& cl : clusters) {
        const int hBin = static_cast<int>(std::floor(cl.lch.H)) % 360;
        const double prop = excited[hBin];
        const double propScore = prop * 70.0;

        // Target OKLCH chroma ~0.16 (similar to HCT chroma 48); reward vivid colors cleanly.
        const double chromaWeight = cl.lch.C < 0.14 ? 140.0 : 210.0;
        const double chromaScore = std::min(0.28, cl.lch.C) * chromaWeight;

        // Gentle lightness bell peaking around L = 0.55 so neither black nor snow white dominates.
        const double lightPenalty = std::fabs(cl.lch.L - 0.55) * 14.0;
        const double popBoost = std::log1p(static_cast<double>(cl.count)) * 1.4;

        cl.score = propScore + chromaScore + popBoost - lightPenalty;

        if (cl.lch.C >= 0.032 && cl.lch.L >= 0.12 && cl.lch.L <= 0.93 && prop >= 0.008) {
            chromatic.push_back(cl);
        } else {
            fallback.push_back(cl);
        }
    }

    auto byScore = [](const ColorCluster& a, const ColorCluster& b) {
        return a.score > b.score;
    };
    std::sort(chromatic.begin(), chromatic.end(), byScore);
    std::sort(fallback.begin(), fallback.end(), [](const ColorCluster& a, const ColorCluster& b) {
        return a.count > b.count;
    });

    std::vector<ColorCluster> chosen;
    constexpr size_t kMaxSeeds = 6;

    // Pick distinct hues with progressive relaxation (30 deg -> 20 deg -> 12 deg),
    // matching Google MCU Score::score's distinct-hue selection.
    const double thresholds[] = {30.0, 20.0, 12.0};
    for (double minDeg : thresholds) {
        for (const auto& cand : chromatic) {
            if (chosen.size() >= kMaxSeeds) break;
            bool dup = false;
            for (const auto& ex : chosen) {
                if (hueDiff(cand.lch.H, ex.lch.H) < minDeg) {
                    dup = true;
                    break;
                }
            }
            if (!dup) {
                chosen.push_back(cand);
            }
        }
        if (chosen.size() >= 4) {
            break;
        }
    }

    // If the wallpaper is monochrome / low-saturation, add top neutral clusters.
    if (chosen.empty()) {
        for (const auto& cand : fallback) {
            if (chosen.size() >= 3) break;
            if (cand.lch.L >= 0.15 && cand.lch.L <= 0.85) {
                chosen.push_back(cand);
            }
        }
        if (chosen.empty() && !fallback.empty()) {
            chosen.push_back(fallback.front());
        }
    }

    return chosen;
}

struct PaletteSpec {
    double priH = 0.0, priC = 0.14;
    double secH = 0.0, secC = 0.055;
    double terH = 60.0, terC = 0.11;
    double neuH = 0.0, neuC = 0.014;
    double nvH  = 0.0, nvC  = 0.028;
    bool vividAccents = true;
};

PaletteSpec buildSpec(const Oklch& src, const QString& schemeType) {
    PaletteSpec s;
    const double H = src.H;
    const double C = src.C;
    const QString t = schemeType.trimmed().toLower();

    if (t == QStringLiteral("scheme-monochrome")) {
        s.priH = H; s.priC = 0.0;
        s.secH = H; s.secC = 0.0;
        s.terH = H; s.terC = 0.0;
        s.neuH = H; s.neuC = 0.0;
        s.nvH  = H; s.nvC  = 0.0;
        s.vividAccents = false;
        return s;
    }

    if (t == QStringLiteral("scheme-neutral")) {
        s.priH = H;               s.priC = 0.045;
        s.secH = H;               s.secC = 0.030;
        s.terH = normHue(H + 35); s.terC = 0.055;
        s.neuH = H;               s.neuC = 0.008;
        s.nvH  = H;               s.nvC  = 0.015;
        s.vividAccents = false;
        return s;
    }

    if (t == QStringLiteral("scheme-vibrant")) {
        s.priH = H;               s.priC = clampRange(std::max(0.18, C * 1.25), 0.16, 0.24);
        s.secH = normHue(H + 18); s.secC = 0.095;
        s.terH = normHue(H + 52); s.terC = clampRange(std::max(0.15, C * 1.05), 0.14, 0.21);
        s.neuH = H;               s.neuC = 0.025;
        s.nvH  = H;               s.nvC  = 0.045;
        return s;
    }

    if (t == QStringLiteral("scheme-expressive")) {
        s.priH = normHue(H + 240); s.priC = 0.155;
        s.secH = normHue(H + 45);  s.secC = 0.085;
        s.terH = normHue(H + 120); s.terC = 0.145;
        s.neuH = normHue(H + 15);  s.neuC = 0.024;
        s.nvH  = normHue(H + 15);  s.nvC  = 0.042;
        return s;
    }

    if (t == QStringLiteral("scheme-fidelity")) {
        s.priH = H;                s.priC = clampRange(std::max(0.08, C), 0.08, 0.22);
        s.secH = H;                s.secC = clampRange(C * 0.45, 0.035, 0.10);
        s.terH = normHue(H + 180); s.terC = clampRange(std::max(0.09, C * 0.85), 0.08, 0.18);
        s.neuH = H;                s.neuC = clampRange(C * 0.16, 0.008, 0.026);
        s.nvH  = H;                s.nvC  = clampRange(C * 0.28, 0.015, 0.045);
        return s;
    }

    if (t == QStringLiteral("scheme-content")) {
        s.priH = H;               s.priC = clampRange(std::max(0.08, C), 0.08, 0.21);
        s.secH = H;               s.secC = clampRange(C * 0.48, 0.035, 0.10);
        s.terH = normHue(H + 42); s.terC = clampRange(std::max(0.09, C * 0.90), 0.08, 0.18);
        s.neuH = H;               s.neuC = clampRange(C * 0.16, 0.008, 0.026);
        s.nvH  = H;               s.nvC  = clampRange(C * 0.28, 0.015, 0.045);
        return s;
    }

    if (t == QStringLiteral("scheme-rainbow")) {
        s.priH = H;               s.priC = 0.165;
        s.secH = H;               s.secC = 0.060;
        s.terH = normHue(H + 60); s.terC = 0.135;
        s.neuH = H;               s.neuC = 0.0;
        s.nvH  = H;               s.nvC  = 0.0;
        return s;
    }

    if (t == QStringLiteral("scheme-fruit-salad")) {
        s.priH = normHue(H - 50); s.priC = 0.165;
        s.secH = normHue(H - 50); s.secC = 0.110;
        s.terH = H;               s.terC = 0.155;
        s.neuH = H;               s.neuC = 0.030;
        s.nvH  = H;               s.nvC  = 0.050;
        return s;
    }

    // Default: scheme-tonal-spot
    // Keep neutral/gray wallpapers gracefully understated, while chromatic wallpapers get vivid accents.
    const double basePriC = C < 0.03 ? 0.02 : clampRange(std::max(0.12, C * 0.95), 0.12, 0.185);
    const double baseTerC = C < 0.03 ? 0.02 : clampRange(std::max(0.10, C * 0.82), 0.10, 0.160);
    s.priH = H;               s.priC = basePriC;
    s.secH = H;               s.secC = C < 0.03 ? 0.015 : 0.058;
    s.terH = normHue(H + 55); s.terC = baseTerC;
    s.neuH = H;               s.neuC = C < 0.03 ? 0.006 : 0.016;
    s.nvH  = H;               s.nvC  = C < 0.03 ? 0.010 : 0.030;
    return s;
}

QString sanitizeOutputName(const QString& rawName) {
    QString s = rawName.trimmed();
    if (s.isEmpty()) {
        return QStringLiteral("colors");
    }
    static const QRegularExpression badChars(QStringLiteral("[^A-Za-z0-9._-]"));
    s.replace(badChars, QStringLiteral("_"));
    while (s.contains(QStringLiteral(".."))) {
        s.replace(QStringLiteral(".."), QStringLiteral("_"));
    }
    return s.isEmpty() ? QStringLiteral("colors") : s;
}

} // namespace

QStringList MatugenEngine::extractSourceColors(const QString& imagePath) {
    const QImage img = loadSmallWallpaper(imagePath);
    if (img.isNull()) {
        return {};
    }
    const std::vector<ColorCluster> seeds = quantizeAndScore(img);
    QStringList out;
    out.reserve(static_cast<int>(seeds.size()));
    for (const auto& s : seeds) {
        const QString hex = rgbToHex(qRound(s.r), qRound(s.g), qRound(s.b));
        if (!out.contains(hex)) {
            out.append(hex);
        }
    }
    return out;
}

QString MatugenEngine::generateSchemeJson(
    const QString& imagePath,
    const QString& schemeType,
    int seedIndex,
    double contrast,
    QString* outError
) {
    const QImage img = loadSmallWallpaper(imagePath);
    if (img.isNull()) {
        if (outError) {
            *outError = QStringLiteral("matugen-state: no such or unreadable image: ") + imagePath;
        }
        return {};
    }

    const std::vector<ColorCluster> seeds = quantizeAndScore(img);
    Oklch src{0.55, 0.14, 210.0};
    if (!seeds.empty()) {
        const int idx = (seedIndex >= 0 && seedIndex < static_cast<int>(seeds.size())) ? seedIndex : 0;
        src = seeds[static_cast<size_t>(idx)].lch;
    }

    const PaletteSpec sp = buildSpec(src, schemeType);
    const double c = clampRange(contrast, -1.0, 1.0);

    // Material 3 tone tables adjusted cleanly by contrast (-1 .. +1).
    const double dkPrimary          = clampRange(80.0 + c * 9.0, 70.0, 92.0);
    const double dkOnPrimary        = clampRange(20.0 - c * 7.0, 10.0, 28.0);
    const double dkPrimaryContainer = clampRange(31.0 - c * 9.0, 18.0, 42.0);
    const double dkSecondary        = clampRange(80.0 + c * 7.0, 72.0, 90.0);
    const double dkSecondaryCont    = clampRange(30.0 - c * 8.0, 18.0, 40.0);
    const double dkTertiary         = clampRange(82.0 + c * 8.0, 72.0, 92.0);
    const double dkInversePrimary   = clampRange(40.0 - c * 10.0, 26.0, 50.0);
    const double dkOutline          = clampRange(60.0 + c * 10.0, 48.0, 74.0);
    const double dkOutlineVariant   = clampRange(30.0 + c * 8.0, 22.0, 42.0);

    const double ltPrimary          = clampRange(42.0 - c * 12.0, 26.0, 54.0);
    const double ltPrimaryContainer = clampRange(90.0 + c * 4.0, 84.0, 95.0);
    const double ltSecondaryCont    = clampRange(90.0 - c * 3.0, 84.0, 94.0);
    const double ltSurface          = 98.0;
    const double ltSurfaceVariant   = clampRange(90.0 - c * 5.0, 82.0, 94.0);
    const double ltOutlineVariant   = clampRange(80.0 - c * 10.0, 66.0, 88.0);

    QJsonObject dark;
    dark.insert(QStringLiteral("primary"),                  toneHex(sp.priH, sp.priC, dkPrimary, sp.vividAccents));
    dark.insert(QStringLiteral("on_primary"),               toneHex(sp.priH, sp.priC * 0.78, dkOnPrimary));
    dark.insert(QStringLiteral("primary_container"),        toneHex(sp.priH, sp.priC * 0.85, dkPrimaryContainer));
    dark.insert(QStringLiteral("secondary"),                toneHex(sp.secH, sp.secC, dkSecondary));
    dark.insert(QStringLiteral("secondary_container"),      toneHex(sp.secH, sp.secC * 0.90, dkSecondaryCont));
    dark.insert(QStringLiteral("tertiary"),                 toneHex(sp.terH, sp.terC, dkTertiary, sp.vividAccents));
    dark.insert(QStringLiteral("inverse_primary"),          toneHex(sp.priH, sp.priC * 0.92, dkInversePrimary));
    dark.insert(QStringLiteral("outline"),                  toneHex(sp.nvH,  sp.nvC,  dkOutline));
    dark.insert(QStringLiteral("surface"),                  toneHex(sp.neuH, sp.neuC, 6.0));
    dark.insert(QStringLiteral("surface_container_lowest"), toneHex(sp.neuH, sp.neuC, 4.0));
    dark.insert(QStringLiteral("surface_container"),        toneHex(sp.neuH, sp.neuC, 12.0));
    dark.insert(QStringLiteral("surface_container_high"),   toneHex(sp.neuH, sp.neuC, 17.0));
    dark.insert(QStringLiteral("outline_variant"),          toneHex(sp.nvH,  sp.nvC,  dkOutlineVariant));

    QJsonObject light;
    light.insert(QStringLiteral("primary"),                  toneHex(sp.priH, sp.priC * 0.92, ltPrimary));
    light.insert(QStringLiteral("primary_container"),        toneHex(sp.priH, sp.priC * 0.72, ltPrimaryContainer));
    light.insert(QStringLiteral("secondary_container"),      toneHex(sp.secH, sp.secC * 0.85, ltSecondaryCont));
    light.insert(QStringLiteral("surface"),                  toneHex(sp.neuH, sp.neuC, ltSurface));
    light.insert(QStringLiteral("surface_variant"),          toneHex(sp.nvH,  sp.nvC,  ltSurfaceVariant));
    light.insert(QStringLiteral("surface_container_lowest"), toneHex(sp.neuH, sp.neuC, 100.0));
    light.insert(QStringLiteral("surface_container"),        toneHex(sp.neuH, sp.neuC, 94.0));
    light.insert(QStringLiteral("surface_container_high"),   toneHex(sp.neuH, sp.neuC, 92.0));
    light.insert(QStringLiteral("outline_variant"),          toneHex(sp.nvH,  sp.nvC,  ltOutlineVariant));

    QJsonObject root;
    root.insert(QStringLiteral("dark"), dark);
    root.insert(QStringLiteral("light"), light);

    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

bool MatugenEngine::applyToCache(
    const QString& imagePath,
    const QString& schemeType,
    const QString& outputName,
    int seedIndex,
    double contrast,
    const QString& cacheMatugenDir,
    QString* outJson,
    QString* outError
) {
    const QString json = generateSchemeJson(imagePath, schemeType, seedIndex, contrast, outError);
    if (json.isEmpty()) {
        return false;
    }

    if (!cacheMatugenDir.isEmpty()) {
        QDir().mkpath(cacheMatugenDir);
        const QString filePath = cacheMatugenDir + QLatin1Char('/') + sanitizeOutputName(outputName) + QStringLiteral(".json");
        QSaveFile f(filePath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            if (outError) {
                *outError = QStringLiteral("matugen-state: failed to open output: ") + filePath;
            }
            return false;
        }
        f.write(json.toUtf8());
        if (!f.commit()) {
            if (outError) {
                *outError = QStringLiteral("matugen-state: failed to commit output: ") + filePath;
            }
            return false;
        }
    }

    if (outJson) {
        *outJson = json;
    }
    return true;
}

} // namespace SekaiPop::Services
