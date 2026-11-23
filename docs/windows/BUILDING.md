# Building SekaiPop for Windows from Source

This document explains how to compile all Windows executables (`SekaiPopShell.exe`, `SekaiPopBoot.exe`, `SekaiPopManager.exe`, and `SekaiPopSetupStub.exe`) and package the standalone portable folder and single-file Setup installer.

---

## Prerequisites

1. **Windows 10 or Windows 11 (x64)**
2. **Qt 6.8.3** (6.5+ supported; 6.8.3 recommended) with components:
   - `Qt6::Core`, `Qt6::Gui`, `Qt6::Network`, `Qt6::Quick`, `Qt6::Qml`, `Qt6::QuickControls2`, `Qt6::Svg`, `Qt6::Widgets`
   - `Qt6::ShaderTools` (`qsb.exe`, optional if prebuilt shaders are already present)
3. **CMake 3.21+** and **Ninja**
4. **C++20 Compiler Toolchain**:
   - **LLVM-MinGW (`clang++`, UCRT x86_64)** (used for official releases), or
   - **MSVC (Visual Studio 2022 + Windows 10/11 SDK)**

---

## 1. Configure & Compile

### Using LLVM-MinGW + Ninja (Recommended)

```powershell
cmake -S . -B build -G Ninja `
  -DCMAKE_PREFIX_PATH="C:\Qt\6.8.3\llvm-mingw_64" `
  -DCMAKE_BUILD_TYPE=Release

cmake --build build -j 8
```

### Using MSVC (Visual Studio 2022)

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH="C:\Qt\6.8.3\msvc2022_64"
cmake --build build --config Release -j 8
```

### Build Targets Produced

- `build\SekaiPopShell.exe` - Main Qt 6 / Win32 desktop shell
- `build\SekaiPopBoot.exe` - Winlogon watchdog and deferred startup launcher
- `build\SekaiPopManager.exe` - Shell mode manager and uninstaller UI
- `build\SekaiPopSetupStub.exe` - Self-extracting setup stub used to build the single-file installer

Optional CMake flag:
- `-DSEKAIPOP_CONSOLE=ON` builds `SekaiPopShell.exe` with an attached console window for live debug output.

---

## 2. Packaging the Standalone Portable Folder & Single-File Installer

Run the release packaging script from the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build-release.ps1
```

If Qt is installed in a different path, pass `-QtDir`:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build-release.ps1 -QtDir "C:\Qt\6.8.3\llvm-mingw_64" -Version "1.0"
```

What `scripts/build-release.ps1` does:
1. Compiles all 4 Release CMake targets.
2. Stages executables, C++ runtime DLLs (`libc++.dll`, `libunwind.dll`), `sekaipop/`, `shaders/`, `qml/`, `ukikura-lyrics/`, `EmergencyRestoreWindows.bat`, `LICENSE`, and `README_WINDOWS.md` into `release\SekaiPop-1.0-Windows-Portable\`.
3. Runs `windeployqt` to bundle all required Qt 6 DLLs and QML modules cleanly.
4. Packs the portable tree into a solid archive and compresses it with Windows `makecab` (`LZX:21`).
5. Appends the compressed CAB payload and binary footer onto `SekaiPopSetupStub.exe` to generate `release\SekaiPop-1.0-Windows-Setup.exe`.

---

## 3. Clean-PATH Smoke Test

Before publishing a release, verify that the portable bundle starts and exits cleanly with no system Qt installation in `PATH`:

```powershell
$oldPath = $env:PATH
$env:PATH = "C:\Windows\System32;C:\Windows"
try {
    $p = Start-Process -FilePath "release\SekaiPop-1.0-Windows-Portable\SekaiPopShell.exe" `
                       -ArgumentList "--smoke-ms","3500" -PassThru -Wait
    Write-Host "PortableExitCode=$($p.ExitCode)"
} finally {
    $env:PATH = $oldPath
}
```

Expected result: `PortableExitCode=0` and zero warnings or errors in `%LOCALAPPDATA%\SekaiPop\SekaiPopShell\shell.log`.
