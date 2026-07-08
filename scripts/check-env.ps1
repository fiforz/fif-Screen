param(
    [switch]$Json
)

$ErrorActionPreference = 'Continue'
$RepoRoot = Split-Path -Parent $PSScriptRoot
$Results = New-Object System.Collections.Generic.List[object]

function Add-Check {
    param(
        [string]$Category,
        [string]$Name,
        [ValidateSet('OK', 'MISSING', 'WARNING', 'HUMAN ACTION')]
        [string]$Status,
        [string]$Detail = ''
    )

    $Results.Add([pscustomobject]@{
        Category = $Category
        Name = $Name
        Status = $Status
        Detail = $Detail
    })
}

function Get-FirstExistingPath {
    param([string[]]$Paths)
    foreach ($path in $Paths) {
        if ($path -and (Test-Path -LiteralPath $path)) {
            return (Resolve-Path -LiteralPath $path).Path
        }
    }
    return $null
}

function Get-ToolPath {
    param(
        [string]$Name,
        [string[]]$Fallbacks = @()
    )

    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    return Get-FirstExistingPath $Fallbacks
}

function Get-ToolOutput {
    param(
        [string]$Path,
        [string[]]$Arguments
    )

    if (-not $Path) { return '' }
    try {
        return ((& $Path @Arguments 2>&1 | Select-Object -First 3) -join ' ').Trim()
    } catch {
        return $_.Exception.Message
    }
}

function Get-VsInstallations {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) { return @() }

    try {
        $json = & $vswhere -all -products * -format json -include packages 2>$null
        if (-not $json) { return @() }
        return @($json | ConvertFrom-Json)
    } catch {
        return @()
    }
}

function Resolve-AdbPath {
    $projectAdb = Join-Path $RepoRoot 'tools\android-sdk\platform-tools\adb.exe'
    $paths = @(
        $projectAdb,
        $env:FIF_ADB,
        (Join-Path $env:LOCALAPPDATA 'Android\Sdk\platform-tools\adb.exe'),
        'D:\SoftWare\i4Tools\i4Tools9\files\adb\adb.exe'
    )
    $found = Get-FirstExistingPath $paths
    if ($found) { return $found }

    $cmd = Get-Command adb -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    return $null
}

function Add-ToolCheck {
    param(
        [string]$Category,
        [string]$Name,
        [string]$Path,
        [string[]]$VersionArgs = @('--version')
    )

    if ($Path) {
        $version = Get-ToolOutput $Path $VersionArgs
        Add-Check $Category $Name OK "$Path $version"
    } else {
        Add-Check $Category $Name MISSING 'not found'
    }
}

