/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* A flux message contains route, topic, payload protocol information.
 * When sent it is formed into the following zeromq frames.
 *
 * [route]
 * [route]
 * [route]
 * ...
 * [route]
 * [route delimiter - empty frame]
 * topic frame
 * [payload frame]
 * PROTO frame
 *
 * See also: RFC 3
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <arpa/inet.h>
#include <assert.h>
#include <fnmatch.h>
#include <inttypes.h>
#include <czmq.h>
#include <jansson.h>

/* czmq and ccan both define streq */
#ifdef streq
#undef streq
#endif
#include "src/common/libmsgproto/msgproto.h"
#include "src/common/libutil/aux.h"
#include "src/common/libutil/errno_safe.h"

#include "message.h"

struct flux_msg {
    struct msgproto proto;
    json_t *json;
    char *lasterr;
    struct aux_item *aux;
    int refcount;
};

flux_msg_t *flux_msg_create_common (void)
{
    flux_msg_t *msg;

    if (!(msg = calloc (1, sizeof (*msg))))
        return NULL;
    msgproto_init (&msg->proto);
    msg->refcount = 1;
    return msg;
}

flux_msg_t *flux_msg_create (int type)
{
    flux_msg_t *msg;

    if (!(msg = flux_msg_create_common ()))
        return NULL;
    if (msgproto_setup_type (&msg->proto, type) < 0)
        goto error;
    return msg;
error:
    flux_msg_destroy (msg);
    return NULL;
}

void flux_msg_destroy (flux_msg_t *msg)
{
    if (msg && --msg->refcount == 0) {
        int saved_errno = errno;
        msgproto_cleanup (&msg->proto);
        json_decref (msg->json);
        aux_destroy (&msg->aux);
        free (msg->lasterr);
        free (msg);
        errno = saved_errno;
    }
}

/* N.B. const attribute of msg argument is defeated internally for
 * incref/decref to allow msg destruction to be juggled to whoever last
 * decrements the reference count.  Other than its eventual destruction,
 * the message content shall not change.
 */
void flux_msg_decref (const flux_msg_t *const_msg)
{
    flux_msg_t *msg = (flux_msg_t *)const_msg;
    flux_msg_destroy (msg);
}

const flux_msg_t *flux_msg_incref (const flux_msg_t *const_msg)
{
    flux_msg_t *msg = (flux_msg_t *)const_msg;

    if (!msg) {
        errno = EINVAL;
        return NULL;
    }
    msg->refcount++;
    return msg;
}

/* N.B. const attribute of msg argument is defeated internally to
 * allow msg to be "annotated" for convenience.
 * The message content is otherwise unchanged.
 */
int flux_msg_aux_set (const flux_msg_t *const_msg, const char *name,
                      void *aux, flux_free_f destroy)
{
    flux_msg_t *msg = (flux_msg_t *)const_msg;
    if (!msg) {
        errno = EINVAL;
        return -1;
    }
    return aux_set (&msg->aux, name, aux, destroy);
}

void *flux_msg_aux_get (const flux_msg_t *msg, const char *name)
{
    if (!msg) {
        errno = EINVAL;
        return NULL;
    }
    return aux_get (msg->aux, name);
}

int flux_msg_set_type (flux_msg_t *msg, int type)
{
    if (!msg) {
        errno = EINVAL;
        return -1;
    }
    if (msgproto_setup_type (&msg->proto, type) < 0)
        return -1;
    return 0;
}

int flux_msg_get_type (const flux_msg_t *msg, int *type)
{
    if (!msg) {
        errno = EINVAL;
        return -1;
    }
    (*type) = msg->proto.type;
    return 0;
}

int flux_msg_set_flags (flux_msg_t *msg, uint8_t fl)
{
    const uint8_t valid_flags = FLUX_MSGFLAG_TOPIC | FLUX_MSGFLAG_PAYLOAD
                              | FLUX_MSGFLAG_ROUTE | FLUX_MSGFLAG_UPSTREAM
                              | FLUX_MSGFLAG_PRIVATE | FLUX_MSGFLAG_STREAMING
                              | FLUX_MSGFLAG_NORESPONSE;

    if (!msg || fl & ~valid_flags || ((fl & FLUX_MSGFLAG_STREAMING)
                                   && (fl & FLUX_MSGFLAG_NORESPONSE)) != 0) {
        errno = EINVAL;
        return -1;
    }
    msg->proto.flags = fl;
    return 0;
}

int flux_msg_get_flags (const flux_msg_t *msg, uint8_t *fl)
{
    if (!msg || !fl) {
        errno = EINVAL;
        return -1;
    }
    (*fl) = msg->proto.flags;
    return 0;
}

int flux_msg_set_private (flux_msg_t *msg)
{
    uint8_t flags = 0;
    if (flux_msg_get_flags (msg, &flags) < 0)
        return -1;
    if (flux_msg_set_flags (msg, flags | FLUX_MSGFLAG_PRIVATE) < 0)
        return -1;
    return 0;
}

bool flux_msg_is_private (const flux_msg_t *msg)
{
    uint8_t flags = 0;
    if (flux_msg_get_flags (msg, &flags) < 0)
        return true;
    return (flags & FLUX_MSGFLAG_PRIVATE) ? true : false;
}

