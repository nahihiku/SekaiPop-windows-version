#pragma once

#include <QList>
#include <QString>

namespace SekaiPop::Quickshell {
class QsPanelWindow;
}

namespace SekaiPop::Win32 {

// Captures a SekaiPop-only (Wallpaper + Widgets + Dock + Bar, never user app windows)
// per-monitor uncompressed 32-bit BMP snapshot to %LOCALAPPDATA%\SekaiPop\cache\boot_curtain\
// so SekaiPopBoot.exe (pure zero-Qt Win32) can paint the exact desktop UI in < 10ms at Windows logon.
class BootCurtainCache {
public:
    static QString cacheDir();
    static bool captureFromPanels(const QList<Quickshell::QsPanelWindow*>& panels, bool isEditModeActive);
};

} // namespace SekaiPop::Win32
