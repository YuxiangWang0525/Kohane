AC_PREREQ([2.69])
AC_INIT([kohane], [1.0.0], [https://github.com/YuxiangWang0525/Kohane/issues])
AC_CONFIG_SRCDIR([main.c])
AM_INIT_AUTOMAKE([foreign -Wall -Werror])

# Checks for programs
AC_PROG_CC
AC_PROG_INSTALL

# Checks for header files
AC_CHECK_HEADERS([sys/stat.h sys/types.h errno.h])

# Checks for FFmpeg libraries via pkg-config
PKG_CHECK_MODULES([AVFORMAT],  [libavformat],  [], [AC_MSG_ERROR([libavformat not found])])
PKG_CHECK_MODULES([AVCODEC],   [libavcodec],   [], [AC_MSG_ERROR([libavcodec not found])])
PKG_CHECK_MODULES([AVUTIL],    [libavutil],    [], [AC_MSG_ERROR([libavutil not found])])
PKG_CHECK_MODULES([SWSCALE],   [libswscale],   [], [AC_MSG_ERROR([libswscale not found])])
PKG_CHECK_MODULES([LIBPNG],    [libpng],       [], [AC_MSG_ERROR([libpng not found])])

# Merge all FFmpeg flags
FFMPEG_CFLAGS="$AVFORMAT_CFLAGS $AVCODEC_CFLAGS $AVUTIL_CFLAGS $SWSCALE_CFLAGS"
FFMPEG_LIBS="$AVFORMAT_LIBS $AVCODEC_LIBS $AVUTIL_LIBS $SWSCALE_LIBS"
AC_SUBST([FFMPEG_CFLAGS])
AC_SUBST([FFMPEG_LIBS])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
