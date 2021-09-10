/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sdprocess.h"

struct flux_sdprocess {
    flux_t *h;
    char *unitname;
    char **argv;
    char **envv;
    int stdin_fd;
    int stdout_fd;
    int stderr_fd;
};

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

static void sdprocess_destroy (void *data)
{
    if (data) {
        struct flux_sdprocess *fsd = data;
        free (fsd->unitname);
        strv_destroy (fsd->argv);
        strv_destroy (fsd->envv);
        free (fsd);
    }
}

static int strv_len (char **strv)
{
    int len = 0;
    if (strv) {
        char **ptr = strv;
        while (*ptr) {
            len++;
            ptr++;
        }
    }
    return len;
}

static int strv_copy (char **strv, char ***cpyp)
{
    char **cpy = NULL;
    int len = strv_len (strv);

    if (len) {
        int i;
        /* +1 for NULL terminating pointer */
        if (!(cpy = calloc (1, sizeof (char *) * (len + 1))))
            return -1;
        for (i = 0; i < len; i++) {
            if (!(cpy[i] = strdup (strv[i]))) {
                strv_destroy (cpy);
                return -1;
            }
        }
        *cpyp = cpy;
    }
    return 0;
}

static flux_sdprocess_t *sdprocess_create (flux_t *h,
                                           const char *unitname,
                                           char **argv,
                                           char **envv,
                                           int stdin_fd,
                                           int stdout_fd,
                                           int stderr_fd)
{
    struct flux_sdprocess *fsd = NULL;

    if (!(fsd = calloc (1, sizeof (*fsd))))
        return NULL;
    if (!(fsd->unitname = strdup (unitname)))
        goto cleanup;
    if (strv_copy (argv, &fsd->argv) < 0)
        goto cleanup;
    if (strv_copy (envv, &fsd->envv) < 0)
        goto cleanup;
    fsd->h = h;
    fsd->stdin_fd = stdin_fd;
    fsd->stdout_fd = stdout_fd;
    fsd->stderr_fd = stderr_fd;
    return fsd;

 cleanup:
    sdprocess_destroy (fsd);
    return NULL;
}

flux_sdprocess_t *flux_sdprocess_exec (flux_t *h,
                                       const char *unitname,
                                       char **argv,
                                       char **envv,
                                       int stdin_fd,
                                       int stdout_fd,
                                       int stderr_fd)
{
    struct flux_sdprocess *fsd = NULL;

    if (!unitname
        || !argv) {
        errno = EINVAL;
        return NULL;
    }

    if (!(fsd = sdprocess_create (h,
                                  unitname,
                                  argv,
                                  envv,
                                  stdin_fd,
                                  stdout_fd,
                                  stderr_fd)))
        return NULL;

    if (stdin_fd == 5)
        goto cleanup;

    return fsd;

cleanup:
    sdprocess_destroy (fsd);
    return NULL;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
