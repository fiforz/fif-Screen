# Driver Install Gate

Date: 2026-07-07

## Current Status

`FifIddDriver` has been compiled and packaged. It has not been installed.

Install is intentionally gated because it changes Windows driver state and may require trusted signing, test-signing, and a reboot depending on the chosen path.

## Current Package

```text
D:\Documents\fif-Screen\windows-driver\FifIddDriver\x64\Debug\FifIddDriver.inf
D:\Documents\fif-Screen\windows-driver\FifIddDriver\x64\Debug\FifIddDriver.dll
D:\Documents\fif-Screen\windows-driver\FifIddDriver\x64\Debug\FifIddDriver\fifidddriver.cat
```

## Blocking Evidence

`signtool verify /pa /v` fails for both DLL and CAT because the WDK build certificate is not trusted:

```text
Issued to: WDKTestCert 29989,134279100762949792
SignTool Error: A certificate chain processed, but terminated in a root certificate which is not trusted by the trust provider.
```

## Actions Not Performed

- Did not run `pnputil /add-driver`.
- Did not install a certificate.
- Did not add a certificate to Trusted Root.
- Did not enable test signing.
- Did not run `bcdedit`.
- Did not change Secure Boot.
- Did not reboot.

## Manual Approval Required Before Any Install Path

Before continuing, the user must explicitly choose and approve one path:

1. Trusted development signing path.
2. Test-signing path.
3. Production signing path.

Only after that approval should any script run `pnputil`, install certificates, change boot settings, or request a reboot.
