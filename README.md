<div align="center">

<h1>SekaiPop for Windows</h1>

<p><strong>Native C++20 / Qt 6 + Win32 Desktop Shell for Windows 10 &amp; Windows 11</strong></p>

<p>
<a href="#features">Features</a> &nbsp;|&nbsp;
<a href="#installation">Installation</a> &nbsp;|&nbsp;
<a href="#keybinds">Keybinds</a> &nbsp;|&nbsp;
<a href="#documentation">Documentation</a> &nbsp;|&nbsp;
<a href="#building-from-source">Build from Source</a> &nbsp;|&nbsp;
<a href="#emergency-recovery">Emergency Recovery</a>
</p>

<img src="assets/shot-desktop-widgets.png" alt="SekaiPop desktop with customizable widgets, bar, and dock" width="100%">

<sub>Native Windows port of the SekaiPop desktop environment: customizable widgets, status bar, smart dock, 2.5D phone control center, live wallpapers, and full Winlogon shell replacement.</sub>

</div>

---

## Overview

**SekaiPop for Windows** is a native Windows desktop shell written in C++20, Qt 6 Quick/QML, and direct Win32/WinRT APIs. It brings the complete SekaiPop design language and workflow to Windows 10 and Windows 11 without requiring WSL, Linux compositors, or terminal commands to install.

SekaiPop can run in two modes managed through a graphical installer and manager app:

1. **Full Native Shell Mode (Recommended)**: Registers `SekaiPopBoot.exe` as your Windows Winlogon shell (`explorer.exe` does not load its taskbar or desktop at login). Includes a crash-recovery watchdog, deferred startup app scheduling, native `Win+` shortcuts, multi-monitor screenshots, and automatic Safe Mode fallback to Windows Explorer.
2. **Seamless Overlay Mode**: Keeps `explorer.exe` running in the background while hiding the Windows taskbar and auto-starting `SekaiPopShell.exe` on login.

---

## Showcase

<table>
<tr>
<td width="50%" valign="top">
<img src="assets/shot-quick-panel-phone.png" alt="Quick Panel phone control center" width="100%">
<br><b>2.5D Phone Control Center &amp; App Launcher</b><br>
Wi-Fi, Bluetooth, speaker/microphone audio controls, brightness, night light, media playback, notifications, and full Windows Start Menu + Store/MSIX app search inside a unified phone UI.
</td>
<td width="50%" valign="top">
<img src="assets/shot-settings-phone.png" alt="Landscape phone Settings UI" width="100%">
<br><b>Landscape Settings &amp; Live Theming</b><br>
Rotate the phone sideways to configure themes, Matugen wallpaper palettes, bar &amp; dock layouts, desktop widgets, display scaling, and shell behavior without editing config files.
</td>
</tr>
<tr>
<td width="50%" valign="top">
<img src="assets/shot-edit-desktop.png" alt="Edit Desktop scrapbook layout editor" width="100%">
<br><b>Edit Desktop &amp; Draggable Widgets</b><br>
Place, drag, and resize desktop widgets (clock, weather, calendar, to-do, sticky notes, audio visualizer, live lyrics, palette, user card, custom images) per monitor with saved presets.
</td>
<td width="50%" valign="top">
<img src="assets/shot-sekai-pad-eq.png" alt="Sekai Pad interface" width="100%">
<br><b>Native Media, Lyrics &amp; Audio Visualizer</b><br>
Integrates with Windows SMTC (GlobalSystemMediaTransportControls), WASAPI loopback spectrum capture, and multi-provider synchronized lyrics (Ukikura Lyrics).
</td>
</tr>
</table>

---

## Features

- **Single-File Graphical Installer (`SekaiPop-1.0-Windows-Setup.exe`)**: Self-extracting LZX-compressed Win32 installer with bundled Qt 6 runtime, shaders, and assets. Zero manual terminal commands needed.
- **Native Shell Replacement with Watchdog (`SekaiPopBoot.exe`)**: Replaces `explorer.exe` via Winlogon shell integration, restarts `SekaiPopShell.exe` automatically on unexpected exit, staggers Windows startup apps until the shell UI is visible, and automatically falls back to `explorer.exe` in Windows Safe Mode or after repeated crashes.
- **Shell Mode Manager (`SekaiPopManager.exe`)**: Switch between Full Native Shell, Seamless Overlay, and Standard Windows Explorer anytime, or perform a clean uninstall from Windows Settings (`Apps & Features`).
- **Fast Cold Boot & Low Resource Footprint**: Shared Direct3D 11 QRhi graphics device across all shell windows, URL-lazy QML loading for overlays/popups/widgets, and automatic pausing of background visualizers when fullscreen apps or games are active.
- **Full UWP / MSIX & Win32 App Support**: Launches classic `.exe`/`.lnk` apps, standalone File Explorer windows (`/separate`), and Store/MSIX apps (such as WhatsApp Desktop, Windows Terminal, Calculator, and Settings) via `IApplicationActivationManager` even when `explorer.exe` is not running, with automatic tray-window surfacing and headless single-instance recovery.
- **Native Multi-Monitor Screenshot Tool**: Built-in region snip overlay (`PrtSc` / `Win + Shift + S`), active monitor capture (`Win + PrtSc`), active window capture (`Alt + PrtSc`), and all-monitors capture (`Ctrl + PrtSc`) with clipboard copy and PNG saving.
- **Native Taskman & Minimized Window Handling**: Registers as the Win32 Taskman window with `ARW_HIDE` when `explorer.exe` is absent so minimized apps stay hidden cleanly instead of spawning legacy floating titlebar pills.
- **Live Video & Shader Wallpapers**: Per-monitor static, animated shader transitions (`Ring`, `Iris`, `Star`, `Blocks`, `Flash`, `Fade`), Wallhaven browser, and hardware-decoded live video wallpapers.

