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
#include "list.h"

#include <libxml/parser.h>
#include <libxml/relaxng.h>
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
    xmlRelaxNGPtr      rng;
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
    // FIXME: We should probably have a way for the user to influence
    // the save mode for Augeas (or just settle on a good default)
    if (ncf->driver->augeas == NULL)
        ncf->driver->augeas = aug_init(ncf->root, NULL, AUG_SAVE_BACKUP);
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

/* Callback for reporting RelaxNG errors */
static void rng_error(void *ctx, const char *format, ...) {
    struct netcf *ncf = ctx;
    va_list ap;

    va_start(ap, format);
    vreport_error(ncf, NETCF_EXMLINVALID, format, ap);
    va_end(ap);
}

static xmlRelaxNGPtr rng_parse(struct netcf *ncf, const char *fname) {
    char *path = NULL;
    xmlRelaxNGPtr result = NULL;
    xmlRelaxNGParserCtxtPtr ctxt = NULL;
    int r;

    r = xasprintf(&path, "%s/xml/%s", ncf->data_dir, fname);
    ERR_COND_BAIL(r < 0, ncf, ENOMEM);

    ctxt = xmlRelaxNGNewParserCtxt(path);
    xmlRelaxNGSetParserErrors(ctxt, rng_error, rng_error, ncf);

    result = xmlRelaxNGParse(ctxt);

 error:
    xmlRelaxNGFreeParserCtxt(ctxt);
    free(path);
    return result;
}

static void rng_validate(struct netcf *ncf, xmlDocPtr doc) {
	xmlRelaxNGValidCtxtPtr ctxt;
	int r;

	ctxt = xmlRelaxNGNewValidCtxt(ncf->driver->rng);
	xmlRelaxNGSetValidErrors(ctxt, rng_error, rng_error, ncf);

    r = xmlRelaxNGValidateDoc(ctxt, doc);
    if (r != 0 && ncf->errcode == NETCF_NOERROR)
        report_error(ncf, NETCF_EXMLINVALID,
           "Interface definition fails to validate");

	xmlRelaxNGFreeValidCtxt(ctxt);
}

static char *xml_prop(xmlNodePtr node, const char *name) {
    return (char *) xmlGetProp(node, BAD_CAST name);
}

static const char *device_name(struct netcf_if *nif) {
    struct augeas *aug = NULL;
    char *path = NULL;
    const char *name;
    int r;

    aug = get_augeas(nif->ncf);
    ERR_BAIL(nif->ncf);

    r = xasprintf(&path, "%s/DEVICE", nif->path);
    ERR_COND_BAIL(r < 0, nif->ncf, ENOMEM);

    r = aug_get(aug, path, &name);
    ERR_COND_BAIL(r < 0, nif->ncf, EOTHER);
    FREE(path);
    return name;
 error:
    FREE(path);
    return NULL;
}

int drv_init(struct netcf *ncf) {
    int r;

    if (ALLOC(ncf->driver) < 0)
        return -1;
    // FIXME: Check for errors
    xsltInit();
    r = xslt_ext_register();
    ERR_THROW(r < 0, ncf, EINTERNAL, "xsltRegisterExtModule failed");
    ncf->driver->get = parse_stylesheet(ncf, "initscripts-get.xsl");
    ncf->driver->put = parse_stylesheet(ncf, "initscripts-put.xsl");
    ncf->driver->rng = rng_parse(ncf, "interface.rng");
    return 0;
 error:
    FREE(ncf->driver);
    return -1;
}

