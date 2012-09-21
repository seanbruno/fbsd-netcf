/*
 * Copyright (c) 2012 ADARA Networks.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/types.h>
#include <assert.h>
#include <ifaddrs.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_types.h>

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/sockio.h>

#define GETIFADDRS_BUF_SIZE 1024
static void
add_addr(char *base, char **p, struct sockaddr **oaddr, struct sockaddr *addr)
{
	if (addr) {
		*oaddr = (struct sockaddr *)*p;
		*p += addr->sa_len;
		assert(*p - base < GETIFADDRS_BUF_SIZE);
		memcpy(*oaddr, addr, addr->sa_len);
	} else {
		*oaddr = NULL;
	}
}

static void
add_ifaddr(struct ifaddrs *base, size_t *off, int last, const char *name,
    u_int flags, struct sockaddr *addr, struct sockaddr *netmask,
    struct sockaddr *dstaddr, void *data)
{
	struct ifaddrs *ifa;
	char *p = (char *)base + *off;

	ifa = (struct ifaddrs *)p;
	p += sizeof(*ifa);
	assert(p - (char *)base < GETIFADDRS_BUF_SIZE);
	memset (ifa, 0, sizeof(*ifa));

	ifa->ifa_name = p;
	p += strlen(name) + 1;
	assert(p - (char *)base < GETIFADDRS_BUF_SIZE);
	strcpy(ifa->ifa_name, name);

	ifa->ifa_flags = flags;

	add_addr((char *)base, &p, &ifa->ifa_addr, addr);
	add_addr((char *)base, &p, &ifa->ifa_netmask, netmask);
	add_addr((char *)base, &p, &ifa->ifa_dstaddr, dstaddr);
	ifa->ifa_data = NULL;

	if (!last)
		ifa->ifa_next = (struct ifaddrs *)p;
	*off = p - (char *)base;
}

static struct sockaddr_dl *
sockaddr_dl_common(const char *name, const char *addr, size_t addr_len)
{
	static struct sockaddr_dl sdl;
	char *p;

	assert(strlen(name) + addr_len < sizeof(sdl.sdl_data));

	sdl.sdl_nlen = strlen(name);
	sdl.sdl_alen = addr_len;

	p = sdl.sdl_data;
	memcpy(p, name, strlen(name));
	p += strlen(name);
	if (addr_len > 0) {
		memcpy(p, addr, addr_len);
		p+= ETHER_ADDR_LEN;
	}

	sdl.sdl_len = p - (char *)&sdl;
	return &sdl;
}

static struct sockaddr *
sockaddr_loop(const char *name)
{
	static struct sockaddr_dl *sdl;

	sdl = sockaddr_dl_common(name, NULL, 0);
	sdl->sdl_family = AF_LINK;
	sdl->sdl_index = 0;
	sdl->sdl_type = IFT_LOOP;
	sdl->sdl_slen = 0;
	
	return (struct sockaddr *)sdl;
}

static struct sockaddr *
sockaddr_ether(const char *name, const char *addr)
{
	static struct sockaddr_dl *sdl;
	struct ether_addr *ea;

	ea = ether_aton(addr);
	sdl = sockaddr_dl_common(name, (const char *)ea->octet, ETHER_ADDR_LEN);
	sdl->sdl_family = AF_LINK;
	sdl->sdl_index = 0;
	sdl->sdl_type = IFT_ETHER;
	sdl->sdl_slen = 0;

	return (struct sockaddr *)sdl;
}

static void
add_ether_interface(struct ifaddrs *base, size_t *off, int last, const char *name, const char *ether_addr)
{
	add_ifaddr(base, off, last, name, 0, sockaddr_ether(name, ether_addr), 0, 0, 0);
}

static void
add_loop_interface(struct ifaddrs *base, size_t *off, int last, const char *name)
{
	add_ifaddr(base, off, last, name, 0, sockaddr_loop(name), 0, 0, 0);
}

static void
add_bridge_interface(struct ifaddrs *base, size_t *off, int last, const char *name)
{
	add_ifaddr(base, off, last, name, 0, sockaddr_loop(name), 0, 0, 0);
}

static void
add_lagg_interface(struct ifaddrs *base, size_t *off, int last, const char *name)
{
	add_ifaddr(base, off, last, name, 0, sockaddr_loop(name), 0, 0, 0);
}

int getifaddrs(struct ifaddrs **ifap)
{
	struct ifaddrs *ifa;
	size_t off = 0;

	ifa = malloc(GETIFADDRS_BUF_SIZE);
	add_ether_interface(ifa, &off, 0, "em0", "90:2b:34:01:02:03");
	add_ether_interface(ifa, &off, 0, "em1", "aa:bb:cc:dd:ee:ff");
	add_loop_interface(ifa, &off, 0, "lo0");
	add_lagg_interface(ifa, &off, 0, "lagg0");
	add_bridge_interface(ifa, &off, 1, "bridge0");

	*ifap = ifa;
	return (0);
}

/* ioctl */
static int is_valid_name(const char *name)
{
	if (!strcmp(name, "em0") ||
	    !strcmp(name, "em1") ||
	    !strcmp(name, "lo0") ||
	    !strcmp(name, "lagg0") ||
	    !strcmp(name, "bridge0"))
		return (0);
	return (-1);
}

int ioctl(int d, unsigned long request, ...)
{
	struct ifreq *ifr;
	void *data;
	va_list ap;

	va_start(ap, request);
	data = va_arg(ap, void *);
	va_end(ap);

	switch (request) {
	case SIOCGIFFLAGS:
		ifr = (struct ifreq *)data;
		return (is_valid_name(ifr->ifr_name));
	case SIOCSIFFLAGS:
		ifr = (struct ifreq *)data;
		return (is_valid_name(ifr->ifr_name));
	}
	return (-1);
}

