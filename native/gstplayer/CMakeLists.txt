cmake_minimum_required(VERSION 3.10)
project(gstplayer C CXX)

set(CMAKE_CXX_STANDARD 17)
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

# _GNU_SOURCE: SDK 使用 pthread_setcancelstate 等，需 glibc GNU feature macro
# -include pthread.h: SDK 头（looper/Thread.h）未显式 include pthread.h
add_definitions(-D__DEBUG -DHAASUI_LOG_TO_SYSLOG -D_GNU_SOURCE)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -include pthread.h")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -include pthread.h")

# GStreamer (目标设备 gstreamer-1.0, CI 用 aarch64 multiarch 开发包编译,
# 运行时链接设备 /usr/lib 下同名 soname 库: libgstreamer-1.0.so.0 等)
# 头文件/链接搜索路径由 pkg-config (GST_CFLAGS/GST_LIBS) 或环境覆盖
find_package(PkgConfig)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(GST gstreamer-1.0)
endif()
if(NOT GST_FOUND)
    message(WARNING "pkg-config gstreamer-1.0 未找到, 依赖采集 GST_ROOT/include+lib")
endif()

file(GLOB_RECURSE SDK_SOURCES "${IOT_UI_SDK_PATH}/src/*.cpp")

# JSAPI 模块: 不依赖 gstreamer 本体; 只拉起/守护 gstplayerd 子进程
add_library(jsapi_gstplayer SHARED
    ${SDK_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/GstPlayer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/JSGstPlayerModule.cpp)
target_include_directories(jsapi_gstplayer PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(jsapi_gstplayer dl pthread)
set_target_properties(jsapi_gstplayer PROPERTIES PREFIX "lib")

# 独立守护进程: gstreamer 全在其中, 崩溃不影响 miniapp 宿主
add_executable(gstplayerd
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PlayCore.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/daemon.cpp)
target_include_directories(gstplayerd PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_include_directories(gstplayerd PRIVATE ${GST_INCLUDE_DIRS})
target_link_directories(gstplayerd PRIVATE ${GST_LIBRARY_DIRS})
target_link_libraries(gstplayerd ${GST_LIBRARIES} gstreamer-1.0 gobject-2.0 glib-2.0 pthread)

set(CMAKE_VERBOSE_MAKEFILE ON)
