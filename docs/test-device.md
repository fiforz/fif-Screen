# Android Test Device

Date: 2026-07-08

## ADB Device Detection

Command:

```powershell
D:\Documents\fif-Screen\tools\android-sdk\platform-tools\adb.exe version
D:\Documents\fif-Screen\tools\android-sdk\platform-tools\adb.exe kill-server
D:\Documents\fif-Screen\tools\android-sdk\platform-tools\adb.exe start-server
D:\Documents\fif-Screen\tools\android-sdk\platform-tools\adb.exe devices -l
```

Raw Output:

```text
Android Debug Bridge version 1.0.41
Version 37.0.0-14910828
Installed as D:\Documents\fif-Screen\tools\android-sdk\platform-tools\adb.exe
Running on Windows 10.0.19045

* daemon not running; starting now at tcp:5037
* daemon started successfully

List of devices attached
10AE7N2FX900178        device product:PD2403 model:V2403A device:PD2403 transport_id:1
```

Parsed Result:

```text
serial=10AE7N2FX900178
state=device
product=PD2403
model=V2403A
device=PD2403
```

## Device Properties

Command:

```powershell
adb -s 10AE7N2FX900178 shell getprop ro.product.manufacturer
adb -s 10AE7N2FX900178 shell getprop ro.product.model
adb -s 10AE7N2FX900178 shell getprop ro.product.name
adb -s 10AE7N2FX900178 shell getprop ro.build.version.release
adb -s 10AE7N2FX900178 shell getprop ro.build.version.sdk
adb -s 10AE7N2FX900178 shell getprop ro.product.cpu.abi
adb -s 10AE7N2FX900178 shell getprop ro.product.cpu.abilist
```

Raw Output:

```text
manufacturer=vivo
model=V2403A
product=PD2403
android=16
api=36
abi=arm64-v8a
abi_list=arm64-v8a
```

Parsed Result:

```text
manufacturer=vivo
model=V2403A
product=PD2403
Android version=16
API level=36
primary ABI=arm64-v8a
ABI list=arm64-v8a
```

## Display

Command:

```powershell
adb -s 10AE7N2FX900178 shell wm size
adb -s 10AE7N2FX900178 shell wm density
adb -s 10AE7N2FX900178 shell dumpsys display
```

Raw Output:

```text
Physical size: 1260x2800
Physical density: 560
Override density: 490

DisplayDeviceInfo{"内置屏幕": uniqueId="local:4630946231300050819", 1260 x 2800, modeId 5, renderFrameRate 60.000004,
supportedRefreshRates [120.00001, 144.00002, 120.00001, 90.0, 90.0, 72.00001, 60.000004, 60.000004],
supportedModes [{id=1, width=1260, height=2800, fps=120.00001}, {id=2, width=1260, height=2800, fps=144.00002},
{id=3, width=1260, height=2800, fps=90.0}, {id=4, width=1260, height=2800, fps=72.00001},
{id=5, width=1260, height=2800, fps=60.000004}]}
```

Parsed Result:

```text
physical_size=1260x2800
physical_density=560
override_density=490
active_mode_id=5
current_refresh_rate=60.000004
supported_refresh_rates=120.00001,144.00002,90.0,72.00001,60.000004
```

## H.264 Decoder Capability

Command:

```powershell
adb -s 10AE7N2FX900178 logcat -d -v time | Select-String "FIFSCREEN_DECODER"
```

Raw Output:

```text
FIFSCREEN_DECODER event=enumerate_h264 mime=video/avc decoder_count=6
FIFSCREEN_DECODER event=h264_decoder name=OMX.google.h264.decoder mime=video/avc hardware_accelerated=false software_only=true vendor=false profiles_levels=65536:65536,1:65536,2:65536,524288:65536,8:65536 width_range=[2,_4080] height_range=[2,_4080] frame_rate_range=[0,_960] low_latency_feature=false
FIFSCREEN_DECODER event=h264_decoder name=OMX.qcom.video.decoder.avc mime=video/avc hardware_accelerated=true software_only=false vendor=true profiles_levels=1:524288,65536:524288,2:524288,8:524288,524288:524288 width_range=[96,_8192] height_range=[96,_8192] frame_rate_range=[1,_480] low_latency_feature=false
FIFSCREEN_DECODER event=h264_decoder name=OMX.qcom.video.decoder.avc.low_latency mime=video/avc hardware_accelerated=true software_only=false vendor=true profiles_levels=1:524288,65536:524288,2:524288,8:524288,524288:524288 width_range=[96,_8192] height_range=[96,_8192] frame_rate_range=[1,_480] low_latency_feature=true
FIFSCREEN_DECODER event=h264_decoder name=c2.android.avc.decoder mime=video/avc hardware_accelerated=false software_only=true vendor=false profiles_levels=65536:65536,1:65536,2:65536,524288:65536,8:65536 width_range=[2,_4080] height_range=[2,_4080] frame_rate_range=[0,_960] low_latency_feature=false
FIFSCREEN_DECODER event=h264_decoder name=c2.qti.avc.decoder mime=video/avc hardware_accelerated=true software_only=false vendor=true profiles_levels=1:524288,65536:524288,2:524288,8:524288,524288:524288 width_range=[96,_8192] height_range=[96,_8192] frame_rate_range=[1,_480] low_latency_feature=false
FIFSCREEN_DECODER event=h264_decoder name=c2.qti.avc.decoder.low_latency mime=video/avc hardware_accelerated=true software_only=false vendor=true profiles_levels=1:524288,65536:524288,2:524288,8:524288,524288:524288 width_range=[96,_8192] height_range=[96,_8192] frame_rate_range=[1,_480] low_latency_feature=true
```

Parsed Result:

```text
H.264 decoders found=6
MVP usable hardware decoder=yes
selected test decoder=OMX.qcom.video.decoder.avc
hardware low-latency-capable decoders=yes
low_latency_feature_api=available on API 36
```