$vs = Get-VsInstallations
$vsRoot = ($vs | Select-Object -First 1).installationPath
$msbuild = Get-FirstExistingPath @(
    (Join-Path $vsRoot 'MSBuild\Current\Bin\MSBuild.exe'),
    (Join-Path $vsRoot 'MSBuild\Microsoft\VC\v180\Bin\MSBuild.exe')
)
$cmake = Get-ToolPath 'cmake' @((Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'))
$ninja = Get-ToolPath 'ninja' @((Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'))
$cl = Get-FirstExistingPath @((Join-Path $vsRoot 'VC\Tools\MSVC\14.51.36244\bin\Hostx64\x64\cl.exe'))
if (-not $cl -and $vsRoot) {
    $cl = Get-ChildItem -Path (Join-Path $vsRoot 'VC\Tools\MSVC') -Recurse -Filter cl.exe -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match '\\bin\\Hostx64\\x64\\cl.exe$' } |
        Sort-Object FullName -Descending |
        Select-Object -First 1 -ExpandProperty FullName
}

Add-ToolCheck 'General' 'git' (Get-ToolPath 'git') @('--version')
Add-ToolCheck 'General' 'cmake' $cmake @('--version')
Add-ToolCheck 'General' 'ninja' $ninja @('--version')
Add-ToolCheck 'General' 'PowerShell 7 portable' (Get-FirstExistingPath @((Join-Path $RepoRoot 'tools\powershell\7.6.3\pwsh.exe'))) @('-NoLogo', '-NoProfile', '-Command', '$PSVersionTable.PSVersion.ToString()')

if ($vsRoot) {
    $vsName = ($vs | Select-Object -First 1).displayName
    $vsVersion = ($vs | Select-Object -First 1).installationVersion
    Add-Check 'Windows Host' 'Visual Studio' OK "$vsName $vsVersion at $vsRoot"
} else {
    Add-Check 'Windows Host' 'Visual Studio' MISSING 'vswhere found no installation'
}
Add-ToolCheck 'Windows Host' 'MSBuild' $msbuild @('-version', '-nologo')
Add-ToolCheck 'Windows Host' 'MSVC cl.exe' $cl @('/Bv')

$hostExe = Join-Path $RepoRoot 'build\stage-host-clean\windows-host\fif-host.exe'
$protocolTestExe = Join-Path $RepoRoot 'build\stage-host-clean\tests\fif-protocol-test.exe'
Add-Check 'Windows Host' 'fif-host.exe' ($(if (Test-Path $hostExe) { 'OK' } else { 'MISSING' })) $hostExe
Add-Check 'Windows Host' 'fif-protocol-test.exe' ($(if (Test-Path $protocolTestExe) { 'OK' } else { 'MISSING' })) $protocolTestExe

$jdk = Get-ToolPath 'java' @('C:\Program Files\Eclipse Adoptium\jdk-17.0.19.10-hotspot\bin\java.exe')
Add-ToolCheck 'Android' 'JDK java' $jdk @('-version')
$detectedJavaHome = $null
if ($jdk) {
    $detectedJavaHome = Split-Path -Parent (Split-Path -Parent $jdk)
}
if ($env:JAVA_HOME) {
    Add-Check 'Android' 'JAVA_HOME' OK $env:JAVA_HOME
} else {
    Add-Check 'Android' 'JAVA_HOME' WARNING 'not set in current shell; build scripts set it when needed'
}

$androidSdk = Join-Path $RepoRoot 'tools\android-sdk'
$sdkmanager = Join-Path $androidSdk 'cmdline-tools\latest\bin\sdkmanager.bat'
$adb = Resolve-AdbPath
Add-Check 'Android' 'Android SDK' ($(if (Test-Path $androidSdk) { 'OK' } else { 'MISSING' })) $androidSdk
$savedJavaHome = $env:JAVA_HOME
if (-not $env:JAVA_HOME -and $detectedJavaHome) {
    $env:JAVA_HOME = $detectedJavaHome
}
Add-ToolCheck 'Android' 'sdkmanager' ($(if (Test-Path $sdkmanager) { $sdkmanager } else { $null })) @('--version')
$env:JAVA_HOME = $savedJavaHome
Add-ToolCheck 'Android' 'adb' $adb @('version')
Add-Check 'Android' 'Gradle Wrapper' ($(if ((Test-Path (Join-Path $RepoRoot 'gradlew.bat')) -and (Test-Path (Join-Path $RepoRoot 'gradle\wrapper\gradle-wrapper.jar'))) { 'OK' } else { 'MISSING' })) (Join-Path $RepoRoot 'gradlew.bat')

$apk = Join-Path $RepoRoot 'android-client\build\outputs\apk\debug\android-client-debug.apk'
Add-Check 'Android' 'debug APK' ($(if (Test-Path $apk) { 'OK' } else { 'MISSING' })) $apk
if ($adb) {
    $rawDevices = & $adb devices -l 2>&1
    $deviceLines = @($rawDevices | Where-Object { $_ -match '^\S+\s+\S+' -and $_ -notmatch '^List of devices' })
    $unauthorized = @($deviceLines | Where-Object { $_ -match '\sunauthorized\s' })
    $online = @($deviceLines | Where-Object { $_ -match '\sdevice\s' })
    if ($online.Count -gt 0) {
        Add-Check 'Android' 'device authorization' OK ($online -join '; ')
    } elseif ($unauthorized.Count -gt 0) {
        Add-Check 'Android' 'device authorization' 'HUMAN ACTION' 'unlock phone and accept USB debugging RSA prompt'
    } else {
        Add-Check 'Android' 'device connection' 'HUMAN ACTION' 'no Android device connected'
    }
}

$wdkComponent = $false
$spectreComponent = $false
foreach ($instance in $vs) {
    foreach ($pkg in @($instance.packages)) {
        if ($pkg.id -in @('Component.Microsoft.Windows.DriverKit', 'Microsoft.Windows.DriverKit', 'Component.Microsoft.Windows.DriverKit.BuildTools', 'Microsoft.VisualStudio.WindowsDriverKit.Build')) {
            $wdkComponent = $true
        }
        if ($pkg.id -in @('Microsoft.VisualStudio.Component.VC.14.51.x86.x64.Spectre', 'Microsoft.VisualStudio.Component.VC.Runtimes.x86.x64.Spectre')) {
            $spectreComponent = $true
        }
    }
}
Add-Check 'Driver' 'VS WDK component' ($(if ($wdkComponent) { 'OK' } else { 'MISSING' })) 'requires Component.Microsoft.Windows.DriverKit'
Add-Check 'Driver' 'VS Spectre libraries' ($(if ($spectreComponent) { 'OK' } else { 'MISSING' })) 'required by WDK sample/project'

$wdkPkg = Join-Path $RepoRoot 'windows-driver\packages\Microsoft.Windows.WDK.x64.10.0.28000.1839'
$iddcx = Join-Path $wdkPkg 'c\Include\10.0.28000.0\um\iddcx\1.6\IddCx.h'
$wdf = Join-Path $wdkPkg 'c\Include\wdf\umdf\2.25\wdf.h'
$infverif = Join-Path $wdkPkg 'c\tools\10.0.28000.0\x64\infverif.exe'
Add-Check 'Driver' 'WDK NuGet package' ($(if (Test-Path $wdkPkg) { 'OK' } else { 'MISSING' })) $wdkPkg
Add-Check 'Driver' 'IddCx.h' ($(if (Test-Path $iddcx) { 'OK' } else { 'MISSING' })) $iddcx
Add-Check 'Driver' 'UMDF wdf.h' ($(if (Test-Path $wdf) { 'OK' } else { 'MISSING' })) $wdf
Add-ToolCheck 'Driver' 'InfVerif' ($(if (Test-Path $infverif) { $infverif } else { $null })) @('/?')

$driverDll = Join-Path $RepoRoot 'windows-driver\FifIddDriver\x64\Debug\FifIddDriver.dll'
$driverInf = Join-Path $RepoRoot 'windows-driver\FifIddDriver\x64\Debug\FifIddDriver.inf'
$driverCat = Join-Path $RepoRoot 'windows-driver\FifIddDriver\x64\Debug\FifIddDriver\fifidddriver.cat'
Add-Check 'Driver' 'FifIddDriver.dll' ($(if (Test-Path $driverDll) { 'OK' } else { 'MISSING' })) $driverDll
Add-Check 'Driver' 'FifIddDriver.inf' ($(if (Test-Path $driverInf) { 'OK' } else { 'MISSING' })) $driverInf
Add-Check 'Driver' 'FifIddDriver.cat' ($(if (Test-Path $driverCat) { 'OK' } else { 'MISSING' })) $driverCat
Add-Check 'Driver' 'driver installation' 'HUMAN ACTION' 'not attempted; requires explicit approval, trusted signing path, and likely elevated shell'

if ($Json) {
    [pscustomobject]@{
        Timestamp = (Get-Date).ToString('o')
        RepoRoot = $RepoRoot
        Results = $Results
    } | ConvertTo-Json -Depth 6
    exit
}

foreach ($item in $Results) {
    $detail = if ($item.Detail) { " - $($item.Detail)" } else { '' }
    "[{0}] {1}: {2}{3}" -f $item.Status, $item.Category, $item.Name, $detail
}
