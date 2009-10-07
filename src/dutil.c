/*
 * dutil.c: Global utility functions for driver backends.
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
#include <internal.h>

#include <augeas.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>


#include "safe-alloc.h"
#include "ref.h"
#include "list.h"
#include "netcf.h"
#include "dutil.h"

#include <libxml/parser.h>
#include <libxml/relaxng.h>
#include <libxml/tree.h>
#include <libxslt/xslt.h>
#include <libxslt/xsltInternals.h>
#include <libxslt/transform.h>
#include <libxslt/xsltutils.h>

/* Like asprintf, but set *STRP to NULL on error */
int xasprintf(char **strp, const char *format, ...) {
  va_list args;
  int result;

  va_start (args, format);
  result = vasprintf (strp, format, args);
  va_end (args);
  if (result < 0)
      *strp = NULL;
  return result;
}

int add_augeas_xfm_table(struct netcf *ncf,
                         const struct augeas_xfm_table *xfm) {
    int slot, r;
    struct driver *d = ncf->driver;

    if (d->augeas_xfm_num_tables == 0) {
        slot = 0;
        r = ALLOC(d->augeas_xfm_tables);
        ERR_NOMEM(r < 0, ncf);
        d->augeas_xfm_num_tables = 1;
    } else {
        for (slot =0;
             slot < d->augeas_xfm_num_tables
                 && d->augeas_xfm_tables[slot] != NULL;
             slot++);
        if (slot == d->augeas_xfm_num_tables) {
            r = REALLOC_N(ncf->driver->augeas_xfm_tables, slot + 1);
            ERR_NOMEM(r < 0, ncf);
            d->augeas_xfm_num_tables = slot + 1;
        }
    }

    ncf->driver->augeas_xfm_tables[slot] = xfm;
    ncf->driver->copy_augeas_xfm = 1;
    return 0;
 error:
    return -1;
}

int remove_augeas_xfm_table(struct netcf *ncf,
                            const struct augeas_xfm_table *xfm) {
    int slot;
    const int last = ncf->driver->augeas_xfm_num_tables;
    const struct augeas_xfm_table **tables =
        ncf->driver->augeas_xfm_tables;

    for (slot = 0; slot < last && tables[slot] != xfm; slot++);
    if (tables[slot] == xfm) {
        tables[slot] = NULL;
        ncf->driver->copy_augeas_xfm = 1;
    }
    return 0;
}

/* Get the Augeas instance; if we already initialized it, just return
 * it. Otherwise, create a new one and return that.
 */
struct augeas *get_augeas(struct netcf *ncf) {
    int r;

    if (ncf->driver->augeas == NULL) {
        struct augeas *aug;
        char *path;

        r = xasprintf(&path, "%s/lenses", ncf->data_dir);
        ERR_NOMEM(r < 0, ncf);

        aug = aug_init(ncf->root, path, AUG_NO_MODL_AUTOLOAD);
        FREE(path);
        ERR_THROW(aug == NULL, ncf, EOTHER, "aug_init failed");
        ncf->driver->augeas = aug;
        ncf->driver->copy_augeas_xfm = 1;
    }

    if (ncf->driver->copy_augeas_xfm) {
        struct augeas *aug = ncf->driver->augeas;
        /* Only look at a few config files */
        r = aug_rm(aug, "/augeas/load/*");
        ERR_THROW(r < 0, ncf, EOTHER, "aug_rm failed in get_augeas");

        for (int slot = 0; slot < ncf->driver->augeas_xfm_num_tables; slot++) {
            const struct augeas_xfm_table *t =
                ncf->driver->augeas_xfm_tables[slot];
            if (t == NULL)
                continue;
            for (int i=0; i < t->size; i++) {
                r = aug_set(aug, t->pv[i].path, t->pv[i].value);
                ERR_THROW(r < 0, ncf, EOTHER,
                          "transform setup failed to set %s",
                          t->pv[i].path);
            }
        }
        ncf->driver->copy_augeas_xfm = 0;
        ncf->driver->load_augeas = 1;
    }

