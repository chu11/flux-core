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
#include <flux/core.h>

static void depend_cb (flux_future_t *f, void *arg)
{
    flux_t *h = arg;
    if (flux_future_get (f, NULL) < 0) {
        if (errno == ENODATA) {
            flux_log_error (h, "depend callback, module unloaded");
            flux_reactor_stop (flux_get_reactor (h));
        }
        else {
            if (errno == ENOSYS)
                flux_log_error (h, "depend callback, module not loaded");
            else
                flux_log_error (h, "depend callback error");
            flux_reactor_stop_error (flux_get_reactor (h));
        }
        return;
    }
    /* success, stop dependency reactor */
    flux_reactor_stop (flux_get_reactor (h));
    flux_future_reset (f);
}

static flux_future_t *check_dependency (flux_t *h)
{
    flux_future_t *f = NULL;
    int rc;
    flux_reactor_t *r = flux_get_reactor (h);

    if (!(f = flux_rpc_pack (h,
                             "testmod.depend",
                             FLUX_NODEID_ANY,
                             FLUX_RPC_STREAMING,
                             "{s:s}",
                             "name",
                             "dependmod"))) {
        flux_log_error (h, "flux_rpc_pack");
        return NULL;
    }
    if (flux_future_then (f, -1., depend_cb, h) < 0) {
        flux_future_destroy (f);
        flux_log_error (h, "flux_future_then");
        return NULL;
    }
    if ((rc = flux_reactor_run (r, 0)) < 0) {
        flux_log_error (h, "module can't be loaded, dependency issue");
        flux_future_destroy (f);
        return NULL;
    }
    return f;
}

static void dependmod_test_cb (flux_t *h,
                               flux_msg_handler_t *mh,
                               const flux_msg_t *msg,
                               void *arg)
{
    if (flux_respond_pack (h, msg, "{s:i}", "dependmodtest", 1) < 0)
        flux_log_error (h, "error responding to request");
}

static struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST, "dependmod.test", dependmod_test_cb, 0 },
    FLUX_MSGHANDLER_TABLE_END,
};

int mod_flags (void)
{
    return FLUX_MODFLAG_NO_SET_RUNNING;
}

int mod_main (flux_t *h, int argc, char **argv)
{
    flux_msg_handler_t **handlers = NULL;
    flux_future_t *f;
    int rc;

    if (!(f = check_dependency (h)))
        return -1;

    if (flux_msg_handler_addvec (h,
                                 htab,
                                 NULL,
                                 &handlers) < 0) {
        flux_future_destroy (f);
        flux_log_error (h, "flux_msg_handler_addvec");
        return -1;
    }

    if (flux_module_set_running (h) < 0) {
        flux_log_error (h, "flux_module_set_running");
        return -1;
    }

    if ((rc = flux_reactor_run (flux_get_reactor (h), 0)) < 0) {
        flux_future_destroy (f);
        flux_log_error (h, "flux_reactor_run");
        return -1;
    }

    flux_future_destroy (f);
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
