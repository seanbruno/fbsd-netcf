/*
 * dutil_posix.h: *NIX utility functions for driver backends.
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
 */

#ifndef DUTIL_POSIX_H_
#define DUTIL_POSIX_H_

enum
{
    EXIT_DUP2=124,          /* dup2() of stdout/stderr in child failed */
    EXIT_SIGMASK=125,       /* failed to reset signal mask of child */
    EXIT_CANNOT_INVOKE=126, /* program located, but not usable. */
    EXIT_ENOENT=127,        /* could not find program to execute */

    /* any application-specific exit codes returned by the exec'ed
     * binary should be in the range 193-199 to avoid various
     *ambiguities (confusion with signals, truncation...
     */
    /* NB: the following code matches that in the netcf-transact script */
    EXIT_INVALID_IN_THIS_STATE=199, /* wrong state to perform this operation */
};

/* run an external program */
int run_program(struct netcf *ncf, const char *const *argv, char **output);
void run1(struct netcf *ncf, const char *prog, const char *arg);

/* Get a file descriptor to a ioctl socket */
int init_ioctl_fd(struct netcf *ncf);

#endif

/*
 * Local variables:
 *  indent-tabs-mode: nil
 *  c-indent-level: 4
 *  c-basic-offset: 4
 *  tab-width: 4
 * End:
 */
/* vim: set ts=4 sw=4 et: */
