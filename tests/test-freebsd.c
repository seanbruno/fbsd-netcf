/*
 * test-freebsd.c:
 *
 * Copyright (C) 2009 Red Hat Inc.
 * Copyright (C) 2012 ADARA Networks
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA
 */

#include <config.h>
#include "netcf.h"
#include "internal.h"
#include "cutest.h"
#include "safe-alloc.h"
#include "read-file.h"

#include "tutil.h"

#include <stdio.h>

#include <libxml/tree.h>

extern const char *abs_top_srcdir;
extern const char *abs_top_builddir;
extern char *driver_name;
extern char *root, *src_root;
extern struct netcf *ncf;

static void testListInterfaces(CuTest *tc) {
    int nint;
    char **names;
    static const char *const exp_names[] =
        { "em0", "em1", "lo0", "lagg0", "bridge0" };
    static const int exp_nint = ARRAY_CARDINALITY(exp_names);

    nint = ncf_num_of_interfaces(ncf, NETCF_IFACE_ACTIVE|NETCF_IFACE_INACTIVE);
    CuAssertIntEquals(tc, exp_nint, nint);
    if (ALLOC_N(names, nint) < 0)
        die("allocation failed");
    nint = ncf_list_interfaces(ncf, nint, names, NETCF_IFACE_ACTIVE|NETCF_IFACE_INACTIVE);
    CuAssertIntEquals(tc, exp_nint, nint);
    for (int i=0; i < exp_nint; i++) {
        int found = 0;
        for (int j=0; j < nint; j++) {
            if (STREQ(names[j], exp_names[i]))
                found = 1;
        }
        CuAssert(tc, "Unknown interface name", found);
    }
}

static void testLookupByName(CuTest *tc) {
    struct netcf_if *nif;

    /* Check that a bogus interface is not found. */
    nif = ncf_lookup_by_name(ncf, "fake0");
    CuAssertPtrEquals(tc, NULL, nif);
    /* Check that a valid interface is found and is as expected. */
    nif = ncf_lookup_by_name(ncf, "bridge0");
    CuAssertPtrNotNull(tc, nif);
    CuAssertStrEquals(tc, "bridge0", nif->name);
    ncf_if_free(nif);
    CuAssertIntEquals(tc, 1, ncf->ref);
}

/* Check that we get the right ifcfg- file when we have to
 * look it up indirectly by HWADDR
 */
static void testLookupByNameDecoy(CuTest *tc) {
    struct netcf_if *nif;
    char *xml, *loc;

    nif = ncf_lookup_by_name(ncf, "em0");
    CuAssertPtrNotNull(tc, nif);
    CuAssertStrEquals(tc, "em0", nif->name);

    xml = ncf_if_xml_desc(nif);
    CuAssertPtrNotNull(tc, xml);
    loc = strstr(xml, "<mac address=\"00:00:00:00:00:01\"/>");
    CuAssertPtrNotNull(tc, loc);

    free(xml);
    ncf_if_free(nif);
    CuAssertIntEquals(tc, 1, ncf->ref);
}

static void testLookupByMAC(CuTest *tc) {
    static const char *const good_mac = "aa:bb:cc:dd:ee:ff";
    static const char *const good_mac_caps = "AA:bb:cc:DD:Ee:ff";
    struct netcf_if *nif;
    int r;

    r = ncf_lookup_by_mac_string(ncf, "00:00:00:00:00:00", 1, &nif);
    CuAssertIntEquals(tc, 0, r);
    CuAssertPtrEquals(tc, NULL, nif);

    r = ncf_lookup_by_mac_string(ncf, good_mac, 1, &nif);
    CuAssertIntEquals(tc, 1, r);
    CuAssertPtrNotNull(tc, nif);
    CuAssertStrEquals(tc, "br0", nif->name);
    CuAssertStrEquals(tc, good_mac, ncf_if_mac_string(nif));
    ncf_if_free(nif);
    CuAssertIntEquals(tc, 1, ncf->ref);

    r = ncf_lookup_by_mac_string(ncf, good_mac_caps, 1, &nif);
    CuAssertIntEquals(tc, 1, r);
    CuAssertPtrNotNull(tc, nif);
    CuAssertStrEquals(tc, "br0", nif->name);
    CuAssertStrEquals(tc, good_mac, ncf_if_mac_string(nif));
    ncf_if_free(nif);
    CuAssertIntEquals(tc, 1, ncf->ref);
}

