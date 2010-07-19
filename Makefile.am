SUBDIRS=gnulib/lib src gnulib/tests tests doc

ACLOCAL_AMFLAGS = -I gnulib/m4

EXTRA_DIST=netcf.spec build/aux/move-if-change

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = netcf.pc

netcfdir=$(datadir)/netcf
xmldir=$(netcfdir)/xml
lensdir=$(netcfdir)/lenses

dist_xml_DATA=data/xml/augeas.rng data/xml/interface.rng \
	data/xml/util-get.xsl data/xml/util-put.xsl \
	data/xml/initscripts-get.xsl data/xml/initscripts-put.xsl
dist_netcf_DATA=data/iptables-forward-bridged
dist_lens_DATA=data/lenses/netcf.aug data/lenses/sysconfig.aug

# This requires that trang is installed, but we don't want to require
# that, even for building, since the .rnc files are only a convenience
# when using Emacs
rnc: data/xml/augeas.rnc data/xml/interface.rnc

%.rnc: %.rng
	trang $< $@
