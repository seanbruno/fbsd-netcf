/*
 * test-initscripts.c:
 *
 * Copyright (C) 2009 Red Hat Inc.
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
 *
 * Author: David Lutterkort <lutter@redhat.com>
 */

#include <config.h>
#include "netcf.h"
#include "internal.h"
#include "cutest.h"
#include "safe-alloc.h"

#include <stdio.h>

static const char *abs_top_srcdir;
static char *root;
static struct netcf *ncf;

#define die(msg)                                                    \
    do {                                                            \
        fprintf(stderr, "%d: Fatal error: %s\n", __LINE__, msg);    \
        exit(EXIT_FAILURE);                                         \
    } while(0)

static void setup(CuTest *tc) {
    int r;
    r = ncf_init(&ncf, root);
    CuAssertIntEquals(tc, 0, r);
}

static void teardown(ATTRIBUTE_UNUSED CuTest *tc) {
    ncf_close(ncf);
}

static void testListInterfaces(CuTest *tc) {
    int nint;
    char **names;
    static const char *const exp_names[] = { "br0", "bond0", "lo" };
    static const int exp_nint = ARRAY_CARDINALITY(exp_names);

    nint = ncf_num_of_interfaces(ncf);
    CuAssertIntEquals(tc, exp_nint, nint);
    if (ALLOC_N(names, nint) < 0)
        die("allocation failed");
    nint = ncf_list_interfaces(ncf, nint, names);
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

    nif = ncf_lookup_by_name(ncf, "br0");
    CuAssertPtrNotNull(tc, nif);
    CuAssertStrEquals(tc, "/files/etc/sysconfig/network-scripts/ifcfg-br0",
                      nif->path);
    ncf_if_free(nif);
    CuAssertIntEquals(tc, 1, ncf->ref);
}

int main(void) {
    char *output = NULL;
    CuSuite* suite = CuSuiteNew();

    abs_top_srcdir = getenv("abs_top_srcdir");
    if (abs_top_srcdir == NULL)
        die("env var abs_top_srcdir must be set");
    if (asprintf(&root, "%s/tests/root", abs_top_srcdir) < 0) {
        die("failed to set root");
    }

    CuSuiteSetup(suite, setup, teardown);

    SUITE_ADD_TEST(suite, testListInterfaces);
    SUITE_ADD_TEST(suite, testLookupByName);

    CuSuiteRun(suite);
    CuSuiteSummary(suite, &output);
    CuSuiteDetails(suite, &output);
    printf("%s\n", output);
    free(output);
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