int flux_msg_set_streaming (flux_msg_t *msg)
{
    uint8_t flags = 0;
    if (flux_msg_get_flags (msg, &flags) < 0)
        return -1;
    flags &= ~FLUX_MSGFLAG_NORESPONSE;
    if (flux_msg_set_flags (msg, flags | FLUX_MSGFLAG_STREAMING) < 0)
        return -1;
    return 0;
}

bool flux_msg_is_streaming (const flux_msg_t *msg)
{
    uint8_t flags = 0;
    if (flux_msg_get_flags (msg, &flags) < 0)
        return true;
    return (flags & FLUX_MSGFLAG_STREAMING) ? true : false;
}

int flux_msg_set_noresponse (flux_msg_t *msg)
{
    uint8_t flags = 0;
    if (flux_msg_get_flags (msg, &flags) < 0)
        return -1;
    flags &= ~FLUX_MSGFLAG_STREAMING;
    if (flux_msg_set_flags (msg, flags | FLUX_MSGFLAG_NORESPONSE) < 0)
        return -1;
    return 0;
}

bool flux_msg_is_noresponse (const flux_msg_t *msg)
{
    uint8_t flags = 0;
    if (flux_msg_get_flags (msg, &flags) < 0)
        return true;
    return (flags & FLUX_MSGFLAG_NORESPONSE) ? true : false;
}

int flux_msg_set_userid (flux_msg_t *msg, uint32_t userid)
{
    if (!msg) {
        errno = EINVAL;
        return -1;
    }
    msg->proto.userid = userid;
    return 0;
}

int flux_msg_get_userid (const flux_msg_t *msg, uint32_t *userid)
{
    if (!msg || !userid) {
        errno = EINVAL;
        return -1;
    }
    (*userid) = msg->proto.userid;
    return 0;
}

int flux_msg_set_rolemask (flux_msg_t *msg, uint32_t rolemask)
{
    if (!msg) {
        errno = EINVAL;
        return -1;
    }
    msg->proto.rolemask = rolemask;
    return 0;
}

int flux_msg_get_rolemask (const flux_msg_t *msg, uint32_t *rolemask)
{
    if (!msg || !rolemask) {
        errno = EINVAL;
        return -1;
    }
    (*rolemask) = msg->proto.rolemask;
    return 0;
}

int flux_msg_get_cred (const flux_msg_t *msg, struct flux_msg_cred *cred)
{
    if (!msg || !cred) {
        errno = EINVAL;
        return -1;
    }
    if (flux_msg_get_rolemask (msg, &cred->rolemask) < 0)
        return -1;
    if (flux_msg_get_userid (msg, &cred->userid) < 0)
        return -1;
    return 0;
}

int flux_msg_set_cred (flux_msg_t *msg, struct flux_msg_cred cred)
{
    if (!msg) {
        errno = EINVAL;
        return -1;
    }
    if (flux_msg_set_rolemask (msg, cred.rolemask) < 0)
        return -1;
    if (flux_msg_set_userid (msg, cred.userid) < 0)
        return -1;
    return 0;
}

int flux_msg_cred_authorize (struct flux_msg_cred cred, uint32_t userid)
{
    if ((cred.rolemask & FLUX_ROLE_OWNER))
        return 0;
    if ((cred.rolemask & FLUX_ROLE_USER) && cred.userid != FLUX_USERID_UNKNOWN
                                         && cred.userid == userid)
        return 0;
    errno = EPERM;
    return -1;
}

int flux_msg_authorize (const flux_msg_t *msg, uint32_t userid)
{
    struct flux_msg_cred cred;

    if (flux_msg_get_cred (msg, &cred) < 0)
        return -1;
    if (flux_msg_cred_authorize (cred, userid) < 0)
        return -1;
    return 0;
}

int flux_msg_set_nodeid (flux_msg_t *msg, uint32_t nodeid)
{
    if (!msg)
        goto error;
    if (nodeid == FLUX_NODEID_UPSTREAM) /* should have been resolved earlier */
        goto error;
    if (msg->proto.type != FLUX_MSGTYPE_REQUEST)
        goto error;
    msg->proto.nodeid = nodeid;
    return 0;
error:
    errno = EINVAL;
    return -1;
}

int flux_msg_get_nodeid (const flux_msg_t *msg, uint32_t *nodeidp)
{
    if (!msg || !nodeidp) {
        errno = EINVAL;
        return -1;
    }
    if (msg->proto.type != FLUX_MSGTYPE_REQUEST) {
        errno = EPROTO;
        return -1;
    }
    *nodeidp = msg->proto.nodeid;
    return 0;
}

int flux_msg_set_errnum (flux_msg_t *msg, int e)
{
    if (!msg
        || (msg->proto.type != FLUX_MSGTYPE_RESPONSE
            && msg->proto.type != FLUX_MSGTYPE_KEEPALIVE)) {
        errno = EINVAL;
        return -1;
    }
    msg->proto.errnum = e;
    return 0;
}

int flux_msg_get_errnum (const flux_msg_t *msg, int *e)
{
    if (!msg || !e) {
        errno = EINVAL;
        return -1;
    }
    if (msg->proto.type != FLUX_MSGTYPE_RESPONSE
        && msg->proto.type != FLUX_MSGTYPE_KEEPALIVE) {
        errno = EPROTO;
        return -1;
    }
    *e = msg->proto.errnum;
    return 0;
}

