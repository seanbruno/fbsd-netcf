/*
 * ncftransform.c: transform between interface and augeas XML
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
#include "netcf.h"
#include "internal.h"

#include <stdio.h>
#include "read-file.h"

static void die(const char *format, ...) {
    va_list ap;

    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);

    exit(1);
}

int main(int argc, char **argv) {
    char *in_xml = NULL, *out_xml = NULL;
    size_t length;
    struct netcf *ncf = NULL;
    int r;

    if (argc != 3 || (STRNEQ(argv[1], "get") && STRNEQ(argv[1], "put")))
        die("Usage: ncftransform (put|get) FILE\n");

    in_xml = read_file(argv[2], &length);
    if (in_xml == NULL)
        die("Failed to read %s\n", argv[2]);

    r = ncf_init(&ncf, "/dev/null");
    if (r < 0)
        die("Failed to initialize netcf\n");

    if (STREQ(argv[1], "get")) {
        ncf_get_aug(ncf, in_xml, &out_xml);
    } else {
        ncf_put_aug(ncf, in_xml, &out_xml);
    }
    puts(out_xml);

    ncf_close(ncf);
}
/*
 * Local variables:
 *  indent-tabs-mode: nil
 *  c-indent-level: 4
 *  c-basic-offset: 4
 *  tab-width: 4
 * End:
 */
