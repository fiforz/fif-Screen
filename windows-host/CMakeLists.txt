add_executable(fif-host
  src/adb.cpp
  src/host_server.cpp
  src/main.cpp
  src/mf_h264_encoder.cpp
  src/screen_capture.cpp
  src/win32_socket.cpp
)

if(WIN32)
  set(FIFSCREEN_FILE_DESCRIPTION "FifScreen Windows Host")
  set(FIFSCREEN_INTERNAL_NAME "fif-host")
  set(FIFSCREEN_ORIGINAL_FILENAME "fif-host.exe")
  configure_file(
    ${CMAKE_SOURCE_DIR}/packaging/windows/version.rc.in
    ${CMAKE_CURRENT_BINARY_DIR}/fif-host-version.rc
    @ONLY
  )
  target_sources(fif-host PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/fif-host-version.rc)
endif()

target_include_directories(fif-host PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(fif-host PRIVATE fif_protocol)

if(WIN32)
  target_link_libraries(fif-host PRIVATE
    Ws2_32 User32 Gdi32 Gdiplus Winmm
    Mf Mfplat Mfuuid Wmcodecdspuuid Ole32
  )
else()
  message(FATAL_ERROR "windows-host currently targets Windows only")
endif()
