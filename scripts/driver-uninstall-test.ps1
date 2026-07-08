[CmdletBinding()]
param(
    [switch]$ConfirmGate,
    [string]$PublishedName
)

$ErrorActionPreference = 'Stop'

function Test-IsAdministrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal] $identity
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Get-PnpDriverRecords {
    $records = New-Object System.Collections.Generic.List[object]
    $current = $null

    foreach ($line in (& pnputil /enum-drivers 2>&1)) {
        if ($line -match '^\s*$') {
            if ($current) {
                $records.Add([pscustomobject]$current)
                $current = $null
            }
            continue
        }

        if ($line -match '^\s*([^:]+):\s*(.*)$') {
            $key = $matches[1].Trim()
            $value = $matches[2].Trim()
            if ($key -eq 'Published Name') {
                if ($current) {
                    $records.Add([pscustomobject]$current)
                }
                $current = [ordered]@{}
            }
            if ($current) {
                $current[$key] = $value
            }
        }
    }

    if ($current) {
        $records.Add([pscustomobject]$current)
    }

    return $records
}

function Get-FifScreenDevices {
    Get-PnpDevice -ErrorAction SilentlyContinue |
        Where-Object {
            $_.FriendlyName -eq 'FifScreen Indirect Display' -or
            $_.InstanceId -like 'ROOT\FIFSCREENIDD\*' -or
            $_.InstanceId -eq 'FifIddDriver'
        }
}

Write-Host "FifScreen driver uninstall test gate"
Write-Host "Administrator: $(Test-IsAdministrator)"
Write-Host "This script deletes only exact FifScreen device/package identifiers."

if (-not $PublishedName) {
    Write-Host "PublishedName is required. Use scripts\driver-state-check.ps1 or pnputil /enum-drivers to find the exact oemXX.inf."
    Write-Host "No driver modifications executed."
    exit 2
}

if ($PublishedName -notmatch '^oem\d+\.inf$') {
    throw "PublishedName must be an exact OEM INF name such as oem42.inf. Wildcards are not allowed."
}

$record = Get-PnpDriverRecords |
    Where-Object { $_.'Published Name' -ieq $PublishedName } |
    Select-Object -First 1

if (-not $record) {
    throw "Driver package not found in Driver Store: $PublishedName"
}

$originalName = [string]$record.'Original Name'
$providerName = [string]$record.'Provider Name'
Write-Host "Matched package:"
$record | Format-List

if ($originalName -ine 'FifIddDriver.inf' -or $providerName -ine 'FifScreen') {
    throw "Refusing to remove $PublishedName because it is not the exact FifIddDriver package."
}

$devices = @(Get-FifScreenDevices)
if ($devices.Count -eq 0) {
    Write-Host "No current FifScreen PnP device instance found."
} else {
    Write-Host "Exact FifScreen device instances planned for removal:"
    $devices | Select-Object Status,Class,FriendlyName,InstanceId | Format-Table -AutoSize
}

Write-Host "Planned package command:"
Write-Host "  pnputil /delete-driver $PublishedName /uninstall /force"
Write-Host "Planned rescan command:"
Write-Host "  pnputil /scan-devices"

if (-not $ConfirmGate) {
    Write-Host "No driver modifications executed. Re-run only after explicit approval:"
    Write-Host "  powershell -ExecutionPolicy Bypass -File scripts\driver-uninstall-test.ps1 -ConfirmGate -PublishedName $PublishedName"
    exit 2
}

if (-not (Test-IsAdministrator)) {
    throw "Administrator shell required for driver removal."
}

foreach ($device in $devices) {
    Write-Host "Removing exact device: $($device.InstanceId)"
    & pnputil /remove-device $device.InstanceId
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to remove device $($device.InstanceId)"
    }
}

& pnputil /delete-driver $PublishedName /uninstall /force
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

& pnputil /scan-devices
exit $LASTEXITCODE

