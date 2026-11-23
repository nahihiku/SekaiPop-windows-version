# Windows Architecture

SekaiPop for Windows is a native C++20 application built on Qt 6 Quick/QML and Win32/WinRT system APIs. Unlike the upstream Linux shell (which depends on Quickshell, Hyprland, Wayland, and shell scripts), the Windows edition implements a complete native host runtime directly inside `SekaiPopShell.exe`.

---

## Executable Components

| Binary | Role |
|---|---|
| `SekaiPopBoot.exe` | Lightweight Win32 Winlogon shell watchdog. Launches `SekaiPopShell.exe --native-shell`, waits for UI-ready signaling before starting user Windows startup apps, restarts the shell on crash, and automatically falls back to `explorer.exe` in Safe Mode or repeated crash loops. |
| `SekaiPopShell.exe` | Primary Qt 6 Quick + Win32 desktop shell hosting the wallpaper layer, status bar, smart dock, 2.5D phone Quick Panel, Settings, App Launcher, Edit Desktop, desktop widgets, screenshot overlay, and all native Windows services. |
| `SekaiPopManager.exe` | Native Win32 GUI utility to switch between Full Native Shell, Seamless Overlay, and Standard Windows Explorer, or uninstall SekaiPop cleanly. |
| `SekaiPop-1.0-Windows-Setup.exe` | Self-extracting Win32 setup executable (`SekaiPopSetupStub.exe` + embedded LZX-21 CAB payload containing all binaries, Qt 6 DLLs, shaders, and QML assets). |

---

## Core Win32 Subsystems (`src/win32/`)

- **`WallpaperHost` (`WallpaperHost.cpp`)**: Pins per-monitor wallpaper windows permanently to the desktop bottom layer (`HWND_BOTTOM`, `WM_WINDOWPOSCHANGING`, `WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE`) and marks them appropriately so they stay behind application windows whether `explorer.exe` is present or absent.
- **`AppBarManager` (`AppBarManager.cpp`)**: Reserves monitor workarea for the SekaiPop Bar (`top`, `right`, `bottom`, `left`). Uses `SHAppBarMessage` when `explorer.exe` is running, and seamlessly switches to `SystemParametersInfoW(SPI_SETWORKAREA)` in Full Native Shell mode while automatically hiding bars/docks on monitors running true fullscreen games or videos.
- **`NativeTaskmanHost` (`Win32Utils.cpp`)**: When `explorer.exe` (`Shell_TrayWnd`) is absent, registers a hidden top-level Taskman/ShellHook window (`SetTaskmanWindow` + `RegisterShellHookWindow`) and sets `ARW_HIDE` in `MINIMIZEDMETRICS`. This prevents Windows `win32k` from parking minimized application windows as floating legacy titlebar pills at the bottom-left corner of the screen.
- **`WindowTracker` (`WindowTracker.cpp`)**: Tracks visible, minimized, maximized, and true-fullscreen windows across all monitors using `SetWinEventHook` + `EnumWindows` + `DwmGetWindowAttribute(DWMWA_CLOAKED)` + `IVirtualDesktopManager`. Resolves UWP `ApplicationFrameWindow` child PIDs and manages virtual workspace switching.
- **`HotkeyManager` (`HotkeyManager.cpp`)**: Combines a low-level keyboard hook (`WH_KEYBOARD_LL`) and `RegisterHotKey` to capture solo `Win` key taps (suppressing the Windows Start Menu cleanly), global launcher/emergency shortcuts, and native `Win+` chords (`Win+E`, `Win+R`, `Win+I`, `Win+D`, `Win+Shift+S`), `PrtSc` screenshot modes, and media/volume keys when `explorer.exe` is not running.
- **`ScreenshotOverlay` (`ScreenshotOverlay.cpp`)**: Multi-monitor DPI-aware desktop capture (`BitBlt` over virtual screen bounds) with an interactive region selection overlay, instant monitor/window/all-screens capture, clipboard bitmap export, and PNG saving to `Pictures\Screenshots`.
- **`Win32Utils` (`Win32Utils.cpp`)**: Handles reliable cross-process window focusing (`focusWindowForce`) and universal application launching (`launchShellTarget`):
  - **AppsFolder Win32 / CLSID items** (including `Microsoft.Windows.Explorer`, browsers, and desktop apps): resolves `PKEY_Link_TargetParsingPath` via `IShellItem2` and opens File Explorer via `/separate` without launching the Windows taskbar.
  - **Packaged MSIX / UWP apps** (such as WhatsApp Desktop, Windows Terminal, Calculator, and Microsoft Store): activates via `IApplicationActivationManager::ActivateApplication` (working cleanly without `explorer.exe`), transfers foreground focus permission ahead of activation, brings launched/restored windows to the front, unhides tray-minimized WinUI 3 windows, and automatically recovers stuck headless single-instance processes.
