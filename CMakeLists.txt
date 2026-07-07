cmake_minimum_required(VERSION 3.20)
project(Kohane C)

set(CMAKE_C_STANDARD 11)

add_executable(Kohane main.c)

# MSVC: treat source and string literals as UTF-8
if(MSVC)
    target_compile_options(Kohane PRIVATE /utf-8)
endif()

# Conan dependencies (via CMakeDeps generator)
find_package(ffmpeg REQUIRED)
find_package(PNG REQUIRED)

target_link_libraries(Kohane PRIVATE
    ffmpeg::ffmpeg
    PNG::PNG
)
