/*
 * ncftool.c: comand line interface for ncf
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
#include "netcf.h"
#include "internal.h"
#include "safe-alloc.h"
#include "list.h"
#include "read-file.h"

#include <assert.h>
#include <stdio.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <getopt.h>
#include <limits.h>
#include <stdbool.h>
#include <locale.h>

enum command_opt_tag {
    CMD_OPT_NONE,
    CMD_OPT_BOOL,
    CMD_OPT_ARG
};

struct command_opt_def {
    enum command_opt_tag tag;
    const char          *name;
};

#define CMD_OPT_DEF_LAST { .tag = CMD_OPT_NONE, .name = NULL }

/* Handlers return one of these */
enum command_result {
    CMD_RES_OK,
    CMD_RES_ERR,
    CMD_RES_ENOMEM,
    CMD_RES_QUIT
};

struct command {
    const struct command_def *def;
    struct command_opt *opt;
};

typedef int(*cmd_handler)(const struct command*);

struct command_def {
    const char             *name;
    const struct command_opt_def *opts;
    cmd_handler             handler;
    const char             *synopsis;
    const char             *help;
};

static const struct command_def cmd_def_last =
    { .name = NULL, .opts = NULL, .handler = NULL,
      .synopsis = NULL, .help = NULL };

struct command_opt {
    struct command_opt     *next;
    const struct command_opt_def *def;
    /* Switched on def->tag */
    union {
        bool                    bvalue;  /* CMD_OPT_BOOL */
        char                   *string;  /* CMD_OPT_ARG */
    };
};

/* Global variables */

static const struct command_def const *commands[];

struct netcf *ncf;
static const char *const progname = "ncftool";
const char *root = NULL;

static int opt_present(const struct command *cmd, const char *name) {
    for (struct command_opt *o = cmd->opt; o != NULL; o = o->next) {
        if (STREQ(o->def->name, name))
            return 1;
    }
    return 0;
}

static const char *arg_value(const struct command *cmd, const char *name) {
    for (struct command_opt *o = cmd->opt; o != NULL; o = o->next) {
        if (STREQ(o->def->name, name))
            return o->string;
    }
    assert(0);
}

static int cmd_list(ATTRIBUTE_UNUSED const struct command *cmd) {
    int nint;
    char **intf;

    nint = ncf_num_of_interfaces(ncf);
    if (nint < 0)
        return CMD_RES_ERR;
    if (ALLOC_N(intf, nint) < 0)
        return CMD_RES_ENOMEM;
    nint = ncf_list_interfaces(ncf, nint, intf);
    if (nint < 0)
        return CMD_RES_ERR;
    for (int i=0; i < nint; i++) {
        if (opt_present(cmd, "macs")) {
            struct netcf_if *nif = NULL;
            const char *mac = NULL;
            nif = ncf_lookup_by_name(ncf, intf[i]);
            if (nif == NULL) {
                printf("%-8s lookup failed\n", intf[i]);
                continue;
            }
            mac = ncf_if_mac_string(nif);
            if (mac == NULL) {
                printf("%-8s could not get MAC\n", intf[i]);
                ncf_if_free(nif);
                continue;
            }
            printf("%-8s %s\n", intf[i], mac);
            ncf_if_free(nif);
        } else {
            printf("%s\n", intf[i] == NULL ? "(none)" : intf[i]);
        }
        FREE(intf[i]);
    }
    FREE(intf);
    return CMD_RES_OK;
}

static const struct command_opt_def cmd_list_opts[] = {
    { .tag = CMD_OPT_BOOL, .name = "macs" },
    CMD_OPT_DEF_LAST
};

static const struct command_def cmd_list_def = {
    .name = "list",
    .opts = cmd_list_opts,
    .handler = cmd_list,
    .synopsis = "list network interfaces",
    .help = "list all network interfaces"
};

static int cmd_dump_xml(const struct command *cmd) {
    char *xml = NULL;
    const char *name = arg_value(cmd, "iface");
    struct netcf_if *nif = NULL;
    int result = CMD_RES_ERR;

    if (opt_present(cmd, "mac"))
        nif = ncf_lookup_by_mac_string(ncf, name);
    else
        nif = ncf_lookup_by_name(ncf, name);

    if (nif == NULL) {
        fprintf(stderr,
            "Interface %s does not exist or is not a toplevel interface\n",
            name);
        goto done;
    }

    xml = ncf_if_xml_desc(nif);
    if (xml == NULL)
        goto done;

    printf("%s\n", xml);
    result= CMD_RES_OK;

 done:
    free(xml);
    ncf_if_free(nif);
    return result;
}

