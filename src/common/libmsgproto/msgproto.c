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
# include "config.h"
#endif

#include <stdarg.h>
#include <string.h>
#include <stdbool.h>
#include <arpa/inet.h>
#include <errno.h>

#include <jansson.h>

#include "src/common/libflux/message.h"
#include "src/common/libutil/macros.h"

#include "msgproto.h"

/* XXX asssumes caller memset/callocs */
void msgproto_init (struct msgproto *proto)
{
    if (proto) {
        list_head_init (&proto->routes);
        proto->userid = FLUX_USERID_UNKNOWN;
        proto->rolemask = FLUX_ROLE_NONE;
    }
}

void msgproto_cleanup (struct msgproto *proto)
{
    if (proto) {
        struct msgproto_route *r;
        while ((r = list_pop (&proto->routes,
                              struct msgproto_route,
                              msgproto_route_node)))
            msgproto_route_destroy (r);
        free (proto->topic);
        free (proto->payload);
    }
}

int msgproto_setup_type (struct msgproto *proto, int type)
{
    if (!proto) {
        errno = EINVAL;
        return -1;
    }
    switch (type) {
        case FLUX_MSGTYPE_REQUEST:
            proto->nodeid = FLUX_NODEID_ANY;
            proto->matchtag = FLUX_MATCHTAG_NONE;
            break;
        case FLUX_MSGTYPE_RESPONSE:
            /* N.B. don't clobber matchtag from possible request */
            proto->errnum = 0;
            break;
        case FLUX_MSGTYPE_EVENT:
            proto->sequence = 0;
            proto->aux2 = 0;
            break;
        case FLUX_MSGTYPE_KEEPALIVE:
            proto->errnum = 0;
            proto->status = 0;
            break;
        default:
            errno = EINVAL;
            return -1;
    }
    proto->type = type;
    return 0;
}

static void proto_set_u32 (uint8_t *frame, int index, uint32_t val)
{
    uint32_t x = htonl (val);
    int offset = PROTO_OFF_U32_ARRAY + index * 4;
    memcpy (&frame[offset], &x, sizeof (x));
}

int msgproto_get_protoframe (const struct msgproto *proto,
                             uint8_t *frame,
                             unsigned int frame_len)
{
    if (!proto || !frame || frame_len < PROTO_SIZE) {
        errno = EINVAL;
        return -1;
    }
    memset (frame, '\0', frame_len);
    frame[PROTO_OFF_MAGIC] = PROTO_MAGIC;
    frame[PROTO_OFF_VERSION] = PROTO_VERSION;
    frame[PROTO_OFF_TYPE] = proto->type;
    frame[PROTO_OFF_FLAGS] = proto->flags;
    proto_set_u32 (frame, PROTO_IND_USERID, proto->userid);
    proto_set_u32 (frame, PROTO_IND_ROLEMASK, proto->rolemask);
    proto_set_u32 (frame, PROTO_IND_AUX1, proto->aux1);
    proto_set_u32 (frame, PROTO_IND_AUX2, proto->aux2);
    return 0;
}

void msgproto_route_destroy (void *data)
{
    if (data) {
        struct msgproto_route *r = data;
        free (r);
    }
}

struct msgproto_route *msgproto_route_create (const char *id,
                                              unsigned int id_len)
{
    struct msgproto_route *r;
    if (!(r = calloc (1, sizeof (*r) + id_len + 1)))
        return NULL;
    if (id && id_len) {
        memcpy (r->id, id, id_len);
        list_node_init (&(r->msgproto_route_node));
    }
    return r;
}

int msgproto_routes_push (struct msgproto *proto,
                          const char *id,
                          unsigned int id_len)
{
    struct msgproto_route *r = NULL;
    if (!proto) {
        errno = EINVAL;
        return -1;
    }
    if (!(r = msgproto_route_create (id, id_len)))
        return -1;
    list_add (&proto->routes, &r->msgproto_route_node);
    proto->routes_len++;
    return 0;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
