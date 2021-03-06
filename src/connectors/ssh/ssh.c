/************************************************************\
 * Copyright 2016 Lawrence Livermore National Security, LLC
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
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <signal.h>
#include <poll.h>
#include <unistd.h>
#include <argz.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/popen2.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libyuarel/yuarel.h"
#include "src/common/librouter/usock.h"

struct ssh_connector {
    struct usock_client *uclient;
    struct popen2_child *p;
    flux_t *h;
};

static const struct flux_handle_ops handle_ops;

static int op_pollevents (void *impl)
{
    struct ssh_connector *ctx = impl;

    return usock_client_pollevents (ctx->uclient);
}

static int op_pollfd (void *impl)
{
    struct ssh_connector *ctx = impl;

    return usock_client_pollfd (ctx->uclient);
}

static int op_send (void *impl, const flux_msg_t *msg, int flags)
{
    struct ssh_connector *ctx = impl;

    return usock_client_send (ctx->uclient, msg, flags);
}

static flux_msg_t *op_recv (void *impl, int flags)
{
    struct ssh_connector *ctx = impl;

    return usock_client_recv (ctx->uclient, flags);
}

static int op_event_subscribe (void *impl, const char *topic)
{
    struct ssh_connector *ctx = impl;
    flux_future_t *f;
    int rc = 0;

    if (!(f = flux_rpc_pack (ctx->h, "local.sub", FLUX_NODEID_ANY, 0,
                             "{ s:s }", "topic", topic)))
        goto done;
    if (flux_future_get (f, NULL) < 0)
        goto done;
    rc = 0;
done:
    flux_future_destroy (f);
    return rc;
}

static int op_event_unsubscribe (void *impl, const char *topic)
{
    struct ssh_connector *ctx = impl;
    flux_future_t *f;
    int rc = 0;

    if (!(f = flux_rpc_pack (ctx->h, "local.unsub", FLUX_NODEID_ANY, 0,
                             "{ s:s }", "topic", topic)))
        goto done;
    if (flux_future_get (f, NULL) < 0)
        goto done;
    rc = 0;
done:
    flux_future_destroy (f);
    return rc;
}

static void op_fini (void *impl)
{
    struct ssh_connector *ctx = impl;

    if (ctx) {
        int saved_errno = errno;
        usock_client_destroy (ctx->uclient);
        pclose2 (ctx->p);
        free (ctx);
        errno = saved_errno;
    }
}

static char *which (const char *prog, char *buf, size_t size)
{
    char *path = getenv ("PATH");
    char *cpy = path ? strdup (path) : NULL;
    char *dir, *saveptr = NULL, *a1 = cpy;
    struct stat sb;
    char *result = NULL;

    if (cpy) {
        while ((dir = strtok_r (a1, ":", &saveptr))) {
            snprintf (buf, size, "%s/%s", dir, prog);
            if (stat (buf, &sb) == 0 && S_ISREG (sb.st_mode)
                                     && access (buf, X_OK) == 0) {
                result = buf;
                break;
            }
            a1 = NULL;
        }
    }
    free (cpy);
    return result;
}

/* uri_path is interpreted as:
 *   [user@]hostname[:port]/unix-path
 * Sets *argvp, *argbuf (caller must free).
 * The last argv[] element is a NULL (required by popen2).
 * Returns 0 on success, -1 on failure with errno set.
 */
int build_ssh_command (const char *uri_path,
                       const char *ssh_cmd,
                       const char *flux_cmd,
                       const char *ld_lib_path,
                       char ***argvp,
                       char **argbuf)
{
    char buf[PATH_MAX + 1];
    struct yuarel yuri;
    char *cpy;
    char *argz = NULL;
    size_t argz_len = 0;
    int argc;
    char **argv;

    if (asprintf (&cpy, "ssh://%s", uri_path) < 0)
        return -1;
    if (yuarel_parse (&yuri, cpy) < 0) {
        errno = EINVAL;
        goto error;
    }
    if (!yuri.path || !yuri.host || yuri.query || yuri.fragment) {
        errno = EINVAL;
        goto error;
    }
    /* ssh */
    if (argz_add (&argz, &argz_len, ssh_cmd) != 0)
        goto nomem;

    /* [-p port] */
    if (yuri.port != 0) {
        (void)snprintf (buf, sizeof (buf), "%d", yuri.port);
        if (argz_add (&argz, &argz_len, "-p") != 0)
            goto nomem;
        if (argz_add (&argz, &argz_len, buf) != 0)
            goto nomem;
    }
    /* [user@]hostname */
    if (yuri.username) {
        (void)snprintf (buf, sizeof (buf), "%s@%s", yuri.username, yuri.host);
        if (argz_add (&argz, &argz_len, buf) != 0)
            goto nomem;
    }
    else {
        if (argz_add (&argz, &argz_len, yuri.host) != 0)
            goto nomem;
    }

    /* LD_LIBRARY_PATH */
    if (ld_lib_path) {
        (void)snprintf (buf, sizeof (buf), "LD_LIBRARY_PATH=%s", ld_lib_path);
        if (argz_add (&argz, &argz_len, buf) != 0)
            goto nomem;
    }

    /* flux-relay */
    if (argz_add (&argz, &argz_len, flux_cmd) != 0)
        goto nomem;
    if (argz_add (&argz, &argz_len, "relay") != 0)
        goto nomem;

    /* path */
    (void)snprintf (buf, sizeof (buf), "/%s", yuri.path);
    if (argz_add (&argz, &argz_len, buf) != 0)
        goto nomem;

    /* Convert argz to argv needed by popen2()
     */
    argc = argz_count (argz, argz_len) + 1;
    if (!(argv = calloc (argc, sizeof (argv[0]))))
        goto error;
    argz_extract (argz, argz_len, argv);

    free (cpy);

    *argvp = argv;
    *argbuf = argz;

    return 0;
nomem:
    errno = ENOMEM;
error:
    ERRNO_SAFE_WRAP (free, cpy);
    ERRNO_SAFE_WRAP (free, argz);
    return -1;
}

