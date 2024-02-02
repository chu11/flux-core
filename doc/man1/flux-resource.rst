.. flux-help-include: true
.. flux-help-section: instance

================
flux-resource(1)
================


SYNOPSIS
========

| **flux** **resource** **list** [*-n*] [*-o* *FORMAT*] [*-s* *STATES*] [*-i* *TARGETS*]
| **flux** **resource** **info** [*-s* *STATES*] [*-i* *TARGETS*]
| **flux** **resource** **R** [*-s* *STATES*] [*-i* *TARGETS*]

| **flux** **resource** **status** [*-n*] [*-o* *FORMAT*] [*-s* *STATES*] [*-i* *TARGETS*]

| **flux** **resource** **drain** [*-n*] [*-o* *FORMAT*] [*-i* *TARGETS*]
| **flux** **resource** **drain** [*-f*] [*-u*] [*targets*] [*reason*]
| **flux** **resource** **undrain** *targets*

| **flux** **resource** **reload** [-f] [--xml] *path*

| **flux** **resource** **watch** [-a] [-r]

DESCRIPTION
===========

.. program:: flux resource

:program:`flux resource` lists and manipulates Flux resources.  The resource
inventory is maintained and monitored by the resource service.  The scheduler
acquires a subset of resources from the resource service to allocate to jobs,
and relies on the resource service to inform it of status changes that affect
the usability of resources by jobs as described in RFC 27.

The :program:`flux resource list` subcommand queries the scheduler for its view
of resources, including allocated/free status.

The other :program:`flux resource` subcommands operate on the resource service
and are primarily of interest to system administrators of a Flux system
instance.  For example, they can show whether or not a node is booted, and may
be used to administratively drain and undrain nodes.

A few notes on drained nodes:

- While a node is drained, the scheduler will not allocate it to jobs.
- The act of draining a node does not affect running jobs.
- When an instance is restarted, drained nodes remain drained.
- The scheduler may determine that a job request is *feasible* if the total
  resource set, including drained nodes, would allow it to run.
- In :program:`flux resource status` and :program:`flux resource drain`, the
  drain state of a node will be presented as "drained" if the node has no job
  allocations, and "draining" if there are still jobs running on the node.
- If a node is drained and offline, then "drained*" will be displayed.

Some further background on resource service operation may be found in the
`RESOURCE INVENTORY`_ section below.


COMMANDS
========

list
----

.. program:: flux resource list

Show the scheduler view of resources.

In the scheduler view, excluded resources are always omitted, and
unavailable resources are shown in the "down" state regardless of the
reason (drained, offline, etc).  Valid states in the scheduler view are:

up
  Available for use.

down
  Unavailable for use.

allocated
  Allocated to jobs.

free
  Not allocated to jobs.

all
  Wildcard matching all resources.

.. option:: -s, --states=STATE,...

  Restrict displayed resource states to a comma separated list of
  the states listed above.

  If unspecified, :option:`free,allocated,down` is used.

.. option:: -i, --include=TARGETS

  Filter results to only include resources matching *TARGETS*, which may be
  specified either as an idset of broker ranks or list of hosts in hostlist
  form. It is not an error to specify ranks or hosts which do not exist.

.. option:: -o, --format=FORMAT

  Customize the output format (See the `OUTPUT FORMAT`_ section below).

.. option:: -n, --no-header

  Suppress header from output,

info
----

.. program:: flux resource info

Show a brief, single line summary of scheduler view of resources, for
example::

  8 Nodes, 32 Cores, 0 GPUs

.. option:: -s, --states=STATE,...

  Limit the output to specified resource states as described above for
  the `list`_ command.

  If unspecified, :option:`all` is used.

.. option:: -i, --include=TARGETS

  Filter results to only include resources matching *TARGETS*, which may be
  specified either as an idset of broker ranks or list of hosts in hostlist
  form. It is not an error to specify ranks or hosts which do not exist.

R
-

.. program:: flux resource R

Emit an RFC 20 Resource Set based on the scheduler view of resources.

.. option:: -s, --states=STATE,...

  Limit the output to specified resource states as described above for
  the `list`_ command.

  If unspecified, :option:`all` is used.

