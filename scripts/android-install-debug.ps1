param(
    [string]$ApkPath,
    [string]$AdbPath
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot

if (-not $ApkPath) {
    $ApkPath = Join-Path $repoRoot 'android-client\build\outputs\apk\debug\android-client-debug.apk'
}

if (-not (Test-Path $ApkPath)) {
    throw "APK not found: $ApkPath"
}

& (Join-Path $PSScriptRoot 'android-device-check.ps1') -AdbPath $AdbPath
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

if (-not $AdbPath) {
    $AdbPath = Join-Path $repoRoot 'tools\android-sdk\platform-tools\adb.exe'
}

Write-Host "[OK] Installing $ApkPath"
& $AdbPath install -r $ApkPath

