/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* list.c - list jobs */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <jansson.h>
#include <flux/core.h>
#include <assert.h>

#include "src/common/libutil/errno_safe.h"
#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/grudgeset.h"
#include "src/common/libutil/jpath.h"
#include "src/common/libeventlog/eventlog.h"
#include "src/common/librlist/rlist.h"
#include "src/common/libidset/idset.h"


#include "idsync.h"
#include "list.h"
#include "job_util.h"
#include "job_state.h"

json_t *get_job_by_id (struct list_ctx *ctx,
                       job_list_error_t *errp,
                       const flux_msg_t *msg,
                       flux_jobid_t id,
                       json_t *attrs,
                       bool *stall);

/* Filter test to determine if job desired by caller */
bool job_filter (struct job *job, uint32_t userid, int states, int results)
{
    if (!(job->state & states))
        return false;
    if (userid != FLUX_USERID_UNKNOWN && job->userid != userid)
        return false;
    if (job->state & FLUX_JOB_STATE_INACTIVE
        && !(job->result & results))
        return false;
    return true;
}

/* Put jobs from list onto jobs array, breaking if max_entries has
 * been reached. Returns 1 if jobs array is full, 0 if continue, -1
 * one error with errno set:
 *
 * ENOMEM - out of memory
 */
int get_jobs_from_list (json_t *jobs,
                        job_list_error_t *errp,
                        zlistx_t *list,
                        int max_entries,
                        json_t *attrs,
                        uint32_t userid,
                        int states,
                        int results)
{
    struct job *job;

    job = zlistx_first (list);
    while (job) {
        if (job_filter (job, userid, states, results)) {
            json_t *o;
            if (!(o = job_to_json (job, attrs, errp)))
                return -1;
            if (json_array_append_new (jobs, o) < 0) {
                json_decref (o);
                errno = ENOMEM;
                return -1;
            }
            if (json_array_size (jobs) == max_entries)
                return 1;
        }
        job = zlistx_next (list);
    }

    return 0;
}

static int dependency_add (struct job *job,
                           const char *description)
{
    if (grudgeset_add (&job->dependencies, description) < 0
        && errno != EEXIST)
        /*  Log non-EEXIST errors, but it is not fatal */
        flux_log_error (job->ctx->h,
                        "job %ju: dependency-add",
                        (uintmax_t) job->id);
    return 0;
}

static int dependency_remove (struct job *job,
                              const char *description)
{
    int rc = grudgeset_remove (job->dependencies, description);
    if (rc < 0 && errno == ENOENT) {
        /*  No matching dependency is non-fatal error */
        flux_log (job->ctx->h,
                  LOG_DEBUG,
                  "job %ju: dependency-remove '%s' not found",
                  (uintmax_t) job->id,
                  description);
        rc = 0;
    }
    return rc;
}

static int dependency_context_parse (flux_t *h,
                                     struct job *job,
                                     const char *cmd,
                                     json_t *context)
{
    int rc;
    const char *description = NULL;

    if (!context
        || json_unpack (context,
                        "{s:s}",
                        "description", &description) < 0) {
        flux_log (h, LOG_ERR,
                  "job %ju: dependency-%s context invalid",
                  (uintmax_t) job->id,
                  cmd);
        errno = EPROTO;
        return -1;
    }

    if (strcmp (cmd, "add") == 0)
        rc = dependency_add (job, description);
    else if (strcmp (cmd, "remove") == 0)
        rc = dependency_remove (job, description);
    else {
        flux_log (h, LOG_ERR,
                  "job %ju: invalid dependency event: dependency-%s",
                  (uintmax_t) job->id,
                  cmd);
        return -1;
    }
    return rc;
}

static int memo_update (flux_t *h,
                        struct job *job,
                        json_t *o)
{
    if (!o) {
        flux_log (h, LOG_ERR, "%ju: invalid memo context", (uintmax_t) job->id);
        errno = EPROTO;
        return -1;
    }
    if (!job->annotations && !(job->annotations = json_object ())) {
        errno = ENOMEM;
        return -1;
    }
    if (jpath_update (job->annotations, "user", o) < 0
        || jpath_clear_null (job->annotations) < 0)
        return -1;
    if (json_object_size (job->annotations) == 0) {
        json_decref (job->annotations);
        job->annotations = NULL;
    }
    return 0;
}

struct res_level {
    const char *type;
    int count;
    json_t *with;
};

static int parse_res_level (struct list_ctx *ctx,
                            struct job *job,
                            json_t *o,
                            struct res_level *resp)
{
    json_error_t error;
    struct res_level res;

    res.with = NULL;
    /* For jobspec version 1, expect exactly one array element per level.
     */
    if (json_unpack_ex (o, &error, 0,
                        "[{s:s s:i s?o}]",
                        "type", &res.type,
                        "count", &res.count,
                        "with", &res.with) < 0) {
        flux_log (ctx->h, LOG_ERR,
                  "%s: job %ju invalid jobspec: %s",
                  __FUNCTION__, (uintmax_t)job->id, error.text);
        return -1;
    }
    *resp = res;
    return 0;
}

