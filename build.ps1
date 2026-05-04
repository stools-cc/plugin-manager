$ErrorActionPreference = "Stop"
$ROOT = $PSScriptRoot

# --- VS Developer Environment ---
$vsPath = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
if (-not $vsPath) { throw "Visual Studio not found" }

Import-Module "$vsPath\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath $vsPath -DevCmdArguments "-arch=x64 -host_arch=x64" -SkipAutomaticLocation

$OBS_BIN    = "C:\Program Files\obs-studio\bin\64bit"
$DEPS_DIR   = "$ROOT\deps"
$OBS_SRC    = "$DEPS_DIR\obs-studio"
$OBS_DEPS   = "$DEPS_DIR\obs-deps"
$QT6_DIR    = "$DEPS_DIR\qt6"
$OBS_LIBS   = "$DEPS_DIR\obs-libs"
$BUILD_DIR  = "$ROOT\build"
$OBS_VERSION = "32.1.0"

# --- 1. OBS headers (sparse clone) ---
if (-not (Test-Path "$OBS_SRC\libobs\obs-module.h")) {
    Write-Host "[1/5] Cloning OBS Studio headers..."
    git clone --depth 1 --branch $OBS_VERSION --filter=blob:none --sparse `
        "https://github.com/obsproject/obs-studio.git" $OBS_SRC
    Push-Location $OBS_SRC
    git sparse-checkout set libobs frontend/api deps/w32-pthreads
    Pop-Location

    @"
#pragma once
#define OBS_DATA_PATH "data"
#define OBS_PLUGIN_PATH "obs-plugins/64bit"
#define OBS_PLUGIN_DESTINATION "obs-plugins/64bit"
#define OBS_INSTALL_PREFIX "C:/Program Files/obs-studio"
#define OBS_RELEASE_CANDIDATE 0
#define OBS_BETA 0
"@ | Out-File -Encoding ASCII "$OBS_SRC\libobs\obsconfig.h"
} else {
    Write-Host "[1/5] OBS headers: OK"
}

# --- 2. OBS deps (curl headers + libs) ---
$depsZip = "$DEPS_DIR\windows-deps-x64.zip"
if (-not (Test-Path "$OBS_DEPS\lib\libcurl_imp.lib")) {
    Write-Host "[2/5] Downloading OBS deps..."
    New-Item -ItemType Directory -Force -Path $DEPS_DIR | Out-Null
    $depsUrl = "https://github.com/obsproject/obs-deps/releases/download/2025-08-23/windows-deps-2025-08-23-x64.zip"
    Invoke-WebRequest -Uri $depsUrl -OutFile $depsZip -UseBasicParsing
    Expand-Archive -Path $depsZip -DestinationPath $OBS_DEPS -Force
} else {
    Write-Host "[2/5] OBS deps: OK"
}

# --- 3. Qt6 deps ---
$qt6Zip = "$DEPS_DIR\windows-deps-qt6-x64.zip"
if (-not (Test-Path "$QT6_DIR\include\QtWidgets")) {
    Write-Host "[3/5] Downloading Qt6 deps..."
    New-Item -ItemType Directory -Force -Path $QT6_DIR | Out-Null
    $qt6Url = "https://github.com/obsproject/obs-deps/releases/download/2025-08-23/windows-deps-qt6-2025-08-23-x64.zip"
    Invoke-WebRequest -Uri $qt6Url -OutFile $qt6Zip -UseBasicParsing
    Expand-Archive -Path $qt6Zip -DestinationPath $QT6_DIR -Force
} else {
    Write-Host "[3/5] Qt6 deps: OK"
}

# --- 4. Generate OBS import libraries ---
if (-not (Test-Path "$OBS_LIBS\obs.lib") -or (Get-Item "$OBS_LIBS\obs.lib").Length -lt 10000) {
    Write-Host "[4/5] Generating import libraries..."
    New-Item -ItemType Directory -Force -Path $OBS_LIBS | Out-Null

    foreach ($name in @("obs", "obs-frontend-api", "w32-pthreads")) {
        $raw = (& dumpbin /exports "$OBS_BIN\$name.dll" 2>&1) | Out-String
        $lines = $raw -split "`r?`n"
        $defLines = @("LIBRARY `"$name`"", "EXPORTS")
        $capture = $false
        foreach ($line in $lines) {
            if ($line -match "ordinal\s+hint\s+RVA\s+name") { $capture = $true; continue }
            if ($capture -and $line -match "Summary") { break }
            if ($capture -and $line -match "^\s+(\d+)\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(\S+)") {
                $defLines += "    $($Matches[2])"
            }
        }
        $defLines -join "`n" | Out-File -Encoding ASCII "$OBS_LIBS\$name.def" -NoNewline
        & lib /nologo /def:"$OBS_LIBS\$name.def" /out:"$OBS_LIBS\$name.lib" /machine:x64 2>$null | Out-Null
    }
} else {
    Write-Host "[4/5] Import libraries: OK"
}

# --- 5. Build ---
Write-Host "[5/5] Building..."
$debugFlag = if ($env:DEBUG_BUILD -eq "1") { "-DDEBUG_BUILD=ON" } else { "-DDEBUG_BUILD=OFF" }

cmake -S $ROOT -B $BUILD_DIR -G "Ninja" `
    -DCMAKE_BUILD_TYPE=RelWithDebInfo `
    -DCMAKE_C_COMPILER=cl `
    -DCMAKE_CXX_COMPILER=cl `
    -DOBS_SOURCE_DIR="$OBS_SRC" `
    -DOBS_LIB_DIR="$OBS_LIBS" `
    -DCURL_DIR="$OBS_DEPS" `
    -DQT6_DIR="$QT6_DIR" `
    $debugFlag

cmake --build $BUILD_DIR --config RelWithDebInfo

$dll = "$BUILD_DIR\st-pluginmanager.dll"
if (Test-Path $dll) {
    $size = [math]::Round((Get-Item $dll).Length / 1KB, 1)
    Write-Host ""
    Write-Host "BUILD SUCCESSFUL: st-pluginmanager.dll ($size KB)" -ForegroundColor Green
    Write-Host "Output: $dll"
} else {
    Write-Host "BUILD FAILED" -ForegroundColor Red
    exit 1
}
