/*
 * dutil_linux.h: Linux utility functions for driver backends.
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

#ifndef DUTIL_LINUX_H_
#define DUTIL_LINUX_H_

#include <netlink/netlink.h>

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

/* Add a table of transformations that the next GET_AUGEAS should run */
int add_augeas_xfm_table(struct netcf *ncf,
                         const struct augeas_xfm_table *table);

/* Remove a table of transformations that the next GET_AUGEAS should run */
int remove_augeas_xfm_table(struct netcf *ncf,
                            const struct augeas_xfm_table *table);

/* Get or create the augeas instance from NCF */
struct augeas *get_augeas(struct netcf *ncf);

/* Define a node inside the augeas tree */
ATTRIBUTE_FORMAT(printf, 4, 5)
int defnode(struct netcf *ncf, const char *name, const char *value,
                   const char *format, ...);

/* Format a path by doing a printf of FMT and the var args, then call
   AUG_MATCH on that path. Sets NCF->ERRCODE on error */
ATTRIBUTE_FORMAT(printf, 3, 4)
int aug_fmt_match(struct netcf *ncf, char ***matches, const char *fmt, ...);

/* Free matches from aug_match (or aug_submatch) */
void free_matches(int nint, char ***intf);

/* Returns a list of all interfaces with MAC address MAC */
int aug_match_mac(struct netcf *ncf, const char *mac, char ***matches);

/* Get the MAC address of the interface NAME
 *
 * Returns 1 if the MAC for NAME was found, 0 if none was found, and a
 * negative value on error. MAC is only set to a non-NULL value when the
 * MAC for NAME was found; in all other cases it is set to NULL. The
 * returned MAC address will be all lowercase.
 */
int aug_get_mac(struct netcf *ncf, const char *name, const char **mac);

/* Add an 'alias NAME bonding' to an appropriate file in /etc/modprobe.d,
 * if none exists yet. If we need to create a new one, it goes into the
 * file netcf.conf.
 */
void modprobed_alias_bond(struct netcf *ncf, const char *name);

/* Remove an 'alias NAME bonding' as created by modprobed_alias_bond */
void modprobed_unalias_bond(struct netcf *ncf, const char *name);

/* Get a file descriptor to a ioctl socket */
int init_ioctl_fd(struct netcf *ncf);

/* setup the netlink socket */
int netlink_init(struct netcf *ncf);

/*shutdown the netlink socket and release its resources */
int netlink_close(struct netcf *ncf);

/* Check if the interface INTF is up using an ioctl call */
int if_is_active(struct netcf *ncf, const char *intf);

/* Interface types recognized by netcf. */
typedef enum {
    NETCF_IFACE_TYPE_NONE = 0,  /* not yet determined */
    NETCF_IFACE_TYPE_ETHERNET,  /* any physical device is "ethernet" */
    NETCF_IFACE_TYPE_BOND,
    NETCF_IFACE_TYPE_BRIDGE,
    NETCF_IFACE_TYPE_VLAN,
} netcf_if_type_t;

/* Return the type of the interface.
 */
netcf_if_type_t if_type(struct netcf *ncf, const char *intf);

/* Given a netcf_if_type_t enum value, return a const char *representation
 * This pointer has an indefinite life, and shouldn't be / can't be free'd.
 */
const char *if_type_str(netcf_if_type_t type);

/* Add the state of the interface (currently all addresses + netmasks)
 * to its xml document.
 */
void add_state_to_xml_doc(struct netcf_if *nif, xmlDocPtr doc);

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