/* Return basename of path if there is a '/' in path.  Otherwise return
 * full path */
static const char *
parse_job_name (const char *path)
{
    char *p = strrchr (path, '/');
    if (p) {
        p++;
        /* user mistake, specified a directory with trailing '/',
         * return full path */
        if (*p == '\0')
            return path;
        return p;
    }
    return path;
}

struct job *sqliterow_2_job (struct list_ctx *ctx, sqlite3_stmt *res)
{
    struct job *job = NULL;
    const char *s;
    char *endptr = NULL;

    if (!(job = calloc (1, sizeof (*job))))
        return NULL;

    job->wait_status = -1;
    job->result = FLUX_JOB_RESULT_FAILED;


    s = (const char *)sqlite3_column_text (res, 0);
    assert (s);
    job->id = (flux_jobid_t)strtoul (s, &endptr, 0);
    flux_log (ctx->h, LOG_DEBUG, "loading job id %ju", (uintmax_t)job->id);
    job->userid = sqlite3_column_int (res, 1);
    job->ranks = strdup ((const char *)sqlite3_column_text (res, 2));
    job->t_submit = sqlite3_column_double (res, 3);
    job->t_run = sqlite3_column_double (res, 4);
    job->t_cleanup = sqlite3_column_double (res, 5);
    job->t_inactive = sqlite3_column_double (res, 6);
    s = (const char *)sqlite3_column_text (res, 7);
    job->eventlog = strdup (s);
    assert (job->eventlog);
    s = (const char *)sqlite3_column_text (res, 8);
    job->jobspec = json_loads (s, 0, NULL);
    assert (job->jobspec);
    s = (const char *)sqlite3_column_text (res, 9);
    job->R = json_loads (s, 0, NULL);
    job->state = FLUX_JOB_STATE_INACTIVE;

    {
        json_error_t error;
        json_t *tasks, *resources, *command, *jobspec_job = NULL;

        if (json_unpack_ex (job->jobspec, &error, 0,
                            "{s:{s:{s?:o}}}",
                            "attributes",
                            "system",
                            "job",
                            &jobspec_job) < 0) {
            flux_log (ctx->h, LOG_ERR,
                      "%s: job %ju invalid jobspec: %s",
                      __FUNCTION__, (uintmax_t)job->id, error.text);
            return NULL;        /* leaking */
        }

        if (jobspec_job) {
            if (!json_is_object (jobspec_job)) {
                flux_log (ctx->h, LOG_ERR,
                          "%s: job %ju invalid jobspec",
                          __FUNCTION__, (uintmax_t)job->id);
                return NULL;        /* leaking */
            }
            job->jobspec_job = json_incref (jobspec_job);
        }

        if (json_unpack_ex (job->jobspec, &error, 0,
                            "{s:o}",
                            "tasks", &tasks) < 0) {
            flux_log (ctx->h, LOG_ERR,
                      "%s: job %ju invalid jobspec: %s",
                      __FUNCTION__, (uintmax_t)job->id, error.text);
            return NULL;        /* leaking */
        }
        if (json_unpack_ex (tasks, &error, 0,
                            "[{s:o}]",
                            "command", &command) < 0) {
            flux_log (ctx->h, LOG_ERR,
                      "%s: job %ju invalid jobspec: %s",
                      __FUNCTION__, (uintmax_t)job->id, error.text);
            return NULL;        /* leaking */
        }

        if (!json_is_array (command)) {
            flux_log (ctx->h, LOG_ERR,
                      "%s: job %ju invalid jobspec",
                      __FUNCTION__, (uintmax_t)job->id);
            return NULL;        /* leaking */
        }

        job->jobspec_cmd = json_incref (command);

        if (job->jobspec_job) {
            if (json_unpack_ex (job->jobspec_job, &error, 0,
                                "{s?:s}",
                                "name", &job->name) < 0) {
                flux_log (ctx->h, LOG_ERR,
                          "%s: job %ju invalid job dictionary: %s",
                          __FUNCTION__, (uintmax_t)job->id, error.text);
                return NULL;        /* leaking */
            }
        }

        /* If user did not specify job.name, we treat arg 0 of the command
         * as the job name */
        if (!job->name) {
            json_t *arg0 = json_array_get (job->jobspec_cmd, 0);
            if (!arg0 || !json_is_string (arg0)) {
                flux_log (ctx->h, LOG_ERR,
                          "%s: job %ju invalid job command",
                          __FUNCTION__, (uintmax_t)job->id);
                /* non fatal error */
                goto outjobspec;
            }
            else {
                job->name = parse_job_name (json_string_value (arg0));
                assert (job->name);
            }
        }

        if (json_unpack_ex (job->jobspec, &error, 0,
                            "{s:o}",
                            "resources", &resources) < 0) {
            flux_log (ctx->h, LOG_ERR,
                      "%s: job %ju invalid jobspec: %s",
                      __FUNCTION__, (uintmax_t)job->id, error.text);
            return NULL;        /* leaking */
        }

        /* Set job->ntasks
         */
        if (json_unpack_ex (tasks, NULL, 0,
                            "[{s:{s:i}}]",
                            "count", "total", &job->ntasks) < 0) {
            int per_slot, slot_count = 0;
            struct res_level res[3];

            if (json_unpack_ex (tasks, NULL, 0,
                                "[{s:{s:i}}]",
                                "count", "per_slot", &per_slot) < 0) {
                flux_log (ctx->h, LOG_ERR,
                          "%s: job %ju invalid jobspec: %s",
                          __FUNCTION__, (uintmax_t)job->id, error.text);
                return NULL; /* leaking */;
            }
            if (per_slot != 1) {
                flux_log (ctx->h, LOG_ERR,
                          "%s: job %ju: per_slot count: expected 1 got %d: %s",
                          __FUNCTION__, (uintmax_t)job->id, per_slot,
                          error.text);
                return NULL; /* leaking */;
            }
            /* For jobspec version 1, expect either:
             * - node->slot->core->NIL
             * - slot->core->NIL
             * Set job->slot_count and job->cores_per_slot.
             */
            memset (res, 0, sizeof (res));
            if (parse_res_level (ctx, job, resources, &res[0]) < 0)
                return NULL; /* leaking */;
            if (res[0].with && parse_res_level (ctx, job, res[0].with, &res[1]) < 0)
                return NULL; /* leaking */;
            if (res[1].with && parse_res_level (ctx, job, res[1].with, &res[2]) < 0)
                return NULL; /* leaking */;
            if (res[0].type != NULL && !strcmp (res[0].type, "slot")
                && res[1].type != NULL && !strcmp (res[1].type, "core")
                && res[1].with == NULL) {
                slot_count = res[0].count;
            }
            else if (res[0].type != NULL && !strcmp (res[0].type, "node")
                     && res[1].type != NULL && !strcmp (res[1].type, "slot")
                     && res[2].type != NULL && !strcmp (res[2].type, "core")
                     && res[2].with == NULL) {
                slot_count = res[0].count * res[1].count;
            }
            else {
                flux_log (ctx->h, LOG_WARNING,
                          "%s: job %ju: Unexpected resources: %s->%s->%s%s",
                          __FUNCTION__,
                          (uintmax_t)job->id,
                          res[0].type ? res[0].type : "NULL",
                          res[1].type ? res[1].type : "NULL",
                          res[2].type ? res[2].type : "NULL",
                          res[2].with ? "->..." : NULL);
                slot_count = -1;
            }
            job->ntasks = slot_count;
        }
    }

