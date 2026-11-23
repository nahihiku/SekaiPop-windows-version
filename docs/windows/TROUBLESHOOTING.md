# Troubleshooting & Recovery (Windows)

This guide helps diagnose common issues and recover Windows Explorer quickly if anything goes wrong.

---

## 1. Emergency Recovery to Windows Explorer

If SekaiPop fails to start or you want to immediately return to the standard Windows taskbar and desktop:

1. **Hotkey**: Press **`Ctrl + Alt + Shift + E`**. SekaiPop restores work areas, launches `explorer.exe`, and exits.
2. **Task Manager**: Press **`Ctrl + Shift + Esc`**, select **Run new task**, type `explorer.exe`, and press Enter.
3. **Manager App**: Run `C:\Program Files\SekaiPop\SekaiPopManager.exe`, select **Windows Explorer Standar**, and click Apply.
4. **Emergency Batch Script**: Run `C:\Program Files\SekaiPop\EmergencyRestoreWindows.bat` (from Task Manager or `Win + R`).
5. **Safe Mode**: Booting Windows into Safe Mode automatically causes `SekaiPopBoot.exe` to start `explorer.exe` directly instead of SekaiPop.

---

## 2. Checking Diagnostic Logs

SekaiPop writes structured startup, window tracking, hotkey, and app launch logs to:

```text
%LOCALAPPDATA%\SekaiPop\SekaiPopShell\shell.log
```

Useful log prefixes:
- `STARTUP:` - Boot timings (`engine.load`, `UI ready`)
- `[Win32Utils]` - UWP/MSIX app activation, File Explorer launches, and window focus actions
- `[WindowTracker]` - Tracked windows, occlusion, and fullscreen monitor detection
- `[HotkeyManager]` - Keyboard hook and global hotkey registration status
- `[AppBarManager]` - Monitor workarea reservations

---

## 3. UWP / MSIX Apps (WhatsApp Desktop, Terminal, Store)

In **Full Native Shell** mode (`explorer.exe` not running), standard `ShellExecute("shell:AppsFolder\...")` fails on Windows with `Class not registered`.

SekaiPop solves this natively by:
- Activating MSIX/UWP apps directly via `IApplicationActivationManager::ActivateApplication`.
- Transferring foreground permission (`AllowSetForegroundWindow(ASFW_ANY)`) before activation and surfacing the launched window automatically.
- Restoring tray-minimized WinUI 3 windows (such as WhatsApp Desktop after clicking `X` when `Shell_TrayWnd` is absent) and auto-recovering stuck headless single-instance background processes.

If an older build of SekaiPop was previously installed, run the latest `SekaiPop-1.0-Windows-Setup.exe` so `C:\Program Files\SekaiPop\SekaiPopShell.exe` is updated.

---

## 4. Opening File Explorer Without Starting the Windows Taskbar

In Full Native Shell mode, launching plain `explorer.exe` without arguments can cause Windows to spawn the desktop taskbar. SekaiPop prevents this by automatically launching standalone File Explorer windows with:

```text
explorer.exe /separate,::{20D04FE0-3AEA-1069-A2D8-08002B30309D}
```

This applies automatically to `Win + E`, the Dock **Files** tool button, and pinned **File Explorer** (`shell:AppsFolder\Microsoft.Windows.Explorer`).

---

## 5. Resetting QML Stage Cache

If you see stale UI behavior after manually modifying QML files or upgrading:

1. Exit SekaiPop (`Ctrl + Alt + Shift + Q` or via `SekaiPopManager.exe`).
2. Delete `%LOCALAPPDATA%\SekaiPop\qs_stage`.
3. Restart `SekaiPopShell.exe` (the runtime tree is automatically re-staged on the next launch).

---

## 6. Screenshot Shortcuts Not Responding

- In **Full Native Shell** mode, SekaiPop's low-level hook captures `PrtSc`, `Win + Shift + S`, `Win + PrtSc`, `Alt + PrtSc`, and `Ctrl + PrtSc` automatically and opens `ScreenshotOverlay`.
- In **Seamless Overlay** mode (`explorer.exe` running), Windows' built-in Snipping Tool continues to handle `PrtSc` / `Win + Shift + S` normally.
- If another screenshot app (such as ShareX or Lightshot) holds the `PrintScreen` key, close or disable its capture hotkey first.
