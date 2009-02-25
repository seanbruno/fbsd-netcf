/*
 * xslt_ext.c: XSLT extension functions needed by the stylesheets
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
#include "internal.h"

#include <errno.h>
#include <arpa/inet.h>

#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>
#include <libxslt/xsltutils.h>
#include <libxslt/extensions.h>

// FIXME: Find a better URI for this
#define XSLT_EXT_IPCALC_NS                                  \
    (BAD_CAST "http://redhat.com/xslt/netcf/ipcalc/1.0")
#define XSLT_EXT_BOND_NS                                \
    (BAD_CAST "http://redhat.com/xslt/netcf/bond/1.0")

/* Given an IP address with prefix like "192.168.0.24/24", compute the
 * netmask "255.255.255.0"
 */
static void ipcalc_netmask(xmlXPathParserContextPtr ctxt, int nargs) {
    xmlChar *ip_str;
    unsigned long prefix = 0;
    struct in_addr ip;
    int r;

    if (nargs != 1) {
        xmlXPathSetArityError(ctxt);
        goto error;
    }

    ip_str = xmlStrdup(xmlXPathPopString(ctxt));
    if (ip_str == NULL) {
        xsltTransformError(xsltXPathGetTransformContext(ctxt), NULL, NULL,
                    "ipcalc:netmask: internal error: allocation failed");
        goto error;
    }

    if (xmlStrchr(ip_str, '/') != NULL) {
        xmlChar *prefix_str = (xmlChar *) xmlStrchr(ip_str, '/');
        char *end;
        errno = 0;
        *prefix_str = '\0';
        prefix_str += 1;

        // FIXME: the (char *) cast is wrong
        prefix = strtoul((char *) prefix_str, &end, 10);
        if (errno || end == (char *) prefix_str || prefix > 32) {
            xsltTransformError(xsltXPathGetTransformContext(ctxt), NULL, NULL,
                               "ipcalc:netmask: illegal prefix '%s'",
                               prefix_str);
            goto error;
        }
    }

    r = inet_aton((char *) ip_str, &ip);
    if (r < 0) {
        xsltTransformError(xsltXPathGetTransformContext(ctxt), NULL, NULL,
                           "ipcalc:netmask: illegal address '%s'",
                           ip_str);
        goto error;
    }

    struct in_addr netmask;
    xmlChar netmask_str[16];
    if (prefix > 0) {
        netmask.s_addr = htonl(~((1 << (32 - prefix)) - 1));
    } else {
        if (((ntohl(ip.s_addr) & 0xFF000000) >> 24) <= 127)
            netmask.s_addr = htonl(0xFF000000);
        else if (((ntohl(ip.s_addr) & 0xFF000000) >> 24) <= 191)
            netmask.s_addr = htonl(0xFFFF0000);
        else
            netmask.s_addr = htonl(0xFFFFFF00);
    }

    if (! inet_ntop(AF_INET, &netmask,
                    (char *) netmask_str, sizeof(netmask_str) - 1)) {
        xsltTransformError(xsltXPathGetTransformContext(ctxt), NULL, NULL,
                    "ipcalc:netmask: internal error: inet_ntop failed");
        goto error;
    }
    netmask_str[sizeof(netmask_str)-1] = '\0';
    xmlXPathReturnString(ctxt, xmlStrdup(netmask_str));
 error:
    xmlFree(ip_str);
    return;
}

/* Given an IP address with prefix like "192.168.0.24/24", return the address
 */
static void ipcalc_address(xmlXPathParserContextPtr ctxt, int nargs) {
    xmlChar *str;

    if (nargs != 1) {
        xmlXPathSetArityError(ctxt);
        goto error;
    }

    str = xmlStrdup(xmlXPathPopString(ctxt));
    if (str == NULL) {
        xsltTransformError(xsltXPathGetTransformContext(ctxt), NULL, NULL,
                    "ipcalc:address: internal error: allocation failed");
        goto error;
    }

    if (xmlStrchr(str, '/') != NULL) {
        xmlChar *prefix_str = (xmlChar *) xmlStrchr(str, '/');
        *prefix_str = '\0';
    }

    xmlXPathReturnString(ctxt, str);
 error:
    return;
}

