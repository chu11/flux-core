/*****************************************************************************\
 *  Copyright (c) 2015 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdbool.h>
#include <ctype.h>
#include <czmq.h>
#include <flux/core.h>
#include <jansson.h>

#include "kvsroot.h"

struct kvsroot_mgr {
    zhash_t *roothash;
    zlist_t *removelist;
    bool iterating_roots;
    flux_t *h;
    void *arg;
};

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

kvsroot_mgr_t *kvsroot_mgr_create (flux_t *h, void *arg)
{
    kvsroot_mgr_t *km = NULL;
    int saved_errno;

    if (!(km = calloc (1, sizeof (*km)))) {
        saved_errno = ENOMEM;
        goto error;
    }
    if (!(km->roothash = zhash_new ())) {
        saved_errno = ENOMEM;
        goto error;
    }
    if (!(km->removelist = zlist_new ())) {
        saved_errno = ENOMEM;
        goto error;
    }
    km->iterating_roots = false;
    km->h = h;
    km->arg = arg;
    return km;

 error:
    kvsroot_mgr_destroy (km);
    errno = saved_errno;
    return NULL;
}

void kvsroot_mgr_destroy (kvsroot_mgr_t *km)
{
    if (km) {
        if (km->roothash)
            zhash_destroy (&km->roothash);
        if (km->removelist)
            zlist_destroy (&km->removelist);
        free (km);
    }
}

int kvsroot_mgr_root_count (kvsroot_mgr_t *km)
{
    return zhash_size (km->roothash);
}

static void kvsroot_destroy (void *data)
{
    if (data) {
        struct kvsroot *root = data;
        if (root->namespace)
            free (root->namespace);
        if (root->cm)
            commit_mgr_destroy (root->cm);
        if (root->watchlist)
            wait_queue_destroy (root->watchlist);
        free (data);
    }
}

kvsroot_t *kvsroot_mgr_create_root (kvsroot_mgr_t *km,
                                    struct cache *cache,
                                    const char *hash_name,
                                    const char *namespace,
                                    int flags)
{
    kvsroot_t *root;
    int save_errnum;

    /* Don't modify hash while iterating */
    if (km->iterating_roots) {
        errno = EAGAIN;
        return NULL;
    }

    if (!(root = calloc (1, sizeof (*root)))) {
        flux_log_error (km->h, "calloc");
        return NULL;
    }

    if (!(root->namespace = strdup (namespace))) {
        flux_log_error (km->h, "strdup");
        goto error;
    }

    if (!(root->cm = commit_mgr_create (cache,
                                        root->namespace,
                                        hash_name,
                                        km->h,
                                        km->arg))) {
        flux_log_error (km->h, "commit_mgr_create");
        goto error;
    }

    if (!(root->watchlist = wait_queue_create ())) {
        flux_log_error (km->h, "wait_queue_create");
        goto error;
    }

    root->flags = flags;
    root->remove = false;
    root->km = km;

    if (zhash_insert (km->roothash, namespace, root) < 0) {
        flux_log_error (km->h, "zhash_insert");
        goto error;
    }

    if (!zhash_freefn (km->roothash, namespace, kvsroot_destroy)) {
        flux_log_error (km->h, "zhash_freefn");
        save_errnum = errno;
        zhash_delete (km->roothash, namespace);
        errno = save_errnum;
        goto error;
    }

    return root;

 error:
    save_errnum = errno;
    kvsroot_destroy (root);
    errno = save_errnum;
    return NULL;
}

int kvsroot_mgr_remove_root (kvsroot_mgr_t *km, const char *namespace)
{
    /* don't want to remove while iterating, so save namespace for
     * later removal */
    if (km->iterating_roots) {
        char *str = strdup (namespace);

        if (!str) {
            errno = ENOMEM;
            return -1;
        }

        if (zlist_append (km->removelist, str) < 0) {
            free (str);
            errno = ENOMEM;
            return -1;
        }
    }
    else
        zhash_delete (km->roothash, namespace);
    return 0;
}

kvsroot_t *kvsroot_mgr_lookup_root (kvsroot_mgr_t *km,
                                    const char *namespace)
{
    return zhash_lookup (km->roothash, namespace);
}

kvsroot_t *kvsroot_mgr_lookup_root_safe (kvsroot_mgr_t *km,
                                         const char *namespace)
{
    kvsroot_t *root;

    if ((root = kvsroot_mgr_lookup_root (km, namespace))) {
        if (root->remove)
            root = NULL;
    }
    return root;
}

int kvsroot_mgr_iter_roots (kvsroot_mgr_t *km, kvsroot_root_f cb, void *arg)
{
    kvsroot_t *root;
    char *namespace;

    km->iterating_roots = true;

    root = zhash_first (km->roothash);
    while (root) {
        int ret;

        if ((ret = cb (root, arg)) < 0)
            goto error;

        if (ret == 1)
            break;

        root = zhash_next (km->roothash);
    }

    km->iterating_roots = false;

    while ((namespace = zlist_pop (km->removelist))) {
        kvsroot_mgr_remove_root (km, namespace);
        free (namespace);
    }

    return 0;

error:
    while ((namespace = zlist_pop (km->removelist)))
        free (namespace);
    km->iterating_roots = false;
    return -1;
}

