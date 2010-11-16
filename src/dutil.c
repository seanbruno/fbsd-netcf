/*
 * dutil.c: Global utility functions for driver backends.
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
#include <ctype.h>
#include <errno.h>

#include "safe-alloc.h"
#include "ref.h"
#include "list.h"
#include "netcf.h"
#include "dutil.h"

#include <libxml/parser.h>
#include <libxml/relaxng.h>
#include <libxml/tree.h>
#include <libxslt/xslt.h>
#include <libxslt/xsltInternals.h>
#include <libxslt/transform.h>
#include <libxslt/xsltutils.h>

/* Create a new netcf if instance for interface NAME */
struct netcf_if *make_netcf_if(struct netcf *ncf, char *name) {
    int r;
    struct netcf_if *result = NULL;

    r = make_ref(result);
    ERR_NOMEM(r < 0, ncf);
    result->ncf = ref(ncf);
    result->name = name;
    return result;

 error:
    unref(result, netcf_if);
    return result;
}

/* never call this directly. Only call it via "unref(nif, netcf_if)" */
void free_netcf_if(struct netcf_if *nif) {
    if (nif == NULL)
        return;

    assert(nif->ref == 0);
    unref(nif->ncf, netcf);
    free(nif->name);
    free(nif->mac);
    free(nif);
}

/* never call this directly. Only call it via "unref(ncf, netcf)" */
void free_netcf(struct netcf *ncf) {
    if (ncf == NULL)
        return;

    assert(ncf->ref == 0);
    free(ncf->root);
    free(ncf);
}

/* Like asprintf, but set *STRP to NULL on error */
int xasprintf(char **strp, const char *format, ...) {
  va_list args;
  int result;

  va_start (args, format);
  result = vasprintf (strp, format, args);
  va_end (args);
  if (result < 0)
      *strp = NULL;
  return result;
}

/*
 * argv_to_string() is borrowed from libvirt's
 * src/util.c:virArgvToString()
 */
char *
argv_to_string(const char *const *argv) {
    int i;
    size_t len;
    char *ret, *p;

    for (len = 1, i = 0; argv[i]; i++)
        len += strlen(argv[i]) + 1;

    if (ALLOC_N(ret, len) < 0)
        return NULL;
    p = ret;

    for (i = 0; argv[i]; i++) {
        if (i != 0)
            *(p++) = ' ';

        strcpy(p, argv[i]);
        p += strlen(argv[i]);
    }

    *p = '\0';

    return ret;
}

void report_error(struct netcf *ncf, netcf_errcode_t errcode,
                  const char *format, ...) {
    va_list ap;

    va_start(ap, format);
    vreport_error(ncf, errcode, format, ap);
    va_end(ap);
}

void vreport_error(struct netcf *ncf, netcf_errcode_t errcode,
                   const char *format, va_list ap) {
    /* We only remember the first error */
    if (ncf->errcode != NETCF_NOERROR)
        return;
    assert(ncf->errdetails == NULL);

    ncf->errcode = errcode;
    if (format != NULL) {
        if (vasprintf(&(ncf->errdetails), format, ap) < 0)
            ncf->errdetails = NULL;
    }
}


xsltStylesheetPtr parse_stylesheet(struct netcf *ncf,
                                          const char *fname) {
    xsltStylesheetPtr result = NULL;
    char *path = NULL;
    int r;

    r = xasprintf(&path, "%s/xml/%s", ncf->data_dir, fname);
    ERR_NOMEM(r < 0, ncf);

    if (access(path, R_OK) < 0) {
        report_error(ncf, NETCF_EFILE,
                     "Stylesheet %s does not exist or is not readable",
                     path);
        goto error;
    }

    result = xsltParseStylesheetFile(BAD_CAST path);
    ERR_THROW(result == NULL, ncf, EFILE,
              "Could not parse stylesheet %s", path);

 error:
    free(path);
    return result;
}

ATTRIBUTE_FORMAT(printf, 2, 3)
static void apply_stylesheet_error(void *ctx, const char *format, ...) {
    struct netcf *ncf = ctx;
    va_list ap;

    va_start(ap, format);
    vreport_error(ncf, NETCF_EXSLTFAILED, format, ap);
    va_end(ap);
}

xmlDocPtr apply_stylesheet(struct netcf *ncf, xsltStylesheetPtr style,
                           xmlDocPtr doc) {
    xsltTransformContextPtr ctxt;
    xmlDocPtr res = NULL;
    int r;

    ctxt = xsltNewTransformContext(style, doc);
    ERR_NOMEM(ctxt == NULL, ncf);

    xsltSetTransformErrorFunc(ctxt, ncf, apply_stylesheet_error);

    r = xslt_register_exts(ctxt);
    ERR_NOMEM(r < 0, ncf);

    res = xsltApplyStylesheetUser(style, doc, NULL, NULL, NULL, ctxt);
    if ((ctxt->state == XSLT_STATE_ERROR) ||
        (ctxt->state == XSLT_STATE_STOPPED)) {
        xmlFreeDoc(res);
        res = NULL;
        /* Fallback, in case our error handler isn't called */
        report_error(ncf, NETCF_EXSLTFAILED, NULL);
    }

error:
    xsltFreeTransformContext(ctxt);
    return res;
}

