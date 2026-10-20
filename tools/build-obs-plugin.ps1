<#
.SYNOPSIS
  Builds the AI Audio Engineer OBS Studio plugin DLL against a real,
  locally-installed OBS Studio, without needing the full obs-studio
  CMake/Qt/ffmpeg build.

.DESCRIPTION
  Requires an installed 64-bit OBS Studio and a MinGW-w64 toolchain.
  This script obtains the headers and import library needed to build:

   1. Sparse git-clones just libobs/ (+ plugins/obs-filters/ for
      reference) from the obsproject/obs-studio tag matching the
      installed OBS version -- giving exact, ABI-matching headers.
   2. Generates a MinGW import library (libobs.a) directly from the
      installed obs.dll via gendef + dlltool (no need to build obs-studio
      from source to get obs.lib/libobs.a).
   3. Writes a minimal obsconfig.h stub (normally CMake-generated; only
      the couple of macros obs-config.h actually needs are required here).
   4. Compiles src/core + src/obs-plugin and links ai-audio-engineer.dll
      fully statically (no MinGW runtime DLL dependencies -- only obs.dll
      and the OS-provided Universal CRT are required at load time).
   5. Optionally deploys it to the per-user/per-machine OBS plugin
      directory (%ProgramData%\obs-studio\plugins), which does not
      require admin rights, per OBS's own plugin install docs.

.PARAMETER ObsInstallDir
  Path to the installed OBS Studio (default: the standard winget/installer
  location).

.PARAMETER Deploy
  If set, copies the built DLL (+ a placeholder locale file) into
  %ProgramData%\obs-studio\plugins\ai-audio-engineer so OBS picks it up
  on next launch.
#>
param(
    [string]$ObsInstallDir = "C:\Program Files\obs-studio",
    [switch]$Deploy
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

function Get-GppPath {
    $cxx = (Get-Command g++ -ErrorAction SilentlyContinue).Source
    if ($cxx) { return $cxx }
    $candidate = Get-ChildItem -Path "$env:LOCALAPPDATA\Microsoft\WinGet\Packages" -Filter "g++.exe" -Recurse -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($candidate) { return $candidate.FullName }
    throw "g++ not found. Install a MinGW-w64 toolchain, e.g.: winget install BrechtSanders.WinLibs.POSIX.UCRT"
}

function Get-MingwBinDir([string]$gppPath) { return Split-Path -Parent $gppPath }

$gpp = Get-GppPath
$mingwBin = Get-MingwBinDir $gpp
Write-Host "Using compiler: $gpp"

$obsDll = Join-Path $ObsInstallDir "bin\64bit\obs.dll"
if (-not (Test-Path $obsDll)) { throw "obs.dll not found at '$obsDll'. Pass -ObsInstallDir if OBS is installed elsewhere." }

$obsVersionInfo = (Get-Item $obsDll).VersionInfo
$obsVersion = $obsVersionInfo.ProductVersion
if (-not $obsVersion) { $obsVersion = $obsVersionInfo.FileVersion }
$obsTag = ($obsVersion -split "[^0-9.]")[0].TrimEnd('.')
Write-Host "Detected OBS Studio version: $obsVersion (using git tag '$obsTag')"

$sdkDir = Join-Path $root "build\obs-sdk"
$srcDir = Join-Path $sdkDir "obs-studio-src"
$libobsInc = Join-Path $srcDir "libobs"
New-Item -ItemType Directory -Force -Path $sdkDir | Out-Null

if (-not (Test-Path (Join-Path $libobsInc "obs-module.h"))) {
    Write-Host "Fetching libobs headers for tag $obsTag (sparse checkout, no full repo clone)..."
    if (-not (Test-Path $srcDir)) {
        git clone --depth 1 --branch $obsTag --filter=blob:none --sparse https://github.com/obsproject/obs-studio.git $srcDir
    }
    Push-Location $srcDir
    try {
        git sparse-checkout set libobs plugins/obs-filters
    } finally {
        Pop-Location
    }
}

$obsconfigPath = Join-Path $libobsInc "obsconfig.h"
if (-not (Test-Path $obsconfigPath)) {
    Write-Host "Writing obsconfig.h stub (normally CMake-generated)..."
    @"
// Minimal stand-in for the CMake-generated obsconfig.h from an obs-studio
// build tree. Only defines the macros actually needed to compile this
// plugin's translation units against the real libobs/obs-filters headers.
#pragma once
#define OBS_INSTALL_PREFIX ""
#define OBS_DATA_PATH "data"
#define OBS_VERSION "$obsVersion"
#define OBS_VERSION_CANONICAL "$obsVersion"
"@ | Set-Content -Path $obsconfigPath
}

Write-Host "Generating MinGW import library from the installed obs.dll..."
Push-Location $sdkDir
try {
    & "$mingwBin\gendef.exe" $obsDll | Out-Null
    & "$mingwBin\dlltool.exe" -d (Join-Path $sdkDir "obs.def") -D "obs.dll" -l (Join-Path $sdkDir "libobs.a")
} finally {
    Pop-Location
}

$pluginBuildDir = Join-Path $root "build\obs-plugin"
New-Item -ItemType Directory -Force -Path $pluginBuildDir | Out-Null

$coreSources = Get-ChildItem (Join-Path $root "src\core\*.cpp") | ForEach-Object { $_.FullName }
$pluginSources = @(
    Join-Path $root "src\obs-plugin\calibration-filter.cpp"
    Join-Path $root "src\obs-plugin\plugin-main.cpp"
)

Write-Host "Compiling plugin + core sources..."
$objFiles = @()
foreach ($src in $pluginSources) {
    $obj = Join-Path $pluginBuildDir ((Get-Item $src).BaseName + ".o")
    & $gpp -std=c++17 -Wall -Wextra -O2 -I"$libobsInc" -I"$root\src\core" -c $src -o $obj
    if ($LASTEXITCODE -ne 0) { throw "Failed to compile $src" }
    $objFiles += $obj
}

$dllPath = Join-Path $pluginBuildDir "ai-audio-engineer.dll"
Write-Host "Linking $dllPath ..."
& $gpp -std=c++17 -Wall -Wextra -O2 -shared -static $objFiles $coreSources `
    -I"$root\src\core" -L"$sdkDir" -lobs -o $dllPath
if ($LASTEXITCODE -ne 0) { throw "Link failed." }
Write-Host "Built: $dllPath"

if ($Deploy) {
    $pluginRoot = Join-Path $env:ProgramData "obs-studio\plugins\ai-audio-engineer"
    New-Item -ItemType Directory -Force -Path (Join-Path $pluginRoot "bin\64bit") | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $pluginRoot "data\locale") | Out-Null
    Copy-Item $dllPath (Join-Path $pluginRoot "bin\64bit\ai-audio-engineer.dll") -Force
    $localePath = Join-Path $pluginRoot "data\locale\en-US.ini"
    if (-not (Test-Path $localePath)) {
        "# AI Audio Engineer locale (placeholder; plugin uses plain strings/blog() for now)" | Set-Content $localePath
    }
    Write-Host "Deployed to: $pluginRoot"
    Write-Host "Restart OBS Studio for it to appear as a filter type."
}
