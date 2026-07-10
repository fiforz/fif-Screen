# Software Device Create Runtime Gate

Date: 2026-07-08

Scope: first real `FifIddDeviceLauncher create` execution after Launcher V2
baseline commit. Exactly one create process was started. No second create,
remove, stop, owner termination, driver restage/delete, device disable/enable,
device restart, display mode change, BCD change, certificate change,
BitLocker change, reboot, Git stage, Git commit, or Git push was executed
after the runtime audit began.

## Git Baseline

```text
PRECREATE_COMMIT=0c23ea8b1eae76dfb5e35d33fc2eea370ddf71ae
HEAD_BEFORE_RUNTIME=0c23ea8b1eae76dfb5e35d33fc2eea370ddf71ae
WORKTREE_BEFORE_RUNTIME=clean
```

## Create

```text
COMMAND="D:\Documents\fif-Screen\build\stage-driver-gate-clean\windows-driver\FifIddDeviceLauncher\fif-idd-device-launcher.exe" create
START_TIME=2026-07-08T14:29:46.8079723+08:00
OWNER_PID=13844
OWNER_RUNNING=YES
SOFTWARE_DEVICE_CREATE=YES
SOFTWARE_DEVICE_CREATE_COUNT=1
CREATE_RESULT=SUCCESS_OBSERVED
EXPECTED_INSTANCE_ID=SWD\FifScreenIdd\FifIddDriver
ACTUAL_INSTANCE_ID=SWD\FIFSCREENIDD\FIFIDDDRIVER
INSTANCE_ID_MATCH=YES_CASE_INSENSITIVE
```

## Handle Lifetime

```text
DEVICE_PRESENT_IMMEDIATELY=YES
DEVICE_PRESENT_AFTER_5_SECONDS=YES
DEVICE_PRESENT_WHILE_OWNER_RUNNING=YES
INSTANCE_ID_UNCHANGED_AFTER_5_SECONDS=YES
```

## Driver Binding

```text
BOUND=NO
DRIVER_INF=
PROVIDER=
SERVICE=
DRIVER_VERSION=
DEVICE_NAME=FifScreen Indirect Display
PNP_STATUS=Error
```

The software device exists, but Windows did not bind it to `oem95.inf`.

## DevNode

```text
DN_FLAGS=0x1806400
PROBLEM_CODE=28
PROBLEM_NAME=CM_PROB_FAILED_INSTALL
DEVICE_STARTED=NO
```

## SetupAPI And Events

```text
SETUPAPI_DEV_LOG_DELTA_BYTES=0
BINDING_RESULT=driver name null in Kernel-PnP configuration event
START_RESULT=NOT_STARTED
ERRORS=Problem Code 28
WARNINGS=DeviceSetupManager delayed 50 seconds querying/downloading/installing driver
```

Relevant event evidence:

```text
Kernel-PnP Event 400 configured SWD\FifScreenIdd\FifIddDriver with driver name null.
DeviceSetupManager Event 123 delayed 50 seconds on SWD\FIFSCREENIDD\FIFIDDDRIVER driver query/download/install.
DriverFrameworks-UserMode had no matching runtime events in the checked window.
```

## Display Observation

```text
FIFSCREEN_DISPLAY_ADAPTER_APPEARED=NO
VIRTUAL_MONITOR_APPEARED=NO
VIRTUAL_EXTENDED_DESKTOP_VERIFIED=NO
```

## Artifacts

```text
D:\Documents\fif-Screen\artifacts\software-device-create-v2
owner-process.txt
owner-stdout.txt
owner-stderr.txt
create-result.txt
actual-instance-id.txt
launcher-status-after.txt
pnp-device-after.txt
driver-binding-after.txt
devnode-status-after.txt
setupapi-dev-delta.txt
relevant-events.txt
display-adapters-after.txt
monitors-after.txt
result.txt
```

## Final State

```text
FINAL_STATE=DRIVER_BINDING_FAILED
```
