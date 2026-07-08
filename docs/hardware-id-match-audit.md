# Hardware ID Match Audit

Date: 2026-07-08

Scope: read-only audit for the post-reboot driver install gate. No driver install,
driver removal, software device create, or software device remove was executed.

## Sources Read

```text
FifScreen INF:
D:\Documents\fif-Screen\windows-driver\FifIddDriver\x64\Debug\FifIddDriver\FifIddDriver.inf

FifScreen launcher:
D:\Documents\fif-Screen\windows-driver\FifIddDeviceLauncher\src\main.cpp

Official Microsoft IddSample fixed commit:
D:\Documents\fif-Screen\tools\research\_repos\Windows-driver-samples-2ee527bfeb0aeb6be11f0a8b6dce4011b358ce89\video\IndirectDisplay\IddSampleApp\main.cpp
D:\Documents\fif-Screen\tools\research\_repos\Windows-driver-samples-2ee527bfeb0aeb6be11f0a8b6dce4011b358ce89\video\IndirectDisplay\IddSampleDriver\IddSampleDriver.inf
```

## FifScreen INF Evidence

```text
[Standard.NTamd64.10.0...22000]
%DeviceName%=FifIddDriver_Install, Root\FifScreenIdd
%DeviceName%=FifIddDriver_Install, FifIddDriver
```

Parsed result:

```text
INF_MATCH_IDS=Root\FifScreenIdd; FifIddDriver
INF_SOFTWARE_DEVICE_MATCH_ID=FifIddDriver
INF_ROOT_ENUMERATED_MATCH_ID=Root\FifScreenIdd
```

## FifScreen Launcher Evidence

```text
kEnumeratorName=FifScreenIdd
kParentDevice=HTREE\ROOT\0
kInstanceId=FifIddDriver
kHardwareId=FifIddDriver
kHardwareIdsMultiSz=FifIddDriver\0\0
kCompatibleIdsMultiSz=FifIddDriver\0\0
SwDeviceCreate(kEnumeratorName, kParentDevice, &create_info, ...)
```

Parsed result:

```text
LAUNCHER_ENUMERATOR=FifScreenIdd
LAUNCHER_CREATE_ID=FifIddDriver
LAUNCHER_HARDWARE_ID=FifIddDriver
LAUNCHER_COMPATIBLE_ID=FifIddDriver
EXPECTED_INSTANCE_ID_PATTERN=SWD\FifScreenIdd\FifIddDriver
```

The exact runtime device instance ID is returned by the SwDeviceCreate callback
only after create. Create is forbidden in this phase, so the post-reboot gate
uses the expected software-device instance pattern plus the exact hardware ID
match as the install readiness proof.

## Official IddSample Pattern

Official sample app:

```text
instanceId=IddSampleDriver
hardwareIds=IddSampleDriver\0\0
compatibleIds=IddSampleDriver\0\0
SwDeviceCreate(IddSampleDriver, HTREE\ROOT\0, ...)
```

Official sample INF:

```text
%DeviceName%=MyDevice_Install, Root\IddSampleDriver
%DeviceName%=MyDevice_Install, IddSampleDriver
```

Conclusion:

```text
OFFICIAL_PATTERN=INF has Root\<id> and <id>; app uses <id> as SwDeviceCreate hardware ID.
FIFSCREEN_PATTERN_MATCHES_OFFICIAL_SAMPLE=YES
```

## Identity Interpretation

```text
Root\FifScreenIdd is an INF root-enumerated hardware ID path.
FifIddDriver is the INF software-device hardware ID.
FifIddDriver is also the UMDF service name.
FifIddDriver is also the launcher SwDeviceCreate instance ID.
The PnP driver match is proven by the launcher hardware ID FifIddDriver matching the INF Models hardware ID FifIddDriver.
```

## Result

```text
INF_MATCH_ID=FifIddDriver
LAUNCHER_CREATE_ID=FifIddDriver
EXPECTED_INSTANCE_ID=SWD\FifScreenIdd\FifIddDriver
HARDWARE_ID_MATCH=YES
ROOT_FIFSCREENIDD_IS_HARDWARE_ID=YES, but not the launcher create hardware ID for this gate
FIFIDDDRIVER_IS_HARDWARE_ID=YES
FIFIDDDRIVER_IS_SERVICE_NAME=YES
FIFIDDDRIVER_IS_LAUNCHER_INSTANCE_ID=YES
FINAL_STATE=HARDWARE_ID_MATCH_PROVEN
```
