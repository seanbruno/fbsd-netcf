/*
 * dutil.h: Global utility functions for driver backends.
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

#ifndef DUTIL_H_
#define DUTIL_H_

struct augeas_pv {
    const char *const path;
    const char *const value;
};

struct augeas_xfm_table {
    unsigned int            size;
    const struct augeas_pv *pv;
};

/* Create a new netcf if instance for interface NAME */
struct netcf_if *make_netcf_if(struct netcf *ncf, char *name);

/* never call these directly. Only call via, eg, "unref(ncf, netcf)" */
void free_netcf(struct netcf *ncf);
void free_netcf_if(struct netcf_if *nif);

/* Like asprintf, but set *STRP to NULL on error */
ATTRIBUTE_FORMAT(printf, 2, 3)
int xasprintf(char **strp, const char *format, ...);

/*
 * Convert an array of char* into a single, newly allocated string
 * with a space between each arg.
 */
char *argv_to_string(const char *const *argv);

/*
 * Error reporting
 */
void report_error(struct netcf *ncf, netcf_errcode_t errcode,
                  const char *format, ...)
    ATTRIBUTE_FORMAT(printf, 3, 4);

void vreport_error(struct netcf *ncf, netcf_errcode_t errcode,
                   const char *format, va_list ap)
    ATTRIBUTE_FORMAT(printf, 3, 0);

/* XSLT extension functions in xslt_ext.c */
int xslt_register_exts(xsltTransformContextPtr ctxt);

/* Parse an XSLT stylesheet residing in the file NCF->data_dir/xml/FNAME */
xsltStylesheetPtr parse_stylesheet(struct netcf *ncf, const char *fname);

/* Apply an XSLT stylesheet to a document with our extensions */
xmlDocPtr apply_stylesheet(struct netcf *ncf, xsltStylesheetPtr style,
                           xmlDocPtr doc);

/* Same as APPLY_STYLESHEET, but convert the resulting XML document into a
 * string */
char *apply_stylesheet_to_string(struct netcf *ncf, xsltStylesheetPtr style,
                                 xmlDocPtr doc);

/* Callback for reporting RelaxNG errors */
void rng_error(void *ctx, const char *format, ...);

/* Initialize a rng pointer from the file NCF->data_dir/xml/FNAME */
xmlRelaxNGPtr rng_parse(struct netcf *ncf, const char *fname);

/* Validate the xml document doc using the previously initialized rng pointer */
void rng_validate(struct netcf *ncf, xmlDocPtr doc);

/* Called from SAX on parsing errors in the XML. */
void catch_xml_error(void *ctx, const char *msg ATTRIBUTE_UNUSED, ...);

/* Parse the xml in XML_STR and return a xmlDocPtr to the parsed structure */
xmlDocPtr parse_xml(struct netcf *ncf, const char *xml_str);

/* Return the content the property NAME in NODE */
char *xml_prop(xmlNodePtr node, const char *name);

/* Create a new node (even if one of the same name already exists) and
 * link it to the document. Return NULL on error.
*/
xmlNodePtr xml_new_node(xmlDocPtr doc,
                        xmlNodePtr parent, const char *name);

/* Find an existing node, or create one if not found, and link it to
 * the document. Return NULL on error.
*/
xmlNodePtr xml_node(xmlDocPtr doc,
                    xmlNodePtr parent, const char *name);

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
