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

#include <libxml/relaxng.h>
#include <libxslt/xsltInternals.h>

struct driver {
    struct augeas     *augeas;
    xsltStylesheetPtr  put;
    xsltStylesheetPtr  get;
    xmlRelaxNGPtr      rng;
    int                ioctl_fd;
    unsigned int       load_augeas : 1;
};

/* Like asprintf, but set *STRP to NULL on error */
int xasprintf(char **strp, const char *format, ...);

/* Free matches from aug_match (or aug_submatch) */
void free_matches(int nint, char ***intf);

/* Parse an XSLT stylesheet residing in the file NCF->data_dir/xml/FNAME */
xsltStylesheetPtr parse_stylesheet(struct netcf *ncf, const char *fname);

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

/* Get a file descriptor to a ioctl socket */
int init_ioctl_fd(struct netcf *ncf);

/* Check if the interface INTF is up using a ioctl call */
int is_active(struct netcf *ncf, const char *intf);

/* Transform the interface XML NCF_XML into Augeas XML AUG_XML */
int dutil_get_aug(struct netcf *ncf, const char *ncf_xml, char **aug_xml);

/* Transform the Augeas XML AUG_XML into interface XML NCF_XML */
int dutil_put_aug(struct netcf *ncf, const char *aug_xml, char **ncf_xml);

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
