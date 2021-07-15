/************************************************************  \
 * Copyright 2019 Lawrence Livermore National Security, LLC
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
#include <flux/core.h>
#include <czmq.h>

#include "src/common/libtap/tap.h"
#include "src/common/libtestutil/util.h"
#include "src/common/libzsock/zsock_msg.h"

void check_sendzsock (void)
{
    zsock_t *zsock[2] = { NULL, NULL };
    flux_msg_t *msg, *msg2;
    const char *topic;
    int type;
    const char *uri = "inproc://test";

    /* zsys boiler plate:
     * appears to be needed to avoid atexit assertions when lives_ok()
     * macro (which calls fork()) is used.
     */
    zsys_init ();
    zsys_set_logstream (stderr);
    zsys_set_logident ("test_message.t");
    zsys_handler_set (NULL);
    zsys_set_linger (5); // msec

    ok ((zsock[0] = zsock_new_pair (NULL)) != NULL
                    && zsock_bind (zsock[0], "%s", uri) == 0
                    && (zsock[1] = zsock_new_pair (uri)) != NULL,
        "got inproc socket pair");

    ok ((msg = flux_msg_create (FLUX_MSGTYPE_REQUEST)) != NULL
            && flux_msg_set_topic (msg, "foo.bar") == 0,
        "created test message");

    /* corner case tests */
    ok (zsock_msg_sendzsock (NULL, msg) < 0 && errno == EINVAL,
        "zsock_msg_sendzsock returns < 0 and EINVAL on dest = NULL");
    ok (zsock_msg_sendzsock_ex (NULL, msg, true) < 0 && errno == EINVAL,
        "zsock_msg_sendzsock_ex returns < 0 and EINVAL on dest = NULL");
    ok (zsock_msg_recvzsock (NULL) == NULL && errno == EINVAL,
        "zsock_msg_recvzsock returns NULL and EINVAL on dest = NULL");

    ok (zsock_msg_sendzsock (zsock[1], msg) == 0,
        "zsock_msg_sendzsock works");
    ok ((msg2 = zsock_msg_recvzsock (zsock[0])) != NULL,
        "zsock_msg_recvzsock works");
    ok (flux_msg_get_type (msg2, &type) == 0 && type == FLUX_MSGTYPE_REQUEST
            && flux_msg_get_topic (msg2, &topic) == 0
            && !strcmp (topic, "foo.bar")
            && flux_msg_has_payload (msg2) == false,
        "decoded message looks like what was sent");
    flux_msg_destroy (msg2);

    /* Send it again.
     */
    ok (zsock_msg_sendzsock (zsock[1], msg) == 0,
        "try2: zsock_msg_sendzsock works");
    ok ((msg2 = zsock_msg_recvzsock (zsock[0])) != NULL,
        "try2: zsock_msg_recvzsock works");
    ok (flux_msg_get_type (msg2, &type) == 0 && type == FLUX_MSGTYPE_REQUEST
            && flux_msg_get_topic (msg2, &topic) == 0
            && !strcmp (topic, "foo.bar")
            && flux_msg_has_payload (msg2) == false,
        "try2: decoded message looks like what was sent");
    flux_msg_destroy (msg2);
    flux_msg_destroy (msg);

    zsock_destroy (&zsock[0]);
    zsock_destroy (&zsock[1]);

    /* zsys boiler plate - see note above
     */
    zsys_shutdown();
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    check_sendzsock ();

    done_testing ();

    return 0;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