    if (ncf->driver->load_augeas) {
        struct augeas *aug = ncf->driver->augeas;
        /* Undefine all our variables to work around bug 79 in Augeas */
        aug_defvar(aug, "iptables", NULL);
        aug_defvar(aug, "fw", NULL);
        aug_defvar(aug, "fw_custom", NULL);
        aug_defvar(aug, "ipt_filter", NULL);

        r = aug_load(aug);
        ERR_THROW(r < 0, ncf, EOTHER, "failed to load config files");

        /* FIXME: we need to produce _much_ better diagnostics here - need
         * to analyze what came back in /augeas//error; ultimately, we need
         * to understand whether this is harmless or a real error. For real
         * errors, we need to return an error.
        */
        r = aug_match(aug, "/augeas//error", NULL);
        if (r > 0 && NCF_DEBUG(ncf)) {
            fprintf(stderr, "warning: augeas initialization had errors\n");
            fprintf(stderr, "please file a bug with the following lines in the bug report:\n");
            aug_print(aug, stderr, "/augeas//error");
        }
        ERR_THROW(r > 0, ncf, EOTHER, "errors in loading some config files");
        ncf->driver->load_augeas = 0;
    }
    return ncf->driver->augeas;
 error:
    aug_close(ncf->driver->augeas);
    ncf->driver->augeas = NULL;
    return NULL;
}

ATTRIBUTE_FORMAT(printf, 4, 5)
int defnode(struct netcf *ncf, const char *name, const char *value,
                   const char *format, ...) {
    struct augeas *aug = get_augeas(ncf);
    va_list ap;
    char *expr = NULL;
    int r, created;

    va_start(ap, format);
    r = vasprintf (&expr, format, ap);
    va_end (ap);
    if (r < 0)
        expr = NULL;
    ERR_NOMEM(r < 0, ncf);

    r = aug_defnode(aug, name, expr, value, &created);
    ERR_THROW(r < 0, ncf, EOTHER, "failed to define node %s", name);

    /* Fallthrough intentional */
 error:
    free(expr);
    return (r < 0) ? -1 : created;
}

int aug_fmt_match(struct netcf *ncf, char ***matches, const char *fmt, ...) {
    struct augeas *aug = NULL;
    char *path = NULL;
    va_list args;
    int r;

    aug = get_augeas(ncf);
    ERR_BAIL(ncf);

    va_start(args, fmt);
    r = vasprintf(&path, fmt, args);
    va_end(args);
    if (r < 0) {
        path = NULL;
        ERR_NOMEM(1, ncf);
    }

    r = aug_match(aug, path, matches);
    ERR_COND_BAIL(r < 0, ncf, EOTHER);

    free(path);
    return r;
 error:
    free(path);
    return -1;
}

void free_matches(int nint, char ***intf) {
    if (*intf != NULL) {
        for (int i=0; i < nint; i++)
            FREE((*intf)[i]);
        FREE(*intf);
    }
}

xsltStylesheetPtr parse_stylesheet(struct netcf *ncf,
                                          const char *fname) {
    xsltStylesheetPtr result = NULL;
    char *path = NULL;
    int r;

    r = xasprintf(&path, "%s/xml/%s", ncf->data_dir, fname);
    ERR_NOMEM(r < 0, ncf);

    // FIXME: Error checking ??
    result = xsltParseStylesheetFile(BAD_CAST path);
 error:
    free(path);
    return result;
}

ATTRIBUTE_FORMAT(printf, 2, 3)
static void apply_stylesheet_error(void *ctx, const char *format, ...) {
    struct netcf *ncf = ctx;
    va_list ap;

    va_start(ap, format);
    vreport_error(ncf, NETCF_EXSLTFAILED, format, ap);
    va_end(ap);
}

xmlDocPtr apply_stylesheet(struct netcf *ncf, xsltStylesheetPtr style,
                           xmlDocPtr doc) {
    xsltTransformContextPtr ctxt;
    xmlDocPtr res = NULL;
    int r;

    ctxt = xsltNewTransformContext(style, doc);
    ERR_NOMEM(ctxt == NULL, ncf);

    xsltSetTransformErrorFunc(ctxt, ncf, apply_stylesheet_error);

    r = xslt_register_exts(ctxt);
    ERR_NOMEM(r < 0, ncf);

    res = xsltApplyStylesheetUser(style, doc, NULL, NULL, NULL, ctxt);
    if ((ctxt->state == XSLT_STATE_ERROR) ||
        (ctxt->state == XSLT_STATE_STOPPED)) {
        xmlFreeDoc(res);
        res = NULL;
        /* Fallback, in case our error handler isn't called */
        report_error(ncf, NETCF_EXSLTFAILED, NULL);
    }

error:
    xsltFreeTransformContext(ctxt);
    return res;
}

