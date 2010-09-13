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