int flux_msg_set_seq (flux_msg_t *msg, uint32_t seq)
{
    if (!msg || msg->proto.type != FLUX_MSGTYPE_EVENT) {
        errno = EINVAL;
        return -1;
    }
    msg->proto.sequence = seq;
    return 0;
}

int flux_msg_get_seq (const flux_msg_t *msg, uint32_t *seq)
{
    if (!msg || !seq) {
        errno = EINVAL;
        return -1;
    }
    if (msg->proto.type != FLUX_MSGTYPE_EVENT) {
        errno = EPROTO;
        return -1;
    }
    (*seq) = msg->proto.sequence;
    return 0;
}

int flux_msg_set_matchtag (flux_msg_t *msg, uint32_t t)
{
    if (!msg
        || (msg->proto.type != FLUX_MSGTYPE_REQUEST
            && msg->proto.type != FLUX_MSGTYPE_RESPONSE)) {
        errno = EINVAL;
        return -1;
    }
    msg->proto.matchtag = t;
    return 0;
}

int flux_msg_get_matchtag (const flux_msg_t *msg, uint32_t *t)
{
    if (!msg || !t) {
        errno = EINVAL;
        return -1;
    }
    if (msg->proto.type != FLUX_MSGTYPE_REQUEST
        && msg->proto.type != FLUX_MSGTYPE_RESPONSE) {
        errno = EPROTO;
        return -1;
    }
    (*t) = msg->proto.matchtag;
    return 0;
}

int flux_msg_set_status (flux_msg_t *msg, int s)
{
    if (!msg || msg->proto.type != FLUX_MSGTYPE_KEEPALIVE) {
        errno = EINVAL;
        return -1;
    }
    msg->proto.status = s;
    return 0;
}

int flux_msg_get_status (const flux_msg_t *msg, int *s)
{
    if (!msg || !s) {
        errno = EINVAL;
        return -1;
    }
    if (msg->proto.type != FLUX_MSGTYPE_KEEPALIVE) {
        errno = EPROTO;
        return -1;
    }
    (*s) = msg->proto.status;
    return 0;
}

bool flux_msg_cmp_matchtag (const flux_msg_t *msg, uint32_t matchtag)
{
    uint32_t tag;

    if (flux_msg_get_route_count (msg) > 0)
        return false; /* don't match in foreign matchtag domain */
    if (flux_msg_get_matchtag (msg, &tag) < 0)
        return false;
    if (tag != matchtag)
        return false;
    return true;
}

static bool isa_matchany (const char *s)
{
    if (!s || strlen(s) == 0)
        return true;
    if (!strcmp (s, "*"))
        return true;
    return false;
}

static bool isa_glob (const char *s)
{
    if (strchr (s, '*') || strchr (s, '?') || strchr (s, '['))
        return true;
    return false;
}

bool flux_msg_cmp (const flux_msg_t *msg, struct flux_match match)
{
    if (match.typemask != 0) {
        int type = 0;
        if (flux_msg_get_type (msg, &type) < 0)
            return false;
        if ((type & match.typemask) == 0)
            return false;
    }
    if (match.matchtag != FLUX_MATCHTAG_NONE) {
        if (!flux_msg_cmp_matchtag (msg, match.matchtag))
            return false;
    }
    if (!isa_matchany (match.topic_glob)) {
        const char *topic = NULL;
        if (flux_msg_get_topic (msg, &topic) < 0)
            return false;
        if (isa_glob (match.topic_glob)) {
            if (fnmatch (match.topic_glob, topic, 0) != 0)
                return false;
        } else {
            if (strcmp (match.topic_glob, topic) != 0)
                return false;
        }
    }
    return true;
}

int flux_msg_enable_route (flux_msg_t *msg)
{
    uint8_t flags = 0;

    if (flux_msg_get_flags (msg, &flags) < 0)
        return -1;
    if ((flags & FLUX_MSGFLAG_ROUTE))
        return 0;
    flags |= FLUX_MSGFLAG_ROUTE;
    return flux_msg_set_flags (msg, flags);
}

int flux_msg_clear_route (flux_msg_t *msg)
{
    uint8_t flags = 0;

    if (flux_msg_get_flags (msg, &flags) < 0)
        return -1;
    if (!(flags & FLUX_MSGFLAG_ROUTE))
        return 0;
    flags &= ~(uint8_t)FLUX_MSGFLAG_ROUTE;
    return flux_msg_set_flags (msg, flags);
}

int flux_msg_push_route (flux_msg_t *msg, const char *id)
{
    uint8_t flags = 0;
    struct msgproto_route *r;
    if (!id) {
        errno = EINVAL;
        return -1;
    }
    if (flux_msg_get_flags (msg, &flags) < 0)
        return -1;
    if (!(flags & FLUX_MSGFLAG_ROUTE)) {
        errno = EPROTO;
        return -1;
    }
    if (!(r = msgproto_route_create (id, strlen (id))))
        return -1;
    list_add (&msg->proto.routes, &r->msgproto_route_node);
    msg->proto.routes_len++;
    return 0;
}

