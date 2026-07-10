param(
    [ValidateSet('Gui', 'Start', 'Stop', 'Reconnect', 'Status')]
    [string]$Action = 'Gui'
)

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot
$AdbPath = Join-Path $RepoRoot 'tools\android-sdk\platform-tools\adb.exe'
$ApkPath = Join-Path $RepoRoot 'android-client\build\outputs\apk\debug\android-client-debug.apk'
$HostPath = Join-Path $RepoRoot 'build\host\windows-host\fif-host.exe'
$LauncherPath = Join-Path $RepoRoot 'build\stage-driver-gate-clean\windows-driver\FifIddDeviceLauncher\fif-idd-device-launcher.exe'
$ArtifactDir = Join-Path $RepoRoot 'artifacts\control-panel'
New-Item -ItemType Directory -Force -Path $ArtifactDir | Out-Null
$ControlLogPath = Join-Path $ArtifactDir 'control.log'

function Add-Log {
    param([string]$Message)
    $time = Get-Date -Format 'HH:mm:ss'
    $line = "[$time] $Message"
    try {
        Add-Content -LiteralPath $ControlLogPath -Value $line -Encoding UTF8
    } catch {}

    if ($null -ne $logBox -and -not $logBox.IsDisposed) {
        $logBox.AppendText("$line`r`n")
        $logBox.SelectionStart = $logBox.TextLength
        $logBox.ScrollToCaret()
    } else {
        Write-Host $line
    }
}

function ConvertTo-NativeArgument {
    param([AllowEmptyString()][string]$Value)

    if ($null -eq $Value -or $Value.Length -eq 0) {
        return '""'
    }
    if ($Value -notmatch '[\s"]') {
        return $Value
    }

    $escaped = $Value -replace '(\\*)"', '$1$1\"'
    $escaped = $escaped -replace '(\\+)$', '$1$1'
    return '"' + $escaped + '"'
}

