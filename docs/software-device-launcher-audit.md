# Software Device Launcher Audit

Date: 2026-07-08

Scope: pre-create audit for `FifIddDeviceLauncher`. This audit was performed
before any software device create. No create, remove, driver delete, driver
restage, device disable/enable/restart, display mode change, BCD change,
certificate change, BitLocker change, reboot, or Git operation was executed.

## Launcher Identity

```text
SOURCE=D:\Documents\fif-Screen\windows-driver\FifIddDeviceLauncher\src\main.cpp
EXE=D:\Documents\fif-Screen\build\stage-driver-gate-clean\windows-driver\FifIddDeviceLauncher\fif-idd-device-launcher.exe
EXE_SHA256=89D93836A6A2580FF0021968BDD76F17E772C26398903B6ED105699BA5A57A79
```

## Create Parameters

Source lines:

```text
kEnumeratorName=FifScreenIdd
kParentDevice=HTREE\ROOT\0
kInstanceId=FifIddDriver
kHardwareId=FifIddDriver
kHardwareIdsMultiSz=FifIddDriver\0\0
kCompatibleIdsMultiSz=FifIddDriver\0\0
kDescription=FifScreen Indirect Display
CapabilityFlags=Removable | SilentInstall | DriverRequired
SwDeviceCreate(kEnumeratorName, kParentDevice, &create_info, ...)
```

Parsed result:

```text
PARENT=HTREE\ROOT\0
PSZ_INSTANCE_ID=FifIddDriver
HARDWARE_IDS=FifIddDriver
COMPATIBLE_IDS=FifIddDriver
DEVICE_DESCRIPTION=FifScreen Indirect Display
CAPABILITY_FLAGS=SWDeviceCapabilitiesRemovable | SWDeviceCapabilitiesSilentInstall | SWDeviceCapabilitiesDriverRequired
```

## Callback Audit

Source behavior:

```text
CreateContext stores event, HRESULT result, and instance_id.
creation_callback writes CreateResult into context.result.
creation_callback copies pszDeviceInstanceId into context.instance_id.
creation_callback signals context.event.
command_create waits with WaitForSingleObject(context.event, 10 * 1000).
command_create checks wait_result and FAILED(context.result).
command_create prints created_instance=<callback instance id>.
```

Result:

```text
CALLBACK_WAIT_IMPLEMENTED=YES
CALLBACK_TIMEOUT=10000 ms
CREATE_RESULT_CHECKED=YES
ACTUAL_INSTANCE_ID_CAPTURED=YES
CREATE_OUTPUTS_ACTUAL_INSTANCE_ID=YES
```

This part is acceptable: the launcher does not treat `SwDeviceCreate` returning
`S_OK` as final creation proof; it waits for the callback.

## Lifetime Audit

Current source behavior:

```text
SwDeviceSetLifetime is not called.
SwDeviceLifetimeParentPresent is not used.
After callback success, command_create prints "Press X to remove the software device and exit."
command_create blocks in _getch().
When X is pressed, command_create calls SwDeviceClose(sw_device).
After SwDeviceClose, command_create prints removed_by_close=true and exits.
```

Gate result:

```text
CREATE_LIFETIME_MODE=HANDLE
PROCESS_EXITS_AFTER_CREATE=NO
DEVICE_EXPECTED_TO_SURVIVE_PROCESS_EXIT=NO
DEVICE_EXPECTED_TO_EXIST_ONLY_WHILE_HANDLE_IS_HELD=YES
SOFTWARE_DEVICE_LIFETIME_SAFE_FOR_THIS_GATE=NO
```

The current design is handle-held. It may be suitable for an interactive sample
process that intentionally keeps running, but it does not satisfy this gate's
requirement to prove that the software device survives launcher process exit.

## Duplicate Create Protection

Current source behavior:

```text
command_status calls find_devices(false).
command_create does not call find_devices before SwDeviceCreate.
command_create does not reject an existing FifScreen software device.
command_create does not prove that a second invocation cannot create another matching device.
```

Gate result:

```text
DUPLICATE_CREATE_PROTECTION=NO
```

## Rollback Audit

Current remove behavior:

```text
command_remove scans all setup classes.
command_remove matches records via is_fifscreen_device.
is_fifscreen_device matches instance_id containing SWD\FifScreenIdd.
is_fifscreen_device also matches instance_id containing FifIddDriver.
is_fifscreen_device also matches hardware_id exactly equal to FifIddDriver.
command_remove uses DIF_REMOVE with DI_REMOVEDEVICE_GLOBAL.
command_remove does not delete oem95.inf.
command_remove does not intentionally modify AMD/NVIDIA/Intel devices.
```

Safety gap:

```text
REMOVE_MATCHING_IS_NOT_STRICT_INSTANCE_ID_ONLY=YES
PERSISTENT_PARENT_PRESENT_REACQUIRE_PATH=NOT_APPLICABLE_CURRENTLY
ROLLBACK_PROVEN=NO
```

The remove path is scoped toward FifScreen identifiers, but for this gate it is
not strict enough to prove rollback before create. It uses substring matching on
`FifIddDriver` and the create path itself does not use a persistent lifetime.

## Current System Pre-Create Status

```text
DRIVER_PACKAGE=oem95.inf
SOFTWARE_DEVICE_PRESENT=NO
LAUNCHER_STATUS=fifscreen_software_device_present=false
VIRTUAL_MONITOR_PRESENT=NO
```

## Result

```text
PRECREATE_AUDIT=FAILED
REASON=SOFTWARE_DEVICE_LIFETIME_NOT_SAFE
SECONDARY_REASON=DUPLICATE_CREATE_PROTECTION_NO
SECONDARY_REASON=ROLLBACK_NOT_STRICTLY_PROVEN
SOFTWARE_DEVICE_CREATE_EXECUTED=NO
FINAL_STATE=LAUNCHER_PRECREATE_AUDIT_FAILED
```
