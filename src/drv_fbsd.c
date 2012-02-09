/*
 * Copyright (c) 2012, Sean Bruno sbruno@freebsd.org
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

#include <config.h>
#include <internal.h>

#include <stdio.h>
#include <stdlib.h>
#include <spawn.h>
#include <stdbool.h>
#include <string.h>
#include <wctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <net/if.h>

#include "safe-alloc.h"
#include "ref.h"
#include "list.h"
#include "dutil.h"
#include "dutil_fbsd.h"

int drv_init(struct netcf *ncf) {

   if (ALLOC(ncf->driver) < 0)
		return -1;
   if (ALLOC(ncf->driver) < 0)
        return -1;

    ncf->driver->ioctl_fd = -1;

    /* open a socket for interface ioctls */
    ncf->driver->ioctl_fd = init_ioctl_fd(ncf);
    if (ncf->driver->ioctl_fd < 0) {
        printf("%s:Unable to open device\n", __func__);
        return -1;
    }

    return 0;

}


void drv_close(struct netcf *ncf) {

	if (ncf == NULL || ncf->driver == NULL)
    	return;

	FREE(ncf->driver);

}

void drv_entry (struct netcf *ncf ATTRIBUTE_UNUSED) {
}

static int list_interfaces(struct netcf *ncf ATTRIBUTE_UNUSED, char ***intf) {
    int nint = 0;
    *intf = calloc(1024, sizeof(char*));
    struct ifaddrs *ifap, *ifa;

    getifaddrs(&ifap);
    for (ifa = ifap; ifa != NULL; ifa = ifa->ifa_next) {
        if ((ifa->ifa_addr->sa_family == AF_LINK) &&
           ((ifa->ifa_flags & IFF_CANTCONFIG) == 0)) {
	        (*intf)[nint++] = strndup(ifa->ifa_name, strlen(ifa->ifa_name)+1);
	    }
    }
    freeifaddrs(ifap);

    return nint;
}

static int list_interface_ids(struct netcf *ncf,
                              int maxnames,
                              char **names, unsigned int flags ATTRIBUTE_UNUSED) {
    int nint = 0, nqualified = 0, result = 0;
    char **intf = NULL;

    nint = list_interfaces(ncf, &intf);
    ERR_BAIL(ncf);
    if (!names) {
        maxnames = nint;    /* if not returning list, ignore maxnames too */
    }

    for (result = 0; (result < nint) && (nqualified < maxnames); result++) {
            const char *name;
            int is_qualified = ((flags & (NETCF_IFACE_ACTIVE|NETCF_IFACE_INACTIVE))
                             == (NETCF_IFACE_ACTIVE|NETCF_IFACE_INACTIVE));

            name = intf[result];

            if (!is_qualified) {
                int is_active = if_is_active(ncf, name);
                if ((is_active && (flags & NETCF_IFACE_ACTIVE))
                    || ((!is_active) && (flags & NETCF_IFACE_INACTIVE))) {
                    is_qualified = 1;
                }
            }

            if (is_qualified) {
                if (!names) {
                    names = malloc(sizeof(char *));
                }
                if (names) {
                    names[nqualified] = strdup(name);
                    ERR_NOMEM(names[nqualified] == NULL, ncf);
                }
                nqualified++;
            }
    }
    free_matches(nint, &intf);
    return nqualified;
 error:
    free_matches(nint, &intf);
    return -1;

}

int drv_list_interfaces(struct netcf *ncf,
                        int maxnames, char **names,
                        unsigned int flags) {
    return list_interface_ids(ncf, maxnames, names, flags);
}


int drv_num_of_interfaces(struct netcf *ncf, unsigned int flags) {
    return (list_interface_ids(ncf, 0, NULL, flags));
}


struct netcf_if *drv_lookup_by_name(struct netcf *ncf, const char *name) {

    struct netcf_if *nif = NULL;
    char *name_dup = NULL;

    name_dup = strdup(name);
    ERR_NOMEM(name_dup == NULL, ncf);

    nif = make_netcf_if(ncf, name_dup);
    ERR_BAIL(ncf);
    goto done;

 error:
    unref(nif, netcf_if);
    FREE(name_dup);
 done:
    return nif;
}

const char *drv_mac_string(struct netcf_if *nif) {
    const char *ifcfgmacformat = "/sbin/ifconfig %s|grep ether|awk '{print $2}'";
    char cmdbuffer[64];
    char macaddr[32];
    FILE *cmd;

    sprintf (cmdbuffer, ifcfgmacformat, nif->name);
    cmd = popen(cmdbuffer, "r+"); // HACKERY
    while (fgets(macaddr, sizeof(macaddr)-1, cmd) != NULL);
    pclose(cmd);

    if (strlen(macaddr) < strlen("00:00:00:00:00:00"))
        nif->mac = NULL;
    else {
        nif->mac = strdup(macaddr);
        nif->mac[(strlen(nif->mac)-1)]='\0'; // strip out newline
    }
    
    return nif->mac;
}

