# FifScreen Protocol Spec

Status: draft v0.1 for USB MVP.

## Goals

One protocol must serve USB, LAN, and future remote transports. The transport only moves bytes. It must not know about H.264, display modes, or input semantics.

MVP uses two independent TCP connections over `adb reverse`:

- Control channel: handshake, ping/pong, display mode, codec config, statistics, errors.
- Video channel: encoded H.264 frames, timestamps, frame IDs, IDR/config flags.

This prevents large video packets from blocking control messages.

## Byte Order

All fixed header fields are little endian.

## Packet Header

Every packet starts with a fixed 32-byte header:

| Offset | Size | Field | Description |
| --- | ---: | --- | --- |
| 0 | 4 | magic | ASCII `FIF1` |
| 4 | 2 | version | Protocol version, currently `1` |
| 6 | 2 | type | Message type |
| 8 | 4 | payload_length | Payload bytes after the header |
| 12 | 8 | sequence | Per-channel monotonically increasing sequence |
| 20 | 8 | timestamp_ns | Sender monotonic timestamp in nanoseconds |
| 28 | 4 | flags | Message-specific flags |

Maximum payloads:

- Control: 1 MiB.
- Video: 16 MiB.

Receivers must reject larger payloads and close the channel with an error.

## Message Types

| Type | Name | Channel | Payload |
| ---: | --- | --- | --- |
| 1 | Hello | Control | UTF-8 JSON |
| 2 | HelloAck | Control | UTF-8 JSON |
| 3 | Ping | Control | Empty or UTF-8 JSON |
| 4 | Pong | Control | Empty or UTF-8 JSON |
| 5 | DisplayMode | Control | UTF-8 JSON |
| 6 | CodecConfig | Control | UTF-8 JSON plus optional binary config on video channel |
| 7 | Stats | Control | UTF-8 JSON |
| 8 | RequestIdr | Control | Empty |
| 9 | Disconnect | Control | UTF-8 JSON |
| 100 | VideoConfig | Video | Codec config bytes, such as SPS/PPS for H.264 |
| 101 | VideoFrame | Video | Encoded access unit |
| 200 | InputEvent | Control | Reserved for phase 2 |
| 900 | Error | Control | UTF-8 JSON |

## Video Flags

| Flag | Value | Applies To |
| --- | ---: | --- |
| CodecConfig | `0x00000001` | VideoConfig, VideoFrame |
| IdrFrame | `0x00000002` | VideoFrame |
| DroppedBefore | `0x00000004` | VideoFrame |

## Handshake

Android connects to `127.0.0.1:<control_port>` after `adb reverse` is active.

Android sends `Hello`:

```json
{
  "role": "android-client",
  "protocol": 1,
  "appVersion": "0.1",
  "screen": {"width": 1920, "height": 1080, "refreshHz": 60},
  "decoders": [{"codec": "video/avc", "name": "decoder-name", "lowLatency": true}]
}
```

Windows replies `HelloAck`:

```json
{
  "role": "windows-host",
  "protocol": 1,
  "controlPort": 27183,
  "videoPort": 27184,
  "selectedMode": {"width": 1280, "height": 720, "refreshHz": 60},
  "codec": {"mime": "video/avc", "profile": "baseline", "lowLatency": true}
}
```

Android then opens the video channel.

## Required Receiver Behavior

Receivers must handle:

- Partial packets.
- Coalesced packets.
- Invalid magic/version.
- Invalid or excessive payload length.
- Disconnect during header or payload.
- Timeout.
- Android app restart.
- USB unplug.
- Windows host restart.

## Queue Rule

Every video queue is bounded. If the consumer falls behind, drop old frames and request or send an IDR frame. Real-time display is more important than showing every stale frame.

