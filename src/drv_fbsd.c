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

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <spawn.h>
#include <stdbool.h>
#include <string.h>
#include <wctype.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/ethernet.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <net/if_var.h>
#include <netinet/in_var.h>

#include "safe-alloc.h"
#include "ref.h"
#include "list.h"
#include "dutil.h"
#include "dutil_fbsd.h"

/*
 * Liberally ripped off from sbin/ifconfig/ifconfig.c
 */
static int
setifflags(const char *vname, int value, int ioctl_fd)
{
    struct ifreq        my_ifr;
    int flags;

    memset(&my_ifr, 0, sizeof(my_ifr));
    (void) strlcpy(my_ifr.ifr_name, vname, sizeof(my_ifr.ifr_name));

    if (ioctl(ioctl_fd, SIOCGIFFLAGS, (caddr_t)&my_ifr) < 0) {
        printf("ioctl (SIOCGIFFLAGS)");
        return(-1);
    }
    flags = (my_ifr.ifr_flags & 0xffff) | (my_ifr.ifr_flagshigh << 16);

    if (value < 0) {
        value = -value;
        flags &= ~value;
    } else
        flags |= value;

    my_ifr.ifr_flags = flags & 0xffff;
    my_ifr.ifr_flagshigh = flags >> 16;
    if (ioctl(ioctl_fd, SIOCSIFFLAGS, (caddr_t)&my_ifr) < 0) {
        return(-1);
    }
    return 0;
}

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

/*
 * Populate intf with all interfaces and return total number of interfaces
 */
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

/*
 * For a given interface nif, return it's mac/ether address
 */
const char *drv_mac_string(struct netcf_if *nif) {
    struct ifaddrs *ifap, *ifa;
    getifaddrs(&ifap);
    struct sockaddr_dl *sdl;

    for (ifa = ifap; ifa != NULL; ifa = ifa->ifa_next) {
	sdl = (struct sockaddr_dl *) ifa->ifa_addr;
	if (strncmp(nif->name, ifa->ifa_name, strlen(ifa->ifa_name)) == 0) {
	    if (sdl != NULL && sdl->sdl_alen > 0)
		if ((sdl->sdl_type == IFT_ETHER ||
		     sdl->sdl_type == IFT_L2VLAN ||
		     sdl->sdl_type == IFT_BRIDGE) &&
		     sdl->sdl_alen == ETHER_ADDR_LEN) {
		    nif->mac = strdup(ether_ntoa((struct ether_addr *)LLADDR(sdl)));
		}
	}
    }
    freeifaddrs(ifap);

    return nif->mac;
}

int drv_if_down(struct netcf_if *nif) {
    setifflags(nif->name, -IFF_UP, nif->ncf->driver->ioctl_fd);
    return 0;
}

