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
    CMD_OPT_ARG,       /* Mandatory argument */
    CMD_OPT_PARAM      /* Optional argument  */
};

struct command_opt_def {
    enum command_opt_tag tag;
    const char          *name;
    const char          *help;
};

#define CMD_OPT_DEF_LAST { .tag = CMD_OPT_NONE, .name = NULL }

/* Handlers return one of these */
enum command_result {
    CMD_RES_OK,
    CMD_RES_ERR,
    CMD_RES_ENOMEM,
    CMD_RES_QUIT,
    CMD_RES_UNKNOWN
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

static bool opt_def_is_arg(const struct command_opt_def *def) {
    return def->tag == CMD_OPT_ARG || def->tag == CMD_OPT_PARAM;
}

static const struct command_def *lookup_cmd_def(const char *name) {
    for (int i = 0; commands[i]->name != NULL; i++) {
        if (STREQ(name, commands[i]->name))
            return commands[i];
    }
    return NULL;
}

static int opt_present(const struct command *cmd, const char *name) {
    for (struct command_opt *o = cmd->opt; o != NULL; o = o->next) {
        if (STREQ(o->def->name, name))
            return 1;
    }
    return 0;
}

static const char *param_value(const struct command *cmd, const char *name) {
    for (struct command_opt *o = cmd->opt; o != NULL; o = o->next) {
        if (STREQ(o->def->name, name))
            return o->string;
    }
    return NULL;
}


static const char *arg_value(const struct command *cmd, const char *name) {
    const char *result = param_value(cmd, name);
    if (result == NULL) {
        fprintf(stderr, "internal error: argument without value\n");
        exit(2);
    }
    return result;
}

static int cmd_list(ATTRIBUTE_UNUSED const struct command *cmd) {
    int nint;
    char **intf;
    unsigned int flags = NETCF_IFACE_ACTIVE;

    if (opt_present(cmd, "inactive")) {
        flags = NETCF_IFACE_INACTIVE;
    }
    if (opt_present(cmd, "all")) {
        flags = NETCF_IFACE_ACTIVE | NETCF_IFACE_INACTIVE;
    }

    nint = ncf_num_of_interfaces(ncf, flags);
    if (nint < 0)
        return CMD_RES_ERR;
    if (ALLOC_N(intf, nint) < 0)
        return CMD_RES_ENOMEM;
    nint = ncf_list_interfaces(ncf, nint, intf, flags);
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
    { .tag = CMD_OPT_BOOL, .name = "macs",
      .help = "show MAC addresses" },
    { .tag = CMD_OPT_BOOL, .name = "all",
      .help = "show all (up & down) interfaces" },
    { .tag = CMD_OPT_BOOL, .name = "inactive",
      .help = "show only inactive (down) interfaces" },
    CMD_OPT_DEF_LAST
};

static const struct command_def cmd_list_def = {
    .name = "list",
    .opts = cmd_list_opts,
    .handler = cmd_list,
    .synopsis = "list network interfaces",
    .help = "list the currently configured toplevel network interfaces"
};

static int cmd_dump_xml(const struct command *cmd) {
    char *xml = NULL;
    const char *name = arg_value(cmd, "name");
    struct netcf_if *nif = NULL;
    int maxifaces;
    int result = CMD_RES_ERR;

    if (opt_present(cmd, "mac")) {
        maxifaces = ncf_lookup_by_mac_string(ncf, name, 1, &nif);
        if (maxifaces < 0) {
            fprintf(stderr, "error looking up interface with MAC %s\n",
                    name);
            goto done;
        }
        if (maxifaces > 1) {
            fprintf(stderr,
                    "warning: %d interfaces have MAC %s, only showing one\n",
                    maxifaces, name);
        }
    } else {
        nif = ncf_lookup_by_name(ncf, name);
    }

    if (nif == NULL) {
        fprintf(stderr,
            "Interface %s does not exist or is not a toplevel interface\n",
            name);
        goto done;
    }

    if (opt_present(cmd, "live")) {
        xml = ncf_if_xml_state(nif);
    } else {
        xml = ncf_if_xml_desc(nif);
    }
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
    { .tag = CMD_OPT_BOOL, .name = "mac",
      .help = "interpret the name as a MAC address" },
    { .tag = CMD_OPT_BOOL, .name = "live",
      .help = "include information about the live interface" },
    { .tag = CMD_OPT_ARG, .name = "name",
      .help = "the name of the interface" },
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
    { .tag = CMD_OPT_ARG, .name = "iface",
      .help = "the name of the interface" },
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
    { .tag = CMD_OPT_ARG, .name = "iface",
      .help = "the name of the interface" },
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
    { .tag = CMD_OPT_ARG, .name = "xmlfile",
      .help = "file containing the XML description of the interface" },
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
    { .tag = CMD_OPT_ARG, .name = "iface",
      .help = "the name of the interface" },
    CMD_OPT_DEF_LAST
};

static const struct command_def cmd_undefine_def = {
    .name = "undefine",
    .opts = cmd_undefine_opts,
    .handler = cmd_undefine,
    .synopsis = "undefine an interface",
    .help = "remove the configuration of an interface"
};

static int cmd_help(const struct command *cmd) {
    const char *name = param_value(cmd, "command");
    if (name == NULL) {
        printf("Commands:\n\n");
        for (int i=0; commands[i]->name != NULL; i++) {
            const struct command_def *def = commands[i];
            printf("    %-10s - %s\n", def->name, def->synopsis);
        }
        printf("\nType 'help <command>' for more information on a command\n\n");
    } else {
        const struct command_def *def = lookup_cmd_def(name);
        const struct command_opt_def *odef = NULL;
        if (def == NULL) {
            fprintf(stderr, "unknown command %s\n", name);
            return CMD_RES_ERR;
        }
        printf("  COMMAND\n");
        printf("    %s - %s\n\n", name, def->synopsis);
        printf("  SYNOPSIS\n");
        printf("    %s", name);
        for (odef = def->opts; odef->name != NULL; odef++) {
            switch(odef->tag) {
            case CMD_OPT_BOOL:
                printf(" [--%s]", odef->name);
                break;
            case CMD_OPT_ARG:
                printf(" <%s>", odef->name);
                break;
            case CMD_OPT_PARAM:
                printf(" [<%s>]", odef->name);
                break;
            default:
                fprintf(stderr,
                        "\ninternal error: illegal option definition %d\n",
                        odef->tag);
                break;
            }
        }
        printf("\n\n");
        printf("  DESCRIPTION\n    %s\n\n", def->help);
        printf("  OPTIONS\n");
        for (odef = def->opts; odef->name != NULL; odef++) {
            const char *help = odef->help;
            if (help == NULL)
                help = "";
            if (odef->tag == CMD_OPT_BOOL) {
                printf("    --%-8s %s\n", odef->name, help);
            } else {
                char buf[100];
                snprintf(buf, sizeof(buf), "<%s>", odef->name);
                printf("    %-10s %s\n", buf, help);
            }
        }
        printf("\n");
    }
    return CMD_RES_OK;
}

static const struct command_opt_def cmd_help_opts[] = {
    { .tag = CMD_OPT_PARAM, .name = "command" },
    CMD_OPT_DEF_LAST
};

static const struct command_def cmd_help_def = {
    .name = "help",
    .opts = cmd_help_opts,
    .handler = cmd_help,
    .synopsis = "print help",
    .help = "list all commands or print details about one command"
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
    .synopsis = "exit the program",
    .help = "exit this interactive program"
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
    int narg = 0, nparam = 0;
    const struct command_opt_def *def;

    MEMZERO(cmd, 1);
    tok = nexttoken(&line);
    cmd->def = lookup_cmd_def(tok);
    if (cmd->def == NULL) {
        fprintf(stderr, "Unknown command '%s'\n", tok);
        return -1;
    }
    for (def = cmd->def->opts; def->name != NULL; def ++) {
        if (def->tag == CMD_OPT_ARG) {
            if (nparam > 0) {
                fprintf(stderr,
                    "internal error: mandatory argument after optional one\n");
                exit(2);
            }
            narg++;
        }
        if (def->tag == CMD_OPT_PARAM) nparam++;
    }

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
            if (curarg >= narg + nparam) {
                fprintf(stderr,
                 "Too many arguments. Command %s takes only %d arguments\n",
                  cmd->def->name, narg + nparam);
                return -1;
            }
            for (def = cmd->def->opts;
                 def->name != NULL && !opt_def_is_arg(def);
                 def++);
            for (int i=0; i < curarg; i++)
                for (; def->name != NULL && !opt_def_is_arg(def); def++);
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
    &cmd_help_def,
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
    fprintf(stderr, "Usage: %s [OPTIONS]\n", progname);
    fprintf(stderr, "Interactive shell for netcf\n\n");
    fprintf(stderr, "Type 'help' at the prompt to get a list of commands\n");
    fprintf(stderr, "\nOptions:\n\n");
    fprintf(stderr,
            "  -r, --root ROOT    use ROOT as the root of the filesystem\n\n");
    fprintf(stderr,
            "  -d, --debug        Show debugging output\n\n");

    exit(EXIT_FAILURE);
}

static void parse_opts(int argc, char **argv) {
    int opt;
    struct option options[] = {
        { "help",      0, 0, 'h' },
        { "root",      1, 0, 'r' },
        { "debug",     0, 0, 'd' },
        { 0, 0, 0, 0}
    };
    int idx;

    while ((opt = getopt_long(argc, argv, "+dhr:", options, &idx)) != -1) {
        switch(opt) {
        case 'd':
            setenv("NETCF_DEBUG", "1", 1);
            break;
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

static int run_command_line(const char *line, int *cmdstatus)
{
    struct command cmd;
    char *dup_line;
    int ret = 0;

    MEMZERO(&cmd, 1);

    dup_line = strdup(line);
    if (dup_line == NULL) {
        fprintf(stderr, "Out of memory\n");
        return -1;
    }

    if (parseline(&cmd, dup_line) == 0) {
        *cmdstatus = cmd.def->handler(&cmd);
        switch (*cmdstatus) {
        case CMD_RES_OK:
            ret = 0;
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
            ret = 0;
            break;
        }
    } else {
        ret = -1;
        *cmdstatus = CMD_RES_UNKNOWN;
    }

    free(dup_line);
    cmd.def = NULL;
    while (cmd.opt != NULL) {
        struct command_opt *del = cmd.opt;
        cmd.opt = del->next;
        free(del);
    }

    return ret;
}

static int main_loop(void) {
    char *line;
    int ret;

    while(1) {
        int cmdret;
        int cmdstatus = 0;

        line = readline("ncftool> ");
        if (line == NULL) {
            return ret;
        }

        cmdret = run_command_line(line, &cmdstatus);
        if (ret == 0 && cmdstatus == CMD_RES_QUIT)
            return ret;

        add_history(line);
        ret = cmdret;
    }
}

int main(int argc, char **argv) {
    int r = 0;

    setlocale(LC_ALL, "");

    parse_opts(argc, argv);

    if (ncf_init(&ncf, root) < 0) {
        fprintf(stderr, "Failed to initialize netcf\n");
        if (ncf != NULL)
            print_netcf_error();
        exit(EXIT_FAILURE);
    }
    readline_init();
    if (optind < argc) {
        /* Run a single command */
        int i, ignore_status;
        int cmdsize = 0;
        char *cmd;

        for (i = optind; i < argc; ++i) {
            cmdsize += strlen(argv[i]) + 1;
        }

        r = ALLOC_N(cmd, cmdsize + 1);
        if (r < 0) {
            fprintf(stderr, "Out of memory\n");
            exit(EXIT_FAILURE);
        }

        for (i = optind; i < argc; i++) {
            strncat(cmd, argv[i], cmdsize);
            cmdsize -= strlen(argv[i]);
            strncat(cmd, " ", cmdsize--);
        }

        r = run_command_line(cmd, &ignore_status);
        free(cmd);
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