function Invoke-Captured {
    param(
        [string]$FilePath,
        [string[]]$Arguments = @(),
        [int]$TimeoutMs = 30000
    )
    $psi = [System.Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = $FilePath
    $psi.Arguments = (($Arguments | ForEach-Object {
        ConvertTo-NativeArgument -Value ([string]$_)
    }) -join ' ')
    $psi.WorkingDirectory = $RepoRoot
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.CreateNoWindow = $true

    $process = [System.Diagnostics.Process]::Start($psi)
    if (-not $process.WaitForExit($TimeoutMs)) {
        try { $process.Kill() } catch {}
        return [pscustomobject]@{ ExitCode = 124; Output = ''; Error = 'timeout' }
    }

    return [pscustomobject]@{
        ExitCode = $process.ExitCode
        Output = $process.StandardOutput.ReadToEnd()
        Error = $process.StandardError.ReadToEnd()
    }
}

function Invoke-UiAction {
    param([scriptblock]$Action)

    try {
        & $Action
    } catch {
        Add-Log ("error: " + $_.Exception.Message)
    }
}

function Get-AdbDevices {
    if (-not (Test-Path $AdbPath)) {
        return @()
    }
    $result = Invoke-Captured -FilePath $AdbPath -Arguments @('devices', '-l') -TimeoutMs 10000
    if ($result.ExitCode -ne 0) {
        return @()
    }
    return @($result.Output -split "`r?`n" | Where-Object { $_ -match '^\S+\s+device\b' })
}

function Get-AdbSerial {
    $devices = @(Get-AdbDevices)
    if ($devices.Count -eq 0) {
        return $null
    }
    return (($devices[0] -split '\s+')[0])
}

function Get-LauncherStatus {
    if (-not (Test-Path $LauncherPath)) {
        return [pscustomobject]@{ Raw = "launcher missing: $LauncherPath"; Owner = $false; Device = $false }
    }
    $result = Invoke-Captured -FilePath $LauncherPath -Arguments @('status') -TimeoutMs 30000
    $raw = (($result.Output + $result.Error).Trim())
    return [pscustomobject]@{
        Raw = $raw
        Owner = $raw -match 'owner_running=true'
        Device = $raw -match 'fifscreen_software_device_present=true'
    }
}

function Ensure-Host {
    param([string]$Serial)
    $existing = Get-Process fif-host -ErrorAction SilentlyContinue
    if ($existing) {
        Add-Log "host already running: PID $($existing[0].Id)"
        return
    }

    if (-not (Test-Path $HostPath)) {
        Add-Log "host missing: $HostPath"
        return
    }

    $env:FIF_ADB = $AdbPath
    if ($Serial) {
        $env:FIF_ADB_SERIAL = $Serial
    }
    Remove-Item Env:FIF_SHOW_TEST_OVERLAY -ErrorAction SilentlyContinue
    Remove-Item Env:FIF_SAVE_CAPTURE_PROOF -ErrorAction SilentlyContinue
    Remove-Item Env:FIF_VIDEO_WIDTH -ErrorAction SilentlyContinue
    Remove-Item Env:FIF_VIDEO_HEIGHT -ErrorAction SilentlyContinue
    Remove-Item Env:FIF_VIDEO_FPS -ErrorAction SilentlyContinue

    $out = Join-Path $ArtifactDir 'fif-host.out.log'
    $err = Join-Path $ArtifactDir 'fif-host.err.log'
    $process = Start-Process -FilePath $HostPath -WorkingDirectory $RepoRoot `
        -RedirectStandardOutput $out -RedirectStandardError $err `
        -WindowStyle Hidden -PassThru
    Start-Sleep -Milliseconds 700
    Add-Log "host started: PID $($process.Id)"
}

function Ensure-SoftwareDevice {
    $status = Get-LauncherStatus
    if ($status.Owner -and $status.Device) {
        Add-Log 'extension device owner is running'
        return
    }
    if (-not $status.Owner -and $status.Device) {
        Add-Log 'warning: extension device exists but owner is not running; using existing display node'
        return
    }
    if ($status.Owner -and -not $status.Device) {
        Add-Log 'warning: owner exists but display node is missing; not creating duplicate'
        return
    }

    Add-Log 'creating extension display owner process'
    $out = Join-Path $ArtifactDir 'device-owner.out.log'
    $err = Join-Path $ArtifactDir 'device-owner.err.log'
    $ownerProcess = Start-Process -FilePath $LauncherPath -ArgumentList 'create' -WorkingDirectory $RepoRoot `
        -RedirectStandardOutput $out -RedirectStandardError $err `
        -WindowStyle Hidden -PassThru

    for ($attempt = 0; $attempt -lt 20; $attempt++) {
        Start-Sleep -Milliseconds 500
        $current = Get-LauncherStatus
        if ($current.Owner -and $current.Device) {
            Add-Log "extension display online: owner PID $($ownerProcess.Id)"
            return
        }
        if ($ownerProcess.HasExited) {
            break
        }
    }

    $final = Get-LauncherStatus
    $details = $final.Raw
    if (Test-Path $err) {
        $details = ($details + "`n" + (Get-Content -LiteralPath $err -Raw -ErrorAction SilentlyContinue)).Trim()
    }
    throw "extension display failed to start: $details"
}

function Configure-Android {
    param(
        [string]$Serial,
        [bool]$InstallApk = $false
    )
    if (-not $Serial) {
        Add-Log 'Android device not found; host will keep waiting'
        return
    }
    Add-Log "Android device: $Serial"
    Invoke-Captured -FilePath $AdbPath -Arguments @('-s', $Serial, 'reverse', 'tcp:27183', 'tcp:27183') -TimeoutMs 10000 | Out-Null
    Invoke-Captured -FilePath $AdbPath -Arguments @('-s', $Serial, 'reverse', 'tcp:27184', 'tcp:27184') -TimeoutMs 10000 | Out-Null

    if ($InstallApk) {
        $package = Invoke-Captured -FilePath $AdbPath `
            -Arguments @('-s', $Serial, 'shell', 'pm', 'path', 'com.fif.screen') -TimeoutMs 10000
        $packageInstalled = $package.ExitCode -eq 0 -and $package.Output -match 'package:'
        if (-not $packageInstalled -and (Test-Path $ApkPath)) {
            Add-Log 'installing Android app'
            $install = Invoke-Captured -FilePath $AdbPath -Arguments @('-s', $Serial, 'install', '-r', $ApkPath) -TimeoutMs 120000
            if ($install.ExitCode -ne 0) {
                Add-Log ("install failed: " + ($install.Output + $install.Error).Trim())
            }
        } elseif ($packageInstalled) {
            Add-Log 'Android app already installed'
        }
    }

    Invoke-Captured -FilePath $AdbPath -Arguments @('-s', $Serial, 'shell', 'input', 'keyevent', 'WAKEUP') -TimeoutMs 10000 | Out-Null
    Invoke-Captured -FilePath $AdbPath -Arguments @('-s', $Serial, 'shell', 'wm', 'dismiss-keyguard') -TimeoutMs 10000 | Out-Null
    Invoke-Captured -FilePath $AdbPath -Arguments @('-s', $Serial, 'shell', 'am', 'start', '-n', 'com.fif.screen/.MainActivity') -TimeoutMs 15000 | Out-Null
    Add-Log 'Android app launched'
}

function Start-FifScreen {
    Add-Log 'starting extension screen'
    Ensure-SoftwareDevice
    $serial = Get-AdbSerial
    Ensure-Host -Serial $serial
    Configure-Android -Serial $serial -InstallApk $true
    Refresh-Status
}

function Stop-FifScreen {
    Add-Log 'stopping extension screen'
    $serial = Get-AdbSerial
    if ($serial) {
        Invoke-Captured -FilePath $AdbPath -Arguments @('-s', $serial, 'shell', 'am', 'force-stop', 'com.fif.screen') -TimeoutMs 10000 | Out-Null
    }
    Get-Process fif-host -ErrorAction SilentlyContinue | Stop-Process -Force
    Add-Log 'host stopped'

    $status = Get-LauncherStatus
    if ($status.Owner) {
        Add-Log 'removing extension display through owner stop signal'
        $remove = Invoke-Captured -FilePath $LauncherPath -Arguments @('remove') -TimeoutMs 30000
        Add-Log (($remove.Output + $remove.Error).Trim())
    } elseif ($status.Device) {
        Add-Log 'warning: display node exists without owner; not doing unsafe PnP removal'
    } else {
        Add-Log 'extension display already off'
    }
    Refresh-Status
}

function Reconnect-Android {
    Add-Log 'reconnecting Android'
    $serial = Get-AdbSerial
    Ensure-Host -Serial $serial
    Configure-Android -Serial $serial
    Refresh-Status
}

function Refresh-Status {
    $serial = Get-AdbSerial
    $hostProcess = Get-Process fif-host -ErrorAction SilentlyContinue
    $launcher = Get-LauncherStatus
    $summary = @()
    $summary += if ($serial) { "Android: $serial" } else { 'Android: not connected' }
    $summary += if ($hostProcess) { "Host: running PID $($hostProcess[0].Id)" } else { 'Host: stopped' }
    $summary += if ($launcher.Owner) { 'Owner: running' } else { 'Owner: not running' }
    $summary += if ($launcher.Device) { 'Display node: present' } else { 'Display node: absent' }
    if ($null -ne $statusLabel -and -not $statusLabel.IsDisposed) {
        $statusLabel.Text = ($summary -join '    ')
    }
    Add-Log ($summary -join ' | ')
}

if ($Action -ne 'Gui') {
    switch ($Action) {
        'Start' { Start-FifScreen }
        'Stop' { Stop-FifScreen }
        'Reconnect' { Reconnect-Android }
        'Status' { Refresh-Status }
    }
    exit 0
}

$form = [System.Windows.Forms.Form]::new()
$form.Text = 'FifScreen Control - 1080p Ready'
$form.Size = [System.Drawing.Size]::new(820, 520)
$form.StartPosition = 'CenterScreen'

$statusLabel = [System.Windows.Forms.Label]::new()
$statusLabel.AutoSize = $false
$statusLabel.Location = [System.Drawing.Point]::new(16, 16)
$statusLabel.Size = [System.Drawing.Size]::new(770, 44)
$statusLabel.Text = 'Status: loading'
$form.Controls.Add($statusLabel)

$startButton = [System.Windows.Forms.Button]::new()
$startButton.Text = 'Start Display'
$startButton.Location = [System.Drawing.Point]::new(16, 72)
$startButton.Size = [System.Drawing.Size]::new(150, 42)
$startButton.Add_Click({ Invoke-UiAction -Action { Start-FifScreen } })
$form.Controls.Add($startButton)

$stopButton = [System.Windows.Forms.Button]::new()
$stopButton.Text = 'Stop Display'
$stopButton.Location = [System.Drawing.Point]::new(182, 72)
$stopButton.Size = [System.Drawing.Size]::new(150, 42)
$stopButton.Add_Click({ Invoke-UiAction -Action { Stop-FifScreen } })
$form.Controls.Add($stopButton)

$reconnectButton = [System.Windows.Forms.Button]::new()
$reconnectButton.Text = 'Reconnect Phone'
$reconnectButton.Location = [System.Drawing.Point]::new(348, 72)
$reconnectButton.Size = [System.Drawing.Size]::new(150, 42)
$reconnectButton.Add_Click({ Invoke-UiAction -Action { Reconnect-Android } })
$form.Controls.Add($reconnectButton)

$statusButton = [System.Windows.Forms.Button]::new()
$statusButton.Text = 'Refresh Status'
$statusButton.Location = [System.Drawing.Point]::new(514, 72)
$statusButton.Size = [System.Drawing.Size]::new(150, 42)
$statusButton.Add_Click({ Invoke-UiAction -Action { Refresh-Status } })
$form.Controls.Add($statusButton)

$logBox = [System.Windows.Forms.TextBox]::new()
$logBox.Location = [System.Drawing.Point]::new(16, 130)
$logBox.Size = [System.Drawing.Size]::new(770, 330)
$logBox.Multiline = $true
$logBox.ScrollBars = 'Vertical'
$logBox.ReadOnly = $true
$logBox.Font = [System.Drawing.Font]::new('Consolas', 9)
$form.Controls.Add($logBox)

$form.Add_Shown({ Invoke-UiAction -Action { Refresh-Status } })
[void]$form.ShowDialog()