 outjobspec:
    {
        json_t *a = NULL;
        size_t index;
        json_t *value;


        if (!(a = eventlog_decode (job->eventlog))) {
            flux_log_error (ctx->h, "%s: error parsing eventlog for %ju",
                            __FUNCTION__, (uintmax_t)job->id);
            return NULL;        /* leaking */
        }

#if 0
        FLUX_JOB_STATE_NEW                    = 1,
            FLUX_JOB_STATE_DEPEND                 = 2,
            FLUX_JOB_STATE_PRIORITY               = 4,
            FLUX_JOB_STATE_SCHED                  = 8,
            FLUX_JOB_STATE_RUN                    = 16,
            FLUX_JOB_STATE_CLEANUP                = 32,
            FLUX_JOB_STATE_INACTIVE               = 64,   // captive end state
#endif


        job->states_mask |= FLUX_JOB_STATE_NEW;
        json_array_foreach (a, index, value) {
            const char *name;
            double timestamp;
            json_t *context = NULL;

            if (eventlog_entry_parse (value, &timestamp, &name, &context) < 0) {
                flux_log_error (ctx->h, "%s: error parsing entry for %ju",
                                __FUNCTION__, (uintmax_t)job->id);
                return NULL;        /* leaking */
            }

            if (!strcmp (name, "submit")) {
                assert (context);
                if (json_unpack (context,
                                 "{ s:i }",
                                 "urgency", &job->urgency) < 0) {
                    flux_log (ctx->h, LOG_ERR, "%s: submit context invalid: %ju",
                              __FUNCTION__, (uintmax_t)job->id);
                    return NULL;        /* leaking */
                }
                job->states_mask |= FLUX_JOB_STATE_DEPEND;
            }
            else if (!strcmp (name, "depend")) {
                job->states_mask |= FLUX_JOB_STATE_PRIORITY;
            }
            else if (!strcmp (name, "priority")) {
               assert (context);
               if (json_unpack (context,
                                 "{ s:I }",
                                 "priority", (json_int_t *)&job->priority) < 0) {
                    flux_log (ctx->h, LOG_ERR, "%s: priority context invalid: %ju",
                              __FUNCTION__, (uintmax_t)job->id);
                    return NULL;        /* leaking */
                }
                job->states_mask |= FLUX_JOB_STATE_SCHED;
            }
            else if (!strcmp (name, "urgency")) {
                assert (context);
                if (json_unpack (context, "{ s:i }", "urgency", &job->urgency) < 0) {
                    flux_log (ctx->h, LOG_ERR, "%s: urgency context invalid: %ju",
                              __FUNCTION__, (uintmax_t)job->id);
                    return NULL;        /* leaking */
                }
            }
            else if (!strcmp (name, "exception")) {
                const char *type;
                int severity;
                const char *note = NULL;

                assert (context);
                if (json_unpack (context,
                                 "{s:s s:i s?:s}",
                                 "type", &type,
                                 "severity", &severity,
                                 "note", &note) < 0) {
                    flux_log (ctx->h, LOG_ERR, "%s: exception context invalid: %ju",
                              __FUNCTION__, (uintmax_t)job->id);
                    return NULL;        /* leaking */
                }

                if (!job->exception_occurred
                    || severity < job->exception_severity) {
                    job->exception_occurred = true;
                    job->exception_severity = severity;
                    job->exception_type = type;
                    job->exception_note = note;
                    json_decref (job->exception_context);
                    job->exception_context = json_incref (context);
                }
                if (severity == 0)
                    job->states_mask |= FLUX_JOB_STATE_CLEANUP;
            }
            else if (!strcmp (name, "alloc")) {
                /* context not required if no annotations */
                if (context) {
                    json_t *annotations;
                    if (!(annotations = json_object_get (context, "annotations"))) {
                        flux_log (ctx->h, LOG_ERR,
                                  "%s: alloc context for %ju invalid",
                                  __FUNCTION__, (uintmax_t)job->id);
                        return NULL;        /* leaking */
                    }
                    if (!json_is_null (annotations))
                        job->annotations = json_incref (annotations);
                }
                /* this ok? */
                if (job->states_mask & FLUX_JOB_STATE_SCHED)
                    job->states_mask |= FLUX_JOB_STATE_RUN;
            }
            else if (!strcmp (name, "finish")) {
                assert (context);
                if (json_unpack (context,
                                 "{ s:i }",
                                 "status", &job->wait_status) < 0) {
                    flux_log (ctx->h, LOG_ERR, "%s: finish context invalid: %ju",
                              __FUNCTION__, (uintmax_t)job->id);
                    return NULL;        /* leaking */
                }

                if (!job->wait_status)
                    job->success = true;

                if (job->states_mask & FLUX_JOB_STATE_RUN)
                    job->states_mask |= FLUX_JOB_STATE_CLEANUP;
            }
            else if (!strcmp (name, "clean")) {
                job->states_mask |= FLUX_JOB_STATE_INACTIVE;
            }
            else if (!strcmp (name, "flux-restart")) {
                /* should track current job->state? and set accordingly? not clear */
            }
            else if (!strncmp (name, "dependency-", 11)) {
                if (dependency_context_parse (ctx->h, job, name+11, context) < 0) {
                    flux_log (ctx->h, LOG_ERR, "%s: dependency context invalid: %ju",
                              __FUNCTION__, (uintmax_t)job->id);
                    return NULL;        /* leaking */
                }
            }
            else if (!strcmp (name, "memo")) {
                if (context && memo_update (ctx->h, job, context) < 0) {
                    flux_log (ctx->h, LOG_ERR, "%s: memo context invalid: %ju",
                              __FUNCTION__, (uintmax_t)job->id);
                    return NULL;        /* leaking */
                }
            }

        }
    }

