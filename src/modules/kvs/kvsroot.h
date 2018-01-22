#ifndef _FLUX_KVS_KVSROOT_H
#define _FLUX_KVS_KVSROOT_H

#include <stdbool.h>
#include <flux/core.h>

#include "cache.h"
#include "commit.h"
#include "waitqueue.h"
#include "src/common/libutil/blobref.h"

typedef struct kvsroot_mgr kvsroot_mgr_t;

struct kvsroot {
    char *namespace;
    int seq;
    blobref_t ref;
    commit_mgr_t *cm;
    waitqueue_t *watchlist;
    int watchlist_lastrun_epoch;
    int flags;
    bool remove;
    kvsroot_mgr_t *km;
};

/* return -1 on error, 0 on success, 1 on success & to stop iterating */
typedef int (*kvsroot_root_f)(struct kvsroot *root, void *arg);

kvsroot_mgr_t *kvsroot_mgr_create (flux_t *h, void *arg);

void kvsroot_mgr_destroy (kvsroot_mgr_t *km);

int kvsroot_mgr_root_count (kvsroot_mgr_t *km);

struct kvsroot *kvsroot_mgr_create_root (kvsroot_mgr_t *km,
                                         struct cache *cache,
                                         const char *hash_name,
                                         const char *namespace,
                                         int flags);

int kvsroot_mgr_remove_root (kvsroot_mgr_t *km, const char *namespace);

struct kvsroot *kvsroot_mgr_lookup_root (kvsroot_mgr_t *km,
                                         const char *namespace);

struct kvsroot *kvsroot_mgr_lookup_root_safe (kvsroot_mgr_t *km,
                                              const char *namespace);

int kvsroot_mgr_iter_roots (kvsroot_mgr_t *km, kvsroot_root_f cb, void *arg);

const char *kvsroot_get_namespace (struct kvsroot *root);
commit_mgr_t *kvsroot_get_commit_mgr (struct kvsroot *root);
waitqueue_t *kvsroot_get_watchlist (struct kvsroot *root);

void kvsroot_set_remove_flag (struct kvsroot *root, bool remove);
bool kvsroot_get_remove_flag (struct kvsroot *root);

void kvsroot_set_sequence (struct kvsroot *root, int sequence);
int kvsroot_get_sequence (struct kvsroot *root);

void kvsroot_set_rootref (struct kvsroot *root, const char *rootref);
const char *kvsroot_get_rootref (struct kvsroot *root);

void kvsroot_set_flags (struct kvsroot *root, int flags);
int kvsroot_get_flags (struct kvsroot *root);

void kvsroot_set_watchlist_lastrun_epoch (struct kvsroot *root, int epoch);
int kvsroot_get_watchlist_lastrun_epoch (struct kvsroot *root);

/* indicates if no processing remains in kvsroot (no watchers, no
 * fences, no committing).  Used for checks before cleaning up */
bool kvsroot_processing_done (struct kvsroot *root);

#endif /* !_FLUX_KVS_KVSROOT_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
