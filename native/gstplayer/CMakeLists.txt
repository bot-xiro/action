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

# _GNU_SOURCE: SDK 使用 pthread_setcancelstate 等，需 glibc GNU feature macro（Android bionic 默认提供）
# -include pthread.h: SDK 头（looper/Thread.h）未显式 include pthread.h，glibc 下依赖 include 顺序而失败
add_definitions(-D__DEBUG -DHAASUI_LOG_TO_SYSLOG -D_GNU_SOURCE)
# 实验开关：KMSSINK_TEST=ON 时视频 sink 用 kmssink（KMS overlay 双平面直出，Lilo 方案）
option(KMSSINK_TEST "Use kmssink overlay-plane video output (experimental)" OFF)
if(KMSSINK_TEST)
    add_definitions(-DKMSSINK_TEST)
endif()
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -include pthread.h")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -include pthread.h")

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

# libdrm（仅头文件）：KMSSINK 分支用 drmModeObjectSetProperty 直接控制视频
# plane 的 zpos，绕开 kmssink 缺失的 zpos 属性（运行时符号从设备解析）
if(KMSSINK_TEST)
    pkg_check_modules(DRM REQUIRED libdrm)
    include_directories(${DRM_INCLUDE_DIRS})
endif()

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
