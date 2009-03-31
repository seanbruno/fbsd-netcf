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
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
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

/* Augeas should only load the files we are interested in */
static const char *const augeas_xfm[][2] = {
    /* Ifcfg files */
    { "/augeas/load/Netcf_ifcfg/lens", "Shellvars.lns" },
    { "/augeas/load/Netcf_ifcfg/incl",
      "/etc/sysconfig/network-scripts/*" },
    { "/augeas/load/Netcf_ifcfg/excl[1]", "*.augnew" },
    { "/augeas/load/Netcf_ifcfg/excl[2]", "*.augsave" },
    { "/augeas/load/Netcf_ifcfg/excl[3]", "*.rpmsave" },
    { "/augeas/load/Netcf_ifcfg/excl[4]", "*.rpmnew" },
    { "/augeas/load/Netcf_ifcfg/excl[5]", "*~" },
    /* iptables config */
    { "/augeas/load/Iptables/lens", "Iptables.lns" },
    { "/augeas/load/Iptables/incl", "/etc/sysconfig/iptables" },
    /* modprobe config */
    { "/augeas/load/Modprobe/lens", "Modprobe.lns" },
    { "/augeas/load/Modprobe/incl[1]", "/etc/modprobe.d/*" },
    { "/augeas/load/Modprobe/incl[2]", "/etc/modprobe.conf" },
    { "/augeas/load/Modprobe/excl[1]", "*.augnew" },
    { "/augeas/load/Modprobe/excl[2]", "*.augsave" },
    { "/augeas/load/Modprobe/excl[3]", "*.rpmsave" },
    { "/augeas/load/Modprobe/excl[4]", "*.rpmnew" },
    { "/augeas/load/Modprobe/excl[5]", "*~" },
    /* lokkit */
    { "/augeas/load/Lokkit/lens", "Lokkit.lns" },
    { "/augeas/load/Lokkit/incl", "/etc/sysconfig/system-config-firewall" }
};

static const char *const prog_lokkit = "/usr/sbin/lokkit";
static const char *const lokkit_custom_rules =
    "--custom-rules=ipv4:filter:" DATADIR "/netcf/iptables-forward-bridged";

static const char *const prog_rc_d_iptables = "/etc/init.d/iptables";

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
    if (ncf->driver->augeas == NULL) {
        struct augeas *aug;
        int r;
        aug = aug_init(ncf->root, NULL, AUG_NO_LOAD);
        ERR_THROW(aug == NULL, ncf, EOTHER, "aug_init failed");
        ncf->driver->augeas = aug;
        /* Only look at a few config files */
        r = aug_rm(aug, "/augeas/load/*");
        ERR_THROW(r < 0, ncf, EOTHER, "aug_rm failed in get_augeas");

        for (int i=0; i < ARRAY_CARDINALITY(augeas_xfm); i++) {
            r = aug_set(aug, augeas_xfm[i][0], augeas_xfm[i][1]);
            ERR_THROW(r < 0, ncf, EOTHER,
                      "transform setup failed to set %s",
                      augeas_xfm[i][0]);
        }
        r = aug_load(aug);
        ERR_THROW(r < 0, ncf, EOTHER, "failed to load config files");
    }
    return ncf->driver->augeas;
 error:
    aug_close(ncf->driver->augeas);
    ncf->driver->augeas = NULL;
    return NULL;
}

