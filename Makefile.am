SUBDIRS=gnulib/lib src gnulib/tests tests doc

ACLOCAL_AMFLAGS = -I gnulib/m4

EXTRA_DIST=netcf.spec autogen.sh bootstrap bootstrap.conf

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = netcf.pc

netcfdir=$(datadir)/netcf
xmldir=$(netcfdir)/xml
lensdir=$(netcfdir)/lenses

dist_xml_DATA=data/xml/augeas.rng data/xml/interface.rng \
	data/xml/util-get.xsl data/xml/util-put.xsl \
	data/xml/debian-get.xsl data/xml/debian-put.xsl \
	data/xml/redhat-get.xsl data/xml/redhat-put.xsl \
	data/xml/suse-get.xsl data/xml/suse-put.xsl
dist_netcf_DATA=data/iptables-forward-bridged
dist_lens_DATA= \
	data/lenses/netcf.aug \
	data/lenses/sysconfig.aug \
	data/lenses/persist_net_rules.aug \
	data/lenses/routes.aug


# This requires that trang is installed, but we don't want to require
# that, even for building, since the .rnc files are only a convenience
# when using Emacs
rnc: data/xml/augeas.rnc data/xml/interface.rnc

%.rnc: %.rng
	trang $< $@
