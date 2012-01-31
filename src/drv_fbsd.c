/*
 */

#include <config.h>
#include <internal.h>

#include <stdio.h>
#include <stdlib.h>
#include <spawn.h>
#include <stdbool.h>
#include <string.h>

#include "safe-alloc.h"
#include "ref.h"
#include "list.h"
#include "dutil.h"
#include "dutil_fbsd.h"

int drv_init(struct netcf *ncf) {
    return 0;
}


void drv_close(struct netcf *ncf) {

    return;

}


void build_adapter_table(struct netcf *ncf) {

    return;

}

static int list_interface_ids(struct netcf *ncf,
                              int maxnames,
                              char **names, unsigned int flags ATTRIBUTE_UNUSED) {
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
    ERR_THROW(1 == 1, ncf, EOTHER, "not implemented on this platform");
}

const char *drv_mac_string(struct netcf_if *nif) {
    ERR_THROW(1 == 1, ncf, EOTHER, "not implemented on this platform");
}

int drv_if_down(struct netcf_if *nif) {
    ERR_THROW(1 == 1, ncf, EOTHER, "not implemented on this platform");
}

int drv_if_up(struct netcf_if *nif) {
    ERR_THROW(1 == 1, ncf, EOTHER, "not implemented on this platform");
}


struct netcf_if *drv_define(struct netcf *ncf, const char *xml_str ATTRIBUTE_UNUSED) {
    ERR_THROW(1 == 1, ncf, EOTHER, "not implemented on this platform");
}

int drv_undefine(struct netcf_if *nif) {
    ERR_THROW(1 == 1, nif->ncf, EOTHER, "not implemented on this platform");
}


char *drv_xml_desc(struct netcf_if *nif) {
    ERR_THROW(1 == 1, nif->ncf, EOTHER, "not implemented on this platform");
}

char *drv_xml_state(struct netcf_if *nif) {
    ERR_THROW(1 == 1, nif->ncf, EOTHER, "not implemented on this platform");
}

int drv_if_status(struct netcf_if *nif, unsigned int *flags ATTRIBUTE_UNUSED) {
    ERR_THROW(1 == 1, nif->ncf, EOTHER, "not implemented on this platform");
}

int drv_lookup_by_mac_string(struct netcf *ncf,
			     const char *mac ATTRIBUTE_UNUSED,
                             int maxifaces ATTRIBUTE_UNUSED,
			     struct netcf_if **ifaces ATTRIBUTE_UNUSED)
{
    ERR_THROW(1 == 1, ncf, EOTHER, "not implemented on this platform");
}

int
drv_change_begin(struct netcf *ncf, unsigned int flags ATTRIBUTE_UNUSED)
{
    ERR_THROW(1 == 1, ncf, EOTHER, "not implemented on this platform");
}

int
drv_change_rollback(struct netcf *ncf, unsigned int flags ATTRIBUTE_UNUSED)
{
    ERR_THROW(1 == 1, ncf, EOTHER, "not implemented on this platform");
}

int
drv_change_commit(struct netcf *ncf, unsigned int flags ATTRIBUTE_UNUSED)
{
    ERR_THROW(1 == 1, ncf, EOTHER, "not implemented on this platform");
}
