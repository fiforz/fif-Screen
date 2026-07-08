# Capture Benchmark

Status: not yet runnable on this machine.

## Prerequisites

- Visual Studio/MSVC.
- Windows SDK.
- WDK with IddCx headers and libraries.
- First FifScreen virtual display driver build.
- Host capture prototype.

## Candidate A: Capture Virtual Monitor

APIs:

- DXGI Desktop Duplication.
- Windows Graphics Capture.
- D3D11 texture handoff.

Expected benefit: fastest route after the virtual monitor exists.

Expected cost: likely extra capture indirection and possible GPU copy.

## Candidate B: Use IddCx SwapChain Surface

APIs:

- IddCx swapchain assignment callback.
- Direct3D surface handling.
- Encoder input texture path.

Expected benefit: fewer copies and lower latency.

Expected cost: more driver/host integration complexity.

## Measurements To Record

| Path | Feasible | GPU Copies | CPU Copies | CPU % | GPU % | Encode ms | End-to-end ms | Complexity | Notes |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- |
| A | Not measured | TBD | TBD | TBD | TBD | TBD | TBD | Medium | Blocked by WDK/toolchain |
| B | Not measured | TBD | TBD | TBD | TBD | TBD | TBD | High | Blocked by WDK/toolchain |

No performance claims are made before measurements exist.

