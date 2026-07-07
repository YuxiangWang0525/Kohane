cmake_minimum_required(VERSION 3.20)
project(Kohane C)

set(CMAKE_C_STANDARD 11)

add_executable(Kohane main.c)

# MSVC: treat source and string literals as UTF-8
if(MSVC)
    target_compile_options(Kohane PRIVATE /utf-8)
endif()

# Custom FindModule path
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# ── Dependency resolution: Conan first, fallback to FindModule / find_library ──
find_package(ffmpeg QUIET)   # Conan
find_package(PNG QUIET)      # Conan / system

if(NOT ffmpeg_FOUND OR NOT PNG_FOUND)
    # FFmpeg: custom FindFFmpeg (find_library, no pkg-config needed)
    if(NOT TARGET ffmpeg::ffmpeg)
        find_package(FFmpeg REQUIRED)
    endif()

    # PNG: find_library fallback (vcpkg / system packages)
    if(NOT TARGET PNG::PNG)
        find_library(PNG_LIBRARY NAMES png png16 libpng16)
        find_path(PNG_INCLUDE_DIR NAMES png.h PATH_SUFFIXES libpng16)
        if(PNG_LIBRARY AND PNG_INCLUDE_DIR)
            add_library(PNG::PNG UNKNOWN IMPORTED)
            set_target_properties(PNG::PNG PROPERTIES
                IMPORTED_LOCATION "${PNG_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${PNG_INCLUDE_DIR}"
            )
        else()
            message(FATAL_ERROR "Could not find PNG library")
        endif()
    endif()
endif()

target_link_libraries(Kohane PRIVATE
    ffmpeg::ffmpeg
    PNG::PNG
)

# Windows system libs required by FFmpeg (networking, TLS, crypto)
if(WIN32)
    target_link_libraries(Kohane PRIVATE ws2_32 secur32 crypt32 ncrypt)
endif()
