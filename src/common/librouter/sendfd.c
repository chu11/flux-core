/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* sendfd.c - send and receive flux_msg_t over file descriptors
 *
 * These functions use the following encoding for each message:
 *
 *   4 bytes - IOBUF_MAGIC
 *   4 bytes - size in network byte order, includes magic and size
 *   N bytes - message encoded
 *
 * These functions work with file descriptors configured for either
 * blocking or non-blocking modes.  In blocking mode, the iobuf
 * argument may be set to NULL.  In non-blocking mode, an iobuf should
 * be provided to allow messages to be assembled across multiple calls.
 *
 * In non-blocking mode, sendfd() or recfd() may fail with EWOULDBLOCK
 * or EAGAIN.  This should not be treated as an error.  When poll(2) or
 * equivalent indicates that the file descriptor is ready again, sendfd()
 * or recvfd() may be called again, continuing I/O to/from the same iobuf.
 *
 * Separate iobufs are required for sendfd() and recvfd().
 * Call iobuf_init() on an iobuf before its first use.
 * Call iobuf_clean() on an iobuf after its last use.
 * The iobuf is managed by sendfd() and recvfd() across multiple messages.
 *
 * Notes:
 *
 * - to decrease small message latency, the iobuf contains a fixed size
 *   static buffer.  When a message requires more than this fixed size for
 *   assembly, a dynamic buffer is allocated temporarily while that message
 *   is assembled, then it is freed.  The static buffer is sized somewhat
 *   arbitrarily at 4K.
 *
 * - sendfd/recvfd do not encrypt messages, therefore this transport
 *   is only appropriate for use on AF_LOCAL sockets or on file descriptors
 *   tunneled through a secure channel.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <arpa/inet.h>
#include <unistd.h>
#include <flux/core.h>
#include <czmq.h>

/* czmq and ccan both define streq */
#ifdef streq
#undef streq
#endif
#include "src/common/libmsgproto/msgproto.h"
#include "src/common/libflux/message_private.h"

#include "sendfd.h"

#define IOBUF_MAGIC 0xffee0012

void iobuf_init (struct iobuf *iobuf)
{
    memset (iobuf, 0, sizeof (*iobuf));
}

void iobuf_clean (struct iobuf *iobuf)
{
    if (iobuf->buf && iobuf->buf != iobuf->buf_fixed)
        free (iobuf->buf);
    memset (iobuf, 0, sizeof (*iobuf));
}

void encode_count (ssize_t *size, size_t len)
{
    if (len < 255)
        (*size) += 1;
    else
        (*size) += 1 + 4;
    (*size) += len;
}

ssize_t msg_encode_size (const flux_msg_t *msg)
{
    struct msgproto *proto;
    ssize_t size = 0;

    proto = flux_msg_get_proto ((flux_msg_t *)msg);
    if (!proto) {
        errno = EINVAL;
        return -1;
    }
    encode_count (&size, PROTO_SIZE);
    if (proto->flags & FLUX_MSGFLAG_PAYLOAD)
        encode_count (&size, proto->payload_size);
    if (proto->flags & FLUX_MSGFLAG_TOPIC)
        encode_count (&size, strlen (proto->topic));
    if (proto->flags & FLUX_MSGFLAG_ROUTE) {
        struct msgproto_route *r;
        /* route delimeter */
        encode_count (&size, 0);
        list_for_each_rev (&proto->routes, r, msgproto_route_node)
            encode_count (&size, strlen (r->id));
    }
    return size;
}

static ssize_t encode_frame (uint8_t *buf,
                             size_t buf_len,
                             void *frame,
                             size_t frame_size)
{
    ssize_t n = 0;
    if (frame_size < 0xff) {
        if (buf_len < (frame_size + 1)) {
            errno = EINVAL;
            return -1;
        }
        *buf++ = (uint8_t)frame_size;
        n += 1;
    } else {
        if (buf_len < (frame_size + 1 + 4)) {
            errno = EINVAL;
            return -1;
        }
        *buf++ = 0xff;
        *(uint32_t *)buf = htonl (frame_size);
        buf += 4;
        n += 1 + 4;
    }
    if (frame && frame_size)
        memcpy (buf, frame, frame_size);
    return (frame_size + n);
}

