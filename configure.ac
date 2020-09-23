dnl                                               -*- Autoconf -*-
dnl Process this file with autoconf to produce a configure script.

AC_INIT([ODR-PadEnc], [3.0.0], [http://opendigitalradio.org/])
AC_CONFIG_AUX_DIR(.)
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([tar-ustar foreign])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])
AC_CONFIG_HEADER([config.h])

dnl Checks for programs.
AC_PROG_CC
AC_PROG_CXX
AC_PROG_LIBTOOL
LT_INIT

AX_CXX_COMPILE_STDCXX(11,noext,mandatory)


AC_LANG_PUSH([C++])
AX_CHECK_COMPILE_FLAG([-Wduplicated-cond], [CXXFLAGS="$CXXFLAGS -Wduplicated-cond"], [], ["-Werror"])
AX_CHECK_COMPILE_FLAG([-Wduplicated-branches], [CXXFLAGS="$CXXFLAGS -Wduplicated-branches"], [], ["-Werror"])
AX_CHECK_COMPILE_FLAG([-Wlogical-op], [CXXFLAGS="$CXXFLAGS -Wlogical-op"], [], ["-Werror"])
AX_CHECK_COMPILE_FLAG([-Wrestrict], [CXXFLAGS="$CXXFLAGS -Wrestrict"], [], ["-Werror"])
AX_CHECK_COMPILE_FLAG([-pedantic], [CXXFLAG_PEDANTIC="-pedantic"], [], ["-Werror"])
AX_CHECK_COMPILE_FLAG([-Wpedantic], [CXXFLAG_PEDANTIC="-Wpedantic"], [], ["-Werror"])
CXXFLAGS="$CXXFLAGS $CXXFLAG_PEDANTIC"
AX_CHECK_COMPILE_FLAG(["-Wformat=2"], [CXXFLAGS="$CXXFLAGS -Wformat=2"], [], ["-Werror"])
AC_LANG_POP([C++])

AC_CHECK_LIB([m], [sin])

if pkg-config MagickWand; then
    MAGICKWAND_CFLAGS=`pkg-config MagickWand --cflags`
    MAGICKWAND_LDADD=`pkg-config MagickWand --libs`
    AC_SUBST(MAGICKWAND_CFLAGS)
    AC_SUBST(MAGICKWAND_LDADD)
else
    AC_MSG_WARN(MagickWand not found)
fi

AS_IF([ pkg-config MagickWand ],
       AC_DEFINE(HAVE_MAGICKWAND, [1], [Define if MagickWand is available]))
AS_IF([ pkg-config "MagickWand < 7" ],
       AC_DEFINE(HAVE_MAGICKWAND_LEGACY, [1], [Define if a legacy (prior to version 7) MagickWand is available]))


AM_CONDITIONAL([IS_GIT_REPO], [test -d '.git'])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT

enabled=""
disabled=""

AS_IF([ pkg-config MagickWand ],
      [enabled="$enabled magickwand"],
      [disabled="$disabled magickwand"])

echo
echo "***********************************************"
echo "Features enabled:"
echo "  Enabled: $enabled"
echo "  Disabled: $disabled"
echo
echo "ODR-PadEnc configured, please run make"
echo
