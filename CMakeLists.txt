cmake_minimum_required(VERSION 3.16)
project(LogitechScriptBridge LANGUAGES C CXX)
if(WIN32)
  enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Portable single exe: link the C/C++ runtime statically.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
if(MSVC)
  add_compile_options(/utf-8)  # sources are UTF-8
endif()
if(MINGW)
  add_link_options(-static -static-libgcc -static-libstdc++)
endif()

# Lua 5.4, compiled as C++ so script errors unwind with exceptions.
file(GLOB LUA_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lua/*.c)
set_source_files_properties(${LUA_SOURCES} PROPERTIES LANGUAGE CXX)
add_library(lua STATIC ${LUA_SOURCES})
target_include_directories(lua PUBLIC third_party/lua)
target_compile_definitions(lua PUBLIC LUA_COMPAT_5_3)
if(MSVC)
  target_compile_options(lua PRIVATE /w)
else()
  target_compile_options(lua PRIVATE -w)
endif()

add_library(bridge_core STATIC src/engine.cpp src/keys.cpp src/backend.cpp src/serial.cpp src/config.cpp)
target_include_directories(bridge_core PUBLIC src)
target_link_libraries(bridge_core PUBLIC lua)
find_package(Threads REQUIRED)
target_link_libraries(bridge_core PUBLIC Threads::Threads)

if(WIN32)
  target_sources(bridge_core PRIVATE src/platform_win.cpp)
  add_executable(LogitechScriptBridge WIN32 src/gui_win.cpp src/app.rc)
  target_link_libraries(LogitechScriptBridge PRIVATE bridge_core comctl32 comdlg32 shell32 winmm gdiplus dwmapi uxtheme)
  target_compile_definitions(LogitechScriptBridge PRIVATE UNICODE _UNICODE)
  if(MINGW)
    target_link_options(LogitechScriptBridge PRIVATE -municode -s)
  elseif(MSVC)
    target_link_options(LogitechScriptBridge PRIVATE /MANIFEST:NO)  # app.rc embeds our manifest
  endif()
else()
  target_sources(bridge_core PRIVATE src/platform_posix.cpp)
endif()

enable_testing()
add_library(test_main STATIC tests/test_main.cpp)
target_include_directories(test_main PUBLIC tests)

add_executable(bridge_tests tests/engine_tests.cpp tests/config_tests.cpp tests/serial_tests.cpp)
target_link_libraries(bridge_tests PRIVATE bridge_core test_main)
add_test(NAME bridge_tests COMMAND bridge_tests)

# The firmware sketches, compiled against a mock Arduino core and driven by the real engine.
foreach(fw esp32 arduino)
  add_executable(${fw}_fw_tests tests/firmware/${fw}_fw_tests.cpp)
  target_include_directories(${fw}_fw_tests PRIVATE tests/firmware/mock tests/firmware)
  target_link_libraries(${fw}_fw_tests PRIVATE bridge_core test_main)
  add_test(NAME ${fw}_fw_tests COMMAND ${fw}_fw_tests)
endforeach()
