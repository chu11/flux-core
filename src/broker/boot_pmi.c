/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
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
#include <limits.h>
#include <unistd.h>
#include <jansson.h>
#include <flux/hostlist.h>
#include <flux/taskmap.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/cleanup.h"
#include "src/common/libutil/ipaddr.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libpmi/upmi.h"
#include "ccan/str/str.h"

#include "attr.h"
#include "overlay.h"
#include "topology.h"
#include "boot_pmi.h"


/*  If the broker is launched via flux-shell, then the shell may opt
 *  to set a "flux.instance-level" parameter in the PMI kvs to tell
 *  the booting instance at what "level" it will be running, i.e. the
 *  number of parents. If the PMI key is missing, this is not an error,
 *  instead the level of this instance is considered to be zero.
 */
static int set_instance_level_attr (struct upmi *upmi,
                                    attr_t *attrs,
                                    bool *under_flux)
{
    char *val = NULL;
    int rc = -1;

    (void)upmi_get (upmi, "flux.instance-level", -1, &val, NULL);
    if (attr_add (attrs, "instance-level", val ? val : "0", ATTR_IMMUTABLE) < 0)
        goto error;
    *under_flux = val ? true : false;
    rc = 0;
error:
    ERRNO_SAFE_WRAP (free, val);
    return rc;
}

/* If the tbon.interface-hint broker attr is not already set, set it.
 * If running under Flux, use the value, if any, placed in PMI KVS by
 * the enclsoing instance.  Otherwise, set a default value.
 */
static int set_tbon_interface_hint_attr (struct upmi *upmi,
                                         attr_t *attrs,
                                         struct overlay *ov,
                                         bool under_flux)
{
    char *val = NULL;
    int rc = -1;

    if (attr_get (attrs, "tbon.interface-hint", NULL, NULL) == 0)
        return 0;
    if (under_flux)
        (void)upmi_get (upmi, "flux.tbon-interface-hint", -1, &val, NULL);
    if (overlay_set_tbon_interface_hint (ov, val) < 0)
        goto error;
    rc = 0;
error:
    ERRNO_SAFE_WRAP (free, val);
    return rc;
}

static char *pmi_mapping_to_taskmap (const char *s)
{
    char *result;
    flux_error_t error;
    struct taskmap *map = taskmap_decode (s, &error);
    if (!map) {
        log_err ("failed to decode PMI_process_mapping: %s",
                 error.text);
        return NULL;
    }
    result = taskmap_encode (map, 0);
    taskmap_destroy (map);
    return result;
}

/* Set broker.mapping attribute from enclosing instance taskmap.
 */
static int set_broker_mapping_attr (struct upmi *upmi,
                                    int size,
                                    attr_t *attrs)
{
    char *val = NULL;
    int rc;

    if (size == 1)
        val = strdup ("[[0,1,1,1]]");
    else {
        /* First attempt to get flux.taskmap, falling back to
         * PMI_process_mapping if this key is not available.
         */
        char *s;
        if (upmi_get (upmi, "flux.taskmap", -1, &s, NULL) == 0
            || upmi_get (upmi, "PMI_process_mapping", -1, &s, NULL) == 0) {
            val = pmi_mapping_to_taskmap (s);
            free (s);
        }
    }
    rc = attr_add (attrs, "broker.mapping", val, ATTR_IMMUTABLE);
    free (val);
    return rc;
}

/* Check if IPC can be used to communicate.
 * Currently this only goes so far as to check if the process mapping of
 * brokers has all brokers on the same node.  We could check if all peers
 * are on the same node, but given how the TBON maps to rank assignments,
 * it is fairly unlikely.
 */
static bool use_ipc (attr_t *attrs)
{
    bool result = false;
    struct taskmap *map = NULL;
    const char *val;

    if (attr_get (attrs, "tbon.prefertcp", &val, NULL) == 0
        && !streq (val, "0"))
        goto done;
    if (attr_get (attrs, "broker.mapping", &val, NULL) < 0 || !val)
        goto done;
    if (!(map = taskmap_decode (val, NULL)))
        goto done;
    if (taskmap_nnodes (map) == 1)
        result = true;
done:
    taskmap_destroy (map);
    return result;
}