    if (job->R)
    {
        struct rlist *rl = NULL;
        struct idset *idset = NULL;
        struct hostlist *hl = NULL;
        json_error_t error;
        int flags = IDSET_FLAG_BRACKETS | IDSET_FLAG_RANGE;

        if (!(rl = rlist_from_json (job->R, &error))) {
            flux_log_error (ctx->h, "rlist_from_json: %s", error.text);
            return NULL;        /* leaking */
        }

        job->expiration = rl->expiration;

        if (!(idset = rlist_ranks (rl)))
            return NULL;        /* leaking */

        job->nnodes = idset_count (idset);
        if (!(job->ranks = idset_encode (idset, flags)))
            return NULL;        /* leaking */

        /* reading nodelist from R directly would avoid the creation /
         * destruction of a hostlist.  However, we get a hostlist to
         * ensure that the nodelist we return to users is consistently
         * formatted.
         */
        if (!(hl = rlist_nodelist (rl)))
            return NULL;        /* leaking */

        if (!(job->nodelist = hostlist_encode (hl)))
            return NULL;        /* leaking */

        hostlist_destroy (hl);
        idset_destroy (idset);
        rlist_destroy (rl);
    }


    /* Default result is failed, overridden below */
    if (job->success)
        job->result = FLUX_JOB_RESULT_COMPLETED;
    else if (job->exception_occurred) {
        if (!strcmp (job->exception_type, "cancel"))
            job->result = FLUX_JOB_RESULT_CANCELED;
        else if (!strcmp (job->exception_type, "timeout"))
            job->result = FLUX_JOB_RESULT_TIMEOUT;
    }

