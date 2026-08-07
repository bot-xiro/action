cmake_minimum_required(VERSION 3.10)
project(gstplayer C CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(IOT_UI_SDK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/third_party/iot-miniapp-sdk")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/src")
include_directories("${IOT_UI_SDK_PATH}/include")

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

# GStreamer 开发头文件（宿主架构）仅用于编译期；
# 运行时符号从设备 GStreamer 1.22 动态库解析（-Wl,-unresolved-symbols=ignore-all）
find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED gstreamer-1.0 gstreamer-base-1.0 gstreamer-plugins-base-1.0)
# 交叉编译时剔除宿主 libc 头目录（/usr/include/x86_64-linux-gnu），
# 防止与 aarch64 工具链 glibc 头混用导致 gnu/stubs-32.h 缺失
if(CMAKE_CROSSCOMPILING)
    list(FILTER GST_INCLUDE_DIRS EXCLUDE REGEX "^/usr/include/x86_64-linux-gnu$")
endif()
include_directories(${GST_INCLUDE_DIRS})

file(GLOB_RECURSE SDK_SOURCES "${IOT_UI_SDK_PATH}/src/*.cpp")
file(GLOB_RECURSE PLAYER_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_library(jsapi_gstplayer SHARED ${SDK_SOURCES} ${PLAYER_SOURCES})
target_include_directories(jsapi_gstplayer PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(jsapi_gstplayer dl pthread -Wl,-unresolved-symbols=ignore-all)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    target_link_libraries(jsapi_gstplayer dl pthread -Wl,-undefined,dynamic_lookup)
else()
    target_link_libraries(jsapi_gstplayer dl pthread)
endif()

set(CMAKE_VERBOSE_MAKEFILE ON)
