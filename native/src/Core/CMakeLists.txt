# OpenNOW Core — portable C++20 library.
#
# This is the only part of the native client that is not Windows specific, so it
# is also the only part that can be unit tested on a Linux CI box. The WinUI app
# consumes the same sources through OpenNOW.Core.vcxproj.

cmake_minimum_required(VERSION 3.20)
project(OpenNOWCore LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(OPENNOW_CORE_SOURCES
    src/Json.cpp
    src/Hash.cpp
    src/Http.cpp
    src/Logging.cpp
    src/Paths.cpp
    src/Settings.cpp
    src/Types.cpp
    src/gfn/Auth.cpp
    src/gfn/Catalog.cpp
    src/gfn/CloudMatch.cpp
    src/gfn/Constants.cpp
    src/gfn/DeviceId.cpp
    src/gfn/Signaling.cpp
    src/gfn/SignalingWinrt.cpp
    src/gfn/Subscription.cpp
)

add_library(opennow_core STATIC ${OPENNOW_CORE_SOURCES})
target_include_directories(opennow_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(MSVC)
    target_compile_options(opennow_core PRIVATE /W4 /permissive- /utf-8)
    target_compile_definitions(opennow_core PRIVATE _UNICODE UNICODE)
else()
    target_compile_options(opennow_core PRIVATE -Wall -Wextra)
endif()

option(OPENNOW_BUILD_TESTS "Build the Core unit tests" ON)
if(OPENNOW_BUILD_TESTS)
    enable_testing()
    add_executable(opennow_core_tests tests/test_core.cpp)
    target_link_libraries(opennow_core_tests PRIVATE opennow_core)
    add_test(NAME core COMMAND opennow_core_tests)
endif()