flux_t *connector_init (const char *path, int flags)
{
    struct ssh_connector *ctx;
    char buf[PATH_MAX + 1];
    const char *ssh_cmd;
    const char *flux_cmd;
    const char *ld_lib_path;
    char *argbuf = NULL;
    char **argv = NULL;

    if (!(ctx = calloc (1, sizeof (*ctx))))
        return NULL;

    /* FLUX_SSH may be used to select a different remote shell command
     * from the compiled-in default.  Most rsh variants ought to work.
     */
    if (!(ssh_cmd = getenv ("FLUX_SSH")))
        ssh_cmd = PATH_SSH;
    /* FLUX_SSH_RCMD may be used to select a different path to the flux
     * command front end than the default.  The default is to use the one
     * from the client's PATH.
     */
    flux_cmd = getenv ("FLUX_SSH_RCMD");
    if (!flux_cmd)
        flux_cmd = which ("flux", buf, sizeof (buf));
    if (!flux_cmd)
        flux_cmd = "flux"; // maybe this will work for installed version

    /* ssh and rsh do not forward environment variables, thus LD_LIBRARY_PATH
     * is not guaranteed to be set on the remote node where the flux command is
     * run.  If the flux command is linked against libraries that can only be
     * found when LD_LIBRARY_PATH is set, then the flux command will fail to
     * run over ssh.  Grab the client-side LD_LIBRARY_PATH so that we can
     * manually forward it. See flux-core issue #3457 for more details.
     */
    ld_lib_path = getenv ("LD_LIBRARY_PATH");

    /* Construct argv for ssh command from uri "path" (non-scheme part)
     * and flux and ssh command paths.
     */
    if (build_ssh_command (path, ssh_cmd, flux_cmd, ld_lib_path,
                           &argv, &argbuf) < 0)
        goto error;

    /* Start the ssh command
     */
    if (!(ctx->p = popen2 (ssh_cmd, argv))) {
        /* If popen fails because ssh cannot be found, flux_open()
         * will just fail with errno = ENOENT, which is not all that helpful.
         * Emit a hint on stderr even though this is perhaps not ideal.
         */
        fprintf (stderr, "ssh-connector: %s: %s\n", ssh_cmd, strerror (errno));
        fprintf (stderr, "Hint: set FLUX_SSH in environment to override\n");
        goto error;
    }
    /* The ssh command is the "client" here, tunneling through flux-relay
     * to a remote local:// connector.  The "auth handshake" is performed
     * between this client and flux-relay.  The byte returned is always zero,
     * but performing this handshake forces some errors to be handled here
     * inside flux_open() rather than in some less obvious context later.
     */
    if (!(ctx->uclient = usock_client_create (popen2_get_fd (ctx->p))))
        goto error;
    if (!(ctx->h = flux_handle_create (ctx, &handle_ops, flags)))
        goto error;
    free (argbuf);
    free (argv);
    return ctx->h;
error:
    if (ctx) {
        if (ctx->h)
            flux_handle_destroy (ctx->h); /* calls op_fini */
        else
            op_fini (ctx);
    }
    ERRNO_SAFE_WRAP (free, argbuf);
    ERRNO_SAFE_WRAP (free, argv);
    return NULL;
}

static const struct flux_handle_ops handle_ops = {
    .pollfd = op_pollfd,
    .pollevents = op_pollevents,
    .send = op_send,
    .recv = op_recv,
    .event_subscribe = op_event_subscribe,
    .event_unsubscribe = op_event_unsubscribe,
    .impl_destroy = op_fini,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
