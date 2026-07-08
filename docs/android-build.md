# Android Build

Date: 2026-07-08

## Toolchain

- JDK: Eclipse Temurin 17.0.19.
- Android Gradle Plugin: 8.7.3.
- Kotlin Android plugin: 2.0.21.
- Gradle Wrapper: 8.9.
- compileSdk: 35.
- minSdk: 26.
- targetSdk: 35.
- Android SDK root: `tools/android-sdk`.
- Android Build Tools installed: `35.0.0`.
- ADB: `tools/android-sdk/platform-tools/adb.exe`, version 37.0.0-14910828.

The wrapper currently uses a local distribution zip:

```properties
distributionUrl=file:///D:/Documents/fif-Screen/tools/downloads/gradle-8.9-bin.zip
```

This is repeatable in the current workspace. It is not yet a clean cross-machine wrapper configuration.

## Commands Executed

```powershell
.\gradlew.bat clean --stacktrace
.\gradlew.bat :android-client:testDebugUnitTest --stacktrace
.\gradlew.bat assembleDebug --stacktrace
.\gradlew.bat :android-client:testDebugUnitTest :android-client:assembleDebug --stacktrace
```

The commands completed successfully.

## Test Coverage Added

Android unit tests now read the shared protocol vectors under `protocol/test-vectors` and cover:

- `Hello` frame decode.
- `HelloAck` frame decode.
- video header frame decode.
- partial packet reads.
- sticky packet reads.
- invalid payload length rejection.
- unsupported protocol version rejection.
- `HelloAck` encode/decode round trip.

## APK Evidence

APK:

```text
D:\Documents\fif-Screen\android-client\build\outputs\apk\debug\android-client-debug.apk
```

Size:

```text
844525 bytes
```

SHA-256:

```text
E9FC4D72D7D73A7A0F3BE0493463B4EA9EE4F632F97B27C6F51A2F37DEF1BD61
```

`aapt dump badging` evidence:

- package: `com.fif.screen`
- versionCode: `1`
- versionName: `0.1.0`
- compileSdkVersion: `35`
- min sdk: `26`
- target sdk: `35`
- permission: `android.permission.INTERNET`
- launchable activity: `com.fif.screen.MainActivity`
- debug build: yes

`apksigner verify --print-certs` evidence:

- debug certificate DN: `C=US, O=Android, CN=Android Debug`
- certificate SHA-256 digest: `ac1433c2bf8a09b4b488a26020662e8820f28c6e595af65845fa842eeca18a39`

## Device Status

Current hardware command:

```powershell
D:\Documents\fif-Screen\tools\android-sdk\platform-tools\adb.exe devices -l
```

Result:

```text
10AE7N2FX900178        device product:PD2403 model:V2403A device:PD2403 transport_id:1
```

APK install, app launch, H.264 decoder enumeration, Surface lifecycle, and USB control handshake were performed on the device. Details are in:

```text
docs/test-device.md
docs/android-hardware-test.md
docs/android-usb-reconnect-test.md
```

The physical USB unplug/replug test remains blocked by human action.
