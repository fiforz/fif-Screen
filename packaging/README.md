# FifScreen Packaging

`scripts/build-installer.ps1` creates a single versioned x64 Windows installer.

The installer contains:

- statically linked Windows Host and software-device launcher;
- FifScreen indirect display driver package;
- Android platform-tools ADB runtime;
- versioned Android APK;
- control panel and maintenance scripts;
- update manifest configuration and complete uninstall cleanup.

The target PC does not need Visual Studio, the WDK, CMake, Gradle, a JDK, the
Android SDK, or a Visual C++ redistributable. The Windows executables use the
static MSVC runtime, and the required ADB files are part of the payload.

## Development build

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build-installer.ps1
```

The output is written to `artifacts\installer`. On the first **Start Display**,
the control panel installs the bundled APK on an authorized USB-debugging
device. Later Windows package upgrades also upgrade an older installed APK.

The development build contains a test-signed driver. Setup requires an explicit
warning confirmation (or `/ALLOWTESTDRIVER=1` for silent installs), imports only
the matching public certificate, and never changes BCD or Secure Boot.

## Production build gate

A production build must supply:

1. a Microsoft-signed FifScreen driver package;
2. a release-signed Android APK;
3. an Authenticode code-signing certificate for Setup and Windows binaries;
4. an HTTPS update manifest URL.

The build script rejects `-DriverFlavor Production` when the driver catalog is
not signed by Microsoft or the required release inputs are missing.

The same fixed Inno Setup `AppId` is used for every version, so installing a
newer package performs an in-place upgrade and preserves a single uninstall
entry.

## Update and uninstall contracts

The installed update shortcut reads `update.json`, requires an HTTPS manifest,
checks product, channel, architecture, version, and SHA-256, and requires a
valid Authenticode signature on production-channel installers.

Uninstall stops the installed runtime, removes the FifScreen software device
and driver packages, removes only certificates originally added by Setup, and
deletes FifScreen logs. It also removes the APK and ADB reverse ports from every
connected authorized Android device. An APK on a disconnected phone cannot be
removed by the Windows uninstaller and must be uninstalled on that phone.

The bundled Simplified Chinese Inno Setup translation is distributed under its
MIT license in `packaging\licenses`.
