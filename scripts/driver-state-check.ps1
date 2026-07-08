[CmdletBinding()]
param()

$ErrorActionPreference = 'Continue'

function Write-Section {
    param([string]$Name)
    Write-Host ""
    Write-Host "=== $Name ==="
}

Write-Section "System"
Get-CimInstance Win32_OperatingSystem |
    Select-Object Caption,Version,BuildNumber,OSArchitecture |
    Format-List

Write-Section "Secure Boot"
try {
    "Confirm-SecureBootUEFI=$(Confirm-SecureBootUEFI)"
} catch {
    "Confirm-SecureBootUEFI=unavailable: $($_.Exception.Message)"
}

Write-Section "TESTSIGNING"
$boot = & bcdedit /enum 2>&1
$testSigning = @($boot | Where-Object { $_ -match 'testsigning' })
if ($testSigning.Count -eq 0) {
    "testsigning entry not present in bcdedit output"
} else {
    $testSigning
}

Write-Section "BitLocker Sanitized"
try {
    Get-BitLockerVolume |
        Select-Object MountPoint,VolumeType,ProtectionStatus,LockStatus,EncryptionPercentage,EncryptionMethod |
        Format-Table -AutoSize
} catch {
    "Get-BitLockerVolume unavailable: $($_.Exception.Message)"
}

Write-Section "Display Adapters"
Get-CimInstance Win32_VideoController |
    Select-Object Name,PNPDeviceID,DriverVersion,CurrentHorizontalResolution,CurrentVerticalResolution,CurrentRefreshRate |
    Format-List

Write-Section "PnP Display"
Get-PnpDevice -Class Display -ErrorAction SilentlyContinue |
    Select-Object Status,Class,FriendlyName,InstanceId |
    Format-Table -AutoSize

Write-Section "PnP Monitor"
Get-PnpDevice -Class Monitor -ErrorAction SilentlyContinue |
    Select-Object Status,Class,FriendlyName,InstanceId |
    Format-Table -AutoSize

Write-Section "FifScreen Devices"
Get-PnpDevice -ErrorAction SilentlyContinue |
    Where-Object {
        $_.FriendlyName -like '*FifScreen*' -or
        $_.InstanceId -like 'ROOT\FIFSCREENIDD\*' -or
        $_.InstanceId -eq 'FifIddDriver'
    } |
    Select-Object Status,Class,FriendlyName,InstanceId |
    Format-Table -AutoSize

Write-Section "FifScreen Driver Store Packages"
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

$records |
    Where-Object {
        [string]$_.'Original Name' -ieq 'FifIddDriver.inf' -or
        [string]$_.'Provider Name' -ieq 'FifScreen' -or
        [string]$_.'Published Name' -like '*Fif*'
    } |
    Format-List

