param(
    [Parameter(Mandatory = $true)]
    [string]$InstallDir,

    [switch]$AllowTestDriver
)

$ErrorActionPreference = 'Stop'

$runtimeDir = Join-Path $InstallDir 'runtime'
$driverDir = Join-Path $runtimeDir 'driver'
$manifestPath = Join-Path $runtimeDir 'manifest.json'
$infPath = Join-Path $driverDir 'FifIddDriver.inf'
$catPath = Join-Path $driverDir 'FifIddDriver.cat'
$certPath = Join-Path $driverDir 'FifIddDriver.cer'
$registryPath = 'HKLM:\Software\FifScreen'
$logDir = Join-Path $env:ProgramData 'FifScreen\logs'
$logPath = Join-Path $logDir 'driver-install.log'

New-Item -ItemType Directory -Force -Path $logDir | Out-Null

function Write-InstallLog {
    param([string]$Message)
    $line = '[{0}] {1}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'), $Message
    Add-Content -LiteralPath $logPath -Value $line -Encoding UTF8
    Write-Host $line
}

function Assert-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Administrator rights are required to install the display driver.'
    }
}

function Remove-CertificateIfImported {
    param(
        [string]$Thumbprint,
        [bool]$ImportedRoot,
        [bool]$ImportedPublisher
    )

    if ($ImportedRoot) {
        Remove-Item -LiteralPath "Cert:\LocalMachine\Root\$Thumbprint" -Force -ErrorAction SilentlyContinue
    }
    if ($ImportedPublisher) {
        Remove-Item -LiteralPath "Cert:\LocalMachine\TrustedPublisher\$Thumbprint" -Force -ErrorAction SilentlyContinue
    }
}

Assert-Administrator

foreach ($required in @($manifestPath, $infPath, $catPath)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Required driver payload is missing: $required"
    }
}

$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$driverFlavor = [string]$manifest.driver.flavor
$driverVersion = [string]$manifest.driver.version
$existingProperties = Get-ItemProperty -Path $registryPath -ErrorAction SilentlyContinue
$ownedRootCertificates = @($existingProperties.InstallerOwnedRootCertificates) |
    Where-Object { $_ -match '^[0-9A-Fa-f]{40}$' }
$ownedPublisherCertificates = @($existingProperties.InstallerOwnedPublisherCertificates) |
    Where-Object { $_ -match '^[0-9A-Fa-f]{40}$' }
$legacyThumbprint = [string]$existingProperties.TestCertificateThumbprint
if ($legacyThumbprint -match '^[0-9A-Fa-f]{40}$') {
    if ([int]$existingProperties.TestCertificateImportedRoot -eq 1) {
        $ownedRootCertificates += $legacyThumbprint
    }
    if ([int]$existingProperties.TestCertificateImportedPublisher -eq 1) {
        $ownedPublisherCertificates += $legacyThumbprint
    }
}
$ownedRootCertificates = @($ownedRootCertificates | Select-Object -Unique)
$ownedPublisherCertificates = @($ownedPublisherCertificates | Select-Object -Unique)
$importedRoot = $false
$importedPublisher = $false
$certificateThumbprint = ''

New-Item -Path $registryPath -Force | Out-Null
New-ItemProperty -Path $registryPath -Name 'InstallDir' -Value $InstallDir -PropertyType String -Force | Out-Null
New-ItemProperty -Path $registryPath -Name 'Version' -Value ([string]$manifest.productVersion) -PropertyType String -Force | Out-Null
New-ItemProperty -Path $registryPath -Name 'DriverFlavor' -Value $driverFlavor -PropertyType String -Force | Out-Null
New-ItemProperty -Path $registryPath -Name 'DriverPackageVersion' -Value $driverVersion -PropertyType String -Force | Out-Null