static int _commit_ready_cb (kvsroot_t *root, void *arg)
{
    bool *ready = arg;

    if (commit_mgr_commits_ready (root->cm)) {
        if (ready)
            (*ready) = true;
        return 1;
    }

    return 0;
}

int kvsroot_mgr_commits_ready (kvsroot_mgr_t *km, bool *ready)
{
    if (kvsroot_mgr_iter_roots (km, _commit_ready_cb, ready) < 0) {
        flux_log_error (km->h, "%s: kvsroot_mgr_iter_roots", __FUNCTION__);
        return -1;
    }
    return 0;
}

struct commits_apply_data {
    bool merge;
    kvsroot_commit_apply_f cb;
    void *arg;
};

static int commit_check_root_cb (kvsroot_t *root, void *arg)
{
    struct commits_apply_data
    struct kvs_cb_data *cbd = arg;
    commit_mgr_t *cm = kvsroot_get_commit_mgr (root);
    commit_t *c;

    if ((c = commit_mgr_get_ready_commit (cm))) {
        if (cbd->ctx->commit_merge) {
            /* if merge fails, set errnum in commit_t, let
             * commit_apply() handle error handling.
             */
            if (commit_mgr_merge_ready_commits (cm) < 0)
                commit_set_aux_errnum (c, errno);
        }

        /* It does not matter if root has been marked for removal,
         * we want to process and clear all lingering ready
         * commits in this commit mgr
         */
        commit_apply (c);
    }

    return 0;
}

int kvsroot_mgr_commits_apply (kvsroot_mgr_t *km, bool merge,
                               kvsroot_commit_apply_f cb, void *arg)
{
    struct commits_apply_data cad = { .merge = merge .cb = cb, .arg = arg };

    if (kvsroot_mgr_iter_roots (km, _commit_apply_cb, &cad) < 0) {
        flux_log_error (km->h, "%s: kvsroot_mgr_iter_roots", __FUNCTION__);
        return -1;
    }
    return 0;
}

const char *kvsroot_get_namespace (kvsroot_t *root)
{
    return root->namespace;
}

int kvsroot_get_sequence (kvsroot_t *root)
{
    return root->seq;
}

const char *kvsroot_get_rootref (kvsroot_t *root)
{
    return root->ref;
}

commit_mgr_t *kvsroot_get_commit_mgr (kvsroot_t *root)
{
    return root->cm;
}

void kvsroot_set_remove_flag (kvsroot_t *root, bool remove)
{
    root->remove = remove;
}

bool kvsroot_get_remove_flag (kvsroot_t *root)
{
    return root->remove;
}

void kvsroot_set_flags (kvsroot_t *root, int flags)
{
    root->flags = flags;
}

int kvsroot_get_flags (kvsroot_t *root)
{
    return root->flags;
}

int kvsroot_watchlist_add (kvsroot_t *root, wait_t *wait)
{
    if (wait_addqueue (root->watchlist, wait) < 0) {
        flux_log_error (root->km->h, "%s: wait_addqueue", __FUNCTION__);
        return -1;
    }
    return 0;
}

int kvsroot_watchlist_run (kvsroot_t *root, int current_epoch)
{
    if (wait_queue_length (root->watchlist) > 0) {
        if (wait_runqueue (root->watchlist) < 0) {
            flux_log_error (root->km->h, "%s: wait_runqueue", __FUNCTION__);
            return -1;
        }
        root->watchlist_lastrun_epoch = current_epoch;
    }
    return 0;
}

int kvsroot_watchlist_age (kvsroot_t *root, int current_epoch)
{
    return (current_epoch - root->watchlist_lastrun_epoch);
}

int kvsroot_watchlist_length (kvsroot_t *root)
{
    return wait_queue_length (root->watchlist);
}

int kvsroot_watchlist_wait_destroy_msg (kvsroot_t *root, wait_test_msg_f cb,
                                        void *arg)
{
    if (wait_destroy_msg (root->watchlist, cb, arg) < 0) {
        flux_log_error (root->km->h, "%s: wait_destroy_msg", __FUNCTION__);
        return -1;
    }
    return 0;
}

int kvsroot_setroot (kvsroot_t *root, const char *rootref, int rootseq,
                     int epoch)
{
    if (rootseq == 0 || rootseq > kvsroot_get_sequence (root)) {
        assert (strlen (rootref) < sizeof (blobref_t));
        strcpy (root->ref, rootref);
        root->seq = rootseq;
        if (kvsroot_watchlist_run (root, epoch) < 0) {
            /* Technically should save orig values and revert them on
             * error here, but won't to avoid excessive copies */
            flux_log_error (root->km->h, "%s: kvsroot_watchlist_run",
                            __FUNCTION__);
            return -1;
        }
    }
    return 0;
}

bool kvsroot_processing_done (kvsroot_t *root)
{
    if (!wait_queue_length (root->watchlist)
        && !commit_mgr_fences_count (root->cm)
        && !commit_mgr_ready_commit_count (root->cm))
        return true;
    return false;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
