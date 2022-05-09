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

#include "job_state.h"

struct job_archive_ctx *job_archive_setup (flux_t *h, int ac, char **av);

void job_archive_ctx_destroy (struct job_archive_ctx *ctx);

int job_archive_store (struct job_archive_ctx *ctx, struct job *job);

#endif /* _FLUX_JOB_ARCHIVE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

