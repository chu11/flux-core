/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _ZSOCK_MSG_H
#define _ZSOCK_MSG_H

#include "src/common/libflux/message.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Send message to zeromq socket.
 * Returns 0 on success, -1 on failure with errno set.
 */
int zsock_msg_sendzsock (void *dest, const flux_msg_t *msg);
int zsock_msg_sendzsock_ex (void *dest, const flux_msg_t *msg, bool nonblock);

/* Receive a message from zeromq socket.
 * Returns message on success, NULL on failure with errno set.
 */
flux_msg_t *zsock_msg_recvzsock (void *dest);

#ifdef __cplusplus
}
#endif

#endif /* !_ZSOCK_MSG_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

