#pragma once

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>
#include <QString>

namespace SekaiPop::Win32 {

/**
 * QQuickImageProvider registered under "winicon" to extract native Windows
 * application icons for .lnk shortcuts, .exe binaries, AppsFolder AUMIDs,
 * and active top-level HWNDs without relying on Qt5-only QtWinExtras.
 *
 * Supported URLs:
 *   image://winicon/app/<utf8-base64url-target>
 *   image://winicon/hwnd/<decimal-hwnd>
 */
class WinIconProvider final : public QQuickImageProvider {
public:
    WinIconProvider();

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

    static QString appIconUrl(const QString& targetOrPath);
    static QString hwndIconUrl(quintptr hwnd);
    static QString fallbackIconUrl(const QString& label);

private:
    QImage extractForAppTarget(const QString& target, int targetSize);
    QImage extractForHwnd(quintptr hwndVal, int targetSize);
    static QImage makeFallbackIcon(const QString& label, int targetSize);

    QMutex m_mutex;
    QHash<QString, QImage> m_cache;
};

} // namespace SekaiPop::Win32