char *apply_stylesheet_to_string(struct netcf *ncf, xsltStylesheetPtr style,
                                 xmlDocPtr doc) {
    xmlDocPtr doc_xfm = NULL;
    char *result = NULL;
    int r, result_len;

    doc_xfm = apply_stylesheet(ncf, style, doc);
    ERR_BAIL(ncf);

    r = xsltSaveResultToString((xmlChar **) &result, &result_len,
                               doc_xfm, style);
    ERR_NOMEM(r < 0, ncf);
    xmlFreeDoc(doc_xfm);
    return result;

 error:
    FREE(result);
    xmlFreeDoc(doc_xfm);
    return NULL;
}

/* Callback for reporting RelaxNG errors */
void rng_error(void *ctx, const char *format, ...) {
    struct netcf *ncf = ctx;
    va_list ap;

    va_start(ap, format);
    vreport_error(ncf, NETCF_EXMLINVALID, format, ap);
    va_end(ap);
}

xmlRelaxNGPtr rng_parse(struct netcf *ncf, const char *fname) {
    char *path = NULL;
    xmlRelaxNGPtr result = NULL;
    xmlRelaxNGParserCtxtPtr ctxt = NULL;
    int r;

    r = xasprintf(&path, "%s/xml/%s", ncf->data_dir, fname);
    ERR_NOMEM(r < 0, ncf);

    ctxt = xmlRelaxNGNewParserCtxt(path);
    xmlRelaxNGSetParserErrors(ctxt, rng_error, rng_error, ncf);

    result = xmlRelaxNGParse(ctxt);

 error:
    xmlRelaxNGFreeParserCtxt(ctxt);
    free(path);
    return result;
}

void rng_validate(struct netcf *ncf, xmlDocPtr doc) {
	xmlRelaxNGValidCtxtPtr ctxt;
	int r;

	ctxt = xmlRelaxNGNewValidCtxt(ncf->driver->rng);
	xmlRelaxNGSetValidErrors(ctxt, rng_error, rng_error, ncf);

    r = xmlRelaxNGValidateDoc(ctxt, doc);
    if (r != 0 && ncf->errcode == NETCF_NOERROR)
        report_error(ncf, NETCF_EXMLINVALID,
           "Interface definition fails to validate");

	xmlRelaxNGFreeValidCtxt(ctxt);
}

/* Called from SAX on parsing errors in the XML. */
void catch_xml_error(void *ctx, const char *msg ATTRIBUTE_UNUSED, ...) {
    xmlParserCtxtPtr ctxt = (xmlParserCtxtPtr) ctx;

    if (ctxt != NULL) {
        struct netcf *ncf = ctxt->_private;

        if (ctxt->lastError.level == XML_ERR_FATAL &&
            ctxt->lastError.message != NULL) {
            report_error(ncf, NETCF_EXMLPARSER,
                         "at line %d: %s",
                         ctxt->lastError.line,
                         ctxt->lastError.message);
        }
    }
}

xmlDocPtr parse_xml(struct netcf *ncf, const char *xml_str) {
    xmlParserCtxtPtr pctxt;
    xmlDocPtr xml = NULL;

    /* Set up a parser context so we can catch the details of XML errors. */
    pctxt = xmlNewParserCtxt();
    ERR_NOMEM(pctxt == NULL || pctxt->sax == NULL, ncf);

    pctxt->sax->error = catch_xml_error;
    pctxt->_private = ncf;

    xml = xmlCtxtReadDoc (pctxt, BAD_CAST xml_str, "netcf.xml", NULL,
                          XML_PARSE_NOENT | XML_PARSE_NONET |
                          XML_PARSE_NOWARNING);
    ERR_THROW(xml == NULL, ncf, EXMLPARSER,
              "failed to parse xml document");
    ERR_THROW(xmlDocGetRootElement(xml) == NULL, ncf, EINTERNAL,
              "missing root element");

    xmlFreeParserCtxt(pctxt);
    return xml;
error:
    xmlFreeParserCtxt (pctxt);
    xmlFreeDoc (xml);
    return NULL;
}

