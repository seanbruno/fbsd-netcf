/*
 * Copyright (c) 2012, Sean Bruno sbruno@freebsd.org
 * Copyright (c) 2012, Hiren Panchasara hiren.panchasara@gmail.com
 * All rights reserved.

 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the <organization> nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * dutil_freebsd.c: FreeBSD utility functions for driver backends.
 */

#include <config.h>
#include <internal.h>

#include "dutil.h"

struct driver {
    struct augeas     *augeas;
    xsltStylesheetPtr  put;
    xsltStylesheetPtr  get;
    int                ioctl_fd;
    struct nl_handle  *nl_sock;
    struct nl_cache   *link_cache;
    struct nl_cache   *addr_cache;
    unsigned int       load_augeas : 1;
    unsigned int       copy_augeas_xfm : 1;
    unsigned int       augeas_xfm_num_tables;
    const struct augeas_xfm_table **augeas_xfm_tables;
};

/* run an external program */
int run_program(struct netcf *ncf, const char *const *argv, char **output);
void run1(struct netcf *ncf, const char *prog, const char *arg);

/* Free matches from aug_match (or aug_submatch) */
void free_matches(int nint, char ***intf);

/* Check if the interface INTF is up using an ioctl call */
int if_is_active(struct netcf *ncf, const char *intf);

/* Get a file descriptor to a ioctl socket */
int init_ioctl_fd(struct netcf *ncf);
