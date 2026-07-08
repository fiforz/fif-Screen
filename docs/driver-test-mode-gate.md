# Driver Test Mode Gate

Date: 2026-07-08

Scope: exact public test certificate import and TESTSIGNING BCD enablement. No reboot was performed.

## Package Signer

```text
subject=CN="WDKTestCert 29989,134279100762949792"
issuer=CN="WDKTestCert 29989,134279100762949792"
serial=379987E82B219EAE46DF4258DEA5669D
thumbprint_sha1=09FE7D270B6B9D00BBCE41FEC84F826D4E687009
fingerprint_sha256=87E272311823B7B2B739E9A177CCA20C8783F946F02ED42AC99172CBCF2C471D
valid_from=2026-07-07T23:01:16+08:00
valid_to=2036-07-07T08:00:00+08:00
eku=Code Signing
timestamp=<none>
```

## CER Match

Candidate:

```text
D:\Documents\fif-Screen\windows-driver\FifIddDriver\x64\Debug\FifIddDriver.cer
```

Match result:

```text
SubjectMatch=True
SerialMatch=True
ThumbprintMatch=True
PublicKeyMatch=True
DLL_CAT_SameSigner=True
CER_PACKAGE_MATCH=True
candidate_has_private_key=False
```

## Certificate Store Before

```text
LocalMachine\Root exact thumbprint present: False
LocalMachine\TrustedPublisher exact thumbprint present: False
```

## Certificate Import

Commands:

```powershell
Import-Certificate -FilePath D:\Documents\fif-Screen\windows-driver\FifIddDriver\x64\Debug\FifIddDriver.cer -CertStoreLocation Cert:\LocalMachine\Root
Import-Certificate -FilePath D:\Documents\fif-Screen\windows-driver\FifIddDriver\x64\Debug\FifIddDriver.cer -CertStoreLocation Cert:\LocalMachine\TrustedPublisher
```

Results:

```text
root_exit_code=0
root_import_thumbprint=09FE7D270B6B9D00BBCE41FEC84F826D4E687009
trustedpublisher_exit_code=0
trustedpublisher_import_thumbprint=09FE7D270B6B9D00BBCE41FEC84F826D4E687009
private_key_imported=false
pfx_imported=false
```

## Certificate Store After

```text
LocalMachine\Root exact thumbprint present: True
LocalMachine\TrustedPublisher exact thumbprint present: True
HasPrivateKey=False
```

## Signature Verification After Import

```text
Get-AuthenticodeSignature FifIddDriver.dll: Valid
Get-AuthenticodeSignature fifidddriver.cat: Valid

signtool verify /pa /v FifIddDriver.dll: exit=0
signtool verify /pa /v fifidddriver.cat: exit=0

signtool verify /kp /v FifIddDriver.dll: exit=1, does not chain to a Microsoft Root Cert
signtool verify /kp /v fifidddriver.cat: exit=1, does not chain to a Microsoft Root Cert
```

Final signature status:

```text
TEST SIGNATURE TRUSTED ON THIS DEVELOPMENT MACHINE
PRODUCTION SIGNED=false
```

## Safety Check Before TESTSIGNING

```text
Confirm-SecureBootUEFI=False
BitLockerMountPoint=C:
BitLockerVolumeType=OperatingSystem
BitLockerProtectionStatus=Off
BitLockerLockStatus=Unlocked
BitLockerEncryptionPercentage=0
BitLockerEncryptionMethod=None
Administrator=True
TESTSIGNING_BEFORE=False
```

## BCD Before

Saved:

```text
D:\Documents\fif-Screen\artifacts\pre-driver-install\bcd-before-testsigning.txt
```

## TESTSIGNING Command

Command:

```cmd
bcdedit /set testsigning on
```

Result:

```text
exit_code=0
stdout_stderr=The operation completed successfully.
```

Only this BCD setting was changed. No `nointegritychecks`, `debug`, `bootlog`, `safeboot`, `recoveryenabled`, or `bootstatuspolicy` change was executed.

## BCD After

Saved:

```text
D:\Documents\fif-Screen\artifacts\pre-driver-install\bcd-after-testsigning.txt
```

Parsed result:

```text
testsigning Yes
```

## Boundary

```text
reboot_required=true
reboot_performed=false
driver_package_installed=false
software_device_created=false
virtual_monitor_verified=false
FINAL_STATE=REBOOT_REQUIRED_FOR_DRIVER_TEST_MODE
```

