/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>

#include "src/common/libczmqcontainers/czmq_containers.h"

#include "job-list.h"
#include "job-archive.h"
#include "job_state.h"
#include "list.h"
#include "idsync.h"

static void stats_cb (flux_t *h, flux_msg_handler_t *mh,
                      const flux_msg_t *msg, void *arg)
{
    struct list_ctx *ctx = arg;
    int pending = zlistx_size (ctx->jsctx->pending);
    int running = zlistx_size (ctx->jsctx->running);
    int inactive = zlistx_size (ctx->jsctx->inactive);
    int idsync_lookups = zlistx_size (ctx->idsync_lookups);
    int idsync_waits = zhashx_size (ctx->idsync_waits);
    if (flux_respond_pack (h, msg, "{s:{s:i s:i s:i} s:{s:i s:i}}",
                           "jobs",
                           "pending", pending,
                           "running", running,
                           "inactive", inactive,
                           "idsync",
                           "lookups", idsync_lookups,
                           "waits", idsync_waits) < 0) {
        flux_log_error (h, "%s: flux_respond_pack", __FUNCTION__);
        goto error;
    }

    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

static void job_stats_cb (flux_t *h, flux_msg_handler_t *mh,
                          const flux_msg_t *msg, void *arg)
{
    struct list_ctx *ctx = arg;
    json_t *o = job_stats_encode (&ctx->jsctx->stats);
    if (o == NULL)
        goto error;
    if (flux_respond_pack (h, msg, "o", o) < 0) {
        flux_log_error (h, "%s: flux_respond_pack", __FUNCTION__);
        goto error;
    }
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

static const struct flux_msg_handler_spec htab[] = {
    { .typemask     = FLUX_MSGTYPE_REQUEST,
      .topic_glob   = "job-list.list",
      .cb           = list_cb,
      .rolemask     = FLUX_ROLE_USER
    },
    { .typemask     = FLUX_MSGTYPE_REQUEST,
      .topic_glob   = "job-list.list-inactive",
      .cb           = list_inactive_cb,
      .rolemask     = FLUX_ROLE_USER
    },
    { .typemask     = FLUX_MSGTYPE_REQUEST,
      .topic_glob   = "job-list.list-id",
      .cb           = list_id_cb,
      .rolemask     = FLUX_ROLE_USER
    },
    { .typemask     = FLUX_MSGTYPE_REQUEST,
      .topic_glob   = "job-list.list-attrs",
      .cb           = list_attrs_cb,
      .rolemask     = FLUX_ROLE_USER
    },
    { .typemask     = FLUX_MSGTYPE_REQUEST,
      .topic_glob   = "job-list.job-state-pause",
      .cb           = job_state_pause_cb,
      .rolemask     = FLUX_ROLE_USER
    },
    { .typemask     = FLUX_MSGTYPE_REQUEST,
      .topic_glob   = "job-list.job-state-unpause",
      .cb           = job_state_unpause_cb,
      .rolemask     = FLUX_ROLE_USER
    },
    { .typemask     = FLUX_MSGTYPE_REQUEST,
      .topic_glob   = "job-list.job-stats",
      .cb           = job_stats_cb,
      .rolemask     = FLUX_ROLE_USER
    },
    { .typemask     = FLUX_MSGTYPE_REQUEST,
      .topic_glob   = "job-list.stats.get",
      .cb           = stats_cb,
      .rolemask     = 0
    },
    FLUX_MSGHANDLER_TABLE_END,
};

static void list_ctx_destroy (struct list_ctx *ctx)
{
    if (ctx) {
        int saved_errno = errno;
        flux_msg_handler_delvec (ctx->handlers);
        if (ctx->jsctx)
            job_state_destroy (ctx->jsctx);
        if (ctx->idsync_lookups)
            idsync_cleanup (ctx);
        free (ctx);
        errno = saved_errno;
    }
}

static struct list_ctx *list_ctx_create (flux_t *h)
{
    struct list_ctx *ctx = calloc (1, sizeof (*ctx));
    if (!ctx)
        return NULL;
    ctx->h = h;
    if (flux_msg_handler_addvec (h, htab, ctx, &ctx->handlers) < 0)
        goto error;
    if (!(ctx->jsctx = job_state_create (ctx)))
        goto error;
    if (idsync_setup (ctx) < 0)
        goto error;
    return ctx;
error:
    list_ctx_destroy (ctx);
    return NULL;
}

int mod_main (flux_t *h, int argc, char **argv)
{
    struct list_ctx *ctx = NULL;
    struct job_archive_ctx *actx = NULL;
    flux_future_t *f = NULL;
    int rc = -1;

    if (!(actx = job_archive_setup (h, argc, argv))) {
        flux_log_error (h, "archive initialization error");
        goto done;
    }
    if (!(ctx = list_ctx_create (h))) {
        flux_log_error (h, "initialization error");
        goto done;
    }
    ctx->actx = actx;
    ctx->jsctx->actx = actx;
    if (!(f = flux_service_register (h, "job-list"))) {
        flux_log_error (h, "flux_service_register");
        goto done;
    }
    if (flux_future_get (f, NULL) < 0) {
        flux_log_error (h, "flux_future_get");
        goto done;
    }
    if (job_state_init_from_kvs (ctx) < 0)
        goto done;
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0)
        goto done;
    rc = 0;
done:
    job_archive_ctx_destroy (actx);
    list_ctx_destroy (ctx);
    flux_future_destroy (f);
    return rc;
}

MOD_NAME ("job-db");

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */