/*
 * drv_suse.c: the suse backend for suse
 *
 * Copyright (C) 2010 Novell Inc.
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
 * Derived from the drv_initscripts.c for the Redhat backend authored
 * by David Lutterkort.
 *
 * Author: Patrick Mullaney <pmullaney@novell.com>
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
#include <sys/types.h>
#include <dirent.h>


#include "safe-alloc.h"
#include "ref.h"
#include "list.h"
#include "dutil.h"
#include "dutil_posix.h"
#include "dutil_linux.h"

#include <libxml/parser.h>
#include <libxml/relaxng.h>
#include <libxml/tree.h>
#include <libxslt/xslt.h>
#include <libxslt/xsltInternals.h>
#include <libxslt/transform.h>
#include <libxslt/xsltutils.h>

#include <libexslt/exslt.h>

#define NETRULE_PATH "/etc/udev/rules.d/70-persistent-net.rules"

#define NETCF_TRANSACTION "/bin/false"

static const char *const aug_files =
    "/files";

static const char *const network_scripts_path =
    "/etc/sysconfig/network";

static const char *const ifcfg_path =
    "/files/etc/sysconfig/network/*";

static const char *const udev_netrule_path =
    NETRULE_PATH;

static const char *const ifcfg_prefix =
    "ifcfg-";

/* Augeas should only load the files we are interested in */
static const struct augeas_pv augeas_xfm_common_pv[] = {
    /* Netrule files */
    { "/augeas/load/Persist_Net_Rules/lens", "Persist_Net_Rules.lns" },
    { "/augeas/load/Persist_Net_Rules/incl", NETRULE_PATH },
    { "/augeas/load/Persist_Net_Rules/excl[1]", "*.augnew" },
    { "/augeas/load/Persist_Net_Rules/excl[2]", "*.augsave" },
    { "/augeas/load/Persist_Net_Rules/excl[3]", "*.rpmsave" },
    { "/augeas/load/Persist_Net_Rules/excl[4]", "*.rpmnew" },
    { "/augeas/load/Persist_Net_Rules/excl[5]", "*~" },
    /* Routes files */
    { "/augeas/load/Routes/lens", "Routes.lns" },
    { "/augeas/load/Routes/incl", "/etc/sysconfig/network/routes" },
    { "/augeas/load/Routes/excl[1]", "*.augnew" },
    { "/augeas/load/Routes/excl[2]", "*.augsave" },
    { "/augeas/load/Routes/excl[3]", "*.rpmsave" },
    { "/augeas/load/Routes/excl[4]", "*.rpmnew" },
    { "/augeas/load/Routes/excl[5]", "*~" },
    /* Ifcfg files */
    { "/augeas/load/Ifcfg/lens", "Sysconfig.lns" },
    { "/augeas/load/Ifcfg/incl", "/etc/sysconfig/network/ifcfg-*" },
    { "/augeas/load/Ifcfg/excl[1]", "*~" },
    { "/augeas/load/Ifcfg/excl[2]", "*.bak" },
    { "/augeas/load/Ifcfg/excl[3]", "*.orig" },
    { "/augeas/load/Ifcfg/excl[4]", "*.rpmnew" },
    { "/augeas/load/Ifcfg/excl[5]", "*.rpmorig" },
    { "/augeas/load/Ifcfg/excl[6]", "*.rpmsave" },
    { "/augeas/load/Ifcfg/excl[7]", "*.augnew" },
    { "/augeas/load/Ifcfg/excl[8]", "*.augsave" },
    /* modprobe config */
    { "/augeas/load/Modprobe/lens", "Modprobe.lns" },
    { "/augeas/load/Modprobe/incl[1]", "/etc/modprobe.d/*" },
    { "/augeas/load/Modprobe/incl[2]", "/etc/modprobe.conf" },
    { "/augeas/load/Modprobe/excl[1]", "*.augnew" },
    { "/augeas/load/Modprobe/excl[2]", "*.augsave" },
    { "/augeas/load/Modprobe/excl[3]", "*.rpmsave" },
    { "/augeas/load/Modprobe/excl[4]", "*.rpmnew" },
    { "/augeas/load/Modprobe/excl[5]", "*~" },
    /* sysfs (choice entries from /class/net) */
    { "/augeas/load/Sysfs/lens", "Netcf.id" },
    { "/augeas/load/Sysfs/incl", "/sys/class/net/*/address" }
};

static const struct augeas_xfm_table augeas_xfm_common =
    { .size = ARRAY_CARDINALITY(augeas_xfm_common_pv),
      .pv = augeas_xfm_common_pv };

/* Entries in a ifcfg file that tell us that the interface
 * is not a toplevel interface
 */
static const char *const subif_paths[] = {
    "BONDING_MASTER", "BRIDGE"
};

static int is_slave(struct netcf *ncf, const char *intf) {
    for (int s = 0; s < ARRAY_CARDINALITY(subif_paths); s++) {
        int r;
        r = aug_fmt_match(ncf, NULL, "%s%s/ifcfg-%s/%s",
                          aug_files, network_scripts_path, intf, subif_paths[s]);
        if (r != 0)
            return r;
    }
    return 0;
}

