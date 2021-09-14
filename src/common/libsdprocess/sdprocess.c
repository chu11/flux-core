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
#include <stdint.h>
#include <unistd.h>
#include <limits.h>

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

    /* wait watcher */
    flux_watcher_t *w;

    /* exit stuff */
    bool completed;
    uint32_t exec_main_status;
    int exec_main_code;
    char *active_state;
    char *result;
    int exit_status;
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

        if (sdp->w) {
            flux_watcher_stop (sdp->w);
            flux_watcher_destroy (sdp->w);
        }

        free (sdp->active_state);
        free (sdp->result);
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

static int get_final_properties (flux_sdprocess_t *sdp)
{
    sd_bus_error error = SD_BUS_ERROR_NULL;
    uint32_t exec_main_status;
    char *result = NULL;
    int rv = -1;

    if (sd_bus_get_property_trivial (sdp->bus,
                                     "org.freedesktop.systemd1",
                                     sdp->service_path,
                                     "org.freedesktop.systemd1.Service",
                                     "ExecMainStatus",
                                     &error,
                                     'i',
                                     &exec_main_status) < 0) {
        flux_log (sdp->h, LOG_ERR, "sd_bus_get_property_trivial: %s",
                  error.message ? error.message : strerror (errno));
        goto cleanup;
    }

    if (sd_bus_get_property_string (sdp->bus,
                                    "org.freedesktop.systemd1",
                                    sdp->service_path,
                                    "org.freedesktop.systemd1.Service",
                                    "Result",
                                    &error,
                                    &result) < 0) {
        flux_log (sdp->h, LOG_ERR, "sd_bus_get_property_string: %s",
                  error.message ? error.message : strerror (errno));
        goto cleanup;
    }
    
    sdp->exec_main_status = exec_main_status;
    sdp->result = result;
    rv = 0;
 cleanup:
    sd_bus_error_free (&error);
    if (rv < 0)
        free (result);
    return rv;
}

static int calc_exit_status (flux_sdprocess_t *sdp, const char *active_state)
{
    /* XXX should 128 be done at a higher level, not here? */
    /* result guaranteed to be set by this point? */
    if (!strcmp (sdp->result, "signal"))
        sdp->exit_status = sdp->exec_main_status + 128;
    else
        sdp->exit_status = sdp->exec_main_status;

    return 0;
}

