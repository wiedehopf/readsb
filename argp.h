/*
 * file:        argp.h
 * description: minimal replacement for GNU Argp library
 * Copyright 2011 Peter Desnoyers, Northeastern University
 * Copyright 2022 Matthias Wirth
 *
 * This file is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */
#ifndef __ARGP_H__


/* This only includes the features I've used in my programs to date;
 * in particular, it totally ignores any sort of flag.
 */

#define __VARNOTUSED(V) ((void) V)

#ifndef __error_t_defined
typedef int error_t;
# define __error_t_defined
#endif

// just for compat, this flag is default for this version
#define ARGP_NO_EXIT (0)

struct argp_option {
    char *name;
    int  key;
    char *arg;
    int   flags;
    char *doc;
    int   group;                /* ignored */
};

struct argp_state {
    void *input;
    char *name;
    struct argp *argp;
    int  maxlen;
    int arg_num;
};

struct argp {
    struct argp_option *options;
    error_t (*parser)(int key, char *arg, struct argp_state *state);
    const char *arg_doc;
    const char *prog_doc;
    void *unused1;
    void *unused2;
    void *unused3;
};

void argp_help(struct argp_state *state);
void argp_usage(struct argp_state *state);

char *cleanarg(char *s);
enum {ARGP_KEY_ARG, ARGP_KEY_END, ARGP_ERR_UNKNOWN, ARGP_IN_ORDER};

enum {OPTION_NONE, OPTION_DOC, OPTION_HIDDEN};

int argp_parse(struct argp *argp, int argc, char **argv, int flags, int tmp, void *input);


extern const char *argp_program_version;
extern const char *argp_program_bug_address;


#endif /* __ARGP_H__ */

