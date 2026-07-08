# Progress

Date: 2026-07-08

## CREATED

- Complete Android Gradle project.
- Complete WDK driver project.
- Windows Host.
- Shared protocol library and protocol test vectors.
- Environment and build documentation.
- Android hardware verification documentation:
  - `docs/test-device.md`
  - `docs/android-hardware-test.md`
  - `docs/android-usb-reconnect-test.md`
- Driver pre-install audit documentation:
  - `docs/driver-machine-preflight.md`
  - `docs/driver-signature-audit.md`
  - `docs/driver-test-paths.md`
  - `docs/display-recovery.md`
- Driver Gate scripts:
  - `scripts/driver-install-test.ps1`
  - `scripts/driver-uninstall-test.ps1`
  - `scripts/driver-state-check.ps1`
- IDD software device launcher:
  - `windows-driver/FifIddDeviceLauncher`
- Driver Test Mode Gate docs:
  - `docs/idd-device-lifecycle.md`
  - `docs/fif-idd-device-identity.md`
  - `docs/device-launcher-build.md`
  - `docs/driver-test-mode-gate.md`
  - `docs/driver-gate-checkpoint.md`
- Driver Package Stage docs:
  - `docs/driver-package-stage.md`
- Software Device Create Gate docs:
  - `docs/software-device-launcher-audit.md`
  - `docs/software-device-launcher-audit-v2.md`
  - `docs/software-device-create.md`
  - `docs/software-device-create-runtime.md`

## COMPILED

- Android debug APK:
  - `android-client/build/outputs/apk/debug/android-client-debug.apk`
  - SHA-256: `E9FC4D72D7D73A7A0F3BE0493463B4EA9EE4F632F97B27C6F51A2F37DEF1BD61`
- Windows Host clean build:
  - `build/stage-host-clean/windows-host/fif-host.exe`
- Protocol C++ test executable:
  - `build/stage-host-clean/tests/fif-protocol-test.exe`
- Official Microsoft `video.indirectdisplay` sample:
  - `IddSampleDriver.dll`
  - `iddsampledriver.cat`
  - `IddSampleDriver.inf`
  - `IddSampleApp.exe`
- FifScreen `FifIddDriver`:
  - `windows-driver/FifIddDriver/x64/Debug/FifIddDriver/FifIddDriver.dll`
  - SHA-256: `ABE48FFE52647223B95ECA4B35D32258114297EB113B63A6D13DD0F62AAE2683`
  - `windows-driver/FifIddDriver/x64/Debug/FifIddDriver/FifIddDriver.inf`
  - SHA-256: `38D26D91B8628128C5750F26B3917F864CC5EE8F0AB588E35EA433C637E2D930`
  - `windows-driver/FifIddDriver/x64/Debug/FifIddDriver/fifidddriver.cat`
  - SHA-256: `D2E2F5A99648884B22F5109FF308BB340F0BA4453D5F58B3D660120581008F81`
- FifScreen IDD software device launcher:
  - `build/stage-driver-gate-clean/windows-driver/FifIddDeviceLauncher/fif-idd-device-launcher.exe`
  - SHA-256: `3C6D7E8CD84608E3630B7F2001DE027D3058BFB958C9D05E4CA0A47D8F8343D5`

## EXECUTED

- Android `clean`: success.
- Android `testDebugUnitTest`: success.
- Android `assembleDebug`: success.
- Android `aapt dump badging`: package metadata verified.
- Android device detection:
  - `adb devices -l` reports `10AE7N2FX900178 device product:PD2403 model:V2403A device:PD2403`.
- Android APK install:
  - `adb install -r android-client-debug.apk`: success.
- Android app launch:
  - `com.fif.screen/.MainActivity`: success.
- Android structured diagnostics added and observed:
  - `FIFSCREEN_DEVICE`
  - `FIFSCREEN_DISPLAY`
  - `FIFSCREEN_DECODER`
  - `FIFSCREEN_NETWORK`
  - `FIFSCREEN_SURFACE`
- Android H.264 decoder enumeration: success, 6 `video/avc` decoders found.
- Android Surface lifecycle checks: background/foreground and lock/wake checks executed.
- ADB reverse create/check/remove/recover: success for TCP 27183 and 27184.
- Windows Host USB control handshake:
  - Android Hello sent.
  - Windows Host Hello received.
  - Windows Host HelloAck sent.
  - Android HelloAck received.
- Windows Host restart and Android app restart recovery check: success.
- Physical Android USB unplug/replug recovery:
  - device disappeared from `adb devices -l`.
  - same serial recovered as `device` with new transport.
  - ADB reverse was recreated.
  - Android `Hello` recovered.
  - Windows `HelloAck` recovered.
  - recovered handshake RTT: `17.695729 ms`.
