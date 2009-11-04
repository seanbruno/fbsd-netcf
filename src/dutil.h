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
#include <netlink/netlink.h>

struct driver {
    struct augeas     *augeas;
    xsltStylesheetPtr  put;
    xsltStylesheetPtr  get;
    xmlRelaxNGPtr      rng;
    int                ioctl_fd;
    struct nl_handle  *nl_sock;
    struct nl_cache   *link_cache;
    struct nl_cache   *addr_cache;
    unsigned int       load_augeas : 1;
    unsigned int       copy_augeas_xfm : 1;
    unsigned int       augeas_xfm_num_tables;
    const struct augeas_xfm_table **augeas_xfm_tables;
};

struct augeas_pv {
    const char *const path;
    const char *const value;
};

struct augeas_xfm_table {
    unsigned int            size;
    const struct augeas_pv *pv;
};

/* Like asprintf, but set *STRP to NULL on error */
ATTRIBUTE_FORMAT(printf, 2, 3)
int xasprintf(char **strp, const char *format, ...);

/* Add a table of transformations that the next GET_AUGEAS should run */
int add_augeas_xfm_table(struct netcf *ncf,
                         const struct augeas_xfm_table *table);

/* Remove a table of transformations that the next GET_AUGEAS should run */
int remove_augeas_xfm_table(struct netcf *ncf,
                            const struct augeas_xfm_table *table);

/* Get or create the augeas instance from NCF */
struct augeas *get_augeas(struct netcf *ncf);

/* Define a node inside the augeas tree */
ATTRIBUTE_FORMAT(printf, 4, 5)
int defnode(struct netcf *ncf, const char *name, const char *value,
                   const char *format, ...);

/* Format a path by doing a printf of FMT and the var args, then call
   AUG_MATCH on that path. Sets NCF->ERRCODE on error */
ATTRIBUTE_FORMAT(printf, 3, 4)
int aug_fmt_match(struct netcf *ncf, char ***matches, const char *fmt, ...);

/* Free matches from aug_match (or aug_submatch) */
void free_matches(int nint, char ***intf);

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

/* Get a file descriptor to a ioctl socket */
int init_ioctl_fd(struct netcf *ncf);

/* setup the netlink socket */
int netlink_init(struct netcf *ncf);

/*shutdown the netlink socket and release its resources */
int netlink_close(struct netcf *ncf);

/* Check if the interface INTF is up using an ioctl call */
int if_is_active(struct netcf *ncf, const char *intf);

/* Interface types recognized by netcf. */
typedef enum {
    NETCF_IFACE_TYPE_NONE = 0,  /* not yet determined */
    NETCF_IFACE_TYPE_ETHERNET,  /* any physical device is "ethernet" */
    NETCF_IFACE_TYPE_BOND,
    NETCF_IFACE_TYPE_BRIDGE,
    NETCF_IFACE_TYPE_VLAN,
} netcf_if_type_t;

/* Return the type of the interface.
 */
netcf_if_type_t if_type(struct netcf *ncf, const char *intf);

/* Given a netcf_if_type_t enum value, return a const char *representation
 * This pointer has an indefinite life, and shouldn't be / can't be free'd.
 */
const char *if_type_str(netcf_if_type_t type);

/* Create a new netcf if instance for interface NAME */
struct netcf_if *make_netcf_if(struct netcf *ncf, char *name);

/* Transform the interface XML NCF_XML into Augeas XML AUG_XML */
int dutil_get_aug(struct netcf *ncf, const char *ncf_xml, char **aug_xml);

/* Transform the Augeas XML AUG_XML into interface XML NCF_XML */
int dutil_put_aug(struct netcf *ncf, const char *aug_xml, char **ncf_xml);

/* Add the state of the interface (currently all addresses + netmasks)
 * to its xml document.
 */
void add_state_to_xml_doc(struct netcf_if *nif, xmlDocPtr doc);

/* Run the program PROG with the single argument ARG */
void run1(struct netcf *ncf, const char *prog, const char *arg);

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
