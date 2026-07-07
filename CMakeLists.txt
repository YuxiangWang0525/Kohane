cmake_minimum_required(VERSION 3.20)
project(Kohane C)

set(CMAKE_C_STANDARD 11)

add_executable(Kohane main.c)

# MSVC: treat source and string literals as UTF-8
if(MSVC)
    target_compile_options(Kohane PRIVATE /utf-8)
endif()

# 自定义 FindModule 路径
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# ── 依赖查找: 优先 Conan, 回退到 FindModule / find_library ──
find_package(ffmpeg QUIET)   # Conan
find_package(PNG QUIET)      # Conan / 系统

if(NOT ffmpeg_FOUND OR NOT PNG_FOUND)
    # FFmpeg: 使用自定义 FindFFmpeg (find_library, 无需 pkg-config)
    if(NOT TARGET ffmpeg::ffmpeg)
        find_package(FFmpeg REQUIRED)
    endif()

    # PNG: find_library 回退 (vcpkg / 系统包)
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
