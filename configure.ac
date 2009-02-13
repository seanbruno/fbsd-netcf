AC_INIT(netcf, 0.0.1)
AC_CONFIG_SRCDIR([src/netcf.c])
AC_CONFIG_AUX_DIR([build/aux])
AM_CONFIG_HEADER([config.h])
AM_INIT_AUTOMAKE([-Wno-portability])


AC_SUBST([LIBNETCF_VERSION_INFO], [0:0:1])

AC_GNU_SOURCE

AC_PROG_CC
gl_EARLY

AC_PROG_LIBTOOL

## Compiler flags to be used everywhere
NETCF_CFLAGS=-std=gnu99
AC_SUBST(NETCF_CFLAGS)

gl_INIT

AC_OUTPUT(Makefile \
          src/Makefile)
