cmake_minimum_required(VERSION 3.20)
project(Kohane C)

set(CMAKE_C_STANDARD 11)

add_executable(Kohane main.c)

# Conan dependencies (via CMakeDeps generator)
find_package(ffmpeg REQUIRED)
find_package(PNG REQUIRED)

target_link_libraries(Kohane PRIVATE
    ffmpeg::ffmpeg
    PNG::PNG
)
