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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <systemd/sd-bus.h>

#include "sdprocess.h"

struct flux_sdprocess {
    flux_t *h;
    flux_reactor_t *r;
    char *unitname;
    char **argv;
    char **envv;
    int stdin_fd;
    int stdout_fd;
    int stderr_fd;

    sd_bus *bus;
    char *service_name;         /* <unitname>.service */
};

static void strv_destroy (char **strv)
{
    if (strv) {
        char **ptr = strv;
        while (*ptr) {
            free (*ptr);
            ptr++;
        }
        free (strv);
    }
}

static void sdprocess_destroy (void *data)
{
    if (data) {
        struct flux_sdprocess *fsd = data;
        free (fsd->unitname);
        strv_destroy (fsd->argv);
        strv_destroy (fsd->envv);

        sd_bus_flush_close_unref (fsd->bus);
        free (fsd->service_name);
        free (fsd);
    }
}

static int strv_len (char **strv)
{
    int len = 0;
    if (strv) {
        char **ptr = strv;
        while (*ptr) {
            len++;
            ptr++;
        }
    }
    return len;
}

static int strv_copy (char **strv, char ***cpyp)
{
    char **cpy = NULL;
    int len = strv_len (strv);

    if (len) {
        int i;
        /* +1 for NULL terminating pointer */
        if (!(cpy = calloc (1, sizeof (char *) * (len + 1))))
            return -1;
        for (i = 0; i < len; i++) {
            if (!(cpy[i] = strdup (strv[i]))) {
                strv_destroy (cpy);
                return -1;
            }
        }
        *cpyp = cpy;
    }
    return 0;
}

static flux_sdprocess_t *sdprocess_create (flux_t *h,
                                           flux_reactor_t *r,
                                           const char *unitname,
                                           char **argv,
                                           char **envv,
                                           int stdin_fd,
                                           int stdout_fd,
                                           int stderr_fd)
{
    struct flux_sdprocess *fsd = NULL;

    if (!(fsd = calloc (1, sizeof (*fsd))))
        return NULL;
    if (!(fsd->unitname = strdup (unitname)))
        goto cleanup;
    if (strv_copy (argv, &fsd->argv) < 0)
        goto cleanup;
    if (strv_copy (envv, &fsd->envv) < 0)
        goto cleanup;
    fsd->h = h;
    fsd->r = r;
    fsd->stdin_fd = stdin_fd;
    fsd->stdout_fd = stdout_fd;
    fsd->stderr_fd = stderr_fd;

    if (sd_bus_default_user (&fsd->bus) < 0)
        goto cleanup;
    if (asprintf (&fsd->service_name, "%s.service", fsd->unitname) < 0)
        goto cleanup;

    return fsd;

 cleanup:
    sdprocess_destroy (fsd);
    return NULL;
}