static bool has_ifcfg_file(struct netcf *ncf, const char *name) {
    struct augeas *aug = NULL;
    char *path = NULL;
    int nmatches = 0, r;

    aug = get_augeas(ncf);
    ERR_BAIL(ncf);

    /* if ifcfg-NAME exists, true */
    r = xasprintf(&path, "%s/%s/ifcfg-%s", aug_files, network_scripts_path, name);
    ERR_NOMEM(r < 0, ncf);

    nmatches = aug_match(aug, path, NULL);
    ERR_COND_BAIL(nmatches < 0, ncf, EOTHER);

    FREE(path);

error:
    return nmatches > 0;
}

static int cmpstrp(const void *p1, const void *p2) {
    const char *s1 = * (const char **)p1;
    const char *s2 = * (const char **)p2;
    return strcmp(s1, s2);
}

static int
find_ifcfg_file_by_name(struct netcf *ncf, const char *name, char ***intf)
{
    DIR *dp;
    struct dirent *ent;
    const char *ifprefix = "ifcfg-";
    const char *ifdir = network_scripts_path;
    int found=-1, result=0, r=0;

    dp = opendir (ifdir);
    ERR_COND_BAIL(dp == NULL, ncf, EOTHER);
    r = ALLOC_N(*intf, 1);
    while ((ent = readdir (dp)))
        if (ent->d_type == DT_REG &&
            !strncmp(ifprefix, ent->d_name, strlen(ifprefix)) &&
            strlen(ent->d_name + strlen(ifprefix)) == strlen(name) &&
            !strncmp((ent->d_name + strlen(ifprefix)),name, strlen(name))) {
            result = xasprintf( &(*intf)[0], "%s",
                                ent->d_name + strlen(ifprefix));
            ERR_NOMEM(result < 0, ncf);
            found = 1;
        }
    closedir (dp);

    return found;

error:
    FREE((*intf)[0]);
    FREE((*intf));
    return -1;
}

static int
find_ifcfg_files(struct netcf *ncf, char ***intf)
{
    DIR *dp;
    struct dirent *ent;
    const char *ifprefix = "ifcfg-";
    const char *ifdir = network_scripts_path;
    int count = 0, result =0, r =0;;

    dp = opendir (ifdir);
    ERR_COND_BAIL(dp == NULL, ncf, EOTHER);
    while ((ent = readdir (dp)))
        if (ent->d_type == DT_REG &&
            !strncmp(ifprefix, ent->d_name, strlen(ifprefix))) {
            count++;
        }
    closedir (dp);

    dp = opendir (ifdir);
    ERR_COND_BAIL(dp == NULL, ncf, EOTHER);
    r = ALLOC_N(*intf, count);
    ERR_NOMEM(r < 0, ncf);
    count = 0;
    while ((ent = readdir (dp))) {
        if (ent->d_type == DT_REG &&
            !strncmp(ifprefix, ent->d_name, strlen(ifprefix))) {
            result = xasprintf( &(*intf)[count], "%s",
                                ent->d_name + strlen(ifprefix));
            ERR_NOMEM(result < 0, ncf);
            count++;
        }
    }
    closedir (dp);

    qsort(*intf, count, sizeof(**intf), cmpstrp);

    return count;

error:
    for(;count>=0;count--)
        FREE((*intf)[count]);
    FREE((*intf));
    return -1;
}

/* Find the mac address given the interface name in the udev persistent
 * netrule file.
 */
static int find_hwaddr_by_device(struct netcf *ncf, const char *name,
                                 const char **addr) {
    int nmatches = 0, r;
    char **matches = NULL;
    struct augeas *aug = NULL;
    char *path = NULL;

    aug = get_augeas(ncf);
    ERR_BAIL(ncf);

    nmatches = aug_fmt_match(ncf, &matches, "%s/%s/*[ NAME = '%s']",
                             aug_files, udev_netrule_path, name);
    ERR_COND_BAIL(nmatches < 0 , ncf, EOTHER);

    if (nmatches == 1 ) {
        r = xasprintf(&path, "%s/%s", matches[0], "ATTR{address}");
        ERR_NOMEM(r < 0, ncf);
        *addr = NULL; /* ensure that addr is NULL */
        r = aug_get(aug, path, addr);
        FREE(path);
    } else
        goto error;

    free_matches(nmatches, &matches);
    return nmatches;
 error:
    free_matches(nmatches, &matches);
    return 0;
}

/* Find the path to the ifcfg file that has the configuration for
 * the device NAME. The logic follows the need_config function
 * in /etc/sysconfig/network-scripts/network-functions
 */
static char *find_ifcfg_path(struct netcf *ncf, const char *name) {
    struct augeas *aug = NULL;
    char *path = NULL;
    int r, nmatches;

    aug = get_augeas(ncf);
    ERR_BAIL(ncf);

    /* if ifcfg-NAME exists, use that */
    r = xasprintf(&path, "%s/%s/ifcfg-%s", aug_files, network_scripts_path, name);
    ERR_NOMEM(r < 0, ncf);

    nmatches = aug_match(aug, path, NULL);
    ERR_COND_BAIL(nmatches < 0, ncf, EOTHER);

    if (nmatches == 1)
        return path;

 error:
    FREE(path);

    return NULL;
}

