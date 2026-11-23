# SekaiPop Windows Desktop Shell (v1.0)

Port native Windows dari tampilan desktop **SekaiPop** yang dibangun dengan **C++20**, **Qt 6 Quick/QML**, dan **Win32/WinRT API** langsung. Dirancang untuk menggantikan antarmuka desktop Windows (`explorer.exe`) secara utuh maupun berjalan sebagai overlay mulus, lengkap dengan installer grafis satu file tanpa perlu mengetik perintah di terminal.

Untuk dokumentasi lengkap berbahasa Inggris, lihat folder [docs/windows/](docs/windows/INSTALLATION.md).

---

## Fitur Utama di Windows

- **Installer Satu File (`SekaiPop-1.0-Windows-Setup.exe`)**: Cukup klik dua kali untuk memasang `SekaiPopShell.exe`, `SekaiPopBoot.exe`, `SekaiPopManager.exe`, seluruh runtime Qt 6, shader, dan pintasan Start Menu.
- **Dua Mode Operasi** (dapat diubah kapan saja melalui **SekaiPop Shell Manager**):
  1. **Pengganti UI Penuh (Winlogon Native Shell)**: `SekaiPopBoot.exe` dijalankan langsung oleh Windows Winlogon menggantikan taskbar dan desktop `explorer.exe`, dilengkapi watchdog anti-crash dan fallback otomatis ke `explorer.exe` saat Safe Mode.
  2. **Seamless Overlay**: `explorer.exe` tetap aktif di latar belakang sementara taskbar Windows disembunyikan otomatis dan SekaiPop berjalan saat login.
- **Startup Cepat & Ringan**: Menggunakan satu perangkat Direct3D 11 QRhi bersama untuk seluruh jendela shell, lazy-loading QML berbasis URL untuk popup/widget/overlay, serta penjadwalan bertahap aplikasi startup Windows setelah wallpaper dan bar tampil.
- **Peluncuran Aplikasi Win32, File Explorer, dan UWP/MSIX**: Mendukung `.exe`, `.lnk`, File Explorer mandiri (`/separate`), serta aplikasi Microsoft Store/MSIX (WhatsApp Desktop, Windows Terminal, Calculator, Settings) melalui `IApplicationActivationManager` lengkap dengan pemulihan jendela tray dan auto-recovery proses headless.
- **Screenshot Multi-Monitor Native**: Overlay snipping area (`PrtSc` / `Win + Shift + S`), tangkap monitor aktif (`Win + PrtSc`), jendela aktif (`Alt + PrtSc`), dan semua monitor (`Ctrl + PrtSc`).
- **Integrasi Sistem Windows Native**: SMTC media player, WASAPI loopback audio visualizer, Ukikura Lyrics multi-sumber, Wi-Fi/Bluetooth/Audio CoreAudio, baterai, pemantau CPU/RAM, serta dukungan multi-monitor DPI `PerMonitorV2`.

---

## Shortcut Penting & Pemulihan Darurat

| Pintasan | Fungsi |
|---|---|
| `Win` (tekan singkat) / `Alt + N` | Buka / tutup Quick Panel (Phone control center) |
| `Alt + Space` / `Ctrl + Space` | Buka App Launcher |
| `Win + E` | Buka File Explorer mandiri |
| `Win + R` | Buka dialog Run Windows |
| `Win + I` | Buka Windows Settings |
| `Win + D` | Minimalkan / kembalikan semua jendela (Show Desktop) |
| `PrtSc` / `Win + Shift + S` | Screenshot pilih area (snip overlay) |
| `Win + PrtSc` | Screenshot monitor aktif |
| `Alt + PrtSc` | Screenshot jendela aktif |
| `Ctrl + PrtSc` | Screenshot seluruh monitor |
| `Ctrl + Alt + Shift + R` | Restart SekaiPop Shell |
| `Ctrl + Alt + Shift + E` | **Darurat: Pulihkan Windows Explorer dan keluar dari SekaiPop** |
| `Ctrl + Alt + Shift + Q` | Keluar dari SekaiPop dan pulihkan work area Windows |

Selain pintasan di atas, Anda selalu dapat menjalankan `SekaiPopManager.exe` atau `EmergencyRestoreWindows.bat` di folder instalasi (`C:\Program Files\SekaiPop`) untuk kembali ke Windows Explorer standar.

---

## Cara Build dari Source di Windows

Prasyarat:
- **Qt 6.8+** (LLVM-MinGW atau MSVC) dengan modul `Core`, `Gui`, `Network`, `Quick`, `Qml`, `QuickControls2`, `Svg`, `Widgets`, dan `ShaderTools` (opsional).
- **CMake 3.21+**, **Ninja**, dan compiler **LLVM-MinGW (`clang++`)** atau **MSVC (Visual Studio 2022 + Windows SDK)**.

Build target CMake:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH="C:\Qt\6.8.3\llvm-mingw_64" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 8
```

Bangun paket rilis portabel dan installer satu file `release\SekaiPop-1.0-Windows-Setup.exe`:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build-release.ps1
```

Lihat dokumentasi teknis lengkap di:
- [docs/windows/INSTALLATION.md](docs/windows/INSTALLATION.md)
- [docs/windows/ARCHITECTURE.md](docs/windows/ARCHITECTURE.md)
- [docs/windows/KEYBINDS.md](docs/windows/KEYBINDS.md)
- [docs/windows/BUILDING.md](docs/windows/BUILDING.md)
- [docs/windows/TROUBLESHOOTING.md](docs/windows/TROUBLESHOOTING.md)