int flux_msg_pop_route (flux_msg_t *msg, char **id)
{
    uint8_t flags = 0;
    struct msgproto_route *r;

    /* do not check 'id' for NULL, a "pop" is acceptable w/o returning
     * data to the user.  Caller may wish to only "pop" and not look
     * at the data.
     */
    if (flux_msg_get_flags (msg, &flags) < 0)
        return -1;
    if (!(flags & FLUX_MSGFLAG_ROUTE)) {
        errno = EPROTO;
        return -1;
    }
    if (list_empty (&msg->proto.routes)) {
        if (id)
            (*id) = NULL;
        return 0;
    }
    r = list_pop (&msg->proto.routes,
                  struct msgproto_route,
                  msgproto_route_node);
    assert (r);
    if (id) {
        if (!((*id) = strdup (r->id)))
            return -1;
    }
    msgproto_route_destroy (r);
    msg->proto.routes_len--;
    return 0;
}

/* replaces flux_msg_nexthop */
int flux_msg_get_route_last (const flux_msg_t *msg, char **id)
{
    uint8_t flags = 0;
    struct msgproto_route *r;

    if (!id) {
        errno = EINVAL;
        return -1;
    }
    if (flux_msg_get_flags (msg, &flags) < 0)
        return -1;
    if (!(flags & FLUX_MSGFLAG_ROUTE)) {
        errno = EPROTO;
        return -1;
    }
    if ((r = list_top (&msg->proto.routes,
                       struct msgproto_route,
                       msgproto_route_node))) {
        if (!((*id) = strdup (r->id)))
            return -1;
    }
    else
        (*id) = NULL;
    return 0;
}

static int find_route_first (const flux_msg_t *msg, struct msgproto_route **r)
{
    uint8_t flags = 0;

    if (flux_msg_get_flags (msg, &flags) < 0)
        return -1;
    if (!(flags & FLUX_MSGFLAG_ROUTE)) {
        errno = EPROTO;
        return -1;
    }
    (*r) = list_tail (&msg->proto.routes, struct msgproto_route, msgproto_route_node);
    return 0;
}

/* replaces flux_msg_sender */
int flux_msg_get_route_first (const flux_msg_t *msg, char **id)
{
    struct msgproto_route *r = NULL;

    if (!id) {
        errno = EINVAL;
        return -1;
    }
    if (find_route_first (msg, &r) < 0)
        return -1;
    if (r) {
        if (!((*id) = strdup (r->id)))
            return -1;
    }
    else
        (*id) = NULL;
    return 0;
}

int flux_msg_get_route_count (const flux_msg_t *msg)
{
    uint8_t flags = 0;

    if (flux_msg_get_flags (msg, &flags) < 0)
        return -1;
    if (!(flags & FLUX_MSGFLAG_ROUTE)) {
        errno = EPROTO;
        return -1;
    }
    return msg->proto.routes_len;
}

/* Get sum of size in bytes of route frames
 */
static int flux_msg_get_route_size (const flux_msg_t *msg)
{
    uint8_t flags = 0;
    struct msgproto_route *r;
    int size = 0;

    if (flux_msg_get_flags (msg, &flags) < 0)
        return -1;
    if (!(flags & FLUX_MSGFLAG_ROUTE)) {
        errno = EPROTO;
        return -1;
    }
    list_for_each (&msg->proto.routes, r, msgproto_route_node)
        size += strlen (r->id);
    return size;
}

static char *flux_msg_get_route_nth (const flux_msg_t *msg, int n)
{
    uint8_t flags = 0;
    struct msgproto_route *r;
    int count = 0;

    if (flux_msg_get_flags (msg, &flags) < 0)
        return NULL;
    if (!(flags & FLUX_MSGFLAG_ROUTE)) {
        errno = EPROTO;
        return NULL;
    }
    list_for_each (&msg->proto.routes, r, msgproto_route_node) {
        if (count == n)
            return r->id;
        count++;
    }
    errno = ENOENT;
    return NULL;
}

char *flux_msg_get_route_string (const flux_msg_t *msg)
{
    int hops, len;
    int n;
    char *buf, *cp;

    if (!msg) {
        errno = EINVAL;
        return NULL;
    }
    if ((hops = flux_msg_get_route_count (msg)) < 0
                    || (len = flux_msg_get_route_size (msg)) < 0) {
        return NULL;
    }
    if (!(cp = buf = malloc (len + hops + 1)))
        return NULL;
    for (n = hops - 1; n >= 0; n--) {
        char *id;
        if (cp > buf)
            *cp++ = '!';
        if (!(id = flux_msg_get_route_nth (msg, n))) {
            ERRNO_SAFE_WRAP (free, buf);
            return NULL;
        }
        int cpylen = strlen (id);
        if (cpylen > 8) /* abbreviate long UUID */
            cpylen = 8;
        assert (cp - buf + cpylen < len + hops);
        memcpy (cp, id, cpylen);
        cp += cpylen;
    }
    *cp = '\0';
    return buf;
}

static bool payload_overlap (flux_msg_t *msg, const void *b)
{
    return ((char *)b >= (char *)msg->proto.payload
         && (char *)b <  (char *)msg->proto.payload + msg->proto.payload_size);
}