static int list_interfaces(struct netcf *ncf, char ***intf) {
    int nint = 0, result = 0;
    struct augeas *aug = NULL;

    aug = get_augeas(ncf);
    ERR_BAIL(ncf);

    /* Look for all interfaces */
    nint = find_ifcfg_files(ncf, intf);
    ERR_BAIL(ncf);
    result = nint;

    /* Filter out the interfaces that are slaves/subordinate */
    for (int i = 0; i < result;) {
        if (is_slave(ncf, (*intf)[i])) {
            FREE((*intf)[i]);
            memmove(*intf + i, *intf + i + 1,
                    (nint - (i + 1))*sizeof((*intf)[0]));
            result -= 1;
        } else {
            i += 1;
        }
    }
    return result;
 error:
    free_matches(nint, intf);
    return -1;
}


int drv_init(struct netcf *ncf) {
    int r;

    if (ALLOC(ncf->driver) < 0)
        return -1;

    ncf->driver->ioctl_fd = -1;

    r = add_augeas_xfm_table(ncf, &augeas_xfm_common);
    if (r < 0)
        goto error;

    // FIXME: Check for errors

    xsltInit();
    exsltStrRegister();
    ncf->driver->get = parse_stylesheet(ncf, "suse-get.xsl");
    ncf->driver->put = parse_stylesheet(ncf, "suse-put.xsl");
    ERR_BAIL(ncf);

    /* open a socket for interface ioctls */
    ncf->driver->ioctl_fd = init_ioctl_fd(ncf);
    if (ncf->driver->ioctl_fd < 0)
        goto error;
    if (netlink_init(ncf) < 0)
        goto error;
    return 0;

 error:
    drv_close(ncf);
    return -1;
}

void drv_close(struct netcf *ncf) {
    if (ncf == NULL || ncf->driver == NULL)
        return;
    xsltFreeStylesheet(ncf->driver->get);
    xsltFreeStylesheet(ncf->driver->put);
    netlink_close(ncf);
    if (ncf->driver->ioctl_fd >= 0)
        close(ncf->driver->ioctl_fd);
    aug_close(ncf->driver->augeas);
    FREE(ncf->driver->augeas_xfm_tables);
    FREE(ncf->driver);
}

void drv_entry(struct netcf *ncf) {
    ncf->driver->load_augeas = 1;
}

