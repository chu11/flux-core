/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include "src/common/libtap/tap.h"
#include "src/common/libsdprocess/sdprocess.h"

int main (int argc, char *argv[])
{
    flux_reactor_t *r;

    plan (NO_PLAN);

    // Create shared reactor for all tests
    ok ((r = flux_reactor_create (FLUX_REACTOR_SIGCHLD)) != NULL,
        "flux_reactor_create");

    /* diag ("basic"); */
    /* test_basic (r); */
    /* diag ("basic_fail"); */
    /* test_basic_fail (r); */
    /* diag ("basic_errors"); */
    /* test_basic_errors (r); */

    flux_reactor_destroy (r);
    done_testing ();
    return 0;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