int flux_msg_set_payload (flux_msg_t *msg, const void *buf, int size)
{
    uint8_t flags = 0;

    if (!msg) {
        errno = EINVAL;
        return -1;
    }
    json_decref (msg->json);            /* invalidate cached json object */
    msg->json = NULL;
    if (flux_msg_get_flags (msg, &flags) < 0)
        return -1;
    if (!(flags & FLUX_MSGFLAG_PAYLOAD) && (buf == NULL || size == 0))
        return 0;
    /* Case #1: replace existing payload.
     */
    if ((flags & FLUX_MSGFLAG_PAYLOAD) && (buf != NULL && size > 0)) {
        assert (msg->proto.payload);
        if (msg->proto.payload != buf || msg->proto.payload_size != size) {
            if (payload_overlap (msg, buf)) {
                errno = EINVAL;
                return -1;
            }
        }
        if (size > msg->proto.payload_size) {
            void *ptr;
            if (!(ptr = realloc (msg->proto.payload, size))) {
                errno = ENOMEM;
                return -1;
            }
            msg->proto.payload = ptr;
            msg->proto.payload_size = size;
        }
        memcpy (msg->proto.payload, buf, size);
    /* Case #2: add payload.
     */
    } else if (!(flags & FLUX_MSGFLAG_PAYLOAD) && (buf != NULL && size > 0)) {
        assert (!msg->proto.payload);
        if (!(msg->proto.payload = malloc (size))) {
            errno = ENOMEM;
            return -1;
        }
        msg->proto.payload_size = size;
        memcpy (msg->proto.payload, buf, size);
        flags |= FLUX_MSGFLAG_PAYLOAD;
    /* Case #3: remove payload.
     */
    } else if ((flags & FLUX_MSGFLAG_PAYLOAD) && (buf == NULL || size == 0)) {
        assert (msg->proto.payload);
        free (msg->proto.payload);
        msg->proto.payload = NULL;
        msg->proto.payload_size = 0;
        flags &= ~(uint8_t)(FLUX_MSGFLAG_PAYLOAD);
    }
    if (flux_msg_set_flags (msg, flags) < 0)
        return -1;
    return 0;
}

static inline void msg_lasterr_reset (flux_msg_t *msg)
{
    if (msg) {
        free (msg->lasterr);
        msg->lasterr = NULL;
    }
}

static inline void msg_lasterr_set (flux_msg_t *msg,
                                    const char *fmt,
                                    ...)
{
    va_list ap;
    int saved_errno = errno;

    va_start (ap, fmt);
    if (vasprintf (&msg->lasterr, fmt, ap) < 0)
        msg->lasterr = NULL;
    va_end (ap);

    errno = saved_errno;
}

int flux_msg_vpack (flux_msg_t *msg, const char *fmt, va_list ap)
{
    char *json_str = NULL;
    json_t *json;
    json_error_t err;
    int saved_errno;

    msg_lasterr_reset (msg);

    if (!(json = json_vpack_ex (&err, 0, fmt, ap))) {
        msg_lasterr_set (msg, "%s", err.text);
        goto error_inval;
    }
    if (!json_is_object (json)) {
        msg_lasterr_set (msg, "payload is not a JSON object");
        goto error_inval;
    }
    if (!(json_str = json_dumps (json, JSON_COMPACT))) {
        msg_lasterr_set (msg, "json_dumps failed on pack result");
        goto error_inval;
    }
    if (flux_msg_set_string (msg, json_str) < 0) {
        msg_lasterr_set (msg, "flux_msg_set_string: %s", strerror (errno));
        goto error;
    }
    free (json_str);
    json_decref (json);
    return 0;
error_inval:
    errno = EINVAL;
error:
    saved_errno = errno;
    free (json_str);
    json_decref (json);
    errno = saved_errno;
    return -1;
}

int flux_msg_pack (flux_msg_t *msg, const char *fmt, ...)
{
    va_list ap;
    int rc;

    va_start (ap, fmt);
    rc = flux_msg_vpack (msg, fmt, ap);
    va_end (ap);
    return rc;
}

int flux_msg_get_payload (const flux_msg_t *msg, const void **buf, int *size)
{
    uint8_t flags = 0;

    if (!buf && !size) {
        errno = EINVAL;
        return -1;
    }
    if (flux_msg_get_flags (msg, &flags) < 0)
        return -1;
    if (!(flags & FLUX_MSGFLAG_PAYLOAD)) {
        errno = EPROTO;
        return -1;
    }
    if (buf)
        *buf = msg->proto.payload;
    if (size)
        *size = msg->proto.payload_size;
    return 0;
}

bool flux_msg_has_payload (const flux_msg_t *msg)
{
    uint8_t flags = 0;
    if (flux_msg_get_flags (msg, &flags) < 0) {
        errno = 0;
        return false;
    }
    return ((flags & FLUX_MSGFLAG_PAYLOAD));
}

int flux_msg_set_string (flux_msg_t *msg, const char *s)
{
    if (s) {
        return flux_msg_set_payload (msg, s, strlen (s) + 1);
    }
    else
        return flux_msg_set_payload (msg, NULL, 0);
}

int flux_msg_get_string (const flux_msg_t *msg, const char **s)
{
    const char *buf;
    int size;
    int rc = -1;

    if (!s) {
        errno = EINVAL;
        goto done;
    }
    if (flux_msg_get_payload (msg, (const void **)&buf, &size) < 0) {
        errno = 0;
        *s = NULL;
    } else {
        if (!buf || size == 0 || buf[size - 1] != '\0') {
            errno = EPROTO;
            goto done;
        }
        *s = buf;
    }
    rc = 0;
done:
    return rc;
}

