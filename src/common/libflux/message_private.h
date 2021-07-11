/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_CORE_MESSAGE_PRIVATE_H
#define _FLUX_CORE_MESSAGE_PRIVATE_H

#include <stdint.h>
#include "src/common/libczmqcontainers/czmq_containers.h"

/* 'aux' for any auxiliary data user may wish to associate with iovec,
 * user is responsible to free/destroy */
struct msg_iovec {
    const void *data;
    size_t size;
    void *aux;
};

int flux_iovec_to_msg (flux_msg_t *msg,
                       struct msg_iovec *iov,
                       int iovcnt);

int flux_msg_to_iovec (const flux_msg_t *msg,
                       struct msg_iovec **iovp,
                       int *iovcntp);

#endif /* !_FLUX_CORE_MESSAGE_PRIVATE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

