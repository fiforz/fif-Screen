# Android Hardware Test

Date: 2026-07-08

## APK Install

Command:

```powershell
$apk = "D:\Documents\fif-Screen\android-client\build\outputs\apk\debug\android-client-debug.apk"
Get-FileHash -LiteralPath $apk -Algorithm SHA256
adb -s 10AE7N2FX900178 install -r $apk
```

Raw Output:

```text
Length: 844525 bytes
SHA-256: E9FC4D72D7D73A7A0F3BE0493463B4EA9EE4F632F97B27C6F51A2F37DEF1BD61

Performing Streamed Install
Success
exit_code=0
stderr=<empty>
```

APK metadata:

```text
package=com.fif.screen
versionCode=1
versionName=0.1.0
compileSdkVersion=35
minSdkVersion=26
targetSdkVersion=35
permission=android.permission.INTERNET
launchable_activity=com.fif.screen.MainActivity
```

## App Launch

Command:

```powershell
adb -s 10AE7N2FX900178 logcat -c
adb -s 10AE7N2FX900178 shell am start -n com.fif.screen/com.fif.screen.MainActivity
adb -s 10AE7N2FX900178 shell pidof com.fif.screen
adb -s 10AE7N2FX900178 logcat -d -v time
```

Raw Output:

```text
Starting: Intent { cmp=com.fif.screen/.MainActivity }
pid=31039

I/com.fif.screen: Late-enabling -Xcheck:jni
I/com.fif.screen: Using CollectorTypeCC GC.
I/FIFSCREEN_DEVICE: event=activity_on_create manufacturer=vivo model=V2403A product=PD2403 android=16 api=36 abi=arm64-v8a
I/FIFSCREEN_DISPLAY: event=activity_on_create refresh_rate=60.000004
I/FIFSCREEN_NETWORK: event=defaults control_host=127.0.0.1 control_port=27183 video_port=27184
I/FIFSCREEN_DECODER: event=enumerate_h264 mime=video/avc decoder_count=6
I/FIFSCREEN_SURFACE: event=created valid=true
I/FIFSCREEN_SURFACE: event=changed format=4 width=2800 height=1260 valid=true
I/ActivityTaskManager: Displayed com.fif.screen/.MainActivity for user 0: +573ms
```

Parsed Result:

```text
process_created=yes
activity_created=yes
surface_created=yes
surface_changed=yes
fatal_exception=no
anr=no
native_crash=no
```

## Structured Diagnostics Added

Files:

```text
D:\Documents\fif-Screen\android-client\src\main\java\com\fif\screen\diagnostics\FifLog.kt
D:\Documents\fif-Screen\android-client\src\main\java\com\fif\screen\diagnostics\H264CapabilityLogger.kt
D:\Documents\fif-Screen\android-client\src\main\java\com\fif\screen\MainActivity.kt
D:\Documents\fif-Screen\android-client\src\main\java\com\fif\screen\net\StreamClient.kt
D:\Documents\fif-Screen\android-client\src\main\java\com\fif\screen\video\H264SurfaceDecoder.kt
```

Tags:

```text
FIFSCREEN_DEVICE
FIFSCREEN_DISPLAY
FIFSCREEN_DECODER
FIFSCREEN_NETWORK
FIFSCREEN_SURFACE
```

All project diagnostics use `key=value` pairs so scripts can filter by tag and parse fields.

## Surface Lifecycle

Command:

```powershell
adb -s 10AE7N2FX900178 logcat -c
adb -s 10AE7N2FX900178 shell input keyevent HOME
adb -s 10AE7N2FX900178 shell am start -n com.fif.screen/com.fif.screen.MainActivity
adb -s 10AE7N2FX900178 shell input keyevent 26
adb -s 10AE7N2FX900178 shell input keyevent 26
adb -s 10AE7N2FX900178 shell input keyevent 82
adb -s 10AE7N2FX900178 shell am start -n com.fif.screen/com.fif.screen.MainActivity
adb -s 10AE7N2FX900178 logcat -d -v time
```

Observed project logs:

```text
FIFSCREEN_SURFACE event=destroyed
FIFSCREEN_NETWORK event=client_stop_requested
FIFSCREEN_SURFACE event=created valid=true
FIFSCREEN_SURFACE event=changed format=4 width=2800 height=1260 valid=true
FIFSCREEN_SURFACE event=destroyed
FIFSCREEN_NETWORK event=client_stop_requested
FIFSCREEN_SURFACE event=created valid=true
FIFSCREEN_SURFACE event=changed format=4 width=2800 height=1260 valid=true
```

Parsed Result:

```text
background_foreground_lifecycle=passed
lock_wake_lifecycle=passed
physical_rotation_test=not_executed
fatal_exception=no
anr=no
native_crash=no
decoder_stop_on_disconnect=observed during control test
```

Physical rotation was not claimed because it requires a physical device action or changing Android rotation settings. No Android system setting was modified.

## USB Control Handshake

Host command:

```powershell
D:\Documents\fif-Screen\build\stage-host-clean\windows-host\fif-host.exe --no-adb --control-port 27183 --video-port 27184
```

ADB reverse commands:

```powershell
adb -s 10AE7N2FX900178 reverse tcp:27183 tcp:27183
adb -s 10AE7N2FX900178 reverse tcp:27184 tcp:27184
adb -s 10AE7N2FX900178 reverse --list
```

Android action:

```text
Tapped START button bounds [16,108][734,255], center [375,181].
```

Host Output:

```text
control client connected
FIFSCREEN_HOST event=hello_received windows_timestamp_ns=86235470539400
FIFSCREEN_HOST event=hello_ack_sent windows_timestamp_ns=86235477982800
video client connected; encoder/capture path is not wired yet
```

Android Output:

```text
FIFSCREEN_NETWORK event=hello_sent android_timestamp_ns=653722654322601
FIFSCREEN_NETWORK event=hello_ack_received android_timestamp_ns=653722668944059 rtt_ms=14.621458
FIFSCREEN_DECODER event=decoder_start_requested mime=video/avc
FIFSCREEN_DECODER event=selected name=OMX.qcom.video.decoder.avc mime=video/avc
FIFSCREEN_DECODER event=low_latency_requested api=36
FIFSCREEN_DECODER event=started name=OMX.qcom.video.decoder.avc
FIFSCREEN_NETWORK event=connect_success host=127.0.0.1 port=27184
```

Parsed Result:

```text
android_to_windows_hello=passed
windows_to_android_hello_ack=passed
android_monotonic_handshake_rtt_ms=14.621458
windows_receive_timestamp_ns=86235470539400
windows_send_timestamp_ns=86235477982800
video_end_to_end_latency=not_measured
```

The Android and Windows timestamp values are from different monotonic clock domains. Only the Android-side RTT is computed here; it is not video latency.

