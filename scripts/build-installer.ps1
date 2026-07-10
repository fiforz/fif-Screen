[CmdletBinding()]
param(
    [ValidateSet('Development', 'Production')]
    [string]$DriverFlavor = 'Development',

    [string]$DriverPackageDir = '',

    [string]$ApkPath = '',

    [string]$UpdateManifestUrl = '',

    [string]$ReleaseApiUrl = 'https://api.github.com/repos/fiforz/fif-Screen/releases/latest',

    [string]$FallbackManifestUrl = '',

    [string]$UpdateBaseUrl = '',

    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot
$DriverPackageWasProvided = -not [string]::IsNullOrWhiteSpace($DriverPackageDir)
$ApkWasProvided = -not [string]::IsNullOrWhiteSpace($ApkPath)
$Version = (Get-Content -LiteralPath (Join-Path $RepoRoot 'VERSION') -Raw).Trim()
$VersionCode = [int](Get-Content -LiteralPath (Join-Path $RepoRoot 'VERSION_CODE') -Raw).Trim()
$UpdateChannel = if ($DriverFlavor -eq 'Production') { 'stable' } else { 'development' }
if (-not $FallbackManifestUrl) {
    $FallbackManifestUrl = "https://raw.githubusercontent.com/fiforz/fif-Screen/main/updates/latest-$UpdateChannel.json"
}

if ($Version -notmatch '^\d+\.\d+\.\d+$') {
    throw "VERSION must use semantic x.y.z format: $Version"
}

if ($DriverFlavor -eq 'Production') {
    if (-not $DriverPackageWasProvided) {
        throw 'Production packaging requires -DriverPackageDir with a Microsoft-signed driver package.'
    }
    if (-not $ApkWasProvided) {
        throw 'Production packaging requires -ApkPath with a release-signed Android APK.'
    }
    if (-not $ReleaseApiUrl) {
        throw 'Production packaging requires -ReleaseApiUrl with an HTTPS endpoint.'
    }
}

foreach ($candidateUrl in @($UpdateManifestUrl, $ReleaseApiUrl, $FallbackManifestUrl, $UpdateBaseUrl)) {
    if ($candidateUrl) {
        $uri = [Uri]$candidateUrl
        if (-not $uri.IsAbsoluteUri -or $uri.Scheme -ne 'https') {
            throw "Installer update URLs must use HTTPS: $candidateUrl"
        }
    }
}

function Find-RequiredFile {
    param([string]$Name, [string[]]$Candidates)
    foreach ($candidate in $Candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate)) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }
    throw "Required tool was not found: $Name"
}

function Invoke-Checked {
    param(
        [string]$FilePath,
        [string[]]$Arguments,
        [string]$Description
    )
    Write-Host "==> $Description"
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Description failed with exit code $LASTEXITCODE"
    }
}

function Invoke-DeveloperCommand {
    param([string]$Command, [string]$Description)
    Write-Host "==> $Description"
    $fullCommand = 'call "{0}" -arch=x64 -host_arch=x64 >nul && {1}' -f $script:VsDevCmd, $Command
    & cmd.exe /d /s /c $fullCommand
    if ($LASTEXITCODE -ne 0) {
        throw "$Description failed with exit code $LASTEXITCODE"
    }
}

