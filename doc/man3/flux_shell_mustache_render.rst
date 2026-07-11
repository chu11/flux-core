=============================
flux_shell_mustache_render(3)
=============================

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

   #include <flux/shell.h>

   char *flux_shell_mustache_render (flux_shell_t *shell,
                                     const char *fmt);

   char *flux_shell_rank_mustache_render (flux_shell_t *shell,
                                          int shell_rank,
                                          const char *fmt);

   char *flux_shell_task_mustache_render (flux_shell_t *shell,
                                          flux_shell_task_t *task,
                                          const char *fmt);

   bool flux_shell_mustache_is_per_rank (flux_shell_t *shell,
                                         const char *fmt);

DESCRIPTION
===========

:func:`flux_shell_mustache_render` expands mustache templates in :var:`fmt`
using the current shell context and returns an allocated string with the
result. The caller must free the result.

:func:`flux_shell_rank_mustache_render` is similar but renders the template
for an alternate shell rank specified by :var:`shell_rank`.

:func:`flux_shell_task_mustache_render` renders the template for a specific
task specified by :var:`task`.

These functions are useful for expanding job-specific templates in environment
variables, command arguments, or plugin configurations.

:func:`flux_shell_mustache_is_per_rank` returns ``true`` if :var:`fmt` renders
to a different result across shell ranks or tasks, i.e. it contains a per-rank
or per-task tag such as ``{{node.id}}``, ``{{node.name}}``, ``{{task.id}}``, or
``{{tmpdir}}``. This is useful for deciding whether a templated output path
yields a distinct file per shell.

RETURN VALUE
============

The render functions return an allocated string on success which the caller
must free, or NULL on failure with :var:`errno` set.

:func:`flux_shell_mustache_is_per_rank` returns ``true`` or ``false``, setting
:var:`errno` to :var:`EINVAL` and returning ``false`` on invalid arguments.


ERRORS
======

EINVAL
   :var:`shell`, :var:`fmt`, or :var:`task` is NULL.

ENOMEM
   Out of memory.


RESOURCES
=========

.. include:: common/resources.rst


SEE ALSO
========

:man7:`flux-shell-plugins`
