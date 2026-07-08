add_executable(fif-idd-device-launcher
  src/main.cpp
)

target_compile_features(fif-idd-device-launcher PRIVATE cxx_std_20)

if(WIN32)
  target_link_libraries(fif-idd-device-launcher PRIVATE Swdevice Setupapi Cfgmgr32)
else()
  message(FATAL_ERROR "fif-idd-device-launcher targets Windows only")
endif()

