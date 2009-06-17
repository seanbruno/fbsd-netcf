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
#include "read-file.h"

#include <stdio.h>

#include <libxml/tree.h>

static const char *abs_top_srcdir;
static char *root;
static struct netcf *ncf;

#define die(msg)                                                    \
    do {                                                            \
        fprintf(stderr, "%s:%d: Fatal error: %s\n", __FILE__, __LINE__, msg); \
        exit(EXIT_FAILURE);                                         \
    } while(0)

static char *read_test_file(CuTest *tc, const char *relpath) {
    char *path = NULL;
    char *txt = NULL;
    size_t length;

    if (asprintf(&path, "%s/tests/%s", abs_top_srcdir, relpath) < 0)
        CuFail(tc, "Could not format path for file");
    txt = read_file(path, &length);
    if (txt == NULL)
        CuFail(tc, "Failed to read file");
    return txt;
}

static xmlDocPtr parse_xml(const char *xml_str) {
    xmlParserCtxtPtr pctxt;
    xmlDocPtr xml = NULL;

    /* Set up a parser context so we can catch the details of XML errors. */
    pctxt = xmlNewParserCtxt();
    if (pctxt == NULL || pctxt->sax == NULL)
        die("xmlNewParserCtxt failed");

    xml = xmlCtxtReadDoc (pctxt, BAD_CAST xml_str, "netcf.xml", NULL,
                          XML_PARSE_NOENT | XML_PARSE_NONET |
                          XML_PARSE_NOWARNING);
    if (xml == NULL)
        die("failed to parse xml document");
    if (xmlDocGetRootElement(xml) == NULL)
        die("missing root element");

    xmlFreeParserCtxt(pctxt);
    return xml;
}

static int xml_attrs_subset(xmlNodePtr n1, xmlNodePtr n2, char **err) {
    for (xmlAttrPtr a1 = n1->properties; a1 != NULL; a1 = a1->next) {
        xmlChar *v1 = xmlGetProp(n1, a1->name);
        xmlChar *v2 = xmlGetProp(n2, a1->name);
        if (!xmlStrEqual(v1, v2)) {
            asprintf(err, "Different values for attribute %s/@%s: %s != %s (lines %d and %d)", n1->name, a1->name, v1, v2, n1->line, n2->line);
            return 0;
        }
    }
    return 1;
}

static xmlNodePtr xml_next_element(xmlNodePtr n) {
    while (n != NULL && n->type != XML_ELEMENT_NODE)
        n = n->next;
    return n;
}

static int xml_nodes_equal(xmlNodePtr n1, xmlNodePtr n2, char **err) {
    if (n1 == NULL && n2 == NULL)
        return 1;
    if (n1 == NULL && n2 != NULL) {
        asprintf(err, "First node null, second node %s (line %d)",
                 n2->name, n2->line);
        return 0;
    }
    if (n1 != NULL && n2 == NULL) {
        asprintf(err, "First node %s, second node null (line %d)",
                 n1->name, n1->line);
        return 0;
    }
    if (!xmlStrEqual(n1->name, n2->name)) {
        asprintf(err, "Different node names: %s != %s (lines %d and %d)",
                 n1->name, n2->name, n1->line, n2->line);
        return 0;
    }
    if (! xml_attrs_subset(n1, n2, err))
        return 0;
    if (! xml_attrs_subset(n2, n1, err))
        return 0;

    n1 = xml_next_element(n1->children);
    n2 = xml_next_element(n2->children);
    while (n1 != NULL && n2 != NULL) {
        if (! xml_nodes_equal(n1, n2, err))
            return 0;
        n1 = xml_next_element(n1->next);
        n2 = xml_next_element(n2->next);
    }
    if (n1 != NULL) {
        asprintf(err, "Additional element %s (line %d)", n1->name, n1->line);
        return 0;
    } else if (n2 != NULL) {
        asprintf(err, "Additional element %s (line %d)", n2->name, n2->line);
        return 0;
    }
    return 1;
}

static void assert_xml_equals(CuTest *tc, const char *fname,
                              char *exp, char *act) {
    char *err, *msg;
    xmlDocPtr exp_doc, act_doc;
    int result;

    exp_doc = parse_xml(exp);
    act_doc = parse_xml(act);
    result = xml_nodes_equal(xmlDocGetRootElement(exp_doc),
                             xmlDocGetRootElement(act_doc), &err);
    xmlFreeDoc(exp_doc);
    xmlFreeDoc(act_doc);

    if (! result) {
        asprintf(&msg, "%s: %s", fname, err);
        CuFail(tc, msg);
    }
}

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
    CuAssertStrEquals(tc, "br0", nif->name);
    ncf_if_free(nif);
    CuAssertIntEquals(tc, 1, ncf->ref);
}

static void testLookupByMAC(CuTest *tc) {
    static const char *const good_mac = "aa:bb:cc:dd:ee:ff";
    struct netcf_if *nif;

    nif = ncf_lookup_by_mac_string(ncf, "00:00:00:00:00:00");
    CuAssertPtrEquals(tc, NULL, nif);
    nif = ncf_lookup_by_mac_string(ncf, good_mac);
    CuAssertPtrNotNull(tc, nif);
    CuAssertStrEquals(tc, "br0", nif->name);
    CuAssertStrEquals(tc, good_mac, ncf_if_mac_string(nif));
    ncf_if_free(nif);
    CuAssertIntEquals(tc, 1, ncf->ref);
}

static void assert_transforms(CuTest *tc, const char *base) {
    char *aug_fname = NULL, *ncf_fname = NULL;
    char *aug_xml_exp = NULL, *ncf_xml_exp = NULL;
    char *aug_xml_act = NULL, *ncf_xml_act = NULL;
    int r;

    r = asprintf(&aug_fname, "initscripts/%s.xml", base);
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
    assert_transforms(tc, "bridge");
    assert_transforms(tc, "ethernet-static");
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
    SUITE_ADD_TEST(suite, testLookupByMAC);
    SUITE_ADD_TEST(suite, testTransforms);

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