/* N.B. const attribute of msg argument is defeated internally to
 * allow msg to be "annotated" with parsed json object for convenience.
 * The message content is otherwise unchanged.
 */
int flux_msg_vunpack (const flux_msg_t *cmsg, const char *fmt, va_list ap)
{
    int rc = -1;
    const char *json_str;
    json_error_t err;
    flux_msg_t *msg = (flux_msg_t *)cmsg;

    msg_lasterr_reset (msg);

    if (!msg || !fmt || *fmt == '\0') {
        errno = EINVAL;
        goto done;
    }
    if (!msg->json) {
        if (flux_msg_get_string (msg, &json_str) < 0) {
            msg_lasterr_set (msg, "flux_msg_get_string: %s", strerror (errno));
            goto done;
        }
        if (!json_str) {
            msg_lasterr_set (msg, "message does not have a string payload");
            errno = EPROTO;
            goto done;
        }
        if (!(msg->json = json_loads (json_str, JSON_ALLOW_NUL, &err))) {
            msg_lasterr_set (msg, "%s", err.text);
            errno = EPROTO;
            goto done;
        }
        if (!json_is_object (msg->json)) {
            msg_lasterr_set (msg, "payload is not a JSON object");
            errno = EPROTO;
            goto done;
        }
    }
    if (json_vunpack_ex (msg->json, &err, 0, fmt, ap) < 0) {
        msg_lasterr_set (msg, "%s", err.text);
        errno = EPROTO;
        goto done;
    }
    rc = 0;
done:
    return rc;
}

int flux_msg_unpack (const flux_msg_t *msg, const char *fmt, ...)
{
    va_list ap;
    int rc;

    va_start (ap, fmt);
    rc = flux_msg_vunpack (msg, fmt, ap);
    va_end (ap);
    return rc;
}

const char *flux_msg_last_error (const flux_msg_t *msg)
{
    if (!msg)
        return "msg object is NULL";
    if (msg->lasterr == NULL)
        return "";
    return msg->lasterr;
}

int flux_msg_set_topic (flux_msg_t *msg, const char *topic)
{
    uint8_t flags = 0;

    if (flux_msg_get_flags (msg, &flags) < 0)
        return -1;
    if ((flags & FLUX_MSGFLAG_TOPIC) && topic) {        /* case 1: repl topic */
        free (msg->proto.topic);
        if (!(msg->proto.topic = strdup (topic)))
            return -1;
    } else if (!(flags & FLUX_MSGFLAG_TOPIC) && topic) {/* case 2: add topic */
        if (!(msg->proto.topic = strdup (topic)))
            return -1;
        flags |= FLUX_MSGFLAG_TOPIC;
        if (flux_msg_set_flags (msg, flags) < 0)
            return -1;
    } else if ((flags & FLUX_MSGFLAG_TOPIC) && !topic) { /* case 3: del topic */
        free (msg->proto.topic);
        msg->proto.topic = NULL;
        flags &= ~(uint8_t)FLUX_MSGFLAG_TOPIC;
        if (flux_msg_set_flags (msg, flags) < 0)
            return -1;
    }
    return 0;
}

int flux_msg_get_topic (const flux_msg_t *msg, const char **topic)
{
    uint8_t flags = 0;
    if (!topic) {
        errno = EINVAL;
        return -1;
    }
    if (flux_msg_get_flags (msg, &flags) < 0)
        return -1;
    if (!(flags & FLUX_MSGFLAG_TOPIC)) {
        errno = EPROTO;
        return -1;
    }
    *topic = msg->proto.topic;
    return 0;
}

flux_msg_t *flux_msg_copy (const flux_msg_t *msg, bool payload)
{
    flux_msg_t *cpy = NULL;

    if (!(cpy = flux_msg_create_common ()))
        return NULL;

    cpy->proto.type = msg->proto.type;
    cpy->proto.flags = msg->proto.flags;
    cpy->proto.userid = msg->proto.userid;
    cpy->proto.rolemask = msg->proto.rolemask;
    cpy->proto.aux1 = msg->proto.aux1;
    cpy->proto.aux2 = msg->proto.aux2;

    if (!list_empty (&msg->proto.routes)) {
        struct msgproto_route *r;
        list_for_each_rev (&msg->proto.routes, r, msgproto_route_node) {
            struct msgproto_route *rcpy;
            if (!(rcpy = msgproto_route_create (r->id, strlen (r->id))))
                goto error;
            list_add (&cpy->proto.routes, &rcpy->msgproto_route_node);
            cpy->proto.routes_len++;
        }
    }
    if (msg->proto.topic) {
        if (!(cpy->proto.topic = strdup (msg->proto.topic)))
            goto nomem;
    }
    if (msg->proto.payload) {
        if (payload) {
            cpy->proto.payload_size = msg->proto.payload_size;
            if (!(cpy->proto.payload = malloc (cpy->proto.payload_size)))
                goto nomem;
            memcpy (cpy->proto.payload,
                    msg->proto.payload,
                    msg->proto.payload_size);
        }
        else
            cpy->proto.flags &= ~FLUX_MSGFLAG_PAYLOAD;
    }
    return cpy;
nomem:
    errno = ENOMEM;
error:
    flux_msg_destroy (cpy);
    return NULL;
}

