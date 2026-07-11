cmake_minimum_required(VERSION 3.21)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" FIFSCREEN_VERSION LIMIT_COUNT 1)
string(STRIP "${FIFSCREEN_VERSION}" FIFSCREEN_VERSION)
project(FifScreen VERSION "${FIFSCREEN_VERSION}" LANGUAGES CXX)

enable_testing()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_subdirectory(protocol)
add_subdirectory(windows-host)
add_subdirectory(windows-driver/FifIddDeviceLauncher)
add_subdirectory(tests)
