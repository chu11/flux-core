/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
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
#include <sys/types.h>
#include <sys/socket.h>
#include <pthread.h>
#include <assert.h>
#include <flux/core.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libpmi/simple_server.h"
#include "src/common/libpmi/pmi.h"
#include "src/common/libutil/fdutils.h"
#include "src/common/liblsd/cbuf.h"

#include "src/common/libtap/tap.h"

#include "server_thread.h"

struct pmi_server_context;

// server side context for one client
struct client {
    int sfd;
    int rank;
    cbuf_t cbuf;
    char buf[SIMPLE_MAX_PROTO_LINE];
    struct pmi_server_context *ctx;
};

#define MAGIC_VALUE 0x5354534a
struct pmi_server_context {
    int magic;
    pthread_t t;
    struct client *cli;
    zhashx_t *kvs;
    struct pmi_simple_server *pmi;
    int size;
    char buf[SIMPLE_MAX_PROTO_LINE];
    int rig_barrier_entry_failure;
    int rig_barrier_exit_failure;
};

static void destroy_string (void **valp)
{
    free (*valp);
    *valp = NULL;
}

static void *dup_string (const void *val)
{
    return strdup (val);
}

static int s_kvs_put (void *arg, const char *kvsname, const char *key,
                      const char *val)
{
    struct pmi_server_context *ctx = arg;

    assert (ctx->magic == MAGIC_VALUE);
    zhashx_update (ctx->kvs, key, (char *)val);

    return 0;
}

static int s_kvs_get (void *arg, void *client,
                      const char *kvsname, const char *key)
{
    struct pmi_server_context *ctx = arg;
    char *value;

    assert (ctx->magic == MAGIC_VALUE);
    value = zhashx_lookup (ctx->kvs, key);
    pmi_simple_server_kvs_get_complete (ctx->pmi, client, value);

    return 0;
}

static int s_send_response (void *client, const char *buf)
{
    int *rfd = client;

    return dprintf (*rfd, "%s", buf);
}

static void s_buf_cb (flux_reactor_t *r,
                      flux_watcher_t *w,
                      int revents,
                      void *arg)
{
    struct client *cli = arg;
    struct pmi_server_context *ctx = cli->ctx;
    int nbytes;

    assert (ctx->magic == MAGIC_VALUE);

    nbytes = cbuf_write_from_fd (cli->cbuf, cli->sfd, -1, NULL);
    if (nbytes < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN)
            return;
        goto error;
    }
    if (nbytes == 0) { // premature EOF
        errno = EIO;
        goto error;
    }
    // truncation is not possible - buf is the same size as cbuf
    while ((nbytes = cbuf_read_line (cli->cbuf,
                                     cli->buf,
                                     sizeof (cli->buf),
                                     1)) != 0) {
        if (nbytes < 0) // programming error
            BAIL_OUT ("cbuf_read_line failed: %s", strerror (errno));
        int rc = pmi_simple_server_request (ctx->pmi, cli->buf, cli, cli->rank);
        if (rc < 0)
            goto error;
        if (rc == 1) {
            flux_watcher_stop (w); // normal exit
            break;
        }
    }
    return;
error:
    flux_reactor_stop_error (r);
}

static int s_barrier_enter (void *arg)
{
    struct pmi_server_context *ctx = arg;

    assert (ctx->magic == MAGIC_VALUE);
    if (ctx->rig_barrier_entry_failure)
        return -1;
    if (ctx->rig_barrier_exit_failure)
        pmi_simple_server_barrier_complete (ctx->pmi, PMI_FAIL);
    else
        pmi_simple_server_barrier_complete (ctx->pmi, 0);
    return 0;
}

static void *server_thread (void *arg)
{
    struct pmi_server_context *ctx = arg;
    flux_reactor_t *reactor = NULL;
    flux_watcher_t **w;
    int i;

    assert (ctx->magic == MAGIC_VALUE);
    if (!(reactor = flux_reactor_create (0)))
        BAIL_OUT ("flux_reactor_create failed");
    if (!(w = calloc (ctx->size, sizeof (w[0]))))
        BAIL_OUT ("calloc failed");
    for (i = 0; i < ctx->size; i++) {
        fd_set_nonblocking (ctx->cli[i].sfd);
        if (!(w[i] = flux_fd_watcher_create (reactor,
                                             ctx->cli[i].sfd,
                                             FLUX_POLLIN,
                                             s_buf_cb,
                                             &ctx->cli[i])))
            BAIL_OUT ("could not create fd watcher: %s", strerror (errno));
        flux_watcher_start (w[i]);
    }
    if (flux_reactor_run (reactor, 0) < 0)
        BAIL_OUT ("flux_reactor_run failed");
    for (i = 0; i < ctx->size; i++)
        flux_watcher_destroy (w[i]);
    free (w);
    flux_reactor_destroy (reactor);
    return NULL;
}

