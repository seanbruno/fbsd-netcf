/*
 * drv_initscripts.c: the initscripts backend for netcf
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

#include <config.h>
#include <internal.h>

#include <augeas.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "safe-alloc.h"
#include "ref.h"

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxslt/xslt.h>
#include <libxslt/xsltInternals.h>
#include <libxslt/transform.h>

static const char *const ifcfg_path =
    "/files/etc/sysconfig/network-scripts/*";

struct driver {
    struct augeas     *augeas;
    xsltStylesheetPtr  put;
    xsltStylesheetPtr  get;
};

/* Entries in a ifcfg file that tell us that the interface
 * is not a toplevel interface
 */
static const char *const subif_paths[] = {
    "MASTER", "BRIDGE"
};

/* Like asprintf, but set *STRP to NULL on error */
static int xasprintf(char **strp, const char *format, ...) {
  va_list args;
  int result;

  va_start (args, format);
  result = vasprintf (strp, format, args);
  va_end (args);
  if (result < 0)
      *strp = NULL;
  return result;
}

static struct augeas *get_augeas(struct netcf *ncf) {
    if (ncf->driver->augeas == NULL)
        ncf->driver->augeas = aug_init(ncf->root, NULL, AUG_NONE);
    ERR_COND(ncf->driver->augeas == NULL, ncf, EOTHER);
    return ncf->driver->augeas;
}

static int aug_submatch(struct netcf *ncf, const char *p1,
                        const char *p2, char ***matches) {
    struct augeas *aug = get_augeas(ncf);
    char *path = NULL;
    int r;

    r = xasprintf(&path, "%s/%s", p1, p2);
    ERR_COND_BAIL(r < 0, ncf, EOTHER);

    r = aug_match(aug, path, matches);
    ERR_COND_BAIL(r < 0, ncf, EOTHER);

    free(path);
    return r;
 error:
    free(path);
    return -1;
}

static void free_matches(int nint, char ***intf) {
    if (*intf != NULL) {
        for (int i=0; i < nint; i++)
            FREE((*intf)[i]);
        FREE(*intf);
    }
}

static int is_slave(struct netcf *ncf, const char *intf) {
    for (int s = 0; s < ARRAY_CARDINALITY(subif_paths); s++) {
        int r;
        r = aug_submatch(ncf, intf, subif_paths[s], NULL);
        if (r != 0)
            return r;
    }
    return 0;
}

