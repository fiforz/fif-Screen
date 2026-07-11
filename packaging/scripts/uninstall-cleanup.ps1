param(
    [Parameter(Mandatory = $true)]
    [string]$InstallDir
)

$ErrorActionPreference = 'Continue'

$registryPath = 'HKLM:\Software\FifScreen'
$runtimeDir = Join-Path $InstallDir 'runtime'
$adbPath = Join-Path $runtimeDir 'adb\adb.exe'
$launcherPath = Join-Path $runtimeDir 'bin\fif-idd-device-launcher.exe'
$stopScript = Join-Path $InstallDir 'maintenance\stop-runtime.ps1'
$logDir = Join-Path $env:ProgramData 'FifScreen\logs'
$logPath = Join-Path $logDir 'uninstall.log'

New-Item -ItemType Directory -Force -Path $logDir | Out-Null

function Write-UninstallLog {
    param([string]$Message)
    $line = '[{0}] {1}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'), $Message
    Add-Content -LiteralPath $logPath -Value $line -Encoding UTF8
    Write-Host $line
}

if (Test-Path -LiteralPath $stopScript) {
    & $stopScript -InstallDir $InstallDir
}

foreach ($firewallRule in @('FifScreen-LAN-Discovery', 'FifScreen-LAN-Transport')) {
    Get-NetFirewallRule -Name $firewallRule -ErrorAction SilentlyContinue |
        Remove-NetFirewallRule -ErrorAction SilentlyContinue
}
Write-UninstallLog 'Removed FifScreen LAN firewall rules'

if (Test-Path -LiteralPath $adbPath) {
    $deviceLines = @(& $adbPath devices 2>&1)
    foreach ($line in $deviceLines) {
        if ($line -match '^(\S+)\s+device\b') {
            $serial = $Matches[1]
            & $adbPath -s $serial reverse --remove tcp:27183 2>&1 | Out-Null
            & $adbPath -s $serial reverse --remove tcp:27184 2>&1 | Out-Null
            & $adbPath -s $serial uninstall com.fif.screen 2>&1 | Out-Null
            Write-UninstallLog "Removed Android app and reverse ports from $serial"
        }
    }
}

$pnpUtil = Join-Path $env:SystemRoot 'System32\pnputil.exe'
& $pnpUtil /remove-device 'SWD\FifScreenIdd\FifIddDriver' 2>&1 | ForEach-Object {
    Write-UninstallLog ([string]$_)
}

$publishedNames = @()
$properties = Get-ItemProperty -Path $registryPath -ErrorAction SilentlyContinue
if ($properties.DriverPublishedName -match '^oem\d+\.inf$') {
    $publishedNames += [string]$properties.DriverPublishedName
}

try {
    $publishedNames += Get-WindowsDriver -Online -All -ErrorAction Stop |
        Where-Object {
            $_.ProviderName -eq 'FifScreen' -and
            $_.OriginalFileName -match '(?i)FifIddDriver\.inf$' -and
            $_.Driver -match '^oem\d+\.inf$'
        } |
        Select-Object -ExpandProperty Driver
} catch {
    Write-UninstallLog "Could not re-query FifScreen driver packages: $($_.Exception.Message)"
}

foreach ($publishedName in @($publishedNames | Select-Object -Unique)) {
    if ($publishedName -notmatch '^oem\d+\.inf$') {
        continue
    }
    & $pnpUtil /delete-driver $publishedName /uninstall /force 2>&1 | ForEach-Object {
        Write-UninstallLog ([string]$_)
    }
}

$ownedRootCertificates = @($properties.InstallerOwnedRootCertificates) |
    Where-Object { $_ -match '^[0-9A-Fa-f]{40}$' }
$ownedPublisherCertificates = @($properties.InstallerOwnedPublisherCertificates) |
    Where-Object { $_ -match '^[0-9A-Fa-f]{40}$' }
$legacyThumbprint = [string]$properties.TestCertificateThumbprint
if ($legacyThumbprint -match '^[0-9A-Fa-f]{40}$') {
    if ([int]$properties.TestCertificateImportedRoot -eq 1) {
        $ownedRootCertificates += $legacyThumbprint
    }
    if ([int]$properties.TestCertificateImportedPublisher -eq 1) {
        $ownedPublisherCertificates += $legacyThumbprint
    }
}

foreach ($thumbprint in @($ownedRootCertificates | Select-Object -Unique)) {
    Remove-Item -LiteralPath "Cert:\LocalMachine\Root\$thumbprint" -Force -ErrorAction SilentlyContinue
    Write-UninstallLog "Removed installer-added Root certificate $thumbprint"
}
foreach ($thumbprint in @($ownedPublisherCertificates | Select-Object -Unique)) {
    Remove-Item -LiteralPath "Cert:\LocalMachine\TrustedPublisher\$thumbprint" -Force -ErrorAction SilentlyContinue
    Write-UninstallLog "Removed installer-added TrustedPublisher certificate $thumbprint"
}

Write-UninstallLog 'FifScreen cleanup completed; Windows boot policy was not changed.'
exit 0
