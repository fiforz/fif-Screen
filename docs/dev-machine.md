# Dev Machine Snapshot

Date: 2026-07-07

## Windows

- OS: Microsoft Windows 10 Pro, version 10.0.19045, 64-bit.
- Machine: ASUS MacPro7,1.

## GPU

- Todesk Virtual Display Adapter, driver 16.44.2.509.
- AMD Radeon RX 5700, driver 32.0.21030.2001, adapter RAM about 4 GB.

Initial encoder direction: keep `IVideoEncoder` vendor-neutral. This machine has AMD hardware, so AMF is the likely hardware path after the transport and protocol are stable. Media Foundation hardware H.264 should be tested first because it has the smallest dependency surface.

## Tooling Found

- Git: `D:\SoftWare\Git\cmd\git.exe`, version 2.50.0.windows.2.
- Visual Studio Community 2026: `D:\SoftWare\Visual Studio\app`, version 18.7.2.
- MSVC: `D:\SoftWare\Visual Studio\app\VC\Tools\MSVC\14.51.36231\bin\Hostx64\x64\cl.exe`, product version 14.51.36248.0.
- MSBuild: `D:\SoftWare\Visual Studio\app\MSBuild\Current\Bin\MSBuild.exe`.
- CMake: `D:\SoftWare\Visual Studio\app\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`, version 4.3.1-msvc1.
- Ninja: `D:\SoftWare\Visual Studio\app\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe`.
- Windows SDK: `D:\Windows Kits\10`, include version 10.0.26100.0.
- adb: `D:\SoftWare\i4Tools\i4Tools9\files\adb\adb.exe`, version 29.0.4-5871666. It is not on PATH.

## Missing From PATH

- GitHub CLI `gh`.
- CMake.
- Ninja.
- MSVC `cl`.
- MSBuild.
- Windows SDK / WDK under the standard Windows Kits path.
- WDK headers: `iddcx.h` and `wdf.h` were not found under `D:\Windows Kits\10`.
- Android-suitable JDK `java`.
- Gradle.
- Android `adb` on PATH.
- `ANDROID_HOME`, `ANDROID_SDK_ROOT`, `JAVA_HOME`.

## Impact

- Windows Host C++ can be built by first running `D:\SoftWare\Visual Studio\app\VC\Auxiliary\Build\vcvars64.bat`.
- Windows Driver code cannot be compiled until Visual Studio, Windows SDK, and WDK are installed.
- Android app cannot be built or device-tested until JDK, Android SDK, Gradle, and adb are installed.
- USB MVP cannot complete runtime validation until adb can detect an authorized Android device.

## Minimal Install Path

- WDK matching the installed Windows SDK.
- Android Studio or command line Android SDK with platform-tools.
- JDK 17 or newer matching the Android Gradle Plugin requirement.
- Optional: GitHub CLI for authenticated repository search and higher API limits.

No security settings, test-signing settings, certificates, or reboot-required changes were made.
