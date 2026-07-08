# Official IddSample Build

Date: 2026-07-07

## Source

Repository:

```text
microsoft/Windows-driver-samples
```

Commit:

```text
2ee527bfeb0aeb6be11f0a8b6dce4011b358ce89
```

Local path:

```text
tools/research/_repos/Windows-driver-samples-2ee527bfeb0aeb6be11f0a8b6dce4011b358ce89
```

License:

```text
MS-PL
```

## Build Preparation

The official repo `packages.config` was restored with NuGet to the repo-local `packages` directory. The restored WDK/SDK package version is `10.0.28000.1839`.

PowerShell 7 was required by `Build-Samples.ps1`. `winget install Microsoft.PowerShell` failed with `0x80070422`, so portable PowerShell 7.6.3 was used from `tools/powershell/7.6.3/pwsh.exe`.

VS WDK and Spectre components were required before the sample could build.

## Failed Intermediate Attempts

Wrong sample filter:

```powershell
./Build-Samples.ps1 -Samples 'video.IndirectDisplay.IddSampleDriver' -Configurations Debug -Platforms x64 -ThrottleLimit 1
```

Result: official script could not find a solution under the child driver directory. The correct sample name is `video.indirectdisplay`, because the `.sln` is one directory higher.

Missing Spectre libraries:

```text
error MSB8040
```

Root cause: the sample projects set `Driver_SpectreMitigation` to `Spectre`. Installing MSVC v14.51 Spectre libraries fixed this.

## Successful Build Command

```powershell
tools\powershell\7.6.3\pwsh.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -Command "Set-Location 'tools/research/_repos/Windows-driver-samples-2ee527bfeb0aeb6be11f0a8b6dce4011b358ce89'; ./Build-Samples.ps1 -Samples 'video.indirectdisplay' -Configurations Debug -Platforms x64 -ThrottleLimit 1"
```

Result:

```text
Environment: NuGet
Build Number: 28000
NuGet Version: 10.0.28000.1839
WDK VS Component: 10.0.26586.0
Samples: 1
Succeeded: 1
Failed: 0
```

## Output Evidence

```text
IddSampleDriver.dll | 64128 bytes | 6555BA7F1B2151EA3E92BCCF514D96B2746EAE7F9FC2503435953AD13D8D11F3
iddsampledriver.cat | 2708 bytes | B8DF34DBE039C93DFB9F678752224CD687AF3181439CCC6D8982E4B2290616BE
IddSampleDriver.inf | 4130 bytes | E1ABA2B23AC47DB1A72A5D1974D7FAD7FEF92AF8B6D69B3E349B7C407EE48431
IddSampleApp.exe | 29184 bytes | 40390C23CC9AF1BEDE4D751F8FEC9177911F00686E4B9A5402A630A102A5211C
```

Log report:

```text
_logs/_overview.csv: "video.indirectdisplay","Succeeded"
```

No official sample driver was installed.
