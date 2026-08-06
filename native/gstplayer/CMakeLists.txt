cmake_minimum_required(VERSION 3.10)
project(jsapi_gstplayer C CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# iot-miniapp-sdk path
set(IOT_UI_SDK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../iot-miniapp-sdk")
if(NOT EXISTS "${IOT_UI_SDK_PATH}")
    set(IOT_UI_SDK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../WPE4YDPv2/jsapi/iot-miniapp-sdk")
endif()

include_directories("${CMAKE_CURRENT_SOURCE_DIR}/src")
include_directories("${IOT_UI_SDK_PATH}/include")

# GStreamer dev headers (host arch, Ubuntu 22.04 amd64 default paths).
# Used for COMPILE only; actual symbols resolve at runtime on device.
include_directories(
    /usr/include/gstreamer-1.0
    /usr/lib/x86_64-linux-gnu/gstreamer-1.0/include
    /usr/include/glib-2.0
    /usr/lib/x86_64-linux-gnu/glib-2.0/include
)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release")
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(BUILD_FLAGS "-Wall -Werror=return-type -g -O0")
else()
    set(BUILD_FLAGS "-Wall -Werror=return-type -Os -g")
endif()

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${BUILD_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${BUILD_FLAGS}")
set(CMAKE_SKIP_BUILD_RPATH true)

add_definitions(-D__DEBUG -DHAASUI_LOG_TO_SYSLOG)

file(GLOB_RECURSE SDK_SOURCES "${IOT_UI_SDK_PATH}/src/*.cpp")
file(GLOB_RECURSE GSTPLAYER_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_library(jsapi_gstplayer SHARED ${SDK_SOURCES} ${GSTPLAYER_SOURCES})

# No external dependencies for now (stub module)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(jsapi_gstplayer dl pthread -Wl,-unresolved-symbols=ignore-all)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    target_link_libraries(jsapi_gstplayer dl pthread -Wl,-undefined,dynamic_lookup)
else()
    target_link_libraries(jsapi_gstplayer dl pthread)
endif()

set(CMAKE_VERBOSE_MAKEFILE ON)
