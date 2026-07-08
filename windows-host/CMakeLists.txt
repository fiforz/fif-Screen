add_executable(fif-host
  src/adb.cpp
  src/host_server.cpp
  src/main.cpp
  src/screen_capture.cpp
  src/win32_socket.cpp
)

target_include_directories(fif-host PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(fif-host PRIVATE fif_protocol)

if(WIN32)
  target_link_libraries(fif-host PRIVATE Ws2_32 User32 Gdi32 Gdiplus)
else()
  message(FATAL_ERROR "windows-host currently targets Windows only")
endif()
