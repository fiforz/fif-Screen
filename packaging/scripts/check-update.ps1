[CmdletBinding()]
param(
    [string]$InstallDir = '',
    [string]$ReleaseApiUrl = '',
    [string]$FallbackManifestUrl = '',
    [string]$CurrentVersion = '',
    [switch]$Background,
    [int]$ParentProcessId = 0,
    [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
[Net.ServicePointManager]::SecurityProtocol = `
    [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
Add-Type -AssemblyName System.Windows.Forms

if ([string]::IsNullOrWhiteSpace($InstallDir)) {
    $InstallDir = Split-Path -Parent $PSScriptRoot
}

$DefaultReleaseApiUrl = 'https://api.github.com/repos/fiforz/fif-Screen/releases/latest'
$DefaultFallbackManifestUrl = 'https://github.com/fiforz/fif-Screen/releases/latest/download/latest-development.json'
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

function ConvertFrom-WebResponseJson {
    param([Parameter(Mandatory = $true)]$Response)

    $content = $Response.Content
    $json = if ($content -is [byte[]]) {
        [Text.Encoding]::UTF8.GetString($content)
    } else {
        [string]$content
    }
    if ($json.Length -gt 0 -and $json[0] -eq [char]0xFEFF) {
        $json = $json.Substring(1)
    }
    if ([string]::IsNullOrWhiteSpace($json)) {
        throw 'GitHub 返回了空的 JSON 响应。'
    }
    return $json | ConvertFrom-Json
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

function Resolve-UpdateManifest {
    param(
        [Parameter(Mandatory = $true)]$Manifest,
        [string]$ExpectedChannel,
        [bool]$RequireAuthenticode = $false
    )

    if ([string]$Manifest.product -ne 'FifScreen') {
        throw "静态更新清单产品无效：$($Manifest.product)"
    }
    if ([string]$Manifest.channel -ne $ExpectedChannel) {
        throw "静态更新清单通道无效：$($Manifest.channel)"
    }
    if ([string]$Manifest.architecture -ne 'x64') {
        throw "静态更新清单架构无效：$($Manifest.architecture)"
    }

    try {
        $version = [version][string]$Manifest.version
    } catch {
        throw "静态更新清单版本号无效：$($Manifest.version)"
    }
    $installerName = [string]$Manifest.installerName
    $escapedVersion = [Regex]::Escape($version.ToString(3))
    if ($installerName -notmatch "^FifScreen-Setup-$escapedVersion-(?:dev-)?x64\.exe$") {
        throw "静态更新清单安装包名称无效：$installerName"
    }
    if ($RequireAuthenticode -and $installerName -match '-dev-x64\.exe$') {
        throw '正式更新通道不能使用开发版安装包。'
    }
    if ([string]$Manifest.sha256 -notmatch '^[0-9A-Fa-f]{64}$') {
        throw '静态更新清单缺少有效的 SHA-256。'
    }
    Assert-HttpsUri -Value ([string]$Manifest.installerUrl) `
        -AllowedHosts @('github.com') -Description '静态清单安装包下载地址' | Out-Null

    $publishedAt = if ($Manifest.publishedAt) {
        [DateTimeOffset]::Parse([string]$Manifest.publishedAt).ToLocalTime()
    } else {
        [DateTimeOffset]::Now
    }
    return [pscustomobject]@{
        Version = $version
        VersionText = $version.ToString(3)
        Tag = "v$($version.ToString(3))"
        PublishedAt = $publishedAt
        Notes = if ([string]::IsNullOrWhiteSpace([string]$Manifest.releaseNotes)) {
            '本版本未提供更新日志。'
        } else {
            ([string]$Manifest.releaseNotes).Trim()
        }
        InstallerName = $installerName
        InstallerUrl = [string]$Manifest.installerUrl
        ExpectedHash = ([string]$Manifest.sha256).ToUpperInvariant()
        HashUrl = ''
        ReleaseUrl = [string]$Manifest.releaseUrl
    }
}

function Get-LatestUpdate {
    param(
        [string]$ApiUrl,
        [string]$ManifestUrl,
        [string]$VersionText,
        [string]$Channel,
        [bool]$RequireAuthenticode
    )

    try {
        $release = Get-LatestRelease -ApiUrl $ApiUrl -VersionText $VersionText `
            -RequireAuthenticode $RequireAuthenticode
        Write-UpdateLog '已通过 GitHub Release API 获取最新版本'
        return $release
    } catch {
        $apiError = $_.Exception.Message
        Write-UpdateLog "GitHub Release API 失败，尝试静态清单：$apiError"
    }

    if ([string]::IsNullOrWhiteSpace($ManifestUrl)) {
        throw "GitHub Release API 请求失败：$apiError"
    }
    try {
        Assert-HttpsUri -Value $ManifestUrl `
            -AllowedHosts @('raw.githubusercontent.com', 'github.com') `
            -Description '静态更新清单地址' | Out-Null
        $headers = Get-GitHubHeaders -VersionText $VersionText
        $response = Invoke-WebRequest -Uri $ManifestUrl -Headers $headers `
            -UseBasicParsing -TimeoutSec 20
        $manifest = ConvertFrom-WebResponseJson -Response $response
        $release = Resolve-UpdateManifest -Manifest $manifest -ExpectedChannel $Channel `
            -RequireAuthenticode $RequireAuthenticode
        Write-UpdateLog '已通过仓库静态清单获取最新版本'
        return $release
    } catch {
        throw "GitHub Release API 与静态清单均失败。API：$apiError；清单：$($_.Exception.Message)"
    }
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
    $fakeManifest = [pscustomobject]@{
        product = 'FifScreen'
        channel = 'development'
        architecture = 'x64'
        version = '9.8.7'
        publishedAt = '2026-07-10T08:00:00Z'
        releaseNotes = '静态清单测试'
        installerName = 'FifScreen-Setup-9.8.7-dev-x64.exe'
        installerUrl = 'https://github.com/fiforz/fif-Screen/releases/download/v9.8.7/FifScreen-Setup-9.8.7-dev-x64.exe'
        sha256 = '0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef'
        releaseUrl = 'https://github.com/fiforz/fif-Screen/releases/tag/v9.8.7'
    }
    $manifestResult = Resolve-UpdateManifest -Manifest $fakeManifest -ExpectedChannel 'development'
    if ($manifestResult.VersionText -ne '9.8.7' -or
        $manifestResult.ExpectedHash -ne '0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF') {
        throw '静态更新清单自检失败。'
    }
    $jsonBytes = [Text.Encoding]::UTF8.GetBytes(($fakeManifest | ConvertTo-Json -Depth 4))
    $manifestBytes = [byte[]]([Text.Encoding]::UTF8.GetPreamble() + $jsonBytes)
    $decodedManifest = ConvertFrom-WebResponseJson -Response ([pscustomobject]@{ Content = $manifestBytes })
    $decodedResult = Resolve-UpdateManifest -Manifest $decodedManifest -ExpectedChannel 'development'
    if ($decodedResult.VersionText -ne '9.8.7') {
        throw '二进制静态更新清单解码自检失败。'
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
    if ([string]::IsNullOrWhiteSpace($FallbackManifestUrl)) {
        $FallbackManifestUrl = [string]$config.fallbackManifestUrl
    }
    if ([string]::IsNullOrWhiteSpace($FallbackManifestUrl)) {
        $FallbackManifestUrl = $DefaultFallbackManifestUrl
    }
    $channel = if ([string]::IsNullOrWhiteSpace([string]$config.channel)) {
        'development'
    } else {
        [string]$config.channel
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
$manualAttempt = 0
$releaseInfo = $null
while ($null -eq $releaseInfo) {
    if (-not (Test-ParentProcess)) {
        exit 0
    }
    try {
        $releaseInfo = Get-LatestUpdate -ApiUrl $ReleaseApiUrl `
            -ManifestUrl $FallbackManifestUrl -VersionText $CurrentVersion `
            -Channel $channel -RequireAuthenticode $requireAuthenticode
    } catch {
        $manualAttempt += 1
        Write-UpdateLog "连接 GitHub 失败，第 $manualAttempt 次检查未成功：$($_.Exception.Message)"
        if (-not $Background -and $manualAttempt -ge 3) {
            Show-UpdateMessage `
                -Text '连接GitHub仓库失败，请检查网络环境，并再次点击检查更新！' `
                -Icon ([System.Windows.Forms.MessageBoxIcon]::Warning)
            exit 1
        }
        $waitSeconds = if ($Background) { $retrySeconds } else { 2 * $manualAttempt }
        if (-not (Wait-ForRetry -Seconds $waitSeconds)) {
            exit 0
        }
        if ($Background) {
            $retrySeconds = [Math]::Min($retrySeconds * 2, 900)
        }
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
