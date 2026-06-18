.. _kvs_gc:

=======================================================
Online Garbage Collection for KVS Content Backing Store
=======================================================

Problem
=======

The Flux content service is append-only. As the KVS evolves through commits,
old versions of the hash tree accumulate in the backing store. Offline GC uses
:man1:`flux-dump` to dump the current KVS snapshot and :man1:`flux-restore` to
recreate the backing store from scratch. This requires downtime and doesn't
help long-running instances.

**Goal**: Reclaim unreferenced blobs while the instance is running.

**Safety requirement**: GC must never delete a referenced blob. It is
acceptable (and expected) that some garbage survives multiple GC cycles - we
are conservative by design.

Related Documents
=================

- :doc:`kvs`
- :doc:`rfc:spec_10`
- :doc:`rfc:spec_11`

Approach
========

GC runs as a standalone **external tool**, :man1:`flux-gc`, in the same spirit
as :man1:`flux-dump` / :man1:`flux-restore`: a manual, instance-owner operation
that walks the KVS tree and interacts with the content store over RPC. The
tool is a stateless orchestrator; the backing store provides a few small,
content-agnostic primitives; and a monotonic **epoch** stamped on every stored
blob acts as the reclamation horizon that makes the whole thing safe against
a live, committing instance.

The horizon
-----------

The design rests on this idea: at the start of a run the tool
reads the current epoch and **freezes** it as the horizon :math:`H`. It then
marks every blob reachable from a marked root up to :math:`H`, and sweeps only
blobs whose epoch is :math:`< H`. Because the epoch only ever increases and
every store stamps the *current* epoch (:math:`\geq H`), any blob the tool
didn't mark is guaranteed to be either genuine garbage (epoch :math:`< H`) or
too recent to touch (epoch :math:`\geq H`). The horizon is what lets the tool
take its time, crash, or even run concurrently with another copy of itself
without endangering a referenced blob. :math:`H`, the mark phase, and the sweep
phase are developed in full below; keep this horizon in mind as the key
invariant.

This division of labor is deliberate:

- **The tool understands RFC 11.** Like :man1:`flux-dump`, it walks the tree by
  loading treeobjs and parsing them. It knows *structurally* whether a child
  blobref is a raw-data leaf (a ``valref``'s ``data[]``) or a treeobj (a
  ``dirref``/``dir`` entry), so it never loads raw-data blobs and never has to
  guess a blob's type. It shares the walker (``kvs_treewalk``) with
  :man1:`flux-dump` and :man1:`flux-fsck`, which keeps a bounded window of
  content loads in flight to hide the per-node round-trip latency that
  otherwise dominates a walk of a large KVS. GC prunes subtrees it has already
  walked this run (roots overlap heavily) and skips loading ``valref`` leaves
  entirely, since marking needs only their blobrefs.
- **The backing store stays content-agnostic.** content-sqlite gains only an
  epoch column and three primitives (mark / sweep / gc-info); it never parses
  treeobjs.
- **The epoch provides concurrency safety**, not the tool. The tool can run for
  minutes, crash, or be killed at any point — or be started a second time while
  a first run is still going — without risking a referenced blob, because
  correctness rests on server-side atomic operations and the horizon, not on
  the tool holding any lock or consistent snapshot.

Background: how content and the KVS fit together
=================================================

A few facts about how the KVS and content store already worked underpin the
design (the epoch column and the dedup-dirty behavior noted below are the parts
GC adds; everything else is pre-existing):

- **content-sqlite is a content-agnostic blob store.** It maps a hash to an
  opaque blob (the ``objects`` table, to which GC adds an ``epoch`` column) and
  knows nothing about treeobjs.

- **The KVS is the only component that understands the tree** and which *roots*
  are currently live. It maintains all roots (primary and private)
  and can iterate them.

- **Only the primary namespace is checkpointed.** ``checkpoint_put()`` in
  ``kvs.c`` checkpoints ``KVS_PRIMARY_NAMESPACE`` only. Private (per-job,
  guest-owned) namespaces are in-memory roots whose treeobjs and values live in
  the *same* shared content backing store but appear in **no** checkpoint. This
  omission is the central correctness hazard and is addressed below.