/* Given a netmask like "255.255.0.0" convert it into a prefix, e.g. "/16".
 * If the netmask is the empty string, return the empty string.
 */
static void ipcalc_prefix(xmlXPathParserContextPtr ctxt, int nargs) {
    xmlChar *netmask_str;
    char *prefix_str = NULL;
    struct in_addr netmask;
    unsigned int prefix = 8*sizeof(netmask.s_addr);
    int r;

    if (nargs != 1) {
        xmlXPathSetArityError(ctxt);
        goto error;
    }

    netmask_str = xmlXPathPopString(ctxt);
    if (xmlStrlen(netmask_str) == 0) {
        xmlXPathReturnEmptyString(ctxt);
        goto error;
    }

    r = inet_aton((char *) netmask_str, &netmask);
    if (r < 0) {
        xsltTransformError(xsltXPathGetTransformContext(ctxt), NULL, NULL,
                           "ipcalc:prefix: illegal netmask '%s'",
                           netmask_str);
        goto error;
    }

    for (int i = 0; i < 8*sizeof(netmask.s_addr); i++) {
        if (!(ntohl(netmask.s_addr) & ((2 << i) - 1)))
            prefix--;
    }

    if (asprintf(&prefix_str, "/%d", prefix) < 0) {
        prefix_str = NULL;
        goto error;
    }

    xmlXPathReturnString(ctxt, BAD_CAST prefix_str);
    return;
 error:
    free(prefix_str);
    return;
}

/* Given BONDING_OPTS, fish out the 'mode=FOO' token and return 'FOO'
 */
static void bond_option(xmlXPathParserContextPtr ctxt, int nargs) {
    xmlChar *bond_opts;
    xmlChar *name;
    const xmlChar *val, *val_end;

    if (nargs != 2) {
        xmlXPathSetArityError(ctxt);
        return;
    }

    name = xmlXPathPopString(ctxt);
    bond_opts = xmlXPathPopString(ctxt);
    val = xmlStrstr(bond_opts, name);
    if (val == NULL) {
        xmlXPathReturnEmptyString(ctxt);
        return;
    }
    val += xmlStrlen(name);
    if (*val != '=') {
        // FIXME: We should really go look for the next occurrence of name
        xmlXPathReturnEmptyString(ctxt);
        return;
    }
    val += 1;
    for (val_end = val;
         *val_end != '\0' && xmlStrchr(BAD_CAST " \t'\"", *val_end) == NULL;
         val_end++);

    int len = xmlStrlen(val) - xmlStrlen(val_end);
    xmlXPathReturnString(ctxt, xmlStrndup(val, len));
}

static void *xslt_ipcalc_init(ATTRIBUTE_UNUSED xsltTransformContextPtr ctxt,
                              ATTRIBUTE_UNUSED const xmlChar *URI) {
    xsltRegisterExtModuleFunction(BAD_CAST "netmask", XSLT_EXT_IPCALC_NS,
                                  ipcalc_netmask);
    xsltRegisterExtModuleFunction(BAD_CAST "address", XSLT_EXT_IPCALC_NS,
                                  ipcalc_address);
    xsltRegisterExtModuleFunction(BAD_CAST "prefix", XSLT_EXT_IPCALC_NS,
                                  ipcalc_prefix);
    return NULL;
}

static void *xslt_bond_init(ATTRIBUTE_UNUSED xsltTransformContextPtr ctxt,
                            ATTRIBUTE_UNUSED const xmlChar *URI) {
    xsltRegisterExtModuleFunction(BAD_CAST "option", XSLT_EXT_BOND_NS,
                                  bond_option);
    return NULL;
}

int xslt_ext_register(void) {
    int r = 0;
    r = xsltRegisterExtModule(XSLT_EXT_IPCALC_NS, xslt_ipcalc_init, NULL);
    if (r < 0) return r;
    r = xsltRegisterExtModule(XSLT_EXT_BOND_NS, xslt_bond_init, NULL);
    return r;
}

void xslt_ext_unregister(void) {
    xsltUnregisterExtModule	(XSLT_EXT_IPCALC_NS);
}

/*
 * Local variables:
 *  indent-tabs-mode: nil
 *  c-indent-level: 4
 *  c-basic-offset: 4
 *  tab-width: 4
 * End:
 */
