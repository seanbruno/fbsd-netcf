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

#include <stdlib.h>
#include <string.h>
#include "safe-alloc.h"

#include "internal.h"
#include "netcf.h"

/* Clear error code and details */
#define ERR_RESET(ncf)                          \
    do {                                        \
        (ncf)->errcode = NETCF_NOERROR;         \
        free((ncf)->errdetails);                \
    } while(0);

/* Human-readable error messages. This array is indexed by NETCF_ERRCODE_T */
static const char *const errmsgs[] = {
    "no error",                           /* NOERROR   */
    "internal error",                     /* EINTERNAL */
    "unspecified error",                  /* EOTHER    */
    "allocation failed"                   /* ENOMEM    */
};

int ncf_init(struct netcf **ncf, const char *root) {
    *ncf = NULL;
    if (ALLOC(*ncf) < 0)
        goto oom;
    (*ncf)->root = strdup(root);
    if ((*ncf)->root == NULL)
        goto oom;
    return drv_init(*ncf);
 oom:
    ncf_close(*ncf);
    return -2;
}

void ncf_close(struct netcf *ncf) {
    if (ncf == NULL)
        return;

    ERR_RESET(ncf);
    drv_close(ncf);
    free(ncf->root);
    free(ncf);
}

/* Number of known interfaces and list of them.
 * For listing we identify the interfaces by UUID, since we don't want
 * to assume that each interface has a (device) name or a hwaddr.
 *
 * Maybe we should just list them as STRUCT NETCF_IF *
 */
int ncf_num_of_interfaces(struct netcf *ncf) {
    ERR_RESET(ncf);
    return drv_num_of_interfaces(ncf);
}

#if 0
int ncf_list_interfaces(struct netcf *ncf, int maxuuid, char **uuids) {
    ERR_RESET(ncf);
    return -1;
}

/* Look up interfaces by UUID, name and hwaddr (MAC-48) */
struct netcf_if *
ncf_lookup_by_uuid_string(struct netcf *ncf, const char *uuid) {
    ERR_RESET(ncf);
    return NULL;
}

struct netcf_if * ncf_lookup_by_name(struct netcf *, const char *name) {
    ERR_RESET(ncf);
    return NULL;
}

/* MAC is 48 bit (6 byte) array */
struct netcf_if *
ncf_lookup_by_mac(struct netcf *, const unsigned char *mac) {
    ERR_RESET(ncf);
    return NULL;
}

/*
 * Define/start/stop/undefine interfaces
 */

/* Define a new interface */
struct netcf_if *
ncf_define(struct netcf *, const char *xml) {
    ERR_RESET(ncf);
    return NULL;
}

/* Bring the interface up */
int ncf_up(struct netcf_if *) {
    ERR_RESET(ncf);
    return -1;
}

/* Take it down */
int ncf_down(struct netcf_if *) {
    ERR_RESET(ncf);
    return -1;
}

/* Delete the definition */
int ncf_undefine(struct netcf_if *) {
    ERR_RESET(ncf);
    return -1;
}

/* Produce an XML description for the interface, in the same format that
 * NCF_DEFINE expects
 */
char *ncf_xml_desc(struct netcf_if *) {
    ERR_RESET(ncf);
    return NULL;
}

/* Release any resources used by this NETCF_IF; the pointer is invalid
 * after this call
 */
void ncf_free(struct netcf_if *) {
    ERR_RESET(ncf);
    return;
}
#endif

int ncf_error(struct netcf *ncf, const char **errmsg, const char **details) {
    netcf_errcode_t errcode = ncf->errcode;

    if (ncf->errcode >= ARRAY_CARDINALITY(errmsgs))
        errcode = NETCF_EINTERNAL;
    *errmsg = errmsgs[errcode];
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
