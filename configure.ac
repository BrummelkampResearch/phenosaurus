AC_PREREQ([2.69])

m4_define([screen_analyzer_version_major],[2])
m4_define([screen_analyzer_version_minor],[0])
m4_define([screen_analyzer_version_micro],[1])
m4_define([screen_analyzer_version_extra],[])
m4_define([screen_analyzer_version],[screen_analyzer_version_major().screen_analyzer_version_minor().screen_analyzer_version_micro()screen_analyzer_version_extra])

AC_INIT([screen-analyzer], [screen_analyzer_version], [m.hekkelman@nki.nl])

dnl Switch to a C++ compiler, and check if it works.
AC_LANG(C++)
AX_CXX_COMPILE_STDCXX_17([noext])

AC_CONFIG_SRCDIR([src/screen-analyzer.cpp])
AC_CONFIG_HEADERS([src/config.hpp])
AC_CONFIG_AUX_DIR(config)
AC_CONFIG_MACRO_DIR([config/m4])

AC_PREFIX_DEFAULT(/usr/local)

dnl AC_DEFUN([read_test], [AC_LANG_SOURCE(
dnl	esyscmd(tools/m4esc.sh config-tests/$1))])

AC_PROG_INSTALL

AC_ARG_VAR([DEBUG], [Build a debug version of the application])
AC_ARG_VAR([MRC], [Specify a location for the mrc executable])

dnl We really want to use mrc
if test "x$MRC" = "x"; then
	AC_PATH_PROG([MRC], [mrc])
fi

if test "x$MRC" = "x"; then
	AC_MSG_ERROR([mrc not found, the application can not be built])
fi

AC_CHECK_PROG([HAVE_YARN], [yarn], [yes],
	[AC_MSG_ERROR([yarn was not found, it is required to package the javascripts.])])

AC_CHECK_PROG([HAVE_GOOGLE_FONT_DOWNLOADER], [google-font-downloader], [yes],
	[AC_MSG_ERROR([google-font-downloader was not found.])])

PKG_PROG_PKG_CONFIG

AX_PKG_CHECK_MODULES([PQXX], [libpqxx >= 7.1], [], [], [AC_MSG_ERROR([the required package libpqxx-dev is not installed])])
AX_PKG_CHECK_MODULES([PQ], [libpq], [], [], [AC_MSG_ERROR([the required package libpq-dev is not installed])])
AX_PKG_CHECK_MODULES([SSL], [libssl openssl], [], [], [AC_MSG_ERROR([the required package libssl-dev is not installed])])
AX_PKG_CHECK_MODULES([MAILIO], [mailio], [], [], [AC_MSG_ERROR([the required package mailio-dev is not installed])])
AX_PKG_CHECK_MODULES([ZLIB], [zlib], [], [], [AC_MSG_ERROR([the required package zlib-dev is not installed])])

AX_PTHREAD

AX_BOOST_BASE([1.65.1], [], [AC_MSG_ERROR([Could not find a recent version of boost])])
AX_BOOST_ASIO
AX_BOOST_IOSTREAMS
AX_BOOST_PROGRAM_OPTIONS
AX_BOOST_DATE_TIME

AX_LIBZEEP
AX_FILESYSTEM

dnl check whether to build xattr support

dnl Verify if configure has been invoked with option --enable-xattr or
dnl --disable-xattr, and set shell variable want_xattr as appropriate.
AC_MSG_CHECKING([whether to enable xattr support])
AC_ARG_ENABLE([xattr],
  AS_HELP_STRING([--disable-xattr], [Disable xattr support (default: auto)]),
	[want_xattr=$enableval],
	[want_xattr=auto])
AC_MSG_RESULT([$want_xattr])

dnl now check the presence of <sys/xattr.h> and the function setxattr()
ibs_xattr_msg="disabled"
if test "xno" != "x$want_xattr"; then
  AC_CHECK_HEADERS([sys/xattr.h],
	[ AC_CHECK_FUNC([setxattr], [ibs_xattr_msg="enabled"]) ] )
  if test "xyes" = "x$want_xattr"; then
    AC_MSG_WARN([xattr support will not be built])
  fi
fi
if test "enabled" = "$ibs_xattr_msg"; then
  AC_DEFINE(USE_XATTR, 1, [if you want to build xattr support])
fi

# AC_ARG_ENABLE(
# 	debug,
# 	[AS_HELP_STRING([--enable-debug], [Generate a debug version of the library])],
# 	[],
# 	[enable_debug=auto])

dnl Process Makefile.in to create Makefile
AC_OUTPUT([GNUmakefile])