static int transient_service_set_properties (flux_sdprocess_t *fsd, sd_bus_message *m)
{
    /* XXX: need real description? */

    if (sd_bus_message_append (m,
                               "(sv)",
                               "Description",
                               "s",
                               "flux sdprocess task") < 0) {
        flux_log_error (fsd->h, "sd_bus_message_append");
        return -1;
    }

    /* achu: no property assignments for the time being */

    if (sd_bus_message_append (m, "(sv)", "AddRef", "b", 1) < 0) {
        flux_log_error (fsd->h, "sd_bus_message_append");
        return -1;
    }

    if (sd_bus_message_append (m, "(sv)", "RemainAfterExit", "b", true) < 0) {
        flux_log_error (fsd->h, "sd_bus_message_append");
        return -1;
    }

    /* if (sd_bus_message_append(m, "(sv)", "User", "s", arg_exec_user) < 0) { */
    /*     flux_log_error (fsd->h, "sd_bus_message_append"); */
    /*     return -1; */
    /* } */

    /* if (sd_bus_message_append (m, "(sv)", "Group", "s", arg_exec_group) < 0) { */
    /*     flux_log_error (fsd->h, "sd_bus_message_append"); */
    /*     return -1; */
    /* } */

    /* if (sd_bus_message_append (m, "(sv)", "WorkingDirectory", "s", arg_working_directory) < 0) { */
    /*     flux_log_error (fsd->h, "sd_bus_message_append"); */
    /*     return -1; */
    /* } */

    /* Stdio */

    if (fsd->stdin_fd >= 0) {
        if (sd_bus_message_append (m,
                                   "(sv)",
                                   "StandardInputFileDescriptor", "h", fsd->stdin_fd) < 0) {
            flux_log_error (fsd->h, "sd_bus_message_append");
            return -1;
        }
    }

    if (fsd->stdout_fd >= 0) {
        if (sd_bus_message_append (m,
                                   "(sv)",
                                   "StandardOutputFileDescriptor", "h", fsd->stdout_fd) < 0) {
            flux_log_error (fsd->h, "sd_bus_message_append");
            return -1;
        }
    }

    if (fsd->stderr_fd >= 0) {
        if (sd_bus_message_append (m,
                                   "(sv)",
                                   "StandardErrorFileDescriptor", "h", fsd->stderr_fd) < 0) {
            flux_log_error (fsd->h, "sd_bus_message_append");
            return -1;
        }
    }

    /* Environment */

    if (fsd->envv) {
        if (sd_bus_message_open_container (m, 'r', "sv") < 0) {
            flux_log_error (fsd->h, "sd_bus_message_open_container");
            return -1;
        }

        if (sd_bus_message_append (m, "s", "Environment") < 0) {
            flux_log_error (fsd->h, "sd_bus_message_append");
            return -1;
        }

        if (sd_bus_message_open_container (m, 'v', "as") < 0) {
            flux_log_error (fsd->h, "sd_bus_message_open_container");
            return -1;
        }

        if (sd_bus_message_append_strv (m, fsd->envv) < 0) {
            flux_log_error (fsd->h, "sd_bus_message_append_strv");
            return -1;
        }

        if (sd_bus_message_close_container (m) < 0) {
            flux_log_error (fsd->h, "sd_bus_message_close_container");
            return -1;
        }

        if (sd_bus_message_close_container (m) < 0) {
            flux_log_error (fsd->h, "sd_bus_message_close_container");
            return -1;
        }
    }

    /* Cmdline */

    if (sd_bus_message_open_container (m, 'r', "sv") < 0) {
        flux_log_error (fsd->h, "sd_bus_message_open_container");
        return -1;
    }

    if (sd_bus_message_append (m, "s", "ExecStart") < 0) {
        flux_log_error (fsd->h, "sd_bus_message_append");
        return -1;
    }

    if (sd_bus_message_open_container (m, 'v', "a(sasb)") < 0) {
        flux_log_error (fsd->h, "sd_bus_message_open_container");
        return -1;
    }

    if (sd_bus_message_open_container (m, 'a', "(sasb)") < 0) {
        flux_log_error (fsd->h, "sd_bus_message_open_container");
        return -1;
    }

    if (sd_bus_message_open_container (m, 'r', "sasb") < 0) {
        flux_log_error (fsd->h, "sd_bus_message_open_container");
        return -1;
    }

    if (sd_bus_message_append (m, "s", fsd->argv[0]) < 0) {
        flux_log_error (fsd->h, "sd_bus_message_append");
        return -1;
    }

    if (sd_bus_message_append_strv (m, fsd->argv) < 0) {
        flux_log_error (fsd->h, "sd_bus_message_append_strv");
        return -1;
    }

    if (sd_bus_message_append (m, "b", false) < 0) {
        flux_log_error (fsd->h, "sd_bus_message_append");
        return -1;
    }

    if (sd_bus_message_close_container (m) < 0) {
        flux_log_error (fsd->h, "sd_bus_message_close_container");
        return -1;
    }

    if (sd_bus_message_close_container (m) < 0) {
        flux_log_error (fsd->h, "sd_bus_message_close_container");
        return -1;
    }

    if (sd_bus_message_close_container (m) < 0) {
        flux_log_error (fsd->h, "sd_bus_message_close_container");
        return -1;
    }

    if (sd_bus_message_close_container (m) < 0) {
        flux_log_error (fsd->h, "sd_bus_message_close_container");
        return -1;
    }

    return 0;
}