void drv_close(struct netcf *ncf) {
    xsltFreeStylesheet(ncf->driver->get);
    xsltFreeStylesheet(ncf->driver->put);
    xslt_ext_unregister();
    xsltCleanupGlobals();
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

static struct netcf_if *make_netcf_if(struct netcf *ncf, char *path) {
    int r;
    struct netcf_if *result = NULL;

    r = make_ref(result);
    ERR_THROW(r < 0, ncf, ENOMEM, NULL);
    result->ncf = ref(ncf);
    result->path = path;
    return result;

 error:
    unref(result, netcf_if);
    return result;
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

    nif = make_netcf_if(ncf, intf[0]);
    ERR_BAIL(ncf);
    intf[0] = NULL;
    goto done;

 error:
    unref(nif, netcf_if);
 done:
    FREE(pathx);
    free_matches(nint, &intf);
    return nif;
}

/* Get an XML desription of the interfaces (just paths, really) in INTF.
 * The format is a very simple representation of the Augeas tree (see
 * xml/augeas.rng)
 */
static xmlDocPtr aug_get_xml(struct netcf *ncf, int nint, char **intf) {
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

/* Write the XML doc in the simple Augeas format into the Augeas tree */
static int aug_put_xml(struct netcf *ncf, xmlDocPtr xml, char **top_path) {
    xmlNodePtr forest;
    char *lpath = NULL;
    struct augeas *aug = NULL;
    int r;

    *top_path = NULL;

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
        char *path = xml_prop(tree, "path");
        int toplevel = 1;
        /* This is a little drastic, since it clears out the file entirely */
        r = aug_rm(aug, path);
        ERR_THROW(r < 0, ncf, EINTERNAL, "aug_rm of '%s' failed", path);
        list_for_each(node, tree->children) {
            char *label = xml_prop(node, "label");
            char *value = xml_prop(node, "value");
            /* We should mark the toplevel interface from the XSLT */
            if (STREQ(label, "BRIDGE") || STREQ(label, "MASTER")) {
                toplevel = 0;
            }
            r = xasprintf(&lpath, "%s/%s", path, label);
            ERR_THROW(r < 0, ncf, ENOMEM, NULL);

            r = aug_set(aug, lpath, value);
            ERR_THROW(r < 0, ncf, EOTHER,
                      "aug_set of '%s' failed", lpath);
            FREE(lpath);
        }
        if (toplevel) {
            ERR_THROW(*top_path != NULL, ncf, EINTERNAL,
                      "multiple toplevel interfaces");
            (*top_path) = strdup(path);
            ERR_THROW(*top_path == NULL, ncf, ENOMEM, NULL);
        }
    }
    ERR_THROW(*top_path == NULL, ncf, EXMLINVALID,
              "no toplevel interface");

    return 0;
 error:
    FREE(lpath);
    FREE(*top_path);
    return -1;
}

/* Called from SAX on parsing errors in the XML. */
static void
catch_xml_error(void *ctx, const char *msg ATTRIBUTE_UNUSED, ...) {
    xmlParserCtxtPtr ctxt = (xmlParserCtxtPtr) ctx;

    if (ctxt != NULL) {
        struct netcf *ncf = ctxt->_private;

        if (ctxt->lastError.level == XML_ERR_FATAL &&
            ctxt->lastError.message != NULL) {
            report_error(ncf, NETCF_EXMLPARSER,
                         "at line %d: %s",
                         ctxt->lastError.line,
                         ctxt->lastError.message);
        }
    }
}

static xmlDocPtr parse_xml(struct netcf *ncf, const char *xml_str) {
    xmlParserCtxtPtr pctxt;
    xmlDocPtr xml = NULL;

    /* Set up a parser context so we can catch the details of XML errors. */
    pctxt = xmlNewParserCtxt();
    ERR_COND_BAIL(pctxt == NULL || pctxt->sax == NULL, ncf, ENOMEM);

    pctxt->sax->error = catch_xml_error;
    pctxt->_private = ncf;

    xml = xmlCtxtReadDoc (pctxt, BAD_CAST xml_str, "netcf.xml", NULL,
                          XML_PARSE_NOENT | XML_PARSE_NONET |
                          XML_PARSE_NOWARNING);
    ERR_THROW(xml == NULL, ncf, EXMLPARSER,
              "failed to parse xml document");
    ERR_THROW(xmlDocGetRootElement(xml) == NULL, ncf, EINTERNAL,
              "missing root element");

    xmlFreeParserCtxt(pctxt);
    return xml;
error:
    xmlFreeParserCtxt (pctxt);
    xmlFreeDoc (xml);
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

    name = device_name(nif);
    ERR_BAIL(ncf);

    r = xasprintf(&path,
          "%s[ DEVICE = '%s' or BRIDGE = '%s' or MASTER = '%s']",
          ifcfg_path, name, name, name);
    ERR_COND_BAIL(r < 0, ncf, ENOMEM);

    nint = aug_match(aug, path, &intf);
    ERR_THROW(nint < 0, ncf, EINTERNAL,
              "no nodes match '%s'", path);
    FREE(path);

    aug_xml = aug_get_xml(ncf, nint, intf);
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

/* Get the content of /interface/name */
static xmlChar *device_name_from_xml(xmlDocPtr xml) {
    xmlNodePtr iface, name;

    iface = xmlDocGetRootElement(xml);
    if (iface == NULL) return NULL;

    for (name = iface->children; name != NULL; name = name->next)
        if (xmlStrcmp(name->name, BAD_CAST "name") == 0)
            break;
    if (name == NULL) return NULL;

    return xmlNodeListGetString(xml, name->children, 1);
}

struct netcf_if *drv_define(struct netcf *ncf, const char *xml_str) {
    xmlDocPtr ncf_xml = NULL, aug_xml = NULL;
    char *nif_path = NULL;
    char *name = NULL, *path = NULL;
    struct netcf_if *result = NULL;
    int r;
    struct augeas *aug = get_augeas(ncf);

    ncf_xml = parse_xml(ncf, xml_str);
    ERR_BAIL(ncf);

    rng_validate(ncf, ncf_xml);
    ERR_BAIL(ncf);

    name = (char *) device_name_from_xml(ncf_xml);
    ERR_COND_BAIL(name == NULL, ncf, EINTERNAL);

    /* Clean out existing definitions */
    r = xasprintf(&path,
          "%s[ DEVICE = '%s' or BRIDGE = '%s' or MASTER = '%s']",
          ifcfg_path, name, name, name);
    ERR_COND_BAIL(r < 0, ncf, ENOMEM);

    r = aug_rm(aug, path);
    ERR_COND_BAIL(r < 0, ncf, EOTHER);

    // FIXME: Check for errors from ApplyStylesheet
    aug_xml = xsltApplyStylesheet(ncf->driver->get, ncf_xml, NULL);
    aug_put_xml(ncf, aug_xml, &nif_path);
    ERR_BAIL(ncf);

    r = aug_save(aug);
    ERR_THROW(r < 0, ncf, EOTHER, "aug_save failed");

    result = make_netcf_if(ncf, nif_path);
    ERR_BAIL(ncf);

 done:
    free(path);
    xmlFree(name);
    xmlFreeDoc(ncf_xml);
    xmlFreeDoc(aug_xml);
    return result;
 error:
    FREE(nif_path);
    unref(result, netcf_if);
    goto done;
}

int drv_undefine(struct netcf_if *nif) {
    struct augeas *aug = NULL;
    struct netcf *ncf = nif->ncf;
    const char *name;
    int r;
    char *path = NULL;

    aug = get_augeas(ncf);
    ERR_BAIL(ncf);

    name = device_name(nif);
    ERR_BAIL(ncf);

    r = xasprintf(&path,
          "%s[ DEVICE = '%s' or BRIDGE = '%s' or MASTER = '%s']",
          ifcfg_path, name, name, name);
    ERR_COND_BAIL(r < 0, ncf, ENOMEM);

    r = aug_rm(aug, path);
    ERR_COND_BAIL(r < 0, ncf, EOTHER);

    r = aug_save(aug);
    ERR_COND_BAIL(r < 0, ncf, EOTHER);

    FREE(path);
    return 0;
 error:
    FREE(path);
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