- Protocol C++ test: `protocol tests passed`.
- IDD Software Device mechanism audit:
  - official sample uses `SwDeviceCreate`.
  - FifScreen creator was not implemented before this phase.
  - `FifIddDeviceLauncher` was created and compiled.
  - launcher `status` executed read-only and reported no existing FifScreen software device.
- `infverif /u /v FifIddDriver.inf`: `INF is VALID`.
- Driver pre-install read-only audit:
  - system state snapshot saved.
  - display devices enumerated.
  - Secure Boot read.
  - TESTSIGNING read.
  - BitLocker status read without recovery keys.
  - FifIddDriver DLL/INF/CAT signature state audited.
- Exact public test certificate was matched to the current DLL/CAT signer and imported to:
  - `LocalMachine\Root`
  - `LocalMachine\TrustedPublisher`
- `bcdedit /set testsigning on` executed successfully.
- Reboot was not performed.
- Post-reboot read-only gate executed after manual Windows reboot:
  - reboot confirmed by boot time after BCD/checkpoint file times.
  - TESTSIGNING is present in current BCD.
  - Secure Boot remains `False`.
  - C: and D: BitLocker remain fully decrypted with protection off.
  - exact public test certificate is present in `LocalMachine\Root`.
  - exact public test certificate is present in `LocalMachine\TrustedPublisher`.
  - current driver package hashes match the gated package.
  - DLL/CAT test signatures are trusted on this development machine with `/pa`.
  - driver package is not present in Driver Store.
  - FifScreen software device is not present.
  - FifScreen virtual monitor is not present.
  - launcher `status` reports `fifscreen_software_device_present=false`.
  - hardware ID match is proven.
- Driver Package Stage executed:
  - exact command: `pnputil /add-driver "D:\Documents\fif-Screen\windows-driver\FifIddDriver\x64\Debug\FifIddDriver\FifIddDriver.inf"`.
  - no wildcard, `/subdirs`, `/install`, `/reboot`, or `/delete-driver` was used.
  - exit code: `0`.
  - published Driver Store package: `oem95.inf`.
  - Original Name: `fifidddriver.inf`.
  - Provider: `FifScreen`.
  - Signer: `WDKTestCert 29989,134279100762949792`.
  - Driver Store changed only by adding the expected FifIddDriver package.
  - FifScreen software device is still not present.
  - FifScreen virtual monitor is still not present.
  - launcher `status` still reports `fifscreen_software_device_present=false`.
- Software Device Create Gate pre-create audit executed:
  - launcher `status` reports `fifscreen_software_device_present=false`.
  - PnP enumeration found no existing FifScreen software device.
  - callback wait is implemented with a 10000 ms timeout.
  - callback CreateResult is checked.
  - callback actual Device Instance ID capture is implemented.
  - current create lifetime mode is handle-held.
  - `SwDeviceSetLifetime(SwDeviceLifetimeParentPresent)` is not used.
  - device survival after launcher process exit is not proven.
  - duplicate create protection is not implemented.
  - rollback is not strictly proven for this gate.
  - `FifIddDeviceLauncher create` was not executed.
- Software Device Create Gate pre-create audit V2 executed:
  - `FifIddDeviceLauncher` was refactored into a handle-lifetime owner process.
  - `SwDeviceSetLifetime(SwDeviceLifetimeHandle)` is explicitly called after callback success.
  - `SwDeviceLifetimeParentPresent` is not used.
  - strict owner mutex is implemented: `Local\FifScreenIddOwnerMutex`.
  - stop channel is implemented: `Local\FifScreenIddStopEvent`.
  - direct SetupDi/DIF remove path was removed from launcher `remove`.
  - duplicate create decision matrix is implemented and covered by launcher `selftest`.
  - failure cleanup closes `HSWDEVICE` and waits up to 15000 ms for PnP disappearance.
  - launcher clean build passed.
  - launcher `selftest` passed all four duplicate-state cases.
  - launcher `status` reports `owner_running=false`, `fifscreen_software_device_present=false`, `actual_instance_id=NOT_CREATED`.
  - Driver Store package remains `oem95.inf`.
  - FifIddDriver DLL/INF/CAT hashes remain unchanged.
  - FifScreen software device is still not present.
  - FifScreen virtual monitor is still not present.
  - `FifIddDeviceLauncher create` was not executed.