static const struct command_opt_def cmd_dump_xml_opts[] = {
    { .tag = CMD_OPT_ARG, .name = "iface" },
    { .tag = CMD_OPT_BOOL, .name = "mac" },
    CMD_OPT_DEF_LAST
};

static const struct command_def cmd_dump_xml_def = {
    .name = "dumpxml",
    .opts = cmd_dump_xml_opts,
    .handler = cmd_dump_xml,
    .synopsis = "dump the XML description of an interface",
    .help = "dump the XML description of an interface"
};

static int cmd_if_up(const struct command *cmd) {
    const char *name = arg_value(cmd, "iface");
    struct netcf_if *nif = NULL;
    int result = CMD_RES_ERR;

    nif = ncf_lookup_by_name(ncf, name);
    if (nif == NULL) {
        fprintf(stderr,
            "Interface %s does not exist or is not a toplevel interface\n",
            name);
        goto done;
    }

    if (ncf_if_up(nif) == 0) {
        fprintf(stderr, "Interface %s successfully brought up\n", name);
        result= CMD_RES_OK;
    } else {
        fprintf(stderr, "Interface %s bring-up failed!\n", name);
    }

 done:
    ncf_if_free(nif);
    return result;
}

static const struct command_opt_def cmd_if_up_opts[] = {
    { .tag = CMD_OPT_ARG, .name = "iface" },
    CMD_OPT_DEF_LAST
};

static const struct command_def cmd_if_up_def = {
    .name = "ifup",
    .opts = cmd_if_up_opts,
    .handler = cmd_if_up,
    .synopsis = "bring up an interface",
    .help = "bring up an interface"
};

static int cmd_if_down(const struct command *cmd) {
    const char *name = arg_value(cmd, "iface");
    struct netcf_if *nif = NULL;
    int result = CMD_RES_ERR;

    nif = ncf_lookup_by_name(ncf, name);
    if (nif == NULL) {
        fprintf(stderr,
            "Interface %s does not exist or is not a toplevel interface\n",
            name);
        goto done;
    }

    if (ncf_if_down(nif) == 0) {
        fprintf(stderr, "Interface %s successfully brought down\n", name);
        result= CMD_RES_OK;
    } else {
        fprintf(stderr, "Interface %s bring-down failed!\n", name);
    }

 done:
    ncf_if_free(nif);
    return result;
}

static const struct command_opt_def cmd_if_down_opts[] = {
    { .tag = CMD_OPT_ARG, .name = "iface" },
    CMD_OPT_DEF_LAST
};

static const struct command_def cmd_if_down_def = {
    .name = "ifdown",
    .opts = cmd_if_down_opts,
    .handler = cmd_if_down,
    .synopsis = "bring down an interface",
    .help = "bring down an interface"
};

static int cmd_define(const struct command *cmd) {
    const char *fname = arg_value(cmd, "xmlfile");
    char *xml;
    size_t length;
    struct netcf_if *nif = NULL;
    int result = CMD_RES_ERR;

    xml = read_file(fname, &length);
    if (xml == NULL) {
        fprintf(stderr, "Failed to read %s\n", fname);
        goto done;
    }
    nif = ncf_define(ncf, xml);
    if (nif == NULL)
        goto done;
    printf("Defined interface %s\n", ncf_if_name(nif));
    result = CMD_RES_OK;

 done:
    free(xml);
    ncf_if_free(nif);
    return result;
}

static const struct command_opt_def cmd_define_opts[] = {
    { .tag = CMD_OPT_ARG, .name = "xmlfile" },
    CMD_OPT_DEF_LAST
};

static const struct command_def cmd_define_def = {
    .name = "define",
    .opts = cmd_define_opts,
    .handler = cmd_define,
    .synopsis = "define an interface from an XML file",
    .help = "define an interface from an XML file"
};

