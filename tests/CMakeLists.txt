add_executable(fif-protocol-test protocol_frame_test.cpp)
target_link_libraries(fif-protocol-test PRIVATE fif_protocol)

if(WIN32)
  add_executable(fif-touch-injector-test
    touch_injector_test.cpp
    ${CMAKE_SOURCE_DIR}/windows-host/src/touch_injector.cpp
  )
  target_include_directories(fif-touch-injector-test PRIVATE
    ${CMAKE_SOURCE_DIR}/windows-host/src
  )
  target_compile_definitions(fif-touch-injector-test PRIVATE
    _WIN32_WINNT=0x0A00
    WINVER=0x0A00
  )
  target_link_libraries(fif-touch-injector-test PRIVATE fif_protocol User32)
endif()
