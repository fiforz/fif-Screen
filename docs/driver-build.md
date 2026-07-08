# FifIddDriver Build

Date: 2026-07-07

## Project Created

Created real WDK project files under `windows-driver/FifIddDriver`:

- `FifIddDriver.sln`
- `FifIddDriver.vcxproj`
- `FifIddDriver.vcxproj.filters`

Created project-local WDK package files:

- `windows-driver/packages.config`
- `windows-driver/Directory.Build.props`

The project uses:

- `WindowsUserModeDriver10.0`
- UMDF 2.25
- IddCx
- Debug x64
- WDK/SDK NuGet packages `10.0.28000.1839`

## Driver Implementation State

The current driver is a minimal UMDF IddCx implementation:

- initializes IddCx from `EvtDeviceAdd`;
- initializes one adapter from D0 entry;
- reports one EDID-less monitor;
- advertises `1920x1080@60` and `1280x720@60`;
- handles mode commit as a no-op;
- rejects assigned swapchains by deleting them because video frame transport is out of scope for this stage.

This is compile-verified driver code, not a hardware-verified extended display implementation.

## Commands Executed

Restore packages:

```powershell
nuget restore windows-driver\packages.config -PackagesDirectory windows-driver\packages -NonInteractive -Verbosity normal
```

Build:

```powershell
cmd.exe /d /c "call ""D:\SoftWare\Visual Studio\app\VC\Auxiliary\Build\vcvars64.bat"" && msbuild ""D:\Documents\fif-Screen\windows-driver\FifIddDriver\FifIddDriver.sln"" /m:1 /p:Configuration=Debug /p:Platform=x64 /p:RunCodeAnalysis=false /v:minimal"
```

Result:

```text
FifIddDriver.vcxproj -> D:\Documents\fif-Screen\windows-driver\FifIddDriver\x64\Debug\FifIddDriver.dll
Driver is 'Universal'.
Signability test complete.
Errors: None
Warnings: None
Catalog generation complete.
Successfully signed: FifIddDriver.dll
Successfully signed: fifidddriver.cat
```

INF verification:

```powershell
windows-driver\packages\Microsoft.Windows.WDK.x64.10.0.28000.1839\c\tools\10.0.28000.0\x64\infverif.exe /u /v windows-driver\FifIddDriver\x64\Debug\FifIddDriver.inf
```

Result:

```text
INF is VALID
```

## Output Evidence

```text
FifIddDriver.dll | 28040 bytes | ABE48FFE52647223B95ECA4B35D32258114297EB113B63A6D13DD0F62AAE2683
FifIddDriver.inf | 1506 bytes | 38D26D91B8628128C5750F26B3917F864CC5EE8F0AB588E35EA433C637E2D930
fifidddriver.cat | 2672 bytes | D2E2F5A99648884B22F5109FF308BB340F0BA4453D5F58B3D660120581008F81
FifIddDriver.pdb | 1462272 bytes | B58217041906AB151DA29A195CF7A3BF66D1FB3E32471C9B653DF1FA21DBE9AC
```

## Signature Status

`signtool verify /pa /v` was executed for the DLL and CAT.

Result:

```text
Signing Certificate Chain:
Issued to: WDKTestCert 29989,134279100762949792
Issued by: WDKTestCert 29989,134279100762949792

SignTool Error:
A certificate chain processed, but terminated in a root certificate which is not trusted by the trust provider.
```

Meaning:

- The package was build-signed by WDK tooling.
- The signing chain is not trusted for install.
- This is not hardware verification.
- Installation remains blocked until the user explicitly approves the signing/install path.
