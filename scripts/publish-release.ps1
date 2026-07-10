[CmdletBinding()]
param(
    [string]$Version = '',
    [string]$InstallerPath = '',
    [string]$ReleaseNotesPath = '',
    [string]$Repository = 'fiforz/fif-Screen',
    [string]$TargetCommit = 'HEAD',
    [switch]$Prerelease
)

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent $PSScriptRoot

if (-not $Version) {
    $Version = (Get-Content -LiteralPath (Join-Path $RepoRoot 'VERSION') -Raw).Trim()
}
if ($Version -notmatch '^\d+\.\d+\.\d+$') {
    throw "无效版本号：$Version"
}

if (-not $InstallerPath) {
    $InstallerPath = Join-Path $RepoRoot "artifacts\installer\FifScreen-Setup-$Version-dev-x64.exe"
}
if (-not $ReleaseNotesPath) {
    $ReleaseNotesPath = Join-Path $RepoRoot "docs\releases\v$Version.md"
}
if (-not (Test-Path -LiteralPath $InstallerPath)) {
    throw "找不到安装包：$InstallerPath"
}
if (-not (Test-Path -LiteralPath $ReleaseNotesPath)) {
    throw "找不到 Release 说明：$ReleaseNotesPath"
}

$status = (& git -C $RepoRoot status --porcelain) -join "`n"
if ($LASTEXITCODE -ne 0) {
    throw '无法读取 Git 工作区状态。'
}
if (-not [string]::IsNullOrWhiteSpace($status)) {
    throw '发布前 Git 工作区必须干净。'
}

$remote = (& git -C $RepoRoot remote get-url origin).Trim()
if ($remote -ne "git@github.com:$Repository.git") {
    throw "origin 必须使用 SSH 地址 git@github.com:$Repository.git，当前为：$remote"
}
$ResolvedTargetCommit = (& git -C $RepoRoot rev-parse "${TargetCommit}^{commit}").Trim()
if ($LASTEXITCODE -ne 0 -or $ResolvedTargetCommit -notmatch '^[0-9a-fA-F]{40}$') {
    throw "无法解析发布目标提交：$TargetCommit"
}

function Get-GitHubApiHeaders {
    $credentialInput = "protocol=https`nhost=github.com`n`n"
    $credentialLines = @($credentialInput | & git credential fill 2>$null)
    if ($LASTEXITCODE -ne 0) {
        throw '无法从 Git Credential Manager 读取 GitHub 登录凭据。'
    }
    $passwordLine = $credentialLines | Where-Object { $_ -like 'password=*' } | Select-Object -First 1
    if (-not $passwordLine) {
        throw 'GitHub CLI 未登录，Git Credential Manager 中也没有可用凭据。'
    }
    $token = $passwordLine.Substring('password='.Length)
    return @{
        Accept = 'application/vnd.github+json'
        Authorization = "Bearer $token"
        'X-GitHub-Api-Version' = '2022-11-28'
        'User-Agent' = 'FifScreen-Release-Publisher'
    }
}

