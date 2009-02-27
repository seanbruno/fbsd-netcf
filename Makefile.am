SUBDIRS=gnulib/lib src gnulib/tests tests

ACLOCAL_AMFLAGS = -I gnulib/m4

EXTRA_DIST=netcf.spec build/aux/move-if-change

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = netcf.pc

xmldir=$(datadir)/netcf/xml

dist_xml_DATA=xml/augeas.rng xml/interface.rng xml/initscripts-get.xsl \
  xml/initscripts-put.xsl

# This requires that trang is installed, but we don't want to require
# that, even for building, since the .rnc files are only a convenience
# when using Emacs
rnc: xml/augeas.rnc xml/interface.rng

%.rnc: %.rng
	trang $< $@
