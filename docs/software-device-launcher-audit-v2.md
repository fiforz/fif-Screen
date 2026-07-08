# Software Device Launcher Audit V2

Date: 2026-07-08

Scope: V2 pre-create audit for `FifIddDeviceLauncher` after refactoring it
into a handle-lifetime software-device owner. No software device create, remove,
stop of a real device, driver package restage/delete, driver rebuild,
device disable/enable/restart, display mode change, BCD change, certificate
change, BitLocker change, reboot, Git stage, Git commit, Git push, Git reset,
or Git stash was executed.

## Launcher Identity

```text
SOURCE=D:\Documents\fif-Screen\windows-driver\FifIddDeviceLauncher\src\main.cpp
EXE=D:\Documents\fif-Screen\build\stage-driver-gate-clean\windows-driver\FifIddDeviceLauncher\fif-idd-device-launcher.exe
OLD_EXE_SHA256=89D93836A6A2580FF0021968BDD76F17E772C26398903B6ED105699BA5A57A79
NEW_EXE_SHA256=3C6D7E8CD84608E3630B7F2001DE027D3058BFB958C9D05E4CA0A47D8F8343D5
```

## Architecture

```text
LIFETIME_MODE=SwDeviceLifetimeHandle
SwDeviceSetLifetime(SwDeviceLifetimeHandle)=YES
SwDeviceLifetimeParentPresent_USED=NO
OWNER_PROCESS_REQUIRED=YES
OWNER_PROCESS_HOLDS_HSWDEVICE=YES
OWNER_PROCESS_EXIT_EXPECTED_TO_REMOVE_DEVICE=YES
NORMAL_REMOVAL_MECHANISM=stop event signals owner; owner calls SwDeviceClose; owner waits for PnP disappearance
```

The launcher is intentionally not a fire-and-exit creator. It is now the device
owner process for the lifetime of the software device.

## Duplicate Create Protection

```text
OWNER_MUTEX=Local\FifScreenIddOwnerMutex
STOP_EVENT=Local\FifScreenIddStopEvent
OWNER_STATE_MAP=Local\FifScreenIddOwnerState
OWNER_PRESENT_CHECK=YES
DEVICE_PRESENT_CHECK=YES
MUTEX_RACE_PROTECTION=YES
ERROR_ALREADY_EXISTS_REFUSED=YES
DUPLICATE_CREATE_PROTECTION=YES
```

Create decision matrix:

```text
owner=false device=false => ALLOW_CREATE
owner=true  device=true  => ALREADY_RUNNING
owner=true  device=false => INCONSISTENT_OWNER_STATE
owner=false device=true  => ORPHAN_OR_TRANSITIONAL_DEVICE_STATE
```

The final selftest executed those four states and all results were `PASS`.

## Control Channel

```text
STOP_CHANNEL=Local\FifScreenIddStopEvent
REMOVE_COMMAND_DIRECT_PNP_REMOVE=NO
REMOVE_COMMAND_DIF_REMOVE=NO
REMOVE_COMMAND_SETUPDI_REMOVE=NO
REMOVE_COMMAND_SIGNALS_OWNER=YES
OWNER_CTRL_C_HANDLER_SETS_STOP_EVENT=YES
REMOVE_COMMAND_EXECUTED=NO
```

`remove` no longer scans and removes matching device nodes. It either signals
the current owner or refuses an orphan/transitional device state.

## Callback

```text
CALLBACK_WAIT_IMPLEMENTED=YES
CALLBACK_TIMEOUT=10000 ms
CREATE_RESULT_CHECKED=YES
ACTUAL_INSTANCE_ID_CAPTURED=YES
ACTUAL_INSTANCE_ID_STORED_IN_OWNER_STATE=YES
ACTUAL_INSTANCE_ID_COPY_BOUNDED=YES
```

## Rollback

```text
ROLLBACK_EXECUTED=NO
ROLLBACK_PATH_STATICALLY_PROVEN=YES
ROLLBACK_TARGET=exact owner process handle
OWNER_CALLS_SwDeviceClose=YES
PNP_DISAPPEARANCE_WAIT=15000 ms
ORPHAN_DEVICE_DIRECT_REMOVE=NO
ORPHAN_DEVICE_STATE_REFUSED=YES
DRIVER_PACKAGE_UNTOUCHED=YES
ROLLBACK_PROVEN=YES
```

The rollback path is owned by the launcher process that holds `HSWDEVICE`; the
control path does not delete the Driver Store package and does not remove broad
PnP matches.

## Failure Cleanup

```text
CREATE_PRECHECK_FAILURE_CREATES_DEVICE=NO
SwDeviceCreate_IMMEDIATE_FAILURE_CREATES_OWNER_LOOP=NO
CALLBACK_TIMEOUT_CLOSES_HANDLE=YES
CALLBACK_FAILURE_CLOSES_HANDLE=YES
LIFETIME_SET_FAILURE_CLOSES_HANDLE=YES
OWNER_STATE_INIT_FAILURE_CLOSES_HANDLE=YES
FAILURE_WAIT_FOR_PNP_DISAPPEARANCE=YES
```

## Build And Checks

```text
BUILD_TARGET=fif-idd-device-launcher
BUILD_RESULT=PASS
SELFTEST_RESULT=PASS
STATUS_RESULT=owner_running=false; fifscreen_software_device_present=false; actual_instance_id=NOT_CREATED
STATIC_CHECK_SwDeviceLifetimeParentPresent=ABSENT
STATIC_CHECK_DIRECT_PNP_REMOVE=ABSENT
```

Driver package file hashes remained unchanged:

```text
FifIddDriver.dll=ABE48FFE52647223B95ECA4B35D32258114297EB113B63A6D13DD0F62AAE2683
FifIddDriver.inf=38D26D91B8628128C5750F26B3917F864CC5EE8F0AB588E35EA433C637E2D930
fifidddriver.cat=D2E2F5A99648884B22F5109FF308BB340F0BA4453D5F58B3D660120581008F81
```

## Current System State

```text
DRIVER_PACKAGE=oem95.inf
DRIVER_PROVIDER=FifScreen
DRIVER_SIGNER=WDKTestCert 29989,134279100762949792
SOFTWARE_DEVICE_PRESENT=NO
VIRTUAL_MONITOR_PRESENT=NO
SOFTWARE_DEVICE_CREATE_EXECUTED=NO
SOFTWARE_DEVICE_REMOVE_EXECUTED=NO
```

## Result

```text
PRECREATE_AUDIT_V2=PASS
FINAL_STATE=READY_FOR_SOFTWARE_DEVICE_CREATE_GATE_V2
```
