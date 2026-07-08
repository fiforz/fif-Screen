# Post-Reboot Read-Only Check

Date: 2026-07-08

Scope: read-only gate after manual Windows reboot. No driver install, driver
removal, software device create, software device remove, certificate change,
BCD change, BitLocker change, display driver change, or reboot was executed.

## Artifacts

```text
D:\Documents\fif-Screen\artifacts\post-reboot\system-state.txt
D:\Documents\fif-Screen\artifacts\post-reboot\boot-state.txt
D:\Documents\fif-Screen\artifacts\post-reboot\bcdedit-enum.txt
D:\Documents\fif-Screen\artifacts\post-reboot\testsigning-state.txt
D:\Documents\fif-Screen\artifacts\post-reboot\bitlocker-state.txt
D:\Documents\fif-Screen\artifacts\post-reboot\certificate-state.txt
D:\Documents\fif-Screen\artifacts\post-reboot\driver-store-before-install.txt
D:\Documents\fif-Screen\artifacts\post-reboot\devices-before-install.txt
D:\Documents\fif-Screen\artifacts\post-reboot\display-devices-before-install.txt
D:\Documents\fif-Screen\artifacts\post-reboot\signature-verification.txt
D:\Documents\fif-Screen\artifacts\post-reboot\file-hashes.txt
D:\Documents\fif-Screen\artifacts\post-reboot\launcher-status.txt
```

## Reboot

```text
REBOOT_CONFIRMED=YES
CHECK_CAPTURE_TIME=2026-07-08T10:03:29.7928615+08:00
BOOT_STATE_REFRESH_TIME=2026-07-08T10:07:58.6910305+08:00
BOOT_TIME=2026-07-08T09:33:03.5000000+08:00
BCD_AFTER_TESTSIGNING_FILE_TIME=2026-07-08T09:24:17+08:00
PRE_DOC_UPDATE_DRIVER_GATE_CHECKPOINT_FILE_TIME=2026-07-08T09:26:46+08:00
DRIVER_TEST_MODE_GATE_FILE_TIME=2026-07-08T09:26:46+08:00
REBOOT_AFTER_BCD_CHANGE=YES
```

## System

```text
WINDOWS=Windows 10 Pro, 10.0.19045, build 19045
WINDOWS_VERSION_FIELD=2009
BOOT_MODE=UEFI
SECURE_BOOT=False
BITLOCKER_C=FullyDecrypted, Protection Off, Unlocked
BITLOCKER_D=FullyDecrypted, Protection Off, Unlocked
```

## Test Mode

```text
TESTSIGNING_BCD=YES
TEST_MODE_BOOT_PRECONDITION=PASS
TEST_MODE_WATERMARK_OBSERVED=NOT_CHECKED
```

## Certificate

```text
EXPECTED_THUMBPRINT=09FE7D270B6B9D00BBCE41FEC84F826D4E687009
ROOT_FOUND=YES
TRUSTEDPUBLISHER_FOUND=YES
HAS_PRIVATE_KEY=False
SUBJECT=CN="WDKTestCert 29989,134279100762949792"
SERIAL=379987E82B219EAE46DF4258DEA5669D
```

## Package Identity

```text
FifIddDriver.dll SHA-256=ABE48FFE52647223B95ECA4B35D32258114297EB113B63A6D13DD0F62AAE2683
FifIddDriver.inf SHA-256=38D26D91B8628128C5750F26B3917F864CC5EE8F0AB588E35EA433C637E2D930
fifidddriver.cat SHA-256=D2E2F5A99648884B22F5109FF308BB340F0BA4453D5F58B3D660120581008F81
DLL_HASH_MATCH=YES
INF_HASH_MATCH=YES
CAT_HASH_MATCH=YES
```

## Signature

```text
Get-AuthenticodeSignature FifIddDriver.dll=Valid
Get-AuthenticodeSignature fifidddriver.cat=Valid
signtool verify /pa FifIddDriver.dll=exit 0
signtool verify /pa fifidddriver.cat=exit 0
signtool verify /kp FifIddDriver.dll=exit 1, does not chain to Microsoft Root Cert
signtool verify /kp fifidddriver.cat=exit 1, does not chain to Microsoft Root Cert
TEST_SIGNATURE_STATUS=TEST SIGNATURE TRUSTED ON THIS DEVELOPMENT MACHINE
PRODUCTION_SIGNED=false
```

## Baseline

```text
DRIVER_PACKAGE_PRESENT=NO
SOFTWARE_DEVICE_PRESENT=NO
VIRTUAL_MONITOR_PRESENT=NO
DISPLAY_BASELINE=Todesk Virtual Display Adapter; AMD Radeon RX 5700; generic PnP monitors
```

## Launcher

```text
LAUNCHER_PATH=D:\Documents\fif-Screen\build\stage-driver-gate-clean\windows-driver\FifIddDeviceLauncher\fif-idd-device-launcher.exe
LAUNCHER_SHA-256=89D93836A6A2580FF0021968BDD76F17E772C26398903B6ED105699BA5A57A79
HASH_MATCH=YES
STATUS_RESULT=fifscreen_software_device_present=false
STATUS_EXIT=0
```

## Hardware ID

```text
INF_MATCH_ID=FifIddDriver
INF_ALSO_HAS_ROOT_ID=Root\FifScreenIdd
LAUNCHER_CREATE_ID=FifIddDriver
EXPECTED_INSTANCE_ID=SWD\FifScreenIdd\FifIddDriver
MATCH_PROVEN=YES
```

## Safety

```text
ROLLBACK_READY=YES
INSTALL_SCRIPT_SAFE=YES, gated by -ConfirmGate and package hash checks
UNINSTALL_SCRIPT_SAFE=YES, requires exact oemXX.inf and -ConfirmGate
```

## Modifications Executed

```text
DRIVER_INSTALL=NO
DRIVER_REMOVE=NO
SOFTWARE_DEVICE_CREATE=NO
SOFTWARE_DEVICE_REMOVE=NO
BCD_CHANGE=NO
CERTIFICATE_CHANGE=NO
BITLOCKER_CHANGE=NO
DISPLAY_DRIVER_CHANGE=NO
REBOOT=NO
```

## Final State

```text
POST_REBOOT_CHECK=PASS
FINAL_STATE=READY_FOR_DRIVER_INSTALL_GATE
```
