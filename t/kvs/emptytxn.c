/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* write "nothing" to the KVS
 *
 * - this cannot be done via the `flux kvs` command, thus the need
 * for this utility test.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"

int main (int argc, char *argv[])
{
    flux_t *h;
    flux_kvs_txn_t *txn;
    flux_future_t *f;

    if (argc != 1) {
        fprintf (stderr, "Usage: emptycommit");
        exit (1);
    }

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (!(txn = flux_kvs_txn_create ()))
        log_err_exit ("flux_kvs_txn_create");
    if (!(f = flux_kvs_commit (h, NULL, 0, txn)))
        log_err_exit ("flux_kvs_commit");
    if (flux_future_get (f, NULL) < 0)
        log_err_exit ("flux_future_get");
    flux_future_destroy (f);
    flux_kvs_txn_destroy (txn);
    flux_close (h);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
