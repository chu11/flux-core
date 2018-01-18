#ifndef _FLUX_KVS_KVSROOT_H
#define _FLUX_KVS_KVSROOT_H

#include <stdbool.h>
#include <flux/core.h>
#include <czmq.h>

#include "cache.h"
#include "commit.h"
#include "waitqueue.h"
#include "src/common/libutil/blobref.h"

struct kvsroot {
    char *namespace;
    int seq;
    blobref_t ref;
    commit_mgr_t *cm;
    waitqueue_t *watchlist;
    int watchlist_lastrun_epoch;
    int flags;
    bool remove;
};

void kvsroot_remove (zhash_t *roothash, const char *namespace);

struct kvsroot *kvsroot_lookup (zhash_t *roothash, const char *namespace);

struct kvsroot *kvsroot_lookup_safe (zhash_t *roothash, const char *namespace);

struct kvsroot *kvsroot_create (zhash_t *roothash,
                                struct cache *cache,
                                const char *hash_name,
                                const char *namespace,
                                int flags,
                                flux_t *h,
                                void *arg);

#endif /* !_FLUX_KVS_KVSROOT_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