int msg_encode (const flux_msg_t *msg, void *buf, size_t size)
{
    struct msgproto *proto;
    uint8_t protoframe[PROTO_SIZE];
    ssize_t total = 0;
    ssize_t n;

    proto = flux_msg_get_proto ((flux_msg_t *)msg);
    if (!proto) {
        errno = EINVAL;
        return -1;
    }
    if (proto->flags & FLUX_MSGFLAG_ROUTE) {
        struct msgproto_route *r;
        list_for_each (&proto->routes, r, msgproto_route_node) {
            if ((n = encode_frame (buf + total,
                                   size - total,
                                   r->id,
                                   strlen (r->id))) < 0)
                return -1;
            total += n;
        }
        /* route delimeter */
        if ((n = encode_frame (buf + total,
                               size - total,
                               NULL,
                               0)) < 0)
            return -1;
        total += n;
    }
    if (proto->flags & FLUX_MSGFLAG_TOPIC) {
        if ((n = encode_frame (buf + total,
                               size - total,
                               proto->topic,
                               strlen (proto->topic))) < 0)
            return -1;
        total += n;
    }
    if (proto->flags & FLUX_MSGFLAG_PAYLOAD) {
        if ((n = encode_frame (buf + total,
                               size - total,
                               proto->payload,
                               proto->payload_size)) < 0)
            return -1;
        total += n;
    }
    if (msgproto_get_protoframe (proto, protoframe, PROTO_SIZE) < 0)
        return -1;
    if ((n = encode_frame (buf + total,
                           size - total,
                           protoframe,
                           PROTO_SIZE)) < 0)
        return -1;
    total += n;
    return 0;
}

