# Android USB Reconnect Test

Date: 2026-07-08

## Reverse Mapping Repeatability

Commands:

```powershell
adb -s 10AE7N2FX900178 reverse --remove tcp:27183
adb -s 10AE7N2FX900178 reverse --remove tcp:27184
adb -s 10AE7N2FX900178 reverse tcp:27183 tcp:27183
adb -s 10AE7N2FX900178 reverse tcp:27184 tcp:27184
adb -s 10AE7N2FX900178 reverse --list
adb -s 10AE7N2FX900178 reverse --remove tcp:27183
adb -s 10AE7N2FX900178 reverse --remove tcp:27184
adb -s 10AE7N2FX900178 reverse --list
adb -s 10AE7N2FX900178 reverse tcp:27183 tcp:27183
adb -s 10AE7N2FX900178 reverse tcp:27184 tcp:27184
adb -s 10AE7N2FX900178 reverse --list
```

Raw Output:

```text
initial remove tcp:27183: adb.exe: error: listener 'tcp:27183' not found
initial remove tcp:27184: adb.exe: error: listener 'tcp:27184' not found

create tcp:27183: 27183
create tcp:27184: 27184

UsbFfs tcp:27183 tcp:27183
UsbFfs tcp:27184 tcp:27184

after remove:
<empty>

recover tcp:27183: 27183
recover tcp:27184: 27184

UsbFfs tcp:27183 tcp:27183
UsbFfs tcp:27184 tcp:27184
```

Parsed Result:

```text
create=passed
check=passed
remove=passed
recover=passed
duplicate_mapping_loop=no
```

The initial remove errors were expected cleanup of a missing listener, not a link failure.

## App And Host Restart Recovery

Commands:

```powershell
adb -s 10AE7N2FX900178 shell am force-stop com.fif.screen
adb -s 10AE7N2FX900178 shell am start -n com.fif.screen/com.fif.screen.MainActivity
D:\Documents\fif-Screen\build\stage-host-clean\windows-host\fif-host.exe --no-adb --control-port 27183 --video-port 27184
```

Host Output:

```text
control client connected
FIFSCREEN_HOST event=hello_received windows_timestamp_ns=86287542899300
FIFSCREEN_HOST event=hello_ack_sent windows_timestamp_ns=86287551761200
video client connected; encoder/capture path is not wired yet
```

Android Output:

```text
FIFSCREEN_NETWORK event=hello_sent android_timestamp_ns=653774726542529
FIFSCREEN_NETWORK event=hello_ack_received android_timestamp_ns=653774741562998 rtt_ms=15.020469
FIFSCREEN_DECODER event=selected name=OMX.qcom.video.decoder.avc mime=video/avc
FIFSCREEN_DECODER event=low_latency_requested api=36
FIFSCREEN_DECODER event=started name=OMX.qcom.video.decoder.avc
FIFSCREEN_NETWORK event=connect_success host=127.0.0.1 port=27184
```

Parsed Result:

```text
app_restart_recovery=passed
host_restart_recovery=passed
android_monotonic_handshake_rtt_ms=15.020469
```

## Final Cleanup

Command:

```powershell
adb -s 10AE7N2FX900178 reverse --remove tcp:27183
adb -s 10AE7N2FX900178 reverse --remove tcp:27184
adb -s 10AE7N2FX900178 reverse --list
```

Raw Output:

```text
<empty>
```

Parsed Result:

```text
adb_reverse_leftovers=no
```

## Physical USB Unplug/Replug

Status:

```text
executed
```

Precondition:

```text
serial=10AE7N2FX900178
pre_unplug_transport_id=2
adb_reverse_before_unplug=UsbFfs tcp:27183 tcp:27183; UsbFfs tcp:27184 tcp:27184
host_pid=37268
android_pid=2692
pre_unplug_hello=passed
pre_unplug_hello_ack=passed
pre_unplug_handshake_rtt_ms=15.962291
```

Disconnect Evidence:

```text
disconnect_poll_start=2026-07-08T09:15:22.9281365+08:00
disconnect_observed=2026-07-08T09:18:03.1130785+08:00
adb devices -l after physical unplug:
List of devices attached
<empty>
```

Host Evidence:

```text
control client disconnected
video client disconnected
waiting for Android control client
waiting for Android video client
host_process_alive=true
host_cpu_after_disconnect=0.015625
host_crash=false
unhandled_exception=false
busy_loop_observed=false
```

Android Evidence after replug:

```text
FIFSCREEN_NETWORK event=client_error message=socket_closed
FIFSCREEN_NETWORK event=cleanup
FIFSCREEN_DECODER event=stopped name=OMX.qcom.video.decoder.avc
fatal_exception=false
anr=false
native_crash=false
```

Reconnect Evidence:

```text
reconnect_poll_start=2026-07-08T09:18:20.3466475+08:00
reconnect_observed=2026-07-08T09:18:20.3783229+08:00
final_status=device
final_line=10AE7N2FX900178        device product:PD2403 model:V2403A device:PD2403 transport_id:3
```

Reverse Recovery:

```text
adb reverse tcp:27183 tcp:27183 -> 27183
adb reverse tcp:27184 tcp:27184 -> 27184
adb reverse --list:
UsbFfs tcp:27183 tcp:27183
UsbFfs tcp:27184 tcp:27184
```

Recovered Handshake:

```text
Host:
FIFSCREEN_HOST event=hello_received windows_timestamp_ns=89318567819200
FIFSCREEN_HOST event=hello_ack_sent windows_timestamp_ns=89318579949600

Android:
FIFSCREEN_NETWORK event=connect_success host=127.0.0.1 port=27183
FIFSCREEN_NETWORK event=hello_sent android_timestamp_ns=656368468928831
FIFSCREEN_NETWORK event=hello_ack_received android_timestamp_ns=656368486624560 rtt_ms=17.695729
FIFSCREEN_NETWORK event=connect_success host=127.0.0.1 port=27184
```

Parsed Result:

```text
physical_usb_unplug=true
physical_usb_replug=true
adb_recovered=true
reverse_recovered=true
socket_recovered=true
hello_recovered=true
hello_ack_recovered=true
host_crash=false
android_app_crash=false
android_control_path_hardware_verified=true
```

Timing:

```text
ADB recovery time from disconnect observed to device observed: about 17.265 seconds
Control path recovery time from disconnect observed to HelloAck received: about 55.401 seconds
Recovered handshake RTT: 17.695729 ms
```

Notes:

```text
Reconnect Time != Handshake RTT
Handshake RTT != Video End-to-End Latency
Current MVP reconnect is manual recovery: recreate reverse and tap Start. Automatic reconnect/backoff is not implemented yet.
```

Final cleanup:

```text
adb reverse --list after cleanup:
<empty>
fif-host process after cleanup:
<none>
```