- **The checkpoint value is opaque JSON** stored verbatim by content-sqlite in
  the ``checkpt_v2`` table, which has a dense
  ``id INTEGER PRIMARY KEY AUTOINCREMENT``. That ``id`` serves as the epoch.

- **The content cache does not re-flush an already-clean blob.** A blob
  re-referenced by a new commit is deduplicated in the cache and never re-sent
  to the backing store, so it would never update its epoch. GC's dedup-dirty
  change marks such an entry dirty on re-store so it is re-flushed to
  content-sqlite (without rewriting the object) — a hard prerequisite for epoch
  refresh.

- **content-sqlite writes are atomic and serialized per blob.** It runs a
  single reactor thread, and SQLite gives statement-level atomicity with
  single-writer semantics, so row operations on a given blob never partially
  apply or interleave.

The epoch
=========

**Epoch** is a dense, monotonic counter maintained by content-sqlite: the
``checkpt_v2.id`` autoincrement value. Each checkpoint advances the epoch by
exactly one, with no gaps (SQLite ``AUTOINCREMENT`` never reuses ids, even
across prune). ``current_epoch = MAX(id)`` of ``checkpt_v2``, seeded at module
open and advanced in the ``checkpoint-put`` handler.

.. note::

   Using the checkpoint ``id`` (not the KVS commit ``sequence``, which jumps
   per-commit) keeps epochs dense and 64-bit, avoiding sparse gaps and 32-bit
   wraparound, and means content-sqlite assigns epochs itself without parsing
   the checkpoint value.

**Every stored blob is stamped with** ``current_epoch``:

.. code-block:: sql

   INSERT INTO objects (hash, size, object, epoch)
   VALUES (?, ?, ?, ?)            -- current_epoch
   ON CONFLICT(hash) DO UPDATE SET epoch = excluded.epoch;   -- object NOT rewritten

Combined with the dedup-dirty fix, a blob re-referenced by a new commit has its
epoch refreshed to ``current_epoch``. A blob's epoch thus records **when it was
last touched** — stored, re-stored via dedup, or marked reachable by GC.

Schema change
=============

.. code-block:: sql

   ALTER TABLE objects ADD COLUMN epoch INT DEFAULT 0;   -- when missing

Added to existing databases at open (detected via ``PRAGMA table_info``).
Pre-existing rows get ``epoch = 0``; the first GC's mark phase refreshes every
reachable blob before any sweep (see ordering invariant), so legacy blobs that
are reachable survive and the rest are collected. No secondary index on
``epoch`` is added: sweep stays cheap via a rowid cursor instead, keeping the
store/mark write path untaxed (see "Performance characteristics").

Enumerating private namespace roots
====================================