static int get_properties_changed (flux_sdprocess_t *sdp)
{
    sd_bus_message *m = NULL;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    int ret, rv = -1;

    if (sd_bus_call_method (sdp->bus,
                            "org.freedesktop.systemd1",
                            sdp->service_path,
                            "org.freedesktop.DBus.Properties",
                            "GetAll",
                            &error,
                            &m,
                            "s", "") < 0) {
        flux_log (sdp->h, LOG_ERR, "sd_bus_call_method: %s",
                  error.message ? error.message : strerror (errno));
        goto cleanup;
    }

    if (sd_bus_message_enter_container (m, SD_BUS_TYPE_ARRAY, "{sv}") < 0) {
        flux_log (sdp->h, LOG_ERR, "sd_bus_message_enter_container: %s",
                  error.message ? error.message : strerror (errno));
        goto cleanup;
    }

    while ((ret = sd_bus_message_enter_container (m,
                                                  SD_BUS_TYPE_DICT_ENTRY,
                                                  "sv")) > 0) {
        const char *member;

        if (sd_bus_message_read_basic (m, SD_BUS_TYPE_STRING, &member) < 0) {
            flux_log (sdp->h, LOG_ERR, "sd_bus_message_read_basic: %s",
                      error.message ? error.message : strerror (errno));
            goto cleanup;
        }

        if (!strcmp (member, "ActiveState")) {
            const char *contents;
            const char *active_state;
            char type;

            if (sd_bus_message_peek_type (m, NULL, &contents) < 0) {
                flux_log (sdp->h, LOG_ERR, "sd_bus_message_peek_type: %s",
                          error.message ? error.message : strerror (errno));
                goto cleanup;
            }

            if (sd_bus_message_enter_container (m,
                                                SD_BUS_TYPE_VARIANT,
                                                contents) < 0) {
                flux_log (sdp->h, LOG_ERR, "sd_bus_message_enter_container: %s",
                          error.message ? error.message : strerror (errno));
                goto cleanup;
            }

            if (sd_bus_message_peek_type (m, &type, NULL) < 0) {
                flux_log (sdp->h, LOG_ERR, "sd_bus_message_peek_type: %s",
                          error.message ? error.message : strerror (errno));
                goto cleanup;
            }

            if (type != SD_BUS_TYPE_STRING) {
                flux_log (sdp->h, LOG_ERR, "Invalid type %d, expected %d",
                          type, SD_BUS_TYPE_STRING);
                goto cleanup;
            }

            if (sd_bus_message_read_basic (m, type, &active_state) < 0) {
                flux_log (sdp->h, LOG_ERR, "sd_bus_message_read_basic: %s",
                          error.message ? error.message : strerror (errno));
                goto cleanup;
            }

            if (!sdp->active_state
                || strcmp (sdp->active_state, active_state) != 0) {
                char *tmp;
                if (!(tmp = strdup (active_state)))
                    goto cleanup;
                free (sdp->active_state);
                sdp->active_state = tmp;
            }

            if (sd_bus_message_exit_container (m) < 0) {
                flux_log (sdp->h, LOG_ERR, "sd_bus_message_exit_container: %s",
                          error.message ? error.message : strerror (errno));
                goto cleanup;
            }
        }
        else if (!strcmp (member, "Result")) {
            const char *contents;
            const char *result;
            char type;

            if (sd_bus_message_peek_type (m, NULL, &contents) < 0) {
                flux_log (sdp->h, LOG_ERR, "sd_bus_message_peek_type: %s",
                          error.message ? error.message : strerror (errno));
                goto cleanup;
            }

            if (sd_bus_message_enter_container (m,
                                                SD_BUS_TYPE_VARIANT,
                                                contents) < 0) {
                flux_log (sdp->h, LOG_ERR, "sd_bus_message_enter_container: %s",
                          error.message ? error.message : strerror (errno));
                goto cleanup;
            }

            if (sd_bus_message_peek_type (m, &type, NULL) < 0) {
                flux_log (sdp->h, LOG_ERR, "sd_bus_message_peek_type: %s",
                          error.message ? error.message : strerror (errno));
                goto cleanup;
            }

            if (type != SD_BUS_TYPE_STRING) {
                flux_log (sdp->h, LOG_ERR, "Invalid type %d, expected %d",
                          type, SD_BUS_TYPE_STRING);
                goto cleanup;
            }

            if (sd_bus_message_read_basic (m, type, &result) < 0) {
                flux_log (sdp->h, LOG_ERR, "sd_bus_message_read_basic: %s",
                          error.message ? error.message : strerror (errno));
                goto cleanup;
            }

            if (!sdp->result
                || strcmp (sdp->result, result) != 0) {
                char *tmp;
                if (!(tmp = strdup (result)))
                    goto cleanup;
                free (sdp->result);
                sdp->result = tmp;
            }

            if (sd_bus_message_exit_container (m) < 0) {
                flux_log (sdp->h, LOG_ERR, "sd_bus_message_exit_container: %s",
                          error.message ? error.message : strerror (errno));
                goto cleanup;
            }
        }
        else if (!strcmp (member, "ExecMainStatus")) {
            const char *contents;
            uint32_t exec_main_status;
            char type;

            if (sd_bus_message_peek_type (m, NULL, &contents) < 0) {
                flux_log (sdp->h, LOG_ERR, "sd_bus_message_peek_type: %s",
                          error.message ? error.message : strerror (errno));
                goto cleanup;
            }

            if (sd_bus_message_enter_container (m,
                                                SD_BUS_TYPE_VARIANT,
                                                contents) < 0) {
                flux_log (sdp->h, LOG_ERR, "sd_bus_message_enter_container: %s",
                          error.message ? error.message : strerror (errno));
                goto cleanup;
            }

            if (sd_bus_message_peek_type (m, &type, NULL) < 0) {
                flux_log (sdp->h, LOG_ERR, "sd_bus_message_peek_type: %s",
                          error.message ? error.message : strerror (errno));
                goto cleanup;
            }

            if (type != SD_BUS_TYPE_INT32) {
                flux_log (sdp->h, LOG_ERR, "Invalid type %d, expected %d",
                          type, SD_BUS_TYPE_INT32);
                goto cleanup;
            }

            if (sd_bus_message_read_basic (m, type, &exec_main_status) < 0) {
                flux_log (sdp->h, LOG_ERR, "sd_bus_message_read_basic: %s",
                          error.message ? error.message : strerror (errno));
                goto cleanup;
            }

            if (sdp->exec_main_status != exec_main_status)
                sdp->exec_main_status = exec_main_status;

            if (sd_bus_message_exit_container (m) < 0) {
                flux_log (sdp->h, LOG_ERR, "sd_bus_message_exit_container: %s",
                          error.message ? error.message : strerror (errno));
                goto cleanup;
            }
        }
        else if (!strcmp (member, "ExecMainCode")) {
            const char *contents;
            uint32_t exec_main_code;
            char type;

            if (sd_bus_message_peek_type (m, NULL, &contents) < 0) {
                flux_log (sdp->h, LOG_ERR, "sd_bus_message_peek_type: %s",
                          error.message ? error.message : strerror (errno));
                goto cleanup;
            }

            if (sd_bus_message_enter_container (m,
                                                SD_BUS_TYPE_VARIANT,
                                                contents) < 0) {
                flux_log (sdp->h, LOG_ERR, "sd_bus_message_enter_container: %s",
                          error.message ? error.message : strerror (errno));
                goto cleanup;
            }

            if (sd_bus_message_peek_type (m, &type, NULL) < 0) {
                flux_log (sdp->h, LOG_ERR, "sd_bus_message_peek_type: %s",
                          error.message ? error.message : strerror (errno));
                goto cleanup;
            }

            if (type != SD_BUS_TYPE_INT32) {
                flux_log (sdp->h, LOG_ERR, "Invalid type %d, expected %d",
                          type, SD_BUS_TYPE_INT32);
                goto cleanup;
            }

            if (sd_bus_message_read_basic (m, type, &exec_main_code) < 0) {
                flux_log (sdp->h, LOG_ERR, "sd_bus_message_read_basic: %s",
                          error.message ? error.message : strerror (errno));
                goto cleanup;
            }

            if (sdp->exec_main_code != exec_main_code)
                sdp->exec_main_code = exec_main_code;

            if (sd_bus_message_exit_container (m) < 0) {
                flux_log (sdp->h, LOG_ERR, "sd_bus_message_exit_container: %s",
                          error.message ? error.message : strerror (errno));
                goto cleanup;
            }
        }
        else {
            if (sd_bus_message_skip (m, "v") < 0) {
                flux_log (sdp->h, LOG_ERR, "sd_bus_message_skip: %s",
                          error.message ? error.message : strerror (errno));
                goto cleanup;
            }
        }

        if (sd_bus_message_exit_container (m) < 0) {
            flux_log (sdp->h, LOG_ERR, "sd_bus_message_exit_container: %s",
                      error.message ? error.message : strerror (errno));
            goto cleanup;
        }
    }
    if (ret < 0) {
        flux_log (sdp->h, LOG_ERR, "sd_bus_message_enter_container: %s",
                  error.message ? error.message : strerror (errno));
        goto cleanup;
    }

    if (sd_bus_message_exit_container (m) < 0) {
        flux_log (sdp->h, LOG_ERR, "sd_bus_message_exit_container: %s",
                  error.message ? error.message : strerror (errno));
        goto cleanup;
    }

    if ((sdp->active_state &&
         (!strcmp (sdp->active_state, "inactive")
          || !strcmp (sdp->active_state, "failed")))
        || sdp->exec_main_code == CLD_EXITED) {
        /* XXX is it impossible for active_state to not yet be set here? */
        if (calc_exit_status (sdp, sdp->active_state) < 0)
            goto cleanup;
        sdp->completed = true;
    }

    rv = 0;
cleanup:
    sd_bus_message_unref (m);
    sd_bus_error_free (&error);
    return rv;
}