char *xml_prop(xmlNodePtr node, const char *name) {
    return (char *) xmlGetProp(node, BAD_CAST name);
}

int init_ioctl_fd(struct netcf *ncf) {
    int ioctl_fd;
    int flags;

    ioctl_fd = socket(AF_INET, SOCK_STREAM, 0);
    ERR_THROW(ioctl_fd < 0, ncf, EINTERNAL, "failed to open socket for interface ioctl");

    flags = fcntl(ioctl_fd, F_GETFD);
    ERR_THROW(flags < 0, ncf, EINTERNAL, "failed to get flags for ioctl socket");

    flags = fcntl(ioctl_fd, F_SETFD, flags | FD_CLOEXEC);
    ERR_THROW(flags < 0, ncf, EINTERNAL, "failed to set FD_CLOEXEC flag on ioctl socket");
    return ioctl_fd;

error:
    if (ioctl_fd >= 0)
        close(ioctl_fd);
    return -1;
}

int if_is_active(struct netcf *ncf, const char *intf) {
    struct ifreq ifr;

    MEMZERO(&ifr, 1);
    strncpy(ifr.ifr_name, intf, sizeof(ifr.ifr_name));
    ifr.ifr_name[sizeof(ifr.ifr_name) - 1] = '\0';
    if (ioctl(ncf->driver->ioctl_fd, SIOCGIFFLAGS, &ifr))  {
        report_error(ncf, NETCF_EIOCTL, "Failed to get interface flags for %s", intf);
        return 0;
    }
    return ((ifr.ifr_flags & IFF_UP) == IFF_UP);
}

