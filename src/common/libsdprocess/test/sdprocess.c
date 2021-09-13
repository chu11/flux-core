/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "src/common/libtap/tap.h"
#include "src/common/libsdprocess/sdprocess.h"
#include "src/common/libutil/fluid.h"

static struct fluid_generator gen;
static bool fluid_generator_init = false;

static char *get_unitname (void)
{
    fluid_t fluid;
    char *unitname;

    if (!fluid_generator_init) {
        /* XXX initial constant timestamp in fugure for consistent
         * names */
        if (fluid_init (&gen, 0, time (NULL)) < 0)
            BAIL_OUT ("fluid_init");
        fluid_generator_init = true;
    }

    if (fluid_generate (&gen, &fluid) < 0)
        BAIL_OUT ("fluid_generate");

    /* XXX - update unitname in future once assumptions relaxed */
    if (asprintf (&unitname, "sdprocess%ju", (uintmax_t) fluid) < 0)
        BAIL_OUT ("asprintf");

    return unitname;
}

static int cmdline_len (char *cmdline)
{
    char *ptr;
    int len;
    while ((ptr = strtok (cmdline, " "))) {
        len++;
        cmdline = NULL;
    }
    return len;
}

static char **cmdline2strv (char *cmdline)
{
    int i, len = 0;
    char *ptr = NULL;
    char **strv = NULL;

    len = cmdline_len (cmdline);
    if (!len)
        BAIL_OUT ("cmdline no arguments");

    /* for NULL pointer at end */
    len++;

    if (!(strv = calloc (1, sizeof (char *) * len)))
        BAIL_OUT ("calloc");

    i = 0;
    while ((ptr = strtok (cmdline, " "))) {
        if (!(strv[i++] = strdup (ptr)))
            BAIL_OUT ("strdup");
        cmdline = NULL;
    }

    return strv;
}

static void strv_destroy (char **strv)
{
    if (strv) {
        char **ptr = strv;
        while (*ptr) {
            free (*ptr);
            ptr++;
        }
        free (strv);
    }
}

static void test_basic (flux_reactor_t *r)
{
    char *unitname = get_unitname ();
    char **cmdv = cmdline2strv ("/bin/true");
    flux_sdprocess_t *sdp = NULL;
    sdp = flux_sdprocess_local_exec (r,
                                     unitname,
                                     cmdv,
                                     NULL,
                                     STDIN_FILENO,
                                     STDOUT_FILENO,
                                     STDERR_FILENO);
    ok (sdp != NULL,
        "flux_sdprocess_local_exec launched process under systemd");

    strv_destroy (cmdv);
    free (unitname);
}

int main (int argc, char *argv[])
{
    flux_reactor_t *r;

    plan (NO_PLAN);

    if (!getenv ("DBUS_SESSION_BUS_ADDRESS"))
        BAIL_OUT ("DBUS_SESSION_BUS_ADDRESS environment variable not set"); 

    if (!getenv ("XDG_RUNTIME_DIR"))
        BAIL_OUT ("XDG_RUNTIME_DIR environment variable not set"); 

    // Create shared reactor for all tests
    ok ((r = flux_reactor_create (FLUX_REACTOR_SIGCHLD)) != NULL,
        "flux_reactor_create");

    diag ("basic");
    test_basic (r);

    printf ("here\n");
    flux_reactor_destroy (r);
    done_testing ();
    return 0;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
