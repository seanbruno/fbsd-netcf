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

dnl The backend driver. Right now hardcoded to initscripts, but
dnl eventually needs to be configurable at buildtime
AM_CONDITIONAL([NETCF_DRIVER_INITSCRIPTS], [true])

dnl Compiler flags to be used everywhere
AC_SUBST([NETCF_CFLAGS], ['--std=gnu99 -Wp,-D_FORTIFY_SOURCE=2 -fexceptions -fasynchronous-unwind-tables'])

gl_INIT

dnl FIXME: Make the set of flags more configurable
dnl compile_warnings == no
dnl compile_warnings == minimum
gl_WARN_ADD([-Wall])
gl_WARN_ADD([-Wformat])
gl_WARN_ADD([-Wformat-security])
dnl compile_warnings == yes
gl_WARN_ADD([-Wmissing-prototypes])
dnl compile_warnings == maximum
gl_WARN_ADD([-Wnested-externs])
gl_WARN_ADD([-Wpointer-arith])
gl_WARN_ADD([-Wextra])
gl_WARN_ADD([-Wshadow])
gl_WARN_ADD([-Wcast-align])
gl_WARN_ADD([-Wwrite-strings])
gl_WARN_ADD([-Waggregate-return])
gl_WARN_ADD([-Wstrict-prototypes])
gl_WARN_ADD([-Winline])
gl_WARN_ADD([-Wredundant-decls])
gl_WARN_ADD([-Wno-sign-compare])
dnl FIXME: we might need to take this out to make
dnl builds work on other platforms (Solaris)
gl_WARN_ADD([-Werror])

dnl Need to test if pkg-config exists
PKG_PROG_PKG_CONFIG
PKG_CHECK_MODULES([LIBAUGEAS], [augeas])

NETCF_CHECK_READLINE

AC_OUTPUT(Makefile     \
          gnulib/lib/Makefile \
          gnulib/tests/Makefile \
          src/Makefile \
          tests/Makefile)
