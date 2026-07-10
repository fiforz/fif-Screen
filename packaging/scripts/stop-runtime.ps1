param(
    [Parameter(Mandatory = $true)]
    [string]$InstallDir
)

$ErrorActionPreference = 'Continue'

$launcherPath = Join-Path $InstallDir 'runtime\bin\fif-idd-device-launcher.exe'
if (Test-Path -LiteralPath $launcherPath) {
    $status = (& $launcherPath status 2>&1) -join "`n"
    if ($status -match 'owner_running=true') {
        & $launcherPath remove 2>&1 | Out-Null
    }
}

$normalizedInstallDir = [IO.Path]::GetFullPath($InstallDir).TrimEnd('\') + '\'
Get-CimInstance Win32_Process -ErrorAction SilentlyContinue |
    Where-Object {
        $_.Name -in @('fif-host.exe', 'fif-idd-device-launcher.exe', 'adb.exe') -and
        $_.ExecutablePath -and
        [IO.Path]::GetFullPath($_.ExecutablePath).StartsWith($normalizedInstallDir, [StringComparison]::OrdinalIgnoreCase)
    } |
    ForEach-Object { Invoke-CimMethod -InputObject $_ -MethodName Terminate -ErrorAction SilentlyContinue | Out-Null }

Get-CimInstance Win32_Process -Filter "Name='powershell.exe'" -ErrorAction SilentlyContinue |
    Where-Object {
        $_.CommandLine -and
        $_.CommandLine -like '*fifscreen-control.ps1*' -and
        $_.CommandLine -like "*$InstallDir*"
    } |
    ForEach-Object { Invoke-CimMethod -InputObject $_ -MethodName Terminate -ErrorAction SilentlyContinue | Out-Null }

exit 0
