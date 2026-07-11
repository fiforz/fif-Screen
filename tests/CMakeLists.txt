add_executable(fif-protocol-test protocol_frame_test.cpp)
target_link_libraries(fif-protocol-test PRIVATE fif_protocol)
add_test(NAME fif-protocol-test COMMAND fif-protocol-test)

function(fif_enable_test_assertions target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /UNDEBUG)
  else()
    target_compile_options(${target} PRIVATE -UNDEBUG)
  endif()
endfunction()

fif_enable_test_assertions(fif-protocol-test)

if(WIN32)
  add_executable(fif-pairing-crypto-test
    pairing_crypto_test.cpp
    ${CMAKE_SOURCE_DIR}/windows-host/src/pairing_crypto.cpp
  )
  target_include_directories(fif-pairing-crypto-test PRIVATE
    ${CMAKE_SOURCE_DIR}/windows-host/src
  )
  target_compile_definitions(fif-pairing-crypto-test PRIVATE
    _WIN32_WINNT=0x0A00
    WINVER=0x0A00
  )
  target_link_libraries(fif-pairing-crypto-test PRIVATE fif_protocol Bcrypt)
  fif_enable_test_assertions(fif-pairing-crypto-test)
  add_test(NAME fif-pairing-crypto-test COMMAND fif-pairing-crypto-test)

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
  fif_enable_test_assertions(fif-touch-injector-test)
  add_test(NAME fif-touch-injector-test COMMAND fif-touch-injector-test)
endif()
