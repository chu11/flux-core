/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/*  drainset.c - a set of drained ranks with timestamp and reason
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <time.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libutil/errprintf.h"
#include "ccan/str/str.h"

#include "drainset.h"

struct draininfo {
    struct idset *ranks;
    double timestamp;
    char *reason;
};

struct drainset {
    zhashx_t *map;
};

static void draininfo_destroy (struct draininfo *d)
{
    if (d) {
        int saved_errno = errno;
        idset_destroy (d->ranks);
        free (d->reason);
        free (d);
        errno = saved_errno;
    }
}

static void draininfo_free (void **item)
{
    if (item) {
        draininfo_destroy (*item);
        *item = NULL;
    }
}

static struct draininfo *draininfo_create_rank (unsigned int rank,
                                                const char *reason,
                                                double timestamp)
{
    struct draininfo *d;
    if (!(d = calloc (1, sizeof (*d)))
        || !(d->ranks = idset_create (0, IDSET_FLAG_AUTOGROW))
        || idset_set (d->ranks, rank) < 0
        || (reason && !(d->reason = strdup (reason))))
        goto error;
    d->timestamp = timestamp;
    return d;
error:
    draininfo_destroy (d);
    return NULL;
}

/* Use "modified Bernstein hash" as employed by zhashx internally, but input
 * is draininfo reason+timestamp instead of a simple NULL-terminated string.
 * Copied from: msg_hash_uuid_matchtag_hasher()
 */
static size_t draininfo_hasher (const void *key)
{
    const struct draininfo *d = key;
    size_t key_hash = 0;
    const char *cp;

    cp = d->reason ? d->reason : "";
    while (*cp)
        key_hash = 33 * key_hash ^ *cp++;
    cp = (const char *) &d->timestamp;
    for (int i = 0; i < sizeof (d->timestamp); i++)
        key_hash = 33 * key_hash ^ *cp++;
    return key_hash;
}

static int drainmap_key_cmp (const void *key1, const void *key2)
{
    const struct draininfo *d1 = key1;
    const struct draininfo *d2 = key2;
    if (d1->timestamp == d2->timestamp) {
        const char *s1 = d1->reason;
        const char *s2 = d2->reason;
        return strcmp (s1 ? s1 : "", s2 ? s2 : "");
    }
    return d1->timestamp < d2->timestamp ? -1 : 1;
}

static zhashx_t *drainmap_create ()
{
    zhashx_t *map;

    if (!(map = zhashx_new ())) {
        errno = ENOMEM;
        return NULL;
    }
    zhashx_set_key_hasher (map, draininfo_hasher);
    zhashx_set_key_comparator (map, drainmap_key_cmp);
    zhashx_set_key_destructor (map, draininfo_free);
    zhashx_set_key_duplicator (map, NULL);
    return map;
}

void drainset_destroy (struct drainset *ds)
{
    if (ds) {
        int saved_errno = errno;
        zhashx_destroy (&ds->map);
        free (ds);
        errno = saved_errno;
    }
}

struct drainset *drainset_create (void)
{
    struct drainset *ds;

    if (!(ds = malloc (sizeof (*ds)))
        || !(ds->map = drainmap_create ()))
        goto error;
    return ds;
error:
    drainset_destroy (ds);
    return NULL;
}

static struct draininfo *drainset_find (struct drainset *ds,
                                        double timestamp,
                                        const char *reason)
{
    struct draininfo tmp = {.timestamp = timestamp, .reason = (char *)reason};
    return zhashx_lookup (ds->map, &tmp);
}

int drainset_drain_rank (struct drainset *ds,
                         unsigned int rank,
                         double timestamp,
                         const char *reason)
{
    int rc = -1;
    struct draininfo *match;
    struct draininfo *new = NULL;
    if (!ds) {
        errno = EINVAL;
        return -1;
    }
    if ((match = drainset_find (ds, timestamp, reason))) {
        if (idset_set (match->ranks, rank) < 0)
            return -1;
        return 0;
    }
    if (!(new = draininfo_create_rank (rank, reason, timestamp))
        || zhashx_insert (ds->map, new, new) < 0) {
        draininfo_destroy (new);
        goto out;
    }
    rc = 0;
out:
    return rc;
}

json_t *drainset_to_json (struct drainset *ds)
{
    json_t *o;
    struct draininfo *d;

    if (!(o = json_object ()))
        goto nomem;
    d = zhashx_first (ds->map);
    while (d) {
        json_t *val;
        char *s;
        if (!(val = json_pack ("{s:f s:s}",
                               "timestamp", d->timestamp,
                               "reason", d->reason ? d->reason : ""))
            || !(s = idset_encode (d->ranks, IDSET_FLAG_RANGE))) {
            json_decref (val);
            goto nomem;
        }
        if (json_object_set_new (o, s, val) < 0) {
            ERRNO_SAFE_WRAP (json_decref, val);
            ERRNO_SAFE_WRAP (free, s);
            goto error;
        }
        free (s);
        d = zhashx_next (ds->map);
    }
    return o;
nomem:
    errno = EPROTO;
error:
    ERRNO_SAFE_WRAP (json_decref, o);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
