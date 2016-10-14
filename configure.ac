dnl                                               -*- Autoconf -*-
dnl Process this file with autoconf to produce a configure script.

AC_INIT([ODR-PadEnc], [2.0.1], [http://opendigitalradio.org/])
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

AX_CXX_COMPILE_STDCXX_11(noext,mandatory)

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
