# Driver Toolchain

Date: 2026-07-07

## Final Toolchain Used

The working path is:

- Visual Studio Community 2026 18.7.2.
- VS DriverKit build integration installed through Visual Studio Installer.
- VS Spectre libraries for MSVC v14.51 installed through Visual Studio Installer.
- WDK and SDK content restored from NuGet package version `10.0.28000.1839`.
- Build target: Debug x64 UMDF IddCx driver.

This is different from the initial plan to use WDK MSI 26100. The MSI path was attempted and failed. The NuGet WDK path was then used because the official `microsoft/Windows-driver-samples` repository supports that mode.

## Failed Path

Command attempted:

```powershell
winget install --id Microsoft.WindowsWDK.10.0.26100 --exact --accept-package-agreements --accept-source-agreements
```

Result:

- Installer failed with exit code `15605`.
- WDK logs under `C:\Users\29989\AppData\Local\Temp\wdk`.
- Root cause evidence: `HttpSendRequest` failed while resolving Microsoft fwlink payloads.

No driver installation, certificate installation, boot setting, or reboot was performed.

## Working Path

NuGet package restore:

```powershell
nuget restore windows-driver\packages.config -PackagesDirectory windows-driver\packages -NonInteractive -Verbosity normal
```

Packages:

- `Microsoft.Windows.SDK.CPP` `10.0.28000.1839`
- `Microsoft.Windows.SDK.CPP.x64` `10.0.28000.1839`
- `Microsoft.Windows.SDK.CPP.arm64` `10.0.28000.1839`
- `Microsoft.Windows.WDK.x64` `10.0.28000.1839`
- `Microsoft.Windows.WDK.arm64` `10.0.28000.1839`

VS components added:

```powershell
setup.exe modify --installPath "D:\SoftWare\Visual Studio\app" --add Component.Microsoft.Windows.DriverKit --add Component.Microsoft.Windows.DriverKit.BuildTools --quiet --norestart
setup.exe modify --installPath "D:\SoftWare\Visual Studio\app" --add Microsoft.VisualStudio.Component.VC.14.51.x86.x64.Spectre --add Microsoft.VisualStudio.Component.VC.Runtimes.x86.x64.Spectre --quiet --norestart
```

Validation:

- Official Windows-driver-samples `video.indirectdisplay` built successfully.
- Project `FifIddDriver` built successfully.
- `infverif /u /v` reports `INF is VALID`.

## Explicit Non-Actions

- Did not install the driver.
- Did not run `pnputil /add-driver`.
- Did not enable test signing.
- Did not modify Secure Boot.
- Did not install a trusted test certificate.
- Did not reboot.
