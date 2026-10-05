cmake_minimum_required(VERSION 3.16)
project(opennow_native LANGUAGES C CXX)

# ------------------------------------------------------------------ options --
# ONOW_PLATFORM picks exactly one platform backend: each of
# src/platform/Platform*.cpp defines create_platform(), so compiling two of
# them into one binary is a link error.
set(ONOW_PLATFORM "" CACHE STRING "Platform backend: win32, glfw, or headless")
set(ONOW_WITH_TESTS OFF CACHE BOOL "Build the headless self-test")

if(NOT ONOW_PLATFORM)
  if(WIN32)
    set(ONOW_PLATFORM "win32")
  else()
    set(ONOW_PLATFORM "glfw")
  endif()
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra -Wno-unused-parameter)

# ------------------------------------------------------------------ sources --
file(GLOB_RECURSE ONOW_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

set(PLATFORM_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/PlatformWin32.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/PlatformGlfw.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/PlatformHeadless.cpp)

if(ONOW_PLATFORM STREQUAL "win32")
  set(ONOW_PLATFORM_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/PlatformWin32.cpp)
elseif(ONOW_PLATFORM STREQUAL "glfw")
  set(ONOW_PLATFORM_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/PlatformGlfw.cpp)
else()
  set(ONOW_PLATFORM "headless")
  set(ONOW_PLATFORM_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/PlatformHeadless.cpp)
endif()

foreach(extra ${PLATFORM_SOURCES})
  if(NOT extra STREQUAL ONOW_PLATFORM_SOURCE)
    list(REMOVE_ITEM ONOW_SOURCES ${extra})
  endif()
endforeach()
list(REMOVE_ITEM ONOW_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/imgui_demo.cpp)

set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp)

# The OpenGL 3 backend is compiled with a custom loader (GLLoader.h) so no
# GLEW/GLAD package is required.
set(IMGUI_BACKEND_SOURCES ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp)

add_executable(opennow ${ONOW_SOURCES} ${IMGUI_SOURCES} ${IMGUI_BACKEND_SOURCES})
target_include_directories(opennow PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends)

target_compile_definitions(opennow PRIVATE
    IMGUI_IMPL_OPENGL_LOADER_CUSTOM
    ONOW_PLATFORM_${ONOW_PLATFORM})

if(ONOW_PLATFORM STREQUAL "win32")
  target_compile_definitions(opennow PRIVATE
      WIN32_LEAN_AND_MEAN
      ONOW_TLS_SCHANNEL
      ONOW_AUDIO_WASAPI)
  target_sources(opennow PRIVATE ${IMGUI_DIR}/backends/imgui_impl_win32.cpp)
  target_link_libraries(opennow PRIVATE opengl32 ws2_32 secur32 crypt32 winmm gdi32 user32 shell32 advapi32 imm32)
  set_target_properties(opennow PROPERTIES WIN32_EXECUTABLE ON)
elseif(ONOW_PLATFORM STREQUAL "glfw")
  find_package(OpenGL REQUIRED)
  find_package(glfw3 QUIET)
  if(NOT glfw3_FOUND)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
      pkg_check_modules(GLFW3 REQUIRED glfw3)
    else()
      message(FATAL_ERROR "GLFW not found. Install glfw3 or use -DONOW_PLATFORM=headless.")
    endif()
  endif()
  target_compile_definitions(opennow PRIVATE ONOW_TLS_OPENSSL ONOW_AUDIO_NONE)
  target_sources(opennow PRIVATE ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp)
  target_link_libraries(opennow PRIVATE OpenGL::GL ${GLFW3_LIBRARIES})
  target_include_directories(opennow PRIVATE ${GLFW3_INCLUDE_DIRS})
else()
  target_compile_definitions(opennow PRIVATE ONOW_TLS_OPENSSL ONOW_AUDIO_NONE)
endif()

target_link_libraries(opennow PRIVATE pthread dl)

# ------------------------------------------------------------------- tests ----
if(ONOW_WITH_TESTS)
  enable_testing()
  # The self-test needs no window and no backend: pure logic checks.
  add_test(NAME self_test COMMAND opennow --self-test)
endif()
