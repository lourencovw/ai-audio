<#
.SYNOPSIS
  Runs common project commands. Use .\run help to list commands.
#>
param(
    [ValidateSet('help', 'build', 'test', 'plugin', 'deploy', 'clean')]
    [string]$Command = 'help',
    [string]$ObsInstallDir = 'C:\Program Files\obs-studio'
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot

try {
    switch ($Command) {
        'help' {
            Write-Host @'
Usage: .\run <command>

  build   Compile the core engine and tests without running tests
  test    Compile the core engine and run all tests
  plugin  Build the OBS plugin
  deploy  Build and install the OBS plugin (close OBS first)
  clean   Remove build output, including downloaded OBS headers
  help    Show this command list

For plugin/deploy, optionally pass -ObsInstallDir "D:\Apps\obs-studio".
'@
        }
        'build' { & "$PSScriptRoot\build.ps1" -SkipRun }
        'test' { & "$PSScriptRoot\build.ps1" }
        'plugin' { & "$PSScriptRoot\build-obs-plugin.ps1" -ObsInstallDir $ObsInstallDir }
        'deploy' { & "$PSScriptRoot\build-obs-plugin.ps1" -ObsInstallDir $ObsInstallDir -Deploy }
        'clean' {
            $buildPath = [IO.Path]::GetFullPath((Join-Path $projectRoot 'build'))
            $expectedPath = [IO.Path]::Combine([IO.Path]::GetFullPath($projectRoot), 'build')
            if ($buildPath -ne $expectedPath) { throw 'Unexpected build directory.' }
            if (Test-Path -LiteralPath $buildPath) {
                $buildItem = Get-Item -LiteralPath $buildPath -Force
                if ($buildItem.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                    throw 'Refusing to clean a build directory that is a link.'
                }
                Remove-Item -LiteralPath $buildPath -Recurse -Force
            }
            Write-Host 'Build output removed.'
        }
    }
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
} catch {
    Write-Error $_
    exit 1
}
