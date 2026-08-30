cmake_minimum_required(VERSION 3.10)

# 交叉编译器优先读环境变量；缺失时回退到 CROSS_TOOLCHAIN_PREFIX。
# 本设备 AArch64 + glibc，工具链 aarch64-none-linux-gnu-（gcc 10.3 ARM A-profile）。
if(DEFINED ENV{CROSS_C_COMPILER} AND DEFINED ENV{CROSS_CXX_COMPILER})
  set(CMAKE_C_COMPILER "$ENV{CROSS_C_COMPILER}")
  set(CMAKE_CXX_COMPILER "$ENV{CROSS_CXX_COMPILER}")
elseif(DEFINED ENV{CROSS_TOOLCHAIN_PREFIX})
  set(CMAKE_C_COMPILER "$ENV{CROSS_TOOLCHAIN_PREFIX}gcc")
  set(CMAKE_CXX_COMPILER "$ENV{CROSS_TOOLCHAIN_PREFIX}g++")
else()
  message(FATAL_ERROR "Set compilers from the target Youdao device profile (aarch64-none-linux-gnu-)")
endif()

project(jsapi_pvplayer C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(SDK "${CMAKE_SOURCE_DIR}/iot-miniapp-sdk")

file(GLOB_RECURSE SDK_SOURCES "${SDK}/src/*.cpp")
add_library(iot_sdk STATIC ${SDK_SOURCES})
target_compile_options(iot_sdk PRIVATE -w)
target_include_directories(iot_sdk PUBLIC "${SDK}/include")
set_target_properties(iot_sdk PROPERTIES POSITION_INDEPENDENT_CODE ON)

file(GLOB_RECURSE PLUGIN_SOURCES "${CMAKE_SOURCE_DIR}/src/*.cpp")
add_library(jsapi_pvplayer SHARED ${PLUGIN_SOURCES})
target_include_directories(jsapi_pvplayer PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_compile_options(jsapi_pvplayer PRIVATE
  -Wall -Wextra -Werror=return-type -Wno-psabi -fvisibility=hidden)
# 只链接 SDK 与 pthread：播放走 gst-launch 子进程，不引入 GStreamer/curl/sqlite。
target_link_libraries(jsapi_pvplayer PRIVATE
  iot_sdk pthread -Wl,-unresolved-symbols=ignore-all)
set_target_properties(jsapi_pvplayer PROPERTIES
  OUTPUT_NAME "jsapi_pvplayer"
  PREFIX "lib")