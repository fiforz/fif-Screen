# Environment Audit

Date: 2026-07-07

## Repository Reality

This repository is no longer only a skeleton. Current verified state:

- Windows Host has a clean CMake/Ninja build under `build/stage-host-clean`.
- Protocol has C++ tests and shared binary test vectors under `protocol/test-vectors`.
- Android has a Gradle Wrapper, project-local Android SDK, unit tests, and a debug APK.
- Driver has a real UMDF IddCx WDK project, restored WDK NuGet packages, and a Debug x64 package build.

Not verified:

- No Android hardware install or launch was performed because no device is connected.
- No Windows display driver install was performed.
- No Secure Boot, test-signing, boot option, reboot, or trusted certificate action was performed.

## Host Tools

- Visual Studio: `D:\SoftWare\Visual Studio\app`, Visual Studio Community 2026 18.7.2.
- MSVC: 19.51 / toolset 14.51.
- MSBuild: available through Visual Studio.
- CMake: Visual Studio bundled CMake 4.3.1-msvc1.
- Ninja: Visual Studio bundled Ninja 1.13.2.
- Windows SDK: `D:\Windows Kits\10`, SDK include version `10.0.26100.0`.
- NuGet: installed through winget at `C:\Users\29989\AppData\Local\Microsoft\WinGet\Links\nuget.exe`.
- PowerShell 7: winget install failed with `0x80070422`; portable PowerShell 7.6.3 was downloaded to `tools/powershell/7.6.3`.

## Android Toolchain

- JDK: Eclipse Temurin 17.0.19 at `C:\Program Files\Eclipse Adoptium\jdk-17.0.19.10-hotspot`.
- Android SDK: project-local at `tools/android-sdk`.
- Android command-line tools: `cmdline-tools/latest`, sdkmanager 20.0.
- Android platform: `platforms;android-35`.
- Android build tools: `build-tools;35.0.0`.
- ADB: `tools/android-sdk/platform-tools/adb.exe`, version 37.0.0-14910828.
- Gradle Wrapper: Gradle 8.9. Current `distributionUrl` points at the local zip in `tools/downloads/gradle-8.9-bin.zip` because online wrapper URL validation failed earlier.

## Driver Toolchain

- WDK MSI 26100 via winget failed with installer exit code `15605`.
- Root cause in WDK MSI logs: Microsoft fwlink/source resolution failed with `HttpSendRequest` while downloading WDK MSI payloads.
- Official Windows-driver-samples NuGet WDK path succeeded.
- VS WDK component was added through Visual Studio Installer:
  - `Component.Microsoft.Windows.DriverKit`
  - `Component.Microsoft.Windows.DriverKit.BuildTools`
- VS Spectre libraries were added:
  - `Microsoft.VisualStudio.Component.VC.14.51.x86.x64.Spectre`
  - `Microsoft.VisualStudio.Component.VC.Runtimes.x86.x64.Spectre`
- Project WDK packages are restored under `windows-driver/packages`, version `10.0.28000.1839`.
- Driver headers verified in NuGet package:
  - `IddCx.h`: `windows-driver/packages/Microsoft.Windows.WDK.x64.10.0.28000.1839/c/Include/10.0.28000.0/um/iddcx/1.6/IddCx.h`
  - `wdf.h`: `windows-driver/packages/Microsoft.Windows.WDK.x64.10.0.28000.1839/c/Include/wdf/umdf/2.25/wdf.h`

## Current Human Blocks

- Android hardware verification: connect a device, enable USB debugging, and accept the RSA prompt.
- Driver installation/hardware verification: explicit approval is required before any install, trusted signing, test-signing, Secure Boot, certificate, or reboot action.
