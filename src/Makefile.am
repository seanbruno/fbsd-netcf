
GNULIB= ../gnulib/lib/libgnu.la
GNULIB_CFLAGS= -I $(top_srcdir)/gnulib/lib

AM_CFLAGS = $(NETCF_CFLAGS) $(GNULIB_CFLAGS) $(WARN_CFLAGS) \
  $(LIBXML_CFLAGS) $(LIBXSLT_CFLAGS) $(LIBAUGEAS_CFLAGS)

include_HEADERS = netcf.h

lib_LTLIBRARIES = libnetcf.la

bin_PROGRAMS = ncftool

noinst_PROGRAMS = ncftransform

EXTRA_DIST = netcf_public.syms \
	netcf_private.syms \
	netcf-transaction.init.sh

if NETCF_DRIVER_INITSCRIPTS
DRIVER_SOURCES = drv_initscripts.c dutil.h dutil.c \
     dutil_linux.h dutil_linux.c
endif

BUILT_SOURCES = datadir.h netcf.syms

DISTCLEANFILES = datadir.h

libnetcf_la_SOURCES = netcf.h netcf.c internal.h \
     ref.h list.h \
     xslt_ext.c $(DRIVER_SOURCES)
libnetcf_la_LDFLAGS = -Wl,--version-script=netcf.syms \
     -version-info $(LIBNETCF_VERSION_INFO)
libnetcf_la_LIBADD = $(NETCF_LIBDEPS) $(GNULIB)
libnetcf_la_DEPENDENCIES = $(libnetcf_la_LIBADD) netcf.syms

ncftool_SOURCES = ncftool.c
ncftool_LDADD = libnetcf.la $(READLINE_LIBS) $(GNULIB)

ncftransform_SOURCES = ncftransform.c
ncftransform_LDADD = libnetcf.la $(GNULIB)

netcf.syms: netcf_public.syms netcf_private.syms
	rm -f $@-tmp $@
	printf '# WARNING: generated from the following files:\n# $^\n\n' >$@-tmp
	cat $(srcdir)/netcf_public.syms >>$@-tmp
	printf '\n\n# Private symbols\n\n' >>$@-tmp
	printf 'NETCF_PRIVATE_$(VERSION) {\n\n'  >>$@-tmp
	printf 'global:\n\n' >>$@-tmp
	cat $(srcdir)/netcf_private.syms >>$@-tmp
	printf '\n\nlocal:\n*;\n\n};' >>$@-tmp
	chmod a-w $@-tmp
	mv $@-tmp $@

# Generate datadir.h. That's where we look for stylesheets
internal.h: datadir.h

datadir.h: $(top_builddir)/config.status
	echo '#define DATADIR "$(datadir)"' > datadir.h

install-data-local: install-init

uninstall-local: uninstall-init

if NETCF_INIT_SCRIPT_RED_HAT
# This is for the initscript that handles network config change
# transactions.
install-init: netcf-transaction.init
	mkdir -p $(DESTDIR)$(sysconfdir)/rc.d/init.d
	$(INSTALL_SCRIPT) netcf-transaction.init \
	  $(DESTDIR)$(sysconfdir)/rc.d/init.d/netcf-transaction

uninstall-init:
	rm -f $(DESTDIR)$(sysconfdir)/rc.d/init.d/netcf-transaction \
	  $(DESTDIR)$(sysconfdir)/sysconfig/netcf-transaction

BUILT_SOURCES += netcf-transaction.init

netcf-transaction.init: netcf-transaction.init.sh $(top_builddir)/config.status
	$(AM_V_GEN)sed					\
	    -e 's!\@localstatedir\@!$(localstatedir)!g'	\
	    -e 's!\@sysconfdir\@!$(sysconfdir)!g'	\
	    < $< > $@-t &&				\
	    chmod a+x $@-t &&				\
	    mv $@-t $@
else
install-init:
uninstall-init:
netcf-transaction.init:
endif # NETCF_INIT_SCRIPT_RED_HAT

DISTCLEANFILES += $(BUILT_SOURCES)