- **`WinIconProvider` (`WinIconProvider.cpp`)**: Custom `QQuickImageProvider("winicon")` extracting high-DPI icons from `.exe`, `.lnk`, live `HWND`s, and `shell:AppsFolder` items.

---

## Native Windows Backend Services (`src/services/`)

- **`AppSource`**: Enumerates Start Menu shortcuts (`FOLDERID_Programs`, `FOLDERID_CommonPrograms`) and Store/MSIX packages (`FOLDERID_AppsFolder`) on a background thread with usage-ranked search.
- **`MediaSource`**: Connects to Windows SMTC (`GlobalSystemMediaTransportControlsSessionManager`) via WinRT COM ABI for live track metadata, cover art, timeline seeking, and playback controls.
- **`AudioSpectrumSource`**: Captures system audio via WASAPI loopback for the desktop visualizer and automatically pauses when the desktop is occluded by maximized/fullscreen windows.
- **`QuickSource` & `StatusIconsService`**: Direct Windows CoreAudio speaker/microphone volume and mute, WinRT Wi-Fi and Bluetooth radio state, IP Helper network interface throughput, display brightness/night light, and power actions.
- **`ResourceSource` & `BatterySource`**: CPU (`GetSystemTimes`), RAM (`GlobalMemoryStatusEx`), and battery telemetry (`GetSystemPowerStatus`).
- **`VideoWallpaperItem` & `MatugenEngine`**: Native video wallpaper playback and automatic Material You color extraction from active desktop/lockscreen wallpapers.

---

## Startup Speed & Graphics Optimization

- **Shared D3D11 QRhi Graphics Device (`QsWindowing.cpp`)**: Shares a single low-power Direct3D 11 QRhi device across all shell `QQuickWindow` instances under `QSG_RENDER_LOOP=basic`, reducing thread count and GPU/RAM overhead substantially.
- **Fast Boot & Deferred Startup Apps (`SekaiPopBoot.cpp`, `main.cpp`)**: Boosts `SekaiPopShell.exe` to `ABOVE_NORMAL_PRIORITY_CLASS` during boot and signals the named Win32 event `Local\SekaiPopShell_UiReady_v1` as soon as the Wallpaper and Bar are rendered. `SekaiPopBoot.exe` holds enabled Windows startup apps until that event fires, checks Windows Task Manager's `Explorer\StartupApproved` (`Run`, `Run32`, and `StartupFolder` in `HKCU` and `HKLM`) so any disabled startup entry is skipped, and launches remaining enabled apps staggered at `BELOW_NORMAL_PRIORITY_CLASS`.
- **URL-Lazy QML Staging (`QsEngineSetup.cpp`, `QsTypes.cpp`)**: Uses a fast production fingerprint check (`0 ms` cache hit on warm boots) and defers non-critical overlays, Bar popups, Dock initialization, Settings pages, QuickPanel, and Desktop Widgets behind URL-based `QsLazyLoader` components so initial UI appears in roughly 3–4 seconds.

---

## User Data & Logs

All runtime data is stored per user under `%LOCALAPPDATA%\SekaiPop`:

- `%LOCALAPPDATA%\SekaiPop\SekaiPopShell\shell.log` - Main shell diagnostic log
- `%LOCALAPPDATA%\SekaiPop\state\sekaipop\` - Settings, dock layout, presets, and widget layouts
- `%LOCALAPPDATA%\SekaiPop\qs_stage\` - Staged Windows-adapted QML runtime tree
- `%LOCALAPPDATA%\SekaiPop\cache\` - Cover art, wallpaper thumbnails, and network disk cache