static int list_interfaces(struct netcf *ncf, char ***intf) {
    int nint = 0, result = 0;
    struct augeas *aug = NULL;

    aug = get_augeas(ncf);
    ERR_BAIL(ncf);

    /* Look in augeas for all interfaces */
    nint = aug_match(aug, ifcfg_path, intf);
    ERR_COND_BAIL(nint < 0, ncf, EOTHER);
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

static xsltStylesheetPtr parse_stylesheet(struct netcf *ncf,
                                          const char *fname) {
    xsltStylesheetPtr result = NULL;
    char *path = NULL;
    int r;

    r = xasprintf(&path, "%s/xml/%s", ncf->data_dir, fname);
    ERR_COND_BAIL(r < 0, ncf, ENOMEM);

    // FIXME: Error checking ??
    result = xsltParseStylesheetFile(BAD_CAST path);
 error:
    free(path);
    return result;
}

int drv_init(struct netcf *ncf) {
    if (ALLOC(ncf->driver) < 0)
        return -1;
    xsltInit();
    ncf->driver->get = parse_stylesheet(ncf, "initscripts-get.xsl");
    ncf->driver->put = parse_stylesheet(ncf, "initscripts-put.xsl");
    return 0;
}

void drv_close(struct netcf *ncf) {
    xsltFreeStylesheet(ncf->driver->get);
    xsltFreeStylesheet(ncf->driver->put);
    FREE(ncf->driver);
}

int drv_num_of_interfaces(struct netcf *ncf) {
    int nint = 0;
    char **intf = NULL;

    nint = list_interfaces(ncf, &intf);
    free_matches(nint, &intf);
    return nint;
}

static int list_interface_ids(struct netcf *ncf,
                              int maxnames, char **names,
                              const char *id_attr) {
    struct augeas *aug = NULL;
    int nint = 0, nmatches = 0, result = 0, r;
    char **intf = NULL, **matches = NULL;

    aug = get_augeas(ncf);
    ERR_BAIL(ncf);
    nint = list_interfaces(ncf, &intf);
    for (result = 0; result < nint && result < maxnames; result++) {
        nmatches = aug_submatch(ncf, intf[result], id_attr, &matches);
        if (nmatches > 0) {
            const char *name;
            r = aug_get(aug, matches[nmatches-1], &name);
            ERR_COND_BAIL(r < 0, ncf, EOTHER);
            names[result] = strdup(name);
            ERR_COND_BAIL(names[result] == NULL, ncf, ENOMEM);
        }
        free_matches(nmatches, &matches);
    }
    free_matches(nint, &intf);
    return result;
 error:
    free_matches(nmatches, &matches);
    free_matches(nint, &intf);
    return -1;
}

int drv_list_interfaces(struct netcf *ncf, int maxnames, char **names) {
    return list_interface_ids(ncf, maxnames, names, "DEVICE");
}

int drv_list_interfaces_uuid_string(struct netcf *ncf,
                                    int maxuuids, char **uuids) {
    /* FIXME: This is somewhat bogus. For interfaces we don't manage,
       there's no guarantee that there is a NCF_UUID. In that case, we'd
       have to generate one and save it in the interface file */
    return list_interface_ids(ncf, maxuuids, uuids, "NCF_UUID");
}

struct netcf_if *drv_lookup_by_name(struct netcf *ncf, const char *name) {
    struct netcf_if *nif = NULL;
    char *pathx = NULL;
    struct augeas *aug;
    char **intf = NULL;
    int nint;
    int r;

    aug = get_augeas(ncf);
    ERR_BAIL(ncf);

    r = xasprintf(&pathx, "%s[DEVICE = '%s']", ifcfg_path, name);
    ERR_COND_BAIL(r < 0, ncf, ENOMEM);

    nint = aug_match(aug, pathx, &intf);
    ERR_COND_BAIL(nint < 0 || nint > 1, ncf, EOTHER);

    if (nint == 0 || is_slave(ncf, intf[0]))
        goto done;

    if (make_ref(nif) < 0)
        ERR_COND_BAIL(1, ncf, ENOMEM);
    nif->ncf = ref(ncf);
    nif->path = intf[0];
    intf[0] = NULL;
    goto done;

 error:
    unref(nif, netcf_if);
 done:
    FREE(pathx);
    free_matches(nint, &intf);
    return nif;
}

static xmlDocPtr dump_xml(struct netcf *ncf, int nint, char **intf) {
    struct augeas *aug;
    xmlDocPtr result = NULL;
    xmlNodePtr root = NULL, tree = NULL;
    char **matches = NULL;
    int nmatches, r;

    aug = get_augeas(ncf);
    ERR_BAIL(ncf);

    result = xmlNewDoc(BAD_CAST "1.0");
    root = xmlNewNode(NULL, BAD_CAST "forest");
    xmlDocSetRootElement(result, root);

    for (int i=0; i < nint; i++) {
        tree = xmlNewChild(root, NULL, BAD_CAST "tree", NULL);
        xmlNewProp(tree, BAD_CAST "path", BAD_CAST intf[i]);
        nmatches = aug_submatch(ncf, intf[i], "*", &matches);
        ERR_COND_BAIL(nint < 0, ncf, EOTHER);
        for (int j = 0; j < nmatches; j++) {
            xmlNodePtr node = xmlNewChild(tree, NULL, BAD_CAST "node", NULL);
            const char *value;
            xmlNewProp(node, BAD_CAST "label",
                       BAD_CAST matches[j] + strlen(intf[i]) + 1);
            r = aug_get(aug, matches[j], &value);
            ERR_COND_BAIL(r < 0, ncf, EOTHER);
            xmlNewProp(node, BAD_CAST "value", BAD_CAST value);
        }
        free_matches(nmatches, &matches);
    }

    return result;

 error:
    free_matches(nmatches, &matches);
    xmlFreeDoc(result);
    return NULL;
}

char *drv_xml_desc(struct netcf_if *nif) {
    const char *name;
    char *path = NULL, *result = NULL;
    struct augeas *aug;
    struct netcf *ncf;
    char **intf = NULL;
    xmlDocPtr aug_xml = NULL, ncf_xml = NULL;
    int nint;
    int r;

    ncf = nif->ncf;
    aug = get_augeas(ncf);
    ERR_BAIL(ncf);

    r = xasprintf(&path, "%s/DEVICE", nif->path);
    ERR_COND_BAIL(r < 0, ncf, ENOMEM);

    r = aug_get(aug, path, &name);
    ERR_COND_BAIL(r < 0, ncf, EOTHER);
    FREE(path);

    r = xasprintf(&path,
          "%s[ DEVICE = '%s' or BRIDGE = '%s' or MASTER = '%s']",
          ifcfg_path, name, name, name);
    ERR_COND_BAIL(r < 0, ncf, ENOMEM);

    nint = aug_match(aug, path, &intf);
    ERR_COND_BAIL(nint < 0, ncf, EOTHER);
    FREE(path);

    aug_xml = dump_xml(ncf, nint, intf);
    ncf_xml = xsltApplyStylesheet(ncf->driver->put, aug_xml, NULL);
    xmlFreeDoc(aug_xml);

    xmlDocDumpFormatMemory(ncf_xml, (xmlChar **) &result, NULL, 1);
    xmlFreeDoc(ncf_xml);
    return result;
 error:
    FREE(path);
    free_matches(nint, &intf);
    xmlFreeDoc(aug_xml);
    xmlFreeDoc(ncf_xml);
    return NULL;
}

/*
 * Local variables:
 *  indent-tabs-mode: nil
 *  c-indent-level: 4
 *  c-basic-offset: 4
 *  tab-width: 4
 * End:
 */
