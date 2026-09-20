# Linux preview for the monster eyes renderer.
#
# Builds the library sources from ../lib unmodified against the Arduino shim in
# shim/, with -DARDUINO_ARCH_ESP32 so that Eyes_Platform.h and Eyes_Assets.cpp
# take the same preprocessor branches they take on the board. Nothing in
# ../lib or ../src is touched, and the PlatformIO firmware build is unaffected.

cmake_minimum_required(VERSION 3.16)
project(monster_eyes_sim CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(EYES_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../lib/Adafruit_Monster_Eyes/src)

# ArduinoJson is header-only and builds natively, so the simulator parses
# config.eye with the same parser the firmware does rather than a lookalike.
# PlatformIO has usually already fetched it; ARDUINOJSON_DIR overrides.
if(NOT ARDUINOJSON_DIR)
    file(GLOB _arduinojson_candidates
         ${CMAKE_CURRENT_SOURCE_DIR}/../.pio/libdeps/*/ArduinoJson/src)
    if(_arduinojson_candidates)
        list(GET _arduinojson_candidates 0 ARDUINOJSON_DIR)
    endif()
endif()
if(NOT ARDUINOJSON_DIR OR NOT EXISTS ${ARDUINOJSON_DIR}/ArduinoJson.h)
    message(FATAL_ERROR
        "ArduinoJson not found. Run 'pio pkg install' in the parent directory, "
        "or pass -DARDUINOJSON_DIR=/path/to/ArduinoJson/src")
endif()

find_package(ZLIB REQUIRED)

# SDL3 is optional: without it the simulator still builds and still captures
# frames headless, which is the half an agent needs.
find_package(SDL3 QUIET)
if(NOT SDL3_FOUND)
    message(WARNING
        "SDL3 not found -- building headless only. Install libsdl3-dev for the "
        "preview window.")
endif()

add_executable(eye-sim
    src/main.cpp
    src/capture.cpp
    shim/Arduino.cpp
    shim/FFat.cpp
    ${EYES_LIB_DIR}/Adafruit_Monster_Eyes.cpp
    ${EYES_LIB_DIR}/Eyes_Assets.cpp
    ${EYES_LIB_DIR}/Eyes_Display.cpp
)

target_include_directories(eye-sim PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/shim
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${EYES_LIB_DIR}
    ${ARDUINOJSON_DIR}
)

target_compile_definitions(eye-sim PRIVATE
    ARDUINO_ARCH_ESP32=1
    # Only the stream reader is wanted from ArduinoJson's Arduino integration;
    # String, Print and PROGMEM would each pull in more core than the shim has
    # any reason to carry.
    ARDUINOJSON_ENABLE_ARDUINO_STREAM=1
    ARDUINOJSON_ENABLE_ARDUINO_STRING=0
    ARDUINOJSON_ENABLE_ARDUINO_PRINT=0
    ARDUINOJSON_ENABLE_PROGMEM=0
    ARDUINOJSON_ENABLE_COMMENTS=1
)

target_link_libraries(eye-sim PRIVATE ZLIB::ZLIB)

if(SDL3_FOUND)
    target_link_libraries(eye-sim PRIVATE SDL3::SDL3)
else()
    target_compile_definitions(eye-sim PRIVATE SIM_HEADLESS_ONLY=1)
endif()

# The library is third-party and warns on its own terms; the simulator's own
# code should build clean.
set_source_files_properties(
    ${EYES_LIB_DIR}/Adafruit_Monster_Eyes.cpp
    ${EYES_LIB_DIR}/Eyes_Assets.cpp
    ${EYES_LIB_DIR}/Eyes_Display.cpp
    PROPERTIES COMPILE_OPTIONS "-w")

target_compile_options(eye-sim PRIVATE -Wall -Wextra)
