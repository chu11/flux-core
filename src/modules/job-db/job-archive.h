/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_JOB_ARCHIVE_H
#define _FLUX_JOB_ARCHIVE_H

#include <flux/core.h>
#include <sqlite3.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/tstat.h"

#include "job_state.h"

struct job_archive_ctx {
    flux_t *h;
    double period;
    char *dbpath;
    unsigned int busy_timeout;
    flux_watcher_t *w;
    sqlite3 *db;
    sqlite3_stmt *store_stmt;
    double since;
    int kvs_lookup_count;
    tstat_t sqlstore;
};

struct job_archive_ctx *job_archive_setup (flux_t *h, int ac, char **av);

void job_archive_ctx_destroy (struct job_archive_ctx *ctx);

#endif /* _FLUX_JOB_ARCHIVE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

