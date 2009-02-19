
GNULIB= ../gnulib/lib/libgnu.la
GNULIB_CFLAGS= -I $(top_srcdir)/gnulib/lib

AM_CFLAGS = $(NETCF_CFLAGS) $(GNULIB_CFLAGS) $(WARN_CFLAGS) \
  $(LIBXML_CFLAGS) $(LIBXSLT_CFLAGS)

include_HEADERS = netcf.h

lib_LTLIBRARIES = libnetcf.la

bin_PROGRAMS = ncftool

if NETCF_DRIVER_INITSCRIPTS
DRIVER_SOURCES = drv_initscripts.c
endif

BUILT_SOURCES = datadir.h

DISTCLEANFILES = datadir.h

libnetcf_la_SOURCES = netcf.h netcf.c internal.h \
     $(DRIVER_SOURCES)
libnetcf_la_LIBADD = $(LIBAUGEAS_LIBS) $(LIBXML_LIBS) $(LIBXSLT_LIBS) $(GNULIB)

ncftool_SOURCES = ncftool.c
ncftool_LDADD = libnetcf.la $(READLINE_LIBS) $(GNULIB)

# Generate datadir.h. That's where we look for stylesheets
internal.h: datadir.h

FORCE-datadir.h: Makefile
	echo '#define DATADIR "$(datadir)"' > datadir.h1
	$(top_srcdir)/build/aux/move-if-change datadir.h1 datadir.h

datadir.h: FORCE-datadir.h
