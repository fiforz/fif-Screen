# Fif IDD Device Launcher Build

Date: 2026-07-08

## Created

```text
D:\Documents\fif-Screen\windows-driver\FifIddDeviceLauncher\CMakeLists.txt
D:\Documents\fif-Screen\windows-driver\FifIddDeviceLauncher\src\main.cpp
```

The launcher supports:

```text
create
status
remove
```

Design:

```text
create uses SwDeviceCreate with hardware ID FifIddDriver.
create holds the software device until X is pressed, then calls SwDeviceClose.
status enumerates exact FifScreen software device records only.
remove removes exact FifScreen software device records only.
```

## Build Command

```cmd
cmd.exe /d /c "call ""D:\SoftWare\Visual Studio\app\VC\Auxiliary\Build\vcvars64.bat"" && ""D:\SoftWare\Visual Studio\app\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"" -S . -B build\stage-driver-gate-clean -G Ninja -DCMAKE_MAKE_PROGRAM=""D:\SoftWare\Visual Studio\app\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"" && ""D:\SoftWare\Visual Studio\app\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"" --build build\stage-driver-gate-clean"
```

Result:

```text
exit_code=0
```

## Output

```text
D:\Documents\fif-Screen\build\stage-driver-gate-clean\windows-driver\FifIddDeviceLauncher\fif-idd-device-launcher.exe
size=824832 bytes
SHA-256=89D93836A6A2580FF0021968BDD76F17E772C26398903B6ED105699BA5A57A79
```

## Read-Only Status Check

Command:

```powershell
build\stage-driver-gate-clean\windows-driver\FifIddDeviceLauncher\fif-idd-device-launcher.exe status
```

Output:

```text
fifscreen_software_device_present=false
exit=0
```

Not executed:

```text
launcher create: NO
launcher remove: NO
driver install: NO
software device created: NO
virtual monitor verified: NO
```

