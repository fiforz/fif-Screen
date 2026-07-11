param(
    [ValidateSet('Gui', 'Start', 'Stop', 'Reconnect', 'Status', 'CheckUpdate')]
    [string]$Action = 'Gui',

    [ValidateSet('Usb', 'Lan')]
    [string]$ConnectionMode = 'Usb',

    [string]$PairingPin = ''
)

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$ErrorActionPreference = 'Stop'

function Test-IsAdministrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

if ($Action -eq 'Gui' -and -not (Test-IsAdministrator)) {
    [void][System.Windows.Forms.MessageBox]::Show(
        '请以管理员身份运行 FifScreen 控制中心。',
        '需要管理员权限',
        [System.Windows.Forms.MessageBoxButtons]::OK,
        [System.Windows.Forms.MessageBoxIcon]::Warning
    )
    [Environment]::Exit(1)
}

$RepoRoot = Split-Path -Parent $PSScriptRoot
$VersionPath = Join-Path $RepoRoot 'VERSION'
$AppVersion = if (Test-Path $VersionPath) {
    (Get-Content -LiteralPath $VersionPath -Raw).Trim()
} else {
    'unknown'
}

function Test-ExecutableVersion {
    param(
        [string]$Path,
        [string]$ExpectedVersion
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        return $false
    }
    if ($ExpectedVersion -eq 'unknown') {
        return $true
    }
    try {
        $actual = [version]([Diagnostics.FileVersionInfo]::GetVersionInfo($Path).FileVersion.Trim())
        $expected = [version]$ExpectedVersion
        return $actual.Major -eq $expected.Major -and
            $actual.Minor -eq $expected.Minor -and
            $actual.Build -eq $expected.Build
    } catch {
        return $false
    }
}

function Select-VersionedExecutable {
    param([string[]]$Candidates)

    foreach ($candidate in $Candidates) {
        if (Test-ExecutableVersion -Path $candidate -ExpectedVersion $AppVersion) {
            return $candidate
        }
    }
    return $Candidates[0]
}

$RuntimeRoot = Join-Path $RepoRoot 'runtime'
$InstalledLayout = Test-Path (Join-Path $RuntimeRoot 'bin\fif-host.exe')

if ($InstalledLayout) {
    $AdbPath = Join-Path $RuntimeRoot 'adb\adb.exe'
    $ApkPath = Join-Path $RuntimeRoot 'android\FifScreen.apk'
    $HostPath = Join-Path $RuntimeRoot 'bin\fif-host.exe'
    $LauncherPath = Join-Path $RuntimeRoot 'bin\fif-idd-device-launcher.exe'
    $UpdateScriptPath = Join-Path $RepoRoot 'maintenance\check-update.ps1'
    $ArtifactDir = Join-Path $env:LOCALAPPDATA 'FifScreen\logs'
} else {
    $AdbPath = Join-Path $RepoRoot 'tools\android-sdk\platform-tools\adb.exe'
    $ApkPath = Join-Path $RepoRoot 'android-client\build\outputs\apk\debug\android-client-debug.apk'
    $HostPath = Select-VersionedExecutable -Candidates @(
        (Join-Path $RepoRoot 'build\installer-release\windows-host\fif-host.exe'),
        (Join-Path $RepoRoot "artifacts\installer\stage-$AppVersion-development\runtime\bin\fif-host.exe"),
        (Join-Path $RepoRoot 'build\host\windows-host\fif-host.exe')
    )
    $LauncherPath = Select-VersionedExecutable -Candidates @(
        (Join-Path $RepoRoot 'build\installer-release\windows-driver\FifIddDeviceLauncher\fif-idd-device-launcher.exe'),
        (Join-Path $RepoRoot "artifacts\installer\stage-$AppVersion-development\runtime\bin\fif-idd-device-launcher.exe"),
        (Join-Path $RepoRoot 'build\stage-driver-gate-clean\windows-driver\FifIddDeviceLauncher\fif-idd-device-launcher.exe')
    )
    $UpdateScriptPath = Join-Path $RepoRoot 'packaging\scripts\check-update.ps1'
    $ArtifactDir = Join-Path $RepoRoot 'artifacts\control-panel'
}

