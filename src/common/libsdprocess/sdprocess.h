/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_CORE_SDPROCESS_H
#define _FLUX_CORE_SDPROCESS_H

#include <flux/core.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct flux_sdprocess flux_sdprocess_t;

/* present assumptions:
 * unitname 1 word, no dashes, periods, etc.
 * command is absolute pathed
 * argv & env NULL terminated
 * fd's < 0 if don't want to use
 */
flux_sdprocess_t *flux_sdprocess_exec (flux_t *h,
                                       const char *unitname,
                                       char **argv,
                                       char **envv,
                                       int stdin_fd,
                                       int stdout_fd,
                                       int stderr_fd);

/* for testing */
flux_sdprocess_t *flux_sdprocess_local_exec (flux_reactor_t *r,
                                             const char *unitname,
                                             char **argv,
                                             char **envv,
                                             int stdin_fd,
                                             int stdout_fd,
                                             int stderr_fd);

/* XXX return flux_future_t later */
int flux_sdprocess_wait (flux_sdprocess_t *sdp);

/* XXX flux_sdprocess_wait_unitname */

bool flux_sdprocess_completed (flux_sdprocess_t *sdp);

int flux_sdprocess_exit_status (flux_sdprocess_t *sdp);

/* different than destroy, cleanup leftovers in systemd */
int flux_sdprocess_systemd_cleanup (flux_sdprocess_t *sdp);

void flux_sdprocess_destroy (flux_sdprocess_t *sdp);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_SDPROCESS_H */
