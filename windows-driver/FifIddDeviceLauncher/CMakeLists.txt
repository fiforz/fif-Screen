add_executable(fif-idd-device-launcher
  src/main.cpp
)

if(WIN32)
  set(FIFSCREEN_FILE_DESCRIPTION "FifScreen Display Device Launcher")
  set(FIFSCREEN_INTERNAL_NAME "fif-idd-device-launcher")
  set(FIFSCREEN_ORIGINAL_FILENAME "fif-idd-device-launcher.exe")
  configure_file(
    ${CMAKE_SOURCE_DIR}/packaging/windows/version.rc.in
    ${CMAKE_CURRENT_BINARY_DIR}/fif-idd-device-launcher-version.rc
    @ONLY
  )
  target_sources(fif-idd-device-launcher PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/fif-idd-device-launcher-version.rc
  )
endif()

target_compile_features(fif-idd-device-launcher PRIVATE cxx_std_20)

if(WIN32)
  target_link_libraries(fif-idd-device-launcher PRIVATE Swdevice Setupapi Cfgmgr32)
else()
  message(FATAL_ERROR "fif-idd-device-launcher targets Windows only")
endif()