    return job;
}

int get_jobs_from_sqlite (struct list_ctx *ctx,
                          json_t *jobs,
                          job_list_error_t *errp,
                          int max_entries,
                          json_t *attrs,
                          uint32_t userid,
                          int states,
                          int results)
{
    char *sql = "SELECT * FROM jobs ORDER BY t_inactive DESC;";
    char *sqllimit = "SELECT * FROM jobs ORDER BY t_inactive DESC LIMIT ?";
    sqlite3_stmt *res = NULL;
    struct job *job;

    if (max_entries) {
        if (sqlite3_prepare_v2 (ctx->actx->db,
                                sqllimit,
                                -1,
                                &res,
                                0) != SQLITE_OK) {
            flux_log_error (ctx->h, "sqlite3_prepare_v2");
            return -1;
        }
        if (sqlite3_bind_int (res, 1, max_entries) != SQLITE_OK) {
            flux_log_error (ctx->h, "sqlite3_bind_int");
            return -1;          /* leak res */
        }
    }
    else {
        if (sqlite3_prepare_v2 (ctx->actx->db,
                                sql,
                                -1,
                                &res,
                                0) != SQLITE_OK) {
            flux_log_error (ctx->h, "sqlite3_prepare_v2");
            return -1;
        }
    }

    while (sqlite3_step (res) == SQLITE_ROW) {
        if (!(job = sqliterow_2_job (ctx, res))) {
            flux_log_error (ctx->h, "sqliterow_2_job");
            return -1;          /* leak job & res */
        }
        if (job_filter (job, userid, states, results)) {
            json_t *o;
            if (!(o = job_to_json (job, attrs, errp)))
                return -1;
            if (json_array_append_new (jobs, o) < 0) {
                json_decref (o);
                errno = ENOMEM;
                return -1;
            }
            if (json_array_size (jobs) == max_entries)
                return 1;
        }

        /* lots of leaking here, deal with later with proper destroy
         * function */
        free (job);
    }

    sqlite3_finalize (res);
    return 0;
}

/* Create a JSON array of 'job' objects.  'max_entries' determines the
 * max number of jobs to return, 0=unlimited.  Returns JSON object
 * which the caller must free.  On error, return NULL with errno set:
 *
 * EPROTO - malformed or empty attrs array, max_entries out of range
 * ENOMEM - out of memory
 */
json_t *get_jobs (struct list_ctx *ctx,
                  job_list_error_t *errp,
                  int max_entries,
                  json_t *attrs,
                  uint32_t userid,
                  int states,
                  int results)
{
    json_t *jobs = NULL;
    int saved_errno;
    int ret = 0;

    if (!(jobs = json_array ()))
        goto error_nomem;

    /* We return jobs in the following order, pending, running,
     * inactive */

    if (states & FLUX_JOB_STATE_PENDING) {
        if ((ret = get_jobs_from_list (jobs,
                                       errp,
                                       ctx->jsctx->pending,
                                       max_entries,
                                       attrs,
                                       userid,
                                       states,
                                       results)) < 0)
            goto error;
    }

    if (states & FLUX_JOB_STATE_RUNNING) {
        if (!ret) {
            if ((ret = get_jobs_from_list (jobs,
                                           errp,
                                           ctx->jsctx->running,
                                           max_entries,
                                           attrs,
                                           userid,
                                           states,
                                           results)) < 0)
                goto error;
        }
    }

    if (states & FLUX_JOB_STATE_INACTIVE) {
        if (!ret) {
            if ((ret = get_jobs_from_sqlite (ctx,
                                             jobs,
                                             errp,
                                             max_entries,
                                             attrs,
                                             userid,
                                             states,
                                             results)) < 0)
                goto error;
        }
    }

    return jobs;

error_nomem:
    errno = ENOMEM;
error:
    saved_errno = errno;
    json_decref (jobs);
    errno = saved_errno;
    return NULL;
}

