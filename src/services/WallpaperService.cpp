#include "WallpaperService.h"
#include "SettingsService.h"

#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QRandomGenerator>
#include <QSet>
#include <QStandardPaths>
#include <algorithm>

namespace SekaiPop::Services {

namespace {

bool isInternalUiTexture(const QString& fileName) {
    const QString low = fileName.toLower();
    return low == QStringLiteral("lcd-mesh.png")
        || low == QStringLiteral("lock-dots.png")
        || low == QStringLiteral("phone-shadow.png");
}

} // namespace

WallpaperService::WallpaperService(SettingsService* settings, const QString& appBaseDir, QObject* parent)
    : QObject(parent)
    , m_settings(settings) {
    m_bundledDefault = QDir(appBaseDir).filePath(QStringLiteral("assets/images/default-wallpaper.png"));

    if (m_settings) {
        const QString saved = m_settings->get(QStringLiteral("wallpaper.defaultPath")).toString();
        if (!saved.isEmpty() && QFileInfo::exists(saved) && !isInternalUiTexture(QFileInfo(saved).fileName())) {
            m_defaultPath = saved;
        }
        const QString style = m_settings->get(QStringLiteral("wallpaper.style")).toString();
        if (availableStyles().contains(style)) {
            m_transitionStyle = style;
        }
    }

    if (m_defaultPath.isEmpty() && QFileInfo::exists(m_bundledDefault)) {
        m_defaultPath = m_bundledDefault;
    }

    refreshEntries();
    m_ready = true;
}

bool WallpaperService::perMonitor() const {
    return m_settings && m_settings->flag(QStringLiteral("wallpaper.perMonitor"));
}

QStringList WallpaperService::availableStyles() const {
    return {
        QStringLiteral("sekaiRing"),
        QStringLiteral("sekaiIris"),
        QStringLiteral("star"),
        QStringLiteral("diagonalBlocks"),
        QStringLiteral("sekaiFlash"),
        QStringLiteral("fade"),
        QStringLiteral("random")
    };
}

QString WallpaperService::pathFor(const QString& screenName) const {
    if (!m_ready) {
        return {};
    }
    if (perMonitor() && m_screenPaths.contains(screenName)) {
        const QString p = m_screenPaths.value(screenName).toString();
        if (!p.isEmpty() && QFileInfo::exists(p)) {
            return p;
        }
    }
    return m_defaultPath;
}

QString WallpaperService::imageFor(const QString& path) const {
    return path;
}

bool WallpaperService::videoFor(const QString&) const {
    return false;
}

QString WallpaperService::styleForSwitch() const {
    if (m_transitionStyle != QStringLiteral("random")) {
        return m_transitionStyle;
    }
    static const QStringList pool = {
        QStringLiteral("sekaiRing"),
        QStringLiteral("sekaiIris"),
        QStringLiteral("star"),
        QStringLiteral("diagonalBlocks"),
        QStringLiteral("sekaiFlash")
    };
    const int idx = (QRandomGenerator::global()->bounded(pool.size()) + m_randomCounter++) % pool.size();
    return pool.at(idx);
}

void WallpaperService::setWallpaper(const QString& path, const QString& screenName) {
    const QString clean = QDir::fromNativeSeparators(path.trimmed());
    if (clean.isEmpty() || !QFileInfo::exists(clean)) {
        return;
    }
    if (perMonitor() && !screenName.isEmpty()) {
        m_screenPaths.insert(screenName, clean);
    } else {
        m_defaultPath = clean;
        if (m_settings) {
            m_settings->set(QStringLiteral("wallpaper.defaultPath"), clean);
        }
    }
    refreshEntries();
    Q_EMIT changed();
}

void WallpaperService::refreshEntries() {
    QVariantList list;
    QSet<QString> seen;

    auto addFile = [&](const QString& filePath) {
        const QFileInfo fi(filePath);
        if (!fi.exists() || !fi.isFile() || isInternalUiTexture(fi.fileName())) {
            return;
        }
        const QString abs = QDir::fromNativeSeparators(fi.absoluteFilePath());
        const QString key = abs.toLower();
        if (seen.contains(key)) {
            return;
        }
        seen.insert(key);

        QImageReader reader(abs);
        const QSize sz = reader.size();
        const QString res = (sz.isValid() && sz.width() > 0 && sz.height() > 0)
            ? QStringLiteral("%1x%2").arg(sz.width()).arg(sz.height())
            : QStringLiteral("HD");

        QString title = fi.completeBaseName();
        title.replace(QLatin1Char('_'), QLatin1Char(' '));
        title.replace(QLatin1Char('-'), QLatin1Char(' '));

        list.push_back(QVariantMap{
            { QStringLiteral("fileName"), fi.fileName() },
            { QStringLiteral("filePath"), abs },
            { QStringLiteral("title"), title },
            { QStringLiteral("resolution"), res },
            { QStringLiteral("folder"), fi.dir().dirName() }
        });
    };

    if (!m_bundledDefault.isEmpty()) {
        addFile(m_bundledDefault);
    }
    if (!m_defaultPath.isEmpty()) {
        addFile(m_defaultPath);
    }

    auto scanFolder = [&](const QString& dirPath, bool recursive, int maxCount) {
        if (dirPath.isEmpty() || !QDir(dirPath).exists()) {
            return;
        }
        const QStringList filters = {
            QStringLiteral("*.png"),
            QStringLiteral("*.jpg"),
            QStringLiteral("*.jpeg"),
            QStringLiteral("*.webp"),
            QStringLiteral("*.bmp")
        };
        QDirIterator it(
            dirPath,
            filters,
            QDir::Files | QDir::Readable,
            recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags
        );
        int added = 0;
        while (it.hasNext() && added < maxCount) {
            addFile(it.next());
            ++added;
        }
    };

    if (!m_defaultPath.isEmpty()) {
        scanFolder(QFileInfo(m_defaultPath).absolutePath(), false, 40);
    }
    const QString picDir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (!picDir.isEmpty()) {
        scanFolder(picDir, false, 40);
        scanFolder(QDir(picDir).filePath(QStringLiteral("Wallpapers")), true, 40);
        scanFolder(QDir(picDir).filePath(QStringLiteral("Saved Pictures")), false, 40);
    }
    scanFolder(QStringLiteral("C:/Windows/Web/Wallpaper"), true, 24);
    scanFolder(QStringLiteral("C:/Windows/Web/4K/Wallpaper"), true, 12);

    m_entries = list;
}

void WallpaperService::setTransitionStyle(const QString& style) {
    if (!availableStyles().contains(style) || m_transitionStyle == style) {
        return;
    }
    m_transitionStyle = style;
    if (m_settings) {
        m_settings->set(QStringLiteral("wallpaper.style"), style);
    }
    Q_EMIT changed();
}

void WallpaperService::pickWallpaperDialog(const QString& screenName) {
    QString startDir = QFileInfo(pathFor(screenName)).absolutePath();
    if (startDir.isEmpty() || !QDir(startDir).exists()) {
        startDir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    }
    const QString picked = QFileDialog::getOpenFileName(
        nullptr,
        QStringLiteral("Select Desktop Wallpaper"),
        startDir,
        QStringLiteral("Images (*.png *.jpg *.jpeg *.webp *.bmp)")
    );
    if (!picked.isEmpty()) {
        setWallpaper(picked, screenName);
    }
}

void WallpaperService::nextInFolder(const QString& screenName) {
    const QString cur = pathFor(screenName);
    QFileInfo fi(cur);
    QDir dir = fi.dir();
    if (!dir.exists()) {
        return;
    }
    QStringList files = dir.entryList(
        { QStringLiteral("*.png"), QStringLiteral("*.jpg"), QStringLiteral("*.jpeg"), QStringLiteral("*.webp"), QStringLiteral("*.bmp") },
        QDir::Files | QDir::Readable,
        QDir::Name
    );
    files.erase(std::remove_if(files.begin(), files.end(), [](const QString& name) {
        return isInternalUiTexture(name);
    }), files.end());
    if (files.size() <= 1) {
        const QString picDir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
        if (!picDir.isEmpty() && QDir(picDir).exists()) {
            const QStringList picFiles = QDir(picDir).entryList(
                { QStringLiteral("*.png"), QStringLiteral("*.jpg"), QStringLiteral("*.jpeg"), QStringLiteral("*.webp"), QStringLiteral("*.bmp") },
                QDir::Files | QDir::Readable,
                QDir::Name
            );
            if (!picFiles.isEmpty()) {
                setWallpaper(QDir(picDir).absoluteFilePath(picFiles.first()), screenName);
            }
        }
        return;
    }
    const int idx = files.indexOf(fi.fileName());
    const int next = (idx + 1) % files.size();
    setWallpaper(dir.absoluteFilePath(files.at(next)), screenName);
}

} // namespace SekaiPop::Services
