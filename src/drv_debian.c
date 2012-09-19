/*
 * drv_initscripts.c: the initscripts backend for netcf
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
 * Author: David Lutterkort <lutter@redhat.com>
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
#include <sys/stat.h>

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

#define NETCF_TRANSACTION "/bin/false"

static const char *const network_interfaces_path =
    "/files/etc/network/interfaces";

/* Augeas should only load the files we are interested in */
static const struct augeas_pv augeas_xfm_common_pv[] = {
    /* Interfaces files */
    { "/augeas/load/Interfaces/lens", "Interfaces.lns" },
    { "/augeas/load/Interfaces/incl",
      "/etc/network/interfaces" },
    { "/augeas/load/Interfaces/excl[1]", "*~" },
    { "/augeas/load/Interfaces/excl[2]", "*.bak" },
    { "/augeas/load/Interfaces/excl[3]", "*.orig" },
    { "/augeas/load/Interfaces/excl[4]", "*.rpmnew" },
    { "/augeas/load/Interfaces/excl[5]", "*.rpmorig" },
    { "/augeas/load/Interfaces/excl[6]", "*.rpmsave" },
    { "/augeas/load/Interfaces/excl[7]", "*.augnew" },
    { "/augeas/load/Interfaces/excl[8]", "*.augsave" },
    { "/augeas/load/Interfaces/excl[9]", "*.dpkg-dist" },
    { "/augeas/load/Interfaces/excl[10]", "*.dpkg-new" },
    { "/augeas/load/Interfaces/excl[11]", "*.dpkg-old" },
    /* modprobe config */
    { "/augeas/load/Modprobe/lens", "Modprobe.lns" },
    { "/augeas/load/Modprobe/incl[1]", "/etc/modprobe.d/*" },
    { "/augeas/load/Modprobe/incl[2]", "/etc/modprobe.conf" },
    { "/augeas/load/Modprobe/excl[1]", "*.augnew" },
    { "/augeas/load/Modprobe/excl[2]", "*.augsave" },
    { "/augeas/load/Modprobe/excl[3]", "*.rpmsave" },
    { "/augeas/load/Modprobe/excl[4]", "*.rpmnew" },
    { "/augeas/load/Modprobe/excl[5]", "*~" },
    { "/augeas/load/Modprobe/excl[6]", "*.dpkg-dist" },
    { "/augeas/load/Modprobe/excl[7]", "*.dpkg-new" },
    { "/augeas/load/Modprobe/excl[8]", "*.dpkg-old" },
    /* sysfs (choice entries from /class/net) */
    { "/augeas/load/Sysfs/lens", "Netcf.id" },
    { "/augeas/load/Sysfs/incl", "/sys/class/net/*/address" }
};

static const struct augeas_xfm_table augeas_xfm_common =
    { .size = ARRAY_CARDINALITY(augeas_xfm_common_pv),
      .pv = augeas_xfm_common_pv };


static int cmpstrp(const void *p1, const void *p2) {
    const char *s1 = * (const char **)p1;
    const char *s2 = * (const char **)p2;
    return strcmp(s1, s2);
}

/* The device NAME is a bond if it is mentioned as the MASTER in sopme
 * other devices config file
 */
static bool is_bond(struct netcf *ncf, const char *name) {
    int nmatches = 0;

    nmatches = aug_fmt_match(ncf, NULL,
                             "%s/iface[. = '%s' and count(./bond_slaves)> 0]",
                             network_interfaces_path,
                             name);
    return nmatches > 0;
}

/* The device NAME is a bridge if it has an entry TYPE=Bridge */
static bool is_bridge(struct netcf *ncf, const char *name) {
    int nmatches = 0;

    nmatches = aug_fmt_match(ncf, NULL,
                             "%s/iface[. = '%s' and count(./bridge_ports)> 0]",
                             network_interfaces_path,
                             name);
    return nmatches > 0;
}

