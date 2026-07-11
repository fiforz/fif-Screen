# FifScreen Protocol Spec

Status: draft v0.2 for USB and LAN transports.

## Goals

One protocol must serve USB, LAN, and future remote transports. The transport only moves bytes. It must not know about H.264, display modes, or input semantics.

USB and LAN use two independent TCP connections:

- Control channel: handshake, ping/pong, display mode, codec config, input, statistics, errors.
- Video channel: encoded H.264 frames, timestamps, frame IDs, IDR/config flags.

This prevents large video packets from blocking control messages.

Transport binding:

- USB: Windows binds TCP to `127.0.0.1`; Android reaches it through `adb reverse`.
- LAN: Windows binds TCP to local interfaces and answers discovery on UDP `27182`; host firewall rules restrict access to `LocalSubnet`.
- Relay: endpoint-directory and host transport interfaces are reserved, but no server transport is implemented.

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
| 10 | PairChallenge | Control | 56-byte binary challenge |
| 11 | PairResponse | Control | 68-byte binary client proof |
| 12 | PairResult | Control | 36-byte binary result and host proof |
| 13 | VideoChallenge | Video | 36-byte binary nonce |
| 14 | VideoAuth | Video | 36-byte binary proof |
| 100 | VideoConfig | Video | Codec config bytes, such as SPS/PPS for H.264 |
| 101 | VideoFrame | Video | Encoded access unit |
| 200 | InputEvent | Control | Versioned binary input payload |
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
  "decoders": [{"codec": "video/avc", "name": "decoder-name", "lowLatency": true}],
  "input": {"touch": true, "maxContacts": 16}
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
  "codec": {"mime": "video/avc", "profile": "baseline", "lowLatency": true},
  "input": {"touch": true, "maxContacts": 16}
}
```

Android then opens the video channel.

## LAN Discovery

Android sends a 20-byte UDP datagram to port `27182` on the limited broadcast address and each active IPv4 interface broadcast address. The PIN is never included.

| Offset | Size | Request | Response |
| --- | ---: | --- | --- |
| 0 | 8 | ASCII `FIFDISC1` | ASCII `FIFHERE1` |
| 8 | 2 | Protocol version `1` | Protocol version `1` |
| 10 | 2 | Reserved zero | Reserved zero |
| 12 | 2 | Zero | Control TCP port |
| 14 | 2 | Zero | Video TCP port |
| 16 | 4 | Random request nonce | Echoed request nonce |

Android accepts only a correctly sized response with matching magic, version, nonce, and nonzero ports. It connects to the source IPv4 address of that response.

## LAN Pairing

The user enters the same four ASCII digits on Windows and Android. The PIN is held in process memory only and is not passed on the host command line.

1. Windows sends `PairChallenge`: payload version, `100000` PBKDF2 iterations, a random 16-byte salt, and a random 32-byte server nonce.
2. Android derives a 32-byte PIN key using PBKDF2-HMAC-SHA256.
3. Android creates a random 32-byte client nonce and sends `PairResponse` with `HMAC(pin_key, "FifScreen/control/v1" || server_nonce || client_nonce)`.
4. Both sides derive `session_key = HMAC(pin_key, "FifScreen/session/v1" || server_nonce || client_nonce)`.
5. Windows validates the client proof and returns `PairResult` with an accepted byte and `HMAC(session_key, "FifScreen/accepted/v1")`.
6. Android validates the host proof before sending the normal `Hello` packet.
7. On the video connection Windows sends a random nonce in `VideoChallenge`; Android replies with `HMAC(session_key, "FifScreen/video/v1" || video_nonce)` in `VideoAuth`.
8. Windows consumes the session after one successful video authentication. Unused sessions expire after 30 seconds.

All pairing binary payloads start with payload version `1`; reserved bytes must be zero. Pairing reads time out after 7 seconds. Failed PIN attempts are serialized by the control listener and delayed before retry.

The LAN handshake authenticates both peers against the shared PIN, but protocol version 1 does not encrypt subsequent video or control traffic. LAN mode is therefore limited to trusted local networks. A future relay transport must add an encrypted channel and must not weaken the USB loopback behavior.

## Touch Input

Android sends one `InputEvent` packet for each `MotionEvent`. A touch frame always contains
all pointers reported by that Android event. Coordinates and contact dimensions are normalized
to `0..65535` relative to the rendered video surface.

Touch frame header:

| Offset | Size | Field | Description |
| --- | ---: | --- | --- |
| 0 | 1 | kind | `1` for a touch frame |
| 1 | 1 | payload_version | Currently `1` |
| 2 | 1 | contact_count | `1..16` |
| 3 | 1 | reserved | Must be zero |

Each contact then occupies 14 bytes:

| Offset | Size | Field | Description |
| --- | ---: | --- | --- |
| 0 | 2 | pointer_id | `1..256`, unique for the contact lifetime |
| 2 | 1 | phase | `1` down, `2` move, `3` up, `4` cancel |
| 3 | 1 | reserved | Must be zero |
| 4 | 2 | x | Normalized horizontal coordinate |
| 6 | 2 | y | Normalized vertical coordinate |
| 8 | 2 | pressure | `0..1024` |
| 10 | 2 | major | Normalized major contact diameter |
| 12 | 2 | minor | Normalized minor contact diameter |

Receivers must reject duplicate IDs, invalid phases, unsupported payload versions, and frames
whose length does not exactly match `contact_count`. On control-channel disconnect, Windows must
cancel every injected contact still active.

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
