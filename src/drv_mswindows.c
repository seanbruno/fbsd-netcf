/*
 * netcf-win.c: windows functions
 *
 * Copyright (C) 2010 Red Hat Inc.
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
 * Author: Adam Stokes <ajs@redhat.com>
 */

#ifndef WINVER
# define WINVER 0x0501
#endif

#include <config.h>
#include <internal.h>

#include <stdio.h>
#include <stdlib.h>
#include <spawn.h>
#include <stdbool.h>
#include <string.h>
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windns.h>
#include <iphlpapi.h>
#include <process.h>

#include "safe-alloc.h"
#include "ref.h"
#include "list.h"
#include "dutil.h"
#include "dutil_mswindows.h"

#define GAA_FLAGS ( GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_ANYCAST )
#define BUFSIZE 1024

#define strerror_s(buf,buflen,e) strerror_r((e),(buf),(buflen))

int drv_init(struct netcf *ncf) {
    if (ALLOC(ncf->driver) < 0)
        return -1;

    return 0;
}


void drv_close(struct netcf *ncf) {
    if (ncf == NULL || ncf->driver == NULL)
        return;
    FREE(ncf->driver);
}


void drv_entry(struct netcf *ncf ATTRIBUTE_UNUSED) {
}

static PIP_ADAPTER_ADDRESSES build_adapter_table(struct netcf *ncf) {
    int r = 0;
    DWORD tableSize = 0;
    PIP_ADAPTER_ADDRESSES pAddresses = NULL;

    GetAdaptersAddresses(AF_UNSPEC, GAA_FLAGS, NULL, pAddresses, &tableSize);
    r = ALLOC_N(pAddresses, tableSize);
    ERR_NOMEM(r < 0, ncf);
    r = GetAdaptersAddresses(AF_INET, GAA_FLAGS, NULL, pAddresses, &tableSize);
    ERR_COND_BAIL(r != NO_ERROR, ncf, EOTHER);
    return pAddresses;
error:
    FREE(pAddresses);
    return NULL;
}

static int list_interface_ids(struct netcf *ncf,
                              int maxnames,
                              char **names, unsigned int flags ATTRIBUTE_UNUSED) {
    size_t nint = 0;
    int r = 0;
    IP_ADAPTER_ADDRESSES *adapter;
    char *wName = NULL;

    adapter = build_adapter_table(ncf);
    ERR_COND_BAIL(adapter == NULL, ncf, EOTHER);

    while(adapter && nint < maxnames) {
        if(names) {
            r = WideCharToMultiByte(CP_UTF8, 0, adapter->FriendlyName,
                                    -1, wName, sizeof(adapter->FriendlyName), NULL, NULL);
            ERR_NOMEM(r == 0, ncf);
            names[nint] = strdup(wName);
            ERR_NOMEM(names[nint] == NULL, ncf);
        }
        nint++;
        adapter = adapter->Next;
    }
    return nint;
 error:
    free(adapter);
    while(nint > 0) {
        free(names[nint]);
        nint--;
    }
    return -1;
}

int drv_list_interfaces(struct netcf *ncf,
                        int maxnames, char **names,
                        unsigned int flags) {
    return list_interface_ids(ncf, maxnames, names, flags);
}


int drv_num_of_interfaces(struct netcf *ncf, unsigned int flags) {
    return list_interface_ids(ncf, 0, NULL, flags);
}


struct netcf_if *drv_lookup_by_name(struct netcf *ncf, const char *name) {
    struct netcf_if *nif = NULL;
    char *nameDup = NULL;
    int r = 0;
    IP_ADAPTER_ADDRESSES *adapter;

    adapter = build_adapter_table(ncf);
    ERR_COND_BAIL(adapter == NULL, ncf, EOTHER);

    while(adapter) {
        if(name) {
            char wName[8192];
            r = WideCharToMultiByte(CP_UTF8, 0, adapter->FriendlyName,
                                    -1, wName, sizeof(wName), NULL, NULL);
            ERR_NOMEM(r == 0, ncf);
            if(strcmp(name,wName) == 0) {
                nameDup = strdup(wName);
                ERR_NOMEM(nameDup == NULL, ncf);
                nif = make_netcf_if(ncf, nameDup);
                ERR_BAIL(ncf);
                return nif;
            }
        }
        adapter = adapter->Next;
    }
    /* If we get here then the device wasn't found, however,
       for cases where we know the device is disabled and
       want to re-enable it we have to assume the device is
       physically present
    */
    nameDup = strdup(name);
    ERR_NOMEM(nameDup == NULL, ncf);
    nif = make_netcf_if(ncf, nameDup);
    ERR_BAIL(ncf);
    return nif;
 error:
    free(adapter);
    free(nameDup);
    unref(nif, netcf_if);
    return nif;
}

