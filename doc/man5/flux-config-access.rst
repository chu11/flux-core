=====================
flux-config-access(5)
=====================


DESCRIPTION
===========

Flux normally denies access to all users except the instance owner (the user
running the Flux instance).  The system instance, however, runs as the
``flux`` user and permits limited access to guests, such as submitting work
and manipulating their own jobs.

The ``access`` table is required for a multi-user Flux system instance and may
contain the following keys:


KEYS
====

allow-guest-user
   (optional) Boolean value to allow guest users to connect and assigns them
   the ``user`` role.  If set to false or not present, connection attempts
   by guests fail with EPERM.

allow-root-owner
   (optional) Boolean value to assign ``owner`` role to root user.  If set to false
   or not present, root is treated like any other guest.

private-mode
   (optional) Boolean value to limit visibility of job data to guests.
   When true, job queries from guests are limited to their own jobs and
   aggregate job statistics are unavailable to them. The instance owner and
   users with the ``admin`` role are unaffected. This key is only meaningful
   when ``allow-guest-user`` is true.  If set to false or not present, guests
   may view all users' jobs.

roles
   (optional) A table of named roles that assign elevated capabilities to
   guests.  See `ROLES`_ below.  This key is only meaningful when
   ``allow-guest-user`` is true.


ROLES
=====

The ``access.roles`` table assigns supplemental roles to guest users based
on user name and group membership.  Currently only the ``admin`` role is
supported, which confers privileges beyond an ordinary guest but less than
the instance owner.  Services may open selected operations to users with the
``admin`` role.

Each named role may contain the following keys:

users
   (optional) An array of user names that are assigned the role.

groups
   (optional) An array of group names whose members are assigned the role.

User and group names are resolved at the time of connection.  A name that
cannot be resolved is logged and ignored rather than causing a connection to
fail.

.. note::
   Because these lookups are inline, a slow or stalled name service delays all
   connection attempts.  A name that cannot be resolved fails closed and does
   not compromise security, but administrators should ensure the name service
   fails fast so that transient failures do not stall connections.

EXAMPLE
=======

::

   [access]
   allow-guest-user = true
   allow-root-owner = true
   private-mode = true

   [access.roles.admin]
   users = [ "alice", "bob" ]
   groups = [ "flux" ]

If only configured by group, a one-line form may be used::

   [access]
   allow-guest-user = true
   roles.admin.groups = [ "flux" ]


RESOURCES
=========

.. include:: common/resources.rst


SEE ALSO
========

:man5:`flux-config`