- Software Device Create Runtime Gate executed:
  - local Launcher V2 baseline commit was created: `0c23ea8b1eae76dfb5e35d33fc2eea370ddf71ae`.
  - exactly one `FifIddDeviceLauncher create` was started.
  - owner PID is `13844`.
  - owner process remained running at the end of this gate.
  - callback/create success was observed via launcher state.
  - actual instance ID is `SWD\FIFSCREENIDD\FIFIDDDRIVER`.
  - software device is present while owner is running.
  - device remained present after 5 seconds with unchanged instance ID.
  - driver binding failed: no `oem95.inf` binding was recorded on the devnode.
  - DevNode status is `0x1806400`.
  - Problem Code is `28` (`CM_PROB_FAILED_INSTALL`).
  - Kernel-PnP configured the device with driver name `null`.
  - DeviceSetupManager delayed 50 seconds on driver query/download/install for the device.
  - SetupAPI.dev.log had no new bytes after the recorded pre-create offset.
  - FifScreen display adapter did not appear.
  - FifScreen virtual monitor did not appear.
  - no remove, stop, owner termination, device restart, driver restage, display mode change, BCD change, certificate change, reboot, or second create was executed.

## HARDWARE VERIFIED

- Android device is physically connected and ADB-authorized:
  - serial `10AE7N2FX900178`.
- APK installed on the real device.
- App process and Activity launch on the real device.
- SurfaceView creates and changes on the real device.
- App startup had no observed `FATAL EXCEPTION`, ANR, or native crash.
- Real-device H.264 `video/avc` MediaCodec enumeration succeeded.
- At least one hardware H.264 decoder is available for MVP:
  - `OMX.qcom.video.decoder.avc`
  - `OMX.qcom.video.decoder.avc.low_latency`
  - `c2.qti.avc.decoder`
  - `c2.qti.avc.decoder.low_latency`
- ADB reverse control channel works for TCP 27183.
- Android to Windows `Hello` works.
- Windows to Android `HelloAck` works.
- HARDWARE VERIFIED - ANDROID CONTROL PATH:
  - physical USB unplug observed.
  - physical USB replug observed.
  - ADB recovered.
  - reverse recovered.
  - socket recovered.
  - Hello recovered.
  - HelloAck recovered.
  - Host did not crash.
  - Android App did not crash.

Not claimed:

- Windows driver loaded is not claimed because the created FifScreen software device did not bind to `oem95.inf`.
- Windows virtual monitor is not claimed because no FifScreen monitor device appeared.
- Virtual extended desktop is not claimed or verified.

## BLOCKED BY HUMAN ACTION

- `DRIVER_BINDING_FAILED`: first real software device create succeeded, but the devnode did not bind to `oem95.inf` and has Problem Code 28.
- `RECOVERY_KEY_CONFIRMED_BY_USER=NO`: no BitLocker recovery key was requested, displayed, saved, or confirmed.
- Virtual Monitor verification is blocked until driver binding succeeds.

Driver modifications executed:

```text
Certificate stores changed: YES, exact public WDKTestCert only
BCD TESTSIGNING changed: YES
Driver package staged in Driver Store: YES, oem95.inf
Driver loaded: NO
Software device created: YES, SWD\FIFSCREENIDD\FIFIDDDRIVER
Owner process running: YES, PID 13844
Driver bound: NO
Device started: NO
Problem code: 28, CM_PROB_FAILED_INSTALL
Virtual monitor verified: NO
Post-reboot read-only check: PASS
Driver package stage: PASS
Software device pre-create audit V1: FAILED
Software device pre-create audit V2: PASS
Software device create runtime gate: DRIVER_BINDING_FAILED
Current stop state: DRIVER_BINDING_FAILED
```

## VERTICAL SLICE UPDATE - 2026-07-08 15:12

Supersedes the earlier `DRIVER_BINDING_FAILED` stop state.

What works:

- `FifScreen Indirect Display` binds to `oem95.inf`.
- Driver provider is `FifScreen`.
- Device service is `WUDFRd`.
- Device status is `Started`.
- Problem Code is `0`.
- Windows Display class includes `FifScreen Indirect Display`.
- Current reported FifScreen resolution is `1920x1080`.
- IddCx adapter initialization succeeds.
- IddCx monitor creation succeeds.
- IddCx monitor arrival succeeds.
- Swapchain assignment succeeds.
- Swapchain worker sets the D3D device and processes the first frame.

What failed and was fixed:

- Code 28 root cause: INF models section targeted Windows 11 build 22000+, excluding this Windows 10 19045 machine.
- Code 1 root cause: `Include=WUDFRD.inf` is not valid on this machine because `WUDFRD.inf` is absent.
- Code 31 root cause: missing `EvtIddCxParseMonitorDescription` caused `IddCxDeviceInitConfig` to return `STATUS_INVALID_PARAMETER`.
- Swapchain churn root cause: `EvtIddCxMonitorAssignSwapChain` immediately deleted the assigned swapchain.

Current blocker:

- `HUMAN_GATE_DISPLAY_VISUAL_CONFIRMATION`: user must visually confirm Windows display settings and window movement behavior.

Next action after confirmation:

- Verify the FifScreen target can be used as an extended desktop target.
- Continue into capture, H.264 encode, ADB transport, and Android decode/display.
