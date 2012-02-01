/*
 * dutil_fbsd.c: FreeBSD utility functions for driver backends.
 */

#include <config.h>
#include <internal.h>

#include "dutil.h"

struct driver {
    struct augeas     *augeas;
    xsltStylesheetPtr  put;
    xsltStylesheetPtr  get;
    int                ioctl_fd;
    struct nl_handle  *nl_sock;
    struct nl_cache   *link_cache;
    struct nl_cache   *addr_cache;
    unsigned int       load_augeas : 1;
    unsigned int       copy_augeas_xfm : 1;
    unsigned int       augeas_xfm_num_tables;
    const struct augeas_xfm_table **augeas_xfm_tables;
};

/* Get or create the augeas instance from NCF */
struct augeas *get_augeas(struct netcf *ncf);

/* Free matches from aug_match (or aug_submatch) */
void free_matches(int nint, char ***intf);

/* Check if the interface INTF is up using an ioctl call */
int if_is_active(struct netcf *ncf, const char *intf);
