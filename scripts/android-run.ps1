param(
    [string]$AdbPath
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot

& (Join-Path $PSScriptRoot 'android-device-check.ps1') -AdbPath $AdbPath
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

if (-not $AdbPath) {
    $AdbPath = Join-Path $repoRoot 'tools\android-sdk\platform-tools\adb.exe'
}

$component = 'com.fif.screen/.MainActivity'
Write-Host "[OK] Starting $component"
& $AdbPath shell am start -n $component

