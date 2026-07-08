[CmdletBinding()]
param(
    [switch]$ConfirmGate,
    [string]$DriverPackageDir,
    [switch]$AllowUntrustedTestCertificate
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot

if (-not $DriverPackageDir) {
    $DriverPackageDir = Join-Path $repoRoot 'windows-driver\FifIddDriver\x64\Debug\FifIddDriver'
}

$expected = @{
    'FifIddDriver.inf' = '38D26D91B8628128C5750F26B3917F864CC5EE8F0AB588E35EA433C637E2D930'
    'fifidddriver.cat' = 'D2E2F5A99648884B22F5109FF308BB340F0BA4453D5F58B3D660120581008F81'
    'FifIddDriver.dll' = 'ABE48FFE52647223B95ECA4B35D32258114297EB113B63A6D13DD0F62AAE2683'
}

function Test-IsAdministrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal] $identity
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Assert-ExpectedHash {
    param(
        [string]$Path,
        [string]$ExpectedHash
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        throw "Required file not found: $Path"
    }

    $actual = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
    if ($actual -ne $ExpectedHash.ToUpperInvariant()) {
        throw "Hash mismatch for $Path. Expected $ExpectedHash, got $actual."
    }

    Write-Host "[OK] SHA-256 $Path $actual"
}

function Get-TestSigningEnabled {
    try {
        $boot = & bcdedit /enum 2>&1
        return [bool](@($boot | Where-Object { $_ -match 'testsigning\s+Yes' }).Count)
    } catch {
        Write-Warning "Could not read TESTSIGNING state: $($_.Exception.Message)"
        return $false
    }
}

$inf = Join-Path $DriverPackageDir 'FifIddDriver.inf'
$cat = Join-Path $DriverPackageDir 'fifidddriver.cat'
$dll = Join-Path $DriverPackageDir 'FifIddDriver.dll'

Write-Host "FifScreen driver install test gate"
Write-Host "Driver package: $DriverPackageDir"
Write-Host "Administrator: $(Test-IsAdministrator)"
Write-Host "TESTSIGNING enabled: $(Get-TestSigningEnabled)"
Write-Host "This script does not modify Secure Boot, TESTSIGNING, BitLocker, BCD, certificates, or reboot state."

Assert-ExpectedHash -Path $inf -ExpectedHash $expected['FifIddDriver.inf']
Assert-ExpectedHash -Path $cat -ExpectedHash $expected['fifidddriver.cat']
Assert-ExpectedHash -Path $dll -ExpectedHash $expected['FifIddDriver.dll']

$catSignature = Get-AuthenticodeSignature -LiteralPath $cat
Write-Host "Catalog signature status: $($catSignature.Status)"
Write-Host "Catalog signature message: $($catSignature.StatusMessage)"

Write-Host "Planned install command:"
Write-Host "  pnputil /add-driver `"$inf`" /install"

if (-not $ConfirmGate) {
    Write-Host "No driver modifications executed. Re-run only after explicit approval:"
    Write-Host "  powershell -ExecutionPolicy Bypass -File scripts\driver-install-test.ps1 -ConfirmGate"
    exit 2
}

if (-not (Test-IsAdministrator)) {
    throw "Administrator shell required for pnputil driver installation."
}

if ($catSignature.Status -ne 'Valid' -and -not $AllowUntrustedTestCertificate) {
    throw "Catalog signature is not trusted on this machine. Resolve the signing gate or pass -AllowUntrustedTestCertificate only for an explicitly approved test-signing run."
}

& pnputil /add-driver $inf /install
exit $LASTEXITCODE

