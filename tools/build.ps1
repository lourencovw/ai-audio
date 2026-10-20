<#
.SYNOPSIS
  Builds the AI Audio Engineer calibration engine core library and its
  unit tests directly with g++ (no CMake required), then runs the tests.

.DESCRIPTION
  Compiles the core engine sources and all test files in one g++
  invocation without requiring CMake. With CMake and a configured
  compiler, use the top-level CMakeLists.txt
  instead (cmake -B build && cmake --build build && ctest --test-dir build).
#>
param(
    [switch]$SkipRun
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

$cxx = (Get-Command g++ -ErrorAction SilentlyContinue).Source
if (-not $cxx) {
    $candidate = Get-ChildItem -Path "$env:LOCALAPPDATA\Microsoft\WinGet\Packages" -Filter "g++.exe" -Recurse -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($candidate) { $cxx = $candidate.FullName }
}
if (-not $cxx) {
    throw "g++ not found. Install a MinGW-w64 toolchain, e.g.: winget install BrechtSanders.WinLibs.POSIX.UCRT"
}

Write-Host "Using compiler: $cxx"

$coreSources = Get-ChildItem "$root\src\core\*.cpp" | ForEach-Object { $_.FullName }
$testSources = Get-ChildItem "$root\tests\*.cpp" | ForEach-Object { $_.FullName }
$outDir = Join-Path $root "build"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$exe = Join-Path $outDir "aiaudio_tests.exe"

# -static avoids depending on MinGW runtime DLLs (libstdc++-6.dll etc.) not
# being on PATH at run time.
$compileArgs = @(
    "-std=c++17", "-Wall", "-Wextra", "-O2", "-static",
    "-I`"$root\src\core`"", "-I`"$root\tests`""
) + $coreSources + $testSources + @("-o", "`"$exe`"")

& $cxx @compileArgs
if ($LASTEXITCODE -ne 0) { throw "Build failed." }
Write-Host "Build succeeded: $exe"

if (-not $SkipRun) {
    & $exe
    exit $LASTEXITCODE
}
