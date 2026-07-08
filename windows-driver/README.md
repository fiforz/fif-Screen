# FifScreen Windows Driver

This module owns the real Windows extended display identity.

Target stack:

- UMDF 2
- IddCx
- Indirect Display Driver

Current state:

- Real WDK project exists under `FifIddDriver/`.
- WDK/SDK packages are restored through NuGet under `windows-driver/packages/`.
- Debug x64 package builds successfully.
- INF validates with `infverif /u /v`.
- No driver installation, Secure Boot change, or test-signing change has been performed.

Manual gates remain required for:

- Installing the driver.
- Trusting or installing any development certificate.
- Enabling test signing.
- Rebooting.

Build evidence is recorded in `docs/driver-build.md`.
Install gate details are recorded in `docs/driver-install-gate.md`.