$BundledAndroidVersionCode = 0
if ($InstalledLayout) {
    $RuntimeManifestPath = Join-Path $RuntimeRoot 'manifest.json'
    if (Test-Path -LiteralPath $RuntimeManifestPath) {
        try {
            $RuntimeManifest = Get-Content -LiteralPath $RuntimeManifestPath -Raw | ConvertFrom-Json
            $BundledAndroidVersionCode = [int]$RuntimeManifest.android.versionCode
        } catch {}
    }
} else {
    $VersionCodePath = Join-Path $RepoRoot 'VERSION_CODE'
    if (Test-Path -LiteralPath $VersionCodePath) {
        $BundledAndroidVersionCode = [int](Get-Content -LiteralPath $VersionCodePath -Raw).Trim()
    }
}
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

function Start-UpdateCheck {
    param([switch]$Background)

    if (-not (Test-Path -LiteralPath $UpdateScriptPath)) {
        Add-Log "找不到更新程序：$UpdateScriptPath"
        return
    }

    $arguments = @(
        '-NoProfile',
        '-ExecutionPolicy', 'Bypass',
        '-File', $UpdateScriptPath,
        '-InstallDir', $RepoRoot,
        '-CurrentVersion', $AppVersion
    )
    if ($Background) {
        $arguments += @('-Background', '-ParentProcessId', [string]$PID)
    } else {
        Add-Log '正在连接 GitHub 检查更新'
    }

    $psi = [System.Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe"
    $psi.Arguments = (($arguments | ForEach-Object {
        ConvertTo-NativeArgument -Value ([string]$_)
    }) -join ' ')
    $psi.WorkingDirectory = $RepoRoot
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    [void][System.Diagnostics.Process]::Start($psi)
}

function Invoke-UiAction {
    param([scriptblock]$Action)

    try {
        & $Action
    } catch {
        Add-Log ("错误：" + $_.Exception.Message)
    }
}

function Show-UsageTutorial {
    $tutorialForm = [System.Windows.Forms.Form]::new()
    $tutorialForm.Text = 'FifScreen 中文使用教程'
    $tutorialForm.Size = [System.Drawing.Size]::new(720, 620)
    $tutorialForm.StartPosition = 'CenterParent'
    $tutorialForm.MinimumSize = [System.Drawing.Size]::new(620, 500)
    $tutorialForm.Font = [System.Drawing.Font]::new('Microsoft YaHei UI', 9)

    $tutorialText = [System.Windows.Forms.RichTextBox]::new()
    $tutorialText.Dock = 'Fill'
    $tutorialText.ReadOnly = $true
    $tutorialText.BackColor = [System.Drawing.SystemColors]::Window
    $tutorialText.BorderStyle = 'None'
    $tutorialText.Font = [System.Drawing.Font]::new('Microsoft YaHei UI', 10)
    $tutorialText.Text = @"
一、在 Android 设备上开启开发者模式

1. 打开“设置”，进入“关于手机”或“关于平板”。
2. 连续点击“版本号”或“内部版本号”约 7 次。
3. 按系统提示输入锁屏密码，看到“您已处于开发者模式”即可。

常见品牌入口：
• 华为/荣耀：设置 → 关于手机/平板 → 连续点击版本号；然后进入“系统和更新 → 开发人员选项”。
• 小米/红米：设置 → 我的设备 → 全部参数与信息 → 连续点击 MIUI/OS 版本；然后进入“更多设置 → 开发者选项”。
• 三星：设置 → 关于手机 → 软件信息 → 连续点击编译编号；然后返回设置底部进入“开发者选项”。
• 其他设备：在设置中搜索“版本号”“编译编号”或“开发者选项”。

二、使用 USB 调试连接

1. 进入“开发者选项”。
2. 打开“USB 调试”。部分系统还需要打开“仅充电模式下允许 ADB 调试”或“USB 调试（安全设置）”。
3. 使用支持数据传输的 USB 线连接电脑；仅能充电的线无法使用。
4. 手机弹出“是否允许 USB 调试”时，勾选“始终允许使用这台计算机进行调试”，再点击“允许”。

三、使用无线局域网连接

1. 先通过 USB 安装一次 FifScreen Android 应用；之后无线使用时不需要连接数据线。
2. 确保电脑与 Android 设备连接到同一个可信局域网，并关闭会隔离局域网设备的访客 Wi-Fi。
3. 在 Windows 控制中心选择“无线局域网”，输入任意四位数字 PIN，再点击“启动扩展屏”。
4. 在 Android 应用中打开连接设置，选择“无线局域网”，输入与电脑相同的四位 PIN。
5. Android 应用会自动发现局域网内的 FifScreen 主机并完成握手。PIN 不会通过局域网广播，也不会保存到磁盘。

四、启动与切换 FifScreen

1. 选择“USB 调试”时，保持手机解锁并连接电脑。
2. 点击“启动扩展屏”。USB 模式会配置 USB 通道、安装或更新手机端 APK，并启动扩展显示；无线模式会等待同一局域网内的 Android 应用连接。
3. Windows 的“设置 → 系统 → 显示”中可以调整扩展屏的位置、方向和缩放比例。
4. USB 重新插拔或更改无线 PIN 后，点击“重新连接手机”。
5. 不再使用时点击“停止扩展屏”。
6. Android 应用中按系统返回键可重新打开连接方式与 PIN 设置。

五、常见问题

• 显示“未连接”：确认 USB 调试授权没有被拒绝，更换数据线或 USB 接口后重新连接。
• 手机只充电：从 USB 用途通知中选择“文件传输”，再重新授权 USB 调试。
• 无线模式找不到电脑：确认两台设备在同一局域网、PIN 完全一致，并检查路由器是否启用了 AP/客户端隔离。
• 画面卡顿：关闭手机省电模式，避免通过 USB 集线器连接，并保持电脑显卡驱动为较新版本。
• 更换手机：新设备必须重新完成一次 USB 调试授权，然后点击“重新连接手机”。
"@

    $closeButton = [System.Windows.Forms.Button]::new()
    $closeButton.Text = '关闭'
    $closeButton.Dock = 'Bottom'
    $closeButton.Height = 42
    $closeButton.DialogResult = [System.Windows.Forms.DialogResult]::OK

    $tutorialForm.Controls.Add($tutorialText)
    $tutorialForm.Controls.Add($closeButton)
    $tutorialForm.AcceptButton = $closeButton
    [void]$tutorialForm.ShowDialog($form)
}

function Show-AboutFifScreen {
    $message = @"
FifScreen $AppVersion

将 Android 手机或平板作为 Windows 的真实扩展显示器。

项目主页：
https://github.com/fiforz/fif-Screen

当前视频模式：1920 × 1080，目标 50 FPS
"@
    [void][System.Windows.Forms.MessageBox]::Show(
        $form,
        $message,
        '关于 FifScreen',
        [System.Windows.Forms.MessageBoxButtons]::OK,
        [System.Windows.Forms.MessageBoxIcon]::Information
    )
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

function Get-AndroidPackageInfo {
    param([string]$Serial)

    $packagePath = Invoke-Captured -FilePath $AdbPath `
        -Arguments @('-s', $Serial, 'shell', 'pm', 'path', 'com.fif.screen') -TimeoutMs 10000
    $installed = $packagePath.ExitCode -eq 0 -and $packagePath.Output -match 'package:'
    $versionCode = 0

    if ($installed) {
        $version = Invoke-Captured -FilePath $AdbPath `
            -Arguments @('-s', $Serial, 'shell', 'cmd', 'package', 'list', 'packages', '--show-versioncode', 'com.fif.screen') `
            -TimeoutMs 10000
        if (($version.Output + $version.Error) -match 'versionCode:(\d+)') {
            $versionCode = [int]$Matches[1]
        }
    }

    return [pscustomobject]@{
        Installed = $installed
        VersionCode = $versionCode
    }
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

function Get-RequestedConnectionSettings {
    $mode = $ConnectionMode
    $pin = $PairingPin
    if ($Action -eq 'Gui') {
        $mode = if ($lanModeRadio.Checked) { 'Lan' } else { 'Usb' }
        $pin = [string]$pinInput.Text
    }
    $pin = $pin.Trim()
    if ($mode -eq 'Lan' -and $pin -notmatch '^[0-9]{4}$') {
        throw '无线局域网连接需要输入四位数字 PIN。'
    }
    return [pscustomobject]@{ Mode = $mode; Pin = $pin }
}

function Get-HostConnectionMode {
    param([System.Diagnostics.Process]$Process)
    try {
        $commandLine = (Get-CimInstance Win32_Process -Filter "ProcessId=$($Process.Id)" -ErrorAction Stop).CommandLine
        if ($commandLine -match '(?i)--transport\s+lan') {
            return 'Lan'
        }
    } catch {}
    return 'Usb'
}

function Ensure-LanFirewallRules {
    $rules = @(
        @{ Name = 'FifScreen-LAN-Discovery'; DisplayName = 'FifScreen 局域网发现'; Protocol = 'UDP'; Port = '27182' },
        @{ Name = 'FifScreen-LAN-Transport'; DisplayName = 'FifScreen 局域网传输'; Protocol = 'TCP'; Port = '27183-27184' }
    )
    foreach ($rule in $rules) {
        Get-NetFirewallRule -Name $rule.Name -ErrorAction SilentlyContinue |
            Remove-NetFirewallRule -ErrorAction SilentlyContinue
        New-NetFirewallRule `
            -Name $rule.Name `
            -DisplayName $rule.DisplayName `
            -Group 'FifScreen' `
            -Direction Inbound `
            -Action Allow `
            -Enabled True `
            -Profile Any `
            -Protocol $rule.Protocol `
            -LocalPort $rule.Port `
            -RemoteAddress LocalSubnet `
            -Program $HostPath `
            -EdgeTraversalPolicy Block `
            -ErrorAction Stop | Out-Null
    }
    Add-Log '局域网防火墙规则已启用，仅允许本地子网访问'
}

function Ensure-Host {
    param(
        [string]$Serial,
        [ValidateSet('Usb', 'Lan')][string]$Mode,
        [string]$Pin
    )
    $existing = @(Get-Process fif-host -ErrorAction SilentlyContinue)
    if ($existing) {
        $matching = @($existing | Where-Object {
            try {
                Test-ExecutableVersion -Path $_.Path -ExpectedVersion $AppVersion
            } catch {
                $false
            }
        })
        $modeMatching = @($matching | Where-Object {
            (Get-HostConnectionMode -Process $_) -eq $Mode
        })
        if ($modeMatching -and $Mode -eq 'Usb') {
            Add-Log "Windows 主机服务已运行：PID $($modeMatching[0].Id) | 版本 $AppVersion | USB"
            return
        }
        if ($modeMatching -and $Mode -eq 'Lan') {
            Add-Log '正在重启无线主机以应用本次 PIN'
        } elseif ($matching) {
            Add-Log "正在切换 Windows 主机连接方式：$Mode"
        } else {
            Add-Log '检测到旧版 Windows 主机，正在仅重启主机服务'
        }
        $existing | Stop-Process -Force
        for ($attempt = 0; $attempt -lt 20; $attempt++) {
            if (-not (Get-Process fif-host -ErrorAction SilentlyContinue)) {
                break
            }
            Start-Sleep -Milliseconds 100
        }
    }

    if (-not (Test-Path $HostPath)) {
        Add-Log "找不到 Windows 主机程序：$HostPath"
        return
    }
    if (-not (Test-ExecutableVersion -Path $HostPath -ExpectedVersion $AppVersion)) {
        Add-Log "Windows 主机版本与控制中心不匹配：需要 $AppVersion | 路径 $HostPath"
        return
    }

    if ($Mode -eq 'Lan') {
        Ensure-LanFirewallRules
    }

    if ($Mode -eq 'Usb') {
        $env:FIF_ADB = $AdbPath
    } else {
        Remove-Item Env:FIF_ADB -ErrorAction SilentlyContinue
    }
    if ($Mode -eq 'Usb' -and $Serial) {
        $env:FIF_ADB_SERIAL = $Serial
    } else {
        Remove-Item Env:FIF_ADB_SERIAL -ErrorAction SilentlyContinue
    }
    $env:FIF_PAIRING_PIN = if ($Mode -eq 'Lan') { $Pin } else { '' }
    Remove-Item Env:FIF_SHOW_TEST_OVERLAY -ErrorAction SilentlyContinue
    Remove-Item Env:FIF_SAVE_CAPTURE_PROOF -ErrorAction SilentlyContinue
    Remove-Item Env:FIF_VIDEO_WIDTH -ErrorAction SilentlyContinue
    Remove-Item Env:FIF_VIDEO_HEIGHT -ErrorAction SilentlyContinue
    Remove-Item Env:FIF_VIDEO_FPS -ErrorAction SilentlyContinue

    $out = Join-Path $ArtifactDir 'fif-host.out.log'
    $err = Join-Path $ArtifactDir 'fif-host.err.log'
    try {
        $process = Start-Process -FilePath $HostPath `
            -ArgumentList @('--transport', $Mode.ToLowerInvariant()) `
            -WorkingDirectory $RepoRoot `
            -RedirectStandardOutput $out -RedirectStandardError $err `
            -WindowStyle Hidden -PassThru
    } finally {
        Remove-Item Env:FIF_PAIRING_PIN -ErrorAction SilentlyContinue
    }
    Start-Sleep -Milliseconds 700
    if ($process.HasExited) {
        $details = if (Test-Path -LiteralPath $err) {
            (Get-Content -LiteralPath $err -Raw -ErrorAction SilentlyContinue).Trim()
        } else { '' }
        throw "Windows 主机服务启动失败：$details"
    }
    Add-Log "Windows 主机服务已启动：PID $($process.Id) | 连接方式 $Mode"
}

function Ensure-SoftwareDevice {
    $status = Get-LauncherStatus
    if ($status.Owner -and $status.Device) {
        Add-Log '扩展显示设备已运行'
        return
    }
    if (-not $status.Owner -and $status.Device) {
        Add-Log '警告：扩展显示设备存在，但所有者进程未运行；继续使用现有显示节点'
        return
    }
    if ($status.Owner -and -not $status.Device) {
        Add-Log '警告：所有者进程存在，但显示节点缺失；不会重复创建设备'
        return
    }

    Add-Log '正在创建扩展显示设备'
    $out = Join-Path $ArtifactDir 'device-owner.out.log'
    $err = Join-Path $ArtifactDir 'device-owner.err.log'
    $ownerProcess = Start-Process -FilePath $LauncherPath -ArgumentList 'create' -WorkingDirectory $RepoRoot `
        -RedirectStandardOutput $out -RedirectStandardError $err `
        -WindowStyle Hidden -PassThru

    for ($attempt = 0; $attempt -lt 20; $attempt++) {
        Start-Sleep -Milliseconds 500
        $current = Get-LauncherStatus
        if ($current.Owner -and $current.Device) {
            Add-Log "扩展显示设备已上线：所有者 PID $($ownerProcess.Id)"
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
    throw "扩展显示设备启动失败：$details"
}

function Configure-Android {
    param(
        [string]$Serial,
        [bool]$InstallApk = $false,
        [ValidateSet('Usb', 'Lan')][string]$Mode = 'Usb'
    )
    if (-not $Serial) {
        if ($Mode -eq 'Lan') {
            Add-Log '无线主机已就绪；请在同一局域网的 Android 应用中选择无线连接并输入相同 PIN'
        } else {
            Add-Log '未发现已授权 USB 调试的 Android 设备，Windows 主机服务将继续等待'
        }
        return
    }
    Add-Log "Android 设备：$Serial"
    if ($Mode -eq 'Usb') {
        Invoke-Captured -FilePath $AdbPath -Arguments @('-s', $Serial, 'reverse', 'tcp:27183', 'tcp:27183') -TimeoutMs 10000 | Out-Null
        Invoke-Captured -FilePath $AdbPath -Arguments @('-s', $Serial, 'reverse', 'tcp:27184', 'tcp:27184') -TimeoutMs 10000 | Out-Null
        Add-Log 'USB 调试通道已配置'
    } else {
        Add-Log '无线模式不会使用 ADB reverse；USB 仅用于安装或启动 Android 应用'
    }

    if ($InstallApk) {
        if (-not (Test-Path -LiteralPath $ApkPath)) {
            throw "找不到 Android APK：$ApkPath"
        }

        $package = Get-AndroidPackageInfo -Serial $Serial
        $installRequired = -not $package.Installed
        if ($package.Installed -and $BundledAndroidVersionCode -gt 0) {
            if ($package.VersionCode -eq $BundledAndroidVersionCode) {
                Add-Log "Android 应用已是当前版本：versionCode $($package.VersionCode)"
            } elseif ($package.VersionCode -gt $BundledAndroidVersionCode) {
                Add-Log "手机上的 Android 应用版本更新，保留 versionCode $($package.VersionCode)"
            } else {
                $installRequired = $true
                Add-Log "正在更新 Android 应用：versionCode $($package.VersionCode) -> $BundledAndroidVersionCode"
            }
        } elseif ($package.Installed) {
            $installRequired = $true
            Add-Log '无法读取 Android 应用版本，将使用安装包内 APK 覆盖更新'
        }

        if ($installRequired) {
            if (-not $package.Installed) {
                Add-Log '正在安装 Android 应用'
            }
            $install = Invoke-Captured -FilePath $AdbPath `
                -Arguments @('-s', $Serial, 'install', '-r', $ApkPath) -TimeoutMs 120000
            $installText = ($install.Output + $install.Error).Trim()

            if ($install.ExitCode -ne 0 -and $installText -match 'INSTALL_FAILED_UPDATE_INCOMPATIBLE') {
                Add-Log '现有 APK 签名不同，正在替换安装'
                $remove = Invoke-Captured -FilePath $AdbPath `
                    -Arguments @('-s', $Serial, 'uninstall', 'com.fif.screen') -TimeoutMs 30000
                if ($remove.ExitCode -eq 0) {
                    $install = Invoke-Captured -FilePath $AdbPath `
                        -Arguments @('-s', $Serial, 'install', $ApkPath) -TimeoutMs 120000
                    $installText = ($install.Output + $install.Error).Trim()
                }
            }

            if ($install.ExitCode -ne 0) {
                throw "Android 应用安装失败：$installText"
            }
            Add-Log "Android 应用已就绪：versionCode $BundledAndroidVersionCode"
        }
    }

    Invoke-Captured -FilePath $AdbPath -Arguments @('-s', $Serial, 'shell', 'input', 'keyevent', 'WAKEUP') -TimeoutMs 10000 | Out-Null
    Invoke-Captured -FilePath $AdbPath -Arguments @('-s', $Serial, 'shell', 'wm', 'dismiss-keyguard') -TimeoutMs 10000 | Out-Null
    Invoke-Captured -FilePath $AdbPath -Arguments @('-s', $Serial, 'shell', 'am', 'start', '-n', 'com.fif.screen/.MainActivity') -TimeoutMs 15000 | Out-Null
    Add-Log 'Android 应用已启动'
}

function Start-FifScreen {
    $settings = Get-RequestedConnectionSettings
    Add-Log "正在启动扩展屏：连接方式 $($settings.Mode)"
    Ensure-SoftwareDevice
    $serial = Get-AdbSerial
    Ensure-Host -Serial $serial -Mode $settings.Mode -Pin $settings.Pin
    Configure-Android -Serial $serial -InstallApk $true -Mode $settings.Mode
    Refresh-Status
}

function Stop-FifScreen {
    Add-Log '正在停止扩展屏'
    $serial = Get-AdbSerial
    if ($serial) {
        Invoke-Captured -FilePath $AdbPath -Arguments @('-s', $serial, 'shell', 'am', 'force-stop', 'com.fif.screen') -TimeoutMs 10000 | Out-Null
    }
    Get-Process fif-host -ErrorAction SilentlyContinue | Stop-Process -Force
    Add-Log 'Windows 主机服务已停止'

    $status = Get-LauncherStatus
    if ($status.Owner) {
        Add-Log '正在通过所有者进程关闭扩展显示设备'
        $remove = Invoke-Captured -FilePath $LauncherPath -Arguments @('remove') -TimeoutMs 30000
        Add-Log (($remove.Output + $remove.Error).Trim())
    } elseif ($status.Device) {
        Add-Log '警告：显示节点存在但没有所有者进程，不执行不安全的 PnP 删除'
    } else {
        Add-Log '扩展显示设备已经关闭'
    }
    Refresh-Status
}

function Reconnect-Android {
    $settings = Get-RequestedConnectionSettings
    Add-Log "正在重新连接 Android 设备：连接方式 $($settings.Mode)"
    $serial = Get-AdbSerial
    Ensure-Host -Serial $serial -Mode $settings.Mode -Pin $settings.Pin
    Configure-Android -Serial $serial -Mode $settings.Mode
    Refresh-Status
}

function Refresh-Status {
    $serial = Get-AdbSerial
    $hostProcess = Get-Process fif-host -ErrorAction SilentlyContinue
    $launcher = Get-LauncherStatus
    $summary = @()
    $summary += if ($serial) { "ADB：$serial" } else { 'ADB：未连接（无线模式无需 ADB）' }
    $summary += if ($hostProcess) {
        $hostMode = Get-HostConnectionMode -Process $hostProcess[0]
        "主机服务：运行中 PID $($hostProcess[0].Id) / $hostMode"
    } else { '主机服务：已停止' }
    $summary += if ($launcher.Owner) { '设备所有者：运行中' } else { '设备所有者：未运行' }
    $summary += if ($launcher.Device) { '扩展显示：已存在' } else { '扩展显示：不存在' }
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
        'CheckUpdate' { Start-UpdateCheck }
    }
    exit 0
}

$form = [System.Windows.Forms.Form]::new()
$form.Text = "FifScreen 控制中心 $AppVersion - 1080p"
$form.Size = [System.Drawing.Size]::new(860, 650)
$form.MinimumSize = [System.Drawing.Size]::new(860, 610)
$form.StartPosition = 'CenterScreen'
$form.Font = [System.Drawing.Font]::new('Microsoft YaHei UI', 9)

$menuStrip = [System.Windows.Forms.MenuStrip]::new()
$aboutMenu = [System.Windows.Forms.ToolStripMenuItem]::new('关于')
$checkUpdateMenu = [System.Windows.Forms.ToolStripMenuItem]::new('检查更新')
$checkUpdateMenu.Add_Click({ Invoke-UiAction -Action { Start-UpdateCheck } })
$tutorialMenu = [System.Windows.Forms.ToolStripMenuItem]::new('使用教程')
$tutorialMenu.Add_Click({ Invoke-UiAction -Action { Show-UsageTutorial } })
$aboutProductMenu = [System.Windows.Forms.ToolStripMenuItem]::new('关于 FifScreen')
$aboutProductMenu.Add_Click({ Invoke-UiAction -Action { Show-AboutFifScreen } })
[void]$aboutMenu.DropDownItems.Add($checkUpdateMenu)
[void]$aboutMenu.DropDownItems.Add($tutorialMenu)
[void]$aboutMenu.DropDownItems.Add([System.Windows.Forms.ToolStripSeparator]::new())
[void]$aboutMenu.DropDownItems.Add($aboutProductMenu)
[void]$menuStrip.Items.Add($aboutMenu)
$form.MainMenuStrip = $menuStrip
$form.Controls.Add($menuStrip)

$statusLabel = [System.Windows.Forms.Label]::new()
$statusLabel.AutoSize = $false
$statusLabel.Location = [System.Drawing.Point]::new(16, 36)
$statusLabel.Size = [System.Drawing.Size]::new(810, 44)
$statusLabel.Anchor = 'Top, Left, Right'
$statusLabel.Text = '状态：正在读取'
$form.Controls.Add($statusLabel)

$connectionGroup = [System.Windows.Forms.GroupBox]::new()
$connectionGroup.Text = '连接方式'
$connectionGroup.Location = [System.Drawing.Point]::new(16, 80)
$connectionGroup.Size = [System.Drawing.Size]::new(810, 78)
$connectionGroup.Anchor = 'Top, Left, Right'
$form.Controls.Add($connectionGroup)

$usbModeRadio = [System.Windows.Forms.RadioButton]::new()
$usbModeRadio.Text = 'USB 调试'
$usbModeRadio.Location = [System.Drawing.Point]::new(20, 31)
$usbModeRadio.Size = [System.Drawing.Size]::new(120, 28)
$usbModeRadio.Checked = $ConnectionMode -eq 'Usb'
$connectionGroup.Controls.Add($usbModeRadio)

$lanModeRadio = [System.Windows.Forms.RadioButton]::new()
$lanModeRadio.Text = '无线局域网'
$lanModeRadio.Location = [System.Drawing.Point]::new(158, 31)
$lanModeRadio.Size = [System.Drawing.Size]::new(132, 28)
$lanModeRadio.Checked = $ConnectionMode -eq 'Lan'
$connectionGroup.Controls.Add($lanModeRadio)

$pinLabel = [System.Windows.Forms.Label]::new()
$pinLabel.Text = '四位 PIN：'
$pinLabel.Location = [System.Drawing.Point]::new(322, 35)
$pinLabel.AutoSize = $true
$connectionGroup.Controls.Add($pinLabel)

$pinInput = [System.Windows.Forms.MaskedTextBox]::new()
$pinInput.Mask = '0000'
$pinInput.PasswordChar = '*'
$pinInput.AsciiOnly = $true
$pinInput.TextMaskFormat = [System.Windows.Forms.MaskFormat]::ExcludePromptAndLiterals
$pinInput.Text = $PairingPin
$pinInput.Location = [System.Drawing.Point]::new(404, 31)
$pinInput.Size = [System.Drawing.Size]::new(84, 28)
$pinInput.Enabled = $lanModeRadio.Checked
$connectionGroup.Controls.Add($pinInput)

$lanHint = [System.Windows.Forms.Label]::new()
$lanHint.Text = '电脑与 Android 设备需处于同一局域网'
$lanHint.Location = [System.Drawing.Point]::new(512, 35)
$lanHint.AutoSize = $true
$connectionGroup.Controls.Add($lanHint)

$usbModeRadio.Add_CheckedChanged({ $pinInput.Enabled = $lanModeRadio.Checked })
$lanModeRadio.Add_CheckedChanged({
    $pinInput.Enabled = $lanModeRadio.Checked
    if ($lanModeRadio.Checked) { $pinInput.Focus() }
})

$startButton = [System.Windows.Forms.Button]::new()
$startButton.Text = '启动扩展屏'
$startButton.Location = [System.Drawing.Point]::new(16, 174)
$startButton.Size = [System.Drawing.Size]::new(150, 42)
$startButton.Add_Click({ Invoke-UiAction -Action { Start-FifScreen } })
$form.Controls.Add($startButton)

$stopButton = [System.Windows.Forms.Button]::new()
$stopButton.Text = '停止扩展屏'
$stopButton.Location = [System.Drawing.Point]::new(182, 174)
$stopButton.Size = [System.Drawing.Size]::new(150, 42)
$stopButton.Add_Click({ Invoke-UiAction -Action { Stop-FifScreen } })
$form.Controls.Add($stopButton)

$reconnectButton = [System.Windows.Forms.Button]::new()
$reconnectButton.Text = '重新连接手机'
$reconnectButton.Location = [System.Drawing.Point]::new(348, 174)
$reconnectButton.Size = [System.Drawing.Size]::new(150, 42)
$reconnectButton.Add_Click({ Invoke-UiAction -Action { Reconnect-Android } })
$form.Controls.Add($reconnectButton)

$statusButton = [System.Windows.Forms.Button]::new()
$statusButton.Text = '刷新状态'
$statusButton.Location = [System.Drawing.Point]::new(514, 174)
$statusButton.Size = [System.Drawing.Size]::new(150, 42)
$statusButton.Add_Click({ Invoke-UiAction -Action { Refresh-Status } })
$form.Controls.Add($statusButton)

$logBox = [System.Windows.Forms.TextBox]::new()
$logBox.Location = [System.Drawing.Point]::new(16, 232)
$logBox.Size = [System.Drawing.Size]::new(810, 360)
$logBox.Anchor = 'Top, Bottom, Left, Right'
$logBox.Multiline = $true
$logBox.ScrollBars = 'Vertical'
$logBox.ReadOnly = $true
$logBox.Font = [System.Drawing.Font]::new('Consolas', 9)
$form.Controls.Add($logBox)

$form.Add_Shown({
    Invoke-UiAction -Action { Refresh-Status }
    Invoke-UiAction -Action { Start-UpdateCheck -Background }
})
[void]$form.ShowDialog()