/* Build URI for broker TBON to bind to.
 * If IPC, use '<rundir>/tbon-<rank>' which should be unique if there are
 * multiple brokers and/or multiple instances per node.
 * If using TCP, choose the address to be the one associated with the default
 * route (see src/common/libutil/ipaddr.h), and a randomly chosen port.
 */
static int format_bind_uri (char *buf, int bufsz, attr_t *attrs, int rank)
{
    if (use_ipc (attrs)) {
        const char *rundir;

        if (attr_get (attrs, "rundir", &rundir, NULL) < 0) {
            log_err ("rundir attribute is not set");
            return -1;
        }
        if (snprintf (buf, bufsz, "ipc://%s/tbon-%d", rundir, rank) >= bufsz)
            goto overflow;
    }
    else {
        char ipaddr[_POSIX_HOST_NAME_MAX + 1];
        flux_error_t error;
        int flags = 0;
        const char *interface = NULL;
        const char *hint;

        if (attr_get (attrs, "tbon.interface-hint", &hint, NULL) < 0) {
            log_err ("tbon.interface-hint attribute is not set");
            return -1;
        }
        if (streq (hint, "hostname"))
            flags |= IPADDR_HOSTNAME;
        else if (streq (hint, "default-route"))
            ; // default behavior
        else
            interface = hint;
        if (getenv ("FLUX_IPADDR_V6"))
            flags |= IPADDR_V6;
        if (ipaddr_getprimary (ipaddr,
                               sizeof (ipaddr),
                               flags,
                               interface,
                               &error) < 0) {
            log_msg ("%s", error.text);
            return -1;
        }
        if (snprintf (buf, bufsz, "tcp://%s:*", ipaddr) >= bufsz)
            goto overflow;
    }
    return 0;
overflow:
    log_msg ("buffer overflow while building bind URI");
    return -1;
}

static int set_hostlist_attr (attr_t *attrs, struct hostlist *hl)
{
    const char *value;
    char *s;
    int rc = -1;

    /*  Allow hostlist attribute to be set on command line for testing.
     *  The value must be re-added if so, so that the IMMUTABLE flag can
     *   be set so that the attribute is properly cached.
     */
    if (attr_get (attrs, "hostlist", &value, NULL) == 0) {
        s = strdup (value);
        (void) attr_delete (attrs, "hostlist", true);
    }
    else
        s = hostlist_encode (hl);
    if (s && attr_add (attrs, "hostlist", s, ATTR_IMMUTABLE) == 0)
        rc = 0;
    ERRNO_SAFE_WRAP (free, s);
    return rc;
}

static int set_broker_boot_method_attr (attr_t *attrs, const char *value)
{
    (void)attr_delete (attrs, "broker.boot-method", true);
    if (attr_add (attrs,
                  "broker.boot-method",
                  value,
                  ATTR_IMMUTABLE) < 0) {
        log_err ("setattr broker.boot-method");
        return -1;
    }
    return 0;
}

static void trace_upmi (void *arg, const char *text)
{
    fprintf (stderr, "boot_pmi: %s\n", text);
}

