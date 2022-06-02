/*
 * file:        argp.c
 * description: minimal replacement for GNU Argp library
 * Copyright 2011 Peter Desnoyers, Northeastern University
 * Copyright 2022 Matthias Wirth
 *
 * This file is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include <getopt.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "argp.h"

char *cleanarg(char *s)
{
    char *v = strrchr(s, '/');
    return v ? v+1 : s;
}

void argp_help(struct argp_state *state)
{
    printf("Usage: %s [OPTIONS...] %s\n%s\n\n",
           state->name, state->argp->arg_doc, state->argp->prog_doc);

    struct argp_option *opt;

    for (opt = state->argp->options; opt->name != NULL || opt->doc != NULL; opt++) {
        if (opt->flags == OPTION_HIDDEN)
            continue;
        if (opt->name == NULL) {
            printf("\n%s\n", opt->doc); // help category names
        } else {
            char tmp[80], *p = tmp;
            p += sprintf(tmp, "--%s", opt->name);
            if (opt->arg)
                sprintf(p, "=%s", opt->arg);
            printf("  %-*s%s\n", state->maxlen + 8, tmp, opt->doc);
        }
    }
    printf("  --%-*s%s\n", state->maxlen+6, "help", "Give this help list");
    printf("  --%-*s%s\n", state->maxlen+6, "usage",
           "Give a short usage message");
    printf("\nReport bugs to %s\n", argp_program_bug_address);
}

void argp_usage(struct argp_state *state)
{
    char buf[64 * 1024], *p = buf, *col0 = buf;
    p += sprintf(p, "Usage: %s", state->name);
    int indent = p-buf;
    struct argp_option *opt;

    for (opt = state->argp->options; opt->name != NULL || opt->doc != NULL; opt++) {
        if (opt->flags == OPTION_HIDDEN)
            continue;
        if (opt->name != NULL) {
            p += sprintf(p, " [--%s%s%s]", opt->name, opt->arg ? "=":"",
                    opt->arg ? opt->arg : "");
            if (p-col0 > (78-state->maxlen)) {
                p += sprintf(p, "\n");
                col0 = p;
                p += sprintf(p, "%*s", indent, "");
            }
        }
    }
    sprintf(p, " %s\n", state->argp->arg_doc);
    printf("%s", buf);
}

int argp_parse(struct argp *argp, int argc, char **argv, int flags, int tmp, void *input)
{
    __VARNOTUSED(flags);
    __VARNOTUSED(tmp);
    int n_opts = 0;
    struct argp_state state = {.name = cleanarg(argv[0]),
                               .input = input, .argp = argp};

    state.arg_num = 0;

    /* calculate max "--opt=var" length */
    int max = 0;
    struct argp_option *opt;
    for (opt = argp->options; opt->name != NULL || opt->doc != NULL; opt++) {
        if (opt->name != NULL) {
            int m = strlen(opt->name) + (opt->arg ? 1+strlen(opt->arg) : 0);
            max = (max < m) ? m : max;
        }
        n_opts++;
    }
    state.maxlen = max+2;

    struct option *long_opts = calloc((n_opts+3) * sizeof(*long_opts), 1);

    int argp_opt_index;
    int long_count = 0;

    for (opt = argp->options; opt->name != NULL || opt->doc != NULL; opt++) {
        if (opt->name != NULL) {
            //fprintf(stderr, "%d %s\n", opt->key, opt->name);
            int has_arg = opt->arg != NULL;
            long_opts[long_count].name = opt->name;
            long_opts[long_count].has_arg = has_arg ? required_argument : no_argument;
            long_opts[long_count].flag = &argp_opt_index;
            long_opts[long_count].val = (opt - argp->options); // opt index
            long_count++;
        }
    }

    // deal with version / usage / help stuff
    if (argc >= 2) {
        if (strcmp(argv[1], "-V") == 0 || strcmp(argv[1], "--version") == 0) {
            fprintf(stderr, "%s\n", argp_program_version);
            exit(argc == 2 ? 0 : 1);
        }
        if (strcmp(argv[1], "-?") == 0 || strcmp(argv[1], "--help") == 0) {
            argp_help(&state);
            exit(argc == 2 ? 0 : 1);
        }
        if (strcmp(argv[1], "--usage") == 0) {
            argp_usage(&state);
            exit(argc == 2 ? 0 : 1);
        }

    }

    /* we only accept long arguments - return value is zero, and 'long_index'
     * gives us the index into 'long_opts[]'
     */
    int long_index;
    int c;
    optind = 1;
    while (argp_opt_index = -1, (c = getopt_long(argc, argv, "", long_opts, &long_index)) != -1) {
        if (c == '?') {
            fprintf(stderr, "Try `%s --help' or `%s --usage' for more information.\n", argv[0], argv[0]);
            return 1;
        }
        if (c != 0) {
            fprintf(stderr, "argp.c: unexpected getopt_long return value: %d %c\n", c, c);
            fprintf(stderr, "Try `%s --help' or `%s --usage' for more information.\n", argv[0], argv[0]);
            return 1;
        }
        if (argp_opt_index == -1) {
            fprintf(stderr, "argp.c: unexpected code path re3Oovei\n");
            fprintf(stderr, "Try `%s --help' or `%s --usage' for more information.\n", argv[0], argv[0]);
            return 1;
        }

        //int index = optind - 1;
        //fprintf(stderr, "option %d: %s %s\n", index, argv[index], optarg);
        int res = argp->parser(argp->options[argp_opt_index].key, optarg, &state);
        if (res == ARGP_ERR_UNKNOWN) {
            return 1;
        }
    }

    while (optind < argc) {
        int res = argp->parser(ARGP_KEY_ARG, argv[optind++], &state);
        if (res == ARGP_ERR_UNKNOWN) {
            return 1;
        }
    }
    argp->parser(ARGP_KEY_END, NULL, &state);

    free(long_opts);
    return 0;
}