static int cmd_undefine(const struct command *cmd) {
    int r;
    const char *name = arg_value(cmd, "iface");
    struct netcf_if *nif = NULL;

    nif = ncf_lookup_by_name(ncf, name);
    if (nif == NULL)
        return CMD_RES_ERR;

    r = ncf_if_undefine(nif);
    if (r < 0)
        return CMD_RES_ERR;

    printf("%s undefined\n", name);
    ncf_if_free(nif);
    return CMD_RES_OK;
}

static const struct command_opt_def cmd_undefine_opts[] = {
    { .tag = CMD_OPT_ARG, .name = "iface" },
    CMD_OPT_DEF_LAST
};

static const struct command_def cmd_undefine_def = {
    .name = "undefine",
    .opts = cmd_undefine_opts,
    .handler = cmd_undefine,
    .synopsis = "undefine an interface",
    .help = "undefine an interface"
};

static int cmd_quit(ATTRIBUTE_UNUSED const struct command *cmd) {
    return CMD_RES_QUIT;
}

static const struct command_opt_def cmd_quit_opts[] = {
    CMD_OPT_DEF_LAST
};

static const struct command_def cmd_quit_def = {
    .name = "quit",
    .opts = cmd_quit_opts,
    .handler = cmd_quit,
    .synopsis = "quit",
    .help = "quit"
};

static char *nexttoken(char **line) {
    char *r, *s;
    char quot = '\0';

    s = *line;

    while (*s && isblank(*s)) s+= 1;
    if (*s == '\'' || *s == '"') {
        quot = *s;
        s += 1;
    }
    r = s;
    while (*s) {
        if ((quot && *s == quot) || (!quot && isblank(*s)))
            break;
        s += 1;
    }
    if (*s)
        *s++ = '\0';
    *line = s;
    return r;
}

static struct command_opt *
make_command_opt(struct command *cmd, const struct command_opt_def *def) {
    struct command_opt *copt = NULL;
    if (ALLOC(copt) < 0) {
        fprintf(stderr, "Allocation failed\n");
        return NULL;
    }
    copt->def = def;
    list_append(cmd->opt, copt);
    return copt;
}

static int parseline(struct command *cmd, char *line) {
    char *tok;
    int narg;
    const struct command_opt_def *def;

    MEMZERO(cmd, 1);
    tok = nexttoken(&line);
    for (int i = 0; commands[i]->name != NULL; i++) {
        if (STREQ(tok, commands[i]->name)) {
            cmd->def = commands[i];
            break;
        }
    }
    if (cmd->def == NULL) {
        fprintf(stderr, "Unknown command '%s'\n", tok);
        return -1;
    }
    for (narg = 0, def = cmd->def->opts; def->name != NULL; def ++)
        if (def->tag == CMD_OPT_ARG) narg++;

    int curarg = 0;
    while (*line != '\0') {
        tok = nexttoken(&line);
        if (tok[0] == '-') {
            char *opt = tok + 1;

            if (*opt == '-') opt += 1;
            for (def = cmd->def->opts; def->name != NULL; def++) {
                if (STREQ(opt, def->name)) {
                    struct command_opt *copt =
                        make_command_opt(cmd, def);
                    if (copt == NULL)
                        return -1;
                    if (def->tag == CMD_OPT_BOOL) {
                        copt->bvalue = 1;
                    } else {
                        assert(0);
                    }
                    break;
                }
            }
            if (def->name == NULL) {
                fprintf(stderr, "Illegal option %s\n", tok);
            }
        } else {
            if (curarg >= narg) {
                fprintf(stderr,
                 "Too many arguments. Command %s takes only %d arguments\n",
                  cmd->def->name, narg);
                return -1;
            }
            for (def = cmd->def->opts;
                 def->name != NULL && def->tag != CMD_OPT_ARG;
                 def++);
            for (int i=0; i < curarg; i++)
                for (; def->name != NULL && def->tag != CMD_OPT_ARG; def++);
            struct command_opt *opt =
                make_command_opt(cmd, def);
            opt->string = tok;
            curarg += 1;
        }
    }

    if (curarg < narg) {
        fprintf(stderr, "Not enough arguments for %s\n", cmd->def->name);
        return -1;
    }

    return 0;
}

static const struct command_def const *commands[] = {
    &cmd_list_def,
    &cmd_dump_xml_def,
    &cmd_define_def,
    &cmd_undefine_def,
    &cmd_if_up_def,
    &cmd_if_down_def,
    &cmd_quit_def,
    &cmd_def_last
};