static int interface_deps(struct netcf *ncf, char ***slaves, const char *fmt, ...) {
    struct augeas *aug = NULL;
    char *path = NULL;
    int r, nslaves = 0;
    char **matches = NULL;
    char **tmp;
    int nmatches = 0;
    const char *devs;
    va_list args;

    aug = get_augeas(ncf);
    ERR_BAIL(ncf);

    va_start(args, fmt);
    r = vasprintf(&path, fmt, args);
    va_end(args);
    if (r < 0) {
        path = NULL;
        ERR_NOMEM(1, ncf);
    }

    nmatches = aug_match(aug, path, &matches);
    ERR_COND_BAIL(nmatches < 0, ncf, EOTHER);

    if (!nmatches)
        return 0;

    for (int i = 0 ; i < nmatches ; i++) {
        r = aug_get(aug, matches[i], &devs);
        ERR_COND_BAIL(r < 0, ncf, EOTHER);

        if (strcmp(devs, "none") == 0)
            continue;

        do {
            const char *skip;
            tmp = realloc(*slaves, sizeof(char *) * (nslaves+1));
            ERR_COND_BAIL(!tmp, ncf, ENOMEM);
            *slaves = tmp;
            nslaves++;

            skip = strchr(devs, ' ');
            if (skip) {
                (*slaves)[nslaves-1] = strndup(devs, skip-devs);
                skip++;
            } else {
                (*slaves)[nslaves-1] = strdup(devs);
            }
            ERR_NOMEM((*slaves)[nslaves-1] == NULL, ncf);
            devs = skip;
        } while (devs && *devs);
    }

    free_matches(nmatches, &matches);
    return nslaves;
 error:
    free_matches(nslaves, slaves);
    free_matches(nmatches, &matches);
    return -1;
}

static int bridge_ports(struct netcf *ncf, const char *name, char ***slaves) {
    return interface_deps(ncf, slaves, "%s/iface[. = '%s']/bridge_ports",
                          network_interfaces_path, name);
}

static int bond_slaves(struct netcf *ncf, const char *name, char ***slaves) {
    return interface_deps(ncf, slaves, "%s/iface[. = '%s']/bond_slaves",
                          network_interfaces_path, name);
}

static int all_slaves(struct netcf *ncf, char ***slaves) {
    int nbridge_ports = 0, nbond_slaves = 0;
    char **abridge_ports = NULL, **abond_slaves = NULL;

    nbridge_ports = interface_deps(ncf, &abridge_ports,
                                    "%s/iface/bridge_ports",
                                    network_interfaces_path);
    ERR_BAIL(ncf);

    nbond_slaves = interface_deps(ncf, &abond_slaves,
                                  "%s/iface/bond_slaves",
                                  network_interfaces_path);
    ERR_BAIL(ncf);

    *slaves = malloc(sizeof(char *) * (nbridge_ports + nbond_slaves));
    ERR_COND_BAIL(*slaves == NULL, ncf, ENOMEM);

    for (int i = 0 ; i < nbridge_ports ; i++) {
        (*slaves)[i] = abridge_ports[i];
        abridge_ports[i] = NULL;
    }
    for (int i = 0 ; i < nbond_slaves ; i++) {
        (*slaves)[nbridge_ports+i] = abond_slaves[i];
        abond_slaves[i] = NULL;
    }
    free(abridge_ports);
    free(abond_slaves);

    return nbond_slaves + nbridge_ports;

 error:
    free_matches(nbridge_ports, &abridge_ports);
    free_matches(nbond_slaves, &abond_slaves);
    return -1;
}


static bool is_slave(struct netcf *ncf, const char *intf) {
    bool r = false;
    char **slaves;
    int nslaves = all_slaves(ncf, &slaves);
    ERR_BAIL(ncf);

    for (int i = 0 ; i < nslaves ; i++) {
        if (strcmp(intf, slaves[i]) == 0) {
            r = true;
            break;
        }
    }
    free_matches(nslaves, &slaves);
    return r;

 error:
    return false;
}

