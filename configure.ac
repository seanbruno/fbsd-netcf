AC_INIT(netcf, 0.1.0)
AC_CONFIG_SRCDIR([src/netcf.c])
AC_CONFIG_AUX_DIR([build/aux])
AM_CONFIG_HEADER([config.h])
AM_INIT_AUTOMAKE([-Wno-portability 1.11 color-tests parallel-tests])
AM_SILENT_RULES([yes]) # make --enable-silent-rules the default.

AC_SUBST([LIBNETCF_VERSION_INFO], [1:0:0])

AC_GNU_SOURCE

AC_PROG_CC
gl_EARLY

AC_PROG_LIBTOOL

dnl The backend driver. Right now hardcoded to initscripts, but
dnl eventually needs to be configurable at buildtime
AM_CONDITIONAL([NETCF_DRIVER_INITSCRIPTS], [true])

dnl Compiler flags to be used everywhere
AC_SUBST([NETCF_CFLAGS], ['--std=gnu99 -Wp,-D_FORTIFY_SOURCE=2 -fexceptions -fasynchronous-unwind-tables'])

gl_INIT

NETCF_COMPILE_WARNINGS

dnl Need to test if pkg-config exists
PKG_PROG_PKG_CONFIG
PKG_CHECK_MODULES([LIBAUGEAS], [augeas >= 0.5.0])
PKG_CHECK_MODULES([LIBXML], [libxml-2.0])
PKG_CHECK_MODULES([LIBXSLT], [libxslt])

NETCF_CHECK_READLINE

NETCF_LIBDEPS=$(echo $LIBAUGEAS_LIBS $LIBXSLT_LIBS $LIBXML_LIBS)
AC_SUBST([NETCF_LIBDEPS])

AC_OUTPUT(Makefile                                          \
          gnulib/lib/Makefile                               \
          gnulib/tests/Makefile                             \
          src/Makefile                                      \
          tests/Makefile                                    \
          doc/Makefile                                      \
          netcf.pc netcf.spec)
