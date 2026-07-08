# Display Recovery

Date: 2026-07-08

Scope: recovery plan for FifScreen driver testing. Do not remove AMD, NVIDIA, Intel, or other real GPU drivers when recovering FifScreen.

## General Order

Use the lowest-risk action that matches the failure:

```text
1. Unplug Android USB.
2. Stop FifScreen Host.
3. Check Windows display settings.
4. Disable or remove the exact FifScreen device only.
5. Uninstall the exact FifScreen driver package only.
6. Use Safe Mode if normal desktop access is broken.
```

## Case A: Virtual Display Appears But Is Not Usable

```text
1. Unplug Android.
2. Stop fif-host.exe.
3. Run scripts\driver-state-check.ps1 to identify exact FifScreen device/package state.
4. Restart FifScreen Host only after the device state is understood.
5. If the device remains bad, remove the exact FifScreen device, then rescan.
```

Do not remove the physical GPU driver.

## Case B: Windows Display Settings Are Abnormal

```text
1. Use Win+P and select PC screen only.
2. Open Display Settings and disable the FifScreen display if visible.
3. Stop FifScreen Host.
4. Unplug Android.
5. If the display remains present, remove the exact FifScreen device.
```

Do not delete all monitor or display adapter entries.

## Case C: Driver Device Code 10 / Code 31 / Code 39

```text
1. Capture state with scripts\driver-state-check.ps1.
2. Confirm the failing device instance is FifScreen, not AMD/NVIDIA/Intel.
3. Remove the exact FifScreen device instance.
4. Rescan devices.
5. If the error returns, uninstall the exact FifScreen OEM INF package.
```

## Case D: UMDF Host Crash

```text
1. Stop FifScreen Host.
2. Unplug Android.
3. Capture Windows Event Viewer UMDF crash details.
4. Capture driver state with scripts\driver-state-check.ps1.
5. Remove the exact FifScreen device only if UMDF repeatedly crashes.
```

## Case E: Black Screen After Login

```text
1. Wait 30 seconds for Windows shell recovery.
2. Press Win+P, arrow to PC screen only, then Enter.
3. Unplug Android.
4. Stop FifScreen Host from Task Manager if visible.
5. If the desktop returns, uninstall the exact FifScreen device/package before retrying.
```

Do not remove the real GPU driver after regaining the desktop.

## Case F: Cannot Enter Desktop Normally

```text
1. Boot Windows Recovery Environment.
2. Enter Safe Mode.
3. Remove the exact FifScreen device if present.
4. Uninstall the exact FifScreen OEM INF package if known.
5. Reboot normally.
```

If the exact FifScreen OEM INF is unknown, do not use a wildcard delete. First identify it from `pnputil /enum-drivers` or from the pre-install snapshot.

