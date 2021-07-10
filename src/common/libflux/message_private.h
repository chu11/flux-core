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

/* PROTO consists of 4 byte prelude followed by a fixed length
 * array of u32's in network byte order.
 */
#define PROTO_MAGIC         0x8e
#define PROTO_VERSION       1

#define PROTO_OFF_MAGIC     0 /* 1 byte */
#define PROTO_OFF_VERSION   1 /* 1 byte */
#define PROTO_OFF_TYPE      2 /* 1 byte */
#define PROTO_OFF_FLAGS     3 /* 1 byte */
#define PROTO_OFF_U32_ARRAY 4

/* aux1
 *
 * request - nodeid
 * response - errnum
 * event - sequence
 * keepalive - errnum
 *
 * aux2
 *
 * request - matchtag
 * response - matchtag
 * event - not used
 * keepalive - status
 */
#define PROTO_IND_USERID    0
#define PROTO_IND_ROLEMASK  1
#define PROTO_IND_AUX1      2
#define PROTO_IND_AUX2      3

#define PROTO_U32_COUNT     4
#define PROTO_SIZE          4 + (PROTO_U32_COUNT * 4)

/* 'aux' for any auxiliary data user may wish to associate with iovec,
 * user is responsible to free/destroy */
struct msg_iovec {
    const void *data;
    size_t size;
    void *aux;
};

flux_msg_t *flux_msg_create_common (void);

int flux_iovec_to_msg (flux_msg_t *msg,
                       struct msg_iovec *iov,
                       int iovcnt);

int flux_msg_to_iovec (const flux_msg_t *msg,
                       uint8_t *proto,
                       int proto_len,
                       struct msg_iovec **iovp,
                       int *iovcntp);

#endif /* !_FLUX_CORE_MESSAGE_PRIVATE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