The mark phase is only safe if it marks from **every root the KVS could still
serve a read from**. Only the primary namespace is checkpointed; private
(per-job) namespaces are live roots in ``kvsroot_mgr`` whose blobs share the
backing store. A private namespace holds the user-writable portion of a job's
KVS (not system-owned data such as jobspec, R, or the primary eventlog, which
live in the primary namespace under the job's directory). Any private-namespace
data the job wrote once and has not since modified is stored once and never
re-stored, so if GC marked only from checkpoints it would age out and be swept
while the namespace is still alive — silent data loss.

**The tool snapshots the current private namespace roots directly from the KVS
at mark time** (``kvs.namespace-list`` to enumerate them, ``kvs.getroot`` for
each rootref), since they appear in no checkpoint.

GC therefore marks from the union of:

- **Stored checkpoints** — all retained primary checkpoints, from
  ``content-backing.checkpoint-get``, which protect everything reachable from
  recent primary roots (and support rollback/recovery to them).
- **Current private namespace roots** — a point-in-time snapshot from the KVS.
- **The live primary root** — read with ``kvs.getroot`` *after* the
  private-namespace snapshot. This protects data re-referenced into the primary
  tree but not yet checkpointed; see "The graft hazard" below for why this is
  required and why the ordering matters.

Marking the live primary root also ensures any primary data not yet
checkpointed is marked directly, rather than relying on epoch recency to
protect it — which is strictly safer.

This deliberately keeps the **checkpoint format unchanged** (primary only).
Persisting private namespaces across a restart is a separate concern owned by
the preserve-running-jobs effort (see "Relationship to preserving running
jobs"); GC reads roots from the running KVS and is agnostic to how they are
persisted.

Coverage:

- **Completed jobs** have had their namespace root grafted into the primary
  tree. Once a checkpoint captures the graft they are reachable from a
  checkpoint root; in the window before that checkpoint they are reachable only
  from the live primary root, which is why GC marks it (see "The graft
  hazard").
- **Running jobs** have a live private namespace root returned by the live-root
  query. A namespace created mid-cycle is covered by epoch recency (its blobs
  are stamped :math:`\geq H`) until the next cycle picks up its root.
- **The KVS symlink that points at a running job's namespace is not followed.**
  It is a target string with no blobref, so a tree walk does not (and must not)
  traverse into the namespace through it. This is exactly why GC enumerates
  roots explicitly instead of relying on tree reachability.

If GC is ever run with the KVS module not loaded, there are no live private
namespaces, so marking from the latest checkpoint alone is correct — mirroring
:option:`flux dump --checkpoint`.

The graft hazard
----------------

When a job completes, job-exec grafts its private namespace into the primary
tree with :man3:`flux_kvs_copy` — copying the namespace root into the job's
guest directory in the primary namespace — and then removes the namespace.
This creates a content-addressed *snapshot*: it reads the namespace root
treeobj and re-puts it at the destination with
:man3:`flux_kvs_txn_put_treeobj`, so the primary commit installs a single
dirref pointing at the existing namespace subtree. **The subtree blobs are
re-referenced but never re-stored**, so they retain their original epoch.
Neither epoch-refresh protection helps:

- the dedup-dirty / epoch-refresh path only fires for a blob whose *content* is
  presented to ``store_cache`` — the subtree a grafted dirref points at is
  never presented; and
- the :math:`\geq H` recency rule only covers blobs *stored* since the horizon
  — the grafted blobs were stored long ago, under the namespace.

This opens a window: after the graft commits and the namespace is removed, but
before the next primary checkpoint captures the graft, the subtree is reachable
from neither a checkpoint root nor a live private namespace root. If its blobs
predate the latest checkpoint — the common case for any job whose data is older
than one checkpoint interval — they have :math:`\text{epoch} < H` and would be
swept: silent loss of a completed job's KVS data. **Marking the live primary
root closes the window**, since once the namespace is gone the graft is always
present in the live primary root.

**Ordering invariant:** the live primary root must be read *after* the
private-namespace snapshot. To see why, name the four moments involved:

- :math:`N` — when the tool snapshots the private namespace roots.
- :math:`P` — when the tool reads the live primary root.
- :math:`c` — when a completing job grafts its namespace into the primary tree.
- :math:`d` — when that job then removes its namespace, necessarily after the
  graft, so :math:`c < d`.

A job's grafted data slips through only if it is missing from *both* root sets:

- absent from the private snapshot, i.e. the namespace was already gone when we
  took it: :math:`d \leq N`; **and**
- absent from the primary root we read, i.e. the graft had not yet happened
  when we read it: :math:`c \geq P`.

Chaining those with :math:`c < d` gives :math:`P \leq c < d \leq N`, which
requires :math:`P < N` — the primary root read *before* the private snapshot.
Reading the primary root last guarantees the opposite, :math:`N \leq P`, so the
gap cannot occur: every graft is caught by at least one of the two root sets.

The same protection generalizes to any re-reference-without-restore into the
primary, such as an operator's own :option:`flux kvs copy`.

Relationship to preserving running jobs
----------------------------------------

Today private namespaces are created and destroyed by job-exec per job; on
destroy, the namespace root is grafted into the primary tree, and while a job
runs a primary-tree symlink points at its private namespace. Running jobs are
not yet preserved across a restart, but will need to be.

GC is intentionally decoupled from that effort. GC's only requirement is that
**a running job's namespace root is enumerable as a live root whenever the
instance considers that job active.** It reads roots from the running KVS at
mark time and does not care how — or whether — they are persisted. However the
preserve-running-jobs design eventually reconstitutes running namespaces after
a restart (re-checkpointing them, persisting per-job state, re-creating them
from job-exec, etc.), they will reappear as live roots in ``kvsroot_mgr`` and
GC will mark them with no change to this plan. This is why the checkpoint
format is deliberately left untouched here: persisting private namespaces is
that effort's decision to make, not GC's.

One existing code path to revisit under that effort is ``checkpoint_running()``
in ``job-exec/checkpoint.c``, which records running jobs' rootrefs as an opaque
value in ``job-exec.kvs-namespaces`` that GC does not traverse, so
reconstituting a namespace from it must re-establish the live root before the
namespace stops being enumerable.

Backing store primitives
=========================

content-sqlite gains three RPCs, all content-agnostic and (like other backing
ops) rank-0-local / instance-owner only:

- **content-backing.mark** — given a batch of blobrefs and target epoch
  :math:`H`, set :math:`\text{epoch} = \max(\text{epoch}, H)`. Idempotent and
  monotonic.
- **content-backing.sweep** — delete a bounded batch of blobs with
  :math:`\text{epoch} < H`, resuming from a ``rowid`` cursor:
  ``SELECT rowid FROM objects WHERE epoch < H AND rowid > cursor AND rowid <=
  high_water ORDER BY rowid LIMIT delete_cap`` (stopping the scan after
  ``window`` rows even if fewer than ``delete_cap`` match), delete those rowids,
  and return the count deleted and a new ``cursor``. A server-side conditional
  delete, so the tool never enumerates the store. The tool passes the returned
  ``cursor`` back into the next call so each scan resumes where the last stopped,
  and loops until ``cursor`` reaches ``high_water`` (see below). Two caps bound
  the per-call reactor stall independently (see "Performance characteristics"):
  ``delete_cap`` bounds rows *deleted* — the dominant cost — and ``window``
  bounds rows *scanned*, so a call cannot stall long whether it lands in dense or
  sparse garbage. The new ``cursor`` is ``cursor + window`` when the scan
  exhausted the window, or the last deleted ``rowid`` when it stopped on
  ``delete_cap`` (resuming mid-window). No remaining count is returned, as that
  would require a full-table scan per call.
- **content-backing.gc-info** — return :math:`\text{current_epoch}` (which
  flux-gc freezes as the horizon :math:`H`) and the current **high-water
  ``rowid``** (``MAX(rowid)`` of ``objects``), which flux-gc uses as the sweep's
  fixed upper bound: rows stored after the run began get a higher ``rowid`` (and
  :math:`\text{epoch} \geq H`), so bounding the sweep at ``high_water`` makes it
  terminate deterministically without chasing newly stored blobs. Also returns,
  only when ``get_count`` is requested, :math:`\text{COUNT}(\text{epoch} < H)`
  for a given threshold. That count is an **unbounded full-table scan on the
  reactor thread**, so it is for the test suite and deliberate low-frequency
  inspection only — never a hot path. flux-gc omits ``get_count`` (it uses only
  ``current_epoch`` and ``high_water``); the count is also not a meaningful
  "reclaimable" estimate before the mark phase runs, since it includes reachable
  data whose epoch the mark would refresh.

The tool's algorithm
====================

.. code-block:: python

   H, high_water = gc-info()                       # freeze horizon + rowid bound

   roots = []
   for cp in content-backing.checkpoint-get():     # stored primary checkpoints
       roots += [cp.rootref]
   for ns in kvs.namespace-list() if ns.private:   # private namespace snapshot
       roots += [kvs.getroot(ns).rootref]
   roots += [kvs.getroot(primary).rootref]         # live primary root, read LAST

   # MARK: walk like flux-dump, but refresh epochs instead of emitting values
   visited = set()                                 # treeobj blobrefs walked this run
   for root in roots:
       walk(root):
           if blobref in visited: skip             # roots share large subtrees
           visited.add(blobref)
           mark([blobref], H)                      # batched mark RPCs
           if treeobj (dirref/dir/valref):
               load + parse
               valref.data[]  -> mark(leaves, H)   # leaves never loaded
               dir/dirref     -> recurse

   # SWEEP: only after mark fully completes (ordering invariant)
   cursor = 0
   while cursor < high_water:                                    # from gc-info
       r = sweep(H, high_water, cursor, delete_cap=D, window=W)  # scan (cursor, cursor+W]
       cursor = r.cursor                            # +window, or last deleted rowid

**Ordering invariant:** sweep for a cycle must not begin until that cycle's
mark has fully completed. The tool always re-marks from scratch before
sweeping, so a crashed previous run is harmless.

Why it is safe against a live instance
=======================================

Two protections cover every referenced blob, both enforced by **atomic
server-side operations** — the store-upsert and the conditional sweep-delete
each apply atomically and are serialized per blob, so they cannot interleave to
leave a blob missing. This rests on SQLite's single-writer atomicity, not on
content-sqlite being single-threaded:

1. **Reachable from a marked root** (a stored checkpoint, the live primary
   root, or a private namespace snapshot root) → the tool marks it to *H*.
2. **Recently stored / re-referenced** → content-sqlite stamped it
   :math:`\text{epoch} \geq H` at store time.

Sweep removes only :math:`\text{epoch} < H`. The races:

- **current_epoch only increases** (it is the checkpoint id), so every store
  during the run stamps :math:`\geq H`; new commits are never swept.
- **Uncheckpointed live data:** blobs committed since the latest checkpoint
  were stamped at :math:`\text{current_epoch} = H`, which :math:`< H` excludes;
  blobs committed in the gap before the latest checkpoint are reachable from
  its root (in the window) and get marked.
- **Dedup re-reference (the dangerous race):** a commit that re-references an
  old garbage blob :math:`B` forces a re-store (dedup-dirty fix), stamping
  :math:`B.\text{epoch} \geq H`. Even if a sweep batch deletes :math:`B` first,
  the re-store re-inserts it, and the dirty cache entry keeps readers from
  faulting an absent blob in the meantime. At quiescence :math:`B` exists.

The only blobs with :math:`\text{epoch} < H` are those last touched strictly
before the latest checkpoint **and** unreachable from any marked root — genuine
garbage. The horizon :math:`H` is what lets the tool take its time: marking may
run for minutes while commits and new checkpoints proceed, because nothing it
protects can drop below :math:`H`.

:math:`H` (the newest checkpoint epoch) is a policy choice, not a correctness
boundary: it is the most aggressive sweep that keeps every retained checkpoint
valid. Any lower threshold is equally safe and simply reclaims less, leaving a
recency margin of uncollected garbage. We default to :math:`H` because, having
marked all retained checkpoints and private namespace roots, everything below
:math:`H` is provably dead.

Why one horizon
===============

A single frozen :math:`H` serves as both the mark target and the sweep
threshold. A finer-grained variant — mark each blob with the epoch of the
newest checkpoint that references it, and sweep below the oldest retained
checkpoint's epoch — is equally safe and writes fewer epochs (a stable blob's
mark becomes a no-op), but it is deferred: the mark **walk**, not the epoch
writes, dominates GC cost today, and the single horizon is simpler and more
aggressive (it marks up to :math:`H`, so it can sweep the whole band below
:math:`H` immediately). Per-checkpoint epochs become worthwhile only alongside
the cached subtree traversal optimization (see Future work), which is the thing
they would key off of; the two are deferred together.

Crash safety
============

The tool holds no persistent state:

- ``mark`` is an idempotent epoch bump; a crashed partial mark just means the
  next run re-marks from scratch.
- ``sweep`` only ever removes blobs older than the window; a partial sweep left
  garbage, never live data.
- Kill ``-9`` at any point → no corruption, nothing to clean up. The
  :man1:`flux-dump` / :man1:`flux-restore` safety model.

Concurrent runs
===============

Two :man1:`flux-gc` processes running at once (e.g. cron launches a second
while a first is wedged) is **safe by the same per-run argument** — no locking
is required. Each run establishes, independently, the invariant already proved
above (see "Why it is safe against a live instance" and "The graft hazard"):

  A run's sweep deletes only blobs unreachable from *every* root the KVS could
  serve a read from, and no such blob can become reachable without first having
  its epoch lifted to :math:`\geq H` by a store, dedup re-store, or mark.

Run :math:`B`'s roots are exactly such live roots, so whatever run :math:`A`
sweeps is nothing :math:`B` relies on — and vice versa, with no ordering assumed
between the runs. Marks don't conflict either: ``mark`` only raises epochs
(:math:`\max`), and each mark/sweep is an atomic per-blob SQL operation
serialized by SQLite. So concurrency is a *waste* (two traversals), not a
hazard; the design prescribes no mechanism to prevent it.

Safety invariants
=================

1. **No false deletions.** Marking from all stored checkpoints, the live
   primary root, and all current private namespace roots, plus epoch-recency
   for recently touched blobs, guarantees referenced blobs are never swept. The
   mark-before-sweep ordering guarantees legacy/epoch-0 blobs are refreshed
   before any delete.
2. **Eventual reclamation.** Genuine garbage ages out of the window and is
   collected, possibly after several cycles. Conservative by design.
3. **Concurrent correctness.** Commits proceed during GC: new stores get
   ``current_epoch ≥ H``; dedup re-stores refresh the epoch; atomic per-blob
   store/sweep operations (serialized by SQLite) prevent interleaving.
4. **Crash safety.** The tool is stateless and restartable; epochs persist; a
   killed run leaves no corruption.

Performance characteristics
============================

Per GC cycle:

- Mark: O(distinct treeobjs loaded + reachable leaves marked). Treeobjs are
  loaded and parsed by the tool; raw-data blobs are marked without loading.

  A per-run **visited set** of already-walked treeobj blobrefs keeps the
  *distinct* qualifier honest. The check sits *before* the ``content.load``, so
  the first time the root blobref of a subtree is seen the whole subtree below
  it is walked, and every later encounter of that same blobref returns
  immediately — pruning the entire subtree, and its loads, not just one node.
  Because blobs are content-addressed this dedup is exact and free: an
  unchanged subtree has the same root blobref everywhere it appears, so one hit
  prunes it.

  This matters because the marked roots overlap heavily. Consecutive retained
  checkpoints differ only along recently-modified paths, the live primary root
  differs from the newest checkpoint only by commits since that checkpoint, and
  a completed job's data, once grafted into the primary tree, is referenced by
  an identical blobref from the live primary root and from every checkpoint
  taken since the graft. So the large, immutable mass of weeks-old job data is
  loaded **once** rather than once per root, and adding the live primary root
  to the mark set (see "The graft hazard") costs only the delta since the last
  checkpoint — the rest is already in the visited set. Memory is bounded by the
  number of distinct interior treeobjs in the live tree (walked once), not by
  the number of roots. The part that does not dedup is running-job private
  namespaces, which are disjoint subtrees until graft — the small active slice,
  not the bulk.
- Sweep: **one O(total blobs) pass total**, with a bounded reactor stall per
  call. There is no index on ``epoch``, so finding blobs with
  :math:`\text{epoch} < H` means scanning rows. Two independent bounds keep that
  cheap, and it is worth being precise about what each one does:

  *Total work — the* **rowid cursor**. ``objects`` is a rowid table (``hash`` is
  the declared primary key, but the implicit 64-bit ``rowid`` is the physical
  key, monotonically assigned on insert). Each ``sweep`` call returns the
  ``rowid`` it scanned up to, and the next call resumes at ``rowid > cursor``.
  Every row at or below the cursor is permanently settled: either it was deleted,
  or it has :math:`\text{epoch} \geq H` and — because epochs only ever rise —
  stays that way, so it never needs re-examining. The whole sweep is therefore a
  single ascending traversal of the table, O(total blobs), plus O(garbage) for
  the deletes — rather than the O(calls × total blobs) that restarting each call
  at the beginning of the table would cost (a quadratic blow-up).  The scan stops
  at the ``high_water`` ``rowid`` frozen by gc-info at the start of the run, so
  it never chases blobs stored after the run began (those have a higher ``rowid``
  and :math:`\text{epoch} \geq H`); the tool terminates when the cursor reaches
  ``high_water``.

  *Per-call stall — two caps.* The cursor alone does **not** bound a single
  call: a ``LIMIT`` on *matches* does not limit *rows scanned*, so under sparse
  garbage one call could scan a huge rowid span to accumulate its deletes. And
  the two per-row costs are very different — a delete rewrites index/page state
  and is roughly an order of magnitude or more costlier than examining a row —
  so a single knob cannot bound both. Each call therefore takes two caps:
  ``delete_cap`` bounds rows *deleted* (the dominant cost), and ``window`` bounds
  rows *scanned* (so a sparse span still makes progress instead of stalling to
  reach ``delete_cap`` matches). The call ends at whichever binds first, and the
  new cursor reflects it: ``cursor + window`` if the scan exhausted the window,
  or the last deleted ``rowid`` if it stopped on ``delete_cap`` (so the next call
  resumes mid-window). A reasonable default derives one from the other —
  ``window ≈ K · delete_cap`` with ``K`` the expected rows-scanned-per-delete —
  keeping the two stall contributions comparable; ``K`` only affects stall
  smoothness, never correctness or total work.

  The cursor and caps are purely optimizations: correctness still rests on the
  :math:`\text{epoch} < H` predicate, so rowid reuse after deletes is harmless
  (a reused-low rowid holds a freshly stored blob with :math:`\text{epoch} \geq
  H`, which the predicate excludes — it is simply left for a later cycle). An
  index on ``epoch`` would cut *total* selection to O(garbage) when garbage is
  sparse, but it would add write-path cost to every store and every mark (both
  write the ``epoch`` column), so it is deliberately omitted; a single O(total
  blobs) pass with a bounded per-call stall is cheap and leaves the write path
  untaxed.
- Space overhead: ~4 bytes/blob (one nullable INT epoch column).

Known Limitations
=================

**Stale roots on compute nodes.** Ranks > 0 cache their current KVS root and
update it via the eventually-consistent ``kvs.setroot`` event mechanism. A
compute node that is slow, partitioned, or stuck (e.g., in a memory reclaim
loop) may hold a stale root for an extended period. If enough new checkpoints
accumulate and GC prunes checkpoints beyond the retention window (default 5),
blobs referenced only by that stale root may be swept. When the compute node
recovers and attempts a lookup, it will fail with "blob not found."

Future work
===========

- **Cached subtree traversal.** This is *not* the per-run visited set GC
  already has (see "Performance characteristics"), which prunes subtrees shared
  between roots *within a single run* but starts empty every run and so reloads
  the whole reachable tree from scratch each cycle. The future work is the
  orthogonal, *across-run* optimization: because blobs are content-addressed, a
  treeobj with an unchanged hash has an unchanged, still-reachable subtree, so by
  persisting parent→child edges between runs (keyed by the dense epoch) stable
  branches can be bulk-marked without reloading at all — a large speedup for
  long-running instances with accumulated, immutable job data. Add only if the
  mark phase proves too slow. This pairs naturally with per-checkpoint mark
  epochs (see "Why one horizon"): an already-marked subtree is exactly one whose
  treeobj is stamped at or above its checkpoint's epoch.
  Crucially this cache must be driven by GC's own traversal state, **not** by a
  bare epoch test: a treeobj stored by a recent commit has
  :math:`\text{epoch} \geq H` while still pointing at unchanged children with
  :math:`\text{epoch} < H` (a commit does not propagate epochs to deduplicated
  children), so :math:`\text{epoch} \geq H` does not imply the subtree is
  marked.
- **Lightweight "touch epoch" backing op.** A dedup re-store currently
  re-transfers and re-compresses the full blob only to bump its epoch. A
  hash-only "touch" op would avoid that. Deferred: dedup re-stores are expected
  to be rare, so the redundant transfer is not a concern in practice.
- **Rate limiting.** The sweep's per-call delete and scan caps are fixed
  internal constants, and the interval between sweep calls is not metered. If GC
  impact ever becomes a concern, these could be exposed as tool options or the
  sweep spread out over time; for now, schedule GC during low-activity periods.
