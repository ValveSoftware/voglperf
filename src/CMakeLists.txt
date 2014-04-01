cmake_minimum_required(VERSION 2.8)

message("")
message("cmake options:")
message("  -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}: Build debug or release. (debug|release)")
message("  -DCMAKE_VERBOSE=${CMAKE_VERBOSE}: Spew cmake project options. (On|Off)")
message("  -DBUILD_X64=${BUILD_X64}: Build 32 or 64-bit. (On|Off)")
message("  -DCLANG_ANALYZE=[On|Off]: Do clang analyze build (will not link).")
message("  -DCLANG_EVERYTHING=[On|Off]: Do clang build with -Weverything.")
message("")

#
# voglperf
#
project(voglperf)
find_package(Threads REQUIRED)

include("${CMAKE_SOURCE_DIR}/build_options.cmake")

set(SRC_LIST voglperf.c)

add_compiler_flag("-fno-exceptions")

add_shared_linker_flag("-Wl,--version-script=${PROJECT_SOURCE_DIR}/libvoglperf_linker_script.txt")

add_library(${PROJECT_NAME} SHARED ${SRC_LIST})

target_link_libraries(${PROJECT_NAME}
    dl
    rt
    m
    ${CMAKE_THREAD_LIBS_INIT}
    )

#
# voglperfrun
#
project(voglperfrun)

message("MAKE_LIBRARY_ARCHITECTURE is ${CMAKE_LIBRARY_ARCHITECTURE}")

# add libedit lib
include_directories(${CMAKE_SOURCE_DIR}/libedit-3.1/include)
set(LIBEDIT_LIBS
    "${CMAKE_SOURCE_DIR}/libedit-3.1/lib/${LIBDIR}/libedit.a"
    tinfo
    )

set(SRC_LIST
    voglperfrun.cpp
    voglutils.cpp
    webby/webby.c)

add_executable(${PROJECT_NAME} ${SRC_LIST})

target_link_libraries(${PROJECT_NAME}
    rt
    ${CMAKE_THREAD_LIBS_INIT}
    ${LIBEDIT_LIBS}
    )

build_options_finalize()