int sendfd (int fd, const flux_msg_t *msg, struct iobuf *iobuf)
{
    struct iobuf local;
    struct iobuf *io = iobuf ? iobuf : &local;
    int rc = -1;

    if (fd < 0 || !msg) {
        errno = EINVAL;
        return -1;
    }
    if (!iobuf)
        iobuf_init (&local);
    if (!io->buf) {
        ssize_t s;
        if ((s = msg_encode_size (msg)) < 0)
            goto done;
        io->size = s + 8;
        if (io->size <= sizeof (io->buf_fixed))
            io->buf = io->buf_fixed;
        else if (!(io->buf = malloc (io->size)))
            goto done;
        *(uint32_t *)&io->buf[0] = IOBUF_MAGIC;
        *(uint32_t *)&io->buf[4] = htonl (io->size - 8);
        if (msg_encode (msg, &io->buf[8], io->size - 8) < 0)
            goto done;
        io->done = 0;
    }
    do {
        rc = write (fd, io->buf + io->done, io->size - io->done);
        if (rc < 0)
            goto done;
        io->done += rc;
    } while (io->done < io->size);
    rc = 0;
done:
    if (iobuf) {
        if (rc == 0 || (errno != EAGAIN && errno != EWOULDBLOCK))
            iobuf_clean (iobuf);
    } else {
        if (rc < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            errno = EPROTO;
        iobuf_clean (&local);
    }
    return rc;
}

static void proto_get_u32 (uint8_t *data, int index, uint32_t *val)
{
    uint32_t x;
    int offset = PROTO_OFF_U32_ARRAY + index * 4;
    memcpy (&x, &data[offset], sizeof (x));
    *val = ntohl (x);
}

static int zmsg_to_msgproto (struct msgproto *proto, zmsg_t *zmsg)
{
    uint8_t *proto_data;
    size_t proto_size;
    zframe_t *zf;

    if (!(zf = zmsg_last (zmsg))) {
        errno = EPROTO;
        return -1;
    }
    proto_data = zframe_data (zf);
    proto_size = zframe_size (zf);
    if (proto_size < PROTO_SIZE
        || proto_data[PROTO_OFF_MAGIC] != PROTO_MAGIC
        || proto_data[PROTO_OFF_VERSION] != PROTO_VERSION) {
        errno = EPROTO;
        return -1;
    }
    proto->type = proto_data[PROTO_OFF_TYPE];
    if (proto->type != FLUX_MSGTYPE_REQUEST
        && proto->type != FLUX_MSGTYPE_RESPONSE
        && proto->type != FLUX_MSGTYPE_EVENT
        && proto->type != FLUX_MSGTYPE_KEEPALIVE) {
        errno = EPROTO;
        return -1;
    }
    proto->flags = proto_data[PROTO_OFF_FLAGS];

    zf = zmsg_first (zmsg);
    if ((proto->flags & FLUX_MSGFLAG_ROUTE)) {
        if (!zf) {
            errno = EPROTO;
            return -1;
        }
        while (zf && zframe_size (zf) > 0) {
            struct msgproto_route *r;
            char *id = (char *)zframe_data (zf);
            assert (id);
            if (!(r = msgproto_route_create (id, zframe_size (zf))))
                return -1;
            list_add_tail (&proto->routes, &r->msgproto_route_node);
            proto->routes_len++;
            zf = zmsg_next (zmsg);
        }
        if (zf)
            zf = zmsg_next (zmsg);
    }
    if ((proto->flags & FLUX_MSGFLAG_TOPIC)) {
        if (!zf) {
            errno = EPROTO;
            return -1;
        }
        if (!(proto->topic = zframe_strdup (zf))) {
            errno = ENOMEM;
            return -1;
        }
        if (zf)
            zf = zmsg_next (zmsg);
    }
    if ((proto->flags & FLUX_MSGFLAG_PAYLOAD)) {
        if (!zf) {
            errno = EPROTO;
            return -1;
        }
        proto->payload_size = zframe_size (zf);
        if (!(proto->payload = malloc (proto->payload_size))) {
            errno = ENOMEM;
            return -1;
        }
        memcpy (proto->payload, zframe_data (zf), proto->payload_size);
        if (zf)
            zf = zmsg_next (zmsg);
    }
    /* proto frame required */
    if (!zf) {
        errno = EPROTO;
        return -1;
    }
    proto_get_u32 (proto_data, PROTO_IND_USERID, &proto->userid);
    proto_get_u32 (proto_data, PROTO_IND_ROLEMASK, &proto->rolemask);
    proto_get_u32 (proto_data, PROTO_IND_AUX1, &proto->aux1);
    proto_get_u32 (proto_data, PROTO_IND_AUX2, &proto->aux2);
    return 0;
}

flux_msg_t *msg_decode (const void *buf, size_t size)
{
    flux_msg_t *msg = NULL;
    struct msgproto *proto;
    uint8_t const *p = buf;
    zmsg_t *zmsg = NULL;
    zframe_t *zf;

    if (!(zmsg = zmsg_new ()))
        goto nomem;
    while (p - (uint8_t *)buf < size) {
        size_t n = *p++;
        if (n == 0xff) {
            if (size - (p - (uint8_t *)buf) < 4) {
                errno = EINVAL;
                goto error;
            }
            n = ntohl (*(uint32_t *)p);
            p += 4;
        }
        if (size - (p - (uint8_t *)buf) < n) {
            errno = EINVAL;
            goto error;
        }
        if (!(zf = zframe_new (p, n)))
            goto nomem;
        if (zmsg_append (zmsg, &zf) < 0)
            goto nomem;
        p += n;
    }
    if (!(msg = flux_msg_create_common ()))
        goto error;
    if (!(proto = flux_msg_get_proto (msg)))
        goto error;
    if (zmsg_to_msgproto (proto, zmsg) < 0)
        goto error;
    zmsg_destroy (&zmsg);
    return msg;
nomem:
    errno = ENOMEM;
error:
    zmsg_destroy (&zmsg);
    flux_msg_destroy (msg);
    return NULL;
}

flux_msg_t *recvfd (int fd, struct iobuf *iobuf)
{
    struct iobuf local;
    struct iobuf *io = iobuf ? iobuf : &local;
    flux_msg_t *msg = NULL;
    int rc = -1;

    if (fd < 0) {
        errno = EINVAL;
        return NULL;
    }
    if (!iobuf)
        iobuf_init (&local);
    if (!io->buf) {
        io->buf = io->buf_fixed;
        io->size = sizeof (io->buf_fixed);
    }
    do {
        if (io->done < 8) {
            rc = read (fd, io->buf + io->done, 8 - io->done);
            if (rc < 0)
                goto done;
            if (rc == 0) {
                errno = ECONNRESET;
                goto done;
            }
            io->done += rc;
            if (io->done == 8) {
                if (*(uint32_t *)&io->buf[0] != IOBUF_MAGIC) {
                    errno = EPROTO;
                    goto done;
                }
                io->size = ntohl (*(uint32_t *)&io->buf[4]) + 8;
                if (io->size > sizeof (io->buf_fixed)) {
                    if (!(io->buf = malloc (io->size)))
                        goto done;
                    memcpy (io->buf, io->buf_fixed, 8);
                }
            }
        }
        if (io->done >= 8 && io->done < io->size) {
            rc = read (fd, io->buf + io->done, io->size - io->done);
            if (rc < 0)
                goto done;
            if (rc == 0) {
                errno = ECONNRESET;
                goto done;
            }
            io->done += rc;
        }
    } while (io->done < io->size);
    if (!(msg = msg_decode (io->buf + 8, io->size - 8)))
        goto done;
done:
    if (iobuf) {
        if (msg != NULL || (errno != EAGAIN && errno != EWOULDBLOCK))
            iobuf_clean (iobuf);
    } else {
        if (rc < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            errno = EPROTO;
        iobuf_clean (&local);
    }
    return msg;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

