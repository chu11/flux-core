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

#include "src/common/libmsgproto/msgproto.h"
#include "message.h"

#ifdef __cplusplus
extern "C" {
#endif

struct msgproto *flux_msg_get_proto (flux_msg_t *msg);

flux_msg_t *flux_msg_create_common (void);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_MESSAGE_PRIVATE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