static int sdbus_properties_changed_cb (sd_bus_message *m,
                                        void *userdata,
                                        sd_bus_error *error)
{
    flux_sdprocess_t *sdp = userdata;
    return get_properties_changed (sdp);
}

static void watcher_properties_changed_cb (flux_reactor_t *r,
                                           flux_watcher_t *w,
                                           int revents,
                                           void *arg)
{
    flux_sdprocess_t *sdp = arg;

    if (revents & FLUX_POLLIN)
        while ( sd_bus_process (sdp->bus, NULL) ) { }
    else {
        flux_log (sdp->h, LOG_ERR, "Unexpected revents: %X", revents);
        return;
    }

    if (sdp->completed) {
        flux_watcher_stop (w);
        return;
    }
}

/* convert usec to millisecond, but with hacks to ensure we round up
 * and handle a few corner cases */
static int usec_to_ms (uint64_t usec)
{
    int timeout = 0;
    if (usec) {
        if (usec >= (UINT64_MAX - 1000))
            timeout = INT_MAX;
        else {
            uint64_t tmp;
            if ((usec % 1000) != 0)
                tmp = (usec + 1000) / 1000;
            else
                tmp = usec / 1000;

            if (tmp > INT_MAX)
                timeout = INT_MAX;
            else
                timeout = tmp;
        }
    }
    return timeout;
}

