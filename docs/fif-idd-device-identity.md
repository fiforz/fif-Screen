# FifScreen IDD Device Identity

Date: 2026-07-08

Source:

```text
D:\Documents\fif-Screen\windows-driver\FifIddDriver\x64\Debug\FifIddDriver\FifIddDriver.inf
```

## INF Identity

```text
Manufacturer=FifScreen
Provider=FifScreen
Class=Display
ClassGuid={4D36E968-E325-11CE-BFC1-08002BE10318}
CatalogFile=FifIddDriver.cat
DriverVer=07/07/2026,23.7.28.287
```

## Hardware IDs

```text
Root\FifScreenIdd
FifIddDriver
```

Interpretation:

```text
Root\FifScreenIdd is available as a root-enumerated hardware ID path.
FifIddDriver is the software-device hardware ID used by the launcher, matching the official IddSampleApp pattern.
```

## UMDF Service

```text
UmdfService=FifIddDriver,FifIddDriver_UmdfService
UmdfServiceOrder=FifIddDriver
UmdfKernelModeClientPolicy=AllowKernelModeClients
UmdfLibraryVersion=2.25.0
UmdfExtensions=IddCx0102
ServiceBinary=%12%\UMDF\FifIddDriver.dll
```

## Device Description

```text
DeviceName="FifScreen Indirect Display"
DiskName="FifScreen Idd Driver Installation Disk"
DeviceGroupId=FifScreenIddGroup
UpperFilters=IndirectKmd
```

## Device Creator Decision

```text
DEVICE_CREATOR=windows-driver/FifIddDeviceLauncher
launcher_enumerator=FifScreenIdd
launcher_parent=HTREE\ROOT\0
launcher_instance_id=FifIddDriver
launcher_hardware_id=FifIddDriver
```

This matches the INF software-device hardware ID exactly.