function Publish-WithGitHubApi {
    param(
        [string]$Tag,
        [string[]]$AssetPaths,
        [string]$NotesPath,
        [hashtable]$Headers
    )

    $apiRoot = "https://api.github.com/repos/$Repository"
    $encodedTag = [Uri]::EscapeDataString($Tag)
    $release = $null
    try {
        $release = Invoke-RestMethod -Uri "$apiRoot/releases/tags/$encodedTag" `
            -Headers $Headers -TimeoutSec 30
    } catch {
        $statusCode = if ($_.Exception.Response) { [int]$_.Exception.Response.StatusCode } else { 0 }
        if ($statusCode -ne 404) {
            throw
        }
    }

    $resolvedNotesPath = (Resolve-Path -LiteralPath $NotesPath).Path
    $releaseBody = [IO.File]::ReadAllText($resolvedNotesPath, [Text.UTF8Encoding]::new($false))
    $payload = [ordered]@{
        tag_name = $Tag
        target_commitish = $ResolvedTargetCommit
        name = "FifScreen $Version"
        body = $releaseBody
        draft = $false
        prerelease = [bool]$Prerelease
        make_latest = if ($Prerelease) { 'false' } else { 'true' }
    } | ConvertTo-Json

    if ($release) {
        $release = Invoke-RestMethod -Method Patch -Uri "$apiRoot/releases/$($release.id)" `
            -Headers $Headers -ContentType 'application/json; charset=utf-8' `
            -Body ([Text.Encoding]::UTF8.GetBytes($payload)) -TimeoutSec 30
    } else {
        $release = Invoke-RestMethod -Method Post -Uri "$apiRoot/releases" `
            -Headers $Headers -ContentType 'application/json; charset=utf-8' `
            -Body ([Text.Encoding]::UTF8.GetBytes($payload)) -TimeoutSec 30
    }

    $uploadRoot = ([string]$release.upload_url) -replace '\{\?name,label\}$', ''
    foreach ($assetPath in $AssetPaths) {
        $assetName = [IO.Path]::GetFileName($assetPath)
        $existingAsset = @($release.assets) | Where-Object { [string]$_.name -eq $assetName } | Select-Object -First 1
        if ($existingAsset) {
            Invoke-RestMethod -Method Delete -Uri "$apiRoot/releases/assets/$($existingAsset.id)" `
                -Headers $Headers -TimeoutSec 30 | Out-Null
        }
        $uploadUrl = "${uploadRoot}?name=$([Uri]::EscapeDataString($assetName))"
        $contentType = if ($assetName.EndsWith('.sha256')) { 'text/plain' } else { 'application/octet-stream' }
        Invoke-RestMethod -Method Post -Uri $uploadUrl -Headers $Headers `
            -ContentType $contentType -InFile $assetPath -TimeoutSec 300 | Out-Null
    }
}

$gh = Get-Command gh -ErrorAction SilentlyContinue
$useGitHubCli = $false
if ($gh) {
    & $gh.Source auth status *> $null
    $useGitHubCli = $LASTEXITCODE -eq 0
}
$apiHeaders = if ($useGitHubCli) { $null } else { Get-GitHubApiHeaders }

$tag = "v$Version"
$hash = (Get-FileHash -LiteralPath $InstallerPath -Algorithm SHA256).Hash
$hashPath = "$InstallerPath.sha256"
"$hash  $([IO.Path]::GetFileName($InstallerPath))" | Set-Content -LiteralPath $hashPath -Encoding ASCII
$releaseAssets = @($InstallerPath, $hashPath)

$currentRepoVersion = (Get-Content -LiteralPath (Join-Path $RepoRoot 'VERSION') -Raw).Trim()
if ($Version -eq $currentRepoVersion) {
    $channel = if ([IO.Path]::GetFileName($InstallerPath) -match '-dev-x64\.exe$') {
        'development'
    } else {
        'stable'
    }
    $latestManifestPath = Join-Path $RepoRoot "updates\latest-$channel.json"
    if (-not (Test-Path -LiteralPath $latestManifestPath)) {
        throw "找不到当前版本的静态更新清单：$latestManifestPath"
    }
    $latestManifest = Get-Content -LiteralPath $latestManifestPath -Raw | ConvertFrom-Json
    if ([string]$latestManifest.version -ne $Version -or
        [string]$latestManifest.installerName -ne [IO.Path]::GetFileName($InstallerPath) -or
        ([string]$latestManifest.sha256).ToUpperInvariant() -ne $hash) {
        throw '静态更新清单与当前安装包版本、文件名或 SHA-256 不一致。'
    }
    $releaseAssets += $latestManifestPath
}

& git -C $RepoRoot push origin main
if ($LASTEXITCODE -ne 0) {
    throw '推送 main 分支失败。'
}

$localTag = & git -C $RepoRoot tag --list $tag
if (-not $localTag) {
    & git -C $RepoRoot tag -a $tag $ResolvedTargetCommit -m "FifScreen $Version"
    if ($LASTEXITCODE -ne 0) {
        throw "创建标签 $tag 失败。"
    }
}
& git -C $RepoRoot push origin $tag
if ($LASTEXITCODE -ne 0) {
    throw "推送标签 $tag 失败。"
}

if ($useGitHubCli) {
    & $gh.Source release view $tag --repo $Repository *> $null
    if ($LASTEXITCODE -eq 0) {
        $uploadArguments = @('release', 'upload', $tag) + $releaseAssets + `
            @('--repo', $Repository, '--clobber')
        & $gh.Source @uploadArguments
    } else {
        $arguments = @('release', 'create', $tag) + $releaseAssets + @(
            '--repo', $Repository,
            '--title', "FifScreen $Version",
            '--notes-file', $ReleaseNotesPath,
            '--latest'
        )
        if ($Prerelease) {
            $arguments += '--prerelease'
        }
        & $gh.Source @arguments
    }
    if ($LASTEXITCODE -ne 0) {
        throw "发布 GitHub Release $tag 失败。"
    }
} else {
    Publish-WithGitHubApi -Tag $tag -AssetPaths $releaseAssets `
        -NotesPath $ReleaseNotesPath -Headers $apiHeaders
}

Write-Host "RELEASE_RESULT=PASS"
Write-Host "RELEASE_TAG=$tag"
Write-Host "INSTALLER_SHA256=$hash"
