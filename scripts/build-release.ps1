param(
    [string]$QtDir = "C:\Qt\6.8.3\llvm-mingw_64",
    [string]$Version = "1.0",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$BuildDir = Join-Path $Root "build"
$ReleaseRoot = Join-Path $Root "release"
$PortableDir = Join-Path $ReleaseRoot "SekaiPop-$Version-Windows-Portable"
$TempWork = Join-Path $ReleaseRoot "_pack_tmp"
$SetupOut = Join-Path $ReleaseRoot "SekaiPop-$Version-Windows-Setup.exe"
$WinDeployQt = Join-Path $QtDir "bin\windeployqt.exe"
if (-not (Test-Path $WinDeployQt)) {
    $WinDeployQt = Join-Path $QtDir "bin\windeployqt6.exe"
}

Write-Host "[1/6] Building Release targets..."
if (-not $SkipBuild) {
    $liveExe = Join-Path $BuildDir "SekaiPopShell.exe"
    if (Test-Path $liveExe) {
        Get-ChildItem $BuildDir -Filter "SekaiPopShell.prev*.exe" -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
        $prevExe = Join-Path $BuildDir ("SekaiPopShell.prev_{0}.exe" -f (Get-Date -Format "yyyyMMddHHmmss"))
        Move-Item -Force $liveExe $prevExe
    }
    & cmake --build $BuildDir -j 8
    if ($LASTEXITCODE -ne 0) { throw "CMake build failed." }
}

foreach ($bin in @("SekaiPopShell.exe", "SekaiPopBoot.exe", "SekaiPopManager.exe", "SekaiPopSetupStub.exe")) {
    $p = Join-Path $BuildDir $bin
    if (-not (Test-Path $p)) { throw "Missing built binary: $p" }
}

Write-Host "[2/6] Staging standalone portable release folder..."
if (Test-Path $PortableDir) { Remove-Item -Recurse -Force $PortableDir }
if (Test-Path $TempWork) { Remove-Item -Recurse -Force $TempWork }
New-Item -ItemType Directory -Force -Path $PortableDir | Out-Null
New-Item -ItemType Directory -Force -Path $TempWork | Out-Null

# Core executables & C++ runtime DLLs
Copy-Item (Join-Path $BuildDir "SekaiPopShell.exe") $PortableDir
Copy-Item (Join-Path $BuildDir "SekaiPopBoot.exe") $PortableDir
Copy-Item (Join-Path $BuildDir "SekaiPopManager.exe") $PortableDir
foreach ($rt in @("libc++.dll", "libunwind.dll")) {
    $src = Join-Path $BuildDir $rt
    if (-not (Test-Path $src)) { $src = Join-Path $QtDir "bin\$rt" }
    if (Test-Path $src) { Copy-Item $src $PortableDir }
}
foreach ($doc in @("README_WINDOWS.md", "LICENSE")) {
    $src = Join-Path $Root $doc
    if (Test-Path $src) { Copy-Item $src $PortableDir }
}
$emergBat = Join-Path $BuildDir "EmergencyRestoreWindows.bat"
if (Test-Path $emergBat) { Copy-Item $emergBat $PortableDir }

# Runtime trees (sekaipop QML + assets + scripts, baked shaders, custom qml, ukikura-lyrics)
Copy-Item -Recurse -Force (Join-Path $Root "sekaipop") (Join-Path $PortableDir "sekaipop")
Copy-Item -Recurse -Force (Join-Path $BuildDir "shaders") (Join-Path $PortableDir "shaders")
Copy-Item -Recurse -Force (Join-Path $Root "qml") (Join-Path $PortableDir "qml")
if (Test-Path (Join-Path $Root "ukikura-lyrics")) {
    Copy-Item -Recurse -Force (Join-Path $Root "ukikura-lyrics") (Join-Path $PortableDir "ukikura-lyrics")
}

Write-Host "[3/6] Deploying Qt 6 runtime DLLs and QML modules via windeployqt..."
if (-not (Test-Path $WinDeployQt)) { throw "windeployqt not found at $WinDeployQt" }
$env:PATH = "$(Join-Path $QtDir 'bin');$env:PATH"
& $WinDeployQt `
    --no-translations `
    --no-compiler-runtime `
    --no-opengl-sw `
    --qmldir (Join-Path $Root "sekaipop") `
    --qmldir (Join-Path $Root "qml") `
    (Join-Path $PortableDir "SekaiPopShell.exe") | Out-Null
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed with exit code $LASTEXITCODE" }

# Ensure Qt6LabsFolderListModel & QtQuick/Effects/Shapes plugins deployed cleanly
foreach ($extraDll in @("Qt6LabsFolderListModel.dll", "Qt6QuickEffects.dll", "Qt6QuickShapes.dll", "Qt6QuickLayouts.dll", "D3Dcompiler_47.dll")) {
    $dstDll = Join-Path $PortableDir $extraDll
    if (-not (Test-Path $dstDll)) {
        $cand = Join-Path $BuildDir $extraDll
        if (-not (Test-Path $cand)) { $cand = Join-Path $QtDir "bin\$extraDll" }
        if (Test-Path $cand) { Copy-Item $cand $dstDll }
    }
}

# Remove debug/dev-only bloat from portable bundle
foreach ($junk in @("opengl32sw.dll", "qmltooling")) {
    $jp = Join-Path $PortableDir $junk
    if (Test-Path $jp) { Remove-Item -Recurse -Force $jp }
}
Get-ChildItem $PortableDir -Recurse -Include "*.pdb","*.d","*.bak" -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue

Write-Host "[4/6] Packing release tree into solid payload.pak..."
$PakPath = Join-Path $TempWork "payload.pak"
$files = Get-ChildItem $PortableDir -Recurse -File | Sort-Object FullName
$prefixLen = $PortableDir.TrimEnd('\').Length + 1
$totalBytes = [uint64]0
foreach ($f in $files) { $totalBytes += [uint64]$f.Length }

$fs = [System.IO.File]::Create($PakPath)
$bw = New-Object System.IO.BinaryWriter($fs)
$bw.Write([System.Text.Encoding]::ASCII.GetBytes("SKPPPAK1"))
$bw.Write([uint32]$files.Count)
$bw.Write([uint64]$totalBytes)

$copyBuf = New-Object byte[] (262144)
foreach ($f in $files) {
    $rel = $f.FullName.Substring($prefixLen).Replace('/', '\')
    $relBytes = [System.Text.Encoding]::UTF8.GetBytes($rel)
    $bw.Write([uint32]$relBytes.Length)
    $bw.Write($relBytes)
    $bw.Write([uint64]$f.Length)
    $inFs = [System.IO.File]::OpenRead($f.FullName)
    try {
        while (($n = $inFs.Read($copyBuf, 0, $copyBuf.Length)) -gt 0) {
            $bw.Write($copyBuf, 0, $n)
        }
    } finally {
        $inFs.Dispose()
    }
}
$bw.Flush()
$bw.Dispose()
$fs.Dispose()
Write-Host ("  Packed {0} files ({1:F2} MB uncompressed)" -f $files.Count, ($totalBytes / 1MB))

Write-Host "[5/6] Compressing payload.pak with Windows LZX:21 CAB (makecab)..."
$DdfPath = Join-Path $TempWork "payload.ddf"
$CabPath = Join-Path $TempWork "payload.cab"
@"
.OPTION EXPLICIT
.Set CabinetNameTemplate=payload.cab
.Set DiskDirectoryTemplate=$TempWork
.Set Cabinet=on
.Set Compress=on
.Set CompressionType=LZX
.Set CompressionMemory=21
.Set MaxDiskSize=0
.Set FolderSizeThreshold=500000000
"$PakPath"
"@ | Set-Content -Path $DdfPath -Encoding ASCII

& makecab.exe /V1 /F $DdfPath | Out-Null
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $CabPath)) {
    throw "makecab.exe failed (exit code $LASTEXITCODE)"
}
$cabInfo = Get-Item $CabPath
Write-Host ("  Compressed LZX:21 CAB size: {0:F2} MB (ratio: {1:F1}%)" -f ($cabInfo.Length / 1MB), (($cabInfo.Length * 100.0) / [math]::Max(1, $totalBytes)))

Write-Host "[6/6] Building single-file Setup executable ($SetupOut)..."
$StubPath = Join-Path $BuildDir "SekaiPopSetupStub.exe"
$stubBytes = [System.IO.File]::ReadAllBytes($StubPath)
$cabOffset = [uint64]$stubBytes.Length
$cabSize = [uint64]$cabInfo.Length

if (Test-Path $SetupOut) { Remove-Item -Force $SetupOut }
$outFs = [System.IO.File]::Create($SetupOut)
$outBw = New-Object System.IO.BinaryWriter($outFs)
try {
    $outBw.Write($stubBytes)
    $cabFs = [System.IO.File]::OpenRead($CabPath)
    try {
        while (($n = $cabFs.Read($copyBuf, 0, $copyBuf.Length)) -gt 0) {
            $outBw.Write($copyBuf, 0, $n)
        }
    } finally {
        $cabFs.Dispose()
    }
    # 32-byte EmbeddedCabFooter: uint64 cabOffset, uint64 cabSize, uint64 unpackedBytes, char[8] "SKPPCAB1"
    $outBw.Write([uint64]$cabOffset)
    $outBw.Write([uint64]$cabSize)
    $outBw.Write([uint64]$totalBytes)
    $outBw.Write([System.Text.Encoding]::ASCII.GetBytes("SKPPCAB1"))
    $outBw.Flush()
} finally {
    $outBw.Dispose()
    $outFs.Dispose()
}

Remove-Item -Recurse -Force $TempWork -ErrorAction SilentlyContinue

$finalSetup = Get-Item $SetupOut
Write-Host ("DONE! Single-file installer ready: {0} ({1:F2} MB)" -f $finalSetup.FullName, ($finalSetup.Length / 1MB))
Write-Host ("Portable release folder ready:     {0}" -f $PortableDir)
