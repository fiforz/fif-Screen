param(
    [switch]$ConfirmGate
)

$driverInf = Join-Path $PSScriptRoot 'FifIddDriver\FifIddDriver.inf'

if (-not $ConfirmGate) {
    Write-Host "Manual gate required before driver install."
    Write-Host "Why: installing a display driver changes system driver state and may require test signing."
    Write-Host "Command to run after explicit approval:"
    Write-Host "  powershell -ExecutionPolicy Bypass -File windows-driver\install-dev.ps1 -ConfirmGate"
    Write-Host "This script does not enable testsigning, install certificates, or reboot."
    exit 2
}

if (-not ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw "Administrator shell required for pnputil driver installation."
}

pnputil /add-driver $driverInf /install

