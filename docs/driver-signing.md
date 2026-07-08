# Driver Signing

## Development

FifScreen driver development requires test signing for local driver loading.

Manual gate before enabling test signing:

- Why: Windows will not load a locally built unsigned kernel/UMDF driver package.
- Command usually involved: `bcdedit /set testsigning on`.
- Impact: requires administrator rights and a reboot; Windows enters test-signing mode.
- Recovery: `bcdedit /set testsigning off`, then reboot.

This repository does not run that command automatically.

## Test Certificate

A local test certificate may be used for development driver packages. Installing it as a trusted root is a manual gate because it changes machine trust state.

## Production

Normal Windows 10 users should not be asked to disable security features or trust arbitrary self-signed roots.

Production distribution requires the Microsoft driver signing path:

- Microsoft Hardware Developer Program account.
- EV code signing certificate where required by the submission flow.
- HLK or applicable attestation/signing workflow.
- Microsoft-signed driver package for user installation.

Do not represent a self-signed development package as production-ready.

