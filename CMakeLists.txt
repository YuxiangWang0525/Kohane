cmake_minimum_required(VERSION 3.20)
project(Kohane C)

set(CMAKE_C_STANDARD 11)

add_executable(Kohane main.c)

# MSVC: treat source and string literals as UTF-8
if(MSVC)
    target_compile_options(Kohane PRIVATE /utf-8)
endif()

# ── 依赖查找: 优先 Conan, 回退到 pkg-config (系统包/vcpkg) ──
find_package(ffmpeg QUIET)
find_package(PNG QUIET)

if(ffmpeg_FOUND AND PNG_FOUND)
    # Conan 模式
    set(FFMPEG_LIBS ffmpeg::ffmpeg)
    set(PNG_LIBS PNG::PNG)
else()
    # 系统包 / vcpkg 模式 (pkg-config)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
        libavformat libavcodec libavutil libswscale)
    pkg_check_modules(PNG REQUIRED IMPORTED_TARGET libpng)
    set(FFMPEG_LIBS PkgConfig::FFMPEG)
    set(PNG_LIBS PkgConfig::PNG)
endif()

target_link_libraries(Kohane PRIVATE
    ${FFMPEG_LIBS}
    ${PNG_LIBS}
)
