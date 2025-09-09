/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
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
#include <stdlib.h>
#include <jansson.h>
#include <flux/core.h>
#include <time.h>

#include "kvs_checkpoint.h"
#include "src/common/libutil/errno_safe.h"

flux_future_t *kvs_checkpoint_commit (flux_t *h,
                                      const char *rootref,
                                      int sequence,
                                      double timestamp,
                                      int flags)
{
    flux_future_t *f = NULL;
    const char *topic = "content.checkpoint-put";
    int valid_flags = KVS_CHECKPOINT_FLAG_CACHE_BYPASS;

    if (!h || !rootref || (flags & ~valid_flags)) {
        errno = EINVAL;
        return NULL;
    }
    if (timestamp == 0)
        timestamp = flux_reactor_now (flux_get_reactor (h));
    if (flags & KVS_CHECKPOINT_FLAG_CACHE_BYPASS)
        topic = "content-backing.checkpoint-put";

    if (!(f = flux_rpc_pack (h,
                             topic,
                             0,
                             0,
                             "{s:{s:i s:s s:i s:f}}",
                             "value",
                             "version", 1,
                             "rootref", rootref,
                             "sequence", sequence,
                             "timestamp", timestamp)))
        return NULL;

    return f;
}

flux_future_t *kvs_checkpoint_lookup (flux_t *h, int flags)
{
    const char *topic = "content.checkpoint-get";
    int valid_flags = KVS_CHECKPOINT_FLAG_CACHE_BYPASS;

    if (!h || (flags & ~valid_flags)) {
        errno = EINVAL;
        return NULL;
    }
    if (flags & KVS_CHECKPOINT_FLAG_CACHE_BYPASS)
        topic = "content-backing.checkpoint-get";

    return flux_rpc (h, topic, NULL, 0, 0);
}

int kvs_checkpoint_lookup_get (flux_future_t *f, const json_t **checkpoints)
{
    json_t *a;

    if (!f || !checkpoints) {
        errno = EINVAL;
        return -1;
    }

    if (flux_rpc_get_unpack (f,
                             "{s:o}",
                             "value", &a) < 0)
        return -1;

    if (!json_is_array (a)) {
        if (json_is_object (a)) {
            json_t *o;
            /* support backwards compatibility to when single
             * checkpoint returned
             */
            if (!(o = json_pack ("[O]", a))) {
                errno = ENOMEM;
                return -1;
            }
            if (flux_future_aux_set (f,
                                     "checkpoint_array",
                                     o,
                                     (flux_free_f) json_decref) < 0) {
                int save_errno = errno;
                json_decref (o);
                errno = save_errno;
                return -1;
            }
            a = o;
        }
        else {
            errno = EPROTO;
            return -1;
        }
    }

    *checkpoints = a;
    return 0;
}

int kvs_checkpoint_parse_rootref (json_t *checkpoint, const char **rootref)
{
    const char *tmp_rootref;
    int version;

    if (!checkpoint || !rootref) {
        errno = EINVAL;
        return -1;
    }

    if (json_unpack (checkpoint,
                     "{s:i s:s}",
                     "version", &version,
                     "rootref", &tmp_rootref) < 0) {
        errno = EPROTO;
        return -1;
    }

    if (version != 0 && version != 1) {
        errno = EINVAL;
        return -1;
    }

    (*rootref) = tmp_rootref;
    return 0;
}

int kvs_checkpoint_parse_timestamp (json_t *checkpoint, double *timestamp)
{
    int version;
    double ts = 0.;

    if (!checkpoint || !timestamp) {
        errno = EINVAL;
        return -1;
    }
    if (json_unpack (checkpoint,
                     "{s:i s?f}",
                     "version", &version,
                     "timestamp", &ts) < 0) {
        errno = EPROTO;
        return -1;
    }
    if (version != 0 && version != 1) {
        errno = EINVAL;
        return -1;
    }
    *timestamp = ts;
    return 0;
}

int kvs_checkpoint_parse_sequence (json_t *checkpoint, int *sequence)
{
    int version;
    int seq = 0;

    if (!checkpoint || !sequence) {
        errno = EINVAL;
        return -1;
    }
    if (json_unpack (checkpoint,
                     "{s:i s?i}",
                     "version", &version,
                     "sequence", &seq) < 0) {
        errno = EPROTO;
        return -1;
    }
    if (version != 0 && version != 1) {
        errno = EINVAL;
        return -1;
    }
    *sequence = seq;
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
