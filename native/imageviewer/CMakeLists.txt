cmake_minimum_required(VERSION 3.10)
project(imageviewer C CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 复用 bilinet 里 vendored 的 iot-miniapp-sdk (同一份, 不重复提交)
set(IOT_UI_SDK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../bilinet/third_party/iot-miniapp-sdk")
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

add_definitions(-D__DEBUG -DHAASUI_LOG_TO_SYSLOG -D_GNU_SOURCE)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -include pthread.h")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -include pthread.h")

file(GLOB_RECURSE SDK_SOURCES "${IOT_UI_SDK_PATH}/src/*.cpp")
file(GLOB_RECURSE MODULE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

# 图像解码不链接 turbojpeg: 交叉编译环境没有 aarch64 的 libturbojpeg-dev,
# 改为运行时 dlopen("libturbojpeg.so.0") (设备自带), 与 bilinet 用 dlopen 拿 sqlite 同一套路.
add_library(jsapi_imageviewer SHARED ${SDK_SOURCES} ${MODULE_SOURCES})
target_include_directories(jsapi_imageviewer PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(jsapi_imageviewer dl pthread)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    target_link_libraries(jsapi_imageviewer dl pthread -Wl,-undefined,dynamic_lookup)
else()
    target_link_libraries(jsapi_imageviewer dl pthread)
endif()
