cmake_minimum_required(VERSION 2.8)
project(voglperf)

include("${CMAKE_SOURCE_DIR}/build_options.cmake")

message("")
message("cmake options:")
message("  -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}: Build debug or release. (debug|release)")
message("  -DCMAKE_VERBOSE=${CMAKE_VERBOSE}: Spew cmake project options. (On|Off)")
message("  -DBUILD_X64=${BUILD_X64}: Build 32 or 64-bit. (On|Off)")
message("")

set(SRC_LIST voglperf.c)

add_compiler_flag("-fno-exceptions")

add_shared_linker_flag("-Wl,--version-script=${PROJECT_SOURCE_DIR}/libvoglperf_linker_script.txt")

add_library(${PROJECT_NAME} SHARED ${SRC_LIST})

target_link_libraries(${PROJECT_NAME}
    dl
    pthread
    rt
    m
    )

project(voglperfrun)

set(SRC_LIST voglperfrun.cpp)

add_executable(${PROJECT_NAME} ${SRC_LIST})

target_link_libraries(${PROJECT_NAME}
    rt
    )

build_options_finalize()

