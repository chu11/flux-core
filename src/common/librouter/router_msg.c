/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
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
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <inttypes.h>
#include <czmq.h>

#include "router_msg.h"
#include "src/common/libflux/message_private.h"

int router_msg_sendzsock_ex (void *sock, const flux_msg_t *msg, bool nonblock)
{
    void *handle;
    int flags = ZMQ_SNDMORE;
    struct msg_iovec *iov = NULL;
    int iovcnt;
    int count = 0;
    int rc = -1;

    if (!sock || !msg) {
        errno = EINVAL;
        return -1;
    }

    if (flux_msg_to_iovec (msg, &iov, &iovcnt) < 0)
        goto error;

    if (nonblock)
        flags |= ZMQ_DONTWAIT;

    handle = zsock_resolve (sock);
    while (count < iovcnt) {
        if ((count + 1) == iovcnt)
            flags &= ~ZMQ_SNDMORE;
        if (zmq_send (handle,
                      iov[count].data,
                      iov[count].size,
                      flags) < 0)
            goto error;
        count++;
    }
    rc = 0;
error:
    free (iov);
    return rc;
}

int router_msg_sendzsock (void *sock, const flux_msg_t *msg)
{
    return router_msg_sendzsock_ex (sock, msg, false);
}

flux_msg_t *router_msg_recvzsock (void *sock)
{
    void *handle;
    struct msg_iovec *iov = NULL;
    int iovcnt = 0;
    flux_msg_t *msg;
    flux_msg_t *rv = NULL;

    if (!sock) {
        errno = EINVAL;
        return NULL;
    }

    /* N.B. we need to store a zmq_msg_t for each iovec entry so that the
     * memory is available during the call to iovec_to_msg().  We use the
     * msg_iovec's "aux" field to store the entry and then clear/free it
     * later.
     */
    handle = zsock_resolve (sock);
    while (true) {
        struct msg_iovec *tmp;
        if (!(tmp = realloc (iov, (iovcnt + 1) * sizeof (struct msg_iovec))))
            goto error;
        iov = tmp;
        if (!(iov[iovcnt].aux = malloc (sizeof (zmq_msg_t))))
            goto error;
        zmq_msg_init ((zmq_msg_t *)iov[iovcnt].aux);
        if (zmq_recvmsg (handle, (zmq_msg_t *)iov[iovcnt].aux, 0) < 0)
            goto error;
        iov[iovcnt].data = zmq_msg_data ((zmq_msg_t *)iov[iovcnt].aux);
        iov[iovcnt].size = zmq_msg_size ((zmq_msg_t *)iov[iovcnt].aux);
        iovcnt++;
        if (!zsock_rcvmore (handle))
            break;
    }

    if (!(msg = flux_msg_create (FLUX_MSGTYPE_ANY))) {
        errno = ENOMEM;
        goto error;
    }
    if (flux_iovec_to_msg (msg, iov, iovcnt) < 0)
        goto error;
    rv = msg;
error:
    if (iov) {
        int i;
        for (i = 0; i < iovcnt; i++) {
            zmq_msg_close ((zmq_msg_t *)iov[i].aux);
            free (iov[i].aux);
        }
        free (iov);
    }
    return rv;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