.. option:: -i, --include=TARGETS

  Filter results to only include resources matching *TARGETS*, which may be
  specified either as an idset of broker ranks or list of hosts in hostlist
  form. It is not an error to specify ranks or hosts which do not exist.

status
------

.. program:: flux resource status

Show system view of resources.  Valid states in the system view are:

avail
  available for scheduling when up

exclude
  excluded by configuration

draining
  drained but still allocated

drained
  drained and unallocated

drain
  shorthand for :option:`drained,draining`

offline
  node has not joined the Flux instance (e.g. turned off or has not
  started the flux broker).

online
  node has joined the Flux instance

:program:`flux resource status` displays a line of output for each set of
resources that share a state and online/offline state.

.. note::
  :program:`flux resource status` queries both the resource service and
  the scheduler to identify resources that are available, excluded by
  configuration, or administratively drained or draining.

.. option:: -s, --states=STATE,...

  Restrict the set of resource states a comma-separated list.

  If unspecified, :option:`avail,exclude,draining,drained` is used.


.. option:: -i, --include=TARGETS

  Filter the results to only include resources matching *TARGETS*, which
  may be specified either as an idset of broker ranks or list of hosts in
  hostlist form. It is not an error to specify ranks or hosts which do not
  exist.

.. option:: -o, --format=FORMAT

  Customize output formatting.  See the `OUTPUT FORMAT`_ section below for
  details.

.. option:: -n,--no-header

  Suppress header from output,

.. option:: --skip-empty

  Force suppression of empty lines.

  Normally, :program:`flux resource status` skips lines with no resources,
  unless the :option:`-s, --states` option is used.

drain
-----

.. program:: flux resource drain

If specified without *targets*, list the drained nodes. In this mode, the
following options are available:

.. option:: -o, --format=FORMAT

  Customize output formatting.  See the `OUTPUT FORMAT`_ section below for
  details.

.. option:: -n,--no-header

  Suppress header from output,

.. option:: -i, --include=TARGETS

  Filter the results to only include resources matching *TARGETS*, which
  may be specified either as an idset of broker ranks or list of hosts in
  hostlist form. It is not an error to specify ranks or hosts which do not
  exist.

If specified with *targets* (IDSET or HOSTLIST), drain the specified nodes.
Any remaining free arguments are recorded as a reason for the drain event.
By default, :program:`flux resource drain` fails if any of the *targets*
are already drained.

Resources cannot be both excluded and drained, so
:program:`flux resource drain` will also fail if any *targets* are
currently excluded by configuration.  There is no option to force an
excluded node into the drain state.

This command, when run with arguments, is restricted to the Flux instance
owner.

.. option:: -f, --force

  If any of *targets* are already drained, do not fail.  Overwrite the
  original drain reason.  When :option:`--force` is specified twice,
  the original drain timestamp is also overwritten.

.. option:: -u, --update

  If any of *targets* are already drained, do not fail and do not overwrite
  the existing drain reason or timestamp.

undrain
-------

.. program:: flux resource undrain

Undrain the nodes specified by the *targets* argument (IDSET or HOSTLIST).

This command is restricted to the Flux instance owner.

reload
------

.. program:: flux resource reload

Reload the resource inventory from *path*.  By default, *path* refers to a
file in RFC 20 format.

This command is primarily used in test.

.. option:: -x, --xml

  Interpret *path* as a directory of hwloc ``<rank>.xml`` files.

.. option:: -f, --force

  Do not fail if resource contain invalid ranks.

watch
-----

.. program:: flux resource watch

Output changes in resources from the *resource.eventlog* log as they
occur.  May be useful when needing to see resource changes in real
time.

.. option:: -a, --all

  Output all historical changes, not just new ones

.. option:: -r, --ranks

  Output ranks instead of hosts


OUTPUT FORMAT
=============

