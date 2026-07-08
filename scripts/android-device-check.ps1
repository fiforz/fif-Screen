param(
    [string]$AdbPath
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot

if (-not $AdbPath) {
    $candidate = Join-Path $repoRoot 'tools\android-sdk\platform-tools\adb.exe'
    if (Test-Path $candidate) {
        $AdbPath = $candidate
    } elseif ($env:FIF_ADB -and (Test-Path $env:FIF_ADB)) {
        $AdbPath = $env:FIF_ADB
    } else {
        $cmd = Get-Command adb -ErrorAction SilentlyContinue
        if ($cmd) { $AdbPath = $cmd.Source }
    }
}

if (-not $AdbPath -or -not (Test-Path $AdbPath)) {
    Write-Host "[MISSING] adb not found"
    exit 2
}

Write-Host "[OK] adb: $AdbPath"
& $AdbPath version
$devices = & $AdbPath devices -l
$devices | ForEach-Object { Write-Host $_ }

$deviceLines = $devices | Where-Object { $_ -match '^\S+\s+(device|unauthorized|offline)\b' }
if (-not $deviceLines) {
    Write-Host "[HUMAN ACTION] BLOCKED_BY_HUMAN_DEVICE_CONNECTION: no Android device connected"
    exit 3
}

if ($deviceLines | Where-Object { $_ -match '\sunauthorized\b' }) {
    Write-Host "[HUMAN ACTION] Android device is unauthorized. Unlock the phone and accept the USB debugging RSA dialog."
    exit 4
}

if ($deviceLines | Where-Object { $_ -match '\sdevice\b' }) {
    Write-Host "[OK] Android device ready"
    exit 0
}

Write-Host "[WARNING] Device present but not ready"
exit 5

