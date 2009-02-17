/*
 * internal.h: importantdefinitions used through netcf
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

#ifndef INTERNAL_H_
#define INTERNAL_H_

#include <config.h>
#include "netcf.h"

#include <string.h>

/*
 * Macros for gcc's attributes
 */
#ifdef __GNUC__

#ifndef __GNUC_PREREQ
#define __GNUC_PREREQ(maj,min) 0
#endif

/**
 * ATTRIBUTE_UNUSED:
 *
 * Macro to flag conciously unused parameters to functions
 */
#ifndef ATTRIBUTE_UNUSED
#define ATTRIBUTE_UNUSED __attribute__((__unused__))
#endif

/**
 * ATTRIBUTE_FORMAT
 *
 * Macro used to check printf/scanf-like functions, if compiling
 * with gcc.
 */
#ifndef ATTRIBUTE_FORMAT
#define ATTRIBUTE_FORMAT(args...) __attribute__((__format__ (args)))
#endif

#ifndef ATTRIBUTE_PURE
#define ATTRIBUTE_PURE __attribute__((pure))
#endif

#ifndef ATTRIBUTE_RETURN_CHECK
#if __GNUC_PREREQ (3, 4)
#define ATTRIBUTE_RETURN_CHECK __attribute__((__warn_unused_result__))
#else
#define ATTRIBUTE_RETURN_CHECK
#endif
#endif

#else
#define ATTRIBUTE_UNUSED
#define ATTRIBUTE_FORMAT(...)
#define ATTRIBUTE_PURE
#define ATTRIBUTE_RETURN_CHECK
#endif                                   /* __GNUC__ */

/* This needs ATTRIBUTE_RETURN_CHECK */
#include "ref.h"

/*
 * various convenience macros
 */
#define ARRAY_CARDINALITY(array) (sizeof (array) / sizeof *(array))

#define MEMZERO(ptr, n) memset((ptr), 0, (n) * sizeof(*(ptr)));

/* String equality tests, suggested by Jim Meyering. */
#define STREQ(a,b) (strcmp((a),(b)) == 0)
#define STRCASEEQ(a,b) (strcasecmp((a),(b)) == 0)
#define STRCASEEQLEN(a,b,n) (strncasecmp((a),(b),(n)) == 0)
#define STRNEQ(a,b) (strcmp((a),(b)) != 0)
#define STRCASENEQ(a,b) (strcasecmp((a),(b)) != 0)
#define STREQLEN(a,b,n) (strncmp((a),(b),(n)) == 0)
#define STRNEQLEN(a,b,n) (strncmp((a),(b),(n)) != 0)

#define ERR_COND(cond, ncf, err) \
    if (cond) (ncf)->errcode = (NETCF_##err)
#define ERR_BAIL(ncf) if ((ncf)->errcode != NETCF_NOERROR) goto error;

#define ERR_COND_BAIL(cond, ncf, err)           \
    do {                                        \
    ERR_COND(cond, ncf, err);                   \
    ERR_BAIL(ncf);                              \
    } while(0)

/*
 * netcf structures and internal API's
 */
struct netcf {
    ref_t            ref;
    char            *root;                /* The filesystem root */
    netcf_errcode_t  errcode;
    char            *errdetails;          /* Error details */
    struct driver   *driver;              /* Driver specific data */
};

struct netcf_if {
    ref_t         ref;
    struct netcf *ncf;
    char         *path;
};

void free_netcf_if(struct netcf_if *nif);

/* The interface to the driver (backend). The appropriate driver is
 * selected at build time from the available drivers in drv_*
 */
int drv_init(struct netcf *netcf);
void drv_close(struct netcf *netcf);
int drv_num_of_interfaces(struct netcf *ncf);
int drv_list_interfaces(struct netcf *ncf, int maxnames, char **names);
int drv_list_interfaces_uuid_string(struct netcf *, int maxuuid, char **uuids);
struct netcf_if *drv_lookup_by_name(struct netcf *ncf, const char *name);

#endif


/*
 * Local variables:
 *  indent-tabs-mode: nil
 *  c-indent-level: 4
 *  c-basic-offset: 4
 *  tab-width: 4
 * End:
 */