int drv_if_down(struct netcf_if *nif) {
    const char *ifdownformat = "/sbin/ifconfig %s down";
    char cmdbuffer[64];

    sprintf(cmdbuffer, ifdownformat, nif->name);
    system (cmdbuffer);
    return 0;
}

int drv_if_up(struct netcf_if *nif) {
    const char *ifupformat = "/sbin/ifconfig %s up";
    char cmdbuffer[64];

    sprintf(cmdbuffer, ifupformat, nif->name);
    system (cmdbuffer);
    return 0;
}


struct netcf_if *drv_define(struct netcf *ncf, const char *xml_str ATTRIBUTE_UNUSED) {

    ERR_THROW(1 == 1, ncf, EOTHER, "not implemented on this platform");
error:
    return NULL;
}

int drv_undefine(struct netcf_if *nif) {
	int result = 0;

    ERR_THROW(1 == 1, nif->ncf, EOTHER, "not implemented on this platform");
error:
    return result;
}


char *drv_xml_desc(struct netcf_if *nif) {

    ERR_THROW(1 == 1, nif->ncf, EOTHER, "not implemented on this platform");
error:
    return NULL;
}

char *drv_xml_state(struct netcf_if *nif) {

    ERR_THROW(1 == 1, nif->ncf, EOTHER, "not implemented on this platform");
error:
    return NULL;
}

int drv_if_status(struct netcf_if *nif, unsigned int *flags ATTRIBUTE_UNUSED) {
    int is_active;

    ERR_THROW(flags == NULL, nif->ncf, EOTHER, "NULL pointer for flags in ncf_if_status");
    *flags = 0;
    is_active = if_is_active(nif->ncf, nif->name);
    if (is_active)
        *flags |= NETCF_IFACE_ACTIVE;
    else
        *flags |= NETCF_IFACE_INACTIVE;
    return 0;
error:
    return -1;
}

int drv_lookup_by_mac_string(struct netcf *ncf,
			     const char *mac ATTRIBUTE_UNUSED,
                             int maxifaces ATTRIBUTE_UNUSED,
			     struct netcf_if **ifaces ATTRIBUTE_UNUSED)
{
	int result = 0;

    ERR_THROW(1 == 1, ncf, EOTHER, "not implemented on this platform");
error:
    return result;
}

int
drv_change_begin(struct netcf *ncf, unsigned int flags ATTRIBUTE_UNUSED)
{
	int result = 0;

    ERR_THROW(1 == 1, ncf, EOTHER, "not implemented on this platform");
error:
    return result;
}

int
drv_change_rollback(struct netcf *ncf, unsigned int flags ATTRIBUTE_UNUSED)
{
	int result = 0;

    ERR_THROW(1 == 1, ncf, EOTHER, "not implemented on this platform");
error:
    return result;
}

int
drv_change_commit(struct netcf *ncf, unsigned int flags ATTRIBUTE_UNUSED)
{
	int result = 0;

    ERR_THROW(1 == 1, ncf, EOTHER, "not implemented on this platform");
error:
    return result;
}

/*
 * Test interface
 */
static int drv_get_aug(struct netcf *ncf, const char *ncf_xml, char **aug_xml) {
    xmlDocPtr ncf_doc = NULL, aug_doc = NULL;
    int result = -1;

    ncf_doc = parse_xml(ncf, ncf_xml);
    ERR_BAIL(ncf);

    rng_validate(ncf, ncf_doc);
    ERR_BAIL(ncf);

    *aug_xml = apply_stylesheet_to_string(ncf, ncf->driver->get, ncf_doc);
    ERR_BAIL(ncf);

    /* fallthrough intentional */
    result = 0;
 error:
    xmlFreeDoc(ncf_doc);
    xmlFreeDoc(aug_doc);
    return result;
}

/* Transform the Augeas XML AUG_XML into interface XML NCF_XML */
static int drv_put_aug(struct netcf *ncf, const char *aug_xml, char **ncf_xml) {
    xmlDocPtr ncf_doc = NULL, aug_doc = NULL;
    int result = -1;

    aug_doc = parse_xml(ncf, aug_xml);
    ERR_BAIL(ncf);

    *ncf_xml = apply_stylesheet_to_string(ncf, ncf->driver->put, aug_doc);
    ERR_BAIL(ncf);

    /* fallthrough intentional */
    result = 0;
 error:
    xmlFreeDoc(ncf_doc);
    xmlFreeDoc(aug_doc);
    return result;
}

/*
 * Test interface
 */
int ncf_get_aug(struct netcf *ncf, const char *ncf_xml, char **aug_xml) {
    API_ENTRY(ncf);

    return drv_get_aug(ncf, ncf_xml, aug_xml);
}

int ncf_put_aug(struct netcf *ncf, const char *aug_xml, char **ncf_xml) {
    API_ENTRY(ncf);

    return drv_put_aug(ncf, aug_xml, ncf_xml);
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
