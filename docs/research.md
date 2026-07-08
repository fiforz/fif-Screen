# Research Log

Date: 2026-07-07

## Method

Preferred path was `gh search` plus shallow `git clone --depth 1`. `gh` is not installed. Anonymous GitHub REST API returned a rate-limit error. `git clone --depth 1 --filter=blob:none --sparse` failed with `Failed to connect to github.com port 443`.

Fallback used:

- Public GitHub web search.
- GitHub commit Atom feeds.
- Raw GitHub files for selected source and license files.

No GPL source code was copied into FifScreen. The WDK project structure and IddCx callback sequence for `FifIddDriver` were based on the Microsoft `Windows-driver-samples` IndirectDisplay sample after that official sample built locally.

## Repositories

| Repository | Commit Researched | Last Activity Seen | License | Code Reused |
| --- | --- | --- | --- | --- |
| `microsoft/Windows-driver-samples` | `2ee527bfeb0aeb6be11f0a8b6dce4011b358ce89` | 2026-07-02 | MS-PL | Project structure and IddCx callback sequence |
| `VirtualDrivers/Virtual-Display-Driver` | `d7244969b2aa8bb38e76d79505eda217996cefea` | 2026-06-22 | MIT | No |
| `LizardByte/Sunshine` | `cfe812534080ec7e0ee2fa3a968d11777abae024` | 2026-07-06 | GPL | No |
| `moonlight-stream/moonlight-android` | `f10085f552b367cf7203007693d91c322a0a2936` | 2024-07-27 | GPL | No |
| `ClassicOldSong/Apollo` | `adc5c5a0bd80831ce495434bb16aee2cd4175fb8` | 2026-05-21 | GPL | No |
| `tranvuongquocdat/SideScreen` | `b8ac7c49bc8b2776f1382257ae468d6d7698f738` | 2026-06-30 | MIT | No |
| `ppotepa/WBeam` | `0e5e8964c3f6f71b6a5eb217596a4b9b2c8ead17` | 2026-06-30 | No root license file found in this pass | No |
| `Genymobile/scrcpy` | `2322868e9e256eb5fce0b3d659ab2a409f29bae1` | 2026-05-12 | Apache-2.0 | No |

## Notes By Repository

### microsoft/Windows-driver-samples

Researched:

- `video/IndirectDisplay/IddSampleDriver/Driver.cpp`
- IddCx monitor mode creation.
- Adapter lifecycle callbacks.
- Monitor creation and swapchain assignment callbacks.

Borrowed:

- Keep driver lifecycle owned by IddCx/UMDF callbacks.
- Represent supported monitor modes explicitly.
- Treat swapchain assignment as the handoff point for the low-copy path.
- Use the official sample as the build-validation baseline before creating `FifIddDriver.vcxproj`.

License result: MS-PL permits reuse under its terms. No GPL code was used.

### VirtualDrivers/Virtual-Display-Driver

Researched:

- Repository metadata, license, and public tree surface for IddCx/driver/display terms.

Borrowed design only:

- Dynamic monitor configuration must be separated from host transport state.
- Resolution and refresh-rate handling should be explicit, not hidden in Android UI state.

License result: MIT permits reuse with notice, but no code was copied.

### LizardByte/Sunshine

Researched:

- Public source tree around Windows capture/encoding areas.
- License and recent activity.

Borrowed design only:

- Encoder backends must sit behind a common interface.
- Frame queues must be bounded and allowed to drop old frames.
- Low latency requires avoiding B frames and frame reordering.

License result: GPL. No Sunshine code is copied into this repository.

### moonlight-stream/moonlight-android

Researched:

- `app/src/main/java/com/limelight/binding/video/MediaCodecDecoderRenderer.java`
- `app/src/main/java/com/limelight/binding/video/MediaCodecHelper.java`

Borrowed design only:

- Probe available `MediaCodec` decoders before selecting one.
- Prefer Surface output instead of CPU frame conversion.
- Enable low-latency codec options when supported, but do not require them.

License result: GPL. No Moonlight code is copied into this repository.

### ClassicOldSong/Apollo

Researched:

- Repository metadata, license, and public tree surface around display/encoder terms.

Borrowed design only:

- Client connection lifecycle should eventually drive virtual monitor attach/detach.
- Dynamic mode changes belong in a host-display control layer, not in the transport.

License result: GPL. No Apollo code is copied into this repository.

### tranvuongquocdat/SideScreen

Researched:

- Repository metadata, license, and public tree surface around ADB/reverse/display terms.

Borrowed design only:

- USB MVP can use Android localhost plus `adb reverse`.
- Desktop and Android must handshake before starting video decode.

License result: MIT permits reuse with notice, but no code was copied.

### ppotepa/WBeam

Researched:

- Repository metadata and public tree surface around ADB/encoder/display terms.

Borrowed design only:

- ADB video transport needs explicit framing and reconnect behavior.

License result: no root license file found during this pass, so no code can be reused.

### Genymobile/scrcpy

Researched:

- `app/src/adb/adb.c`
- `app/src/demuxer.c`

Borrowed design only:

- ADB command execution and device-state handling should be isolated.
- Encoded video packets need explicit config/key-frame flags and timestamps.
- Sticky/partial packet parsing must be first-class.

License result: Apache-2.0 permits reuse with notice, but no code was copied.

## Primary Documentation To Keep Nearby

- Microsoft Indirect Display Driver Model and IddCx documentation: https://learn.microsoft.com/windows-hardware/drivers/display/indirect-display-driver-model-overview
- Microsoft Windows Graphics Capture documentation: https://learn.microsoft.com/windows/uwp/audio-video-camera/screen-capture
- Android `MediaCodec`: https://developer.android.com/reference/android/media/MediaCodec
- Android `MediaFormat.KEY_LOW_LATENCY`: https://developer.android.com/reference/android/media/MediaFormat#KEY_LOW_LATENCY
- Android `adb reverse`: https://developer.android.com/tools/adb

## Next Research Actions

- Repeat with authenticated `gh` after installation/login.
- Shallow-clone the exact commits into `tools/research/_repos/`.
- Read Virtual-Display-Driver, Apollo, SideScreen, WBeam concrete source files once clone/raw access is reliable.
- Run a capture benchmark after WDK/SDK and a first driver build exist.
