# FindFFmpeg.cmake - Find FFmpeg components (system packages / vcpkg / Homebrew)
#
# Creates imported target: ffmpeg::ffmpeg
# Sets: FFmpeg_FOUND

# Find header files
find_path(FFMPEG_INCLUDE_DIR libavformat/avformat.h)

# Find component libraries
find_library(AVFORMAT_LIBRARY NAMES avformat)
find_library(AVCODEC_LIBRARY   NAMES avcodec)
find_library(AVUTIL_LIBRARY    NAMES avutil)
find_library(SWSCALE_LIBRARY   NAMES swscale)
find_library(SWRESAMPLE_LIBRARY NAMES swresample)

# Collect all libraries
set(FFMPEG_LIBRARIES
    ${AVFORMAT_LIBRARY}
    ${AVCODEC_LIBRARY}
    ${AVUTIL_LIBRARY}
    ${SWSCALE_LIBRARY}
    ${SWRESAMPLE_LIBRARY}
)

# Windows static linking requires extra system libs and third-party dependencies
if(WIN32)
    set(_ffmpeg_extra_libs "")
    foreach(_lib bcrypt secur32 ws2_32 iconv m vfw32 user32 gdi32
                 ole32 oleaut32 strmiids uuid winmm shlwapi
                 zlib z bz2 bzip2 lzma mfx openh264
                 vorbis vorbisenc vorbisfile ogg opus
                 mp3lame x264 x265 vpx vpxenc vpxdec
                 webp webpdecoder sharpyuv
                 aom dav1d svtav1enc svtav1dec
                 ssl crypto)
        find_library(_found_${_lib} NAMES ${_lib})
        if(_found_${_lib})
            list(APPEND _ffmpeg_extra_libs ${_found_${_lib}})
        endif()
    endforeach()
    list(APPEND FFMPEG_LIBRARIES ${_ffmpeg_extra_libs})
endif()

# Create imported target
if(FFMPEG_INCLUDE_DIR AND FFMPEG_LIBRARIES)
    if(NOT TARGET ffmpeg::ffmpeg)
        add_library(ffmpeg::ffmpeg INTERFACE IMPORTED)
        set_target_properties(ffmpeg::ffmpeg PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_INCLUDE_DIR}"
            INTERFACE_LINK_LIBRARIES "${FFMPEG_LIBRARIES}"
        )
    endif()
    set(FFmpeg_FOUND TRUE)
else()
    set(FFmpeg_FOUND FALSE)
endif()

mark_as_advanced(
    FFMPEG_INCLUDE_DIR
    AVFORMAT_LIBRARY AVCODEC_LIBRARY AVUTIL_LIBRARY
    SWSCALE_LIBRARY SWRESAMPLE_LIBRARY
)
