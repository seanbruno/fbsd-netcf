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
#include "safe-alloc.h"

#include "internal.h"
#include "netcf.h"

/* Clear error code and details */
#define API_ENTRY(ncf)                          \
    do {                                        \
        (ncf)->errcode = NETCF_NOERROR;         \
        FREE((ncf)->errdetails);                \
    } while(0);

/* Human-readable error messages. This array is indexed by NETCF_ERRCODE_T */
static const char *const errmsgs[] = {
    "no error",                           /* NOERROR   */
    "internal error",                     /* EINTERNAL */
    "unspecified error",                  /* EOTHER    */
    "allocation failed",                  /* ENOMEM    */
    "XML parser failed",                  /* EXMLPARSER */
    "XML invalid",                        /* EXMLINVALID */
    "required entry missing",             /* ENOENT */
    "failed to execute external program"  /* EEXEC */
};

static void free_netcf(struct netcf *ncf) {
    if (ncf == NULL)
        return;

    assert(ncf->ref == 0);
    free(ncf->root);
    free(ncf);
}

void free_netcf_if(struct netcf_if *nif) {
    if (nif == NULL)
        return;

    assert(nif->ref == 0);
    unref(nif->ncf, netcf);
    free(nif->name);
    free(nif->mac);
    free(nif);
}

int ncf_init(struct netcf **ncf, const char *root) {
    *ncf = NULL;
    if (make_ref(*ncf) < 0)
        goto oom;
    if (root == NULL)
        root = "/";
    (*ncf)->root = strdup(root);
    if ((*ncf)->root == NULL)
        goto oom;
    (*ncf)->data_dir = getenv("NETCF_DATADIR");
    if ((*ncf)->data_dir == NULL)
        (*ncf)->data_dir = DATADIR "/netcf";
    return drv_init(*ncf);
 oom:
    ncf_close(*ncf);
    return -2;
}

void ncf_close(struct netcf *ncf) {
    API_ENTRY(ncf);

    drv_close(ncf);
    unref(ncf, netcf);
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
    const char *const if_up_argv[] = {
        "ifup", nif->name, NULL
    };

    /* I'm a bit concerned that this assumes nif (and nif->ncf) is non-NULL) */
    API_ENTRY(nif->ncf);
    return run_program(nif->ncf, if_up_argv);
}

/* Take it down */
int ncf_if_down(struct netcf_if *nif) {
    const char *const if_down_argv[] = {
        "ifdown", nif->name, NULL
    };

    /* I'm a bit concerned that this assumes nif (and nif->ncf) is non-NULL) */
    API_ENTRY(nif->ncf);
    return run_program(nif->ncf, if_down_argv);
}

/* Produce an XML description for the interface, in the same format that
 * NCF_DEFINE expects
 */
char *ncf_if_xml_desc(struct netcf_if *nif) {
    API_ENTRY(nif->ncf);
    return drv_xml_desc(nif);
}

/* Release any resources used by this NETCF_IF; the pointer is invalid
 * after this call
 */
void ncf_if_free(struct netcf_if *nif) {
    if (nif == NULL)
        return;

    API_ENTRY(nif->ncf);
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
 * Test interface
 */
int ncf_get_aug(struct netcf *ncf, const char *ncf_xml, char **aug_xml) {
    API_ENTRY(ncf);

    return drv_get_aug(ncf, ncf_xml, aug_xml);
}

int ncf_put_aug(struct netcf *ncf, const char *aug_xml, char **ncf_xml) {
    API_ENTRY(ncf);

    return drv_put_aug(ncf, aug_xml, ncf_xml);
}

/*
 * Internal helpers
 */

int run_program(struct netcf *ncf, const char *const *argv) {

    int command_return;
    char *argv_str = argv_to_string(argv);
    int ret = -1;

    ERR_COND_BAIL(argv_str == NULL, ncf, ENOMEM);

    /* BIG FIXME!!!  Before any general release, this *must* be
     * replaced with a call to a function similar to libVirt's
     * virRun(), and if there is an error returned, anything the
     * program produced on stderr or stdout should be placed in
     * ncf->errdetails.
     */
    command_return = system(argv_str);

    ERR_COND_BAIL(command_return != WEXITSTATUS (0), ncf, EEXEC);

    ret = 0;
error:
    FREE(argv_str);
    return ret;
}

/*
 * argv_to_string() is borrowed from libvirt's
 * src/util.c:virArgvToString()
 */
char *
argv_to_string(const char *const *argv) {
    int i;
    size_t len;
    char *ret, *p;

    for (len = 1, i = 0; argv[i]; i++)
        len += strlen(argv[i]) + 1;

    if (ALLOC_N(ret, len) < 0)
        return NULL;
    p = ret;

    for (i = 0; argv[i]; i++) {
        if (i != 0)
            *(p++) = ' ';

        strcpy(p, argv[i]);
        p += strlen(argv[i]);
    }

    *p = '\0';

    return ret;
}

void report_error(struct netcf *ncf, netcf_errcode_t errcode,
                  const char *format, ...) {
    va_list ap;

    va_start(ap, format);
    vreport_error(ncf, errcode, format, ap);
    va_end(ap);
}

void vreport_error(struct netcf *ncf, netcf_errcode_t errcode,
                   const char *format, va_list ap) {
    /* We only remember the first error */
    if (ncf->errcode != NETCF_NOERROR)
        return;
    assert(ncf->errdetails == NULL);

    ncf->errcode = errcode;
    if (format != NULL) {
        if (vasprintf(&(ncf->errdetails), format, ap) < 0)
            ncf->errdetails = NULL;
    }
}

/*
 * Local variables:
 *  indent-tabs-mode: nil
 *  c-indent-level: 4
 *  c-basic-offset: 4
 *  tab-width: 4
 * End:
 */
