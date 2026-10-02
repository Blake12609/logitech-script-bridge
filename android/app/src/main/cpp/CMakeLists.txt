# The same engine as the Windows app (../../../../../src), plus the JNI glue.
cmake_minimum_required(VERSION 3.18)
project(scriptbridge LANGUAGES C CXX)

set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../../..)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Lua 5.4, compiled as C++ so script errors unwind with exceptions.
file(GLOB LUA_SOURCES ${ROOT}/third_party/lua/*.c)
set_source_files_properties(${LUA_SOURCES} PROPERTIES LANGUAGE CXX)

add_library(scriptbridge SHARED
    jni_bridge.cpp
    ${ROOT}/src/engine.cpp
    ${ROOT}/src/keys.cpp
    ${ROOT}/src/backend.cpp
    ${ROOT}/src/serial.cpp
    ${ROOT}/src/mobile_session.cpp
    ${ROOT}/src/platform_posix.cpp
    ${LUA_SOURCES})
target_include_directories(scriptbridge PRIVATE ${ROOT}/src ${ROOT}/third_party/lua)
target_compile_definitions(scriptbridge PRIVATE LUA_COMPAT_5_3)
target_compile_options(scriptbridge PRIVATE -fexceptions -frtti -O2)
set_source_files_properties(${LUA_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")
target_link_libraries(scriptbridge PRIVATE log)
target_link_options(scriptbridge PRIVATE -Wl,--gc-sections)
