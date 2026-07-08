# Driver Test Paths

Date: 2026-07-08

Goal: compare install/signing paths without executing any driver, certificate, BCD, Secure Boot, BitLocker, or reboot operation.

## Path A: Current WDKTestCert + Test Signing

```text
current_stage_applicability=development-only
secure_boot_change_required=likely yes if Secure Boot blocks TESTSIGNING; current machine reports Secure Boot false
testsigning_required=yes
certificate_import_required=yes, unless already trusted on target machine
reboot_required=yes for TESTSIGNING state changes
risk=high for normal users; changes boot policy and trust state
recovery=disable TESTSIGNING, remove test certificate, uninstall exact FifIddDriver package, reboot
ordinary_user_suitable=no
```

Notes:

```text
This is fastest for local development after explicit approval, but it must stay behind Driver Install Gate.
It is not a release path.
```

## Path B: Project-Specific Test Certificate + Test Signing

```text
current_stage_applicability=best development path after explicit approval
secure_boot_change_required=depends on machine policy; current machine reports Secure Boot false
testsigning_required=yes
certificate_import_required=yes, project-specific public certificate only
reboot_required=yes for TESTSIGNING state changes
risk=medium-high; better auditability than WDKTestCert, still development-only
recovery=disable TESTSIGNING, remove project test certificate, uninstall exact FifIddDriver package, reboot
ordinary_user_suitable=no
```

Notes:

```text
This keeps the development trust root scoped to FifScreen instead of a generic WDK-generated certificate.
The private key must never be committed.
```

## Path C: Microsoft Production Signing

```text
current_stage_applicability=release path, not a blocker for local development validation
secure_boot_change_required=no
testsigning_required=no
certificate_import_required=no for ordinary target machines
reboot_required=not for signing state itself; driver install may still require normal device restart behavior
risk=lowest for users
recovery=normal uninstall of exact driver package/device
ordinary_user_suitable=yes
```

Notes:

```text
This is the correct public distribution path.
It should remain separate from development signing so local test policy never leaks into release instructions.
```

## Recommendation For Next Gate

```text
development_gate=B, project-specific test certificate + explicit TESTSIGNING approval
release_gate=C, Microsoft production signing
current_status=WAITING_FOR_DRIVER_TEST_INSTALL_APPROVAL
```