const char *drv_mac_string(struct netcf_if *nif) {
    struct netcf *ncf = nif->ncf;
    size_t i = 0;
    int r = 0;
    char mac[BUFSIZE], *buf;

    IP_ADAPTER_ADDRESSES *adapter;

    adapter = build_adapter_table(ncf);
    ERR_COND_BAIL(adapter == NULL, ncf, EOTHER);

    while(adapter) {
        char wName[8192];
        r = WideCharToMultiByte(CP_UTF8, 0, adapter->FriendlyName,
                                -1, wName, sizeof(wName), NULL, NULL);
        ERR_NOMEM(r == 0, ncf);
        if(strcmp(nif->name, wName) == 0) {
            for(i = 0; i < adapter->PhysicalAddressLength; i++) {
                if (i == 0) {
                    ERR_NOMEM(asprintf(&buf, "%.2X:", adapter->PhysicalAddress[i]) < 0, ncf);
                    strcpy(mac, buf);
                }
                if (i == (adapter->PhysicalAddressLength - 1)) {
                    ERR_NOMEM(asprintf(&buf, "%.2X", adapter->PhysicalAddress[i]) < 0, ncf);
                    strcat(mac, buf);
                } else {
                    ERR_NOMEM(asprintf(&buf, "%.2X:", adapter->PhysicalAddress[i]) < 0, ncf);
                    strcat(mac, buf);
                }
                nif->mac = strdup(mac);
                ERR_NOMEM(nif->mac == NULL, ncf);
            }
            return nif->mac;
        }
        adapter = adapter->Next;
    }
 error:
    free(adapter);
    free(buf);
    return nif->mac;
}

int drv_if_down(struct netcf_if *nif) {
    struct netcf *ncf = nif->ncf;
    char *exe_path;
    char *p;
    int r = 0;

    p = getenv("WINDIR");
    r = asprintf(&exe_path, "%s\\system32\\netsh", p);
    ERR_NOMEM(r < 0, ncf);

    r = _spawnl(_P_WAIT, exe_path, exe_path, "interface",
                "set", "interface", nif->name, "disabled", NULL);
    ERR_COND_BAIL(r != 0, ncf, EEXEC);
    return 0;
 error:
    free(exe_path);
    return -1;
}

int drv_if_up(struct netcf_if *nif) {
    struct netcf *ncf = nif->ncf;
    char *exe_path;
    char *p;
    int r = 0;

    p = getenv("WINDIR");
    r = asprintf(&exe_path, "%s\\system32\\netsh", p);
    ERR_NOMEM(r < 0, ncf);

    r = _spawnl(_P_WAIT, exe_path, exe_path, "interface",
                "set", "interface", nif->name, "enabled", NULL);
    ERR_COND_BAIL(r != 0, ncf, EEXEC);
    return 0;
 error:
    free(exe_path);
    return -1;
}


struct netcf_if *drv_define(struct netcf *ncf, const char *xml_str ATTRIBUTE_UNUSED) {
    struct netcf_if *result = NULL;

    ERR_THROW(1 == 1, ncf, EOTHER, "not implemented on this platform");

error:
    return result;
}

int drv_undefine(struct netcf_if *nif) {
    int result = -1;

    ERR_THROW(1 == 1, nif->ncf, EOTHER, "not implemented on this platform");
    result = 0;
error:
    return result;
}


char *drv_xml_desc(struct netcf_if *nif) {
    char *result = NULL;

    ERR_THROW(1 == 1, nif->ncf, EOTHER, "not implemented on this platform");

error:
    return result;
}

char *drv_xml_state(struct netcf_if *nif) {
    char *result = NULL;

    ERR_THROW(1 == 1, nif->ncf, EOTHER, "not implemented on this platform");

error:
    return result;
}

int drv_if_status(struct netcf_if *nif, unsigned int *flags ATTRIBUTE_UNUSED) {
    int result = -1;

    ERR_THROW(1 == 1, nif->ncf, EOTHER, "not implemented on this platform");
    result = 0;
error:
    return result;
}

int drv_lookup_by_mac_string(struct netcf *ncf,
			     const char *mac ATTRIBUTE_UNUSED,
                             int maxifaces ATTRIBUTE_UNUSED,
			     struct netcf_if **ifaces ATTRIBUTE_UNUSED)
{
    int result = -1;

    ERR_THROW(1 == 1, ncf, EOTHER, "not implemented on this platform");
    result = 0;
error:
    return result;
}

int
drv_change_begin(struct netcf *ncf, unsigned int flags ATTRIBUTE_UNUSED)
{
    int result = -1;

    ERR_THROW(1 == 1, ncf, EOTHER, "not implemented on this platform");
    result = 0;
error:
    return result;
}

int
drv_change_rollback(struct netcf *ncf, unsigned int flags ATTRIBUTE_UNUSED)
{
    int result = -1;

    ERR_THROW(1 == 1, ncf, EOTHER, "not implemented on this platform");
    result = 0;
error:
    return result;
}

int
drv_change_commit(struct netcf *ncf, unsigned int flags ATTRIBUTE_UNUSED)
{
    int result = -1;

    ERR_THROW(1 == 1, ncf, EOTHER, "not implemented on this platform");
    result = 0;
error:
    return result;
}
