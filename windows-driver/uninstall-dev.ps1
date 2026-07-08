param(
    [switch]$ConfirmGate,
    [string]$PublishedName
)

if (-not $ConfirmGate -or -not $PublishedName) {
    Write-Host "Manual gate required before driver removal."
    Write-Host "Find the published name with:"
    Write-Host "  pnputil /enum-drivers"
    Write-Host "Then run:"
    Write-Host "  powershell -ExecutionPolicy Bypass -File windows-driver\uninstall-dev.ps1 -ConfirmGate -PublishedName oemXX.inf"
    exit 2
}

if (-not ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw "Administrator shell required for pnputil driver removal."
}

pnputil /delete-driver $PublishedName /uninstall /force

