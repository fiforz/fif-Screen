# IDD Device Lifecycle

Date: 2026-07-08

Source:

```text
repository=microsoft/Windows-driver-samples
fixed_commit=2ee527bfeb0aeb6be11f0a8b6dce4011b358ce89
local_path=D:\Documents\fif-Screen\tools\research\_repos\Windows-driver-samples-2ee527bfeb0aeb6be11f0a8b6dce4011b358ce89
```

## Official Sample Mechanism

Files read:

```text
video\IndirectDisplay\IddSampleApp\main.cpp
video\IndirectDisplay\IddSampleDriver\IddSampleDriver.inf
```

Evidence:

```text
IddSampleApp\main.cpp:36 instanceId = L"IddSampleDriver"
IddSampleApp\main.cpp:37 hardwareIds = L"IddSampleDriver\0\0"
IddSampleApp\main.cpp:38 compatibleIds = L"IddSampleDriver\0\0"
IddSampleApp\main.cpp:46 CapabilityFlags = SWDeviceCapabilitiesRemovable | SWDeviceCapabilitiesSilentInstall | SWDeviceCapabilitiesDriverRequired
IddSampleApp\main.cpp:51 SwDeviceCreate(L"IddSampleDriver", L"HTREE\\ROOT\\0", ...)
IddSampleApp\main.cpp:90 SwDeviceClose(hSwDevice)

IddSampleDriver.inf:19 Root\IddSampleDriver
IddSampleDriver.inf:20 IddSampleDriver
IddSampleDriver.inf:49 UmdfService=IddSampleDriver,IddSampleDriver_Install
IddSampleDriver.inf:55 ServiceBinary=%12%\UMDF\IddSampleDriver.dll
```

Conclusion:

```text
Driver package staged
then IddSampleApp calls SwDeviceCreate
then Software Device instance is created under the software device enumerator
then Hardware ID "IddSampleDriver" matches the INF
then PnP loads the UMDF IddCx driver
then the driver creates adapter/monitor objects through IddCx callbacks
```

Important distinction:

```text
pnputil /add-driver /install stages/installs the package for matching devices.
It does not, by itself, prove that an indirect display software device instance will be created.
The official sample uses a companion app to create the software device.
```

## FifScreen Status

Project audit:

```text
SwDeviceCreate in existing FifScreen code before this phase: not found
Host software-device creation before this phase: not found
install script software-device creation before this phase: not found
other PnP enumeration mechanism before this phase: not found
```

Conclusion:

```text
DEVICE_CREATOR_BEFORE_THIS_PHASE=NOT_IMPLEMENTED
```

Action taken:

```text
Added windows-driver/FifIddDeviceLauncher/
Added CMake target fif-idd-device-launcher
Implemented create/status/remove commands using exact FifScreen IDs.
```

Action not taken:

```text
Driver package installed: NO
Software device created: NO
Launcher create executed: NO
Launcher remove executed: NO
Virtual monitor verified: NO
```

