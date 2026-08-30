cmake_minimum_required(VERSION 3.10)
project(videoprobe C CXX)

set(CMAKE_CXX_STANDARD 17)

# 交叉编译器说明: 同 CI 父工作流, aarch64-linux-gnu-gcc
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(BUILD_FLAGS "-Wall -g -O0")
else()
  set(BUILD_FLAGS "-Wall -Os -g")
endif()
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${BUILD_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${BUILD_FLAGS}")
set(CMAKE_SKIP_BUILD_RPATH true)
add_definitions(-D_GNU_SOURCE)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -include pthread.h")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -include pthread.h")

# falcon sdk 头文件 (复用 gstplayer 的 iot-miniapp-sdk sdk 树)
set(IOT_UI_SDK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../native/gstplayer/third_party/iot-miniapp-sdk")
include_directories("${IOT_UI_SDK_PATH}/include")

# gstreamer 头文件 (Ubuntu jammy aarch64 开发包; 运行时链接设备 /usr/lib 同名 soname)
find_package(PkgConfig)
if(PKG_CONFIG_FOUND)
  pkg_check_modules(GST gstreamer-1.0)
endif()
if(NOT GST_FOUND)
  message(WARNING "no pkg-config gstreamer-1.0")
endif()

file(GLOB_RECURSE SDK_SOURCES "${IOT_UI_SDK_PATH}/src/*.cpp")

add_library(jsapi_videoprobe SHARED
  ${SDK_SOURCES}
  ${CMAKE_CURRENT_SOURCE_DIR}/native/videoprobe/src/VideoProbeModule.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/native/videoprobe/src/JsVideoprobeModule.cpp)
target_include_directories(jsapi_videoprobe PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/native/videoprobe/src")
target_link_libraries(jsapi_videoprobe dl pthread)
set_target_properties(jsapi_videoprobe PROPERTIES PREFIX "lib")

add_executable(videoprobed
  ${CMAKE_CURRENT_SOURCE_DIR}/native/videoprobe/src/VProbeCore.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/native/videoprobe/src/videoprobed_main.cpp)
target_include_directories(videoprobed PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/native/videoprobe/src")
target_include_directories(videoprobed PRIVATE ${GST_INCLUDE_DIRS})
target_link_directories(videoprobed PRIVATE ${GST_LIBRARY_DIRS})
target_link_libraries(videoprobed ${GST_LIBRARIES} gstreamer-1.0 gobject-2.0 glib-2.0 pthread)
