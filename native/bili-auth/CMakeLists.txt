cmake_minimum_required(VERSION 3.10)
project(bili-auth C CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(IOT_UI_SDK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../gstplayer/third_party/iot-miniapp-sdk")
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

# SQLite：登录 Cookie 与设置本地持久化（/userdisk/xiro/bili/app.db）
find_package(PkgConfig REQUIRED)
pkg_check_modules(SQLITE3 REQUIRED sqlite3)
include_directories(${SQLITE3_INCLUDE_DIRS})

file(GLOB_RECURSE SDK_SOURCES "${IOT_UI_SDK_PATH}/src/*.cpp")
file(GLOB_RECURSE AUTH_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_library(jsapi_biliauth SHARED ${SDK_SOURCES} ${AUTH_SOURCES})
target_include_directories(jsapi_biliauth PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(jsapi_biliauth dl pthread sqlite3 -Wl,-unresolved-symbols=ignore-all)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    target_link_libraries(jsapi_biliauth dl pthread sqlite3 -Wl,-undefined,dynamic_lookup)
else()
    target_link_libraries(jsapi_biliauth dl pthread sqlite3)
endif()

set(CMAKE_VERBOSE_MAKEFILE ON)