int boot_pmi (const char *hostname, struct overlay *overlay, attr_t *attrs)
{
    const char *topo_uri;
    flux_error_t error;
    json_error_t jerror;
    char key[64];
    char *val;
    char *bizcard = NULL;
    struct hostlist *hl = NULL;
    json_t *o;
    struct upmi *upmi;
    struct upmi_info info;
    struct topology *topo = NULL;
    int child_count;
    int *child_ranks = NULL;
    const char *uri;
    int i;
    int upmi_flags = UPMI_LIBPMI_NOFLUX;
    const char *upmi_method;
    bool under_flux;

    // N.B. overlay_create() sets the tbon.topo attribute
    if (attr_get (attrs, "tbon.topo", &topo_uri, NULL) < 0) {
        log_msg ("error fetching tbon.topo attribute");
        return -1;
    }
    if (attr_get (attrs, "broker.boot-method", &upmi_method, NULL) < 0)
        upmi_method = NULL;
    if (getenv ("FLUX_PMI_DEBUG"))
        upmi_flags |= UPMI_TRACE;
    if (!(upmi = upmi_create (upmi_method,
                              upmi_flags,
                              trace_upmi,
                              NULL,
                              &error))) {
        log_msg ("boot_pmi: %s", error.text);
        return -1;
    }
    if (upmi_initialize (upmi, &info, &error) < 0) {
        log_msg ("%s: initialize: %s", upmi_describe (upmi), error.text);
        upmi_destroy (upmi);
        return -1;
    }
    if (set_instance_level_attr (upmi, attrs, &under_flux) < 0) {
        log_err ("set_instance_level_attr");
        goto error;
    }
    if (under_flux) {
        if (attr_add (attrs, "jobid", info.name, ATTR_IMMUTABLE) < 0) {
            log_err ("error setting jobid attribute");
            goto error;
        }
    }
    if (set_tbon_interface_hint_attr (upmi, attrs, overlay, under_flux) < 0) {
        log_err ("error setting tbon.interface-hint attribute");
        goto error;
    }
    if (set_broker_mapping_attr (upmi, info.size, attrs) < 0) {
        log_err ("error setting broker.mapping attribute");
        goto error;
    }
    if (!(topo = topology_create (topo_uri, info.size, &error))) {
        log_msg ("error creating '%s' topology: %s", topo_uri, error.text);
        goto error;
    }
    if (topology_set_rank (topo, info.rank) < 0
        || overlay_set_topology (overlay, topo) < 0)
        goto error;
    if (!(hl = hostlist_create ())) {
        log_err ("hostlist_create");
        goto error;
    }

    /* A size=1 instance has no peers, so skip the PMI exchange.
     */
    if (info.size == 1) {
        if (hostlist_append (hl, hostname) < 0) {
            log_err ("hostlist_append");
            goto error;
        }
        goto done;
    }

    /* Enable ipv6 for maximum flexibility in address selection.
     */
    overlay_set_ipv6 (overlay, 1);

    child_count = topology_get_child_ranks (topo, NULL, 0);
    if (child_count > 0) {
        if (!(child_ranks = calloc (child_count, sizeof (child_ranks[0])))
            || topology_get_child_ranks (topo, child_ranks, child_count) < 0)
            goto error;
    }

    /* If there are to be downstream peers, then bind to socket and extract
     * the concretized URI for sharing with other ranks.
     */
    if (child_count > 0) {
        char buf[1024];

        if (format_bind_uri (buf, sizeof (buf), attrs, info.rank) < 0)
            goto error;
        if (overlay_bind (overlay, buf) < 0)
            goto error;
        uri = overlay_get_bind_uri (overlay);
    }
    else {
        uri = NULL;
    }
    if (attr_add (attrs, "tbon.endpoint", uri, ATTR_IMMUTABLE) < 0) {
        log_err ("setattr tbon.endpoint");
        goto error;
    }

    /* Each broker writes a "business card" consisting of hostname,
     * public key, and URI (empty string for leaf node).
     */
    if (snprintf (key, sizeof (key), "%d", info.rank) >= sizeof (key)) {
        log_msg ("pmi key string overflow");
        goto error;
    }
    if (!(o = json_pack ("{s:s s:s s:s}",
                         "hostname", hostname,
                         "pubkey", overlay_cert_pubkey (overlay),
                         "uri", uri ? uri : ""))
        || !(bizcard = json_dumps (o, JSON_COMPACT))) {
        log_msg ("error encoding pmi business card object");
        json_decref (o);
        goto error;
    }
    json_decref (o);
    if (upmi_put (upmi, key, bizcard, &error) < 0) {
        log_msg ("%s: put %s: %s", upmi_describe (upmi), key, error.text);
        goto error;
    }
    if (upmi_barrier (upmi, &error) < 0) {
        log_msg ("%s: barrier: %s", upmi_describe (upmi), error.text);
        goto error;
    }

    /* Fetch the business card of parent and inform overlay of URI
     * and public key.
     */
    if (info.rank > 0) {
        const char *peer_pubkey;
        const char *peer_uri;
        int parent_rank = topology_get_parent (topo);

        if (snprintf (key, sizeof (key), "%d", parent_rank) >= sizeof (key)) {
            log_msg ("pmi key string overflow");
            goto error;
        }
        if (upmi_get (upmi, key, parent_rank, &val, &error) < 0) {
            log_msg ("%s: get %s: %s", upmi_describe (upmi), key, error.text);
            goto error;
        }
        if (!(o = json_loads (val, 0, &jerror))
            || json_unpack_ex (o,
                               &jerror,
                               0,
                               "{s:s s:s}",
                               "pubkey", &peer_pubkey,
                               "uri", &peer_uri) < 0) {
            log_msg ("error decoding rank %d business card: %s",
                     parent_rank,
                     jerror.text);
            json_decref (o);
            free (val);
            goto error;
        }
        if (strlen (peer_uri) == 0) {
            log_msg ("error decoding rank %d business card", parent_rank);
            json_decref (o);
            free (val);
            goto error;
        }
        if (overlay_set_parent_uri (overlay, peer_uri) < 0) {
            log_err ("overlay_set_parent_uri");
            json_decref (o);
            free (val);
            goto error;
        }
        if (overlay_set_parent_pubkey (overlay, peer_pubkey) < 0) {
            log_err ("overlay_set_parent_pubkey");
            json_decref (o);
            free (val);
            goto error;
        }
        json_decref (o);
        free (val);
    }

    /* Fetch the business card of children and inform overlay of public keys.
     */
    for (i = 0; i < child_count; i++) {
        const char *peer_pubkey;
        int child_rank = child_ranks[i];

        if (snprintf (key, sizeof (key), "%d", child_rank) >= sizeof (key)) {
            log_msg ("pmi key string overflow");
            goto error;
        }
        if (upmi_get (upmi, key, child_rank, &val, &error) < 0) {
            log_msg ("%s: get %s: %s", upmi_describe (upmi), key, error.text);
            goto error;
        }
        if (!(o = json_loads (val, 0, &jerror))
            || json_unpack_ex (o,
                               &jerror,
                               0,
                               "{s:s}",
                               "pubkey", &peer_pubkey) < 0) {
            log_msg ("error decoding rank %d business card: %s",
                     child_rank,
                     jerror.text);
            json_decref (o);
            free (val);
            goto error;
        }
        if (overlay_authorize (overlay, key, peer_pubkey) < 0) {
            log_err ("overlay_authorize %s=%s", key, peer_pubkey);
            json_decref (o);
            free (val);
            goto error;
        }
        json_decref (o);
        free (val);
    }

    /* Fetch the business card of all ranks and build hostlist.
     * The hostlist is built independently (and in parallel) on all ranks.
     */
    for (i = 0; i < info.size; i++) {
        const char *peer_hostname;

        if (snprintf (key, sizeof (key), "%d", i) >= sizeof (key)) {
            log_msg ("pmi key string overflow");
            goto error;
        }
        if (upmi_get (upmi, key, i, &val, &error) < 0) {
            log_msg ("%s: get %s: %s", upmi_describe (upmi), key, error.text);
            goto error;
        }
        if (!(o = json_loads (val, 0, &jerror))
            || json_unpack_ex (o,
                               &jerror,
                               0,
                               "{s:s}",
                               "hostname", &peer_hostname) < 0) {
            log_msg ("error decoding rank %d pmi business card: %s",
                     i,
                     error.text);
            json_decref (o);
            free (val);
            goto error;
        }
        if (hostlist_append (hl, peer_hostname) < 0) {
            log_err ("hostlist_append");
            json_decref (o);
            free (val);
            goto error;
        }
        json_decref (o);
        free (val);
    }

    /* One more barrier before allowing connects to commence.
     * Need to ensure that all clients are "allowed".
     */
    if (upmi_barrier (upmi, &error) < 0) {
        log_msg ("%s: barrier: %s", upmi_describe (upmi), error.text);
        goto error;
    }

done:
    if (set_hostlist_attr (attrs, hl) < 0) {
        log_err ("setattr hostlist");
        goto error;
    }
    if (set_broker_boot_method_attr (attrs, upmi_describe (upmi)) < 0)
        goto error;
    if (upmi_finalize (upmi, &error) < 0) {
        log_msg ("%s: finalize: %s", upmi_describe (upmi), error.text);
        goto error;
    }
    free (bizcard);
    upmi_destroy (upmi);
    hostlist_destroy (hl);
    free (child_ranks);
    topology_decref (topo);
    return 0;
error:
    /* We've logged error to stderr before getting here so the fatal
     * error message passed to the PMI server does not necessarily need
     * to be highly detailed.  Some implementations of abort may not
     * return.
     */
    if (upmi_abort (upmi, "fatal bootstrap error", &error) < 0)
        log_msg ("upmi_abort: %s", error.text);
    free (bizcard);
    upmi_destroy (upmi);
    hostlist_destroy (hl);
    free (child_ranks);
    topology_decref (topo);
    return -1;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
