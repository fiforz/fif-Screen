# Architecture

## First Principles

The product is not screen mirroring. Windows must expose an independent display target, and Android is only the physical presentation device for that target.

Therefore the MVP has four hard boundaries:

1. Display identity belongs to a Windows IddCx/UMDF driver.
2. Frame production belongs to Windows capture or IddCx swapchain handling.
3. Video transport is byte movement only.
4. Android decodes encoded frames directly to a `SurfaceView`.

## MVP Topology

```text
Windows IddCx Virtual Monitor
        |
        v
Capture or IddCx SwapChain
        |
        v
IVideoEncoder
        |
        v
Bounded encoded-frame queue
        |
        v
TCP server on Windows
        |
        v
adb reverse
        |
        v
Android localhost TCP client
        |
        v
MediaCodec -> SurfaceView
```

## Processes And Modules

### windows-driver

Owns the virtual display device. It must use IddCx and UMDF 2. It does not own Android connections, H.264 policy, or network transport.

MVP role:

- Create one virtual monitor.
- Expose fixed `1280x720@60` first, then `1920x1080@60`.
- Surface the swapchain path for later direct encode.

### windows-host

Owns runtime orchestration.

MVP role:

- Detect adb.
- Run `adb reverse tcp:27183 tcp:27183` and `adb reverse tcp:27184 tcp:27184`.
- Listen for Android control and video connections.
- Negotiate mode and codec.
- Push encoded H.264 packets over the video channel.
- Keep bounded queues and latency statistics.

### android-client

Owns device presentation.

MVP role:

- Connect to `127.0.0.1:27183` and `127.0.0.1:27184`.
- Send `Hello`.
- Select H.264 hardware decoder.
- Decode to `SurfaceView`.
- Show connection state and real metrics.

### protocol

Owns the packet format used by all transports.

## Transport Decision

MVP uses `adb reverse` because it is the shortest path to a complete USB chain and avoids custom USB kernel work. The protocol is intentionally transport-neutral so later transports can replace TCP-over-ADB without touching encoder/decoder logic.

## Capture Decision

Two paths must be tested:

- A: capture the IddCx-created display via DXGI Desktop Duplication or Windows Graphics Capture.
- B: consume the IddCx swapchain/direct surface directly.

MVP should first use the path that can be made stable end-to-end. The final low-latency target is path B if it reduces GPU/CPU copies.

## Encoder Decision

The host exposes `IVideoEncoder`:

- `Initialize`
- `Reconfigure`
- `SubmitFrame`
- `PollPacket`
- `RequestIdr`
- `Shutdown`

Backends to evaluate:

- Media Foundation hardware H.264.
- AMD AMF on the current RX 5700 machine.
- NVENC and Intel Quick Sync later for portability.

Software encoding is only a debug fallback.

## Latency Rule

No unbounded video queues. If the Android client or USB path falls behind, drop old encoded frames and converge at the next IDR.

## Manual Gates

These are not automated:

- Enabling Windows test signing.
- Installing a test certificate.
- Installing the driver.
- Rebooting.
- Changing Secure Boot.
- Android USB debugging authorization on the device.

The code and docs can advance before those gates.