try {
    if ($driverFlavor -eq 'Development') {
        if (-not $AllowTestDriver) {
            throw 'This package contains a test-signed driver. Re-run Setup interactively or pass /ALLOWTESTDRIVER=1.'
        }
        if (-not (Test-Path -LiteralPath $certPath)) {
            throw "Development driver certificate is missing: $certPath"
        }

        $certificate = [Security.Cryptography.X509Certificates.X509Certificate2]::new($certPath)
        $certificateThumbprint = $certificate.Thumbprint
        $catalogSignature = Get-AuthenticodeSignature -LiteralPath $catPath
        if ($null -eq $catalogSignature.SignerCertificate -or
            $catalogSignature.SignerCertificate.Thumbprint -ne $certificateThumbprint) {
            throw 'The bundled certificate does not match the driver catalog signer.'
        }

        if (-not (Test-Path -LiteralPath "Cert:\LocalMachine\Root\$certificateThumbprint")) {
            Import-Certificate -FilePath $certPath -CertStoreLocation 'Cert:\LocalMachine\Root' | Out-Null
            $importedRoot = $true
            $ownedRootCertificates += $certificateThumbprint
        }
        if (-not (Test-Path -LiteralPath "Cert:\LocalMachine\TrustedPublisher\$certificateThumbprint")) {
            Import-Certificate -FilePath $certPath -CertStoreLocation 'Cert:\LocalMachine\TrustedPublisher' | Out-Null
            $importedPublisher = $true
            $ownedPublisherCertificates += $certificateThumbprint
        }

        Write-InstallLog "Trusted development driver certificate $certificateThumbprint"
    }

    $pnpUtil = Join-Path $env:SystemRoot 'System32\pnputil.exe'
    $output = @(& $pnpUtil /add-driver $infPath /install 2>&1)
    $exitCode = $LASTEXITCODE
    foreach ($line in $output) {
        Write-InstallLog ([string]$line)
    }
    if ($exitCode -ne 0) {
        throw "Windows rejected the FifScreen driver package (pnputil exit $exitCode). Production use requires a Microsoft-signed driver."
    }

    $publishedNames = [regex]::Matches(($output -join "`n"), '(?i)\boem\d+\.inf\b') |
        ForEach-Object { $_.Value.ToLowerInvariant() } |
        Select-Object -Unique

    $publishedName = $publishedNames | Select-Object -Last 1
    if (-not $publishedName) {
        try {
            $publishedName = Get-WindowsDriver -Online -All -ErrorAction Stop |
                Where-Object {
                    $_.ProviderName -eq 'FifScreen' -and
                    $_.OriginalFileName -match '(?i)FifIddDriver\.inf$'
                } |
                Sort-Object Date -Descending |
                Select-Object -First 1 -ExpandProperty Driver
        } catch {
            Write-InstallLog "Could not query published driver name: $($_.Exception.Message)"
        }
    }

    if ($publishedName -and $publishedName -match '^oem\d+\.inf$') {
        New-ItemProperty -Path $registryPath -Name 'DriverPublishedName' -Value $publishedName -PropertyType String -Force | Out-Null
        Write-InstallLog "Recorded published driver name $publishedName"
    } else {
        Write-InstallLog 'Driver installed, but the published OEM INF name was not available; uninstall will re-query by provider and original INF.'
    }

    if ($driverFlavor -eq 'Development') {
        $ownedRootCertificates = @($ownedRootCertificates | Select-Object -Unique)
        $ownedPublisherCertificates = @($ownedPublisherCertificates | Select-Object -Unique)
        $currentRootOwned = $ownedRootCertificates -contains $certificateThumbprint
        $currentPublisherOwned = $ownedPublisherCertificates -contains $certificateThumbprint

        New-ItemProperty -Path $registryPath -Name 'TestCertificateThumbprint' -Value $certificateThumbprint -PropertyType String -Force | Out-Null
        New-ItemProperty -Path $registryPath -Name 'TestCertificateImportedRoot' -Value ([int]$currentRootOwned) -PropertyType DWord -Force | Out-Null
        New-ItemProperty -Path $registryPath -Name 'TestCertificateImportedPublisher' -Value ([int]$currentPublisherOwned) -PropertyType DWord -Force | Out-Null
        if ($ownedRootCertificates.Count -gt 0) {
            New-ItemProperty -Path $registryPath -Name 'InstallerOwnedRootCertificates' `
                -Value $ownedRootCertificates -PropertyType MultiString -Force | Out-Null
        }
        if ($ownedPublisherCertificates.Count -gt 0) {
            New-ItemProperty -Path $registryPath -Name 'InstallerOwnedPublisherCertificates' `
                -Value $ownedPublisherCertificates -PropertyType MultiString -Force | Out-Null
        }
    }

    Write-InstallLog "FifScreen driver $driverVersion installation completed"
} catch {
    Write-InstallLog "ERROR: $($_.Exception.Message)"
    Remove-CertificateIfImported -Thumbprint $certificateThumbprint `
        -ImportedRoot $importedRoot -ImportedPublisher $importedPublisher
    throw
}
