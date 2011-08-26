dnl TODO: check for GCC builtins: expect, prefetch, ctz, ctzl, ctzll

AC_PREREQ([2.68])
AC_INIT([bzimp2], [current])

AC_CONFIG_SRCDIR([src/main.c])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_HEADERS([lib/config.h])

AM_INIT_AUTOMAKE([1.11])
AM_SILENT_RULES([yes])

AC_PROG_RANLIB
AC_PROG_CC_C89
AC_USE_SYSTEM_EXTENSIONS
gl_EARLY


AC_ARG_ENABLE([warnings],
  [AS_HELP_STRING([--enable-warnings],
    [try to enable many C compiler warnings])],
  [
if test "x$enableval" = xyes
then
  for kjn_warn in \
    '' \
    all \
    extra \
    format-y2k \
    format-nonliteral \
    format-security \
    init-self \
    missing-include-dirs \
    switch-default \
    switch-enum \
    unused \
    unknown-pragmas \
    strict-aliasing \
    float-equal \
    declaration-after-statement \
    shadow \
    pointer-arith \
    bad-function-cast \
    cast-qual \
    cast-align \
    write-strings \
    logical-op \
    aggregate-return \
    strict-prototypes \
    old-style-definition \
    missing-prototypes \
    missing-declarations \
    missing-noreturn \
    missing-format-attribute \
    packed \
    redundant-decls \
    nested-externs \
    unreachable-code \
    inline \
    invalid-pch \
    long-long \
    vla \
    volatile-register-var \
    disabled-optimization \
    stack-protector \
    overlength-strings \
    builtin-macro-redefined \
    mudflap \
    packed-bitfield-compat \
    sync-nand \
    attributes \
    coverage-mismatch \
    multichar \
    unused-macros \
    extra-tokens
  do
    gl_WARN_ADD([-W$kjn_warn])
  done
  CPPFLAGS="$kjn_save_flags"

  AC_SUBST([WARN_CFLAGS])
fi])

AC_C_CONST
AC_C_VOLATILE
AC_C_RESTRICT
AC_C_INLINE
AC_SYS_LARGEFILE

dnl Headers
AC_HEADER_STDC
AC_CHECK_HEADERS([arpa/inet.h])
AC_CHECK_HEADERS([fcntl.h])
AC_CHECK_HEADERS([limits.h])
AC_CHECK_HEADERS([stddef.h])
AC_CHECK_HEADERS([sys/time.h])

dnl Types
AC_TYPE_UINT8_T
AC_TYPE_UINT16_T
AC_TYPE_INT32_T
AC_TYPE_UINT32_T
AC_TYPE_INT64_T
AC_TYPE_UINT64_T
AC_TYPE_PID_T
AC_TYPE_OFF_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T

dnl Functions
AC_FUNC_MALLOC
AC_CHECK_FUNCS([bzero])
AC_CHECK_FUNCS([gettimeofday])
AC_CHECK_FUNCS([strrchr])
AC_CHECK_FUNCS([strtol])
AC_FUNC_LSTAT_FOLLOWS_SLASHED_SYMLINK

gl_INIT

AC_CONFIG_FILES([Makefile src/Makefile yambi/Makefile lib/Makefile])
AC_OUTPUT