struct typemap {
    const char *name;
    const char *sname;
    int type;
};

static struct typemap typemap[] = {
    { "request", ">", FLUX_MSGTYPE_REQUEST },
    { "response", "<", FLUX_MSGTYPE_RESPONSE},
    { "event", "e", FLUX_MSGTYPE_EVENT},
    { "keepalive", "k", FLUX_MSGTYPE_KEEPALIVE},
};
static const int typemap_len = sizeof (typemap) / sizeof (typemap[0]);

const char *flux_msg_typestr (int type)
{
    int i;

    for (i = 0; i < typemap_len; i++)
        if ((type & typemap[i].type))
            return typemap[i].name;
    return "unknown";
}

static const char *msgtype_shortstr (int type)
{
    int i;

    for (i = 0; i < typemap_len; i++)
        if ((type & typemap[i].type))
            return typemap[i].sname;
    return "?";
}

void flux_msg_fprint (FILE *f, const flux_msg_t *msg)
{
    int hops;
    const char *prefix;
    uint8_t protoframe[PROTO_SIZE];
    int i;

    fprintf (f, "--------------------------------------\n");
    if (!msg) {
        fprintf (f, "NULL");
        return;
    }
    prefix = msgtype_shortstr (msg->proto.type);
    /* Route stack
     */
    hops = flux_msg_get_route_count (msg); /* -1 if no route stack */
    if (hops >= 0) {
        int len = flux_msg_get_route_size (msg);
        char *rte = flux_msg_get_route_string (msg);
        assert (rte != NULL);
        fprintf (f, "%s[%3.3d] |%s|\n", prefix, len, rte);
        free (rte);
    };
    /* Topic (keepalive has none)
     */
    if (msg->proto.topic)
        fprintf (f, "%s[%3.3zu] %s\n", prefix,
                                       strlen (msg->proto.topic),
                                       msg->proto.topic);
    /* Payload
     */
    if (flux_msg_has_payload (msg)) {
        const char *s;
        const void *buf;
        int size;
        if (flux_msg_get_string (msg, &s) == 0)
            fprintf (f, "%s[%3.3zu] %s\n", prefix, strlen (s), s);
        else if (flux_msg_get_payload (msg, &buf, &size) == 0)
            fprintf (f, "%s[%3.3d] ...\n", prefix, size);
        else
            fprintf (f, "malformed payload\n");
    }
    /* Proto block
     */
    if (msgproto_get_protoframe (&msg->proto, protoframe, PROTO_SIZE) < 0)
        return;
    fprintf (f, "%s[%03d] ", prefix, PROTO_SIZE);
    for (i = 0; i < PROTO_SIZE; i++)
        fprintf (f, "%02X", protoframe[i]);
    fprintf (f, "\n");
}

static zmsg_t *msg_to_zmsg (const flux_msg_t *msg)
{
    uint8_t protoframe[PROTO_SIZE];
    zmsg_t *zmsg = NULL;

    if (!(zmsg = zmsg_new ())) {
        errno = ENOMEM;
        return NULL;
    }
    if (msgproto_get_protoframe (&msg->proto, protoframe, PROTO_SIZE) < 0)
        goto error;
    if (zmsg_addmem (zmsg, protoframe, PROTO_SIZE) < 0) {
        errno = ENOMEM;
        goto error;
    }
    if (msg->proto.flags & FLUX_MSGFLAG_PAYLOAD) {
        if (zmsg_pushmem (zmsg,
                          msg->proto.payload,
                          msg->proto.payload_size) < 0) {
            errno = ENOMEM;
            goto error;
        }
    }
    if (msg->proto.flags & FLUX_MSGFLAG_TOPIC) {
        if (zmsg_pushmem (zmsg,
                          msg->proto.topic,
                          strlen (msg->proto.topic)) < 0) {
            errno = ENOMEM;
            goto error;
        }
    }
    if (msg->proto.flags & FLUX_MSGFLAG_ROUTE) {
        struct msgproto_route *r;
        if (zmsg_pushmem (zmsg, NULL, 0) < 0) {
            errno = ENOMEM;
            goto error;
        }
        list_for_each_rev (&msg->proto.routes, r, msgproto_route_node) {
            if (zmsg_pushstr (zmsg, r->id) < 0) {
                errno = ENOMEM;
                goto error;
            }
        }
    }
    return zmsg;
error:
    zmsg_destroy (&zmsg);
    return NULL;
}

int flux_msg_sendzsock_ex (void *sock, const flux_msg_t *msg, bool nonblock)
{
    void *handle;
    int flags = ZFRAME_REUSE | ZFRAME_MORE;
    zmsg_t *zmsg = NULL;
    zframe_t *zf;
    size_t count = 0;
    int rc = -1;

    if (!sock || !msg) {
        errno = EINVAL;
        return -1;
    }

    if (!(zmsg = msg_to_zmsg (msg)))
        return -1;

    if (nonblock)
        flags |= ZFRAME_DONTWAIT;

    handle = zsock_resolve (sock);
    zf = zmsg_first (zmsg);
    while (zf) {
        if (++count == zmsg_size (zmsg))
            flags &= ~ZFRAME_MORE;
        if (zframe_send (&zf, handle, flags) < 0)
            goto error;
        zf = zmsg_next (zmsg);
    }
    rc = 0;
error:
    zmsg_destroy (&zmsg);
    return rc;
}

