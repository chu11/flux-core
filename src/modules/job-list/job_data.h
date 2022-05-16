/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_JOB_LIST_JOB_DATA_H
#define _FLUX_JOB_LIST_JOB_DATA_H

#include <flux/core.h>
#include <jansson.h>

#include "job-list.h"
#include "src/common/libutil/grudgeset.h"
#include "src/common/libczmqcontainers/czmq_containers.h"

/* timestamp of when we enter the state
 *
 * associated eventlog entries when restarting
 *
 * t_depend - "submit"
 * t_priority - "priority" (not saved, can be entered multiple times)
 * t_sched - "depend" (not saved, can be entered multiple times)
 * t_run - "alloc"
 * t_cleanup - "finish" or "exception" w/ severity == 0
 * t_inactive - "clean"
 */
struct job {
    struct list_ctx *ctx;

    flux_jobid_t id;
    uint32_t userid;
    int urgency;
    int64_t priority;
    double t_submit;
    // t_depend is identical to t_submit
    // double t_depend;
    double t_run;
    double t_cleanup;
    double t_inactive;
    flux_job_state_t state;
    unsigned int states_mask;
    const char *name;
    int ntasks;
    int nnodes;
    char *ranks;
    char *nodelist;
    double expiration;
    int wait_status;
    bool success;
    bool exception_occurred;
    int exception_severity;
    const char *exception_type;
    const char *exception_note;
    flux_job_result_t result;
    json_t *annotations;
    struct grudgeset *dependencies;

    /* cache of job information */
    json_t *jobspec;
    json_t *jobspec_job;
    json_t *jobspec_cmd;
    json_t *R;
    char *eventlog;
    size_t eventlog_len;
    json_t *exception_context;

    /* Track which states we have seen and have completed transition
     * to.  We do not immediately update to the new state and place
     * onto a new list until we have retrieved any necessary data
     * associated to that state.  For example, when the 'depend' state
     * has been seen, we don't immediately place it on the `pending`
     * list.  We wait until we've retrieved data such as userid,
     * urgency, etc.
     *
     * Track which states we've seen via the states_mask above.
     *
     * Track states seen via events stream in states_events_mask.
     */
    zlist_t *next_states;
    unsigned int states_events_mask;
    void *list_handle;

    int eventlog_seq;           /* last event seq read */
};

void job_destroy (void *data);

struct job *job_create (struct list_ctx *ctx, flux_jobid_t id);

#endif /* ! _FLUX_JOB_LIST_JOB_DATA_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