static char *readline_command_generator(const char *text, int state) {
    static int current = 0;
    const char *name;

    if (state == 0)
        current = 0;

    rl_completion_append_character = ' ';
    while ((name = commands[current]->name) != NULL) {
        current += 1;
        if (STREQLEN(text, name, strlen(text)))
            return strdup(name);
    }
    return NULL;
}

static char **readline_completion(const char *text, int start,
                                  ATTRIBUTE_UNUSED int end) {
    if (start == 0)
        return rl_completion_matches(text, readline_command_generator);

    return NULL;
}

static void readline_init(void) {
    rl_readline_name = "augtool";
    rl_attempted_completion_function = readline_completion;
}

__attribute__((noreturn))
static void usage(void) {
    fprintf(stderr, "Usage: %s [OPTIONS] [COMMAND]\n", progname);
    fprintf(stderr, "Load the Augeas tree and modify it. If no COMMAND is given, run interactively\n");
    fprintf(stderr, "Run '%s help' to get a list of possible commands.\n",
            progname);
    fprintf(stderr, "\nOptions:\n\n");
    fprintf(stderr, "  -c, --typecheck    typecheck lenses\n");
    fprintf(stderr, "  -b, --backup       preserve originals of modified files with\n"
                    "                     extension '.augsave'\n");
    fprintf(stderr, "  -n, --new          save changes in files with extension '.augnew',\n"
                    "                     leave original unchanged\n");
    fprintf(stderr, "  -r, --root ROOT    use ROOT as the root of the filesystem\n");
    fprintf(stderr, "  -I, --include DIR  search DIR for modules; can be given mutiple times\n");
    fprintf(stderr, "  --nostdinc         do not search the builtin default directories for modules\n");

    exit(EXIT_FAILURE);
}

static void parse_opts(int argc, char **argv) {
    int opt;
    struct option options[] = {
        { "help",      0, 0, 'h' },
        { "root",      1, 0, 'r' },
        { 0, 0, 0, 0}
    };
    int idx;

    while ((opt = getopt_long(argc, argv, "hr:", options, &idx)) != -1) {
        switch(opt) {
        case 'h':
            usage();
            break;
        case 'r':
            root = optarg;
            break;
        default:
            usage();
            break;
        }
    }
}

static void print_netcf_error(void) {
    int errcode;
    const char *errmsg, *details;
    errcode = ncf_error(ncf, &errmsg, &details);
    if (errcode != NETCF_NOERROR) {
        fprintf(stderr, "error: %s\n", errmsg);
        if (details != NULL)
            fprintf(stderr, "error: %s\n", details);
    }
}

static int main_loop(void) {
    struct command cmd;
    char *line;
    int ret = 0;

    MEMZERO(&cmd, 1);
    while(1) {
        char *dup_line;

        line = readline("ncftool> ");
        if (line == NULL) {
            printf("\n");
            return ret;
        }

        dup_line = strdup(line);
        if (dup_line == NULL) {
            fprintf(stderr, "Out of memory\n");
            return -1;
        }

        if (parseline(&cmd, dup_line) == 0) {
            int r;
            r = cmd.def->handler(&cmd);
            switch (r) {
            case CMD_RES_OK:
                break;
            case CMD_RES_ERR:
                print_netcf_error();
                ret = -1;
                break;
            case CMD_RES_ENOMEM:
                fprintf(stderr, "error: allocation failed\n");
                ret = -1;
                break;
            case CMD_RES_QUIT:
                return ret;
            }
            add_history(line);
        }
        free(dup_line);
        cmd.def = NULL;
        while (cmd.opt != NULL) {
            struct command_opt *del = cmd.opt;
            cmd.opt = del->next;
            free(del);
        }
    }
}

int main(int argc, char **argv) {
    int r = 0;

    setlocale(LC_ALL, "");

    parse_opts(argc, argv);

    if (ncf_init(&ncf, root) < 0) {
        fprintf(stderr, "Failed to initialize netcf\n");
        exit(EXIT_FAILURE);
    }
    readline_init();
    if (optind < argc) {
        fprintf(stderr, "warning: ignoring additional arguments\n");
    } else {
        r = main_loop();
    }

    return r == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

/*
 * Local variables:
 *  indent-tabs-mode: nil
 *  c-indent-level: 4
 *  c-basic-offset: 4
 *  tab-width: 4
 * End:
 */