static int wait_watcher (flux_sdprocess_t *sdp)
{
    sd_bus_error error = SD_BUS_ERROR_NULL;
    flux_watcher_t *w = NULL;
    int fd;
    short events;
    int timeout = -1;
    int rv = -1;

    /* Subscribe to events on this systemd1 manager */
    if (sd_bus_call_method (sdp->bus,
                            "org.freedesktop.systemd1",
                            "/org/freedesktop/systemd1",
                            "org.freedesktop.systemd1.Manager",
                            "Subscribe",
                            &error,
                            NULL,
                            NULL) < 0) {
        if (!error.name
            || strcmp (error.name,
                       "org.freedesktop.systemd1.AlreadySubscribed") != 0) {
            flux_log (sdp->h, LOG_ERR, "sd_bus_call_method: %s",
                      error.message ? error.message : strerror (errno));
            goto cleanup;
        }
    }

    /* Setup callback when `sd_bus_process ()` is called on properties
     * changed */
    if (sd_bus_match_signal_async (sdp->bus,
                                   NULL,
                                   "org.freedesktop.systemd1",
                                   sdp->service_path,
                                   "org.freedesktop.DBus.Properties",
                                   "PropertiesChanged",
                                   sdbus_properties_changed_cb,
                                   NULL,
                                   sdp) < 0) {
        flux_log_error (sdp->h, "sd_bus_match_signal_async");
        goto cleanup;
    }

    while (1) {
        uint64_t usec;
        fd = sd_bus_get_fd (sdp->bus);
        events = sd_bus_get_events (sdp->bus);
        if (sd_bus_get_timeout (sdp->bus, &usec) >= 0)
            timeout = usec_to_ms (usec);

        /* if no events or no timeout, assume event ready to go right now */
        if (!events || !timeout) {
            while ( sd_bus_process (sdp->bus, NULL) ) {  }
            continue;
        }
        break;
    }

    if (sdp->completed)
        goto done;

    /* Assumption: bus will never change fd */
    if (!(w = flux_fd_watcher_create (sdp->r,
                                      fd,
                                      events,
                                      watcher_properties_changed_cb,
                                      sdp))) {
        flux_log_error (sdp->h, "flux_fd_watcher_create");
        goto cleanup;
    }

    flux_watcher_start (w);
    sdp->w = w;
done:
    rv = 0;
cleanup:
    if (rv < 0)
        flux_watcher_destroy (w);
    sd_bus_error_free (&error);
    return rv;
}