---

## Installation

### Recommended: Windows Installer

1. Download **`SekaiPop-1.0-Windows-Setup.exe`** from the [GitHub Releases](https://github.com/nahihiku/SekaiPop-_windows-version/releases) page.
2. Double-click **`SekaiPop-1.0-Windows-Setup.exe`** and accept the UAC administrator prompt.
3. Choose your preferred shell mode:
   - **Full Native Shell (Winlogon)**: Replaces Windows Explorer UI completely.
   - **Seamless Overlay**: Keeps Windows Explorer running while hiding the Windows taskbar.
4. Click **Install**. SekaiPop installs to `C:\Program Files\SekaiPop`, creates Start Menu shortcuts, registers the uninstaller, and launches immediately.

For detailed installation, portable mode, and uninstall steps, see [docs/windows/INSTALLATION.md](docs/windows/INSTALLATION.md).

---

## Keybinds

| Shortcut | Action |
|---|---|
| `Win` (tap) / `Alt + N` | Toggle Quick Panel (Phone control center) |
| `Alt + Space` / `Ctrl + Space` | Open App Launcher |
| `Win + E` | Open standalone File Explorer |
| `Win + R` | Open Windows Run dialog |
| `Win + I` | Open Windows Settings |
| `Win + D` | Toggle Show Desktop (minimize / restore all windows) |
| `PrtSc` / `Win + Shift + S` | Native region screenshot snip overlay |
| `Win + PrtSc` | Capture active monitor |
| `Alt + PrtSc` | Capture active window |
| `Ctrl + PrtSc` | Capture all monitors |
| `Ctrl + Alt + Shift + R` | Restart SekaiPop Shell cleanly |
| `Ctrl + Alt + Shift + E` | **Emergency: Restore Windows Explorer & Exit SekaiPop** |
| `Ctrl + Alt + Shift + Q` | Exit SekaiPop Shell and restore Windows workareas |

See [docs/windows/KEYBINDS.md](docs/windows/KEYBINDS.md) for the complete reference.

---

## Documentation

Dedicated Windows documentation lives in `docs/windows/`:

- [Installation & Shell Modes](docs/windows/INSTALLATION.md) - Setup installer, Full Native Shell vs Seamless Overlay, portable mode, and clean uninstall
- [Windows Architecture](docs/windows/ARCHITECTURE.md) - Native Win32/WinRT subsystems, `SekaiPopBoot` watchdog, UWP/MSIX launching, and startup optimization
- [Keybinds & Shortcuts](docs/windows/KEYBINDS.md) - Global hotkeys, native `Win+` chords, screenshot shortcuts, and emergency controls
- [Building from Source](docs/windows/BUILDING.md) - Compiling with Qt 6.8 + CMake + Ninja (LLVM-MinGW or MSVC) and packaging the single-file installer
- [Troubleshooting & Recovery](docs/windows/TROUBLESHOOTING.md) - Logs, restoring Explorer, UWP/File Explorer tips, and Safe Mode recovery
- [Panduan Ringkas (Bahasa Indonesia)](README_WINDOWS.md) - Quick-start documentation in Indonesian

---

## Building from Source

Prerequisites: **Windows 10/11 (x64)**, **Qt 6.8+** (LLVM-MinGW or MSVC), **CMake 3.21+**, and **Ninja**.

```powershell
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH="C:\Qt\6.8.3\llvm-mingw_64" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 8
```

To build the standalone portable folder and single-file `SekaiPop-1.0-Windows-Setup.exe`:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build-release.ps1
```

Full build instructions are in [docs/windows/BUILDING.md](docs/windows/BUILDING.md).

---

## Emergency Recovery

SekaiPop is built with multiple safety nets so you are never locked out of Windows:

- Press **`Ctrl + Alt + Shift + E`** at any time to restore work areas, launch `explorer.exe`, and exit SekaiPop.
- Press **`Ctrl + Shift + Esc`** to open Task Manager (`File -> Run new task -> explorer.exe`).
- Run **`EmergencyRestoreWindows.bat`** in `C:\Program Files\SekaiPop` or launch **SekaiPop Shell Manager** (`SekaiPopManager.exe`) to switch back to standard Windows Explorer.
- Booting into **Windows Safe Mode** causes `SekaiPopBoot.exe` to automatically hand off directly to `explorer.exe`.

---

## Credits & License

- Original SekaiPop design and QML shell foundation by the upstream SekaiPop project.
- Windows native C++20 / Qt 6 / Win32 host, boot watchdog, installer, screenshot engine, and Windows service backends maintained in this repository.
- Licensed under the **GNU General Public License v3.0**. See [LICENSE](LICENSE).
