# Driver Signature Audit

Date: 2026-07-08

Scope: read-only signature audit for the current FifIddDriver build output.

## Files

```text
D:\Documents\fif-Screen\windows-driver\FifIddDriver\x64\Debug\FifIddDriver\FifIddDriver.dll
D:\Documents\fif-Screen\windows-driver\FifIddDriver\x64\Debug\FifIddDriver\FifIddDriver.inf
D:\Documents\fif-Screen\windows-driver\FifIddDriver\x64\Debug\FifIddDriver\fifidddriver.cat
```

The package directory contains INF, DLL, and CAT together. The parent `x64\Debug` directory also contains copied build outputs.

## Hashes

Command:

```powershell
Get-FileHash -Algorithm SHA256
```

Parsed Result:

```text
FifIddDriver.dll SHA-256=ABE48FFE52647223B95ECA4B35D32258114297EB113B63A6D13DD0F62AAE2683
FifIddDriver.inf SHA-256=38D26D91B8628128C5750F26B3917F864CC5EE8F0AB588E35EA433C637E2D930
fifidddriver.cat SHA-256=D2E2F5A99648884B22F5109FF308BB340F0BA4453D5F58B3D660120581008F81
```

## INF Validation And CAT Association

Command:

```powershell
D:\Documents\fif-Screen\windows-driver\packages\Microsoft.Windows.WDK.x64.10.0.28000.1839\c\tools\10.0.28000.0\x64\infverif.exe /u /v FifIddDriver.inf
Select-String -LiteralPath FifIddDriver.inf -Pattern "CatalogFile|Class|ClassGuid|Provider|DriverVer|Manufacturer"
```

Raw Output:

```text
Running Universal INF check
Validating FifIddDriver.inf
INF is VALID
Checked 1 INF(s) in 0 m 0 s 5 ms

ClassGUID={4D36E968-E325-11CE-BFC1-08002BE10318}
Class=Display
Provider=%ManufacturerName%
CatalogFile=FifIddDriver.cat
DriverVer = 07/07/2026,23.7.28.287
ManufacturerName="FifScreen"
```

Parsed Result:

```text
inf_syntax_valid=true
class=Display
provider=FifScreen
catalog_association=FifIddDriver.cat
```

## Authenticode State

Command:

```powershell
Get-AuthenticodeSignature -LiteralPath FifIddDriver.dll
Get-AuthenticodeSignature -LiteralPath FifIddDriver.inf
Get-AuthenticodeSignature -LiteralPath fifidddriver.cat
```

Parsed Result:

```text
FifIddDriver.dll signature_type=Authenticode
FifIddDriver.dll status=UnknownError
FifIddDriver.dll status_message=A certificate chain processed, but terminated in a root certificate which is not trusted by the trust provider

FifIddDriver.inf signature_type=None
FifIddDriver.inf status=UnknownError
FifIddDriver.inf status_message=The form specified for the subject is not one supported or known by the specified trust provider

fifidddriver.cat signature_type=Authenticode
fifidddriver.cat status=UnknownError
fifidddriver.cat status_message=A certificate chain processed, but terminated in a root certificate which is not trusted by the trust provider
```

## Signing Certificate

Applies to:

```text
FifIddDriver.dll
fifidddriver.cat
```

Parsed Result:

```text
subject=CN="WDKTestCert 29989,134279100762949792"
issuer=CN="WDKTestCert 29989,134279100762949792"
serial=379987E82B219EAE46DF4258DEA5669D
thumbprint=09FE7D270B6B9D00BBCE41FEC84F826D4E687009
not_before=2026-07-07 23:01:16
not_after=2036-07-07 08:00:00
eku=Code Signing
timestamp=<none>
certificate_type=self-signed WDK test certificate
```

## SignTool Verification

Command:

```powershell
D:\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe verify /pa /v FifIddDriver.dll
D:\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe verify /pa /v fifidddriver.cat
D:\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe verify /kp /v FifIddDriver.dll
D:\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe verify /kp /v fifidddriver.cat
```

Raw Output:

```text
Issued to: WDKTestCert 29989,134279100762949792
Issued by: WDKTestCert 29989,134279100762949792
Expires: Mon Jul 07 08:00:00 2036
SHA1 hash: 09FE7D270B6B9D00BBCE41FEC84F826D4E687009
File is not timestamped.
Number of files successfully Verified: 0
Number of warnings: 0
Number of errors: 1
SignTool Error: A certificate chain processed, but terminated in a root certificate which is not trusted by the trust provider.
```

Parsed Result:

```text
file_content_corruption_detected=false
dll_signature_present=true
cat_signature_present=true
inf_authenticode_signature_present=false
catalog_association_present=true
certificate_is_test_certificate=true
certificate_chain_trusted=false
production_trust_chain=false
```

Conclusion:

```text
The driver package is structurally valid enough to pass infverif and contains DLL/CAT signatures.
The current signatures are WDK test signatures and are not trusted by the current machine.
This is not the same as "driver package invalid"; it is a trust-chain gate before installation.
```