static void testDefineUndefine(CuTest *tc) {
    char *bridge_xml = NULL;
    struct netcf_if *nif = NULL;
    int r;

    bridge_xml = read_test_file(tc, "interface/bridge42.xml");
    CuAssertPtrNotNull(tc, bridge_xml);

    nif = ncf_define(ncf, bridge_xml);
    CuAssertPtrNotNull(tc, nif);
    assert_ncf_no_error(tc);

    r = ncf_if_undefine(nif);
    CuAssertIntEquals(tc, 0, r);
    assert_ncf_no_error(tc);

    ncf_close(ncf);
    r = ncf_init(&ncf, root);
    CuAssertIntEquals(tc, 0, r);

    nif = ncf_lookup_by_name(ncf, "br42");
    CuAssertPtrEquals(tc, NULL, nif);
}

#if 0
static void assert_transforms(CuTest *tc, const char *base) {
    char *aug_fname = NULL, *ncf_fname = NULL;
    char *aug_xml_exp = NULL, *ncf_xml_exp = NULL;
    char *aug_xml_act = NULL, *ncf_xml_act = NULL;
    int r;

    r = asprintf(&aug_fname, "freebsd/schema/%s.xml", base);
    r = asprintf(&ncf_fname, "interface/%s.xml", base);

    aug_xml_exp = read_test_file(tc, aug_fname);
    ncf_xml_exp = read_test_file(tc, ncf_fname);

    r = ncf_get_aug(ncf, ncf_xml_exp, &aug_xml_act);
    CuAssertIntEquals(tc, 0, r);

    r = ncf_put_aug(ncf, aug_xml_exp, &ncf_xml_act);
    CuAssertIntEquals(tc, 0, r);

    assert_xml_equals(tc, ncf_fname, ncf_xml_exp, ncf_xml_act);
    assert_xml_equals(tc, aug_fname, aug_xml_exp, aug_xml_act);

    free(ncf_xml_exp);
    free(ncf_xml_act);
    free(aug_xml_exp);
    free(aug_xml_act);
}

static void testTransforms(CuTest *tc) {
    assert_transforms(tc, "bond");
    assert_transforms(tc, "bond-arp");
    assert_transforms(tc, "bond-defaults");
    assert_transforms(tc, "bridge");
    assert_transforms(tc, "bridge-no-address");
    assert_transforms(tc, "bridge-vlan");
    assert_transforms(tc, "bridge-empty");
    assert_transforms(tc, "bridge-bond");
    assert_transforms(tc, "ethernet-static");
    assert_transforms(tc, "ethernet-static-no-prefix");
    assert_transforms(tc, "ethernet-dhcp");
    assert_transforms(tc, "vlan");

    assert_transforms(tc, "ipv6-local");
    assert_transforms(tc, "ipv6-static");
    assert_transforms(tc, "ipv6-dhcp");
    assert_transforms(tc, "ipv6-autoconf");
    assert_transforms(tc, "ipv6-autoconf-dhcp");
    assert_transforms(tc, "ipv6-static-multi");
}
#endif

static void testCorruptedSetup(CuTest *tc) {
    int r;

    ncf_close(ncf);
    ncf = NULL;

    r = ncf_init(&ncf, "/dev/null");
    CuAssertIntEquals(tc, -1, r);
    CuAssertPtrNotNull(tc, ncf);
    r = ncf_error(ncf, NULL, NULL);
    CuAssertIntEquals(tc, NETCF_EFILE, r);
}

int main(void) {
    char *output = NULL;
    CuSuite* suite = CuSuiteNew();

    abs_top_srcdir = getenv("abs_top_srcdir");
    if (abs_top_srcdir == NULL)
        die("env var abs_top_srcdir must be set");

    abs_top_builddir = getenv("abs_top_builddir");
    if (abs_top_builddir == NULL)
        die("env var abs_top_builddir must be set");

    if (asprintf(&src_root, "%s/tests/freebsd/fsroot", abs_top_srcdir) < 0) {
        die("failed to set src_root");
    }

    driver_name = strdup("freebsd");
    if (driver_name == NULL) {
        die("failed to set driver name");
    }

    CuSuiteSetup(suite, setup, teardown);

    SUITE_ADD_TEST(suite, testListInterfaces);
    SUITE_ADD_TEST(suite, testLookupByName);
    SUITE_ADD_TEST(suite, testLookupByNameDecoy);
    SUITE_ADD_TEST(suite, testLookupByMAC);
#if 1
    SUITE_ADD_TEST(suite, testDefineUndefine);
#if 0
    SUITE_ADD_TEST(suite, testTransforms);
#endif
    SUITE_ADD_TEST(suite, testCorruptedSetup);
#endif

    CuSuiteRun(suite);
    CuSuiteSummary(suite, &output);
    CuSuiteDetails(suite, &output);
    printf("%s\n", output);
    free(output);
    free(driver_name);
    return suite->failCount;
}

/*
 * Local variables:
 *  indent-tabs-mode: nil
 *  c-indent-level: 4
 *  c-basic-offset: 4
 *  tab-width: 4
 * End:
 */
/* vim: set ts=4 sw=4 et: */
