AC_PREREQ([2.68])
AC_INIT([lbzip2], [1.0], [zurgunt@gmail.com])

AC_CONFIG_SRCDIR(src/main.c)
AC_CONFIG_AUX_DIR(build-aux)
AC_CONFIG_HEADERS([lib/config.h])

AM_INIT_AUTOMAKE([1.11])
AM_SILENT_RULES([yes])

AC_PROG_RANLIB
AC_PROG_CC_C89
gl_EARLY

AC_HEADER_STDC
AC_C_CONST
AC_C_VOLATILE
AC_TYPE_SIZE_T
AC_TYPE_OFF_T
AC_SYS_LARGEFILE

gl_INIT

AC_CONFIG_FILES([Makefile src/Makefile yambi/Makefile lib/Makefile
    doc/Makefile])
AC_OUTPUT
