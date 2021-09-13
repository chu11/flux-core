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
    /* <unitname>.service */
    char *service_name;
    /* /org/freedesktop/systemd1/unit/<unitname>_2eservice */
    char *service_path;         
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

void flux_sdprocess_destroy (flux_sdprocess_t *sdp)
{
    if (sdp) {
        free (sdp->unitname);
        strv_destroy (sdp->argv);
        strv_destroy (sdp->envv);

        sd_bus_flush_close_unref (sdp->bus);
        free (sdp->service_name);
        free (sdp->service_path);
        free (sdp);
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
    struct flux_sdprocess *sdp = NULL;

    if (!(sdp = calloc (1, sizeof (*sdp))))
        return NULL;
    if (!(sdp->unitname = strdup (unitname)))
        goto cleanup;
    if (strv_copy (argv, &sdp->argv) < 0)
        goto cleanup;
    if (strv_copy (envv, &sdp->envv) < 0)
        goto cleanup;
    sdp->h = h;
    sdp->r = r;
    sdp->stdin_fd = stdin_fd;
    sdp->stdout_fd = stdout_fd;
    sdp->stderr_fd = stderr_fd;

    if (sd_bus_default_user (&sdp->bus) < 0)
        goto cleanup;
    if (asprintf (&sdp->service_name, "%s.service", sdp->unitname) < 0)
        goto cleanup;
    /* XXX - current assumption, unitname is single word, no need to escape dashes or other special chars.
     * note - . escapes to _2e
     */
    if (asprintf (&sdp->service_path,
                  "/org/freedesktop/systemd1/unit/%s_2eservice",
                  sdp->unitname) < 0)
        goto cleanup;

    return sdp;

 cleanup:
    flux_sdprocess_destroy (sdp);
    return NULL;
}

static int transient_service_set_properties (flux_sdprocess_t *sdp, sd_bus_message *m)
{
    /* XXX: need real description */

    if (sd_bus_message_append (m,
                               "(sv)",
                               "Description",
                               "s",
                               "flux sdprocess task") < 0) {
        flux_log_error (sdp->h, "sd_bus_message_append");
        return -1;
    }

    /* achu: no property assignments for the time being */

    if (sd_bus_message_append (m, "(sv)", "AddRef", "b", 1) < 0) {
        flux_log_error (sdp->h, "sd_bus_message_append");
        return -1;
    }

    if (sd_bus_message_append (m, "(sv)", "RemainAfterExit", "b", true) < 0) {
        flux_log_error (sdp->h, "sd_bus_message_append");
        return -1;
    }

    /* if (sd_bus_message_append(m, "(sv)", "User", "s", arg_exec_user) < 0) { */
    /*     flux_log_error (sdp->h, "sd_bus_message_append"); */
    /*     return -1; */
    /* } */

    /* if (sd_bus_message_append (m, "(sv)", "Group", "s", arg_exec_group) < 0) { */
    /*     flux_log_error (sdp->h, "sd_bus_message_append"); */
    /*     return -1; */
    /* } */

    /* if (sd_bus_message_append (m, "(sv)", "WorkingDirectory", "s", arg_working_directory) < 0) { */
    /*     flux_log_error (sdp->h, "sd_bus_message_append"); */
    /*     return -1; */
    /* } */

    /* Stdio */

    if (sdp->stdin_fd >= 0) {
        if (sd_bus_message_append (m,
                                   "(sv)",
                                   "StandardInputFileDescriptor", "h", sdp->stdin_fd) < 0) {
            flux_log_error (sdp->h, "sd_bus_message_append");
            return -1;
        }
    }

    if (sdp->stdout_fd >= 0) {
        if (sd_bus_message_append (m,
                                   "(sv)",
                                   "StandardOutputFileDescriptor", "h", sdp->stdout_fd) < 0) {
            flux_log_error (sdp->h, "sd_bus_message_append");
            return -1;
        }
    }

    if (sdp->stderr_fd >= 0) {
        if (sd_bus_message_append (m,
                                   "(sv)",
                                   "StandardErrorFileDescriptor", "h", sdp->stderr_fd) < 0) {
            flux_log_error (sdp->h, "sd_bus_message_append");
            return -1;
        }
    }

    /* Environment */

    if (sdp->envv) {
        if (sd_bus_message_open_container (m, 'r', "sv") < 0) {
            flux_log_error (sdp->h, "sd_bus_message_open_container");
            return -1;
        }

        if (sd_bus_message_append (m, "s", "Environment") < 0) {
            flux_log_error (sdp->h, "sd_bus_message_append");
            return -1;
        }

        if (sd_bus_message_open_container (m, 'v', "as") < 0) {
            flux_log_error (sdp->h, "sd_bus_message_open_container");
            return -1;
        }

        if (sd_bus_message_append_strv (m, sdp->envv) < 0) {
            flux_log_error (sdp->h, "sd_bus_message_append_strv");
            return -1;
        }

        if (sd_bus_message_close_container (m) < 0) {
            flux_log_error (sdp->h, "sd_bus_message_close_container");
            return -1;
        }

        if (sd_bus_message_close_container (m) < 0) {
            flux_log_error (sdp->h, "sd_bus_message_close_container");
            return -1;
        }
    }

    /* Cmdline */

    if (sd_bus_message_open_container (m, 'r', "sv") < 0) {
        flux_log_error (sdp->h, "sd_bus_message_open_container");
        return -1;
    }

    if (sd_bus_message_append (m, "s", "ExecStart") < 0) {
        flux_log_error (sdp->h, "sd_bus_message_append");
        return -1;
    }

    if (sd_bus_message_open_container (m, 'v', "a(sasb)") < 0) {
        flux_log_error (sdp->h, "sd_bus_message_open_container");
        return -1;
    }

    if (sd_bus_message_open_container (m, 'a', "(sasb)") < 0) {
        flux_log_error (sdp->h, "sd_bus_message_open_container");
        return -1;
    }

    if (sd_bus_message_open_container (m, 'r', "sasb") < 0) {
        flux_log_error (sdp->h, "sd_bus_message_open_container");
        return -1;
    }

    if (sd_bus_message_append (m, "s", sdp->argv[0]) < 0) {
        flux_log_error (sdp->h, "sd_bus_message_append");
        return -1;
    }

    if (sd_bus_message_append_strv (m, sdp->argv) < 0) {
        flux_log_error (sdp->h, "sd_bus_message_append_strv");
        return -1;
    }

    if (sd_bus_message_append (m, "b", false) < 0) {
        flux_log_error (sdp->h, "sd_bus_message_append");
        return -1;
    }

    if (sd_bus_message_close_container (m) < 0) {
        flux_log_error (sdp->h, "sd_bus_message_close_container");
        return -1;
    }

    if (sd_bus_message_close_container (m) < 0) {
        flux_log_error (sdp->h, "sd_bus_message_close_container");
        return -1;
    }

    if (sd_bus_message_close_container (m) < 0) {
        flux_log_error (sdp->h, "sd_bus_message_close_container");
        return -1;
    }

    if (sd_bus_message_close_container (m) < 0) {
        flux_log_error (sdp->h, "sd_bus_message_close_container");
        return -1;
    }

    return 0;
}

static int start_transient_service (flux_sdprocess_t *sdp)
{

    sd_bus_message *m = NULL, *reply = NULL;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    int ret = -1;

    if (sd_bus_message_new_method_call (sdp->bus,
                                        &m,
                                        "org.freedesktop.systemd1",
                                        "/org/freedesktop/systemd1",
                                        "org.freedesktop.systemd1.Manager",
                                        "StartTransientUnit") < 0) {
        flux_log_error (sdp->h, "sd_bus_message_new_method_call");
        goto cleanup;
    }

    /* Name and mode */
    if (sd_bus_message_append (m, "ss", sdp->service_name, "fail") < 0) {
        flux_log_error (sdp->h, "sd_bus_message_append");
        goto cleanup;
    }

    /* Properties */
    if (sd_bus_message_open_container (m, 'a', "(sv)") < 0) {
        flux_log_error (sdp->h, "sd_bus_message_open_container");
        goto cleanup;
    }

    if (transient_service_set_properties (sdp, m) < 0)
        goto cleanup;

    if (sd_bus_message_close_container (m) < 0) {
        flux_log_error (sdp->h, "sd_bus_message_close_container");
        goto cleanup;
    }

    /* Auxiliary units */
    if (sd_bus_message_append (m, "a(sa(sv))", 0) < 0) {
        flux_log_error (sdp->h, "sd_bus_message_append");
        goto cleanup;
    }

    if (sd_bus_call (sdp->bus, m, 0, &error, &reply) < 0) {
        flux_log (sdp->h, LOG_ERR, "sd_bus_call: %s",
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
    struct flux_sdprocess *sdp = NULL;
    flux_reactor_t *r;

    if (!unitname
        || !argv) {
        errno = EINVAL;
        return NULL;
    }

    if (!(r = flux_get_reactor (h)))
        return NULL;

    if (!(sdp = sdprocess_create (h,
                                  r,
                                  unitname,
                                  argv,
                                  envv,
                                  stdin_fd,
                                  stdout_fd,
                                  stderr_fd)))
        return NULL;

    if (start_transient_service (sdp) < 0)
        goto cleanup;

    return sdp;

cleanup:
    flux_sdprocess_destroy (sdp);
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
    struct flux_sdprocess *sdp = NULL;

    if (!unitname
        || !argv) {
        errno = EINVAL;
        return NULL;
    }

    if (!(sdp = sdprocess_create (NULL,
                                  r,
                                  unitname,
                                  argv,
                                  envv,
                                  stdin_fd,
                                  stdout_fd,
                                  stderr_fd)))
        return NULL;

    if (start_transient_service (sdp) < 0)
        goto cleanup;

    return sdp;

cleanup:
    flux_sdprocess_destroy (sdp);
    return NULL;
}

int flux_sdprocess_systemd_cleanup (flux_sdprocess_t *sdp)
{
    sd_bus_error error = SD_BUS_ERROR_NULL;
    char *active_state = NULL;
    char *load_state = NULL;
    int exec_main_code;
    int rv = -1;

    if (!sdp) {
        errno = EINVAL;
        return -1;
    }

    if (sd_bus_get_property_string (sdp->bus,
                                    "org.freedesktop.systemd1",
                                    sdp->service_path,
                                    "org.freedesktop.systemd1.Unit",
                                    "ActiveState",
                                    &error,
                                    &active_state) < 0) {
        flux_log (sdp->h, LOG_ERR, "sd_bus_get_property_string: %s",
                  error.message ? error.message : strerror (errno));
        goto cleanup;
    }

    if (sd_bus_get_property_string (sdp->bus,
                                    "org.freedesktop.systemd1",
                                    sdp->service_path,
                                    "org.freedesktop.systemd1.Unit",
                                    "LoadState",
                                    &error,
                                    &load_state) < 0) {
        flux_log (sdp->h, LOG_ERR, "sd_bus_get_property_string: %s",
                  error.message ? error.message : strerror (errno));
        goto cleanup;
    }

    if (!strcmp (active_state, "inactive")
        && !strcmp (load_state, "not-found")) {
        errno = ENOENT;
        goto cleanup;
    }

    if (sd_bus_get_property_trivial (sdp->bus,
                                     "org.freedesktop.systemd1",
                                     sdp->service_path,
                                     "org.freedesktop.systemd1.Service",
                                     "ExecMainCode",
                                     &error,
                                     'i',
                                     &exec_main_code) < 0) {
        flux_log (sdp->h, LOG_ERR, "sd_bus_get_property_trivial: %s",
                  error.message ? error.message : strerror (errno));
        goto cleanup;
    }

    if (!exec_main_code) {
        /* XXX - or EAGAIN? or EPERM? */
        errno = EBUSY;
        goto cleanup;
    }

    /* Due to "RemainAfterExit", an exited succesful process will stay "active" */
    if (!strcmp (active_state, "active")) {
        /* This is loosely the equivalent of:
         *
         * systemctl stop --user <unitname>.service
         */
        if (sd_bus_call_method (sdp->bus,
                                "org.freedesktop.systemd1",
                                "/org/freedesktop/systemd1",
                                "org.freedesktop.systemd1.Manager",
                                "StopUnit",
                                &error,
                                NULL,
                                "ss",
                                sdp->service_name, "fail") < 0) {
            flux_log (sdp->h, LOG_ERR, "sd_bus_call_method: %s",
                      error.message ? error.message : strerror (errno));
            goto cleanup;
        }
    }
    else if (!strcmp (active_state, "failed")
            || !strcmp (active_state, "inactive")) {
        /* This is loosely the equivalent of:
         *
         * systemctl reset-failed --user <unitname>.service
         */
        if (sd_bus_call_method (sdp->bus,
                                "org.freedesktop.systemd1",
                                "/org/freedesktop/systemd1",
                                "org.freedesktop.systemd1.Manager",
                                "ResetFailedUnit",
                                &error,
                                NULL,
                                "s",
                                sdp->service_name) < 0) {
            flux_log (sdp->h, LOG_ERR, "sd_bus_call_method: %s",
                      error.message ? error.message : strerror (errno));
            goto cleanup;
        }
    }
    else {
        /* Within libsdprocess states of "reloaded", "activating",
         * "deactivating" are presumably impossible?  Should only see
         * "active", "failed", or "inactive".  Return EAGAIN with no
         * better ideas of what to do if we reach a state we don't
         * know.
         */
        flux_log (sdp->h, LOG_ERR, "Cleanup of ActiveState=%s", active_state);
        errno = EAGAIN;
        goto cleanup;
    }

    rv = 0;
cleanup:
    sd_bus_error_free (&error);
    free (active_state);
    free (load_state);
    return rv;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