int drv_if_up(struct netcf_if *nif) {
    setifflags(nif->name, IFF_UP, nif->ncf->driver->ioctl_fd);
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
    struct ifreq my_ifr;
    int s, mtu = 0;
    char *mac;
    int inet = 0;
    int inet6 = 0;
    struct sockaddr_in *sin;

    struct sockaddr_in6 *sin6, null_sin6;
    struct in6_ifreq ifr6;
    int s6;
    u_int32_t flags6;
    struct in6_addrlifetime lifetime;
    int error;
    u_int32_t scopeid;
    char addr_buf[MAXHOSTNAMELEN *2 + 1];   /* for getnameinfo() */

    printf("name: %s\n", nif->name);

    /* mac address */
    mac = (char *)drv_mac_string(nif);
    printf("mac: %s\n", mac);

    /* mtu */
    memset(&my_ifr, 0, sizeof(my_ifr));
    (void) strlcpy(my_ifr.ifr_name, nif->name, sizeof(my_ifr.ifr_name));

    if ((s = socket(my_ifr.ifr_addr.sa_family, SOCK_DGRAM, 0)) < 0 &&
        (errno != EPROTONOSUPPORT ||
         (s = socket(AF_LOCAL, SOCK_DGRAM, 0)) < 0)) {
        printf("socket(family %u,SOCK_DGRAM", my_ifr.ifr_addr.sa_family);
        return NULL;
    }

    if (ioctl(s, SIOCGIFMTU, &my_ifr) != -1) {
	mtu = my_ifr.ifr_mtu;
    }
    printf("mtu: %d\n", mtu);

    /* inet or inet6 ? */
    struct ifaddrs *ifap, *ifa;
    getifaddrs(&ifap);
    struct sockaddr_dl *sdl;

    for (ifa = ifap; ifa != NULL; ifa = ifa->ifa_next) {
	sdl = (struct sockaddr_dl *) ifa->ifa_addr;
	if (strncmp(nif->name, ifa->ifa_name, strlen(ifa->ifa_name)) == 0) {
	    if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET) {
		inet = 1;
		sin = (struct sockaddr_in *)ifa->ifa_addr;
		if (sin != NULL)
		    printf("inet: %s\n", inet_ntoa(sin->sin_addr));
	    }
	    if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET6) {
		inet6 = 1;

		memset(&null_sin6, 0, sizeof(null_sin6));

		sin6 = (struct sockaddr_in6 *)ifa->ifa_addr;
		if (sin6 == NULL)
			return NULL;

		strncpy(ifr6.ifr_name, ifa->ifa_name, sizeof(ifa->ifa_name));
		if ((s6 = socket(AF_INET6, SOCK_DGRAM, 0)) < 0) {
			warn("socket(AF_INET6,SOCK_DGRAM)");
			return NULL;
		}
		ifr6.ifr_addr = *sin6;
		if (ioctl(s6, SIOCGIFAFLAG_IN6, &ifr6) < 0) {
			warn("ioctl(SIOCGIFAFLAG_IN6)");
			close(s6);
			return NULL;
		}
		flags6 = ifr6.ifr_ifru.ifru_flags6;
		memset(&lifetime, 0, sizeof(lifetime));
		ifr6.ifr_addr = *sin6;
		if (ioctl(s6, SIOCGIFALIFETIME_IN6, &ifr6) < 0) {
			warn("ioctl(SIOCGIFALIFETIME_IN6)");
			close(s6);
			return NULL;
		}
		lifetime = ifr6.ifr_ifru.ifru_lifetime;
		close(s6);

		/* XXX: embedded link local addr check */
		if (IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr) &&
		    *(u_short *)&sin6->sin6_addr.s6_addr[2] != 0) {
			u_short iindex;

			iindex = *(u_short *)&sin6->sin6_addr.s6_addr[2];
			*(u_short *)&sin6->sin6_addr.s6_addr[2] = 0;
			if (sin6->sin6_scope_id == 0)
				sin6->sin6_scope_id = ntohs(iindex);
		}
		scopeid = sin6->sin6_scope_id;

		error = getnameinfo((struct sockaddr *)sin6, sin6->sin6_len, addr_buf,
				    sizeof(addr_buf), NULL, 0, NI_NUMERICHOST);
		if (error != 0)
			inet_ntop(AF_INET6, &sin6->sin6_addr, addr_buf,
				  sizeof(addr_buf));
		printf("inet6: %s\n", addr_buf);

	    }
	}
    }
    freeifaddrs(ifap);

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

int drv_lookup_by_mac_string(struct netcf *ncf ATTRIBUTE_UNUSED,
			     const char *mac, int maxifaces ATTRIBUTE_UNUSED,
			     struct netcf_if **ifaces)
{
	int result = 0;
    struct ifaddrs *ifap, *ifa;
#if 0
struct driver;

struct netcf {
    ref_t            ref;
    char            *root;                /* The filesystem root, always ends
                                           * with '/' */
    const char      *data_dir;            /* Where to find stylesheets etc. */
    xmlRelaxNGPtr    rng;                 /* RNG of <interface> elements */
    netcf_errcode_t  errcode;
    char            *errdetails;          /* Error details */
    struct driver   *driver;              /* Driver specific data */
    unsigned int     debug;
};

struct netcf_if {
    ref_t         ref;
    struct netcf *ncf;
    char         *name;                   /* The device name */
    char         *mac;                    /* The MAC address, filled by
                                             drv_mac_string */
};
struct sockaddr {
        unsigned char   sa_len;         /* total length */
        sa_family_t     sa_family;      /* address family */
        char            sa_data[14];    /* actually longer; address value */
};

#endif
    getifaddrs(&ifap);
    for (ifa = ifap; ifa != NULL; ifa = ifa->ifa_next) {
        if ((ifa->ifa_addr->sa_family == AF_LINK) &&
           ((ifa->ifa_flags & IFF_CANTCONFIG) == 0)) {
		ALLOC((*ifaces));
	        (*ifaces)[result].name = strndup(ifa->ifa_name, strlen(ifa->ifa_name)+1);
	        (*ifaces)[result].mac = strndup(ifa->ifa_addr->sa_data, ifa->ifa_addr->sa_len);
            printf("Found intf(%s) with mac(%s)\n", (*ifaces)[result].name, (*ifaces)[result].mac);
            printf("Looking for mac(%s)\n", mac);
            printf("mac data is (%s)\n", ifa->ifa_addr->sa_data);
            result++;
	    }
    }
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
 * Local variables:
 *  indent-tabs-mode: nil
 *  c-indent-level: 4
 *  c-basic-offset: 4
 *  tab-width: 4
 * End:
 */
/* vim: set ts=4 sw=4 et: */
