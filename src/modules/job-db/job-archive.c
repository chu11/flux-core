/************************************************************\
 * Copyright 2016 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* job-archive: archive job data service for flux */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdbool.h>
#include <flux/core.h>
#include <jansson.h>
#include <sqlite3.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/fsd.h"

#include "job-archive.h"
#include "job_state.h"
#include "job_util.h"

#define BUSY_TIMEOUT_DEFAULT 50
#define BUFSIZE              1024

const char *sql_create_table = "CREATE TABLE if not exists jobs("
                               "  id CHAR(16) PRIMARY KEY,"
                               "  t_inactive REAL,"
                               "  jobdata JSON,"
                               "  eventlog TEXT,"
                               "  jobspec JSON,"
                               "  R JSON"
    ");";

const char *sql_store =    \
    "INSERT INTO jobs"     \
    "("                    \
    "  id,t_inactive,jobdata," \
    "  eventlog,jobspec,R" \
    ") values ("           \
    "  ?1, ?2, ?3, ?4, ?5, ?6" \
    ")";

static void log_sqlite_error (struct job_archive_ctx *ctx, const char *fmt, ...)
{
    char buf[128];
    va_list ap;

    va_start (ap, fmt);
    (void)vsnprintf (buf, sizeof (buf), fmt, ap);
    va_end (ap);

    if (ctx->db) {
        const char *errmsg = sqlite3_errmsg (ctx->db);
        flux_log (ctx->h,
                  LOG_ERR,
                  "%s: %s(%d)",
                  buf,
                  errmsg ? errmsg : "unknown error code",
                  sqlite3_extended_errcode (ctx->db));
    }
    else
        flux_log (ctx->h, LOG_ERR, "%s: unknown error, no sqlite3 handle", buf);
}

void job_archive_ctx_destroy (struct job_archive_ctx *ctx)
{
    if (ctx) {
        free (ctx->dbpath);
        if (ctx->store_stmt) {
            if (sqlite3_finalize (ctx->store_stmt) != SQLITE_OK)
                log_sqlite_error (ctx, "sqlite_finalize store_stmt");
        }
        if (ctx->db) {
            if (sqlite3_close (ctx->db) != SQLITE_OK)
                log_sqlite_error (ctx, "sqlite3_close");
        }
        free (ctx);
    }
}

static struct job_archive_ctx * job_archive_ctx_create (flux_t *h)
{
    struct job_archive_ctx *ctx = calloc (1, sizeof (*ctx));

    if (!ctx) {
        flux_log_error (h, "job_archive_ctx_create");
        goto error;
    }

    ctx->h = h;
    ctx->busy_timeout = BUSY_TIMEOUT_DEFAULT;

    return ctx;
 error:
    job_archive_ctx_destroy (ctx);
    return (NULL);
}

int job_archive_init (struct job_archive_ctx *ctx)
{
    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
    char buf[1024];
    int rc = -1;

    if (sqlite3_open_v2 (ctx->dbpath, &ctx->db, flags, NULL) != SQLITE_OK) {
        log_sqlite_error (ctx, "opening %s", ctx->dbpath);
        goto error;
    }

    if (sqlite3_exec (ctx->db,
                      "PRAGMA journal_mode=WAL",
                      NULL,
                      NULL,
                      NULL) != SQLITE_OK) {
        log_sqlite_error (ctx, "setting sqlite 'journal_mode' pragma");
        goto error;
    }
    if (sqlite3_exec (ctx->db,
                      "PRAGMA synchronous=NORMAL",
                      NULL,
                      NULL,
                      NULL) != SQLITE_OK) {
        log_sqlite_error (ctx, "setting sqlite 'synchronous' pragma");
        goto error;
    }
    snprintf (buf, 1024, "PRAGMA busy_timeout=%u;", ctx->busy_timeout);
    if (sqlite3_exec (ctx->db,
                      buf,
                      NULL,
                      NULL,
                      NULL) != SQLITE_OK) {
        log_sqlite_error (ctx, "setting sqlite 'busy_timeout' pragma");
        goto error;
    }
    if (sqlite3_exec (ctx->db,
                      sql_create_table,
                      NULL,
                      NULL,
                      NULL) != SQLITE_OK) {
        log_sqlite_error (ctx, "creating object table");
        goto error;
    }

    if (sqlite3_prepare_v2 (ctx->db,
                            sql_store,
                            -1,
                            &ctx->store_stmt,
                            NULL) != SQLITE_OK) {
        log_sqlite_error (ctx, "preparing store stmt");
        goto error;
    }

    rc = 0;
error:
    return rc;
}

