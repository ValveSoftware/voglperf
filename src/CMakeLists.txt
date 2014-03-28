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

find_package(PkgConfig)
include(FindPackageHandleStandardArgs)

# Track down libedit...
pkg_check_modules(PKG_LIBEDIT REQUIRED libedit)
set(LIBEDIT_DEFS ${PKG_LIBEDIT_CFLAGS_OTHER})
find_path(LIBEDIT_INCLUDE_DIR histedit.h
          HINTS ${PKG_LIBEDIT_INCLUDEDIR} ${PKG_LIBEDIT_INCLUDE_DIRS} )
find_library(LIBEDIT_LIBRARY NAMES edit 
             HINTS ${PKG_LIBEDIT_LIBDIR} ${PKG_LIBEDIT_LIBRARY_DIRS} )
# Set LIBEDIT_FOUND and bail if libedit not found.
find_package_handle_standard_args(LibEdit DEFAULT_MSG
                                  LIBEDIT_LIBRARY LIBEDIT_INCLUDE_DIR)

set(SRC_LIST
    voglperfrun.cpp
    voglutils.cpp
    webby/webby.c)

add_executable(${PROJECT_NAME} ${SRC_LIST})

target_link_libraries(${PROJECT_NAME}
    rt
    ${LIBEDIT_LIBRARY}
    ${CMAKE_THREAD_LIBS_INIT}
    )

build_options_finalize()