function Reset-ArtifactDirectory {
    param([string]$Path, [string]$AllowedRoot)
    $resolvedPath = [IO.Path]::GetFullPath($Path)
    $resolvedRoot = [IO.Path]::GetFullPath($AllowedRoot).TrimEnd('\') + '\'
    if (-not $resolvedPath.StartsWith($resolvedRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to reset path outside installer artifacts: $resolvedPath"
    }
    if (Test-Path -LiteralPath $resolvedPath) {
        Remove-Item -LiteralPath $resolvedPath -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $resolvedPath | Out-Null
}

function Copy-RequiredFile {
    param([string]$Source, [string]$Destination)
    if (-not (Test-Path -LiteralPath $Source)) {
        throw "Required payload is missing: $Source"
    }
    $parent = Split-Path -Parent $Destination
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
    Copy-Item -LiteralPath $Source -Destination $Destination -Force
}

function Get-RelativePath {
    param([string]$BasePath, [string]$Path)
    $base = [IO.Path]::GetFullPath($BasePath).TrimEnd('\') + '\'
    $full = [IO.Path]::GetFullPath($Path)
    if (-not $full.StartsWith($base, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$full is outside $base"
    }
    return $full.Substring($base.Length).Replace('\', '/')
}

function Assert-FileVersion {
    param([string]$Path, [string]$Component)
    $actual = ([Diagnostics.FileVersionInfo]::GetVersionInfo($Path).FileVersion).Trim()
    if ($actual -notin @($Version, "$Version.0")) {
        throw "$Component file version $actual does not match product version $Version"
    }
}

function Get-SigntoolPath {
    $candidates = @(
        'D:\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe',
        'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe'
    )
    return Find-RequiredFile -Name 'signtool.exe' -Candidates $candidates
}

function Sign-WindowsFile {
    param([string]$Path)
    $pfxPath = $env:FIFSCREEN_SIGN_PFX
    if (-not $pfxPath) {
        return
    }
    if (-not (Test-Path -LiteralPath $pfxPath)) {
        throw "FIFSCREEN_SIGN_PFX does not exist: $pfxPath"
    }
    if (-not $env:FIFSCREEN_SIGN_PFX_PASSWORD) {
        throw 'FIFSCREEN_SIGN_PFX_PASSWORD is required when FIFSCREEN_SIGN_PFX is set.'
    }
    $timestampUrl = if ($env:FIFSCREEN_TIMESTAMP_URL) {
        $env:FIFSCREEN_TIMESTAMP_URL
    } else {
        'http://timestamp.digicert.com'
    }
    $signtool = Get-SigntoolPath
    Invoke-Checked -FilePath $signtool -Description "Sign $(Split-Path -Leaf $Path)" -Arguments @(
        'sign', '/fd', 'SHA256', '/td', 'SHA256', '/tr', $timestampUrl,
        '/f', $pfxPath, '/p', $env:FIFSCREEN_SIGN_PFX_PASSWORD, $Path
    )
}

$VsDevCmd = Find-RequiredFile -Name 'VsDevCmd.bat' -Candidates @(
    'D:\SoftWare\Visual Studio\app\Common7\Tools\VsDevCmd.bat',
    'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat',
    'C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat'
)
$CMake = Find-RequiredFile -Name 'cmake.exe' -Candidates @(
    'D:\SoftWare\Visual Studio\app\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
)
$Ninja = Find-RequiredFile -Name 'ninja.exe' -Candidates @(
    'D:\SoftWare\Visual Studio\app\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
)
$MsBuild = Find-RequiredFile -Name 'msbuild.exe' -Candidates @(
    'D:\SoftWare\Visual Studio\app\MSBuild\Current\Bin\amd64\MSBuild.exe',
    'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe',
    'C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\amd64\MSBuild.exe',
    'D:\SoftWare\Visual Studio\app\MSBuild\Current\Bin\MSBuild.exe'
)
$Iscc = Find-RequiredFile -Name 'ISCC.exe' -Candidates @(
    (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe'),
    'C:\Program Files (x86)\Inno Setup 6\ISCC.exe',
    'C:\Program Files\Inno Setup 6\ISCC.exe'
)
$Aapt = Find-RequiredFile -Name 'aapt.exe' -Candidates @(
    (Join-Path $RepoRoot 'tools\android-sdk\build-tools\35.0.0\aapt.exe'),
    (Join-Path $RepoRoot 'tools\android-sdk\build-tools\34.0.0\aapt.exe')
)
$ApkSigner = Find-RequiredFile -Name 'apksigner.bat' -Candidates @(
    (Join-Path $RepoRoot 'tools\android-sdk\build-tools\35.0.0\apksigner.bat'),
    (Join-Path $RepoRoot 'tools\android-sdk\build-tools\34.0.0\apksigner.bat')
)

$releaseBuildDir = Join-Path $RepoRoot 'build\installer-release'
$hostPath = Join-Path $releaseBuildDir 'windows-host\fif-host.exe'
$launcherPath = Join-Path $releaseBuildDir 'windows-driver\FifIddDeviceLauncher\fif-idd-device-launcher.exe'
$androidDebugApk = Join-Path $RepoRoot 'android-client\build\outputs\apk\debug\android-client-debug.apk'
$driverSolution = Join-Path $RepoRoot 'windows-driver\FifIddDriver\FifIddDriver.sln'
$driverReleaseRoot = Join-Path $RepoRoot 'windows-driver\FifIddDriver\x64\Release'
$wdkToolsRoot = Get-ChildItem -Path (Join-Path $RepoRoot 'windows-driver\packages\Microsoft.Windows.WDK.x64.*\c\bin\*') `
    -Directory -ErrorAction SilentlyContinue |
    Where-Object {
        (Test-Path -LiteralPath (Join-Path $_.FullName 'x64\stampinf.exe')) -and
        (Test-Path -LiteralPath (Join-Path $_.FullName 'x86\Inf2Cat.exe'))
    } |
    Sort-Object Name -Descending |
    Select-Object -First 1

if (-not $wdkToolsRoot) {
    throw 'Bundled WDK tools were not found. Restore the windows-driver NuGet packages first.'
}
$wdkX64Tools = Join-Path $wdkToolsRoot.FullName 'x64'
$wdkX86Tools = Join-Path $wdkToolsRoot.FullName 'x86'

if (-not $SkipBuild) {
    $configure = '"{0}" -S "{1}" -B "{2}" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_MAKE_PROGRAM="{3}"' -f `
        $CMake, $RepoRoot, $releaseBuildDir, $Ninja
    Invoke-DeveloperCommand -Command $configure -Description 'Configure native Release build'

    $build = '"{0}" --build "{1}" --target fif-host fif-idd-device-launcher' -f $CMake, $releaseBuildDir
    Invoke-DeveloperCommand -Command $build -Description 'Build native Release runtime'

    if ($DriverFlavor -eq 'Development' -and -not $DriverPackageDir) {
        $driverBuild = 'set "PATH={0};{1};%PATH%" && "{2}" "{3}" /m:1 /p:Configuration=Release /p:Platform=x64 /p:RunCodeAnalysis=false /v:minimal' -f `
            $wdkX64Tools, $wdkX86Tools, $MsBuild, $driverSolution
        Invoke-DeveloperCommand -Command $driverBuild -Description 'Build and test-sign Release display driver'
    }

    if ($DriverFlavor -eq 'Development' -and -not $ApkPath) {
        Invoke-Checked -FilePath (Join-Path $RepoRoot 'gradlew.bat') `
            -Arguments @(':android-client:assembleDebug') -Description 'Build versioned Android APK'
    }
}

foreach ($nativeBinary in @($hostPath, $launcherPath)) {
    if (-not (Test-Path -LiteralPath $nativeBinary)) {
        throw "Native Release binary is missing: $nativeBinary"
    }
}
Assert-FileVersion -Path $hostPath -Component 'Windows host'
Assert-FileVersion -Path $launcherPath -Component 'Software-device launcher'

if (-not $DriverPackageDir) {
    $driverPackage = Get-ChildItem -Path $driverReleaseRoot -Directory -Recurse -ErrorAction SilentlyContinue |
        Where-Object { Test-Path (Join-Path $_.FullName 'FifIddDriver.inf') } |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
    if (-not $driverPackage) {
        throw 'Release driver package directory was not found. Pass -DriverPackageDir explicitly.'
    }
    $DriverPackageDir = $driverPackage.FullName
}
$DriverPackageDir = (Resolve-Path -LiteralPath $DriverPackageDir).Path

if (-not $ApkPath) {
    $ApkPath = $androidDebugApk
}
$ApkPath = (Resolve-Path -LiteralPath $ApkPath).Path

$apkBadging = @(& $Aapt dump badging $ApkPath 2>&1)
if ($LASTEXITCODE -ne 0) {
    throw "Could not read APK metadata: $($apkBadging -join ' ')"
}
$apkPackageLine = $apkBadging | Where-Object { $_ -match '^package:' } | Select-Object -First 1
if (-not $apkPackageLine -or
    $apkPackageLine -notmatch "name='com\.fif\.screen'" -or
    $apkPackageLine -notmatch "versionCode='$VersionCode'" -or
    $apkPackageLine -notmatch "versionName='$([regex]::Escape($Version))'") {
    throw "APK identity or version does not match FifScreen $Version ($VersionCode): $apkPackageLine"
}
if ($DriverFlavor -eq 'Production' -and ($apkBadging -match '^application-debuggable$')) {
    throw 'Production packaging rejects a debuggable Android APK.'
}

$apkVerification = @(& $ApkSigner verify --verbose --print-certs $ApkPath 2>&1)
if ($LASTEXITCODE -ne 0) {
    throw "Android APK signature verification failed: $($apkVerification -join ' ')"
}
$apkSignerSubject = [string](($apkVerification |
    Where-Object { $_ -match '^Signer #1 certificate DN:' } |
    Select-Object -First 1) -replace '^Signer #1 certificate DN:\s*', '')
if ($DriverFlavor -eq 'Production' -and $apkSignerSubject -match 'CN=Android Debug') {
    throw 'Production packaging rejects an APK signed with the Android debug certificate.'
}

$driverInf = Join-Path $DriverPackageDir 'FifIddDriver.inf'
$driverDll = Join-Path $DriverPackageDir 'FifIddDriver.dll'
$driverCat = Get-ChildItem -LiteralPath $DriverPackageDir -Filter '*.cat' | Select-Object -First 1
if (-not $driverCat) {
    throw "Driver catalog is missing from $DriverPackageDir"
}
Assert-FileVersion -Path $driverDll -Component 'Display driver'

$catalogSignature = Get-AuthenticodeSignature -LiteralPath $driverCat.FullName
if ($catalogSignature.Status -ne 'Valid') {
    throw "Driver catalog signature is not valid: $($catalogSignature.StatusMessage)"
}
if ($DriverFlavor -eq 'Production') {
    if ($null -eq $catalogSignature.SignerCertificate -or
        $catalogSignature.SignerCertificate.Subject -notmatch 'Microsoft Windows Hardware Compatibility Publisher') {
        throw 'Production packaging requires a Microsoft-signed driver catalog.'
    }
    if (-not $env:FIFSCREEN_SIGN_PFX) {
        throw 'Production packaging requires FIFSCREEN_SIGN_PFX for Windows executable and Setup signing.'
    }
}

Sign-WindowsFile -Path $hostPath
Sign-WindowsFile -Path $launcherPath

$artifactRoot = Join-Path $RepoRoot 'artifacts\installer'
$stageDir = Join-Path $artifactRoot ("stage-{0}-{1}" -f $Version, $DriverFlavor.ToLowerInvariant())
Reset-ArtifactDirectory -Path $stageDir -AllowedRoot $artifactRoot

$runtimeDir = Join-Path $stageDir 'runtime'
$binDir = Join-Path $runtimeDir 'bin'
$adbStageDir = Join-Path $runtimeDir 'adb'
$androidStageDir = Join-Path $runtimeDir 'android'
$driverStageDir = Join-Path $runtimeDir 'driver'
$maintenanceDir = Join-Path $stageDir 'maintenance'

Copy-RequiredFile -Source (Join-Path $RepoRoot 'FifScreen Control.cmd') -Destination (Join-Path $stageDir 'FifScreen Control.cmd')
Copy-RequiredFile -Source (Join-Path $RepoRoot 'scripts\fifscreen-control.ps1') -Destination (Join-Path $stageDir 'scripts\fifscreen-control.ps1')
Copy-RequiredFile -Source (Join-Path $RepoRoot 'VERSION') -Destination (Join-Path $stageDir 'VERSION')
Copy-RequiredFile -Source $hostPath -Destination (Join-Path $binDir 'fif-host.exe')
Copy-RequiredFile -Source $launcherPath -Destination (Join-Path $binDir 'fif-idd-device-launcher.exe')
Copy-RequiredFile -Source $ApkPath -Destination (Join-Path $androidStageDir 'FifScreen.apk')
Copy-RequiredFile -Source $driverInf -Destination (Join-Path $driverStageDir 'FifIddDriver.inf')
Copy-RequiredFile -Source $driverDll -Destination (Join-Path $driverStageDir 'FifIddDriver.dll')
Copy-RequiredFile -Source $driverCat.FullName -Destination (Join-Path $driverStageDir 'FifIddDriver.cat')

$certificateSearchRoots = @($DriverPackageDir, (Split-Path -Parent $DriverPackageDir), $driverReleaseRoot) |
    Select-Object -Unique
$driverCertificate = Get-ChildItem -Path $certificateSearchRoots -Filter '*.cer' -Recurse -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending |
    Where-Object {
        try {
            $candidateCertificate = [Security.Cryptography.X509Certificates.X509Certificate2]::new($_.FullName)
            $candidateCertificate.Thumbprint -eq $catalogSignature.SignerCertificate.Thumbprint
        } catch {
            $false
        }
    } |
    Select-Object -First 1
if ($DriverFlavor -eq 'Development') {
    if (-not $driverCertificate) {
        throw 'Development driver public certificate was not found.'
    }
    Copy-RequiredFile -Source $driverCertificate.FullName -Destination (Join-Path $driverStageDir 'FifIddDriver.cer')
}

$adbSourceDir = Join-Path $RepoRoot 'tools\android-sdk\platform-tools'
foreach ($adbFile in @('adb.exe', 'AdbWinApi.dll', 'AdbWinUsbApi.dll')) {
    Copy-RequiredFile -Source (Join-Path $adbSourceDir $adbFile) -Destination (Join-Path $adbStageDir $adbFile)
}
foreach ($optionalAdbFile in @('libwinpthread-1.dll', 'source.properties', 'NOTICE.txt')) {
    $source = Join-Path $adbSourceDir $optionalAdbFile
    if (Test-Path -LiteralPath $source) {
        Copy-RequiredFile -Source $source -Destination (Join-Path $adbStageDir $optionalAdbFile)
    }
}

foreach ($maintenanceScript in @('install-driver.ps1', 'stop-runtime.ps1', 'uninstall-cleanup.ps1', 'check-update.ps1')) {
    Copy-RequiredFile -Source (Join-Path $RepoRoot "packaging\scripts\$maintenanceScript") `
        -Destination (Join-Path $maintenanceDir $maintenanceScript)
}
Copy-RequiredFile `
    -Source (Join-Path $RepoRoot 'packaging\licenses\Inno-Setup-Chinese-Simplified-Translation-LICENSE.txt') `
    -Destination (Join-Path $stageDir 'licenses\Inno-Setup-Chinese-Simplified-Translation-LICENSE.txt')

$driverVersionLine = Select-String -LiteralPath (Join-Path $driverStageDir 'FifIddDriver.inf') -Pattern '^DriverVer=' | Select-Object -First 1
$driverVersion = if ($driverVersionLine -and $driverVersionLine.Line -match ',([^,]+)$') {
    $Matches[1].Trim()
} else {
    'unknown'
}
if ($driverVersion -ne "$Version.0") {
    throw "Driver package version $driverVersion does not match product version $Version.0"
}

$payloadFiles = Get-ChildItem -Path $runtimeDir -File -Recurse | Sort-Object FullName
$payloadEntries = foreach ($file in $payloadFiles) {
    [ordered]@{
        path = Get-RelativePath -BasePath $stageDir -Path $file.FullName
        size = $file.Length
        sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
    }
}

$manifest = [ordered]@{
    schemaVersion = 1
    product = 'FifScreen'
    productVersion = $Version
    versionCode = $VersionCode
    architecture = 'x64'
    buildUtc = (Get-Date).ToUniversalTime().ToString('o')
    driver = [ordered]@{
        flavor = $DriverFlavor
        version = $driverVersion
        catalogSigner = if ($catalogSignature.SignerCertificate) { $catalogSignature.SignerCertificate.Subject } else { '' }
    }
    android = [ordered]@{
        applicationId = 'com.fif.screen'
        versionName = $Version
        versionCode = $VersionCode
        packageFlavor = if ($DriverFlavor -eq 'Production') { 'release' } else { 'debug' }
        signer = $apkSignerSubject
    }
    components = [ordered]@{
        host = [Diagnostics.FileVersionInfo]::GetVersionInfo((Join-Path $binDir 'fif-host.exe')).FileVersion
        launcher = [Diagnostics.FileVersionInfo]::GetVersionInfo((Join-Path $binDir 'fif-idd-device-launcher.exe')).FileVersion
        adb = (& (Join-Path $adbStageDir 'adb.exe') version | Select-Object -First 1)
    }
    files = @($payloadEntries)
}
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $runtimeDir 'manifest.json') -Encoding UTF8

$updateConfig = [ordered]@{
    schemaVersion = 1
    product = 'FifScreen'
    channel = $UpdateChannel
    currentVersion = $Version
    architecture = 'x64'
    manifestUrl = $UpdateManifestUrl
    releaseApiUrl = $ReleaseApiUrl
    fallbackManifestUrl = $FallbackManifestUrl
    repositoryUrl = 'https://github.com/fiforz/fif-Screen'
    requireAuthenticode = $DriverFlavor -eq 'Production'
}
$updateConfig | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $stageDir 'update.json') -Encoding UTF8

$editionLabel = if ($DriverFlavor -eq 'Production') { '' } else { 'Developer Preview' }
$outputBaseName = if ($DriverFlavor -eq 'Production') {
    "FifScreen-Setup-$Version-x64"
} else {
    "FifScreen-Setup-$Version-dev-x64"
}

New-Item -ItemType Directory -Force -Path $artifactRoot | Out-Null
$env:FIFSCREEN_VERSION = $Version
$env:FIFSCREEN_STAGE_DIR = $stageDir
$env:FIFSCREEN_OUTPUT_DIR = $artifactRoot
$env:FIFSCREEN_OUTPUT_BASENAME = $outputBaseName
$env:FIFSCREEN_DRIVER_FLAVOR = $DriverFlavor
$env:FIFSCREEN_EDITION_LABEL = $editionLabel
$env:FIFSCREEN_UPDATE_CHANNEL = $UpdateChannel
$env:FIFSCREEN_RELEASE_API_URL = $ReleaseApiUrl

Invoke-Checked -FilePath $Iscc -Arguments @((Join-Path $RepoRoot 'packaging\FifScreen.iss')) -Description 'Compile FifScreen Setup'

$setupPath = Join-Path $artifactRoot "$outputBaseName.exe"
if (-not (Test-Path -LiteralPath $setupPath)) {
    throw "Setup output was not created: $setupPath"
}
Assert-FileVersion -Path $setupPath -Component 'Windows Setup'
Sign-WindowsFile -Path $setupPath

$setupHash = (Get-FileHash -LiteralPath $setupPath -Algorithm SHA256).Hash
$remoteInstallerUrl = if ($UpdateBaseUrl) {
    $UpdateBaseUrl.TrimEnd('/') + '/' + [IO.Path]::GetFileName($setupPath)
} else {
    ''
}
$feed = [ordered]@{
    schemaVersion = 1
    product = 'FifScreen'
    channel = $UpdateChannel
    version = $Version
    architecture = 'x64'
    installerUrl = $remoteInstallerUrl
    sha256 = $setupHash
    releaseNotesUrl = ''
}
$feedPath = Join-Path $artifactRoot ("FifScreen-update-{0}.json" -f $feed.channel)
$feed | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $feedPath -Encoding UTF8

$releaseNotesPath = Join-Path $RepoRoot "docs\releases\v$Version.md"
$releaseNotes = if (Test-Path -LiteralPath $releaseNotesPath) {
    [IO.File]::ReadAllText((Resolve-Path -LiteralPath $releaseNotesPath).Path, [Text.UTF8Encoding]::new($false))
} else {
    "FifScreen $Version"
}
$latestManifest = [ordered]@{
    schemaVersion = 1
    product = 'FifScreen'
    channel = $UpdateChannel
    version = $Version
    architecture = 'x64'
    publishedAt = (Get-Date).ToUniversalTime().ToString('o')
    releaseNotes = $releaseNotes
    installerName = [IO.Path]::GetFileName($setupPath)
    installerUrl = "https://github.com/fiforz/fif-Screen/releases/download/v$Version/$([IO.Path]::GetFileName($setupPath))"
    sha256 = $setupHash
    releaseUrl = "https://github.com/fiforz/fif-Screen/releases/tag/v$Version"
}
$updatesDir = Join-Path $RepoRoot 'updates'
New-Item -ItemType Directory -Force -Path $updatesDir | Out-Null
$latestManifestPath = Join-Path $updatesDir "latest-$UpdateChannel.json"
$latestManifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $latestManifestPath -Encoding UTF8

$setupSignature = Get-AuthenticodeSignature -LiteralPath $setupPath
$report = [ordered]@{
    result = 'PASS'
    productVersion = $Version
    driverFlavor = $DriverFlavor
    setupPath = $setupPath
    setupSize = (Get-Item -LiteralPath $setupPath).Length
    setupSha256 = $setupHash
    setupSignatureStatus = [string]$setupSignature.Status
    updateFeedPath = $feedPath
    fallbackManifestPath = $latestManifestPath
    stagePath = $stageDir
}
$reportPath = Join-Path $artifactRoot "FifScreen-Setup-$Version-build-report.json"
$report | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $reportPath -Encoding UTF8

$report | Format-List
