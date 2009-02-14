/*
 * drv_initscripts.c: the initscripts backend for netcf
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
#include "safe-alloc.h"

struct driver {
    struct augeas *augeas;
};

/* Entries in a ifcfg file that tell us that the interface
 * is not a toplevel interface
 */
static const char *const subif_paths[] = {
    "MASTER", "BRIDGE"
};

/* Like asprintf, but set *STRP to NULL on error */
static int xasprintf(char **strp, const char *format, ...) {
  va_list args;
  int result;

  va_start (args, format);
  result = vasprintf (strp, format, args);
  va_end (args);
  if (result < 0)
      *strp = NULL;
  return result;
}

static struct augeas *get_augeas(struct netcf *ncf) {
    if (ncf->driver->augeas == NULL)
        ncf->driver->augeas = aug_init(ncf->root, NULL, AUG_NONE);
    ERR_COND(ncf->driver->augeas == NULL, ncf, EOTHER);
    return ncf->driver->augeas;
}

static int aug_submatch(struct netcf *ncf, const char *p1,
                        const char *p2, char ***matches) {
    struct augeas *aug = get_augeas(ncf);
    char *path = NULL;
    int r;

    r = xasprintf(&path, "%s/%s", p1, p2);
    ERR_COND_BAIL(r < 0, ncf, EOTHER);

    r = aug_match(aug, path, matches);
    ERR_COND_BAIL(r < 0, ncf, EOTHER);

    free(path);
    return r;
 error:
    free(path);
    return -1;
}

int drv_init(struct netcf *ncf) {
    if (ALLOC(ncf->driver) < 0)
        return -1;
    return 0;
}

void drv_close(struct netcf *ncf) {
    FREE(ncf->driver);
}

int drv_num_of_interfaces(struct netcf *ncf) {
    struct augeas *aug = NULL;
    int nint = 0, result = 0;
    char **intf = NULL;

    aug = get_augeas(ncf);
    ERR_BAIL(ncf);

    /* Look in augeas for all interfaces */
    nint = aug_match(aug, "/files/etc/sysconfig/network-scripts/*", &intf);
    ERR_COND_BAIL(nint < 0, ncf, EOTHER);
    result = nint;

    /* Filter out the interfaces that are slaves/subordinate */
    for (int i = 0; i < nint; i++) {
        for (int s = 0; s < ARRAY_CARDINALITY(subif_paths); s++) {
            int r;
            r = aug_submatch(ncf, intf[i], subif_paths[s], NULL);
            ERR_BAIL(ncf);
            if (r > 0) {
                result--;
                break;
            }
        }
    }

    return result;
 error:

    return -1;
}

/*
 * Local variables:
 *  indent-tabs-mode: nil
 *  c-indent-level: 4
 *  c-basic-offset: 4
 *  tab-width: 4
 * End:
 */