void s_trace (void *arg, const char *buf)
{
    diag ("%s", buf);
}

void s_warn (void *arg, const char *buf)
{
    diag ("WARN: %s", buf);
}

struct pmi_server_context *pmi_server_create (int *cfd, int size)
{
    char pmi_fd[16];
    struct pmi_simple_ops server_ops = {
        .kvs_put = s_kvs_put,
        .kvs_get = s_kvs_get,
        .barrier_enter = s_barrier_enter,
        .response_send = s_send_response,
        .debug_trace = s_trace,
        .warn = s_warn,
    };
    struct pmi_server_context *ctx;
    int i;

    if (!(ctx = calloc (1, sizeof (*ctx))))
        BAIL_OUT ("calloc failed");
    ctx->magic = MAGIC_VALUE;
    ctx->size = size;
    if (!(ctx->cli = calloc (size, sizeof (ctx->cli[0]))))
        BAIL_OUT ("calloc bailed");

    if (!(ctx->kvs = zhashx_new ()))
        BAIL_OUT ("zhash_new failed");
    zhashx_set_duplicator (ctx->kvs, dup_string);
    zhashx_set_destructor (ctx->kvs, destroy_string);
    zhashx_update (ctx->kvs, "test_key", "test_val");

    for (i = 0; i < size; i++) {
        int fd[2];
#ifdef SOCK_CLOEXEC
        if (socketpair (PF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0, fd) < 0)
            BAIL_OUT ("socketpair failed");
#else
        if (socketpair (PF_LOCAL, SOCK_STREAM, 0, fd) < 0
            || fd_set_cloexec (fd[0]) < 0
            || fd_set_cloexec (fd[1]) < 0)
            BAIL_OUT ("socketpair failed");
#endif

        cfd[i] = fd[0];
        ctx->cli[i].sfd = fd[1];
        ctx->cli[i].rank = i;
        ctx->cli[i].ctx = ctx;
        if (!(ctx->cli[i].cbuf = cbuf_create (SIMPLE_MAX_PROTO_LINE,
                                              SIMPLE_MAX_PROTO_LINE))
            || cbuf_opt_set (ctx->cli[i].cbuf,
                             CBUF_OPT_OVERWRITE,
                             CBUF_NO_DROP) < 0)
            BAIL_OUT ("could not create circular buffer for client %d", i);
    }

    ctx->pmi = pmi_simple_server_create (server_ops,
                                         42,            // appnum
                                         ctx->size,     // size
                                         ctx->size,     // local procs
                                         "bleepgorp",   // kvsname
                                         PMI_SIMPLE_SERVER_TRACE,
                                         ctx);
    if (!ctx->pmi)
        BAIL_OUT ("pmi_simple_server_create failed");

    snprintf (pmi_fd, sizeof (pmi_fd), "%d", cfd[0]);
    setenv ("PMI_FD", pmi_fd, 1);

    if (pthread_create (&ctx->t, NULL, server_thread, ctx) != 0)
        BAIL_OUT ("pthread_create failed");
    return ctx;
}

void pmi_server_destroy (struct pmi_server_context *ctx)
{
    if (pthread_join (ctx->t, NULL) != 0)
        BAIL_OUT ("pthread_join failed");
    pmi_simple_server_destroy (ctx->pmi);
    zhashx_destroy (&ctx->kvs);
    for (int i = 0; i < ctx->size; i++)
        cbuf_destroy (ctx->cli[i].cbuf);
    free (ctx->cli);
    ctx->magic = ~MAGIC_VALUE;
    free (ctx);
}

void pmi_set_barrier_entry_failure (struct pmi_server_context *ctx, int val)
{
    ctx->rig_barrier_entry_failure = val;
}

void pmi_set_barrier_exit_failure (struct pmi_server_context *ctx, int val)
{
    ctx->rig_barrier_exit_failure = val;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
