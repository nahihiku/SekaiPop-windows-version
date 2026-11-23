#pragma once

#include <QString>
#include <QStringList>

namespace SekaiPop::Services {

class MatugenEngine {
public:
    // Extract up to 6 distinct dominant seed colors ("#rrggbb") from a wallpaper image,
    // ranked by perceptual prominence + vibrancy (Material Color Utilities scoring).
    static QStringList extractSourceColors(const QString& imagePath);

    // Generate the full matugen-template.json payload (dark + light Material 3 roles)
    // for the given image, scheme type, seed index, and contrast (-1.0 .. 1.0).
    static QString generateSchemeJson(
        const QString& imagePath,
        const QString& schemeType = QStringLiteral("scheme-tonal-spot"),
        int seedIndex = 0,
        double contrast = 0.0,
        QString* outError = nullptr
    );

    // Generate and atomically write <cacheMatugenDir>/<safeName>.json.
    // Returns true on success and optionally populates outJson.
    static bool applyToCache(
        const QString& imagePath,
        const QString& schemeType,
        const QString& outputName,
        int seedIndex,
        double contrast,
        const QString& cacheMatugenDir,
        QString* outJson = nullptr,
        QString* outError = nullptr
    );
};

} // namespace SekaiPop::Services