The :option:`--format` option can be used to specify an output format using
Python's string format syntax or a defined format by name. For a list of
built-in and configured formats use :option:`-o help`.  An alternate default
format can be set via the :envvar:`FLUX_RESOURCE_STATUS_FORMAT_DEFAULT`,
:envvar:`FLUX_RESOURCE_DRAIN_FORMAT_DEFAULT`, and
:envvar:`FLUX_RESOURCE_LIST_FORMAT_DEFAULT` environment variables (for
:program:`flux resource status`, :program:`flux resource drain`, and
:program:`flux resource list` respectively).  A configuration snippet for an
existing named format may be generated with :option:`--format=get-config=NAME`.
See :man1:`flux-jobs` :ref:`flux_jobs_output_format` section for a detailed
description of this syntax.

Resources are combined into a single line of output when possible depending on
the supplied output format.  Resource counts are not included in the
determination of uniqueness.  Therefore, certain output formats will alter the
number of lines of output.  For example:

::

   $ flux resource list -no {nnodes}

Would simply output a single of output containing the total number of nodes.
The actual state of the nodes would not matter in the output.

The following field names can be specified for the **status** and **drain**
subcommands:

**state**
   State of node(s): "avail", "exclude", "drain", "draining", "drained". If
   the set of resources is offline, an asterisk suffix is appended to the
   state, e.g. "avail*".

**statex**
   Like **state**, but exclude the asterisk for offline resources.

**status**
   Current online/offline status of nodes(s): "online", "offline"

**up**
   Displays a *✔* if the node is online, or *✗* if offline. An ascii *y*
   or *n* may be used instead with **up.ascii**.

**nnodes**
   number of nodes

**ranks**
   ranks of nodes

**nodelist**
   node names

**timestamp**
   If node(s) in drain/draining/drained state, timestamp of node(s)
   set to drain.

**reason**
   If node(s) in drain/draining/drained state, reason node(s) set to
   drain.

The following field names can be specified for the **list** subcommand:

**state**
   State of node(s): "up", "down", "allocated", "free", "all"

**queue**
   queue(s) associated with resources.

**properties**
   Properties associated with resources.

**propertiesx**
   Properties associated with resources, but with queue names removed.

**nnodes**
   number of nodes

**ncores**
   number of cores

**ngpus**
   number of gpus

**ranks**
   ranks of nodes

**nodelist**
   node names

**rlist**
   Short form string of all resources.


CONFIGURATION
=============

Similar to :man1:`flux-jobs`, the :program:`flux resource` command supports
loading a set of config files for customizing utility output formats. Currently
this can be used to register named format strings for the ``status``,
``list``, and ``drain`` subcommands.

Configuration for each :program:`flux resource` subcommand is defined in a
separate table, so to add a new format ``myformat`` for ``flux resource list``,
the following config file could be used::

  # $HOME/.config/flux/flux-resource.toml
  [list.formats.myformat]
  description = "My flux resource list format"
  format = "{state} {nodelist}"

See :man1:`flux-jobs` :ref:`flux_jobs_configuration` section for more
information about the order of precedence for loading these config files.

RESOURCE INVENTORY
==================

The Flux instance's inventory of resources is managed by the resource service,
which determines the set of available resources through one of three
mechanisms:

configuration
   Resources are read from a config file in RFC 20 (R version 1) format.
   This mechanism is typically used in a system instance of Flux.

enclosing instance
   Resources are assigned by the enclosing Flux instance.  The assigned
   resources are read from the job's ``R`` key in the enclosing instance KVS.

dynamic discovery
   Resources are aggregated from the set of resources reported by hwloc
   on each broker.

Once the inventory has been determined, it is stored the KVS ``resource.R``
key, in RFC 20 (R version 1) format.

Events that affect the availability of resources are posted to the KVS
*resource.eventlog*.  Such events include:

resource-define
   The resource inventory is defined with an initial set of drained, online,
   and excluded nodes.

drain
   One or more nodes are administratively removed from scheduling.

undrain
   One or more nodes are no longer drained.

offline
   One or more nodes are removed from scheduling due to unavailability,
   e.g. node was shutdown or crashed.

online
   One or more nodes are no longer offline.


RESOURCES
=========

.. include:: common/resources.rst


FLUX RFC
========

| :doc:`rfc:spec_20`
| :doc:`rfc:spec_22`
| :doc:`rfc:spec_27`
| :doc:`rfc:spec_29`
