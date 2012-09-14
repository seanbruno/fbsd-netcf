/*
 * dutil_posix.c: *NIX utility functions for driver backends.
 *
 * Copyright (C) 2009-2012 Red Hat Inc.
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
#include <errno.h>

#include <dirent.h>
#include <sys/wait.h>
#include <signal.h>
#include <c-ctype.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include "safe-alloc.h"
#include "read-file.h"
#include "ref.h"
#include "list.h"
#include "netcf.h"
#include "dutil.h"
#include "dutil_posix.h"

/*
 * Executing external programs
 */

static int
exec_program(struct netcf *ncf,
             const char *const*argv,
             const char *commandline,
             pid_t *pid,
             int *outfd)
{
    sigset_t oldmask, newmask;
    struct sigaction sig_action;
    char errbuf[128];
    int pipeout[2] = {-1, -1};

    /* commandline is only used for error reporting */
    if (commandline == NULL)
        commandline = argv[0];

    /* create a pipe to receive stdout+stderr from child */
    if (outfd) {
        if (pipe(pipeout) < 0) {
            report_error(ncf, NETCF_EEXEC,
                         "failed to create pipe while forking for '%s': %s",
                         commandline, strerror_r(errno, errbuf, sizeof(errbuf)));
            goto error;
        }
        *outfd = pipeout[0];
    }

    /*
     * Need to block signals now, so that child process can safely
     * kill off caller's signal handlers without a race.
     */
    sigfillset(&newmask);
    if (pthread_sigmask(SIG_SETMASK, &newmask, &oldmask) != 0) {
        report_error(ncf, NETCF_EEXEC,
                     "failed to set signal mask while forking for '%s': %s",
                     commandline, strerror_r(errno, errbuf, sizeof(errbuf)));
        goto error;
    }

    *pid = fork();

    ERR_THROW(*pid < 0, ncf, EEXEC, "failed to fork for '%s': %s",
              commandline, strerror_r(errno, errbuf, sizeof(errbuf)));

    if (*pid) { /* parent */
        /* Restore our original signal mask now that the child is
           safely running */
        ERR_THROW(pthread_sigmask(SIG_SETMASK, &oldmask, NULL) != 0,
                  ncf, EEXEC,
                  "failed to restore signal mask while forking for '%s': %s",
                  commandline, strerror_r(errno, errbuf, sizeof(errbuf)));

        /* parent doesn't use write side of the pipe */
        if (pipeout[1] >= 0)
            close(pipeout[1]);

        return 0;
    }

    /* child */

    /* Clear out all signal handlers from parent so nothing unexpected
       can happen in our child once we unblock signals */

    sig_action.sa_handler = SIG_DFL;
    sig_action.sa_flags = 0;
    sigemptyset(&sig_action.sa_mask);

    int i;
    for (i = 1; i < NSIG; i++) {
        /* Only possible errors are EFAULT or EINVAL
           The former wont happen, the latter we
           expect, so no need to check return value */

        sigaction(i, &sig_action, NULL);
    }

    /* Unmask all signals in child, since we've no idea what the
       caller's done with their signal mask and don't want to
       propagate that to children */
    sigemptyset(&newmask);
    if (pthread_sigmask(SIG_SETMASK, &newmask, NULL) != 0) {
        /* return a unique code and let the parent log the error */
        _exit(EXIT_SIGMASK);
    }

    if (pipeout[1] >= 0) {
        /* direct stdout and stderr to the pipe */
        if (dup2(pipeout[1], fileno(stdout)) < 0
            || dup2(pipeout[1], fileno(stderr)) < 0) {
            /* return a unique code and let the parent log the error */
            _exit(EXIT_DUP2);
        }
    }
    /* child doesn't use the read side of the pipe */
    if (pipeout[0] >= 0)
        close(pipeout[0]);

    /* close all open file descriptors */
    int openmax = sysconf (_SC_OPEN_MAX);
    for (i = 3; i < openmax; i++)
        close(i);

    execvp(argv[0], (char **) argv);

    /* if execvp() returns, it has failed */
    /* return a unique code and let the parent log the error */
    _exit(errno == ENOENT ? EXIT_ENOENT : EXIT_CANNOT_INVOKE);

error:
    /* This is cleanup of parent process only - child
       should never jump here on error */
    if (pipeout[0] >= 0)
        close(pipeout[0]);
    if (pipeout[1] >= 0)
        close(pipeout[1]);
    if (outfd)
        *outfd = -1;
    return -1;
}

