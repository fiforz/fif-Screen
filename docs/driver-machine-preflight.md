# Driver Machine Preflight

Date: 2026-07-08

Scope: read-only system audit before any FifIddDriver installation.

## Snapshot Artifacts

Directory:

```text
D:\Documents\fif-Screen\artifacts\pre-driver-install
```

Files:

```text
drivers-before.txt
devices-before.txt
display-devices-before.txt
boot-state-before.txt
security-state-before.txt
```

No BitLocker recovery key, private key, password, token, or certificate private material was saved.

## System State

Source:

```text
artifacts/pre-driver-install/boot-state-before.txt
artifacts/pre-driver-install/display-devices-before.txt
artifacts/pre-driver-install/security-state-before.txt
```

Parsed Result:

```text
Windows edition=Microsoft Windows 10 Pro
Windows version=10.0.19045
OS build=19045
architecture=64-bit
boot_firmware=UEFI
```

## Secure Boot

Command:

```powershell
Confirm-SecureBootUEFI
```

Raw Output:

```text
False
```

Parsed Result:

```text
secure_boot_enabled=false
secure_boot_modified=false
```

## TESTSIGNING

Command:

```powershell
bcdedit /enum
```

Raw Output:

```text
testsigning entry not present in bcdedit output
```

Parsed Result:

```text
testsigning_enabled=false
bcd_modified=false
```

## BitLocker

Commands:

```powershell
Get-BitLockerVolume
manage-bde -status
```

Raw Output:

```text
MountPoint VolumeType       ProtectionStatus LockStatus EncryptionPercentage EncryptionMethod HasRecoveryProtector
C:         OperatingSystem  Off              Unlocked   0                    None             False
D:         Data             Off              Unlocked   0                    None             False

Volume C:
Conversion Status:    Fully Decrypted
Protection Status:    Protection Off
Lock Status:          Unlocked
Key Protectors:       None Found

Volume D:
Conversion Status:    Fully Decrypted
Protection Status:    Protection Off
Lock Status:          Unlocked
Key Protectors:       None Found
```

Parsed Result:

```text
os_volume_bitlocker_protected=false
data_volume_bitlocker_protected=false
recovery_protector_present=false
RECOVERY_KEY_CONFIRMED_BY_USER=NO
bitlocker_modified=false
```

## HVCI / Memory Integrity

Raw Output:

```text
Enabled=0
Locked=0
VirtualizationBasedSecurityStatus=0
SecurityServicesConfigured={0}
SecurityServicesRunning={0}
CodeIntegrityPolicyEnforcementStatus=0
UsermodeCodeIntegrityPolicyEnforcementStatus=0
```

Parsed Result:

```text
memory_integrity_enabled=false
vbs_running=false
```

## Current Display Devices

Raw Output:

```text
Display adapters:
Todesk Virtual Display Adapter, ROOT\DISPLAY\0000, driver 16.44.2.509
AMD Radeon RX 5700, PCI\VEN_1002&DEV_731F&SUBSYS_57051682&REV_C4\6&256B734F&0&00000019, driver 32.0.21030.2001, 2560x1440 @ 144

PnP Display:
OK Todesk Virtual Display Adapter
OK AMD Radeon RX 5700

PnP Monitor:
OK/Unknown generic PnP monitor entries, full instance IDs saved in display-devices-before.txt
```

Parsed Result:

```text
physical_gpu_present=AMD Radeon RX 5700
existing_virtual_display_adapter=Todesk Virtual Display Adapter
fifscreen_driver_installed=false
fifscreen_virtual_monitor_present=false
display_devices_modified=false
```