static int list_interface_ids(struct netcf *ncf,
                              int maxnames, char **names,
                              unsigned int flags) {
    struct augeas *aug = NULL;
    int nint = 0, nqualified = 0, result = 0;
    char **intf = NULL;

    aug = get_augeas(ncf);
    ERR_BAIL(ncf);
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

int drv_list_interfaces(struct netcf *ncf, int maxnames, char **names,
        unsigned int flags) {
    return list_interface_ids(ncf, maxnames, names, flags);
}

int drv_num_of_interfaces(struct netcf *ncf, unsigned int flags) {
    return list_interface_ids(ncf, 0, NULL, flags);
}

struct netcf_if *drv_lookup_by_name(struct netcf *ncf, const char *name) {
    struct netcf_if *nif = NULL;
    char *pathx = NULL;
    char *name_dup = NULL;
    struct augeas *aug;

    aug = get_augeas(ncf);
    ERR_BAIL(ncf);

    pathx = find_ifcfg_path(ncf, name);
    ERR_BAIL(ncf);

    if (pathx == NULL || is_slave(ncf, name))
        goto done;

    name_dup = strdup(name);
    ERR_NOMEM(name_dup == NULL, ncf);

    nif = make_netcf_if(ncf, name_dup);
    ERR_BAIL(ncf);
    goto done;

 error:
    unref(nif, netcf_if);
    FREE(name_dup);
 done:
    FREE(pathx);
    return nif;
}

/* Get an XML desription of the interfaces (just paths, really) in INTF.
 * The format is a very simple representation of the Augeas tree (see
 * xml/augeas.rng)
 */
static xmlDocPtr aug_get_xml(struct netcf_if *nif, int nint, char **intf) {
    struct augeas *aug;
    xmlDocPtr result = NULL;
    xmlNodePtr root = NULL, tree = NULL;
    char **matches = NULL;
    struct netcf *ncf = nif->ncf;
    int nmatches, r;
    int pathoffset = strlen(aug_files) + strlen(network_scripts_path)
                     + strlen(ifcfg_prefix) + 1;

    aug = get_augeas(ncf);
    ERR_BAIL(ncf);

    result = xmlNewDoc(BAD_CAST "1.0");
    root = xmlNewNode(NULL, BAD_CAST "forest");
    xmlDocSetRootElement(result, root);

    for (int i=0; i < nint; i++) {
        tree = xmlNewChild(root, NULL, BAD_CAST "tree", NULL);
        xmlNewProp(tree, BAD_CAST "path", BAD_CAST intf[i]);
        nmatches = aug_fmt_match(ncf, &matches, "%s%s/ifcfg-%s/%s",
                                 aug_files, network_scripts_path, intf[i], "*");
        ERR_COND_BAIL(nint < 0, ncf, EOTHER);
        for (int j = 0; j < nmatches; j++) {
            xmlNodePtr node = xmlNewChild(tree, NULL, BAD_CAST "node", NULL);
            const char *value;
            xmlNewProp(node, BAD_CAST "label",
                       BAD_CAST matches[j] + pathoffset + strlen(intf[i]) + 1);
            r = aug_get(aug, matches[j], &value);
            ERR_COND_BAIL(r < 0, ncf, EOTHER);
            xmlNewProp(node, BAD_CAST "value", BAD_CAST value);
        }
        {
            xmlNodePtr node = xmlNewChild(tree, NULL, BAD_CAST "node", NULL);
            xmlNewProp(node, BAD_CAST "label",
                       BAD_CAST "DEVICE" );
            xmlNewProp(node, BAD_CAST "value", BAD_CAST nif->name);
        }
        {
            const char *mac = NULL;
            if( find_hwaddr_by_device(ncf, nif->name, &mac) > 0 ) {
                xmlNodePtr node = xmlNewChild(tree, NULL, BAD_CAST "node", NULL);
                xmlNewProp(node, BAD_CAST "label",
                           BAD_CAST "HWADDR" );
                xmlNewProp(node, BAD_CAST "value", BAD_CAST mac);
            }
        }
        free_matches(nmatches, &matches);
    }

    return result;

 error:
    free_matches(nmatches, &matches);
    xmlFreeDoc(result);
    return NULL;
}

/* Write the XML doc in the simple Augeas format into the Augeas tree */
static int aug_put_xml(struct netcf *ncf, xmlDocPtr xml) {
    xmlNodePtr forest;
    char *path = NULL, *lpath = NULL, *label = NULL, *value = NULL;
    char *device = NULL, *mac = NULL, *gateway = NULL;
    char **rules = NULL;
    struct augeas *aug = NULL;
    int result = -1, nrules = 0, ethphysical = 0;
    int toplevel = 1;
    int r;

    aug = get_augeas(ncf);
    ERR_BAIL(ncf);

    forest = xmlDocGetRootElement(xml);
    ERR_THROW(forest == NULL, ncf, EINTERNAL, "missing root element");
    ERR_THROW(! xmlStrEqual(forest->name, BAD_CAST "forest"), ncf,
              EINTERNAL, "expected root node labeled 'forest', not '%s'",
              forest->name);
    list_for_each(tree, forest->children) {
        ERR_THROW(! xmlStrEqual(tree->name, BAD_CAST "tree"), ncf,
                  EINTERNAL, "expected node labeled 'tree', not '%s'",
                  tree->name);
        path = xml_prop(tree, "path");
        /*int toplevel = 1;*/
        /* This is a little drastic, since it clears out the file entirely */
        r = aug_rm(aug, path);
        ERR_THROW(r < 0, ncf, EINTERNAL, "aug_rm of '%s' failed", path);
        list_for_each(node, tree->children) {
            label = xml_prop(node, "label");
            value = xml_prop(node, "value");
            /* We should mark the toplevel interface from the XSLT */
            if (STREQ(label, "BRIDGE") || STREQ(label, "BONDING_MASTER")) {
                toplevel = 0;
            }
            if (STREQ(label, "DEVICE")) {
		device = value;
                if(!strchr(value, '.') && !strncmp("eth", value, strlen("eth")))
                   ethphysical = 1;
                xmlFree(label);
            } else if(STREQ(label, "HWADDR")) {
		mac = value;
                xmlFree(label);
		//continue;
            } else if(STREQ(label, "GATEWAY")) {
		gateway = value;
                xmlFree(label);
		//continue;
            } else {
                r = xasprintf(&lpath, "%s/%s", path, label);
                ERR_NOMEM(r < 0, ncf);

                r = aug_set(aug, lpath, value);
                ERR_THROW(r < 0, ncf, EOTHER,
                          "aug_set of '%s' failed", lpath);
                FREE(lpath);
                xmlFree(label);
                xmlFree(value);
            }
            label = value = NULL;
        }
        xmlFree(path);
        path = NULL;
    }
    if( device && !mac && ethphysical && toplevel ){
        mac = malloc(20);
        if( if_hwaddr(ncf, device, mac, 20) ){
            free(mac);
            mac = NULL;
        }
    }
    if( device && gateway && ethphysical && toplevel ) {

        r = xasprintf(&lpath, "%s%s/%s/%s", aug_files,
                       network_scripts_path, "routes/default", "gateway");
        ERR_NOMEM(r < 0, ncf);

        r = aug_set(aug, lpath, gateway);
        ERR_THROW(r < 0, ncf, EOTHER,
              "aug_set of '%s' failed", lpath);

        FREE(lpath);

        r = xasprintf(&lpath, "%s%s/%s/%s", aug_files,
                       network_scripts_path, "routes/default", "netmask");
        ERR_NOMEM(r < 0, ncf);

        r = aug_set(aug, lpath, "-");
        ERR_THROW(r < 0, ncf, EOTHER,
              "aug_set of '%s' failed", lpath);

        FREE(lpath);

        r = xasprintf(&lpath, "%s%s/%s/%s", aug_files,
                       network_scripts_path, "routes/default", "device");
        ERR_NOMEM(r < 0, ncf);

        r = aug_set(aug, lpath, device );
        ERR_THROW(r < 0, ncf, EOTHER,
              "aug_set of '%s' failed", lpath);

        FREE(lpath);
    }
    if( device && mac && ethphysical && toplevel ) {

        r = xasprintf(&lpath, "%s%s/%s/%s", aug_files,
                       udev_netrule_path, device, "SUBSYSTEM");
        ERR_NOMEM(r < 0, ncf);

        r = aug_set(aug, lpath, "net");
        ERR_THROW(r < 0, ncf, EOTHER,
              "aug_set of '%s' failed", lpath);

        FREE(lpath);

        r = xasprintf(&lpath, "%s%s/%s/%s", aug_files,
                       udev_netrule_path, device, "ACTION");
        ERR_NOMEM(r < 0, ncf);

        r = aug_set(aug, lpath, "add");
        ERR_THROW(r < 0, ncf, EOTHER,
              "aug_set of '%s' failed", lpath);

        FREE(lpath);

        r = xasprintf(&lpath, "%s%s/%s/%s", aug_files,
                       udev_netrule_path, device, "DRIVERS");
        ERR_NOMEM(r < 0, ncf);

        r = aug_set(aug, lpath, "?*");
        ERR_THROW(r < 0, ncf, EOTHER,
              "aug_set of '%s' failed", lpath);

        FREE(lpath);

        r = xasprintf(&lpath, "%s%s/%s/%s", aug_files,
                       udev_netrule_path, device, "ATTR{address}");
        ERR_NOMEM(r < 0, ncf);

        r = aug_set(aug, lpath, mac);
        ERR_THROW(r < 0, ncf, EOTHER,
                  "aug_set of '%s' failed", lpath);

        FREE(lpath);

#ifdef OS113
        r = xasprintf(&lpath, "%s%s/%s/%s", aug_files,
                       udev_netrule_path, device, "ATTR{dev_id}");
        ERR_NOMEM(r < 0, ncf);

        r = aug_set(aug, lpath, "0x0");
        ERR_THROW(r < 0, ncf, EOTHER,
                  "aug_set of '%s' failed", lpath);

        FREE(lpath);
#endif

            r = xasprintf(&lpath, "%s%s/%s/%s", aug_files,
                           udev_netrule_path, device, "ATTR{type}");
            ERR_NOMEM(r < 0, ncf);

            r = aug_set(aug, lpath, "1");
            ERR_THROW(r < 0, ncf, EOTHER,
                  "aug_set of '%s' failed", lpath);

            FREE(lpath);

            r = xasprintf(&lpath, "%s%s/%s/%s", aug_files,
                           udev_netrule_path, device, "KERNEL");
            ERR_NOMEM(r < 0, ncf);

            r = aug_set(aug, lpath, "eth*");
            ERR_THROW(r < 0, ncf, EOTHER,
                  "aug_set of '%s' failed", lpath);

            FREE(lpath);
    }
    xmlFree(device);
    xmlFree(mac);
    xmlFree(gateway);
    result = 0;
 error:
    xmlFree(label);
    xmlFree(value);
    xmlFree(path);
    FREE(lpath);
    return result;
}

/* given a netcf_if, get the config for an interface in the simple
 * Augeas format
 */
static xmlDocPtr aug_get_xml_for_nif(struct netcf_if *nif) {
    struct netcf *ncf;
    char **intf = NULL;
    xmlDocPtr aug_xml = NULL;
    int nint = 0;

    ncf = nif->ncf;
    nint = find_ifcfg_file_by_name(ncf, nif->name, &intf);
    ERR_BAIL(ncf);

    aug_xml = aug_get_xml(nif, nint, intf);

 error:
    free_matches(nint, &intf);
    return aug_xml;
}

/* return the current static configuration (as saved on disk) */
char *drv_xml_desc(struct netcf_if *nif) {
    char *result = NULL;
    struct netcf *ncf;
    xmlDocPtr aug_xml = NULL;

    ncf = nif->ncf;
    aug_xml = aug_get_xml_for_nif(nif);
    ERR_BAIL(ncf);

    result = apply_stylesheet_to_string(ncf, ncf->driver->put, aug_xml);

 error:
    xmlFreeDoc(aug_xml);
    return result;
}

/* return the current live configuration state - a combination of
 * drv_xml_desc + results of querying the interface directly */

char *drv_xml_state(struct netcf_if *nif) {
    char *result = NULL;
    int r, result_len;
    struct netcf *ncf;
    xmlDocPtr ncf_xml = NULL;
    xmlNodePtr root;

    ncf = nif->ncf;

    /* start out with an empty tree rather than the config tree. Just
     * put in the interface node and its name
     */
    ncf_xml = xmlNewDoc(BAD_CAST "1.0");
    ERR_NOMEM(ncf_xml == NULL, ncf);
    root = xmlNewNode(NULL, BAD_CAST "interface");
    ERR_NOMEM(root == NULL, ncf);
    xmlDocSetRootElement(ncf_xml, root);

    /* add all info we can gather from the kernel/sysfs/procfs */
    add_state_to_xml_doc(nif, ncf_xml);
    ERR_BAIL(ncf);

    r = xsltSaveResultToString((xmlChar **)&result, &result_len,
                               ncf_xml, ncf->driver->put);
    ERR_NOMEM(r < 0, ncf);

 done:
    xmlFreeDoc(ncf_xml);
    return result;
 error:
    FREE(result);
    result = 0;
    goto done;
}

/* Report various status info about the interface as bits in
 * "flags". Returns 0 on success, -1 on failure
 */
int drv_if_status(struct netcf_if *nif, unsigned int *flags) {
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

/* Get the content of /interface/@name. Result must be freed with xmlFree()
 *
 * The name on VLAN interfaces is optional; if there is no
 * /interface/@name, construct a name for the VLAN interface and set
 * /interface/@name in NCF_XML to it. This way, other code can assume we
 * always have a name on the interface.
 */
static char *device_name_from_xml(struct netcf *ncf, xmlDocPtr ncf_xml) {
    xmlXPathContextPtr context = NULL;
	xmlXPathObjectPtr obj = NULL;
    char *result = NULL;

	context = xmlXPathNewContext(ncf_xml);
    ERR_NOMEM(context == NULL, ncf);

	obj = xmlXPathEvalExpression(BAD_CAST "string(/interface/@name)", context);
    ERR_NOMEM(obj == NULL, ncf);
    assert(obj->type == XPATH_STRING);

    if (xmlStrlen(obj->stringval) == 0) {
        xmlXPathFreeObject(obj);
        obj = xmlXPathEvalExpression(BAD_CAST
         "concat(/interface/vlan/interface/@name, '.', /interface/vlan/@tag)",
        context);
        ERR_NOMEM(obj == NULL, ncf);
        ERR_COND_BAIL(xmlStrlen(obj->stringval) == 0, ncf, EINTERNAL);
        assert(obj->type == XPATH_STRING);

        xmlNodePtr iface;
        iface = xmlDocGetRootElement(ncf_xml);
        ERR_COND_BAIL(iface == NULL, ncf, EINTERNAL);
        xmlSetProp(iface, BAD_CAST "name", BAD_CAST result);
    }

    result = (char *) xmlStrdup(obj->stringval);
 error:
    xmlXPathFreeObject(obj);
    xmlXPathFreeContext(context);
    return result;
}

/* The device NAME is a bond if it is mentioned as the MASTER in sopme
 * other devices config file
 */
static bool is_bond(struct netcf *ncf, const char *name) {
    int nmatches = 0;

    nmatches = aug_fmt_match(ncf, NULL,
                             "%s[ BONDING_MASTER = '%s']", ifcfg_path, name);
    return nmatches > 0;
}

/* The device NAME is a bridge if it has an entry TYPE=Bridge */
static bool is_bridge(struct netcf *ncf, const char *name) {
    int nmatches = 0;

    nmatches = aug_fmt_match(ncf, NULL,
                             "%s%s/ifcfg-%s[ BRIDGE = 'yes' ]",
                             aug_files, network_scripts_path, name);
    return nmatches > 0;
}

static int bridge_slaves(struct netcf *ncf, const char *name, char ***slaves) {
    struct augeas *aug = NULL;
    int r, nslaves = 0;

    aug = get_augeas(ncf);
    ERR_BAIL(ncf);

    nslaves = aug_fmt_match(ncf, slaves,
                            "%s[ BRIDGE = '%s' ]/DEVICE", ifcfg_path, name);
    ERR_BAIL(ncf);
    for (int i=0; i < nslaves; i++) {
        char *p = (*slaves)[i];
        const char *dev;
        r = aug_get(aug, p, &dev);
        ERR_COND_BAIL(r < 0, ncf, EOTHER);

        (*slaves)[i] = strdup(dev);
        free(p);
        ERR_NOMEM(slaves[i] == NULL, ncf);
    }
    return nslaves;
 error:
    free_matches(nslaves, slaves);
    return -1;
}


/* For an interface NAME, remove the ifcfg-* files for that interface and
 * all its slaves. */
static void rm_interface(struct netcf *ncf, const char *name) {
    int r;
    char *path = NULL;
    char **rules = NULL;
    struct augeas *aug = NULL;
    int nrules = 0;

    aug = get_augeas(ncf);
    ERR_BAIL(ncf);

    /* The last or clause catches slaves of a bond that are enslaved to
     * a bridge NAME */
    r = xasprintf(&path, "%s/%s/ifcfg-%s",
                  aug_files, network_scripts_path, name);
    ERR_NOMEM(r < 0, ncf);

    r = aug_rm(aug, path);
    ERR_COND_BAIL(r < 0, ncf, EOTHER);

    nrules = aug_fmt_match(ncf, &rules, "%s/%s/%s",
                           aug_files, udev_netrule_path, name);
    ERR_COND_BAIL(nrules < 0, ncf, EINTERNAL);

    while(nrules > 0) {
        nrules--;
        r = aug_rm(aug, rules[nrules]);
    }
    free_matches(nrules, &rules);

 error:
    FREE(path);
}

/* Remove all interfaces and their slaves mentioned in NCF_XML.  We need to
 * remove interfaces one by one when we define an interface, since what
 * will become a subinterface may not be related to the new toplevel
 * interface, and calling RM_INTERFACE on the toplevel interface is
 * therefore not enough.
 */
static void rm_all_interfaces(struct netcf *ncf, xmlDocPtr ncf_xml) {
    xmlXPathContextPtr context = NULL;
	xmlXPathObjectPtr obj = NULL;

	context = xmlXPathNewContext(ncf_xml);
    ERR_NOMEM(context == NULL, ncf);

	obj = xmlXPathEvalExpression(BAD_CAST "//interface", context);
    ERR_NOMEM(obj == NULL, ncf);


    xmlNodeSetPtr ns = obj->nodesetval;
    for (int i=0; i < ns->nodeNr; i++) {
        xmlChar *name = xmlGetProp(ns->nodeTab[i], BAD_CAST "name");
        ERR_NOMEM(name == NULL, ncf);
        rm_interface(ncf, (char *) name);
        xmlFree(name);
        ERR_BAIL(ncf);
	}
 error:
    xmlXPathFreeObject(obj);
    xmlXPathFreeContext(context);
}

/* Dig through interface NAME and all its subinterfaces for bonds
 * and either add aliases in modprobe.conf for it (ALIAS == true), or
 * remove such aliases (ALIAS == false)
 */
static void bond_setup(struct netcf *ncf, const char *name, bool alias) {
    void (*setup)(struct netcf *ncf, const char *name);
    int nslaves = 0;
    char **slaves = NULL;

    if (alias)
        setup = modprobed_alias_bond;
    else
        setup = modprobed_unalias_bond;

    if (is_bond(ncf, name)) {
        setup(ncf, name);
        ERR_BAIL(ncf);
    }

    if (is_bridge(ncf, name)) {
        nslaves = bridge_slaves(ncf, name, &slaves);
        ERR_BAIL(ncf);
        for (int i=0; i < nslaves; i++) {
            if (is_bond(ncf, slaves[i])) {
                setup(ncf, slaves[i]);
                ERR_BAIL(ncf);
            }
        }
    }

 error:
    free_matches(nslaves, &slaves);
    return;
}

struct netcf_if *drv_define(struct netcf *ncf, const char *xml_str) {
    xmlDocPtr ncf_xml = NULL, aug_xml = NULL;
    char *name = NULL;
    struct netcf_if *result = NULL;
    int r;
    struct augeas *aug = get_augeas(ncf);

    ERR_BAIL(ncf);

    ncf_xml = parse_xml(ncf, xml_str);
    ERR_BAIL(ncf);

    rng_validate(ncf, ncf_xml);
    ERR_BAIL(ncf);

    name = device_name_from_xml(ncf, ncf_xml);
    ERR_COND_BAIL(name == NULL, ncf, EINTERNAL);

    rm_all_interfaces(ncf, ncf_xml);
    ERR_BAIL(ncf);

    aug_xml = apply_stylesheet(ncf, ncf->driver->get, ncf_xml);
    ERR_BAIL(ncf);

    aug_put_xml(ncf, aug_xml);
    ERR_BAIL(ncf);

    bond_setup(ncf, name, true);
    ERR_BAIL(ncf);

    r = aug_save(aug);
    if (r < 0 && NCF_DEBUG(ncf)) {
        fprintf(stderr, "Errors from aug_save:\n");
        aug_print(aug, stderr, "/augeas//error");
    }
    ERR_THROW(r < 0, ncf, EOTHER, "aug_save failed");

    result = make_netcf_if(ncf, name);
    ERR_BAIL(ncf);

 done:
    xmlFreeDoc(ncf_xml);
    xmlFreeDoc(aug_xml);
    return result;
 error:
    unref(result, netcf_if);
    goto done;
}

int drv_undefine(struct netcf_if *nif) {
    struct augeas *aug = NULL;
    struct netcf *ncf = nif->ncf;
    int r;

    aug = get_augeas(ncf);
    ERR_BAIL(ncf);

    bond_setup(ncf, nif->name, false);
    ERR_BAIL(ncf);

    rm_interface(ncf, nif->name);
    ERR_BAIL(ncf);

    r = aug_save(aug);
    ERR_COND_BAIL(r < 0, ncf, EOTHER);

    return 0;
 error:
    return -1;
}

int drv_lookup_by_mac_string(struct netcf *ncf, const char *mac,
                             int maxifaces, struct netcf_if **ifaces)
{
    struct augeas *aug = NULL;
    char *path = NULL, *ifcfg = NULL;
    const char **names = NULL;
    int nmatches = 0;
    char **matches = NULL;
    int r;
    int result = -1;

    MEMZERO(ifaces, maxifaces);

    aug = get_augeas(ncf);
    ERR_BAIL(ncf);

    nmatches = aug_match_mac(ncf, mac, &matches);
    ERR_BAIL(ncf);
    if (nmatches == 0) {
        result = 0;
        goto done;
    }

    r = ALLOC_N(names, nmatches);
    ERR_NOMEM(r < 0, ncf);

    int cnt = 0;
    for (int i = 0; i < nmatches; i++) {
        if (!has_ifcfg_file(ncf, matches[i]))
            continue;
        r = xasprintf(&ifcfg, "%s[DEVICE = '%s']", ifcfg_path, matches[i]);
        ERR_NOMEM(r < 0, ncf);

        if (! is_slave(ncf, ifcfg))
            names[cnt++] = matches[i];
        FREE(ifcfg);
    }
    for (int i=0; i < cnt && i < maxifaces; i++) {
        char *name = strdup(names[i]);
        ERR_NOMEM(name == NULL, ncf);
        ifaces[i] = make_netcf_if(ncf, name);
        ERR_BAIL(ncf);
    }
    result = cnt;
    goto done;

 error:
    for (int i=0; i < maxifaces; i++)
        unref(ifaces[i], netcf_if);
 done:
    free(names);
    free(ifcfg);
    free(path);
    free_matches(nmatches, &matches);
    return result;
}

const char *drv_mac_string(struct netcf_if *nif) {
    struct netcf *ncf = nif->ncf;
    const char *mac;
    char *path = NULL;
    int r;

    r = aug_get_mac(ncf, nif->name, &mac);
    ERR_THROW(r < 0, ncf, EOTHER, "could not lookup MAC of %s", nif->name);

    if (mac != NULL) {
        if (nif->mac == NULL || STRNEQ(nif->mac, mac)) {
            FREE(nif->mac);
            nif->mac = strdup(mac);
            ERR_NOMEM(nif->mac == NULL, ncf);
        }
    } else {
        FREE(nif->mac);
    }
    /* fallthrough intentional */
 error:
    FREE(path);
    return nif->mac;
}

/*
 * Bringing interfaces up/down
 */

int drv_if_up(struct netcf_if *nif) {
    static const char *const ifup = "ifup";
    struct netcf *ncf = nif->ncf;
    char **slaves = NULL;
    int nslaves = 0;
    int result = -1;

    if (is_bridge(ncf, nif->name)) {
        /* Bring up bridge slaves before the bridge */
        nslaves = bridge_slaves(ncf, nif->name, &slaves);
        ERR_BAIL(ncf);

        for (int i=0; i < nslaves; i++) {
            run1(ncf, ifup, slaves[i]);
            ERR_BAIL(ncf);
        }
    }
    run1(ncf, ifup, nif->name);
    ERR_BAIL(ncf);
    ERR_THROW(!if_is_active(ncf, nif->name), ncf, EOTHER,
              "interface %s failed to become active - "
              "possible disconnected cable.", nif->name);
    result = 0;
 error:
    free_matches(nslaves, &slaves);
    return result;
}

int drv_if_down(struct netcf_if *nif) {
    static const char *const ifdown = "ifdown";
    struct netcf *ncf = nif->ncf;
    char **slaves = NULL;
    int nslaves = 0;
    int result = -1;

    run1(ncf, ifdown, nif->name);
    ERR_BAIL(ncf);
    if (is_bridge(ncf, nif->name)) {
        /* Bring up bridge slaves after the bridge */
        nslaves = bridge_slaves(ncf, nif->name, &slaves);
        ERR_BAIL(ncf);

        for (int i=0; i < nslaves; i++) {
            run1(ncf, ifdown, slaves[i]);
            ERR_BAIL(ncf);
        }
    }
    result = 0;
 error:
    free_matches(nslaves, &slaves);
    return result;
}

/* Functions to take a snapshot of network config (change_begin), and
 * later either revert to that config (change_rollback), or make the
 * new config permanent (change_commit).
 */
int
drv_change_begin(struct netcf *ncf, unsigned int flags)
{
    int result = -1;

    ERR_THROW(flags != 0, ncf, EOTHER, "unsupported flags value %d", flags);
    run1(ncf, NETCF_TRANSACTION, "change-begin");
    ERR_BAIL(ncf);
    result = 0;
error:
    return result;
}

int
drv_change_rollback(struct netcf *ncf, unsigned int flags)
{
    int result = -1;

    ERR_THROW(flags != 0, ncf, EOTHER, "unsupported flags value %d", flags);
    run1(ncf, NETCF_TRANSACTION, "change-rollback");
    ERR_BAIL(ncf);
    result = 0;
error:
    return result;
}

int
drv_change_commit(struct netcf *ncf, unsigned int flags)
{
    int result = -1;

    ERR_THROW(flags != 0, ncf, EOTHER, "unsupported flags value %d", flags);
    run1(ncf, NETCF_TRANSACTION, "change-commit");
    ERR_BAIL(ncf);
    result = 0;
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
