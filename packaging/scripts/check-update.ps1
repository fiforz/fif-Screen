param(
    [string]$InstallDir = (Split-Path -Parent $PSScriptRoot),
    [string]$ManifestUrl = ''
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms

function Show-Message {
    param([string]$Text, [string]$Title = 'FifScreen Update')
    [void][System.Windows.Forms.MessageBox]::Show(
        $Text,
        $Title,
        [System.Windows.Forms.MessageBoxButtons]::OK,
        [System.Windows.Forms.MessageBoxIcon]::Information
    )
}

try {
    $configPath = Join-Path $InstallDir 'update.json'
    $config = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
    $currentVersion = [version]$config.currentVersion
    if (-not $ManifestUrl) {
        $ManifestUrl = [string]$config.manifestUrl
    }
    if (-not $ManifestUrl) {
        Show-Message 'The update interface is installed, but no update manifest URL is configured yet.'
        exit 0
    }

    $manifestUri = [Uri]$ManifestUrl
    if (-not $manifestUri.IsAbsoluteUri -or $manifestUri.Scheme -ne 'https') {
        throw 'The update manifest URL must use HTTPS.'
    }

    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    $remote = Invoke-RestMethod -Uri $ManifestUrl -UseBasicParsing
    if ([string]$remote.product -ne [string]$config.product) {
        throw "Unexpected update product: $($remote.product)"
    }
    if ([string]$remote.channel -ne [string]$config.channel) {
        throw "Unexpected update channel: $($remote.channel)"
    }
    if ([string]$remote.architecture -ne 'x64') {
        throw "Unsupported update architecture: $($remote.architecture)"
    }
    $newVersion = [version]$remote.version
    if ($newVersion -le $currentVersion) {
        Show-Message "FifScreen $currentVersion is up to date."
        exit 0
    }

    if (-not $remote.installerUrl -or [string]$remote.sha256 -notmatch '^[0-9A-Fa-f]{64}$') {
        throw 'The remote update manifest is missing installerUrl or a valid SHA-256 hash.'
    }
    $installerUri = [Uri][string]$remote.installerUrl
    if (-not $installerUri.IsAbsoluteUri -or $installerUri.Scheme -ne 'https') {
        throw 'The update installer URL must use HTTPS.'
    }

    $downloadPath = Join-Path $env:TEMP "FifScreen-Setup-$newVersion-x64.exe"
    Invoke-WebRequest -Uri ([string]$remote.installerUrl) -OutFile $downloadPath -UseBasicParsing
    $actualHash = (Get-FileHash -LiteralPath $downloadPath -Algorithm SHA256).Hash
    if ($actualHash -ne ([string]$remote.sha256).ToUpperInvariant()) {
        Remove-Item -LiteralPath $downloadPath -Force -ErrorAction SilentlyContinue
        throw 'Downloaded installer hash verification failed.'
    }

    if ([bool]$config.requireAuthenticode) {
        $signature = Get-AuthenticodeSignature -LiteralPath $downloadPath
        if ($signature.Status -ne 'Valid') {
            Remove-Item -LiteralPath $downloadPath -Force -ErrorAction SilentlyContinue
            throw "Downloaded installer signature verification failed: $($signature.Status)"
        }
    }

    Start-Process -FilePath $downloadPath -ArgumentList '/UPDATE=1' -Verb RunAs
} catch {
    [void][System.Windows.Forms.MessageBox]::Show(
        $_.Exception.Message,
        'FifScreen Update Error',
        [System.Windows.Forms.MessageBoxButtons]::OK,
        [System.Windows.Forms.MessageBoxIcon]::Error
    )
    exit 1
}
