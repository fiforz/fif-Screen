# Latency Measurement

All timestamps use monotonic clocks. Wall-clock time is only for logs.

## Windows

- T0: display frame acquired.
- T1: frame submitted to encoder.
- T2: encoded packet produced.
- T3: packet send begins.

## Android

- T4: packet received.
- T5: packet submitted to `MediaCodec`.
- T6: decoder output rendered or released to `Surface`.

## Metrics

- Capture latency: T1 - T0.
- Encode latency: T2 - T1.
- Send queue latency: T3 - T2.
- Transport/receive latency: T4 - T3, only reliable after clock sync or loopback calibration.
- Decode latency: T6 - T5.
- End-to-end latency: T6 - T0, only after cross-device timestamp calibration.

## Targets

Initial target for 1080p60 USB mode:

- Internal Windows pipeline below 20 ms where possible.
- End-to-end p50 below 35 ms where possible.
- End-to-end p95 below 50 ms where possible.

If measurements miss these targets, the result must be recorded with the bottleneck, not hidden behind buffered playback.