static bool has_config(struct netcf *ncf, const char *name) {
    int nmatches;

    nmatches = aug_fmt_match(ncf, NULL,
                             "%s/iface[. = '%s']",
                             network_interfaces_path, name);
    if (nmatches == 0)
        return false;

    return !is_slave(ncf, name);
}



/* Given NDEVS path to DEVICE entries which may contain duplicate devices,
 * produce a list of canonical paths to the interfaces in INTF and return
 * the number of entries. Return -1 on error
 */
static int uniq_device_names(struct netcf *ncf,
                            int ndevs, char **devs,
                            char ***intf) {
    struct augeas *aug;
    int r;
    int ndevnames = 0;
    const char **devnames = NULL;

    aug = get_augeas(ncf);
    ERR_BAIL(ncf);

    /* List unique device names */
    r = ALLOC_N(devnames, ndevs);
    ERR_NOMEM(r < 0, ncf);

    for (int i=0; i < ndevs; i++) {
        const char *name = NULL;
        r = aug_get(aug, devs[i], &name);
        ERR_COND_BAIL(r != 1, ncf, EOTHER);
        int exists = 0;
        for (int j = 0; j < ndevnames; j++)
            if (STREQ(name, devnames[j])) {
                exists = 1;
                break;
            }
        if (!exists)
            devnames[ndevnames++] = name;
    }
    qsort(devnames, ndevnames, sizeof(*devnames), cmpstrp);

    /* Find canonical config for each device name */
    r = ALLOC_N(*intf, ndevnames);
    ERR_NOMEM(r < 0, ncf);
    for (int i= 0; i < ndevnames; i++) {
        (*intf)[i] = strdup(devnames[i]);
        ERR_NOMEM(!(*intf)[i], ncf);
    }

    FREE(devnames);
    return ndevnames;

 error:
    FREE(devnames);
    free_matches(ndevnames, intf);
    return -1;
}

static int list_interfaces(struct netcf *ncf, char ***intf) {
    int result = 0, ndevs;
    char **devs = NULL;

    ndevs = aug_fmt_match(ncf, &devs, "%s/iface", network_interfaces_path);
    ERR_COND_BAIL(ndevs < 0, ncf, EOTHER);

    result = uniq_device_names(ncf, ndevs, devs, intf);
    ERR_BAIL(ncf);

    /* Filter out the interfaces that are slaves/subordinate */
    for (int i = 0; i < result;) {
        if (is_slave(ncf, (*intf)[i])) {
            FREE((*intf)[i]);
            memmove(*intf + i, *intf + i + 1,
                    (result - (i + 1))*sizeof((*intf)[0]));
            result -= 1;
        } else {
            i += 1;
        }
    }

    free_matches(ndevs, &devs);
    return result;

 error:
    free_matches(ndevs, &devs);
    return -1;
}