int flux_msg_sendzsock (void *sock, const flux_msg_t *msg)
{
    return flux_msg_sendzsock_ex (sock, msg, false);
}

static void proto_get_u32 (uint8_t *data, int index, uint32_t *val)
{
    uint32_t x;
    int offset = PROTO_OFF_U32_ARRAY + index * 4;
    memcpy (&x, &data[offset], sizeof (x));
    *val = ntohl (x);
}

static int zmsg_to_msg (flux_msg_t *msg, zmsg_t *zmsg)
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
    msg->proto.type = proto_data[PROTO_OFF_TYPE];
    if (msg->proto.type != FLUX_MSGTYPE_REQUEST
        && msg->proto.type != FLUX_MSGTYPE_RESPONSE
        && msg->proto.type != FLUX_MSGTYPE_EVENT
        && msg->proto.type != FLUX_MSGTYPE_KEEPALIVE) {
        errno = EPROTO;
        return -1;
    }
    msg->proto.flags = proto_data[PROTO_OFF_FLAGS];

    zf = zmsg_first (zmsg);
    if ((msg->proto.flags & FLUX_MSGFLAG_ROUTE)) {
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
            list_add_tail (&msg->proto.routes, &r->msgproto_route_node);
            msg->proto.routes_len++;
            zf = zmsg_next (zmsg);
        }
        if (zf)
            zf = zmsg_next (zmsg);
    }
    if ((msg->proto.flags & FLUX_MSGFLAG_TOPIC)) {
        if (!zf) {
            errno = EPROTO;
            return -1;
        }
        if (!(msg->proto.topic = zframe_strdup (zf))) {
            errno = ENOMEM;
            return -1;
        }
        if (zf)
            zf = zmsg_next (zmsg);
    }
    if ((msg->proto.flags & FLUX_MSGFLAG_PAYLOAD)) {
        if (!zf) {
            errno = EPROTO;
            return -1;
        }
        msg->proto.payload_size = zframe_size (zf);
        if (!(msg->proto.payload = malloc (msg->proto.payload_size))) {
            errno = ENOMEM;
            return -1;
        }
        memcpy (msg->proto.payload, zframe_data (zf), msg->proto.payload_size);
        if (zf)
            zf = zmsg_next (zmsg);
    }
    /* proto frame required */
    if (!zf) {
        errno = EPROTO;
        return -1;
    }
    proto_get_u32 (proto_data, PROTO_IND_USERID, &msg->proto.userid);
    proto_get_u32 (proto_data, PROTO_IND_ROLEMASK, &msg->proto.rolemask);
    proto_get_u32 (proto_data, PROTO_IND_AUX1, &msg->proto.aux1);
    proto_get_u32 (proto_data, PROTO_IND_AUX2, &msg->proto.aux2);
    return 0;
}

flux_msg_t *flux_msg_recvzsock (void *sock)
{
    zmsg_t *zmsg;
    flux_msg_t *msg;

    if (!(zmsg = zmsg_recv (sock)))
        return NULL;
    if (!(msg = flux_msg_create_common ())) {
        zmsg_destroy (&zmsg);
        errno = ENOMEM;
        return NULL;
    }
    if (zmsg_to_msg (msg, zmsg) < 0) {
        int save_errno = errno;
        zmsg_destroy (&zmsg);
        errno = save_errno;
        return NULL;
    }
    zmsg_destroy (&zmsg);
    return msg;
}

int flux_msg_frames (const flux_msg_t *msg)
{
    int n = 1; /* 1 for proto frame */
    if (!msg) {
        errno = EINVAL;
        return -1;
    }
    if (msg->proto.flags & FLUX_MSGFLAG_PAYLOAD)
        n++;
    if (msg->proto.flags & FLUX_MSGFLAG_TOPIC)
        n++;
    if (msg->proto.flags & FLUX_MSGFLAG_ROUTE)
        /* +1 for routes delimeter frame */
        n += msg->proto.routes_len + 1;
    return n;
}

struct flux_match flux_match_init (int typemask,
                                     uint32_t matchtag,
                                     const char *topic_glob)
{
    struct flux_match m = {typemask, matchtag, topic_glob};
    return m;
}

void flux_match_free (struct flux_match m)
{
    ERRNO_SAFE_WRAP (free, (char *)m.topic_glob);
}

int flux_match_asprintf (struct flux_match *m, const char *topic_glob_fmt, ...)
{
    va_list args;
    va_start (args, topic_glob_fmt);
    char *topic = NULL;
    int res = vasprintf (&topic, topic_glob_fmt, args);
    va_end (args);
    m->topic_glob = topic;
    return res;
}

bool flux_msg_match_route_first (const flux_msg_t *msg1, const flux_msg_t *msg2)
{
    struct msgproto_route *r1 = NULL;
    struct msgproto_route *r2 = NULL;

    if (find_route_first (msg1, &r1) < 0)
        return false;
    if (find_route_first (msg2, &r2) < 0)
        return false;
    if (!r1 || !r2)
        return false;
    if (strcmp (r1->id, r2->id))
        return false;
    return true;
}

/* private, for internal use only */

struct msgproto *flux_msg_get_proto (flux_msg_t *msg)
{
    if (msg)
        return &(msg->proto);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

