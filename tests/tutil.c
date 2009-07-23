/*
 * tutil.c: Global utility functions for automated testing of netcf
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
#include <stdio.h>
#include <libxml/tree.h>

#include "internal.h"
#include "cutest.h"
#include "safe-alloc.h"
#include "read-file.h"

#include "tutil.h"

const char *abs_top_srcdir;
const char *abs_top_builddir;
char *driver_name = NULL;
char *root, *src_root;
struct netcf *ncf;

void format_error(char **strp, const char *format, ...) {
  va_list args;
  int r;

  va_start (args, format);
  r = vasprintf (strp, format, args);
  va_end (args);
  if (r < 0)
      die("Failed to format error message (out of memory)");
}

char *read_test_file(CuTest *tc, const char *relpath) {
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

void assert_ncf_no_error(CuTest *tc) {
    CuAssertIntEquals(tc, NETCF_NOERROR, ncf_error(ncf, NULL, NULL));
}

xmlDocPtr parse_xml(const char *xml_str) {
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

int xml_attrs_subset(xmlNodePtr n1, xmlNodePtr n2, char **err) {
    for (xmlAttrPtr a1 = n1->properties; a1 != NULL; a1 = a1->next) {
        xmlChar *v1 = xmlGetProp(n1, a1->name);
        xmlChar *v2 = xmlGetProp(n2, a1->name);
        if (!xmlStrEqual(v1, v2)) {
            format_error(err, "Different values for attribute %s/@%s: %s != %s (lines %d and %d)", n1->name, a1->name, v1, v2, n1->line, n2->line);
            return 0;
        }
    }
    return 1;
}

xmlNodePtr xml_next_element(xmlNodePtr n) {
    while (n != NULL && n->type != XML_ELEMENT_NODE)
        n = n->next;
    return n;
}

int xml_nodes_equal(xmlNodePtr n1, xmlNodePtr n2, char **err) {
    if (n1 == NULL && n2 == NULL)
        return 1;
    if (n1 == NULL && n2 != NULL) {
        format_error(err, "First node null, second node %s (line %d)",
                     n2->name, n2->line);
        return 0;
    }
    if (n1 != NULL && n2 == NULL) {
        format_error(err, "First node %s, second node null (line %d)",
                     n1->name, n1->line);
        return 0;
    }
    if (!xmlStrEqual(n1->name, n2->name)) {
        format_error(err, "Different node names: %s != %s (lines %d and %d)",
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
        format_error(err, "Additional element %s (line %d)",
                     n1->name, n1->line);
        return 0;
    } else if (n2 != NULL) {
        format_error(err, "Additional element %s (line %d)",
                     n2->name, n2->line);
        return 0;
    }
    return 1;
}

void assert_xml_equals(CuTest *tc, const char *fname,
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
        format_error(&msg, "%s: %s\nExpected XML:\n%s\nActual XML:\n%s\n",
                     fname, err, exp, act);
        CuFail(tc, msg);
    }
}

void run(CuTest *tc, const char *format, ...) {
    char *command;
    va_list args;
    int r;

    va_start(args, format);
    r = vasprintf(&command, format, args);
    va_end (args);
    if (r < 0)
        CuFail(tc, "Failed to format command (out of memory)");
    r = system(command);
    if (r < 0 || (WIFEXITED(r) && WEXITSTATUS(r) != 0)) {
        char *msg;
        r = asprintf(&msg, "Command %s failed with status %d\n",
                     command, WEXITSTATUS(r));
        CuFail(tc, msg);
        free(msg);
    }
}

void setup(CuTest *tc) {
    int r;

    if (asprintf(&root, "%s/build/test_%s/%s",
                 abs_top_builddir, driver_name, tc->name) < 0) {
        CuFail(tc, "failed to set root");
    }

    run(tc, "test -d %s && chmod -R u+w %s || :", root, root);
    run(tc, "rm -rf %s", root);
    run(tc, "mkdir -p %s", root);
    run(tc, "cp -pr %s/* %s", src_root, root);
    run(tc, "chmod -R u+w %s", root);
    run(tc, "chmod -R a-w %s/sys", root);

    r = ncf_init(&ncf, root);
    CuAssertIntEquals(tc, 0, r);
}

void teardown(ATTRIBUTE_UNUSED CuTest *tc) {
    ncf_close(ncf);
    free(root);
    root = NULL;
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
