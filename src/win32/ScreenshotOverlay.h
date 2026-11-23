#pragma once

namespace SekaiPop::Win32 {

/**
 * Native multi-monitor physical-pixel screenshot tool for SekaiPop Desktop Shell.
 * Works 100% independently of explorer.exe / UWP ScreenClippingHost (crucial in
 * Winlogon Native Shell mode) and handles mixed-DPI multi-monitor setups at 1:1 sharpness.
 */
class ScreenshotOverlay {
public:
    /** Opens the interactive region snip overlay across all monitors. */
    static void startRegionSnip();

    /** Instantly captures the monitor containing the mouse cursor. */
    static void captureActiveMonitor();

    /** Instantly captures the currently focused foreground window. */
    static void captureActiveWindow();

    /** Instantly captures the entire virtual desktop (all monitors combined). */
    static void captureAllMonitors();
};

} // namespace SekaiPop::Win32
