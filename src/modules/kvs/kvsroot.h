#ifndef _FLUX_KVS_KVSROOT_H
#define _FLUX_KVS_KVSROOT_H

#include <stdbool.h>
#include <flux/core.h>

#include "cache.h"
#include "commit.h"
#include "waitqueue.h"
#include "src/common/libutil/blobref.h"

typedef struct kvsroot_mgr kvsroot_mgr_t;

typedef struct kvsroot kvsroot_t;

/* return -1 on error, 0 on success, 1 on success & to stop iterating */
typedef int (*kvsroot_root_f)(kvsroot_t *root, void *arg);
typedef int (*kvsroot_commit_apply_f)(kvsroot_t *root, void *arg);

kvsroot_mgr_t *kvsroot_mgr_create (flux_t *h, void *arg);

void kvsroot_mgr_destroy (kvsroot_mgr_t *km);

int kvsroot_mgr_root_count (kvsroot_mgr_t *km);

kvsroot_t *kvsroot_mgr_create_root (kvsroot_mgr_t *km,
                                    struct cache *cache,
                                    const char *hash_name,
                                    const char *namespace,
                                    int flags);

int kvsroot_mgr_remove_root (kvsroot_mgr_t *km, const char *namespace);

kvsroot_t *kvsroot_mgr_lookup_root (kvsroot_mgr_t *km,
                                    const char *namespace);

kvsroot_t *kvsroot_mgr_lookup_root_safe (kvsroot_mgr_t *km,
                                         const char *namespace);

int kvsroot_mgr_iter_roots (kvsroot_mgr_t *km, kvsroot_root_f cb, void *arg);

int kvsroot_mgr_commits_ready (kvsroot_mgr_t *km, bool *ready);
int kvsroot_mgr_commits_apply (kvsroot_mgr_t *km, bool merge,
                               kvsroot_commit_apply_f cb, void *arg);

const char *kvsroot_get_namespace (kvsroot_t *root);
int kvsroot_get_sequence (kvsroot_t *root);
const char *kvsroot_get_rootref (kvsroot_t *root);
commit_mgr_t *kvsroot_get_commit_mgr (kvsroot_t *root);

void kvsroot_set_remove_flag (kvsroot_t *root, bool remove);
bool kvsroot_get_remove_flag (kvsroot_t *root);

void kvsroot_set_flags (kvsroot_t *root, int flags);
int kvsroot_get_flags (kvsroot_t *root);

int kvsroot_watchlist_add (kvsroot_t *root, wait_t *wait);
int kvsroot_watchlist_run (kvsroot_t *root, int current_epoch);
int kvsroot_watchlist_age (kvsroot_t *root, int current_epoch);
int kvsroot_watchlist_length (kvsroot_t *root);
int kvsroot_watchlist_wait_destroy_msg (kvsroot_t *root, wait_test_msg_f cb,
                                        void *arg);

int kvsroot_setroot (kvsroot_t *root, const char *rootref, int rootseq,
                     int epoch);

/* indicates if no processing remains in kvsroot (no watchers, no
 * fences, no committing).  Used for checks before cleaning up */
bool kvsroot_processing_done (kvsroot_t *root);

#endif /* !_FLUX_KVS_KVSROOT_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