/**
 * Run a command without using the shell.
 *
 * return 0 if the command run and exited with 0 status; Otherwise
 * return -1
 *
 */
int run_program(struct netcf *ncf, const char *const *argv, char **output)
{

    pid_t childpid = -1;
    int exitstatus, waitret;
    char *argv_str;
    int ret = -1;
    char errbuf[128];
    char *outtext = NULL;
    int outfd = -1;
    FILE *outfile = NULL;
    size_t outlen;

    if (!output)
        output = &outtext;

    argv_str = argv_to_string(argv);
    ERR_NOMEM(argv_str == NULL, ncf);

    exec_program(ncf, argv, argv_str, &childpid, &outfd);
    ERR_BAIL(ncf);

    outfile = fdopen(outfd, "r");
    ERR_THROW(outfile == NULL, ncf, EEXEC,
              "Failed to create file stream for output while executing '%s': %s",
              argv_str, strerror_r(errno, errbuf, sizeof(errbuf)));

    *output = fread_file(outfile, &outlen);
    ERR_THROW(*output == NULL, ncf, EEXEC,
              "Error while reading output from execution of '%s': %s",
              argv_str, strerror_r(errno, errbuf, sizeof(errbuf)));

    /* finished with the stream. Close it so the child can exit. */
    fclose(outfile);
    outfile = NULL;

    while ((waitret = waitpid(childpid, &exitstatus, 0) == -1) &&
           errno == EINTR) {
        /* empty loop */
    }

    ERR_THROW(waitret == -1, ncf, EEXEC,
              "Failed waiting for completion of '%s': %s",
              argv_str, strerror_r(errno, errbuf, sizeof(errbuf)));
    ERR_THROW(!WIFEXITED(exitstatus) && WIFSIGNALED(exitstatus), ncf, EEXEC,
              "'%s' terminated by signal: %d",
              argv_str, WTERMSIG(exitstatus));
    ERR_THROW(!WIFEXITED(exitstatus), ncf, EEXEC,
              "'%s' terminated improperly", argv_str);
    ERR_THROW(WEXITSTATUS(exitstatus) == EXIT_ENOENT, ncf, EEXEC,
              "Running '%s' program not found", argv_str);
    ERR_THROW(WEXITSTATUS(exitstatus) == EXIT_CANNOT_INVOKE, ncf, EEXEC,
              "Running '%s' program located but not usable", argv_str);
    ERR_THROW(WEXITSTATUS(exitstatus) == EXIT_SIGMASK, ncf, EEXEC,
              "Running '%s' failed to reset child process signal mask",
              argv_str);
    ERR_THROW(WEXITSTATUS(exitstatus) == EXIT_DUP2, ncf, EEXEC,
              "Running '%s' failed to dup2 child process stdout/stderr",
              argv_str);
    ERR_THROW(WEXITSTATUS(exitstatus) == EXIT_INVALID_IN_THIS_STATE, ncf, EINVALIDOP,
              "Running '%s' operation is invalid in this state",
              argv_str);
    ERR_THROW(WEXITSTATUS(exitstatus) != 0, ncf, EEXEC,
              "Running '%s' failed with exit code %d: %s",
              argv_str, WEXITSTATUS(exitstatus), *output);
    ret = 0;

error:
    if (outfile)
        fclose(outfile);
    else if (outfd >= 0)
        close(outfd);
    FREE(outtext);
    FREE(argv_str);
    return ret;
}

/* Run the program PROG with the single argument ARG */
void run1(struct netcf *ncf, const char *prog, const char *arg) {
    const char *const argv[] = {
        prog, arg, NULL
    };

    run_program(ncf, argv, NULL);
}

/*
 * ioctl and netlink-related utilities
 */

int init_ioctl_fd(struct netcf *ncf) {
    int ioctl_fd;
    int flags;

    ioctl_fd = socket(AF_INET, SOCK_STREAM, 0);
    ERR_THROW(ioctl_fd < 0, ncf, EINTERNAL, "failed to open socket for interface ioctl");

    flags = fcntl(ioctl_fd, F_GETFD);
    ERR_THROW(flags < 0, ncf, EINTERNAL, "failed to get flags for ioctl socket");

    flags = fcntl(ioctl_fd, F_SETFD, flags | FD_CLOEXEC);
    ERR_THROW(flags < 0, ncf, EINTERNAL, "failed to set FD_CLOEXEC flag on ioctl socket");
    return ioctl_fd;

error:
    if (ioctl_fd >= 0)
        close(ioctl_fd);
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
/* vim: set ts=4 sw=4 et: */
