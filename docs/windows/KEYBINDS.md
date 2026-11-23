# Windows Keybinds & Shortcuts

SekaiPop registers global Win32 hotkeys and a low-level keyboard hook so both SekaiPop controls and essential Windows shortcuts work smoothly, including in **Full Native Shell** mode when `explorer.exe` is not running.

---

## Core SekaiPop Controls

| Shortcut | Action |
|---|---|
| `Win` (single tap) | Toggle Quick Panel (2.5D Phone control center) |
| `Alt + N` | Toggle Quick Panel |
| `Alt + Space` | Open / close App Launcher (Phone Apps screen) |
| `Ctrl + Space` | Open / close App Launcher |

---

## Native Windows Chords (Full Native Shell Mode)

When `explorer.exe` is not running, SekaiPop intercepts and handles these essential Windows chords natively:

| Shortcut | Action |
|---|---|
| `Win + E` | Open a standalone File Explorer window (`explorer.exe /separate`) |
| `Win + R` | Open the native Windows Run dialog |
| `Win + I` | Open Windows Settings (`ms-settings:`) |
| `Win + D` | Toggle Show Desktop (minimize / restore tracked windows) |
| `Win + Shift + S` | Start SekaiPop interactive region screenshot snip |
| `Win + L` | Lock Windows workstation (handled directly by Windows Winlogon) |
| `Ctrl + Shift + Esc` | Open Windows Task Manager (handled directly by Windows) |
| `Ctrl + Alt + Del` | Windows Security screen (handled directly by Windows kernel) |

---

## Native Multi-Monitor Screenshots

In Full Native Shell mode, `PrintScreen` is routed directly to SekaiPop's built-in multi-monitor capture tool (images are copied to the clipboard and saved as PNG files):

| Shortcut | Action |
|---|---|
| `PrtSc` | Interactive region snip overlay across all monitors |
| `Win + Shift + S` | Interactive region snip overlay across all monitors |
| `Win + PrtSc` | Instant capture of the active monitor |
| `Alt + PrtSc` | Instant capture of the active foreground window |
| `Ctrl + PrtSc` | Instant capture of all connected monitors |

---

## Media & Volume Keys (Full Native Shell Mode)

Hardware media and volume keys are handled natively when `explorer.exe` is absent:

| Key | Action |
|---|---|
| `Volume Up` / `Volume Down` | Adjust Windows CoreAudio speaker volume (+/- 2%) |
| `Volume Mute` | Toggle speaker mute |
| `Media Play/Pause` | Toggle active Windows SMTC media session |
| `Media Next` / `Previous` | Skip forward or backward in active media session |

---

## Emergency & Shell Recovery Shortcuts

These shortcuts are always registered while `SekaiPopShell.exe` is running:

| Shortcut | Action |
|---|---|
| `Ctrl + Alt + Shift + R` | Restart `SekaiPopShell.exe` cleanly (restores work areas before restarting; exits with code `42` under `SekaiPopBoot` watchdog) |
| `Ctrl + Alt + Shift + E` | **Emergency Restore**: restores work areas, unhides Windows taskbar, launches `explorer.exe`, and exits SekaiPop |
| `Ctrl + Alt + Shift + Q` | Clean exit from `SekaiPopShell.exe`, restoring work areas and starting `explorer.exe` if needed |
