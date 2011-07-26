AC_INIT(netcf, 0.1.9)
AC_CONFIG_SRCDIR([src/netcf.c])
AC_CONFIG_AUX_DIR([build/aux])
AC_CONFIG_MACRO_DIR([gnulib/m4])
AM_CONFIG_HEADER([config.h])
AM_INIT_AUTOMAKE([-Wno-portability 1.11 color-tests parallel-tests])
AM_SILENT_RULES([yes]) # make --enable-silent-rules the default.

AC_SUBST([LIBNETCF_VERSION_INFO], [5:0:4])

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
PKG_CHECK_MODULES([LIBEXSLT], [libexslt])
PKG_CHECK_MODULES([LIBNL], [libnl-1])

NETCF_CHECK_READLINE

NETCF_LIBDEPS=$(echo $LIBAUGEAS_LIBS $LIBEXSLT_LIBS $LIBXSLT_LIBS $LIBXML_LIBS $LIBNL_LIBS)
AC_SUBST([NETCF_LIBDEPS])

AC_CHECK_HEADER([pthread.h],
	[AC_CHECK_LIB([pthread],[pthread_join],[
		AC_DEFINE([HAVE_LIBPTHREAD],[],[Define if pthread (-lpthread)])
		AC_DEFINE([HAVE_PTHREAD_H],[],[Define if <pthread.h>])
		LIBS="-lpthread $LIBS"
	])])

dnl if --prefix is /usr, don't use /usr/var for localstatedir
dnl or /usr/etc for sysconfdir
dnl as this makes a lot of things break in testing situations

if test "$prefix" = "/usr" && test "$localstatedir" = '${prefix}/var' ; then
    localstatedir='/var'
fi
if test "$prefix" = "/usr" && test "$sysconfdir" = '${prefix}/etc' ; then
    sysconfdir='/etc'
fi

dnl
dnl init script flavor
dnl
AC_MSG_CHECKING([for init script flavor])
AC_ARG_WITH([init-script],
            [AC_HELP_STRING([--with-init-script=@<:@redhat|auto|none@:>@],
		     [Style of init script to install @<:@default=auto@:>@])])
if test "x$with_init_script" = "x" || test "x$with_init_script" = "xauto"; then
    if test "$prefix" = "$HOME" || test "$cross_compiling" = yes || test ! -f /etc/redhat-release; then
        with_init_script=none
    else
        with_init_script=redhat
    fi
fi
if test "x$with_init_script" = "xredhat"; then
    AC_DEFINE_UNQUOTED([NETCF_TRANSACTION],
                       ["$sysconfdir/rc.d/init.d/netcf-transaction"],
                       [Location of the netcf-transaction shell script])
fi
AM_CONDITIONAL([NETCF_INIT_SCRIPT_RED_HAT],
               [test x$with_init_script = xredhat])
AC_MSG_RESULT([$with_init_script])

AC_OUTPUT(Makefile                                          \
          gnulib/lib/Makefile                               \
          gnulib/tests/Makefile                             \
          src/Makefile                                      \
          tests/Makefile                                    \
          doc/Makefile                                      \
          netcf.pc netcf.spec)