static int start_transient_service (flux_sdprocess_t *fsd)
{

    sd_bus_message *m = NULL, *reply = NULL;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    int ret = -1;

    if (sd_bus_message_new_method_call (fsd->bus,
                                        &m,
                                        "org.freedesktop.systemd1",
                                        "/org/freedesktop/systemd1",
                                        "org.freedesktop.systemd1.Manager",
                                        "StartTransientUnit") < 0) {
        flux_log_error (fsd->h, "sd_bus_message_new_method_call");
        goto cleanup;
    }

    /* Name and mode */
    if (sd_bus_message_append (m, "ss", fsd->service_name, "fail") < 0) {
        flux_log_error (fsd->h, "sd_bus_message_append");
        goto cleanup;
    }

    /* Properties */
    if (sd_bus_message_open_container (m, 'a', "(sv)") < 0) {
        flux_log_error (fsd->h, "sd_bus_message_open_container");
        goto cleanup;
    }

    if (transient_service_set_properties (fsd, m) < 0)
        goto cleanup;

    if (sd_bus_message_close_container (m) < 0) {
        flux_log_error (fsd->h, "sd_bus_message_close_container");
        goto cleanup;
    }

    /* Auxiliary units */
    if (sd_bus_message_append (m, "a(sa(sv))", 0) < 0) {
        flux_log_error (fsd->h, "sd_bus_message_append");
        goto cleanup;
    }

    if (sd_bus_call (fsd->bus, m, 0, &error, &reply) < 0) {
        flux_log (fsd->h, LOG_ERR, "sd_bus_call: %s",
                  error.message ? error.message : strerror (errno));
        goto cleanup;
    }

    ret = 0;
cleanup:
    sd_bus_message_unref (m);
    sd_bus_message_unref (reply);
    sd_bus_error_free (&error);
    return ret;
}

flux_sdprocess_t *flux_sdprocess_exec (flux_t *h,
                                       const char *unitname,
                                       char **argv,
                                       char **envv,
                                       int stdin_fd,
                                       int stdout_fd,
                                       int stderr_fd)
{
    struct flux_sdprocess *fsd = NULL;
    flux_reactor_t *r;

    if (!unitname
        || !argv) {
        errno = EINVAL;
        return NULL;
    }

    if (!(r = flux_get_reactor (h)))
        return NULL;

    if (!(fsd = sdprocess_create (h,
                                  r,
                                  unitname,
                                  argv,
                                  envv,
                                  stdin_fd,
                                  stdout_fd,
                                  stderr_fd)))
        return NULL;

    if (start_transient_service (fsd) < 0)
        goto cleanup;

    return fsd;

cleanup:
    sdprocess_destroy (fsd);
    return NULL;
}

flux_sdprocess_t *flux_sdprocess_local_exec (flux_reactor_t *r,
                                             const char *unitname,
                                             char **argv,
                                             char **envv,
                                             int stdin_fd,
                                             int stdout_fd,
                                             int stderr_fd)
{
    struct flux_sdprocess *fsd = NULL;

    if (!unitname
        || !argv) {
        errno = EINVAL;
        return NULL;
    }

    if (!(fsd = sdprocess_create (NULL,
                                  r,
                                  unitname,
                                  argv,
                                  envv,
                                  stdin_fd,
                                  stdout_fd,
                                  stderr_fd)))
        return NULL;

    if (start_transient_service (fsd) < 0)
        goto cleanup;

    return fsd;

cleanup:
    sdprocess_destroy (fsd);
    return NULL;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
