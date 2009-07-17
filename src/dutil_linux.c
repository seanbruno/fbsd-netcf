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

#include "safe-alloc.h"
#include "ref.h"
#include "list.h"
#include "netcf.h"
#include "dutil.h"
#include "dutil_linux.h"

/* Returns a list of all interfaces with MAC address INTF */
int aug_match_mac(struct netcf *ncf, const char *mac, char ***matches) {
    int r, nmatches;
    char *path;
    struct augeas *aug = get_augeas(ncf);

    r = xasprintf(&path,
            "/files/sys/class/net/*[address/content = '%s']", mac);
    ERR_COND_BAIL(r < 0, ncf, ENOMEM);

    r = aug_match(aug, path, matches);
    /* Messages for a aug_match-fail are handled outside this function */
    if (r < 0)
        goto error;

    nmatches = r;
    r = -1;
    for (int i = 0; i < nmatches; i++) {
        char *n = strrchr((*matches)[i], '/');
        ERR_THROW(n == NULL, ncf, EINTERNAL, "missing / in sysfs path");
        n += 1;

        /* Replace with freeable copy */
        n = strdup(n);
        ERR_COND_BAIL(n == NULL, ncf, ENOMEM);

        free((*matches)[i]);
        (*matches)[i] = n;
    }

    return nmatches;

 error:
    FREE(path);
    return r;
}

/* Get the MAC address of the interface INTF */
int aug_get_mac(struct netcf *ncf, const char *intf, const char **mac) {
    int r;
    char *path;
    struct augeas *aug = get_augeas(ncf);

    r = xasprintf(&path, "/files/sys/class/net/%s/address/content", intf);
    ERR_COND_BAIL(r < 0, ncf, ENOMEM);

    r = aug_get(aug, path, mac);
    /* Messages for a aug_match-fail are handled outside this function */

    /* fallthrough intentional */
 error:
    FREE(path);
    return r;
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
