param(
    [Parameter(Mandatory = $true)]
    [string]$ControlScript
)

$ErrorActionPreference = 'Stop'

$tokens = $null
$parseErrors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile(
    $ControlScript,
    [ref]$tokens,
    [ref]$parseErrors
)
if ($parseErrors.Count -gt 0) {
    throw "Control script has parse errors: $($parseErrors[0].Message)"
}

function Get-ControlFunctionText {
    param([string]$Name)

    $functionAst = $ast.Find({
        param($node)
        $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
            $node.Name -eq $Name
    }, $true)
    if ($null -eq $functionAst) {
        throw "Missing function in control script: $Name"
    }
    return $functionAst.Extent.Text
}

Invoke-Expression (Get-ControlFunctionText -Name 'Invoke-UiAction')
Invoke-Expression (Get-ControlFunctionText -Name 'Get-RequestedConnectionSettings')
Invoke-Expression (Get-ControlFunctionText -Name 'Select-VersionedExecutable')

function Add-Log {
    param([string]$Message)
    throw "Unexpected UI action failure: $Message"
}

$script:Action = 'Gui'
$script:ConnectionMode = 'Usb'
$script:PairingPin = ''
$script:lanModeRadio = [pscustomobject]@{ Checked = $true }
$script:pinInput = [pscustomobject]@{ Text = '2468' }
$script:result = $null

Invoke-UiAction -Operation {
    $script:result = Get-RequestedConnectionSettings
}

if ($script:result.Mode -ne 'Lan' -or $script:result.Pin -ne '2468') {
    throw "GUI LAN selection was lost: mode=$($script:result.Mode) pinLength=$($script:result.Pin.Length)"
}

$script:lanModeRadio.Checked = $false
$script:pinInput.Text = ''
Invoke-UiAction -Operation {
    $script:result = Get-RequestedConnectionSettings
}

if ($script:result.Mode -ne 'Usb' -or $script:result.Pin -ne '') {
    throw "GUI USB selection was lost: mode=$($script:result.Mode)"
}

$script:AppVersion = '0.5.2'
$candidateRoot = Join-Path ([IO.Path]::GetTempPath()) ("fifscreen-control-test-" + [Guid]::NewGuid())
New-Item -ItemType Directory -Path $candidateRoot | Out-Null
try {
    $olderCandidate = Join-Path $candidateRoot 'older.exe'
    $newerCandidate = Join-Path $candidateRoot 'newer.exe'
    New-Item -ItemType File -Path $olderCandidate, $newerCandidate | Out-Null
    (Get-Item -LiteralPath $olderCandidate).LastWriteTimeUtc = [DateTime]::UtcNow.AddMinutes(-2)
    (Get-Item -LiteralPath $newerCandidate).LastWriteTimeUtc = [DateTime]::UtcNow.AddMinutes(-1)

    function Test-ExecutableVersion {
        param([string]$Path, [string]$ExpectedVersion)
        return (Test-Path -LiteralPath $Path) -and $ExpectedVersion -eq '0.5.2'
    }

    $selected = Select-VersionedExecutable -Component 'test component' `
        -Candidates @($olderCandidate, $newerCandidate)
    if ($selected -ne $newerCandidate) {
        throw "Newest same-version executable was not selected: $selected"
    }
} finally {
    Remove-Item -LiteralPath $candidateRoot -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host 'control script GUI connection mode tests passed'