char *apply_stylesheet_to_string(struct netcf *ncf, xsltStylesheetPtr style,
                                 xmlDocPtr doc) {
    xmlDocPtr doc_xfm = NULL;
    char *result = NULL;
    int r, result_len;

    doc_xfm = apply_stylesheet(ncf, style, doc);
    ERR_BAIL(ncf);

    r = xsltSaveResultToString((xmlChar **) &result, &result_len,
                               doc_xfm, style);
    ERR_NOMEM(r < 0, ncf);
    xmlFreeDoc(doc_xfm);
    return result;

 error:
    FREE(result);
    xmlFreeDoc(doc_xfm);
    return NULL;
}

/* Callback for reporting RelaxNG errors */
void rng_error(void *ctx, const char *format, ...) {
    struct netcf *ncf = ctx;
    va_list ap;

    va_start(ap, format);
    vreport_error(ncf, NETCF_EXMLINVALID, format, ap);
    va_end(ap);
}

xmlRelaxNGPtr rng_parse(struct netcf *ncf, const char *fname) {
    char *path = NULL;
    xmlRelaxNGPtr result = NULL;
    xmlRelaxNGParserCtxtPtr ctxt = NULL;
    int r;

    r = xasprintf(&path, "%s/xml/%s", ncf->data_dir, fname);
    ERR_NOMEM(r < 0, ncf);

    if (access(path, R_OK) < 0) {
        report_error(ncf, NETCF_EFILE,
                     "File %s does not exist or is not readable", path);
        goto error;
    }

    ctxt = xmlRelaxNGNewParserCtxt(path);
    xmlRelaxNGSetParserErrors(ctxt, rng_error, rng_error, ncf);

    result = xmlRelaxNGParse(ctxt);

 error:
    xmlRelaxNGFreeParserCtxt(ctxt);
    free(path);
    return result;
}

void rng_validate(struct netcf *ncf, xmlDocPtr doc) {
	xmlRelaxNGValidCtxtPtr ctxt;
	int r;

	ctxt = xmlRelaxNGNewValidCtxt(ncf->rng);
	xmlRelaxNGSetValidErrors(ctxt, rng_error, rng_error, ncf);

    r = xmlRelaxNGValidateDoc(ctxt, doc);
    if (r != 0 && ncf->errcode == NETCF_NOERROR)
        report_error(ncf, NETCF_EXMLINVALID,
           "Interface definition fails to validate");

	xmlRelaxNGFreeValidCtxt(ctxt);
}

/* Called from SAX on parsing errors in the XML. */
void catch_xml_error(void *ctx, const char *msg ATTRIBUTE_UNUSED, ...) {
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

xmlDocPtr parse_xml(struct netcf *ncf, const char *xml_str) {
    xmlParserCtxtPtr pctxt;
    xmlDocPtr xml = NULL;

    /* Set up a parser context so we can catch the details of XML errors. */
    pctxt = xmlNewParserCtxt();
    ERR_NOMEM(pctxt == NULL || pctxt->sax == NULL, ncf);

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

char *xml_prop(xmlNodePtr node, const char *name) {
    return (char *) xmlGetProp(node, BAD_CAST name);
}

/* Create a new node and link it into the document, even if one of the
 * same name already exists. A NULL return means there was a memory
 * failure, and it needs to be reported by the caller.
 */
xmlNodePtr xml_new_node(xmlDocPtr doc,
                        xmlNodePtr parent, const char *name) {
    xmlNodePtr cur, ret = NULL;

    ret = xmlNewDocNode(doc, NULL, BAD_CAST name, NULL);
    if (ret != NULL) {
        cur = xmlAddChild(parent, ret);
        if (cur == NULL) {
            xmlFreeNode(ret);
            ret = NULL;
        }
    }
    return ret;
}

/* Find existing node of given name within parent, or create and link
 * in a new one if not found.
 */
xmlNodePtr xml_node(xmlDocPtr doc,
                    xmlNodePtr parent, const char *name) {
    xmlNodePtr cur, ret = NULL;

    for (cur = parent->children; cur != NULL; cur = cur->next) {
        if ((cur->type == XML_ELEMENT_NODE)
            && xmlStrEqual(cur->name, BAD_CAST name)) {
            ret = cur;
            break;
        }
    }
    if (ret == NULL) {
        /* node not found, create a new one */
        ret = xml_new_node(doc, parent, name);
    }
    return ret;
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