static int check_exist (flux_sdprocess_t *sdp)
{
    sd_bus_error error = SD_BUS_ERROR_NULL;
    char *active_state = NULL;
    char *load_state = NULL;
    int rv = -1;

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

    rv = 0;
cleanup:
    sd_bus_error_free (&error);
    free (active_state);
    free (load_state);
    return rv;
}

static int check_completed (flux_sdprocess_t *sdp)
{
    sd_bus_error error = SD_BUS_ERROR_NULL;
    char *active_state = NULL;
    int rv = -1;

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

    /* Within libsdprocess states of "reloaded", "activating",                                                                          
     * "deactivating" are presumably impossible?  Should only see                                                                       
     * "active", "failed", or "inactive".  Return EAGAIN with no                                                                        
     * better ideas of what to do if we reach a state we don't                                                                          
     * know.                                                                                                                            
     */   
    if (!strcmp (active_state, "inactive")
        || !strcmp (active_state, "failed")) {
        if (get_final_properties (sdp) < 0)
            goto cleanup;
        if (calc_exit_status (sdp, active_state) < 0)
            goto cleanup;
        sdp->completed = true;
        goto done;
    }
    else if (strcmp (active_state, "active")) {
        flux_log (sdp->h, LOG_ERR, "Wait of ActiveState=%s", active_state);
        errno = EAGAIN;
        goto cleanup;
    }

    if (!strcmp (active_state, "active")) {
        /* If unit is still active, possible it is done b/c of RemainAfterExit, so
         * check if unit completed.
         */
        int exec_main_code;

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

        if (exec_main_code == CLD_EXITED) {
            if (get_final_properties (sdp) < 0)
                goto cleanup;
            if (calc_exit_status (sdp, active_state) < 0)
                goto cleanup;
            sdp->completed = true;
            goto done;
        }
    }

done:
    rv = 0;
cleanup:
    sd_bus_error_free (&error);
    free (active_state);
    return rv;
}

int flux_sdprocess_wait (flux_sdprocess_t *sdp)
{
    if (!sdp) {
        errno = EINVAL;
        return -1;
    }

    if (check_exist (sdp) < 0)
        return -1;

    if (check_completed (sdp) < 0)
        return -1;

    if (sdp->completed)
        return 0;

    if (wait_watcher (sdp) < 0)
        return -1;

    /* Small racy window in which job completed all state changes
     * after we called check_completed() above, but before we finished
     * setting up in wait_watcher().  So no state changes will ever
     * occur going forward.  Call check_completed() again just in
     * case.
     */

    if (!sdp->active_state) {
        if (check_completed (sdp) < 0)
            return -1;
        if (sdp->completed) {
            flux_watcher_stop (sdp->w);
            return 0;
        }
    }

    return 0;
}

bool flux_sdprocess_completed (flux_sdprocess_t *sdp)
{
    if (!sdp)
        return false;
    return sdp->completed;
}

int flux_sdprocess_exit_status (flux_sdprocess_t *sdp)
{
    if (!sdp) {
        errno = EINVAL;
        return -1;
    }

    if (!sdp->completed) {
        /* XXX? */
        errno = EBUSY;
        return -1;
    }

    return sdp->exit_status;
}

int flux_sdprocess_systemd_cleanup (flux_sdprocess_t *sdp)
{
    sd_bus_error error = SD_BUS_ERROR_NULL;
    char *active_state = NULL;
    char *load_state = NULL;
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

    if (!strcmp (active_state, "active")) {
        int exec_main_code;

        /* Due to "RemainAfterExit", an exited succesful process will stay "active".
         * So we gotta make its actually exited.
         *
         * N.B. it is possible to go from "inactive" to "active" due
         * to "RemainAfterExit", so possible for EBUSY to be returned.
         */

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
