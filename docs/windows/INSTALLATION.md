# Installation Guide (Windows 10 & Windows 11)

This guide covers installing, configuring shell modes, updating, and cleanly uninstalling **SekaiPop for Windows**.

---

## System Requirements

| Component | Requirement |
|---|---|
| Operating System | Windows 10 (version 1903+) or Windows 11, 64-bit (`x64`) |
| Graphics | Direct3D 11 compatible GPU (Intel UHD, AMD Radeon, or NVIDIA GeForce) |
| Disk Space | ~120 MB installed (`~28 MB` installer download) |
| Privileges | Administrator privileges required only during installation or when switching Winlogon Shell mode |

No external Qt runtime, Python installation, or terminal tools are required when using the release installer.

---

## 1. Installing via Single-File Setup (Recommended)

1. Download **`SekaiPop-1.0-Windows-Setup.exe`** from the GitHub Releases page.
2. Double-click the executable and approve the Windows UAC prompt.
3. Select the installation folder (default: `C:\Program Files\SekaiPop`).
4. Choose your desired **Shell Integration Mode**:
   - **Full Native Shell (Winlogon Native Shell)**
     - Configures `HKCU\Software\Microsoft\Windows NT\CurrentVersion\Winlogon\Shell` (and optional system integration) to start `SekaiPopBoot.exe`.
     - `explorer.exe` does not launch its desktop/taskbar at login.
     - `SekaiPopBoot.exe` launches `SekaiPopShell.exe --native-shell`, monitors it for crashes, defers other Windows startup apps until SekaiPop UI is visible, and falls back to `explorer.exe` automatically in Safe Mode.
   - **Seamless Overlay (Auto-Start)**
     - Keeps `explorer.exe` as the system shell.
     - Registers `SekaiPopShell.exe` to auto-start at login and automatically hides the Windows taskbar (`Shell_TrayWnd` / `Shell_SecondaryTrayWnd`).
5. Choose whether to create Start Menu / Desktop shortcuts and launch immediately, then click **Install**.

### Updating an Existing Installation

To upgrade to a newer release, simply run the new **`SekaiPop-1.0-Windows-Setup.exe`** directly over your existing installation. The installer safely stops running `SekaiPopBoot.exe` / `SekaiPopShell.exe` processes, updates all binaries and QML resources, preserves your user settings in `%LOCALAPPDATA%\SekaiPop`, and restarts the shell.

---

## 2. Managing Shell Modes After Installation

Open **SekaiPop Shell Manager** from the Start Menu or run:

```text
C:\Program Files\SekaiPop\SekaiPopManager.exe
```

From SekaiPop Shell Manager you can:
- Inspect the currently active shell mode.
- Switch between **Full Native Shell**, **Seamless Overlay**, and **Standard Windows Explorer** with one click.
- Launch or restart SekaiPop immediately.
- Perform a complete uninstall.

---

## 3. Portable Usage (Testing Without Registry Changes)

If you built from source or extracted the portable release folder (`SekaiPop-1.0-Windows-Portable`):

1. Run `SekaiPopShell.exe` directly from the folder.
2. No Windows Registry entries are modified in portable runs.
3. Press `Ctrl + Alt + Shift + E` or `Ctrl + Alt + Shift + Q` at any time to restore the Windows taskbar and exit cleanly.

---

## 4. Uninstalling SekaiPop

You can uninstall cleanly via either method:

1. **Windows Settings**: Open **Settings -> Apps -> Installed apps** (or Control Panel -> Programs and Features), select **SekaiPop Desktop Shell**, and click **Uninstall**.
2. **SekaiPop Shell Manager**: Open `SekaiPopManager.exe` and click **Uninstall**.

The uninstaller automatically:
- Restores `explorer.exe` as the default Windows shell.
- Removes startup registry keys and Start Menu / Desktop shortcuts.
- Restores Windows desktop workareas and shows the Windows taskbar.
- Optionally asks whether to remove user configuration/cache data in `%LOCALAPPDATA%\SekaiPop`.
