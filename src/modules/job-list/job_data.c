/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* job_data.c - primary struct job helper functions */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libczmqcontainers/czmq_containers.h"

#include "job_data.h"

void job_destroy (void *data)
{
    struct job *job = data;
    if (job) {
        free (job->ranks);
        free (job->nodelist);
        json_decref (job->annotations);
        grudgeset_destroy (job->dependencies);
        json_decref (job->jobspec);
        json_decref (job->jobspec_job);
        json_decref (job->jobspec_cmd);
        json_decref (job->R);
        free (job->eventlog);
        json_decref (job->exception_context);
        zlist_destroy (&job->next_states);
        free (job);
    }
}

struct job *job_create (struct list_ctx *ctx, flux_jobid_t id)
{
    struct job *job = NULL;

    if (!(job = calloc (1, sizeof (*job))))
        return NULL;
    job->ctx = ctx;
    job->id = id;
    job->userid = FLUX_USERID_UNKNOWN;
    job->urgency = -1;
    /* pending jobs that are not yet assigned a priority shall be
     * listed after those who do, so we set the job priority to MIN */
    job->priority = FLUX_JOB_PRIORITY_MIN;
    job->state = FLUX_JOB_STATE_NEW;
    job->states_mask = FLUX_JOB_STATE_NEW;
    job->ntasks = -1;
    job->nnodes = -1;
    job->expiration = -1.0;
    job->wait_status = -1;
    job->result = FLUX_JOB_RESULT_FAILED;

    if (!(job->next_states = zlist_new ())) {
        errno = ENOMEM;
        job_destroy (job);
        return NULL;
    }

    job->states_events_mask = FLUX_JOB_STATE_NEW;
    job->eventlog_seq = -1;
    return job;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
