# Driver Gate Checkpoint

Date: 2026-07-08

## CURRENT STATE

```text
Android Control Path status=HARDWARE VERIFIED
Android device serial=10AE7N2FX900178
Physical USB unplug observed=true
Physical USB replug observed=true
ADB recovered=true
Reverse recovered=true
Hello recovered=true
HelloAck recovered=true
Recovered handshake RTT=17.695729 ms
```

```text
Secure Boot=False
BitLocker OS volume=Fully Decrypted
BitLocker Protection=Off
TESTSIGNING BCD state=Yes
Reboot performed=NO
```

```text
certificate Root state=exact thumbprint present
certificate TrustedPublisher state=exact thumbprint present
certificate thumbprint=09FE7D270B6B9D00BBCE41FEC84F826D4E687009
signature status=TEST SIGNATURE TRUSTED ON THIS DEVELOPMENT MACHINE
```

```text
Driver installed=NO
Software Device created=NO
Launcher create executed=NO
Virtual Monitor verified=NO
```

## NEXT PHASE

After manual Windows reboot, first execute only read-only checks:

```text
1. Windows version
2. bcdedit /enum
3. Secure Boot check
4. BitLocker status
5. exact certificate store check
6. Driver Store check
7. existing FifScreen device check
```

Only after those checks pass should the next phase enter:

```text
Driver Package Install
Software Device Create
Virtual Monitor Verification
```

## POST-REBOOT READ-ONLY CHECK

Executed after manual Windows reboot:

```text
REBOOT_CONFIRMED=YES
BOOT_TIME=2026-07-08T09:33:03.5000000+08:00
TESTSIGNING BCD state=Yes
Secure Boot=False
BitLocker C:=FullyDecrypted, Protection Off
BitLocker D:=FullyDecrypted, Protection Off
Root certificate exact thumbprint present=True
TrustedPublisher certificate exact thumbprint present=True
Driver package hashes unchanged=True
Signature status=TEST SIGNATURE TRUSTED ON THIS DEVELOPMENT MACHINE
Driver package present in Driver Store=NO
Software Device present=NO
Virtual Monitor present=NO
Launcher status=fifscreen_software_device_present=false
Hardware ID match proven=True
POST_REBOOT_CHECK=PASS
```

Post-reboot artifacts:

```text
D:\Documents\fif-Screen\artifacts\post-reboot
```

Detailed reports:

```text
docs/post-reboot-read-only-check.md
docs/hardware-id-match-audit.md
```

## DRIVER PACKAGE STAGE

Executed after Git baseline:

```text
Git baseline head=bd37e0faed7c329c432c6046425a12d64e12e4f5
Command=pnputil /add-driver "D:\Documents\fif-Screen\windows-driver\FifIddDriver\x64\Debug\FifIddDriver\FifIddDriver.inf"
Exit code=0
Published Name=oem95.inf
Original Name=fifidddriver.inf
Provider=FifScreen
Signer=WDKTestCert 29989,134279100762949792
Unexpected Driver Store change=NO
Software Device present=NO
Virtual Monitor present=NO
Reboot required=NO
DRIVER_PACKAGE_STAGE=PASS
```

Detailed report:

```text
docs/driver-package-stage.md
```

## SOFTWARE DEVICE CREATE PRE-AUDIT

Executed before any software device create:

```text
Driver package=oem95.inf
Launcher hash=89D93836A6A2580FF0021968BDD76F17E772C26398903B6ED105699BA5A57A79
Launcher status=fifscreen_software_device_present=false
PnP FifScreen software device present=NO
Lifetime mode=HANDLE
SwDeviceLifetimeParentPresent used=NO
Callback wait implemented=YES
Callback timeout=10000 ms
CreateResult checked=YES
Actual Device Instance ID capture implemented=YES
Device survival after launcher process exit proven=NO
Duplicate create protection=NO
Rollback strictly proven=NO
Software device create executed=NO
PRECREATE_AUDIT=FAILED
```

Detailed reports:

```text
docs/software-device-launcher-audit.md
```

## SOFTWARE DEVICE CREATE PRE-AUDIT V2

Executed before any software device create:

```text
Driver package=oem95.inf
Launcher old hash=89D93836A6A2580FF0021968BDD76F17E772C26398903B6ED105699BA5A57A79
Launcher new hash=3C6D7E8CD84608E3630B7F2001DE027D3058BFB958C9D05E4CA0A47D8F8343D5
Launcher status=owner_running=false; fifscreen_software_device_present=false; actual_instance_id=NOT_CREATED
PnP FifScreen software device present=NO
PnP FifScreen virtual monitor present=NO
Lifetime mode=SwDeviceLifetimeHandle
SwDeviceSetLifetime(SwDeviceLifetimeHandle)=YES
SwDeviceLifetimeParentPresent used=NO
Owner process required=YES
Owner mutex=Local\FifScreenIddOwnerMutex
Stop channel=Local\FifScreenIddStopEvent
Duplicate create protection=YES
Rollback statically proven=YES
Direct SetupDi/DIF remove path=NO
Callback wait implemented=YES
Callback timeout=10000 ms
CreateResult checked=YES
Actual Device Instance ID capture implemented=YES
Launcher selftest=PASS
Launcher build=PASS
Driver package files unchanged=YES
Software device create executed=NO
Software device remove executed=NO
PRECREATE_AUDIT_V2=PASS
```

Detailed report:

```text
docs/software-device-launcher-audit-v2.md
docs/software-device-create.md
```

## SOFTWARE DEVICE CREATE RUNTIME GATE

Executed after Launcher V2 baseline commit:

```text
Precreate commit=0c23ea8b1eae76dfb5e35d33fc2eea370ddf71ae
Command="D:\Documents\fif-Screen\build\stage-driver-gate-clean\windows-driver\FifIddDeviceLauncher\fif-idd-device-launcher.exe" create
Start time=2026-07-08T14:29:46.8079723+08:00
Owner PID=13844
Owner running at end=YES
Software device create executed=YES
Software device create count=1
Expected instance ID=SWD\FifScreenIdd\FifIddDriver
Actual instance ID=SWD\FIFSCREENIDD\FIFIDDDRIVER
Instance ID match=YES, case-insensitive
Device present immediately=YES
Device present after 5 seconds=YES
Device present while owner running=YES
Driver bound=NO
Driver INF=
Provider=
Service=
Driver version=
DevNode status=0x1806400
Problem code=28
Problem name=CM_PROB_FAILED_INSTALL
Device started=NO
SetupAPI.dev.log delta bytes=0
Kernel-PnP binding result=driver name null
DeviceSetupManager warning=driver query/download/install delayed 50 seconds
FifScreen display adapter appeared=NO
Virtual monitor appeared=NO
Virtual extended desktop verified=NO
Software device remove executed=NO
Owner terminate executed=NO
Driver package stage executed=NO
Device disable/enable/restart executed=NO
Display mode change executed=NO
BCD/certificate/reboot executed=NO
RUNTIME_GATE=DRIVER_BINDING_FAILED
```

Detailed report:

```text
docs/software-device-create-runtime.md
D:\Documents\fif-Screen\artifacts\software-device-create-v2
```

## STOP STATE

```text
DRIVER_BINDING_FAILED
```
