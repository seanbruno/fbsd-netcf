/*
 * netcf.c: the public interface for netcf
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

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <errno.h>
#include "safe-alloc.h"

#include "internal.h"
#include "netcf.h"
#include "dutil.h"

/* Human-readable error messages. This array is indexed by NETCF_ERRCODE_T */
static const char *const errmsgs[] = {
    "no error",                           /* NOERROR   */
    "internal error",                     /* EINTERNAL */
    "unspecified error",                  /* EOTHER    */
    "allocation failed",                  /* ENOMEM    */
    "XML parser failed",                  /* EXMLPARSER */
    "XML invalid",                        /* EXMLINVALID */
    "required entry missing",             /* ENOENT */
    "failed to execute external program", /* EEXEC */
    "instance still in use",              /* EINUSE */
    "XSLT transformation failed",         /* EXSLTFAILED */
    "File operation failed",              /* EFILE */
    "ioctl operation failed",             /* EIOCTL */
    "NETLINK socket operation failed",    /* ENETLINK */
    "Operation invalid in this state"     /* EINVALIDOP */
};

int ncf_init(struct netcf **ncf, const char *root) {
    *ncf = NULL;
    if (make_ref(*ncf) < 0)
        goto error;
    if (root == NULL)
        root = "/";
    if (root[strlen(root)-1] == '/') {
        (*ncf)->root = strdup(root);
    } else {
        if (xasprintf(&(*ncf)->root, "%s/", root) < 0)
            goto error;
    }
    if ((*ncf)->root == NULL)
        goto error;
    (*ncf)->data_dir = getenv("NETCF_DATADIR");
    if ((*ncf)->data_dir == NULL)
        (*ncf)->data_dir = DATADIR "/netcf";
    (*ncf)->debug = getenv("NETCF_DEBUG") != NULL;
    (*ncf)->rng = rng_parse(*ncf, "interface.rng");
    ERR_BAIL(*ncf);
    return drv_init(*ncf);
error:
    ncf_close(*ncf);
    *ncf = NULL;
    return -2;
}

int ncf_close(struct netcf *ncf) {
    if (ncf == NULL)
        return 0;

    API_ENTRY(ncf);

    ERR_COND_BAIL(ncf->ref > 1, ncf, EINUSE);

    drv_close(ncf);
    xmlRelaxNGFree(ncf->rng);
    unref(ncf, netcf);
    return 0;
 error:
    return -1;
}

/* Number of known interfaces and list of them.
 * For listing we identify the interfaces by UUID, since we don't want
 * to assume that each interface has a (device) name or a hwaddr.
 *
 * Maybe we should just list them as STRUCT NETCF_IF *
 */
int ncf_num_of_interfaces(struct netcf *ncf, unsigned int flags) {
    API_ENTRY(ncf);
    return drv_num_of_interfaces(ncf, flags);
}

int ncf_list_interfaces(struct netcf *ncf, int maxnames, char **names, unsigned int flags) {
    int result;

    API_ENTRY(ncf);
    MEMZERO(names, maxnames);
    result = drv_list_interfaces(ncf, maxnames, names, flags);
    if (result < 0)
        for (int i=0; i < maxnames; i++)
            FREE(names[i]);
    return result;
}

struct netcf_if * ncf_lookup_by_name(struct netcf *ncf, const char *name) {
    API_ENTRY(ncf);
    return drv_lookup_by_name(ncf, name);
}

int
ncf_lookup_by_mac_string(struct netcf *ncf, const char *mac,
                         int maxifaces, struct netcf_if **ifaces) {
    API_ENTRY(ncf);
    return drv_lookup_by_mac_string(ncf, mac, maxifaces, ifaces);
}

/*
 * Define/start/stop/undefine interfaces
 */

/* Define a new interface */
struct netcf_if *
ncf_define(struct netcf *ncf, const char *xml) {
    API_ENTRY(ncf);
    return drv_define(ncf, xml);
}

const char *ncf_if_name(struct netcf_if *nif) {
    API_ENTRY(nif->ncf);
    return nif->name;
}

const char *ncf_if_mac_string(struct netcf_if *nif) {
    API_ENTRY(nif->ncf);
    return drv_mac_string(nif);
}

/* Delete the definition */
int ncf_if_undefine(struct netcf_if *nif) {
    API_ENTRY(nif->ncf);
    return drv_undefine(nif);
}

/* Bring the interface up */
int ncf_if_up(struct netcf_if *nif) {
    /* I'm a bit concerned that this assumes nif (and nif->ncf) is non-NULL) */
    API_ENTRY(nif->ncf);
    return drv_if_up(nif);
}

/* Take it down */
int ncf_if_down(struct netcf_if *nif) {
    /* I'm a bit concerned that this assumes nif (and nif->ncf) is non-NULL) */
    API_ENTRY(nif->ncf);
    return drv_if_down(nif);
}

/* Produce an XML description for the interface, in the same format that
 * NCF_DEFINE expects
 */
char *ncf_if_xml_desc(struct netcf_if *nif) {
    API_ENTRY(nif->ncf);
    return drv_xml_desc(nif);
}

/* Produce an XML description of the current live state of the
 * interface, in the same format that NCF_DEFINE expects, but
 * potentially with extra info not contained in the static config (ie
 * the current IP address of an interface that uses DHCP)
 */
char *ncf_if_xml_state(struct netcf_if *nif) {
    API_ENTRY(nif->ncf);
    return drv_xml_state(nif);
}

/* Report various status info about the interface as bits in
 * "flags". Returns 0 on success, -1 on failure
 */
int ncf_if_status(struct netcf_if *nif, unsigned int *flags) {
    API_ENTRY(nif->ncf);
    return drv_if_status(nif, flags);
}

int
ncf_change_begin(struct netcf *ncf, unsigned int flags)
{
    API_ENTRY(ncf);
    return drv_change_begin(ncf, flags);
}

int
ncf_change_rollback(struct netcf *ncf, unsigned int flags)
{
    API_ENTRY(ncf);
    return drv_change_rollback(ncf, flags);
}

int
ncf_change_commit(struct netcf *ncf, unsigned int flags)
{
    API_ENTRY(ncf);
    return drv_change_commit(ncf, flags);
}

/* Release any resources used by this NETCF_IF; the pointer is invalid
 * after this call
 */
void ncf_if_free(struct netcf_if *nif) {
    if (nif == NULL)
        return;

    unref(nif, netcf_if);
}

int ncf_error(struct netcf *ncf, const char **errmsg, const char **details) {
    netcf_errcode_t errcode = ncf->errcode;

    if (ncf->errcode >= ARRAY_CARDINALITY(errmsgs))
        errcode = NETCF_EINTERNAL;
    if (errmsg)
        *errmsg = errmsgs[errcode];
    if (details)
        *details = ncf->errdetails;
    return errcode;
}

/*
 * Local variables:
 *  indent-tabs-mode: nil
 *  c-indent-level: 4
 *  c-basic-offset: 4
 *  tab-width: 4
 * End:
 */