int job_archive_store (struct job_archive_ctx *ctx, struct job *job)
{
    json_t *o = NULL;
    job_list_error_t err;
    char *job_str = NULL;
    char *jobspec = NULL;
    char *R = NULL;
    char idbuf[64];
    int rv = -1;

    snprintf (idbuf, 64, "%llu", (unsigned long long)job->id);
    if (sqlite3_bind_text (ctx->store_stmt,
                           1,
                           idbuf,
                           strlen (idbuf),
                           SQLITE_STATIC) != SQLITE_OK) {
        log_sqlite_error (ctx, "store: binding id");
        goto out;
    }
    if (sqlite3_bind_double (ctx->store_stmt,
                             2,
                             job->t_inactive) != SQLITE_OK) {
        log_sqlite_error (ctx, "store: binding t_inactive");
        goto out;
    }
    if (!(o = job_to_json_all (job, &err)))
        goto out;
    if (!(job_str = json_dumps (o, JSON_COMPACT))) {
        errno = ENOMEM;
        goto out;
    }
    if (sqlite3_bind_text (ctx->store_stmt,
                           3,
                           job_str,
                           strlen (job_str),
                           SQLITE_STATIC) != SQLITE_OK) {
        log_sqlite_error (ctx, "store: binding jobdata");
        goto out;
    }
    if (sqlite3_bind_text (ctx->store_stmt,
                           4,
                           job->eventlog,
                           strlen (job->eventlog),
                           SQLITE_STATIC) != SQLITE_OK) {
        log_sqlite_error (ctx, "store: binding eventlog");
        goto out;
    }
    if (!(jobspec = json_dumps (job->jobspec, 0))) {
        flux_log_error (ctx->h, "json_dumps jobspec");
        goto out;
    }
    if (sqlite3_bind_text (ctx->store_stmt,
                           5,
                           jobspec,
                           strlen (jobspec),
                           SQLITE_STATIC) != SQLITE_OK) {
        log_sqlite_error (ctx, "store: binding jobspec");
        goto out;
    }
    if (job->R) {
        if (!(R = json_dumps (job->R, 0))) {
            flux_log_error (ctx->h, "json_dumps R");
            goto out;
        }
    }
    if (sqlite3_bind_text (ctx->store_stmt,
                           6,
                           R ? R: "",
                           R ? strlen (R) : 0,
                           SQLITE_STATIC) != SQLITE_OK) {
        log_sqlite_error (ctx, "store: binding R");
        goto out;
    }
    while (sqlite3_step (ctx->store_stmt) != SQLITE_DONE) {
        /* due to rounding errors in sqlite, duplicate entries could be
         * written out on occassion leading to a SQLITE_CONSTRAINT error.
         * We accept this and move on.
         */
        int err = sqlite3_errcode (ctx->db);
        if (err == SQLITE_CONSTRAINT)
            break;
        else if (err == SQLITE_BUSY) {
            /* In the rare case this cannot complete within the normal
             * busytimeout, we elect to spin till it completes.  This
             * may need to be revisited in the future: */
            flux_log (ctx->h, LOG_DEBUG, "%s: BUSY", __FUNCTION__);
            usleep (1000);
            continue;
        }
        else {
            log_sqlite_error (ctx, "store: executing stmt");
            goto out;
        }
    }

    rv = 0;
out:
    sqlite3_reset (ctx->store_stmt);
    json_decref (o);
    free (job_str);
    free (jobspec);
    free (R);
    return rv;
}

static int process_config (struct job_archive_ctx *ctx)
{
    flux_error_t err;
    const char *dbpath = NULL;
    const char *busytimeout = NULL;

    if (flux_conf_unpack (flux_get_conf (ctx->h),
                          &err,
                          "{s?{s?s s?s}}",
                          "archive",
                            "dbpath", &dbpath,
                            "busytimeout", &busytimeout) < 0) {
        flux_log (ctx->h, LOG_ERR,
                  "error reading archive config: %s",
                  err.text);
        return -1;
    }

    if (dbpath) {
        if (!(ctx->dbpath = strdup (dbpath)))
            flux_log_error (ctx->h, "dbpath not configured");
    }
    else {
        const char *dbdir;
        if (!(dbdir = flux_attr_get (ctx->h, "statedir"))) {
            flux_log_error (ctx->h, "statedir not set");
            return -1;
        }

        if (asprintf (&ctx->dbpath, "%s/job-db.sqlite", dbdir) < 0) {
            flux_log_error (ctx->h, "asprintf");
            return -1;
        }
    }
    if (busytimeout) {
        double tmp;
        if (fsd_parse_duration (busytimeout, &tmp) < 0)
            flux_log_error (ctx->h, "busytimeout not configured");
        else
            ctx->busy_timeout = (int)(1000 * tmp);
    }

    return 0;
}

struct job_archive_ctx * job_archive_setup (flux_t *h, int ac, char **av)
{
    struct job_archive_ctx *ctx = job_archive_ctx_create (h);

    if (!ctx)
        return NULL;

    if (process_config (ctx) < 0)
        goto done;

    if (job_archive_init (ctx) < 0)
        goto done;

    return ctx;

done:
    job_archive_ctx_destroy (ctx);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