unsigned int if_ipv4_address(struct netcf *ncf, const char *intf) {
    struct ifreq ifr;

    if (!if_is_active(ncf, intf)) {
        /* SIOCGIFADDR fails on a device that is down */
        return 0;
    }
    MEMZERO(&ifr, 1);
    strncpy(ifr.ifr_name, intf, sizeof(ifr.ifr_name));
    ifr.ifr_name[sizeof(ifr.ifr_name) - 1] = '\0';
    if (ioctl(ncf->driver->ioctl_fd, SIOCGIFADDR, &ifr))  {
        if (errno != EADDRNOTAVAIL) {
            report_error(ncf, NETCF_EIOCTL, "Failed to get ipv4 address for %s - %s",
                         intf, strerror(errno));
        }
        return 0;
    }

    return (((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr.s_addr);
}

unsigned int if_ipv4_netmask(struct netcf *ncf, const char *intf) {
    struct ifreq ifr;

    if (!if_is_active(ncf, intf)) {
        /* SIOCGIFNETMASK fails on a device that is down */
        return 0;
    }
    MEMZERO(&ifr, 1);
    strncpy(ifr.ifr_name, intf, sizeof(ifr.ifr_name));
    ifr.ifr_name[sizeof(ifr.ifr_name) - 1] = '\0';
    if (ioctl(ncf->driver->ioctl_fd, SIOCGIFNETMASK, &ifr))  {
        if (errno != EADDRNOTAVAIL) {
            report_error(ncf, NETCF_EIOCTL, "Failed to get ipv4 netmask for %s - %s",
                         intf, strerror(errno));
        }
        return 0;
    }

    return (((struct sockaddr_in *)&ifr.ifr_netmask)->sin_addr.s_addr);
}

int if_ipv4_prefix(struct netcf *ncf, const char *intf) {
    unsigned int nmv4 = if_ipv4_netmask(ncf, intf);
    int prefix = 0;

    nmv4 = ntohl(nmv4);
    while ((nmv4 & 0xFFFFFFFF) != 0) {
        nmv4 <<= 1;
        prefix++;
    }
    return prefix;
}

/* Create a new netcf if instance for interface NAME */
struct netcf_if *make_netcf_if(struct netcf *ncf, char *name) {
    int r;
    struct netcf_if *result = NULL;

    r = make_ref(result);
    ERR_NOMEM(r < 0, ncf);
    result->ncf = ref(ncf);
    result->name = name;
    return result;

 error:
    unref(result, netcf_if);
    return result;
}

/*
 * Test interface
 */
int dutil_get_aug(struct netcf *ncf, const char *ncf_xml, char **aug_xml) {
    xmlDocPtr ncf_doc = NULL, aug_doc = NULL;
    int result = -1;

    ncf_doc = parse_xml(ncf, ncf_xml);
    ERR_BAIL(ncf);

    rng_validate(ncf, ncf_doc);
    ERR_BAIL(ncf);

    *aug_xml = apply_stylesheet_to_string(ncf, ncf->driver->get, ncf_doc);
    ERR_BAIL(ncf);

    /* fallthrough intentional */
    result = 0;
 error:
    xmlFreeDoc(ncf_doc);
    xmlFreeDoc(aug_doc);
    return result;
}

/* Transform the Augeas XML AUG_XML into interface XML NCF_XML */
int dutil_put_aug(struct netcf *ncf, const char *aug_xml, char **ncf_xml) {
    xmlDocPtr ncf_doc = NULL, aug_doc = NULL;
    int result = -1;

    aug_doc = parse_xml(ncf, aug_xml);
    ERR_BAIL(ncf);

    *ncf_xml = apply_stylesheet_to_string(ncf, ncf->driver->put, aug_doc);
    ERR_BAIL(ncf);

    /* fallthrough intentional */
    result = 0;
 error:
    xmlFreeDoc(ncf_doc);
    xmlFreeDoc(aug_doc);
    return result;
}

/* Given an xml document that follows interface.rng, add the IP
 * address and prefix under protocol/ip
 */
void add_state_to_xml_doc(xmlDocPtr doc, struct netcf *ncf ATTRIBUTE_UNUSED,
                          unsigned int ipv4, int prefix) {

    xmlNodePtr root = NULL, proto = NULL, ip = NULL, cur;
    xmlAttrPtr prop = NULL;
    char ip_str[48], prefix_str[16];


    root = xmlDocGetRootElement(doc);
    ERR_THROW((root == NULL), ncf, EINTERNAL,
              "failed to get document root element");

    ERR_THROW(!xmlStrEqual(root->name, BAD_CAST "interface"),
              ncf, EINTERNAL, "root document is not an interface");

    for (cur = root->children; cur != NULL; cur = cur->next) {
        if ((cur->type == XML_ELEMENT_NODE) &&
            xmlStrEqual(cur->name, BAD_CAST "protocol")) {
            xmlChar *family = xmlGetProp(cur, BAD_CAST "family");
            if (family != NULL) {
                if (xmlStrEqual(family, BAD_CAST "ipv4"))
                    proto = cur;
                xmlFree(family);
                if (proto != NULL) {
                    break;
                }
            }
        }
    }

    if (proto == NULL) {
        proto = xmlNewDocNode(doc, NULL, BAD_CAST "protocol", NULL);
        ERR_NOMEM(proto == NULL, ncf);

        cur = xmlAddChild(root, proto);
        ERR_NOMEM(cur == NULL, ncf);
        prop = xmlSetProp(proto, BAD_CAST "family", BAD_CAST "ipv4");
        ERR_NOMEM(prop == NULL, ncf);

    }

    for (cur = proto->children; cur != NULL; cur = cur->next) {
        if ((cur->type == XML_ELEMENT_NODE) &&
            xmlStrEqual(cur->name, BAD_CAST "ip")) {
            break;
        }
    }

    if (ip == NULL) {
        ip = xmlNewDocNode(doc, NULL, BAD_CAST "ip", NULL);
        ERR_NOMEM(ip == NULL, ncf);

        cur = xmlAddChild(proto, ip);
        ERR_NOMEM(cur == NULL, ncf);
    }

    inet_ntop(AF_INET, (struct in_addr *)&ipv4, ip_str, sizeof(ip_str));
    prop = xmlSetProp(ip, BAD_CAST "address", BAD_CAST ip_str);
    ERR_NOMEM(prop == NULL, ncf);

    snprintf(prefix_str, sizeof(prefix_str), "%d", prefix);
    prop = xmlSetProp(ip, BAD_CAST "prefix", BAD_CAST prefix_str);
    ERR_NOMEM(prop == NULL, ncf);

    prop = xmlSetProp(ip, BAD_CAST "source", BAD_CAST "device");
    ERR_NOMEM(prop == NULL, ncf);

error:
    return;
}

/*
 * Bringing interfaces up/down
 */

/* Run the program PROG with the single argument ARG */
void run1(struct netcf *ncf, const char *prog, const char *arg) {
    const char *const argv[] = {
        prog, arg, NULL
    };

    run_program(ncf, argv);
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