void list_cb (flux_t *h, flux_msg_handler_t *mh,
              const flux_msg_t *msg, void *arg)
{
    struct list_ctx *ctx = arg;
    job_list_error_t err;
    json_t *jobs;
    json_t *attrs;
    int max_entries;
    uint32_t userid;
    int states;
    int results;

    if (flux_request_unpack (msg, NULL, "{s:i s:o s:i s:i s:i}",
                             "max_entries", &max_entries,
                             "attrs", &attrs,
                             "userid", &userid,
                             "states", &states,
                             "results", &results) < 0) {
        seterror (&err, "invalid payload: %s", flux_msg_last_error (msg));
        errno = EPROTO;
        goto error;
    }
    if (max_entries < 0) {
        seterror (&err, "invalid payload: max_entries < 0 not allowed");
        errno = EPROTO;
        goto error;
    }
    if (!json_is_array (attrs)) {
        seterror (&err, "invalid payload: attrs must be an array");
        errno = EPROTO;
        goto error;
    }
    /* If user sets no states, assume they want all information */
    if (!states)
        states = (FLUX_JOB_STATE_PENDING
                  | FLUX_JOB_STATE_RUNNING
                  | FLUX_JOB_STATE_INACTIVE);

    /* If user sets no results, assume they want all information */
    if (!results)
        results = (FLUX_JOB_RESULT_COMPLETED
                   | FLUX_JOB_RESULT_FAILED
                   | FLUX_JOB_RESULT_CANCELED
                   | FLUX_JOB_RESULT_TIMEOUT);

    if (!(jobs = get_jobs (ctx, &err, max_entries,
                           attrs, userid, states, results)))
        goto error;

    if (flux_respond_pack (h, msg, "{s:O}", "jobs", jobs) < 0)
        flux_log_error (h, "%s: flux_respond_pack", __FUNCTION__);

    json_decref (jobs);
    return;

error:
    if (flux_respond_error (h, msg, errno, err.text) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

/* Create a JSON array of 'job' objects.  'since' limits entries
 * returned, only returning entries with 't_inactive' newer than the
 * timestamp.  Returns JSON object which the caller must free.  On
 * error, return NULL with errno set:
 *
 * EPROTO - malformed or empty attrs array
 * ENOMEM - out of memory
 */
json_t *get_inactive_jobs (struct list_ctx *ctx,
                           job_list_error_t *errp,
                           int max_entries,
                           double since,
                           json_t *attrs,
                           const char *name)
{
    char *sql = "SELECT * FROM jobs ORDER BY t_inactive DESC;";
    char *sqllimit = "SELECT * FROM jobs ORDER BY t_inactive DESC LIMIT ?";
    sqlite3_stmt *res = NULL;
    json_t *jobs = NULL;
    struct job *job;
    int saved_errno;

    if (!(jobs = json_array ()))
        goto error_nomem;

    if (max_entries) {
        if (sqlite3_prepare_v2 (ctx->actx->db,
                                sqllimit,
                                -1,
                                &res,
                                0) != SQLITE_OK) {
            flux_log_error (ctx->h, "sqlite3_prepare_v2");
            goto error;
        }
        if (sqlite3_bind_int (res, 1, max_entries) != SQLITE_OK) {
            flux_log_error (ctx->h, "sqlite3_bind_int");
            goto error;          /* leak res */
        }
    }
    else {
        if (sqlite3_prepare_v2 (ctx->actx->db,
                                sql,
                                -1,
                                &res,
                                0) != SQLITE_OK) {
            flux_log_error (ctx->h, "sqlite3_prepare_v2");
            goto error;
        }
    }

    while (sqlite3_step (res) == SQLITE_ROW) {
        if (!(job = sqliterow_2_job (ctx, res))) {
            flux_log_error (ctx->h, "sqliterow_2_job");
            goto error;          /* leak job & res */
        }
        if (job->t_inactive <= since) {
            /* leak */
            break;
        }
        if (!name || strcmp (job->name, name) == 0) {
            json_t *o;
            if (!(o = job_to_json (job, attrs, errp)))
                goto error;
            if (json_array_append_new (jobs, o) < 0) {
                json_decref (o);
                errno = ENOMEM;
                goto error;
            }
            if (json_array_size (jobs) == max_entries)
                goto out;
        }

        /* lots of leaking here, deal with later with proper destroy
         * function */
        free (job);
    }

out:
    sqlite3_finalize (res);
    return jobs;

error_nomem:
    errno = ENOMEM;
error:
    saved_errno = errno;
    json_decref (jobs);
    errno = saved_errno;
    return NULL;
}

void list_inactive_cb (flux_t *h, flux_msg_handler_t *mh,
                       const flux_msg_t *msg, void *arg)
{
    struct list_ctx *ctx = arg;
    job_list_error_t err = {{0}};
    json_t *jobs;
    int max_entries;
    double since;
    json_t *attrs;
    const char *name = NULL;

    if (flux_request_unpack (msg, NULL, "{s:i s:F s:o s?:s}",
                             "max_entries", &max_entries,
                             "since", &since,
                             "attrs", &attrs,
                             "name", &name) < 0) {
        seterror (&err, "invalid payload: %s", flux_msg_last_error (msg));
        goto error;
    }
    if (max_entries < 0) {
        seterror (&err, "invalid payload: max_entries < 0 not allowed");
        errno = EPROTO;
        goto error;
    }
    if (!json_is_array (attrs)) {
        seterror (&err, "invalid payload: attrs must be an array");
        errno = EPROTO;
        goto error;
    }
    if (!(jobs = get_inactive_jobs (ctx, &err,
                                    max_entries,
                                    since,
                                    attrs,
                                    name)))
        goto error;

    if (flux_respond_pack (h, msg, "{s:O}", "jobs", jobs) < 0)
        flux_log_error (h, "%s: flux_respond_pack", __FUNCTION__);

    json_decref (jobs);
    return;

error:
    if (flux_respond_error (h, msg, errno, err.text) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

int wait_id_valid (struct list_ctx *ctx, struct idsync_data *isd)
{
    zlistx_t *list_isd;
    void *handle;
    int saved_errno;

    if ((handle = zlistx_find (ctx->idsync_lookups, isd))) {
        /* detach will not call zlistx destructor */
        zlistx_detach (ctx->idsync_lookups, handle);
    }

    /* idsync_waits holds lists of ids waiting on, b/c multiplers callers
     * could wait on same id */
    if (!(list_isd = zhashx_lookup (ctx->idsync_waits, &isd->id))) {
        if (!(list_isd = zlistx_new ())) {
            flux_log_error (isd->ctx->h, "%s: zlistx_new", __FUNCTION__);
            goto error_destroy;
        }
        zlistx_set_destructor (list_isd, idsync_data_destroy_wrapper);

        if (zhashx_insert (ctx->idsync_waits, &isd->id, list_isd) < 0) {
            flux_log_error (isd->ctx->h, "%s: zhashx_insert", __FUNCTION__);
            goto error_destroy;
        }
    }

    if (!zlistx_add_end (list_isd, isd)) {
        flux_log_error (isd->ctx->h, "%s: zlistx_add_end", __FUNCTION__);
        goto error_destroy;
    }

    return 0;

error_destroy:
    saved_errno = errno;
    idsync_data_destroy (isd);
    errno = saved_errno;
    return -1;
}

void check_id_valid_continuation (flux_future_t *f, void *arg)
{
    struct idsync_data *isd = arg;
    struct list_ctx *ctx = isd->ctx;
    void *handle;

    if (flux_future_get (f, NULL) < 0) {
        if (flux_respond_error (ctx->h, isd->msg, errno, NULL) < 0)
            flux_log_error (ctx->h, "%s: flux_respond_error", __FUNCTION__);
        goto cleanup;
    }
    else {
        /* Job ID is legal.  Chance job-info has seen ID since this
         * lookup was done */
        struct job *job;
        if (!(job = zhashx_lookup (ctx->jsctx->index, &isd->id))
            || job->state == FLUX_JOB_STATE_NEW) {
            /* Must wait for job-info to see state change */
            if (wait_id_valid (ctx, isd) < 0)
                flux_log_error (ctx->h, "%s: wait_id_valid", __FUNCTION__);
            goto cleanup;
        }
        else {
            json_t *o;
            if (!(o = get_job_by_id (ctx, NULL, isd->msg,
                                     isd->id, isd->attrs, NULL))) {
                flux_log_error (ctx->h, "%s: get_job_by_id", __FUNCTION__);
                goto cleanup;
            }
            if (flux_respond_pack (ctx->h, isd->msg, "{s:O}", "job", o) < 0) {
                flux_log_error (ctx->h, "%s: flux_respond_pack", __FUNCTION__);
                goto cleanup;
            }
        }
    }

cleanup:
    /* delete will destroy struct idsync_data and future within it */
    handle = zlistx_find (ctx->idsync_lookups, isd);
    if (handle)
        zlistx_delete (ctx->idsync_lookups, handle);
    return;
}

int check_id_valid (struct list_ctx *ctx,
                    const flux_msg_t *msg,
                    flux_jobid_t id,
                    json_t *attrs)
{
    flux_future_t *f = NULL;
    struct idsync_data *isd = NULL;
    char path[256];
    int saved_errno;

    /* Check to see if the ID is legal, job-info may have not yet
     * seen the ID publication yet */
    if (flux_job_kvs_key (path, sizeof (path), id, NULL) < 0)
        goto error;

    if (!(f = flux_kvs_lookup (ctx->h, NULL, FLUX_KVS_READDIR, path))) {
        flux_log_error (ctx->h, "%s: flux_kvs_lookup", __FUNCTION__);
        goto error;
    }

    if (!(isd = idsync_data_create (ctx, id, msg, attrs, f)))
        goto error;

    /* future now owned by struct idsync_data */
    f = NULL;

    if (flux_future_then (isd->f_lookup,
                          -1,
                          check_id_valid_continuation,
                          isd) < 0) {
        flux_log_error (ctx->h, "%s: flux_future_then", __FUNCTION__);
        goto error;
    }

    if (!zlistx_add_end (ctx->idsync_lookups, isd)) {
        flux_log_error (ctx->h, "%s: zlistx_add_end", __FUNCTION__);
        goto error;
    }

    return 0;

error:
    saved_errno = errno;
    flux_future_destroy (f);
    idsync_data_destroy (isd);
    errno = saved_errno;
    return -1;
}

/* Returns JSON object which the caller must free.  On error, return
 * NULL with errno set:
 *
 * EPROTO - malformed or empty id or attrs array
 * EINVAL - invalid id
 * ENOMEM - out of memory
 */
json_t *get_job_by_id (struct list_ctx *ctx,
                       job_list_error_t *errp,
                       const flux_msg_t *msg,
                       flux_jobid_t id,
                       json_t *attrs,
                       bool *stall)
{
    struct job *job;

    if (!(job = zhashx_lookup (ctx->jsctx->index, &id))) {
        /* first check sql */
        char *sql = "SELECT * FROM jobs WHERE id = ?;";
        sqlite3_stmt *res = NULL;
        struct job *job;

        if (sqlite3_prepare_v2 (ctx->actx->db,
                                sql,
                                -1,
                                &res,
                                0) != SQLITE_OK) {
            flux_log_error (ctx->h, "sqlite3_prepare_v2");
            return NULL;
        }

        if (sqlite3_bind_int64 (res, 1, id) != SQLITE_OK) {
            flux_log_error (ctx->h, "sqlite3_bind_int64");
            return NULL;          /* leak res */
        }


        if (sqlite3_step (res) == SQLITE_ROW) {
            if (!(job = sqliterow_2_job (ctx, res))) {
                flux_log_error (ctx->h, "sqliterow_2_job");
                return NULL;          /* leak res */
            }
            return job_to_json (job, attrs, errp); /* leak job */
        }

        if (stall) {
            if (check_id_valid (ctx, msg, id, attrs) < 0) {
                flux_log_error (ctx->h, "%s: check_id_valid", __FUNCTION__);
                return NULL;
            }
            (*stall) = true;
        }
        return NULL;
    }

    if (job->state == FLUX_JOB_STATE_NEW) {
        if (stall) {
            struct idsync_data *isd;
            if (!(isd = idsync_data_create (ctx, id, msg, attrs, NULL))) {
                flux_log_error (ctx->h, "%s: idsync_data_create", __FUNCTION__);
                return NULL;
            }
            /* Must wait for job-info to see state change */
            if (wait_id_valid (ctx, isd) < 0) {
                flux_log_error (ctx->h, "%s: wait_id_valid", __FUNCTION__);
                return NULL;
            }
            (*stall) = true;
        }
        return NULL;
    }

    return job_to_json (job, attrs, errp);
}

void list_id_cb (flux_t *h, flux_msg_handler_t *mh,
                  const flux_msg_t *msg, void *arg)
{
    struct list_ctx *ctx = arg;
    job_list_error_t err = {{0}};
    json_t *job;
    flux_jobid_t id;
    json_t *attrs;
    bool stall = false;

    if (flux_request_unpack (msg, NULL, "{s:I s:o}",
                             "id", &id,
                             "attrs", &attrs) < 0) {
        seterror (&err, "invalid payload: %s", flux_msg_last_error (msg));
        errno = EPROTO;
        goto error;
    }

    if (!json_is_array (attrs)) {
        seterror (&err, "invalid payload: attrs must be an array");
        errno = EPROTO;
        goto error;
    }

    if (!(job = get_job_by_id (ctx, &err, msg, id, attrs, &stall))) {
        /* response handled after KVS lookup complete */
        if (stall)
            goto stall;
        goto error;
    }

    if (flux_respond_pack (h, msg, "{s:O}", "job", job) < 0)
        flux_log_error (h, "%s: flux_respond_pack", __FUNCTION__);

    json_decref (job);
stall:
    return;

error:
    if (flux_respond_error (h, msg, errno, err.text) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

int list_attrs_append (json_t *a, const char *attr)
{
    json_t *o = json_string (attr);
    if (!o) {
        errno = ENOMEM;
        return -1;
    }
    if (json_array_append_new (a, o) < 0) {
        json_decref (o);
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

void list_attrs_cb (flux_t *h, flux_msg_handler_t *mh,
                    const flux_msg_t *msg, void *arg)
{
    const char *attrs[] = { "userid", "urgency", "priority", "t_submit",
                            "t_depend", "t_run", "t_cleanup", "t_inactive",
                            "state", "name", "ntasks", "nnodes",
                            "ranks", "nodelist", "success", "exception_occurred",
                            "exception_type", "exception_severity",
                            "exception_note", "result", "expiration",
                            "annotations", "waitstatus", "dependencies",
                            NULL };
    json_t *a;
    int i;

    if (!(a = json_array ())) {
        errno = ENOMEM;
        goto error;
    }

    for (i = 0; attrs[i] != NULL; i++) {
        if (list_attrs_append (a, attrs[i]) < 0)
            goto error;
    }

    if (flux_respond_pack (h, msg, "{s:O}", "attrs", a) < 0)
        flux_log_error (h, "%s: flux_respond_pack", __FUNCTION__);

    json_decref (a);
    return;

error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
    json_decref (a);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
