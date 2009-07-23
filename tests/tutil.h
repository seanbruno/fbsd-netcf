/*
 * tutil.h: Global utility functions for automated testing of netcf
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

#ifndef _TUTIL_H
#define _TUTIL_H

#include <config.h>
#include <libxml/tree.h>

#include "internal.h"
#include "cutest.h"
#include "safe-alloc.h"

#include "tutil.h"

#define die(msg)                                                    \
    do {                                                            \
        fprintf(stderr, "%s:%d: Fatal error: %s\n", __FILE__, __LINE__, msg); \
        exit(EXIT_FAILURE);                                         \
    } while(0)

/* Write a string of FORMAT, ... to STRP */
void format_error(char **strp, const char *format, ...);

/* Read data from file, return pointer to result */
char *read_test_file(CuTest *tc, const char *relpath);

/* Assert that the ncf instance is in the NOERROR-state */
void assert_ncf_no_error(CuTest *tc);

/* Parse the xml in XML_STR and return a xmlDocPtr to the parsed structure */
xmlDocPtr parse_xml(const char *xml_str);

/* Check that all attributes of N1 and N2 are equal */
int xml_attrs_subset(xmlNodePtr n1, xmlNodePtr n2, char **err);

/* Get the next element of type XML_ELEMENT_NODE following N, or NULL */
xmlNodePtr xml_next_element(xmlNodePtr n);

/* Check that the nodes N1 and N2 are alike (including their children) */
int xml_nodes_equal(xmlNodePtr n1, xmlNodePtr n2, char **err);

/* Assert the the EXP (expected) and ACT (actual) XML text blobs are alike.
 * FNAME is used for error reporting in the case that they are not alike. */
void assert_xml_equals(CuTest *tc, const char *fname,
                       char *exp, char *act);

/* Run command formed by FORMAT, raise an error if the the command did not
 * exit with status 0 */
void run(CuTest *tc, const char *format, ...);

/* Setup test directories */
void setup(CuTest *tc);

/* Close and free all resources */
void teardown(ATTRIBUTE_UNUSED CuTest *tc);

#endif

/*
 * Local variables:
 *  indent-tabs-mode: nil
 *  c-indent-level: 4
 *  c-basic-offset: 4
 *  tab-width: 4
 * End:
 */
/* vim: set ts=4 sw=4 et: */