ATTRIBUTE_FORMAT(printf, 4, 5)
static int defnode(struct netcf *ncf, const char *name, const char *value,
                   const char *format, ...) {
    struct augeas *aug = get_augeas(ncf);
    va_list ap;
    char *expr = NULL;
    int r, created;

    va_start(ap, format);
    r = vasprintf (&expr, format, ap);
    va_end (ap);
    if (r < 0)
        expr = NULL;
    ERR_THROW(r < 0, ncf, ENOMEM, "failed to format node expression");

    r = aug_defnode(aug, name, expr, value, &created);
    ERR_THROW(r < 0, ncf, EOTHER, "failed to define node %s", name);

    /* Fallthrough intentional */
 error:
    free(expr);
    return (r < 0) ? -1 : created;
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

/* Ensure we have an iptables rule to bridge physdevs. We take care of both
 * systems using iptables directly, and systems using lokkit (even if it's
 * only installed, but not used)
 */
static void bridge_physdevs(struct netcf *ncf) {
    struct augeas *aug = NULL;
    char *path = NULL, *p = NULL;
    const char *argv[5];
    int have_lokkit, use_lokkit;
    int r, nmatches;

    MEMZERO(argv, ARRAY_CARDINALITY(argv));

    aug = get_augeas(ncf);
    ERR_BAIL(ncf);

    defnode(ncf, "iptables", NULL, "/files/etc/sysconfig/iptables");
    ERR_BAIL(ncf);

    nmatches = aug_match(aug,
      "$iptables/table[ . = 'filter']/*[. = 'FORWARD'][match = 'physdev']", NULL);
    ERR_THROW(nmatches < 0, ncf, EOTHER, "failed to look for bridge");
    if (nmatches > 0)
        return;

    have_lokkit = access(prog_lokkit, X_OK) == 0;
    use_lokkit = aug_match(aug,
      "$iptables/#comment[. = 'Firewall configuration written by system-config-firewall']", NULL);
    ERR_THROW(use_lokkit < 0, ncf, EOTHER, "failed to look for lokkit");

    if (have_lokkit) {
        const char *rules_file = strrchr(lokkit_custom_rules, ':') + 1;
        int created;

        defnode(ncf, "fw", NULL, "/files/etc/sysconfig/system-config-firewall");
        ERR_BAIL(ncf);

        created = defnode(ncf, "fw_custom", rules_file,
                          "$fw/custom-rules[. = '%s']", rules_file);
        ERR_BAIL(ncf);

        if (created) {
            r = aug_set(aug, "$fw_custom", rules_file);
            ERR_COND_BAIL(r < 0, ncf, EOTHER);

            r = aug_set(aug, "$fw_custom/type", "ipv4");
            ERR_COND_BAIL(r < 0, ncf, EOTHER);
            FREE(p);

            r = aug_set(aug, "$fw_custom/table", "filter");
            ERR_COND_BAIL(r < 0, ncf, EOTHER);

            FREE(p);

            r = aug_save(aug);
            ERR_COND_BAIL(r < 0, ncf, EOTHER);
        }
        FREE(path);

        if (use_lokkit) {
            argv[0] = prog_lokkit;
            argv[1] = "--update";
            r = run_program(ncf, argv);
            ERR_BAIL(ncf);
        }
    }

    if (! use_lokkit) {
        defnode(ncf, "ipt_filter", NULL, "$iptables/table[. = 'filter']");
        ERR_BAIL(ncf);

        nmatches = aug_match(aug, "$ipt_filter", NULL);
        ERR_COND_BAIL(nmatches < 0, ncf, EOTHER);
        if (nmatches == 0) {
            r = aug_set(aug, "$ipt_filter", "filter");
            ERR_COND_BAIL(r < 0, ncf, EOTHER);
            r = aug_set(aug, "$ipt_filter/chain[1]", "INPUT");
            ERR_COND_BAIL(r < 0, ncf, EOTHER);
            r = aug_set(aug, "$ipt_filter/chain[1]/policy", "ACCEPT");
            ERR_COND_BAIL(r < 0, ncf, EOTHER);
            r = aug_set(aug, "$ipt_filter/chain[2]", "FORWARD");
            ERR_COND_BAIL(r < 0, ncf, EOTHER);
            r = aug_set(aug, "$ipt_filter/chain[2]/policy", "ACCEPT");
            ERR_COND_BAIL(r < 0, ncf, EOTHER);
            r = aug_set(aug, "$ipt_filter/chain[3]", "OUTPUT");
            ERR_COND_BAIL(r < 0, ncf, EOTHER);
            r = aug_set(aug, "$ipt_filter/chain[3]/policy", "ACCEPT");
            ERR_COND_BAIL(r < 0, ncf, EOTHER);
        } else {
            r = aug_insert(aug, "$ipt_filter/chain[last()]", "append", 0);
            ERR_COND_BAIL(r < 0, ncf, EOTHER);
        }
        r = aug_set(aug, "$ipt_filter/append[1]", "FORWARD");
        r = aug_set(aug, "$ipt_filter/append[1]/match", "physdev");
        ERR_COND_BAIL(r < 0, ncf, EOTHER);
        r = aug_set(aug, "$ipt_filter/append[1]/physdev-is-bridged", NULL);
        ERR_COND_BAIL(r < 0, ncf, EOTHER);
        r = aug_set(aug, "$ipt_filter/append[1]/jump", "ACCEPT");
        ERR_COND_BAIL(r < 0, ncf, EOTHER);

        r = aug_save(aug);
        ERR_COND_BAIL(r < 0, ncf, EOTHER);

        argv[0] = prog_rc_d_iptables;
        argv[1] = "condrestart";
        r = run_program(ncf, argv);
        ERR_BAIL(ncf);
    }
 error:
    free(path);
    free(p);
    return;
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
    /* We undconditionally bridge physdevs; could be more discriminating */
    bridge_physdevs(ncf);
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

static struct netcf_if *make_netcf_if(struct netcf *ncf, char *name) {
    int r;
    struct netcf_if *result = NULL;

    r = make_ref(result);
    ERR_THROW(r < 0, ncf, ENOMEM, NULL);
    result->ncf = ref(ncf);
    result->name = name;
    return result;

 error:
    unref(result, netcf_if);
    return result;
}

struct netcf_if *drv_lookup_by_name(struct netcf *ncf, const char *name) {
    struct netcf_if *nif = NULL;
    char *pathx = NULL;
    char *name_dup = NULL;
    struct augeas *aug;
    int nint;
    int r;

    aug = get_augeas(ncf);
    ERR_BAIL(ncf);

    r = xasprintf(&pathx, "%s[DEVICE = '%s']", ifcfg_path, name);
    ERR_COND_BAIL(r < 0, ncf, ENOMEM);

    nint = aug_get(aug, pathx, NULL);
    ERR_COND_BAIL(nint < 0, ncf, EOTHER);

    if (nint == 0 || is_slave(ncf, pathx))
        goto done;

    name_dup = strdup(name);
    ERR_COND_BAIL(name_dup == NULL, ncf, ENOMEM);

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
static int aug_put_xml(struct netcf *ncf, xmlDocPtr xml) {
    xmlNodePtr forest;
    char *path = NULL, *lpath = NULL, *label = NULL, *value = NULL;
    struct augeas *aug = NULL;
    int result = -1;
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
        int toplevel = 1;
        /* This is a little drastic, since it clears out the file entirely */
        r = aug_rm(aug, path);
        ERR_THROW(r < 0, ncf, EINTERNAL, "aug_rm of '%s' failed", path);
        list_for_each(node, tree->children) {
            label = xml_prop(node, "label");
            value = xml_prop(node, "value");
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
            xmlFree(label);
            xmlFree(value);
            label = value = NULL;
        }
        xmlFree(path);
        path = NULL;
    }
    result = 0;
 error:
    xmlFree(label);
    xmlFree(value);
    xmlFree(path);
    FREE(lpath);
    return result;
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
    char *path = NULL, *result = NULL;
    struct augeas *aug;
    struct netcf *ncf;
    char **intf = NULL;
    xmlDocPtr aug_xml = NULL, ncf_xml = NULL;
    int nint = 0;
    int r;

    ncf = nif->ncf;
    aug = get_augeas(ncf);
    ERR_BAIL(ncf);

    r = xasprintf(&path,
          "%s[ DEVICE = '%s' or BRIDGE = '%s' or MASTER = '%s']",
          ifcfg_path, nif->name, nif->name, nif->name);
    ERR_COND_BAIL(r < 0, ncf, ENOMEM);

    nint = aug_match(aug, path, &intf);
    ERR_THROW(nint < 0, ncf, EINTERNAL,
              "no nodes match '%s'", path);
    FREE(path);

    aug_xml = aug_get_xml(ncf, nint, intf);
    ncf_xml = xsltApplyStylesheet(ncf->driver->put, aug_xml, NULL);

    xmlDocDumpFormatMemory(ncf_xml, (xmlChar **) &result, NULL, 1);

 done:
    FREE(path);
    free_matches(nint, &intf);
    xmlFreeDoc(aug_xml);
    xmlFreeDoc(ncf_xml);
    return result;
 error:
    FREE(result);
    goto done;
}

/* Get the content of /interface/name. Result must be freed with free() */
static char *device_name_from_xml(xmlDocPtr xml) {
    xmlNodePtr iface, name;
    xmlChar *xml_result;
    char *result;

    iface = xmlDocGetRootElement(xml);
    if (iface == NULL) return NULL;

    for (name = iface->children; name != NULL; name = name->next)
        if (xmlStrcmp(name->name, BAD_CAST "name") == 0)
            break;
    if (name == NULL) return NULL;

    xml_result = xmlNodeListGetString(xml, name->children, 1);
    if (xml_result == NULL)
        return NULL;
    result = strdup((char *) xml_result);
    xmlFree(xml_result);
    return result;
}

/* The device NAME is a bond if it is mentioned as the MASTER in sopme
 * other devices config file
 */
static bool is_bond(struct netcf *ncf, const char *name) {
    char *path = NULL;
    struct augeas *aug = get_augeas(ncf);
    int r, nmatches = 0;

    r = xasprintf(&path, "%s[ MASTER = '%s']", ifcfg_path, name);
    ERR_COND_BAIL(r < 0, ncf, ENOMEM);
    nmatches = aug_match(aug, path, NULL);
    free(path);
 error:
    return nmatches > 0;
}

/* Add an 'alias NAME bonding' to an appropriate file in /etc/modprobe.d,
 * if none exists yet. If we need to create a new one, it goes into the
 * file netcf.conf.
 */
static void modprobe_alias_bond(struct netcf *ncf, const char *name) {
    char *path = NULL;
    struct augeas *aug = get_augeas(ncf);
    int r, nmatches;

    r = xasprintf(&path, "/files/etc/modprobe.d/*/alias[ . = '%s']", name);
    ERR_COND_BAIL(r < 0, ncf, ENOMEM);

    nmatches = aug_match(aug, path, NULL);
    ERR_COND_BAIL(nmatches < 0, ncf, EOTHER);

    FREE(path);
    if (nmatches == 0) {
        /* Add a new alias node; this probably deserves better API support
           in Augeas, it's too convoluted */
        r = xasprintf(&path,
                      "/files/etc/modprobe.d/netcf.conf/alias[last()]", name);
        ERR_COND_BAIL(r < 0, ncf, ENOMEM);
        nmatches = aug_match(aug, path, NULL);
        if (nmatches > 0) {
            r = aug_insert(aug, path, "alias", 0);
            ERR_COND_BAIL(r < 0, ncf, EOTHER);
        }
        r = aug_set(aug, path, name);
        FREE(path);
    }

    r = xasprintf(&path,
                  "/files/etc/modprobe.d/*/alias[ . = '%s']/modulename",
                  name);
    ERR_COND_BAIL(r < 0, ncf, ENOMEM);

    r = aug_set(aug, path, "bonding");
    ERR_COND_BAIL(r < 0, ncf, EOTHER);

 error:
    FREE(path);
}

/* Remove the alias for NAME to the bonding module */
static void modprobe_unalias_bond(struct netcf *ncf, const char *name) {
    char *path = NULL;
    struct augeas *aug = get_augeas(ncf);
    int r;

    r = xasprintf(&path,
         "/files/etc/modprobe.d/*/alias[ . = '%s'][modulename = 'bonding']",
                  name);
    ERR_COND_BAIL(r < 0, ncf, ENOMEM);

    r = aug_rm(aug, path);
    ERR_COND_BAIL(r < 0, ncf, EOTHER);
 error:
    FREE(path);
}

struct netcf_if *drv_define(struct netcf *ncf, const char *xml_str) {
    xmlDocPtr ncf_xml = NULL, aug_xml = NULL;
    char *name = NULL, *path = NULL;
    struct netcf_if *result = NULL;
    int r;
    struct augeas *aug = get_augeas(ncf);

    ncf_xml = parse_xml(ncf, xml_str);
    ERR_BAIL(ncf);

    rng_validate(ncf, ncf_xml);
    ERR_BAIL(ncf);

    name = device_name_from_xml(ncf_xml);
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

    aug_put_xml(ncf, aug_xml);
    ERR_BAIL(ncf);

    if (is_bond(ncf, name)) {
        modprobe_alias_bond(ncf, name);
        ERR_BAIL(ncf);
    }

    r = aug_save(aug);
    ERR_THROW(r < 0, ncf, EOTHER, "aug_save failed");

    result = make_netcf_if(ncf, name);
    ERR_BAIL(ncf);

 done:
    free(path);
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
    char *path = NULL;

    aug = get_augeas(ncf);
    ERR_BAIL(ncf);

    if (is_bond(ncf, nif->name)) {
        modprobe_unalias_bond(ncf, nif->name);
        ERR_BAIL(ncf);
    }

    r = xasprintf(&path,
          "%s[ DEVICE = '%s' or BRIDGE = '%s' or MASTER = '%s']",
          ifcfg_path, nif->name, nif->name, nif->name);
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
