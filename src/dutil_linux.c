/*
 * dutil_linux.c: Linux utility functions for driver backends.
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
#include <c-ctype.h>
#include <errno.h>

#include "safe-alloc.h"
#include "ref.h"
#include "list.h"
#include "netcf.h"
#include "dutil.h"
#include "dutil_linux.h"

/* Returns a list of all interfaces with MAC address MAC */
int aug_match_mac(struct netcf *ncf, const char *mac, char ***matches) {
    int nmatches;
    char *path = NULL, *mac_lower = NULL;

    mac_lower = strdup(mac);
    ERR_NOMEM(mac_lower == NULL, ncf);
    for (char *s = mac_lower; *s != '\0'; s++)
        *s = c_tolower(*s);

    nmatches = aug_fmt_match(ncf, matches,
            "/files/sys/class/net/*[address/content = '%s']", mac_lower);
    ERR_BAIL(ncf);

    for (int i = 0; i < nmatches; i++) {
        char *n = strrchr((*matches)[i], '/');
        ERR_THROW(n == NULL, ncf, EINTERNAL, "missing / in sysfs path");
        n += 1;

        /* Replace with freeable copy */
        n = strdup(n);
        ERR_NOMEM(n == NULL, ncf);

        free((*matches)[i]);
        (*matches)[i] = n;
    }

    return nmatches;

 error:
    FREE(mac_lower);
    FREE(path);
    return -1;
}

/* Get the MAC address of the interface INTF */
int aug_get_mac(struct netcf *ncf, const char *intf, const char **mac) {
    int r;
    char *path;
    struct augeas *aug = get_augeas(ncf);

    r = xasprintf(&path, "/files/sys/class/net/%s/address/content", intf);
    ERR_NOMEM(r < 0, ncf);

    r = aug_get(aug, path, mac);
    /* Messages for a aug_match-fail are handled outside this function */

    /* fallthrough intentional */
 error:
    FREE(path);
    return r;
}

/* Add an 'alias NAME bonding' to an appropriate file in /etc/modprobe.d,
 * if none exists yet. If we need to create a new one, it goes into the
 * file netcf.conf.
 */
void modprobed_alias_bond(struct netcf *ncf, const char *name) {
    char *path = NULL;
    struct augeas *aug = get_augeas(ncf);
    int r, nmatches;

    nmatches = aug_fmt_match(ncf, NULL,
                             "/files/etc/modprobe.d/*/alias[ . = '%s']",
                             name);
    ERR_BAIL(ncf);

    if (nmatches == 0) {
        /* Add a new alias node; this probably deserves better API support
           in Augeas, it's too convoluted */
        r = xasprintf(&path, "/files/etc/modprobe.d/netcf.conf/alias[last()]");
        ERR_NOMEM(r < 0, ncf);
        nmatches = aug_match(aug, path, NULL);
        if (nmatches > 0) {
            r = aug_insert(aug, path, "alias", 0);
            ERR_COND_BAIL(r < 0, ncf, EOTHER);
        }
        r = aug_set(aug, path, name);
        FREE(path);
    }

    r = xasprintf(&path,
                  "/files/etc/modprobe.d/*/alias[ . = '%s']/modulename",
                  name);
    ERR_NOMEM(r < 0, ncf);

    r = aug_set(aug, path, "bonding");
    ERR_COND_BAIL(r < 0, ncf, EOTHER);

 error:
    FREE(path);
}

/* Remove the alias for NAME to the bonding module */
void modprobed_unalias_bond(struct netcf *ncf, const char *name) {
    char *path = NULL;
    struct augeas *aug = get_augeas(ncf);
    int r;

    r = xasprintf(&path,
         "/files/etc/modprobe.d/*/alias[ . = '%s'][modulename = 'bonding']",
                  name);
    ERR_NOMEM(r < 0, ncf);

    r = aug_rm(aug, path);
    ERR_COND_BAIL(r < 0, ncf, EOTHER);
 error:
    FREE(path);
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