int drv_init(struct netcf *ncf) {
    int r;
    struct stat stats;

    if (ALLOC(ncf->driver) < 0)
        return -1;

    ncf->driver->ioctl_fd = -1;

    r = add_augeas_xfm_table(ncf, &augeas_xfm_common);
    if (r < 0)
        goto error;

    if (stat(ncf->root, &stats) != 0 || !S_ISDIR(stats.st_mode)) {
        report_error(ncf, NETCF_EFILE,
                     "invalid root '%s' is not a directory", ncf->root);
        goto error;
    }

    // FIXME: Check for errors
    xsltInit();
    exsltStrRegister();
    ncf->driver->get = parse_stylesheet(ncf, "debian-get.xsl");
    ncf->driver->put = parse_stylesheet(ncf, "debian-put.xsl");
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
    int nint = 0, nqualified = 0, result = 0;
    char **intf = NULL;

    ERR_BAIL(ncf);
    nint = list_interfaces(ncf, &intf);
    ERR_BAIL(ncf);
    if (!names) {
        maxnames = nint;    /* if not returning list, ignore maxnames too */
    }
    for (result = 0; (result < nint) && (nqualified < maxnames); result++) {
        int is_qualified = ((flags & (NETCF_IFACE_ACTIVE|NETCF_IFACE_INACTIVE))
                            == (NETCF_IFACE_ACTIVE|NETCF_IFACE_INACTIVE));
        if (!is_qualified) {
            int is_active = if_is_active(ncf, intf[result]);
            if ((is_active && (flags & NETCF_IFACE_ACTIVE))
                || ((!is_active) && (flags & NETCF_IFACE_INACTIVE))) {

                is_qualified = 1;
            }
        }
        if (is_qualified) {
            if (names) {
                names[nqualified] = strdup(intf[result]);
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
    char *name_dup = NULL;
    int results;
    char **names = NULL;
    int maxnames = 0;
    int found = 0;
    int r;

    results = list_interface_ids(ncf, 0, NULL, NETCF_IFACE_ACTIVE|NETCF_IFACE_INACTIVE);
    ERR_BAIL(ncf);

    maxnames = results;
    r = ALLOC_N(names, maxnames);
    ERR_NOMEM(r < 0, ncf);

    results = list_interface_ids(ncf, maxnames, names, NETCF_IFACE_ACTIVE|NETCF_IFACE_INACTIVE);
    ERR_BAIL(ncf);

    for (int i = 0 ; i < results ; i++) {
        if (strcmp(names[i], name) == 0) {
            found = 1;
            break;
        }
    }
    ERR_COND_BAIL(found == 0, ncf, ENOENT);

    name_dup = strdup(name);
    ERR_NOMEM(name_dup == NULL, ncf);

    nif = make_netcf_if(ncf, name_dup);
    ERR_BAIL(ncf);
    goto done;

 error:
    unref(nif, netcf_if);
    FREE(name_dup);
 done:
    free_matches(maxnames, &names);
    return nif;
}

/* Get an XML desription of the interfaces (just paths, really) in INTF.
 * The format is a very simple representation of the Augeas tree (see
 * xml/augeas.rng)
 */

static int aug_get_xml_for_intf(struct netcf *ncf,
                                xmlNodePtr array,
                                const char *name) {
    struct augeas *aug;
    xmlNodePtr element = NULL, node = NULL;
    char **matches = NULL;
    char **intf = NULL;
    int nmatches = 0, nintf = 0, r;

    aug = get_augeas(ncf);
    ERR_BAIL(ncf);


    nintf = aug_fmt_match(ncf, &intf, "%s/%s[. = '%s']",
                          network_interfaces_path, "iface", name);
    ERR_BAIL(ncf);
    for (int i = 0 ; i < nintf ; i++) {
        const char *value = NULL;
        aug_get(aug, intf[i], &value);

        element = xmlNewChild(array, NULL, BAD_CAST "element", NULL);
        xmlNewProp(element, BAD_CAST "key", BAD_CAST name);

        nmatches = aug_fmt_match(ncf, &matches, "%s/%s", intf[i], "*");
        ERR_COND_BAIL(nmatches < 0, ncf, EOTHER);

        for (int j = 0; j < nmatches; j++) {
            node = xmlNewChild(element, NULL, BAD_CAST "node", NULL);
            xmlNewProp(node, BAD_CAST "label",
                       BAD_CAST matches[j] + strlen(intf[i]) + 1);
            r = aug_get(aug, matches[j], &value);
            ERR_COND_BAIL(r < 0, ncf, EOTHER);
            xmlNewProp(node, BAD_CAST "value", BAD_CAST value);
        }

        free_matches(nmatches, &matches);
        nmatches = 0;
    }

    free_matches(nintf, &intf);
    return 0;

 error:
    free_matches(nintf, &intf);
    return -1;
}

static xmlDocPtr aug_get_xml(struct netcf_if *nif) {
    xmlDocPtr result = NULL;
    xmlNodePtr root = NULL, tree = NULL, array = NULL, element = NULL, node = NULL;
    char **matches = NULL;
    int nmatches = 0;
    struct netcf *ncf = nif->ncf;
    char **slaves = NULL;
    int nslaves = 0;
    char **ports = NULL;
    int nports = 0;

    result = xmlNewDoc(BAD_CAST "1.0");
    root = xmlNewNode(NULL, BAD_CAST "forest");
    xmlDocSetRootElement(result, root);

    tree = xmlNewChild(root, NULL, BAD_CAST "tree", NULL);
    xmlNewProp(tree, BAD_CAST "path", BAD_CAST network_interfaces_path);

    nmatches = aug_fmt_match(ncf, &matches, "%s/%s",
                             network_interfaces_path, "auto");
    if (nmatches) {
        free_matches(nmatches, &matches);
        nmatches = 0;
        array = xmlNewChild(tree, NULL, BAD_CAST "array", NULL);
        xmlNewProp(array, BAD_CAST "label", BAD_CAST "auto");
        element = xmlNewChild(array, NULL, BAD_CAST "element", NULL);

        node = xmlNewChild(element, NULL, BAD_CAST "node", NULL);
        xmlNewProp(node, BAD_CAST "value", BAD_CAST nif->name);
    }


    array = xmlNewChild(tree, NULL, BAD_CAST "array", NULL);
    xmlNewProp(array, BAD_CAST "label", BAD_CAST "iface");

    aug_get_xml_for_intf(ncf, array, nif->name);

    if (is_bond(ncf, nif->name)) {
        nslaves = bond_slaves(ncf, nif->name, &slaves);
        ERR_BAIL(ncf);

        for (int i = 0 ; i < nslaves ; i++) {
            aug_get_xml_for_intf(ncf, array, slaves[i]);
            ERR_BAIL(ncf);
        }
        free_matches(nslaves, &slaves);
    } else if (is_bridge(ncf, nif->name)) {
        nports = bridge_ports(ncf, nif->name, &ports);
        ERR_BAIL(ncf);

        for (int i = 0 ; i < nports ; i++) {
            aug_get_xml_for_intf(ncf, array, ports[i]);
            ERR_BAIL(ncf);

            nslaves = bond_slaves(ncf, ports[i], &slaves);
            ERR_BAIL(ncf);

            for (int j = 0 ; j < nslaves ; j++) {
                aug_get_xml_for_intf(ncf, array, slaves[j]);
                ERR_BAIL(ncf);
            }
            free_matches(nslaves, &slaves);
        }
        free_matches(nports, &ports);
    }

    ERR_BAIL(ncf);

    return result;

 error:
    free_matches(nmatches, &matches);
    free_matches(nports, &ports);
    free_matches(nslaves, &slaves);
    xmlFreeDoc(result);
    return NULL;
}

/* Write the XML doc in the simple Augeas format into the Augeas tree */
static int aug_put_xml(struct netcf *ncf, xmlDocPtr xml) {
    xmlNodePtr forest;
    char *path = NULL, *lpath = NULL, *label = NULL, *value = NULL, *key = NULL;
    char *arraylabel = NULL;
    char **matches = NULL;
    int nmatches = 0;
    int result = -1;
    int r;
    int n;

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

        list_for_each(array, tree->children) {
            ERR_THROW(! xmlStrEqual(array->name, BAD_CAST "array"), ncf,
                      EINTERNAL, "expected node labeled 'array', not '%s'",
                      array->name);

            arraylabel = xml_prop(array, "label");

            nmatches = aug_fmt_match(ncf, &matches, "%s/%s[last()]",
                                     network_interfaces_path,
                                     arraylabel);
            ERR_COND_BAIL(nmatches < 0, ncf, EOTHER);
            if (nmatches) {
                char *start = strrchr(matches[nmatches-1], '[');
                if (!start)
                    n = 1;
                else
                    n = strtol(start+1, NULL, 10);
            } else {
                n = 0;
            }
            free_matches(nmatches, &matches);

            /* Iterate over all array elements, inserting the new data */
            list_for_each(element, array->children) {
                ERR_THROW(! xmlStrEqual(element->name, BAD_CAST "element"), ncf,
                          EINTERNAL, "expected node labeled 'element', not '%s'",
                          element->name);

                key = xml_prop(element, "key");

                if (key) {
                    r = aug_fmt_set(ncf, key, "%s/%s[%d]",
                                    network_interfaces_path,
                                    arraylabel, ++n);
                    ERR_COND_BAIL(r < 0, ncf, EOTHER);
                } else {
                    n++;
                }

                list_for_each(node, element->children) {
                    label = xml_prop(node, "label");
                    value = xml_prop(node, "value");

                    r = aug_fmt_set(ncf, value, "%s/%s[%d]/%s[last()+1]",
                                    network_interfaces_path,
                                    arraylabel, n, label ? label : "1");
                    ERR_COND_BAIL(r < 0, ncf, EOTHER);
                    xmlFree(label);
                    xmlFree(value);
                    label = value = NULL;
                }
            }
        }
        xmlFree(path);
        path = NULL;
    }
    result = 0;
 error:
    xmlFree(arraylabel);
    xmlFree(label);
    xmlFree(value);
    xmlFree(path);
    FREE(lpath);
    free_matches(nmatches, &matches);
    return result;
}

/* return the current static configuration (as saved on disk) */
char *drv_xml_desc(struct netcf_if *nif) {
    char *result = NULL;
    struct netcf *ncf;
    xmlDocPtr aug_xml = NULL;

    ncf = nif->ncf;
    aug_xml = aug_get_xml(nif);
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


static void rm_interface(struct netcf *ncf, const char *name)
{
    int r;
    char *path = NULL;

    r = aug_fmt_rm(ncf, "%s/auto[./1 = '%s']",
                   network_interfaces_path, name);
    ERR_COND_BAIL(r < 0, ncf, EOTHER);

    r = aug_fmt_rm(ncf, "%s/iface[. = '%s']",
                   network_interfaces_path, name);
    ERR_COND_BAIL(r < 0, ncf, EOTHER);

 error:
    FREE(path);
}


static void rm_all_interfaces(struct netcf *ncf, xmlDocPtr ncf_xml) {
    xmlXPathContextPtr context = NULL;
	xmlXPathObjectPtr obj = NULL;

	context = xmlXPathNewContext(ncf_xml);
    ERR_NOMEM(context == NULL, ncf);

    obj = xmlXPathEvalExpression(BAD_CAST
                                 "//interface[count(parent::vlan) = 0]",
                                 context);
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
        nslaves = bridge_ports(ncf, name, &slaves);
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
    struct netcf_if *result = NULL;
    xmlDocPtr ncf_xml = NULL, aug_xml = NULL;
    char *name = NULL;
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
    char *path = NULL, *ifcfg = NULL;
    const char **names = NULL;
    int nmatches = 0;
    char **matches = NULL;
    int r;
    int result = -1;

    MEMZERO(ifaces, maxifaces);

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
        if (has_config(ncf, matches[i]))
            names[cnt++] = matches[i];
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
    static const char *const ifup = IFUP;
    struct netcf *ncf = nif->ncf;
    int result = -1;

    run1(ncf, ifup, nif->name);
    ERR_BAIL(ncf);
    ERR_THROW(!if_is_active(ncf, nif->name), ncf, EOTHER,
              "interface %s failed to become active - "
              "possible disconnected cable.", nif->name);
    result = 0;
 error:
    return result;
}

int drv_if_down(struct netcf_if *nif) {
    static const char *const ifdown = IFDOWN;
    struct netcf *ncf = nif->ncf;
    int result = -1;

    run1(ncf, ifdown, nif->name);
    ERR_BAIL(ncf);
    result = 0;
 error:
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
