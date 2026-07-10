[CmdletBinding()]
param(
    [string]$InstallDir = (Split-Path -Parent $PSScriptRoot),
    [string]$ReleaseApiUrl = '',
    [string]$CurrentVersion = '',
    [switch]$Background,
    [int]$ParentProcessId = 0,
    [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms

$DefaultReleaseApiUrl = 'https://api.github.com/repos/fiforz/fif-Screen/releases/latest'
$UpdateLogDir = Join-Path $env:LOCALAPPDATA 'FifScreen\logs'
$UpdateLogPath = Join-Path $UpdateLogDir 'update.log'
New-Item -ItemType Directory -Force -Path $UpdateLogDir | Out-Null

function Write-UpdateLog {
    param([string]$Message)

    try {
        Add-Content -LiteralPath $UpdateLogPath `
            -Value "[$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')] $Message" -Encoding UTF8
    } catch {}
}

function Show-UpdateMessage {
    param(
        [string]$Text,
        [string]$Title = 'FifScreen 更新',
        [System.Windows.Forms.MessageBoxIcon]$Icon = [System.Windows.Forms.MessageBoxIcon]::Information
    )

    [void][System.Windows.Forms.MessageBox]::Show(
        $Text,
        $Title,
        [System.Windows.Forms.MessageBoxButtons]::OK,
        $Icon
    )
}

function Test-ParentProcess {
    if ($ParentProcessId -le 0) {
        return $true
    }
    return $null -ne (Get-Process -Id $ParentProcessId -ErrorAction SilentlyContinue)
}

function Wait-ForRetry {
    param([int]$Seconds)

    for ($elapsed = 0; $elapsed -lt $Seconds; $elapsed += 5) {
        if (-not (Test-ParentProcess)) {
            return $false
        }
        Start-Sleep -Seconds ([Math]::Min(5, $Seconds - $elapsed))
    }
    return $true
}

function Assert-HttpsUri {
    param(
        [string]$Value,
        [string[]]$AllowedHosts,
        [string]$Description
    )

    $uri = [Uri]$Value
    if (-not $uri.IsAbsoluteUri -or $uri.Scheme -ne 'https' -or
        $uri.Host -notin $AllowedHosts) {
        throw "$Description 必须使用受信任的 HTTPS 地址。"
    }
    return $uri
}

function Get-GitHubHeaders {
    param([string]$VersionText)

    return @{
        Accept = 'application/vnd.github+json'
        'X-GitHub-Api-Version' = '2022-11-28'
        'User-Agent' = "FifScreen-Updater/$VersionText"
    }
}

function Resolve-GitHubRelease {
    param(
        [Parameter(Mandatory = $true)]$Release,
        [bool]$RequireAuthenticode = $false
    )

    $tag = [string]$Release.tag_name
    $versionText = $tag -replace '^[vV]', ''
    try {
        $version = [version]$versionText
    } catch {
        throw "GitHub Release 标签不是有效版本号：$tag"
    }

    $escapedVersion = [Regex]::Escape($versionText)
    $assets = @($Release.assets)
    $installerCandidates = @($assets | Where-Object {
        [string]$_.name -match "^FifScreen-Setup-$escapedVersion-(?:dev-)?x64\.exe$"
    })
    if ($RequireAuthenticode) {
        $installerCandidates = @($installerCandidates | Where-Object {
            [string]$_.name -notmatch '-dev-x64\.exe$'
        })
    }
    $installer = $installerCandidates | Select-Object -First 1
    if (-not $installer) {
        throw "GitHub Release $tag 中没有适用于 x64 Windows 的安装包。"
    }

    Assert-HttpsUri -Value ([string]$installer.browser_download_url) `
        -AllowedHosts @('github.com') -Description '安装包下载地址' | Out-Null

    $expectedHash = ''
    if ([string]$installer.digest -match '^sha256:([0-9A-Fa-f]{64})$') {
        $expectedHash = $Matches[1].ToUpperInvariant()
    }

    $hashAssetName = ([string]$installer.name) + '.sha256'
    $hashAsset = $assets | Where-Object { [string]$_.name -eq $hashAssetName } | Select-Object -First 1
    if ($hashAsset) {
        Assert-HttpsUri -Value ([string]$hashAsset.browser_download_url) `
            -AllowedHosts @('github.com') -Description '校验文件下载地址' | Out-Null
    }

    $publishedAt = if ($Release.published_at) {
        [DateTimeOffset]::Parse([string]$Release.published_at).ToLocalTime()
    } else {
        [DateTimeOffset]::Now
    }

    return [pscustomobject]@{
        Version = $version
        VersionText = $versionText
        Tag = $tag
        PublishedAt = $publishedAt
        Notes = if ([string]::IsNullOrWhiteSpace([string]$Release.body)) {
            '本版本未提供更新日志。'
        } else {
            ([string]$Release.body).Trim()
        }
        InstallerName = [string]$installer.name
        InstallerUrl = [string]$installer.browser_download_url
        ExpectedHash = $expectedHash
        HashUrl = if ($hashAsset) { [string]$hashAsset.browser_download_url } else { '' }
        ReleaseUrl = [string]$Release.html_url
    }
}

function Get-LatestRelease {
    param(
        [string]$ApiUrl,
        [string]$VersionText,
        [bool]$RequireAuthenticode
    )

    Assert-HttpsUri -Value $ApiUrl -AllowedHosts @('api.github.com') `
        -Description 'GitHub Release API 地址' | Out-Null
    $headers = Get-GitHubHeaders -VersionText $VersionText
    $release = Invoke-RestMethod -Uri $ApiUrl -Headers $headers -TimeoutSec 20
    return Resolve-GitHubRelease -Release $release -RequireAuthenticode $RequireAuthenticode
}

function Get-ReleaseHash {
    param(
        [Parameter(Mandatory = $true)]$ReleaseInfo,
        [string]$VersionText
    )

    if ($ReleaseInfo.ExpectedHash -match '^[0-9A-F]{64}$') {
        return $ReleaseInfo.ExpectedHash
    }
    if ([string]::IsNullOrWhiteSpace($ReleaseInfo.HashUrl)) {
        throw 'GitHub Release 未提供安装包 SHA-256，已拒绝下载。'
    }

    $headers = Get-GitHubHeaders -VersionText $VersionText
    $response = Invoke-WebRequest -Uri $ReleaseInfo.HashUrl -Headers $headers `
        -UseBasicParsing -TimeoutSec 20
    if ([string]$response.Content -notmatch '([0-9A-Fa-f]{64})') {
        throw 'GitHub Release 的 SHA-256 校验文件无效。'
    }
    return $Matches[1].ToUpperInvariant()
}

function Stop-FifScreenRuntime {
    $stopScript = Join-Path $InstallDir 'maintenance\stop-runtime.ps1'
    if (-not (Test-Path -LiteralPath $stopScript)) {
        $stopScript = Join-Path $InstallDir 'packaging\scripts\stop-runtime.ps1'
    }
    if (-not (Test-Path -LiteralPath $stopScript)) {
        throw '找不到 FifScreen 运行时关闭脚本。'
    }

    & $stopScript -InstallDir $InstallDir
    if ($LASTEXITCODE -ne 0) {
        throw "关闭 FifScreen 相关进程失败，退出码：$LASTEXITCODE"
    }
}

function Install-GitHubRelease {
    param(
        [Parameter(Mandatory = $true)]$ReleaseInfo,
        [string]$VersionText,
        [bool]$RequireAuthenticode
    )

    $expectedHash = Get-ReleaseHash -ReleaseInfo $ReleaseInfo -VersionText $VersionText
    $downloadDir = Join-Path $env:LOCALAPPDATA 'FifScreen\updates'
    New-Item -ItemType Directory -Force -Path $downloadDir | Out-Null
    $downloadPath = Join-Path $downloadDir $ReleaseInfo.InstallerName
    $partialPath = "$downloadPath.download"
    Remove-Item -LiteralPath $partialPath -Force -ErrorAction SilentlyContinue

    $reuseDownload = $false
    if (Test-Path -LiteralPath $downloadPath) {
        $reuseDownload = (Get-FileHash -LiteralPath $downloadPath -Algorithm SHA256).Hash -eq $expectedHash
    }
    if (-not $reuseDownload) {
        Write-UpdateLog "开始下载 $($ReleaseInfo.InstallerName)"
        $headers = Get-GitHubHeaders -VersionText $VersionText
        Invoke-WebRequest -Uri $ReleaseInfo.InstallerUrl -Headers $headers `
            -OutFile $partialPath -UseBasicParsing -TimeoutSec 180
        $actualHash = (Get-FileHash -LiteralPath $partialPath -Algorithm SHA256).Hash
        if ($actualHash -ne $expectedHash) {
            Remove-Item -LiteralPath $partialPath -Force -ErrorAction SilentlyContinue
            throw '下载的安装包 SHA-256 校验失败。'
        }
        Move-Item -LiteralPath $partialPath -Destination $downloadPath -Force
    }

    if ($RequireAuthenticode) {
        $signature = Get-AuthenticodeSignature -LiteralPath $downloadPath
        if ($signature.Status -ne 'Valid') {
            throw "下载的安装包数字签名无效：$($signature.Status)"
        }
    }

    Write-UpdateLog '安装包校验通过，正在关闭 FifScreen 相关进程'
    Stop-FifScreenRuntime
    Start-Process -FilePath $downloadPath -ArgumentList '/UPDATE=1' -Verb RunAs
    Write-UpdateLog "已启动 $($ReleaseInfo.InstallerName)"
}

function Invoke-SelfTest {
    $fakeRelease = [pscustomobject]@{
        tag_name = 'v9.8.7'
        published_at = '2026-07-10T08:00:00Z'
        body = "测试更新日志"
        html_url = 'https://github.com/fiforz/fif-Screen/releases/tag/v9.8.7'
        assets = @(
            [pscustomobject]@{
                name = 'FifScreen-Setup-9.8.7-dev-x64.exe'
                browser_download_url = 'https://github.com/fiforz/fif-Screen/releases/download/v9.8.7/FifScreen-Setup-9.8.7-dev-x64.exe'
                digest = 'sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef'
            }
        )
    }
    $resolved = Resolve-GitHubRelease -Release $fakeRelease
    if ($resolved.VersionText -ne '9.8.7' -or
        $resolved.ExpectedHash -ne '0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF') {
        throw '更新解析自检失败。'
    }
    Write-Output 'UPDATE_SELF_TEST=PASS'
}

if ($SelfTest) {
    Invoke-SelfTest
    exit 0
}

try {
    $configPath = Join-Path $InstallDir 'update.json'
    $config = if (Test-Path -LiteralPath $configPath) {
        Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
    } else {
        [pscustomobject]@{}
    }

    if ([string]::IsNullOrWhiteSpace($CurrentVersion)) {
        $CurrentVersion = [string]$config.currentVersion
    }
    if ([string]::IsNullOrWhiteSpace($CurrentVersion)) {
        $versionPath = Join-Path $InstallDir 'VERSION'
        if (Test-Path -LiteralPath $versionPath) {
            $CurrentVersion = (Get-Content -LiteralPath $versionPath -Raw).Trim()
        }
    }
    $current = [version]$CurrentVersion

    if ([string]::IsNullOrWhiteSpace($ReleaseApiUrl)) {
        $ReleaseApiUrl = [string]$config.releaseApiUrl
    }
    if ([string]::IsNullOrWhiteSpace($ReleaseApiUrl)) {
        $ReleaseApiUrl = $DefaultReleaseApiUrl
    }
    $requireAuthenticode = [bool]$config.requireAuthenticode
} catch {
    Write-UpdateLog "更新配置无效：$($_.Exception.Message)"
    if (-not $Background) {
        Show-UpdateMessage -Text "更新配置无效：$($_.Exception.Message)" `
            -Icon ([System.Windows.Forms.MessageBoxIcon]::Error)
    }
    exit 1
}

$retrySeconds = 30
$releaseInfo = $null
while ($null -eq $releaseInfo) {
    if (-not (Test-ParentProcess)) {
        exit 0
    }
    try {
        $releaseInfo = Get-LatestRelease -ApiUrl $ReleaseApiUrl `
            -VersionText $CurrentVersion -RequireAuthenticode $requireAuthenticode
    } catch {
        Write-UpdateLog "连接 GitHub 失败，将重试：$($_.Exception.Message)"
        if (-not $Background) {
            Show-UpdateMessage `
                -Text '连接GitHub仓库失败，请检查网络环境，并再次点击检查更新！' `
                -Icon ([System.Windows.Forms.MessageBoxIcon]::Warning)
            exit 1
        }
        if (-not (Wait-ForRetry -Seconds $retrySeconds)) {
            exit 0
        }
        $retrySeconds = [Math]::Min($retrySeconds * 2, 900)
    }
}

if ($releaseInfo.Version -le $current) {
    Write-UpdateLog "当前版本 $current 已是最新版本"
    if (-not $Background) {
        Show-UpdateMessage -Text "当前已是最新版本：FifScreen $current"
    }
    exit 0
}

$notes = $releaseInfo.Notes
if ($notes.Length -gt 4000) {
    $notes = $notes.Substring(0, 4000) + "`r`n……"
}
$prompt = @"
发现 FifScreen 新版本。

当前版本：$current
最新版本：$($releaseInfo.Version)
更新时间：$($releaseInfo.PublishedAt.ToString('yyyy-MM-dd HH:mm'))

更新内容：
$notes

是否立即下载并安装？
"@
$answer = [System.Windows.Forms.MessageBox]::Show(
    $prompt,
    'FifScreen 发现新版本',
    [System.Windows.Forms.MessageBoxButtons]::YesNo,
    [System.Windows.Forms.MessageBoxIcon]::Information
)
if ($answer -ne [System.Windows.Forms.DialogResult]::Yes) {
    Write-UpdateLog "用户暂不更新到 $($releaseInfo.Version)"
    exit 0
}

try {
    Install-GitHubRelease -ReleaseInfo $releaseInfo -VersionText $CurrentVersion `
        -RequireAuthenticode $requireAuthenticode
} catch {
    Write-UpdateLog "更新失败：$($_.Exception.Message)"
    Show-UpdateMessage -Text "更新失败：$($_.Exception.Message)" `
        -Icon ([System.Windows.Forms.MessageBoxIcon]::Error)
    exit 1
}
