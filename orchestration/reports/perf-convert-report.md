# Faction convert wall-clock optimization (men lane)

## For the verifier: what each commit claims

Eight commits, `4c00673..HEAD`, all in this worktree, none pushed. Sections 1-11
came from faction `men` against the shared state root, and their seven-faction
figures are **derived from measured parts and labelled as such**. Section 12 is
the first one whose seven-faction numbers were **measured** — five real
seven-faction batches — and where the derivations turned out to be optimistic,
it says so.

| Commit | Claim | Where |
|---|---|---|
| `4c00673` | Profiled one convert. Fixed a compiler-identity thundering herd (an *excluded* row cost 36 593 ms; now 52 ms), memoized the corpus parse and document view, single-flighted the shared lazy caches, hoisted the full-corpus closure. Men cold 844→683 s, in-batch warm 339→103 s. Plan parallelism measured a loss and left serial. | §1-§5 |
| `a5fc47c` | Durable plan-row cache. Warm men faction 103.3→28.8 s. | §6 |
| `164f5d0` | Dependency-identity precision: new `accounted` lane, `banner-member`/`builder` into the unit lane, plan key off the whole-package salt. Unrelated-module edit 209.8→44.5 s. Corrects two false premises in the brief — the media caches never keyed on doc compilers, and unit/structure/spellbook precision already worked. | §7 |
| `26727f0` | Object-level process pool (`--object-procs`), workers warm caches only, parent alone writes. Men cold 501→254 s. Hardened `FactionObjectCache.put` against `WinError 5`. | §8 |
| `1b6dde9` | Durable census cache, census lane at 20/186 modules. Parent pass 38.6→28.5 s. | §9 |
| `5cc06f4` | Aggregate short-circuit. **Second and subsequent unchanged runs: 28.5→13.2 s per faction.** | §10 |
| `29a3dc9` | Three verifier-demonstrated holes closed (vacuous artifact guard, unguarded `get()` in a shard, strandable `_CachePending`) plus a blind JSON-stability test. | §11 |
| `69e353b` | **Option C.** Workers produce the coverage rows, the parent verifies and assembles; digest-verified graph shipping; persistent queue-fed pool; largest-first scheduling with per-faction coverage emission. **Seven factions MEASURED, not derived: compiler-edit cold 522 s, repeat 69 s.** | §12 |
| `cced800` | Diagnosed the 420 s floor: uniform saturation, not a straggler tail; the same descriptor is compiled three times per unit. Analysis only. | §14 |
| `HEAD` | **The §14 cuts.** Descriptor memo, faction-discovery memo (4.3 s -> 0 per call), ledger streaming, parallel probe, cost-balanced sharding. **Measured: compiler-edit cold 522 -> 404 s, repeat 69 -> 9.9 s, parent serial 162 -> 16 s. Byte-identical to the serial oracle on retail men and dwarves. The 5-minute bar is still NOT met at 6.8 min** — §15.7 shows why §14's prediction was wrong and where the residual actually lives. | §15 |

Byte-identity was proved at every step and is restated in each section. The
standing caveat: seven-faction numbers are arithmetic, and true full cold (media
caches deleted, Blender reconversion) was never measured — see §10.4.


Every number below came from the shared state root
`C:\Users\Jonathan\Desktop\open-bfme\workspace\retail-work` on 2026-08-20, faction
`men` only, pinned interpreter
`...\workspace\retail-work\tools\python-3.12-env\Scripts\python.exe`, command
replicated exactly from `tools/rotwk_full_content.py`:

```
<python> tools\rotwk_faction_convert_batch.py --install F:\RotWK --game rotwk
         --state-root <state>
         --assets-root <state>\editions\rotwk\cache\effective-assets
         --faction men
```

Logs (all under `C:\Users\Jonathan\Desktop\open-bfme\workspace\logs\perf-convert\`):

| Log | What it is |
|---|---|
| `baseline-men.log` | HEAD, cold object cache |
| `before-warm-men.log` | HEAD, warm object cache (pristine `HEAD` tree in `%TEMP%\obf-base`) |
| `after-cold-men.log` | optimized, `--faction men --faction men` (cold pass then in-batch pass) |

**Confounds, stated up front.**
1. A second agent was profiling the publish path (`profile_publish.py`, two
   Python processes) against this machine for most of the window. Where a stage
   timing moved between two runs of the *same* code — the HEAD plan stage
   measured 167 s cold and 215 s warm — that is why.
2. I twice edited package sources while a run was in flight. Those runs were
   discarded, not measured; the runs above were each taken against frozen code.
   The lesson is real: `compiler_dependency_identity` reads package bytes
   *live*, so a mid-run edit silently re-keys the durable cache.

---

## 1. Profile: where the time actually went

Stage boundaries come from the progress sink every run writes.

| Stage | HEAD cold | HEAD warm |
|---|---|---|
| catalog load | 3 s | 3 s |
| census | 14 s | 15 s |
| document load | 1 s | ~0 s |
| **plan (`build_faction_import_plan`)** | **167 s** | **215 s** |
| convert loop | ~660 s | ~105 s |
| **faction total** | **844.2 s** | **338.7 s** |

None of the top sinks was asset work. Four findings:

### Sink A — compiler-identity thundering herd (the "36 s/object")

The smoking gun, from `baseline-men.log`:

```
done create-a-hero: CreateAHero (excluded, 36593ms)
```

`CreateAHero` is an **excluded** row: its whole code path in
`_convert_one_plan_object` is a three-key `dict.update`, reached only after the
per-object cache key is computed. All 36.5 s was cache-key computation.

`incremental_rebuild.compiler_dependency_identity(family)` memoized per family,
but checked the memo without holding a lock and computed outside one. Every
miss re-read the entire package source corpus (181 files, 13 MB), AST-walked
the dependency closure and sha256'd the selected modules. Families with no
declared lane — `create-a-hero`, `projectile`, `object-inheritance`,
`missing-object`, `retail-object-parser`, where `_family_lane` returns `None` —
take the `full-compiler-fallback` branch and hash **all 181 modules**. With 16
convert workers starting at once, up to 16 threads did that concurrently per
family, under the GIL.

Confirmation from the same class of row later in `before-warm-men.log`, once
the memo was warm:

```
done playable-unit: GondorTowerShieldGuard (converted, cache, 11ms)
done structure: GondorWorkshop (converted, cache, 10ms)
```

### Sink B — the plan stage is a second, serial, uncached full compile

`build_faction_import_plan` compiles a complete descriptor for every object
(`compile_playable_unit_descriptor` / `compile_playable_structure_descriptor` /
`compile_spellbook_descriptor`), serially, with no cache — 167 s for 61 Men
objects. **This is why a fully warm faction still cost 338.7 s**: the durable
object cache short-circuits the convert loop only. The previously reported
angmar warm run has the identical shape (201 s wall, `convertLoopMs` 32 s,
48/50 cache hits) — 169 s of it was plan.

### Sink C — the corpus is parsed 14 times per seven-faction batch

`prepare_playable_unit_compiler(documents)` parses the whole INI corpus and is
called **twice per faction** — once in `build_faction_import_plan`, again in
`build_faction_conversion`. Each returned `PlayableUnitCompilerInputs` also
carries lazy per-kind indexes (`flat_kind_cache`, `named_definition_cache`),
and each miss in `_flat_blocks_for_kind` / `_named_definition_values` scans or
cp1252-decodes *every document in the corpus*. A fresh instance rebuilds all of
that from cold, fourteen times.

### Sink D — per-object full-corpus closure hashing, and a repeated catalog sort

Any plan row without declared `sourceDocumentPaths` (every excluded and gap
row) sends `document_closure_identity` down the `full-corpus-fallback` branch,
rebuilding and hashing a row list over the whole corpus — once per object, with
an identical answer every time. Separately,
`faction_census._effective_entries` sorts the full ~50k-row catalog on every
call, several times per census.

---

## 2. Changes

All are "compute the same answer once instead of N times". None changes an
answer, weakens a check, or touches a hash pin.

1. **`incremental_rebuild.compiler_dependency_identity` — single-flight, one
   source snapshot.** The live computation runs under
   `_LIVE_COMPILER_IDENTITY_COMPUTE_LOCK` with a memo re-check inside; package
   sources are read once per process by the new `_live_module_sources()` into
   `_LIVE_SOURCE_MEMO`. The identity is the same function of the same bytes.
   `clear_compiler_identity_token_memo` clears this third memo layer too, so
   source-mutation tests still see fresh bytes. The computation body moved
   verbatim into `_compute_compiler_dependency_identity`.

2. **`playable_unit_compiler.prepare_playable_unit_compiler` — memoized** on the
   content identity of the document mapping, **and additionally requiring
   `hit.documents is documents`**. The compilers' fail-closed guard
   `prepared.documents is not documents` is deliberate and stays strict. This
   is not theoretical: the first version of this change ignored the guard and
   broke four existing tests
   (`test_conversion_converts_units_and_structures_and_is_deterministic`,
   `test_conversion_records_per_object_failures_and_continues`,
   `test_plan_expands_layered_command_object_missing_from_sealed_graph`,
   `test_conversion_cache_hit_skips_recompile`) with
   `prepared compiler inputs belong to a different document mapping`.

3. **`spellbook_import.spellbook_source_documents` — memoized** per
   (tree, catalog identity, sealed tree fingerprint). This is what makes (2) pay
   off across factions: every faction receives the *same mapping object*, so one
   parse and one set of lazy indexes serve the whole batch while the identity
   guard stays strict. A tree with no `.openbfme/manifest.json` has no cheap
   identity and is **not** memoized at all — the whole-tree inventory fallback
   would cost more than the parse it saves, so no new `rglob` appears anywhere.

4. **Shared lazy caches are single-flight.**
   `PlayableUnitCompilerInputs.cache_lock` became a `threading.Condition` (still
   a plain lock at every existing `with` site), and `_flat_blocks_for_kind`,
   `_named_definition_values` and `_weapon_damage_nuggets` now claim/publish
   their key through `_claim_cache_key` / `_publish_cache_key`. Each miss scans
   or decodes the entire corpus, so N workers racing one key used to cost N full
   scans. Two escapes keep it from ever being worse than before: a plain lock
   cannot wait (old behaviour), and a thread re-entering its own pending key
   recomputes rather than waiting on itself. A raising computation clears its
   pending slot and wakes waiters.

5. **Full-corpus closure hoisted; `_effective_entries` memoized.**
   `build_faction_conversion` computes the full-corpus
   `document_closure_identity` once and passes `full_corpus_closure`;
   `_convert_one_plan_object` uses it **only** when the row declares no source
   paths — a row with declared-but-*missing* paths still recomputes, because its
   `missing` list makes the payload genuinely different. `_effective_entries`
   memoizes on the catalog instance with the same `object.__setattr__` pattern
   `identity_sha256` already uses, degrading to no memo if the instance refuses
   attributes. No caller mutates the returned mapping.

6. **Plan loop refactored to be parallelizable, then left serial on the
   evidence.** The per-object body became `_plan_one(object_id) -> list[row]`
   driven by `pool.map` over the same sorted id list, so ordering (and the plan
   document) is unchanged. **Measured, threading it is a loss**: 16 workers took
   ~240 s against 167-215 s serial, because the stage is pure-Python and
   GIL-bound. `resolve_plan_worker_count` therefore defaults to **1**
   (`OPENBFME_FACTION_PLAN_JOBS` opts in), separate from the convert loop's
   `resolve_convert_worker_count` (`min(16, cpu)`, unchanged — this 24-CPU host
   already resolved to 16).

### Deliberately not done

- **No conversion-cache format change.** `CACHE_VERSION` is untouched and no
  existing entry changes meaning. Cache *keys* do move, because the compiler
  source bytes changed — that is the mechanism working as designed.
- **No durable plan-row cache.** See "what is left" below: it is the whole
  remaining prize, but it is a new durable trust surface and deserves its own
  lane rather than being bolted on at the end of this one.

---

## 3. Before / after

| Run | Code | Object cache | Faction wall | Convert loop | Cache hits |
|---|---|---|---|---|---|
| `baseline-men.log` | HEAD | cold | **844.2 s** | ~660 s | 0 |
| `before-warm-men.log` | HEAD | warm | **338.7 s** | ~105 s | 58 |
| `after-cold-men.log` pass 1 | optimized | cold | **682.9 s** | 438.6 s | 0 |
| `after-cold-men.log` pass 2 | optimized | warm, shared corpus | **103.3 s** | **1.8 s** | 59 |

Pass 2 is the same faction converted a second time in the same process. It is
the honest proxy for "a faction that is not first in the batch": it reuses the
shared parsed corpus and the warm lazy indexes, which is exactly what factions
2-7 of a seven-faction batch now do. Its plan stage took **77 s against pass 1's
220 s** (2.9x), and its object p50 was **21 ms** against a cold p50 of 92 653 ms.

Micro-result for the headline sink, both cold runs, same object:

| | HEAD | optimized |
|---|---|---|
| `CreateAHero` (excluded row) | **36 593 ms** | **52 ms** |

### Seven-faction projection

| Scenario | HEAD | optimized |
|---|---|---|
| Warm caches (no compiler change) | 7 x 338.7 = **~39.5 min** | 245 + 6 x 103.3 = **~14.4 min** |
| Cold caches (a real recook — compiler edits invalidate) | 7 x 844 = **~98 min** | 683 + 6 x 540 = **~65 min** |

**The single-digit-minute target was not met, and I am not going to claim it
was.** Warm seven-faction lands at roughly 14-15 minutes, a 2.7x improvement.

**What is left, with the prize sized.** In the optimized warm projection the
plan stage is 220 s (first faction) + 6 x 77 s = **682 s of the 865 s total**.
Everything else is already near zero (convert loop 1.8 s per warm faction). A
durable plan-row cache — keyed on the full-corpus documents fingerprint, graph
identity, policy fingerprint, compiler token, game and object id, all of which
`build_faction_conversion` already computes — would take that to a per-object
small-file read and put warm seven-faction convert at roughly **3-4 minutes**.
That is the single change that reaches the target, and it is the one I did not
make.

---

## 4. Byte-identity proof

`%TEMP%\men-before` (HEAD warm run output) vs `%TEMP%\men-after` (optimized run
output), both from the same shared state root, compared file-by-file:

```
ARTIFACT_FILES a=183 b=183
ONLY_A 0 []
ONLY_B 0 []
DIFFERING 0 []
PLAN_AGGREGATE_EQUAL True
COVERAGE_ROWS a=61 b=61 differing=61
COVERAGE_ROWS_DIFFERING_BEYOND_COMPILER_IDENTITY 0 []
IDENTICAL True
```

- **All 183 per-object artifacts — descriptors, pack recipes, runtimes,
  lifecycle evidence — are byte-identical.**
- `planAggregateSha256` is identical.
- All 61 coverage rows are identical once `incremental.compilerIdentity` and
  the `incremental.cacheKey` derived from it are set aside. Those two *must*
  differ: the compiler source bytes changed, and tracking that is precisely
  what they are for. `descriptorSha256`, `recipeSha256`, `runtimeSha256`,
  `status`, `family`, `category` and `reason` all match exactly.
- `converted=59 gaps=0 complete=True` in every run, before and after.

The coverage document's own `aggregateSha256` differs, because
`coverage_digest_payload` includes the `incremental` block; that is the same
compiler-identity delta, not a content delta.

### Attestation is not weakened

- `test_compiler_dependency_identity_still_tracks_source_bytes` asserts a
  tampered module body still yields a different identity.
- The source snapshot is a per-process read of the same bytes, cleared by
  `clear_compiler_identity_token_memo`; nothing is skipped and nothing is made
  conditional on anything but a digest.
- The `prepared.documents is documents` fail-closed guard is untouched, and the
  memo was narrowed to respect it.
- No hash pin was edited.

---

## 5. Tests

New: `importer/tests/test_faction_convert_perf.py` (11 tests), each a
correctness guard on a speed change:

- `test_prepared_compiler_inputs_are_shared_for_one_document_view` — memo hits
  only for the same mapping object; an equal-content copy gets its own inputs.
- `test_source_document_view_is_shared_across_factions`,
  `test_unsealed_tree_is_never_memoized`.
- `test_live_compiler_source_snapshot_is_reused_and_cleared`.
- `test_compiler_dependency_identity_computes_once_under_concurrency` — 8
  threads, exactly one computation, one identity.
- `test_compiler_dependency_identity_still_tracks_source_bytes`.
- `test_shared_kind_cache_computes_each_key_once_under_concurrency` — 8 threads,
  one scan per document rather than eight.
- `test_shared_kind_cache_failure_does_not_strand_waiters`.
- `test_hoisted_full_corpus_closure_matches_the_per_object_computation`.
- `test_parallel_plan_is_byte_identical_to_the_serial_plan`.
- `test_worker_count_resolution_is_explicit`, `test_plan_is_serial_by_default`.

**Failing-first:** the same file against a pristine `HEAD` checkout
(`git archive HEAD importer tools` extracted to `%TEMP%\obf-base`) fails at
collection with
`ImportError: cannot import name 'resolve_convert_worker_count'`, and its
behavioural assertions are false on HEAD by construction.

**Named-baseline delta**, worktree vs the same pristine HEAD checkout, same
interpreter and `BFME2_INSTALL`:

| Suite | HEAD | worktree |
|---|---|---|
| `test_faction_import` + `test_faction_object_cache` + `test_incremental_rebuild` | 61 passed | 61 passed |
| + `test_spellbook_import` + `test_faction_convert_perf` | n/a (new file) | 120 passed |
| `test_playable_unit_compiler` | 217 passed, 8 skipped, **1 failed** | 219 passed, 8 skipped, **1 failed** |

The single failure is
`test_horde_dispatch_graphs_cover_exact_effective_retail_corpora`, which is
**pre-existing and environmental** — it wants `workspace/retail-work/catalog/
bfme2.json` relative to the checkout root, which does not exist in a worktree
or in the `%TEMP%` HEAD copy. It fails identically on HEAD.

## 6. Durable plan-row cache (second lane, on top of 4c00673)

Section 3 named the plan stage as the entire remaining prize — 682 s of the
865 s optimized warm seven-faction projection — and said a durable plan-row
cache would reach the single-digit-minute target. It was authorized and built.

Logs: `plancache-cold-men.log`, `plancache-warm-men.log`,
`plancache-poisoned-men.log`.

### Design

New module `importer/openbfme_importer/faction_plan_cache.py`, storing one
entry per planned object under `<state>/cache/faction-plan-rows/<aa>/<key>/
plan-rows.json` (a sibling of the existing `faction-objects` conversion cache,
never inside it; `OPENBFME_SHARED_CACHE` is honoured the same way).
`PLAN_CACHE_VERSION = 1`, written with `write_json_atomic`.

A cached row is admitted only when the whole fail-closed identity chain still
matches. The **key** binds:

| Component | Source |
|---|---|
| full-corpus document closure identity | `document_closure_identity(documents, None)` |
| catalog identity | `catalog_identity_sha256` |
| effective-assets identity | `durable_non_ini_assets_fingerprint` |
| faction graph identity | sha256 of the canonical graph |
| structure policy roots + banner-carrier set | `policy_roots_fingerprint` |
| compiler identity token | `compiler_identity_token()` |
| game, object id | — |

The key uses the *full-corpus* document identity rather than a per-object
projection, because the plan stage decides an object's family before any source
closure is known — that is the only honest pre-compute key, and it is strictly
stricter than a per-object one. The **entry** then carries, and `get()`
re-verifies against freshly computed values:

- `compilerIdentity` — the per-family `compiler_dependency_identity(family)`
  (the fixed, single-flight version from section 2);
- `documentClosureSha256` — the per-object `document_closure_identity` over the
  exact `sourceDocumentPaths` the row declared;
- `rowsSha256` — a digest of the stored rows themselves, so a tampered or
  truncated payload is refused rather than served.

Every failure path — absent file, wrong schema, version drift, key mismatch,
bad digest, foreign compiler identity, changed source closure, unreadable JSON,
`OSError` — returns `None` and recomputes. **A cache fault can only cost time.**
Kill switch: `OPENBFME_NO_PLAN_CACHE` (and the existing
`OPENBFME_NO_OBJECT_CACHE` disables both). Resolution mirrors the object
cache's rule exactly: never fall back to the working directory, and never let
the pytest gate's ambient `OPENBFME_IMPORT_ROOT` point a synthetic conversion
at durable retail entries.

`build_faction_conversion` now computes the document hashes, the full-corpus
closure, the assets fingerprint and the graph identity *before* the plan and
passes them down. That is a move, not a new cost — the convert loop already
computed all four.

The whole cache is **256 KB for 61 objects**.

### Measured

All four runs below are the same frozen bytes, same state root, faction `men`.

| Run | Plan stage | Faction wall | Convert loop | Plan rows |
|---|---|---|---|---|
| cold plan cache, cold object cache | **203 s** | 687.7 s | 438 s | 0 cached, 61 recompiled |
| warm, fresh process (faction-1 position) | **23 s** | **62.9 s** | 2.2 s | 61 cached, 0 recompiled |
| warm, in-batch (faction-2 position) | ~5 s | **28.8 s** | 2.4 s | 61 cached, 0 recompiled |
| poisoned entry, warm | — | 40.9 s | — | 60 cached, 1 recompiled (0 absent, 1 refused on identity) |

Warm faction cost across the three lanes of this work:

| | warm men faction |
|---|---|
| HEAD | 338.7 s |
| after commit 4c00673 (in-batch) | 103.3 s |
| **with plan cache (in-batch)** | **28.8 s** |

An earlier pass of the identical design measured 50.9 s / 22.5 s for the same
two positions; the spread between that and 62.9 s / 28.8 s is the concurrent
publish-profiling load, which was heavier during the final pass. Both passes
are in the logs; the table reports the slower, final-bytes numbers.

### Seven-faction projection

| Scenario | HEAD | 4c00673 | with plan cache |
|---|---|---|---|
| Warm | ~39.5 min | ~14.4 min | **62.9 + 6 x 28.8 = 236 s ≈ 3.9 min** |

**The single-digit-minute target is met** — roughly 3.9 minutes at the load
present during measurement, 3.1 minutes on the lighter-load pass, against
~39.5 minutes on HEAD. That is a ~10x end-to-end improvement on a warm
seven-faction convert.

The cold (post-compiler-edit) case is unchanged at ~65 min: when converter
bytes move, every cache legitimately invalidates and the work is real.

### Byte-identity

Cold-plan-cache output vs warm-plan-cache output, same code, same state root:

```
ARTIFACT_FILES a=183 b=183
ONLY_A 0 []   ONLY_B 0 []   DIFFERING 0 []
COVERAGE_AGGREGATE_EQUAL True
PLAN_AGGREGATE_EQUAL True
COVERAGE_ROWS a=61 b=61 differing=0 []
IDENTICAL True
```

**Full identity, including the coverage `aggregateSha256` and every
`incremental.compilerIdentity`** — as expected, because compiler bytes did not
change between these two runs. `converted=59 gaps=0 complete=True` in both.

The poisoned run is identical too: refusing the tampered entry and recomputing
produced exactly the same 183 artifacts and the same coverage aggregate as the
cold run.

### Live poisoned-cache proof

One byte of a real cached row was corrupted in place
(`MenFortressCenterGeneric`, `descriptorSha256` truncated to `""` in
`cache/faction-plan-rows/00/00363d7c.../plan-rows.json`). The next production
run reported:

```
plan rows: 60 cached, 1 recompiled (0 absent, 1 refused on identity)
```

— refused on the `rowsSha256` gate, recomputed, and produced byte-identical
output. Note this log line is the *corrected* wording: the first version said
"0 compiled" for a run that had in fact recompiled one object, because the
counter it printed only tracked absent files. That was a reporting defect of
exactly the kind rule 5 exists to catch, so it was fixed and **every number in
this section was re-measured on the corrected bytes**.

### Tests

Six added to `importer/tests/test_faction_convert_perf.py` (18 total in file):

- `test_plan_rows_are_recomputed_without_a_cache_and_loaded_with_one` —
  failing-first shape: no cache compiles descriptors; warm cache compiles
  **none** and returns an identical plan (asserted on `aggregateSha256` against
  both the cold and the wholly uncached plan).
- `test_poisoned_plan_cache_row_is_refused_and_recomputed` — flips one byte of a
  cached `descriptorSha256`; the entry is refused and the true row recomputed.
- `test_plan_cache_refuses_a_foreign_compiler_identity` — a row whose stored
  compiler identity is not the current one is refused even with an honest
  self-digest.
- `test_plan_cache_version_drift_misses_cleanly`.
- `test_corrupt_plan_cache_entry_never_fails_the_plan` — unreadable JSON is a
  miss, not a failure.
- `test_plan_cache_is_disabled_by_environment`.

Suite delta: `test_faction_import` + `test_faction_object_cache` +
`test_incremental_rebuild` + `test_spellbook_import` +
`test_faction_convert_perf` = **126 passed**. Wider run including
`test_playable_unit_compiler` and `test_playable_structure_compiler`:
431 passed / 12 skipped / 1 failed, the one failure being the same pre-existing
environmental `test_horde_dispatch_graphs_cover_exact_effective_retail_corpora`
that fails identically on HEAD.

## 7. Cold convert: precision and the honest floor (third lane, on a5fc47c)

Owner requirement: cold convert under 5 minutes. Two fronts were asked for.
Front 1 landed and is measured below. Front 2 is measured and specified but
**not implemented** — see "what I did not build" at the end.

Logs: `precision2-repopulate-men.log`, `precision2-warm-before-men.log`,
`precision2-warm-after-unrelated-edit-men.log`.

### 7.1 A premise that turned out to be false, and one that was true

The brief assumed media/W3D cache entries are invalidated by document-compiler
edits. **They are not, and never were.** The W3D conversion key
(`pipeline.py:1257-1298`, built at `pipeline.py:7147-7165`) is exactly: the two
Blender adapter scripts' bytes, the blender-tree and OpenSAGE-plugin
attestations, the sha256 of each staged input file, and the logical conversion
parameters. No `compiler_dependency_identity`, no package salt. The media
(audio/texture) key (`pipeline.py:1323-1345`) is the same shape. A doc-compiler
edit has never forced a model reconversion, and
`test_media_conversion_key_is_independent_of_doc_compilers` now pins that.

The lane precision for `unit` / `structure` / `spellbook` also already existed
and already worked — `test_doc_compiler_edit_does_not_invalidate_unrelated_lanes`
and `test_structure_pack_edit_leaves_unit_and_spellbook_rows_hits` pass
unchanged on `a5fc47c`. I am not claiming credit for either.

What *was* false cold, and is now fixed:

| Defect | Effect | Fix |
|---|---|---|
| 8 of the 16 family strings a coverage row can hold had no lane (`_family_lane` returned `None`) and hashed all 185 modules | any package edit evicted every excluded/gap row | new `accounted` lane, manifest = common modules + `playable_unit_compiler.py` (78 modules) |
| `banner-member` and `builder` — real unit-lane families that compile unit descriptors — were missing from `_UNIT_FAMILIES` | both silently took the 185-module fallback | added to `_UNIT_FAMILIES` |
| **my own plan-row cache keyed on `compiler_identity_token()`, the whole-package salt** | any edit to any of ~165 non-lane modules threw away all 61 cached plan rows | new `plan_stage_identity()` = union of the four lane identities |
| `plan_stage_identity` asked for the lane *named* `"accounted"`, which `_family_lane` did not recognise, so it fell back to the 185-module digest | the fix above silently did nothing | `_family_lane` resolves a lane name to itself |

That last one is worth calling out: the first version of this work *looked*
precise and measured as a full plan miss anyway. It was caught only because I
ran the A/B instead of trusting the code. `test_live_lanes_all_resolve_precisely
_against_the_real_package` now asserts, against the real package, that every
lane resolves to `explicit-family-manifest` with fewer modules than the whole
corpus — the gate that would have caught it.

Live lane sizes after the fix: unit 80/185, structure 80/185, spellbook 83/185,
accounted 78/185.

### 7.2 Fail-closed rule stands

`_family_lane` still returns `None` — and therefore the whole-package identity —
for any family not explicitly named. Today that is `unknown`, the family used
for a coverage row built after an unexpected convert exception when the plan row
carried no family. `test_unknown_family_still_fails_closed_to_the_whole_package`
pins it. Over-invalidation costs time; under-invalidation ships a stale
artifact.

### 7.3 Measured: the everyday cold trigger

Comment-only edit to `living_world_ui.py`, a module no convert lane imports,
against a fully warm men faction:

| | plan rows | object cache | wall |
|---|---|---|---|
| warm, no edit | 61 cached, 0 recompiled | 59 hits | 53.0 s |
| **before this fix**, after the edit | **0 cached, 61 recompiled** | 59 hits | **209.8 s** |
| **after this fix**, after the edit | **61 cached, 0 recompiled** | 59 hits | **44.5 s** |

Byte-identity across the A/B pair: 183/183 artifacts identical, plan aggregate
identical, coverage `aggregateSha256` identical, 0 of 61 rows differing —
`IDENTICAL True`. Nothing about the emitted content moved; only the wasted work
went away.

### 7.4 Front 2: where a genuinely cold faction's time goes

I sampled the live convert process during a compiler-edit cold run:

```
plan stage    : cpu_s=23.6 wall_s=25 -> cores_busy=0.94 of 24
convert loop  : cpu_s=29.4 wall_s=30 -> cores_busy=0.98 of 24
```

**The whole faction convert is single-core-bound.** The convert loop runs 16
worker threads and delivers 0.98 cores, because the media caches survive
compiler edits, so there is no Blender subprocess work to overlap and what
remains is pure GIL-bound Python. 23 of 24 cores sit idle for the entire run.

That is the headroom, and it explains the arithmetic:

| Approach | 7-faction wall, compiler-edit cold | Under 5 min? |
|---|---|---|
| today, serial factions | 7 x ~690 s ≈ **80 min** | no |
| faction-level process pool (7 procs, 1 core each) | ≈ max per-faction ≈ **11.5 min** | no |
| object-level process pool (all 24 cores) | ~4830 core-s / ~20 usable cores ≈ **4 min** | yes |
| Front 1 precision, edit touching no convert lane | 7 x ~45 s ≈ **5.3 min** serial, ≈ **1 min** with any parallelism | yes |

**Faction-level parallelism — the shape the brief asked for — does not reach
5 minutes.** Seven single-core processes finish no faster than the slowest
faction. Only object-level parallelism, one pool over all ~420 objects of the
batch, converts the idle 23 cores into wall-clock. That is the change worth
making, and it is a different, larger change than the one specified.

### 7.5 Concurrent cache writers: verified, with one hazard

`write_json_atomic` (`util.py:12-23`) writes a unique `mkstemp` file, fsyncs,
then `os.replace`s. `test_atomic_write_never_exposes_a_torn_file_to_concurrent
_readers` hammers one key with 4 writers and 4 readers: **no reader ever saw a
torn, partial or mixed payload**, and last-write-wins leaves one writer's
complete payload. Same-key concurrent writes are safe to that extent.

**But on Windows the writer can fail.** The same test surfaces
`PermissionError: [WinError 5]` from `os.replace` when a reader holds the
destination open. Today that is mostly masked: `FactionObjectCache.put` holds a
process-local `_LOCK`, though `get` does not, so an in-process reader/writer
race is already possible and rare. **Under process-level parallelism there is no
shared lock and this becomes routine.** `FactionPlanRowCache.put` already
swallows `OSError` (a cache we cannot write is a cache we do without);
`FactionObjectCache.put` does not, so a sharing violation there would surface as
a per-object converter-gap. Hardening it the same way is a prerequisite for any
process-level parallelism. I did not make that change in this pass because it
touches a manifest module and would have invalidated the caches this section's
measurements depend on; it is a two-line change and it is named here rather than
left implicit.

### 7.6 The honest floor

- **Cold trigger that touches no convert lane** (living-world, HUD, cursor, map,
  publish, launcher — the large majority of importer edits): now a full cache
  hit. ~45 s per faction, ~5.3 min for seven serially and about a minute with
  any parallelism. **Meets the bar.**
- **Cold trigger that touches one lane** (e.g. only
  `playable_structure_pack_compiler.py`): only that lane's rows recompute — 30
  of 61 men objects. Unit and spellbook rows stay hits. This already worked
  before my change; precision keeps it that way.
- **Cold trigger that touches unit *and* structure** — which is exactly the
  recook named in the original brief (`playable_unit_compiler.py` +
  `playable_structure_pack_compiler.py`) — legitimately invalidates 56 of 61
  men objects. Precision cannot help: those rows really do depend on those
  bytes. At ~690 s per faction and single-core execution, seven factions take
  ~80 min serially and ~11.5 min with faction-level parallelism. **It reaches
  under 5 minutes only with object-level process parallelism (~4 min by the
  arithmetic above), which is not built.**
- **True full cold** (conversion caches deleted, every model reconverted through
  Blender): **not measured, and I declined to measure it.** Doing so means
  deleting the shared media cache, which would cost hours and would sabotage the
  publish-profiling agent working from the same state root. The one data point
  I have is the first baseline cold run — convert loop ~660 s with media cold
  versus ~440 s with media warm — so media adds roughly 220 s per faction of
  Blender subprocess work, which does parallelize across cores. A true-cold
  seven-faction run is therefore certainly well beyond 5 minutes on this
  machine; I will not put a number on it without measuring it.

### 7.7 What I did not build

Front 2 asked for process-level parallel faction converts. I did not implement
it, for three reasons I would rather state than bury:

1. By my own measurement it does not meet the requirement — faction-level
   parallelism tops out around 11.5 min for the worst-case trigger. The change
   that meets the bar is an object-level pool.
2. It needs `FactionObjectCache.put` hardened against `WinError 5` first
   (7.5), plus a ledger/artifact-writer merge across processes.
3. I could not have measured it honestly: the lane brief restricts me to `men`,
   so a seven-faction concurrency number was not available to me, and the
   machine was carrying another agent's profiling load throughout.

### 7.8 Tests added in this lane

Twelve, all in `importer/tests/test_faction_convert_perf.py`:
`test_doc_compiler_edit_does_not_invalidate_unrelated_lanes`,
`test_structure_pack_edit_leaves_unit_and_spellbook_rows_hits`,
`test_unrelated_module_edit_invalidates_no_convert_lane`,
`test_accounted_families_have_a_precise_lane_not_the_whole_package`,
`test_banner_member_and_builder_use_the_unit_lane`,
`test_unknown_family_still_fails_closed_to_the_whole_package`,
`test_blender_adapter_edit_invalidates_every_lane`,
`test_plan_stage_identity_is_lane_union_not_whole_package`,
`test_live_lanes_all_resolve_precisely_against_the_real_package`,
`test_plan_identity_ignores_a_module_outside_every_lane`,
`test_atomic_write_never_exposes_a_torn_file_to_concurrent_readers`,
`test_media_conversion_key_is_independent_of_doc_compilers`.

**Failing-first**, run against a pristine `a5fc47c` checkout in `%TEMP%`, four
fail and 23 pass — precisely the four new behaviours:
`test_unrelated_module_edit_invalidates_no_convert_lane`,
`test_accounted_families_have_a_precise_lane_not_the_whole_package`,
`test_banner_member_and_builder_use_the_unit_lane`,
`test_plan_stage_identity_is_lane_union_not_whole_package`. The other eight
already passed, which is the evidence for "this part already worked" in 7.1.

Suite: 443 passed / 12 skipped / 1 failed across
`test_faction_convert_perf`, `test_incremental_rebuild`, `test_faction_import`,
`test_faction_object_cache`, `test_spellbook_import`,
`test_playable_unit_compiler`, `test_playable_structure_compiler` — the one
failure being the same pre-existing environmental
`test_horde_dispatch_graphs_cover_exact_effective_retail_corpora`.

## 8. Object-level process pool (fourth lane, on 164f5d0)

Logs: `pool-serial-men.log`, `pool-parallel2-men.log`, and per-shard logs under
`<state>/reports/warm-shards/`. The publish agent was finished; the machine was
otherwise idle for these runs, which is why the serial cold men baseline here
(501 s) is faster than the loaded-machine figures in §6 (688 s).

### 8.1 Prerequisite

`FactionObjectCache.put` now swallows `OSError`, matching
`FactionPlanRowCache.put`. On Windows `os.replace` raises
`PermissionError (WinError 5)` when a concurrent reader holds the destination —
routine once workers share a cache root. Failing to store an entry costs a
recompute next run; it must never surface as a converter gap.
`test_object_cache_put_survives_a_refused_atomic_write` pins it.

### 8.2 Design: workers warm caches, the parent alone writes

`--object-procs N` runs a warm-up phase before the ordinary serial pass. Each
worker process is the same script in `--warm-shard i/N` mode: it converts only
its shard and writes **only** the durable plan-row and object caches — no
coverage, no ledger, no artifacts. The parent then runs its normal serial pass
and finds the work done.

This is deliberately not a "workers return rows" design. Because the parent
recomputes and assembles exactly as it always did, byte-identity with a serial
run is **structural** rather than something I have to defend row by row, row
ordering stays sorted by object id, and a crashed or slow worker costs time
only — the parent recomputes whatever the shard missed and logs
`WARM_SHARD_FAILED`.

Sharding is a stable digest of the folded object id modulo N, so a worker never
needs the faction's object list up front and the split is reproducible.
Workers are pooled across *all* requested factions — one process per shard
index handling that shard of every faction — so the ~35 s catalog and corpus
load is paid once per process, not once per faction per process. Item 4 of the
brief warned about exactly this and the first implementation got it wrong
(one process per faction-shard); the shard logs are what exposed it.

### 8.3 Measured, men, both runs fully cold in isolated cache roots

| | wall | detail |
|---|---|---|
| serial (`--object-procs` unset) | **501.2 s** | plan 182 s, 0 cache hits |
| pooled, `--object-procs 12` | **254.2 s** | warm pool 215.6 s + parent pass 38.6 s, 61/61 plan rows and 59 objects then cached |

**1.97x on a single faction.** Byte-identity, serial vs pooled:

```
ARTIFACT_FILES a=183 b=183   ONLY_A 0   ONLY_B 0   DIFFERING 0
COVERAGE_AGGREGATE_EQUAL True
PLAN_AGGREGATE_EQUAL True
COVERAGE_ROWS a=61 b=61 differing=0 []
IDENTICAL True
```

The per-shard logs give the cost structure directly:

```
shard  5/12: objects=1 wall=78.5 s      <- essentially all fixed cost
shard  0/12: objects=8 wall=206.9 s
shard  7/12: objects=8 wall=211.4 s     <- sets the pool wall (215.6 s)
```

**Fixed startup is ~75 s per worker** (catalog + INI corpus + prepared index +
census), and 61 objects over 12 shards is lumpy (1 to 8 objects). One faction is
simply too small to parallelise 12 ways: half the pool wall is startup.

### 8.4 Seven factions: derived, not measured

I could not measure the seven-faction case. The lane brief restricts me to
`men`, and a serial seven-faction cold baseline is ~58 minutes of wall clock
(7 x 501 s) which I did not have left. So the following is arithmetic from
measured parts, and is labelled as such:

- per-worker fixed cost, paid once now that a process spans all factions:
  ~35 s corpus + 7 x ~17 s census = **~154 s**
- convertible work: 7 x ~456 s = **~3192 core-seconds**
- parent serial pass over warm caches: 7 x ~39 s = **~273 s**

| N | pool wall | + parent | total |
|---|---|---|---|
| 12 | 154 + 266 = 420 s | 273 s | **~11.6 min** |
| 16 | 154 + 200 = 354 s | 273 s | **~10.5 min** |
| 20 | 154 + 160 = 314 s | 273 s | **~9.8 min** |

Against ~58 min serial that is roughly **5-6x**, and **it does not reach the
owner's 5-minute bar.** Recommended `N` is **16** on this 24-core host: past
that the per-worker fixed cost stops being amortised by the shrinking work
slice, and 420-odd objects shard evenly enough at 16.

### 8.5 What now blocks 5 minutes, with numbers

Two serial floors remain, and neither is the object work any more:

1. **The parent's serial pass, ~273 s.** It re-runs census (~17 s) and
   plan-row cache verification (~20 s) per faction even though every row is a
   hit. Removing it means the workers returning rows and the parent assembling
   them — the design I deliberately avoided for the identity guarantee. Doing it
   safely needs the graph shipped to workers as bytes (JSON round-tripping the
   faction graph would change tuples to lists and move the identities that key
   both caches).
2. **Per-worker census, ~154 s of the pool wall.** Every worker re-runs census
   for all seven factions. Caching the census per (catalog identity, faction)
   on disk the way plan rows are cached would cut this to near zero and is a
   smaller, safer change than (1).

Fix (2) alone: pool wall drops to ~35 + 200 = 235 s at N=16, total ~8.5 min.
Fix (1) and (2): ~4 min, under the bar. **(2) is the next thing to do, and (1)
is the one that actually crosses the line.**

### 8.6 Not measured, and why

- **The owner's exact scenario** (comment edit to `playable_unit_compiler.py`
  and `playable_structure_pack_compiler.py`, all seven factions, serial vs
  pooled) — needs ~58 min serial plus ~12 min pooled plus a repopulate. Not run.
  The men numbers above are a superset of that shape: both runs here were fully
  cold, which recomputes *more* than a unit+structure edit would.
- **The warm no-edit seven-faction regression check.** For one faction the pool
  is skipped entirely when `--object-procs` is unset (default), so today's
  behaviour is bit-for-bit unchanged without the flag; with the flag on a warm
  tree the workers would find every row cached and cost only their startup.
  I did not measure that, so I am not claiming a number for it.
- Cross-faction asset sharing (does faction 2 hit entries faction 1 wrote) was
  not measured — single-faction constraint.

### 8.7 Tests

Six added: `test_object_shards_partition_every_id_exactly_once` (1/2/3/8/20-way
splits, every id owned by exactly one shard),
`test_object_shard_assignment_is_stable_and_case_insensitive`,
`test_object_selector_restricts_the_plan_without_changing_rows` (three shards
reassemble into exactly the whole plan, row for row),
`test_plan_row_order_is_by_object_id_not_completion_order`,
`test_object_cache_put_survives_a_refused_atomic_write`, plus the batch-module
loader. Failing-first: all six fail on `164f5d0` — `shard_selector`,
`object_selector` and the hardened `put` do not exist there.

Suite: 96 passed across `test_faction_convert_perf`, `test_faction_import`,
`test_faction_object_cache`, `test_incremental_rebuild`.

## 9. Durable census cache (fifth lane, on 26727f0)

Logs: `census-serial-men.log`, `census-pooled12-men.log`.

### 9.1 Storage format: plain JSON, and the measurement that decided it

The brief asked whether the census graph needs canonicalization or a
digest-verified pickle. **Neither: it is already JSON-stable, measured.** On the
real Men census (1,132,897 bytes):

```
json_round_trip_equal: True
identity_stable:       True          # re-serialises to identical bytes
non-json-stable nodes: 0             # recursive scan: no tuples, sets, non-str keys
```

So the cache stores plain JSON. This mattered more than it looks: the graph's
canonical digest is `graph_identity_sha256`, which keys **both** the plan-row
and object caches. Had the graph contained a tuple, a round trip would have
turned it into a list and silently moved every downstream key. The stored
envelope therefore carries `graphSha256`, and `get()` re-derives it from the
loaded object and refuses any mismatch — so if the graph ever stops being
JSON-stable, the result is a clean miss, not a corrupted key.
`test_census_graph_is_json_stable` pins the property.

### 9.2 Identity chain and the census lane

Key: faction, game, catalog identity, effective-assets fingerprint, a policy
fingerprint over the template's implicit roots / source-null images / source-null
command sets / music roots / template / side, and
`compiler_dependency_identity("census")`.

Census got its own lane: **20 of 186 modules**, versus the 78-83 of the payload
lanes. It discovers what retail authors and compiles nothing, so every payload
compiler is in its exclusion set — `test_census_lane_is_precise_and_excludes
_every_payload_compiler` asserts `playable_unit_compiler.py`,
`playable_structure_compiler.py`, `playable_structure_pack_compiler.py`,
`spellbook_compiler.py`, `retail_visual_closure.py` and `w3d_index.py` are all
absent from it. A structure-compiler edit no longer re-censuses anything.

Fail-closed rule unchanged: a lane that cannot resolve its closure falls back to
the whole-package digest.

### 9.3 Measured, men, both fully cold in isolated cache roots

| | wall | parent pass | pool |
|---|---|---|---|
| serial | **498.0 s** | — | — |
| `--object-procs 12` | **240.8 s** | **28.5 s** | 212.3 s |

**2.07x.** The census cache's visible effect is in the parent pass:
**38.6 s -> 28.5 s** (`census men: cached`). It barely moves the pool wall for a
single faction, because all 12 workers start simultaneously, all miss, and all
compute census concurrently — a cold cache cannot dedupe a simultaneous burst.

Byte-identity, serial vs pooled, both with the census cache:

```
ARTIFACT_FILES a=183 b=183   ONLY_A 0   ONLY_B 0   DIFFERING 0
COVERAGE_AGGREGATE_EQUAL True   PLAN_AGGREGATE_EQUAL True
COVERAGE_ROWS a=61 b=61 differing=0 []
IDENTICAL True
```

Against the pre-census-cache serial run from 26727f0: all 183 artifacts
identical and 0 rows differing beyond `incremental.compilerIdentity` and its
derived `cacheKey`, which must move because the importer's bytes changed.

### 9.4 Seven factions: updated derivation — still NOT under 5 minutes

Derived, not measured (men-only lane constraint; a serial seven-faction cold
baseline is ~58 min I did not have budget for):

- parent serial pass: 7 x 28.5 s = **~200 s** (was ~273 s)
- per-worker fixed: ~35 s corpus + census. Each worker walks all seven factions
  sequentially, so after the first faction it increasingly hits entries other
  workers wrote; the honest range is ~11 s (all hits) to ~77 s (all miss). Call
  it **~40 s**, and note the uncertainty rather than pick the flattering end.
- convertible work: **~3192 core-seconds**

| N | pool wall | + parent | total |
|---|---|---|---|
| 12 | 35 + 40 + 266 = 341 s | 200 s | **~9.0 min** |
| 16 | 35 + 40 + 200 = 275 s | 200 s | **~7.9 min** |
| 20 | 35 + 40 + 160 = 235 s | 200 s | **~7.3 min** |

Against ~58 min serial that is **~7-8x**, improved from ~9.8-11.6 min at
26727f0. **The 5-minute bar is not met**, so per item 4 I did not stop, and §9.5
is the design section item 3 asked for.

### 9.5 Design (NOT implemented): getting the parent pass near-O(hits)

The parent pass is now the single largest remaining cost, ~200 s of the ~440 s
total at N=16. It re-runs, per faction, on rows that are all cache hits: census
(now cached, ~1 s), document load, `prepare_playable_unit_compiler`, the
plan-row cache verification loop (~20 s), and the convert loop (~2 s). Three
options, with the trust trade of each stated plainly.

**Option A — aggregate short-circuit (recommended).** Store, per faction, one
envelope holding the finished coverage document plus the aggregate digests it
was built from: `planAggregateSha256`, the coverage `aggregateSha256`, the
graph digest, the full-corpus document closure digest, the assets fingerprint,
the catalog identity and the four lane identities. The parent computes only
those aggregates — all of which it already computes today, cheaply, before the
per-object loops — and if every one matches it emits the stored coverage
document and skips both loops entirely. On mismatch it walks rows exactly as
now.
*Trust trade:* the parent stops re-deriving per-row identities and trusts a
per-faction aggregate instead. That is weaker than today, but not by much: the
aggregate digest is a function of every row, so any row change moves it. The
real exposure is a stored coverage document that was never actually produced by
those inputs — which the digest-of-content check catches — and second-preimage
strength, which sha256 gives. Cost: ~2 s per faction instead of ~28 s.
*Estimated total at N=16: ~5.0 min.* Borderline; combined with a modestly higher
N it clears the bar.

**Option B — parent loads census from the new cache and keeps recomputing
everything else.** Already done in this commit; it is what took 38.6 s to
28.5 s. No further trust trade, and no further headroom — the remaining 28.5 s
is corpus load plus the plan-verification loop.

**Option C — workers return rows, parent assembles.** The parent stops
recomputing entirely; workers ship coverage rows and artifacts back and the
parent sorts by object id and writes. *Trust trade: this is the big one.* It
gives up the structural byte-identity guarantee that §8 was built around — the
parent would no longer be the thing that produced the content, only the thing
that ordered it. It also requires shipping the faction graph to workers as
**bytes**, not as re-parsed JSON, because the graph digest keys both caches;
and it needs the ledger merged across processes. Fastest (~4 min) and the only
option that removes the parent pass outright, but it converts a structural
guarantee into a test-enforced one.

My recommendation is **A**, then raise N. It keeps the parent the producer of
record, needs no IPC of content, and the digests it checks are ones the parent
already computes.

### 9.6 Tests

Eight added: census round-trip and per-key-field invalidation (every one of the
six key fields moves the key and forces a rebuild), poisoned-graph refusal,
corrupt-JSON fail-open, version drift, env kill switch, lane precision, and
JSON-stability.

Plus `test_convert_faction_import_census_key_material_resolves`, which exists
because of a defect this lane produced: the first wiring referenced
`durable_effective_assets_fingerprint`, which `faction_import` does not import.
Every unit test passed — they drove `faction_census_cache` directly — and only a
full convert run caught the `NameError`, after ~8 s and an exit 3. That test now
drives the real call site so the gap cannot recur silently.

Suite: 151 passed across `test_faction_convert_perf`, `test_faction_import`,
`test_faction_object_cache`, `test_incremental_rebuild`, `test_spellbook_import`;
367 passed / 8 skipped / 1 failed with `test_playable_unit_compiler` added, the
one failure being the pre-existing environmental
`test_horde_dispatch_graphs_cover_exact_effective_retail_corpora`.

## 10. Aggregate short-circuit — Option A, implemented (sixth lane, on 1b6dde9)

Logs: `sc-serial-cold-men.log`, `sc-pooled-cold-men.log`, `sc-secondrun-men.log`.

### 10.1 What keys it — and a correction to the brief

The brief listed the plan aggregate and the coverage aggregate among the key
components. **They cannot be**: both are *outputs* of exactly the work the
short-circuit skips, so using them as key material would require doing that work
first. They are instead stored and re-verified for integrity — the loaded
document must reproduce its own recorded `coverageSha256`, its own
`planAggregateSha256` and its own `aggregateSha256`.

The key is every *input* to the plan and convert stages:

| Component | Source |
|---|---|
| faction, game | resolved spec |
| `catalogIdentitySha256` | `catalog.identity_sha256()` |
| `effectiveAssetsFp` | `durable_effective_assets_fingerprint` (whole-tree manifest aggregate) |
| `graphSha256` | canonical digest of the census graph |
| `policyFp` | template roots, source-null images and command sets, music roots, template, side |
| `laneIdentities` | all five lanes' `compiler_dependency_identity` |

The full-corpus document closure is deliberately absent: `documents` is a pure
function of (effective tree, catalog), so the assets fingerprint and catalog
identity already cover it, and adding a second corpus hash would have cost the
cold path ~3 s per faction for nothing.

Two things beyond the key are verified on load, because skipping the loops must
not paper over damage:

- **the stored coverage document against its own digest** — a tampered document
  is refused;
- **a digest manifest of every per-object artifact file** — a deleted or edited
  artifact refuses the entry, so the full walk runs and rewrites it.

Every refusal appends a one-line reason naming what moved
(`input moved: graphSha256`, `artifact tree does not match`,
`cache format version drift`, …) and the parent logs the latest one. A
short-circuit that quietly stops firing is visible, not merely slow.

### 10.2 The trust trade, and why it is bounded

The parent stops re-deriving per-row identities and trusts a per-faction
aggregate. That is acceptable **only because every failure path is the full
per-row walk** — the ordinary code path, and the one that produced the stored
document. There is no third behaviour: hit, or walk.

`test_poisoning_any_component_falls_through_to_the_full_walk` moves each of the
six components on its own and asserts a refusal for each;
`test_short_circuit_refuses_a_tampered_coverage_document`,
`test_short_circuit_refuses_a_deleted_or_edited_artifact` (both edited and
deleted) and `test_short_circuit_version_drift_and_corruption_miss_cleanly`
cover the rest. `OPENBFME_NO_COVERAGE_SHORTCIRCUIT` disables it outright.

### 10.3 Measured, men

| Run | wall | note |
|---|---|---|
| serial, cold | **469.5 s** | `short-circuit miss: no entry for this input identity` |
| pooled `--object-procs 12`, cold | **240.4 s** | pool 211.5 s + parent 28.9 s |
| **second run, unchanged inputs** | **13.2 s** | `coverage short-circuit: men reused` |

The short-circuit does **not** help a first cold run — by definition it misses,
and the shard workers cannot write it because their coverage is partial. It
halves the *repeat* cost: parent per faction 28.9 s → 13.2 s.

Byte-identity, serial full walk vs short-circuited:

```
ARTIFACT_FILES a=183 b=183   ONLY_A 0   ONLY_B 0   DIFFERING 0
COVERAGE_AGGREGATE_EQUAL True   PLAN_AGGREGATE_EQUAL True
COVERAGE_ROWS a=61 b=61 differing=0 []
IDENTICAL True
```

### 10.4 Seven factions: derived

First cold run after a compiler edit (short-circuit misses; pool 35 s corpus +
~40 s census + 3192 core-s of work; parent 7 x 28.9 = 202 s):

| N | pool | + parent | total |
|---|---|---|---|
| 12 | 341 s | 202 s | **~9.1 min** |
| 16 | 275 s | 202 s | **~7.9 min** |
| 20 | 235 s | 202 s | **~7.3 min** |

**The 5-minute bar is still not met on a first cold run.** I am not going to
present the repeat number as if it were the cold number.

**Second and every subsequent unchanged run: 7 x 13.2 s = ~92 s, about 1.5
minutes.** That is the figure the owner lives with day to day — re-running the
convert after an unrelated edit, a publish retry, a gate, an aborted cook. It is
comfortably inside the bar. The expensive case is narrowly: the first run after
an edit that touches the unit or structure lane.

Full picture for a seven-faction convert, from the branch start:

| Scenario | HEAD (4c00673 parent) | now |
|---|---|---|
| repeat run, nothing changed | ~39.5 min | **~1.5 min** |
| edit touching no convert lane | ~39.5 min | ~1.5 min |
| first run after unit+structure edit, pooled N=16 | ~98 min | ~7.9 min |

### 10.5 Option C, not implemented — the ~4 min endgame

Workers return coverage rows and the parent only sorts and writes. It removes
the parent pass outright and is the fastest option (~4 min cold, seven
factions). **Trust cost, for a future owner decision:** the parent stops being
the producer of record and becomes only the thing that orders results, so the
structural byte-identity argument this whole branch rests on becomes a
test-enforced one instead. It also needs the faction graph shipped to workers as
bytes rather than re-parsed JSON — the graph digest keys both the plan and object
caches, so a round trip that changed one type would move every key — and a
ledger merged across processes. Not started, deliberately.

### 10.6 Tests

Seven added: full-match reuse, per-component poisoning (all six), the named
refusal reason, tampered coverage, deleted **and** edited artifact, version
drift, corrupt JSON, env kill switch.

One defect this lane produced and its guard: the short-circuit initially built
its key material unconditionally, including when no cache exists, so
`test_conversion_admits_rotwk_data_driven_catalog` — which passes a `Mock`
catalog whose `identity_sha256()` is not serialisable — failed. Key material is
now built only when a cache exists, and an unserialisable identity falls through
to the full walk rather than raising.

Suite: 375 passed / 8 skipped / 1 failed across `test_faction_convert_perf`,
`test_faction_import`, `test_faction_object_cache`, `test_incremental_rebuild`,
`test_spellbook_import`, `test_playable_unit_compiler` — the one failure being
the pre-existing environmental
`test_horde_dispatch_graphs_cover_exact_effective_retail_corpora`, which fails
identically on HEAD.

## 11. Verifier findings and fixes (seventh lane, on 5cc06f4)

A fresh-context verifier demonstrated three real holes live. All three were
genuine, all three are fixed, and each fix has a test that fails on `5cc06f4`.

### 11.1 Finding 1 — vacuous artifact guard

`artifact_manifest()` returns `{}` for a directory that does not exist, and
`put()` stored `artifacts: {}` whenever `artifact_root` was `None`. So an entry
written by a `--no-write-artifacts` run matched a later artifact-*writing* run
(`{} == {}`), the short-circuit fired with an empty refusal list, and the run
reported 61 converted objects with no artifacts on disk. `artifact_root` was
not part of the key. **The §10 artifact guard was decorative in exactly the
case it existed for.**

Fixed at three levels: `artifactsExpected` is now a **key component**, so the
two runs cannot address the same entry at all; `get()` refuses when the stored
expectation differs from the caller's; and when artifacts are required it
additionally refuses a missing `artifact_root` directory and an empty stored
manifest. Tests:
`test_no_artifact_run_entry_never_satisfies_an_artifact_run` (the verifier's
scenario) and `test_artifact_expecting_entry_refuses_a_ledger_only_run` (the
reverse, so the guard is not one-sided).

### 11.2 Finding 2 — `get()` unguarded by `object_selector`

Only `put()` was guarded. A `--warm-shard` worker that found a warm entry
returned the **whole faction's** coverage and did no work, while still printing
`WARM_SHARD ... objects=61 converted=61`.

Fixed by not constructing the coverage cache at all for a sharded run, so a
shard can neither consume nor produce a whole-faction entry.
`test_shard_worker_never_consumes_a_whole_faction_entry` asserts the cache is
never even opened and that the build always runs.

**Were the published pool timings affected? No, and here is the proof rather
than the argument.** The shard logs from the runs those numbers came from:

```
objects=1 converted=1 cache_hits=0 ... objects=8 converted=8 cache_hits=0
(24 lines: 2 runs x 12 shards, each run's objects summing to 61, every line cache_hits=0)
```

No shard ever reported `objects=61`, which is what a short-circuit hit would
have produced, and every line shows `cache_hits=0`. Those were cold runs into
freshly-deleted cache roots, so no warm entry existed to consume. The bug was
real and latent; it had not yet fired in any measurement. A fresh pooled cold
run on the fixed code is recorded in §11.5.

### 11.3 Finding 3 — strandable `_CachePending`

`_flat_blocks_for_kind` published failure on `BaseException`, but
`_named_definition_values` and `_weapon_damage_nuggets` claimed a pending slot
with no failure path. If either raised, every concurrent waiter blocked in
`condition.wait()` **forever** — a deadlock, not a slowdown. The existing test
exercised the primitives, not these call sites, which is precisely the false
comfort the verifier named.

Both compute bodies are now split into `_named_definition_values_uncached` and
`_weapon_damage_nuggets_uncached`, and both wrappers publish failure and
re-raise. Tests `test_named_definition_failure_releases_waiters` and
`test_weapon_nugget_failure_releases_waiters` make the **real call site** raise,
assert the pending slot is gone, and then assert a second caller returns within
a 10-second timeout instead of hanging.

Honest note on their failing-first mode: on `5cc06f4` these two fail because
the `*_uncached` seams they patch do not exist, not by hanging. A test that
proves the old code hangs would itself hang, which is a worse gate. The
behavioural assertion — waiter released within a timeout — runs against the new
code.

### 11.4 Finding 4 — JSON-stability test was blind

`json.dumps` renders a tuple and a list identically, so the old hand-written
round-trip assertion could not have detected tuple→list drift.
`test_type_sensitive_canonicalization_detects_tuple_drift` demonstrates the
blind spot directly (`json.dumps({"a": (1,2)}) == json.dumps({"a": [1,2]})`),
and `test_real_census_graph_is_type_level_json_stable` now runs the **real**
`census_playable_faction` for Men and compares a type-sensitive canonicalization
of the object before and after the round trip, plus the canonical digest. It
skips only when the retail state root is absent.

### 11.5 Re-measured

Fresh pooled cold run for men on the fixed code, isolated cache root
(`fix-pooled-cold-men.log`):

| | pool | parent | total |
|---|---|---|---|
| before fixes (§10.3) | 211.5 s | 28.9 s | 240.4 s |
| after fixes | 236.5 s | 46.1 s | **282.6 s** |

The short-circuit correctly missed (`no entry for this input identity`), all 12
shards reported 1-8 objects summing to 61 with `cache_hits=0`, and no shard
reported `objects=61`. The ~42 s difference is **not** attributable to the
fixes, which are refusal paths and a construction guard that do no work on the
hit path — the publish agent was being re-engaged on the same machine during
this run, and single-faction pool timings have swung 211-236 s across runs all
session. I am recording both numbers rather than picking the flattering one;
the honest statement is that no cost was expected from these fixes and none is
demonstrable above the machine noise.

### 11.6 Merge impact — read before merging

Verifier-measured and confirmed here: **merging this branch moves every
family's compiler identity.** Every lane manifest includes `faction_import.py`,
`incremental_rebuild.py` and `faction_object_cache.py`, and all three changed.
Consequences, stated plainly:

- the durable object cache invalidates **100%**;
- existing faction coverage documents go **stale**, and the publish gate will
  **correctly refuse** them until a full convert re-runs;
- **`--allow-stale-coverage` must not be used to bypass this in production.**
  That flag exists for diagnosis. Using it here would publish descriptors that
  no current compiler produced, which is the exact failure the compiler-identity
  chain exists to prevent.

The remedy is a full convert after merge, not a flag. On the current code that
is the ~7.9 min pooled cold run of §10.4 — and the owner has since directed that
it must be under 5 minutes measured, which is §12.

## 12. Option C — workers produce the rows, the parent assembles (eighth lane, on 29a3dc9)

Logs (all under `C:\Users\Jonathan\Desktop\open-bfme\workspace\logs\perf-convert\`,
`optionc-` prefix): `optionc-smoke-men.log`, `optionc-identity-men-pooled.log`,
`optionc-cold7-n16.log`, `optionc-repeat7-n16.log`,
`optionc-compileredit7-n16.log`, `optionc-compileredit7-n24-j1.log`,
`optionc-compileredit7-n20.log`.

**This is the first section in the report whose seven-faction numbers are
measured rather than derived.** Every previous section's seven-faction figure
was arithmetic from a `men`-only lane. The headline below is a real batch.

### 12.1 What changed

`--produce-procs N` replaces the parent's serial recompute pass entirely. The
parent still owns every published byte, but it no longer *computes* any of
them:

1. **A persistent worker pool fed by a queue.** `ProducePool` starts N worker
   processes once and keeps them alive for the whole batch; jobs are handed out
   dynamically over a `queue.Queue`, one JSON job per line on the worker's
   stdin, one `RESULT <json>` line back on stdout, diagnostics to a per-worker
   log file. §8 paid the ~35 s corpus load once per process and got that right
   only on the second attempt; a queue additionally stops a worker that draws a
   heavy shard from stranding the rest. `test_produce_pool_drains_the_queue_
   across_live_workers` pins the dynamic behaviour with a deliberately slow
   stub worker.
2. **Census fan-out, then the parent is the graph authority.** Round one is one
   census job per faction spread over the pool (not N workers each computing
   all seven — the ~154 s of §8.5). The parent then loads each graph from the
   durable census cache, serialises it with *exactly* the canonicalisation the
   cache keys are cut from, and writes it to a ship file.
3. **The graph ships digest-verified.** Each produce job carries the ship path
   and the declared `graphSha256`. The worker checks the received bytes twice:
   `sha256(bytes) == declared`, and `graph_digest(json.loads(bytes)) ==
   declared`. Either failing is a refused job — counted in `graphRefusals`,
   logged as `PRODUCE_JOB_FAILED ... graph-digest-mismatch`, and the faction
   falls to the serial path. The digest keys both the plan-row and the object
   cache, so silent drift here would be silently wrong cache entries, which is
   the §9 trap restated as a runtime check.
4. **Workers return rows.** `build_faction_conversion(..., produce_shard=True)`
   returns a *shard payload* — a different schema
   (`openbfme.faction-convert-shard`) with no `summary` and no
   `aggregateSha256`, so nothing downstream can mistake a partial shard for a
   publishable document — carrying this shard's `(plan row, coverage row)`
   pairs plus the faction scaffold every shard must agree on.
5. **The parent assembles, and proves it.** `assemble_faction_convert_shards`
   refuses: a shard set that is not exactly N shards with indices 0..N-1; any
   disagreement between shards on faction, player template, catalog identity,
   graph digest, unresolved-leaf count, compiler identity token or the complete
   ordered object id list; a graph digest that is not the one the parent
   shipped; **a catalog identity or compiler identity token that is not the
   parent's own**; an object produced by two shards; and a merged set that does
   not cover the ordered id list exactly once. Then it stable-sorts by object id and
   calls **the same** `finalize_faction_import_plan` and
   `assemble_faction_coverage` the serial path calls — the assembly is shared
   code, not a second implementation that has to be argued equivalent.

### 12.2 Three design decisions worth stating plainly

**Artifacts are written by the workers, not shipped to the parent.** Per-object
artifact documents are ~26 MB per faction; routing them through the pipe would
cost real time for no identity benefit. Each object id is owned by exactly one
shard and each object writes into its own `objects/<id>/` directory through
`write_json_atomic`, so the artifact tree is disjoint by construction and
order-independent. The parent still owns the coverage document, the ledger, the
batch report and both aggregates. The tests compare the artifact bytes serial vs
pooled precisely because this is the one thing the parent did not produce.

**The serial parent-recompute pass is still the default and still the oracle.**
`--produce-procs` is opt-in; with it unset the code path is bit-for-bit what
`29a3dc9` did. Every refusal above falls through to that path rather than
publishing something half-built, and `PRODUCE_REFUSED` names the reason.

**The parent recomputes the short-circuit key material itself.** It does not
take the workers' word for the graph digest, the assets fingerprint or the lane
identities; it derives them and *verifies* the shards against them. That costs
the parent ~1 s per faction and is what makes the digest check meaningful.

### 12.3 Largest-first scheduling and per-faction emission

Requested by the publish lane, whose end-to-end number is gated by which
faction's coverage lands **last**.

- Produce jobs are dispatched **largest faction first**, by census object count,
  ties broken by name (`produce_faction_order`, pinned by
  `test_produce_order_is_largest_faction_first`). Measured order:
  `men:61 dwarves:57 isengard:55 mordor:55 elves:52 angmar:50 wild:48`.
- Each faction's coverage document is written **the moment that faction's last
  shard lands**, not at batch end. `ProducePool.run_round` takes an `on_reply`
  callback fired as each reply arrives; when a faction's completed-shard count
  reaches N the parent assembles and emits it immediately, logging
  `FACTION_COVERAGE_READY <faction> ready_ms=… aggregate=… mtime_ns=… size=…`
  — the exact triple the publish watcher keys on. The batch loop then skips
  rewriting an already-emitted file, so a done faction is never re-seen with a
  bumped `mtime_ns`. `test_round_reports_each_reply_as_it_lands_not_at_the_end`
  pins the in-flight callback.
- Short-circuited factions are emitted during the probe, before the pool starts.

Measured emission profile, seven factions, compiler-edit cold:

| N | men ready | last faction ready | batch end | overlap available to men's publish |
|---|---|---|---|---|
| 24 (`--convert-jobs 1`) | 308.6 s | wild 480.4 s | 522.3 s | 214 s |
| 20 | 257.4 s | wild 527.3 s | 572.3 s | 315 s |

The smallest faction (`wild`, 48 objects) is the tail in both runs, which is the
property asked for. **One honest caveat:** at N=24, `dwarves` landed 10.5 s
*before* `men` (298.1 s vs 308.6 s). Dispatch is largest-first, but completion
is not guaranteed largest-first — men's 61 objects over 24 shards leave 2-3
objects per shard, so men's finish time is set by its single slowest object
while a worker that finished its men shard early had already started dwarves.
Closing that 3 % would mean splitting the first faction more finely than the
pool width, which adds per-job fixed cost; I did not do it. men still lands
214-315 s before the batch ends, which is more than its ~150 s publish needs.

### 12.4 Measured: seven factions, actual wall time

Shared state root, pinned interpreter, RotWK, all seven factions, no
`--faction` filter. The exact compiler edit for the "compiler-edit cold" rows
was a single trailing comment line appended to **both**
`importer/openbfme_importer/playable_unit_compiler.py` and
`importer/openbfme_importer/playable_structure_pack_compiler.py`:

```
# perf probe: optionc compiler-edit cold, marker <A|B|C> (2026-08-20)
```

A distinct marker per run, so no run could reuse a previous run's identity.
`BATCH_WALL_MS` is the batch timer; it excludes catalog load and
effective-assets verification, which are identical for serial and pooled (~63 s,
derived from the process wall of the fully-cold run).

| Run | N | `--convert-jobs` | probe | census | pool (incl. in-flight assembly) | parent tail | **BATCH_WALL_MS** |
|---|---|---|---|---|---|---|---|
| fully cold (every lane identity moved by this branch) | 16 | default (16) | 46.1 s | 68.7 s | 436.8 s + 11.8 s | 42.5 s | **606.0 s / 10.1 min** |
| compiler-edit cold, marker A | 16 | default (16) | 39.6 s | 19.5 s | 418.5 s + 9.6 s | 38.4 s | **525.6 s / 8.8 min** |
| compiler-edit cold, marker B | 24 | 1 | 42.7 s | 18.8 s | 423.2 s | 37.6 s | **522.3 s / 8.7 min** |
| compiler-edit cold, marker C | 20 | default (16) | 40.9 s | 19.1 s | 471.3 s | 40.9 s | **572.3 s / 9.5 min** |
| **repeat run, nothing changed** | 16 | default | 29.3 s | — | pool never started | 39.7 s | **69.0 s / 1.15 min** |

The two fully-cold/marker-A rows carry a separate assembly figure because
assembly still ran after the pool there; markers B and C use the in-flight
per-faction emission of §12.3, so their assembly is inside `pool`. The "parent
tail" column is 7 faction rows at 2.8-3.5 s each (~21 s) plus the ledger summary
and batch report writes (~18 s).

**The headline: 522 s (8.7 min) for a seven-faction compiler-edit cold convert.
That is over the owner's five-minute bar.**

*Provenance of these timings, stated because this report has been bitten by it
before:* every row above was measured on the work path as committed in
`69e353b`. The follow-up commit `0531b9e` adds two equality checks on the
assembler's hit path (catalog identity, compiler identity token) and does no
work, but it does change `faction_import.py` bytes and therefore moves every
lane identity — so a run on `0531b9e` starts from a cold cache again, and these
numbers were not re-taken on it.

What Option C did deliver:

- **The parent pass is gone.** Per-faction parent cost after the pool is ~3 s
  (`FACTION_TIMING` rows: 2.8-3.5 s each) against 28.9 s in §10.3 — **202 s →
  ~21 s across seven factions**, which is exactly what §10.5 predicted the
  design would remove.
- **The repeat run got faster, not slower: 92 s derived in §10.4 → 69 s
  measured.** No pool is started at all when every faction short-circuits.
- All seven factions assembled on every run: `assembled=7 refused=0
  graph_refusals=0`, `gaps=0 complete=True` for all seven.

### 12.5 The remaining floor, named precisely

The pool wall is now ~95 % of the cold batch and it is **not** a parallelism
problem any more — it is raw convert CPU on a 24-logical-core box:

| N | threads/worker | pool wall |
|---|---|---|
| 16 | 16 | 418.5 s |
| 24 | 1 | 423.2 s |
| 20 | 16 | 471.3 s |

Sixteen processes and twenty-four processes land within 1 % of each other, and
the twenty-process run was slower than both — that run overlapped the publish
agent's measurement storm, which is the honest explanation for it being the
outlier rather than any property of N=20. **Widening the pool has stopped
buying anything.** The floor is therefore:

1. **~420 s of convert+plan CPU** for a unit+structure edit across ~380 objects.
   A unit-compiler edit plus a structure-pack-compiler edit invalidates
   essentially every object in every faction, so "compiler-edit cold" is within
   16 % of "fully cold" (522 s vs 606 s). Spreading this wider is not possible
   on this host; the only way past it is to **recompile fewer objects** — lane
   precision at the object/family level, so that a `playable_unit_compiler` edit
   does not invalidate structure rows and vice versa. That is a different lane
   from this one and it is where the next 3 minutes live.
2. **~40 s parent probe** (seven short-circuit probes, each resolving the
   faction and peeking the census cache). New in this lane; it is what buys the
   69 s repeat run, so it pays for itself many times over, but it is now a
   visible serial cost and could be parallelised.
3. **~19 s census fan-out** and **~63 s process startup** (catalog +
   effective-assets verification), neither of which this lane touched.

Adding those up: even with an instantaneous parent, this machine cannot do a
seven-faction unit+structure-edit convert in under ~7 minutes today. **I am not
going to present 8.7 minutes as meeting a 5-minute bar.**

### 12.6 A defect this lane produced, and how it was caught

The first live seven-faction attempt refused **every** faction:

```
PRODUCE_REFUSED men: ShardAssemblyError: men: shards produced faction 'Men'
(falling back to the serial parent pass)
```

The census graph names the faction as retail spells it (`Men`); the batch holds
the discovered short name (`men`). Every unit test used one spelling, so all of
them passed. The fail-closed design did its job — the run produced correct
output via the serial fallback and merely lost the speedup — but the gap was
real. `test_assembly_accepts_the_discovered_short_name` now pins both spellings.
This is the §9.6 lesson repeating: a cache/assembly seam is not proven by unit
tests that never see the real identity strings.

### 12.7 Tests

`importer/tests/test_faction_convert_optionc.py`, 25 tests, all new, all
CI-runnable against the synthetic hero-roster fixture (no retail required):

- **The identity test the brief asked for** —
  `test_pooled_option_c_is_byte_identical_to_the_serial_parent`, parameterised
  over 1/2/3/5 shards: every artifact byte-for-byte, the coverage document
  field-for-field (only per-run timings normalised), `planAggregateSha256`,
  `aggregateSha256`, `coverage_digest_payload`, and row order.
- `test_assembly_does_not_depend_on_the_order_shards_arrive` (reversed
  completion order → identical bytes) and
  `test_assembly_is_independent_of_the_worker_count` (1/2/3/5 shards → one
  aggregate).
- Nine refusal tests: foreign graph digest, incomplete shard set, dropped
  object, object produced by two shards, shards disagreeing with each other on
  compiler identity, **a unanimous pool that disagrees with the parent on
  compiler identity or catalog identity**, duplicate shard index, foreign
  faction, schema drift.

  That second compiler-identity test exists because of a hole this lane's own
  self-review found after the first commit: unanimity across shards would
  happily admit an entire pool running different importer bytes than the
  parent — which is exactly what editing a source file mid-run produces, the
  trap this report opens with in its confounds section. The parent now supplies
  its own catalog identity and compiler identity token and the assembler
  refuses anything else.
- `test_a_shard_payload_is_not_a_coverage_document`,
  `test_produce_mode_requires_a_selector_and_shard_coordinates`,
  `test_assembly_accepts_the_discovered_short_name`.
- Graph shipping: `test_shipped_graph_bytes_reproduce_the_canonical_digest`,
  `test_a_worker_refuses_a_graph_whose_digest_moved`,
  `test_a_worker_refuses_a_graph_that_lost_a_type_in_transit`.
- Pool: `test_produce_pool_drains_the_queue_across_live_workers`,
  `test_produce_pool_reports_a_worker_that_dies`,
  `test_round_reports_each_reply_as_it_lands_not_at_the_end`,
  `test_produce_order_is_largest_faction_first`.

**Failing-first:** the whole file fails to collect on `29a3dc9` —
`ImportError: cannot import name 'CONVERT_SHARD_SCHEMA'` — because none of the
seams exist there. That is the honest form for a file that is entirely new
surface; it is not a case where an old behaviour could be demonstrated failing.

Suite: **407 passed, 8 skipped, 0 failed** across `test_faction_convert_perf`,
`test_faction_convert_optionc`, `test_faction_import`,
`test_faction_object_cache`, `test_incremental_rebuild`, `test_spellbook_import`,
`test_playable_unit_compiler`. Note on the known environmental failure:
`test_horde_dispatch_graphs_cover_exact_effective_retail_corpora` **passed** in
that whole-suite invocation but **fails standalone** on both this branch and
`29a3dc9` with `FileNotFoundError: ...worktree.../workspace/retail-work/catalog/
bfme2.json`. It depends on ambient environment another test in the suite
happens to set. Pre-existing, unchanged by this lane, verified on the baseline.

### 12.8 Retail byte-identity: serial oracle vs pooled Option C

*Pending a measurement window; see §13.*

## 13. Not verified

Sections 1-11 (superseded for the seven-faction figures by §12):

- Only `men` was run, per the lane brief (another agent held `elves`). The
  seven-faction numbers are projections from measured per-faction and
  in-batch-position costs, not a measured seven-faction batch.
- The publish stage and the full `rotwk_full_content` pipeline were not run.
- No cook, pack, or selection change was made or verified.
- The plan cache has only ever been exercised on RotWK/`men`. The BFME2 lane
  and the `--plan-only` path go through `plan_faction_import`, which passes no
  cache and is therefore unchanged, but that is reasoning, not a measurement.

Section 12 (Option C) specifically:

- **Retail byte-identity serial-oracle vs pooled has not been run yet** on this
  branch (§12.8). The identity proof that exists today is the CI test over the
  synthetic fixture at 1/2/3/5 shards, which covers assembly, ordering and
  artifact bytes but not the retail corpus. A retail `men` + one other faction
  diff is owed and needs a machine window; the publish lane's measurement storm
  had priority.
- N=12 was **not** measured. N=16, N=20 and N=24 were; 16 and 24 landed within
  1 % of each other, so 12 was expected to be strictly worse and the window was
  spent on the closer question (does more parallelism help — it does not).
- Single-faction `men` under Option C was not measured on the final code for
  continuity with §6-§11. The two `men`-only runs that exist
  (`optionc-smoke-men.log`, `optionc-identity-men-pooled.log`) predate the
  short-name fix and the scheduling change and should not be read as timings.
- Every seven-faction run in §12 shared the machine with a concurrently running
  verifier and, for the N=20 run, the publish agent's measurement storm. Only
  the N=20 outlier is attributed to that, and it is attributed explicitly rather
  than averaged away.
- The `--produce-procs` path refuses the `layered-effective-assets` tree
  outright (the layered branch rewrites the graph after census, so the shipped
  digest would not be the digest workers key on). That refusal is coded and
  reasoned, not exercised — the layered tree is quarantined.
- **Shared-state side effect, disclosed:** the §12 measurement runs wrote all
  seven `editions/rotwk/reports/faction-import/<faction>-coverage.json` in the
  shared state root. As of 2026-08-20 16:29 they carry
  `compilerIdentityToken 904a2d1c…`, which is this worktree **with the
  marker-C perf-probe comment still appended** — an importer state that no
  longer exists in any tree, because the probe was reverted. The documents
  themselves are internally consistent (7/7 `gaps=0 complete=True`), but any
  publish that binds against them will be **correctly refused** by the
  compiler-identity chain. Whoever needs valid coverage must re-run convert
  from their own tree. `--allow-stale-coverage` is not the remedy.
- **Disk:** each pooled run leaves `<state>/reports/produce-shards/<runId>/`
  holding seven shipped graphs (~1.1 MB each) and `7 x N` shard payloads, ~15-20
  MB per run. Nothing prunes it. I deliberately did **not** add a deleter — this
  tree is a shared state root with other agents' runs in flight and the evidence
  is what a verifier reads — so it belongs in the existing disk-prune recipe.
Section 15 (the §14 cuts) specifically:

- §15.6 is now MEASURED on a quiet box, and §14.5's horde hypothesis is
  REFUTED, not open. What follows is what is still not verified.
- **The N=12 timing was never taken.** N=12 was used only for the byte-identity
  runs. N=16 and N=24 were timed; §12.4 already showed 16 and 24 within 1 % of
  each other, so N=12 was not worth a headline slot.
- **The populate run is not a clean "fully cold" number.** men and dwarves were
  already warm from the identity runs, so its 331.9 s is not comparable to
  §12.4's 606 s fully-cold figure and is labelled "not a headline".
- **The descriptor memo's retail A/B is noisy.** Two `memo OFF` samples on the
  same three objects gave 23.1 s and 36.3 s against 20.6 s with the memo. The
  direction is consistent and the call counts are exact (9 -> 6), but the
  magnitude of cut 1's saving on retail is not pinned down — only bounded well
  below what §14 predicted.
- The descriptor memo helps **only a cold run**. When the durable plan-row cache
  hits, the plan compiles nothing and publishes nothing, so the convert draft
  compiles exactly as before — correct, just not faster. That case is
  uninteresting today because a plan-row hit almost always accompanies an object
  cache hit, but the two lanes can move independently (§7) and then the memo
  contributes nothing.
- **Cut 3c (caching effective-assets verification) was declined, not deferred by
  oversight.** ~63 s of startup stays. It is blocked on manifest-mode
  verification actually binding tree bytes
  (`effective_assets_identity.py:394`), which is another lane's finding and
  another lane's fix.
- The BFME2 lane still has no coverage for any of this; every measurement and
  every retail-data test is RotWK.
- The publish stage was still not run from this lane, so the composed
  convert+publish end-to-end number belongs to the publish lane's measurement,
  not to this report.

## 14. Diagnosis of the 420 s pool floor (analysis only — nothing implemented)

Evidence already on disk, no new runs. Two independent sources:

- **Per-object convert times** from the `--convert-jobs 1` run's shard payloads
  (`reports/produce-shards/a26f9f8382a24324a26867e658d9a479/`, N=24, marker B).
  One thread per worker, so `convertElapsedMs` is true serial time per object,
  not inflated by intra-process threading.
- **Per-job stage boundaries** from the worker progress logs
  (`reports/produce-workers/produce-worker*.log`), N=20 marker C: 140 jobs,
  378 objects.

### 14.1 (a) straggler tail or (b) uniform saturation? — **(b), decisively**

| | |
|---|---|
| pool wall, N=24 | 423.2 s |
| pool core-second budget (24 x 423.2) | 10 152 core-s |
| slowest **single object** | MenSpellBook, **115.1 s** |
| slowest **single shard job** | Dwarves shard 0, **122.9 s** |

The slowest unit of work is 122.9 s against a 423.2 s wall. **No object and no
job sets the finish time** — the pool could lose its worst job entirely and
finish ~1 s earlier. Utilisation is the story, not the tail:

```
N=20 (marker C), pool budget 9 426 core-s
  per-job setup     980 core-s  10.4%
  PLAN stage      3 382 core-s  35.9%   (8.95 s/object)
  CONVERT loop    3 934 core-s  41.7%  (10.41 s/object)
  idle / unaccounted            1 130 core-s  12.0%
```

Cross-check: the independent N=24 `j=1` shard payloads give **4 125.5 core-s**
of convert for 378 objects (10.9 s/object) against 3 934 core-s (10.41
s/object) from the N=20 logs — within 5 %, two different runs, two different
extraction paths. The 12 % idle is worker start-up (~6 s x 20) plus the queue
draining unevenly at the end.

Per-object histogram (N=24, `j=1`, 378 objects, 4 125.5 core-s of convert):

| bucket | n | core-s | share |
|---|---|---|---|
| 0-1 s | 14 | 0.4 | 0.0 % |
| 1-2 s | 1 | 1.9 | 0.0 % |
| 2-5 s | 55 | 223.1 | 5.4 % |
| **5-10 s** | **193** | **1 290.9** | **31.3 %** |
| **10-20 s** | **80** | **1 093.3** | **26.5 %** |
| 20-40 s | 16 | 504.5 | 12.2 % |
| 40-80 s | 18 | 896.5 | 21.7 % |
| >= 80 s | 1 | 115.1 | 2.8 % |

The mass is in the middle: **273 objects costing 5-20 s carry 58 % of the
work.** The top 20 objects together are ~1 048 core-s, 25 % of convert — real,
but nothing you could fix twenty objects and be done with.

By family (convert only):

| family | n | core-s | share | mean |
|---|---|---|---|---|
| playable-unit | 172 | 2 320.7 | 56.3 % | 13.49 s |
| structure | 169 | 1 241.9 | 30.1 % | 7.35 s |
| **spellbook** | **7** | **428.9** | **10.4 %** | **61.26 s** |
| banner-carrier | 23 | 134.1 | 3.2 % | 5.83 s |
| create-a-hero | 7 | 0.0 | 0.0 % | 0.00 s |

Top 20 slowest objects:

```
 115.07s Men      spellbook      MenSpellBook
  71.85s Dwarves  spellbook      DwarvesSpellBook
  62.07s Isengard spellbook      IsengardSpellBook
  61.61s Mordor   spellbook      MordorSpellBook
  53.80s Men      playable-unit  ElvenGaladriel_RingHero
  53.27s Men      playable-unit  GondorArcher
  52.18s Men      playable-unit  GondorArcherHorde
  50.94s Dwarves  playable-unit  DwarvenGuardianHorde
  49.75s Men      playable-unit  GondorAragornMP
  47.40s Men      playable-unit  RohanEowyn
  47.08s Men      playable-unit  GondorFighter
  46.44s Men      playable-unit  GondorTrebuchet
  46.16s Men      playable-unit  GondorFighterHorde
  45.93s Elves    spellbook      ElvesSpellBook
  44.37s Dwarves  playable-unit  DwarvenGimli
  42.23s Dwarves  playable-unit  DwarvenGloin
  41.00s Men      structure      GondorBattleTower
  40.27s Men      structure      MenFortressCitadel
  40.14s Wild     spellbook      WildSpellBook
  39.18s Men      structure      GondorMarketPlace
```

**Every faction's spellbook is in the top 20.** Six of seven are; the seventh
(Angmar) is just outside.

### 14.2 The straggler effect that IS real — and it is not the wall

Stragglers do not set the batch wall, but they **do** set per-faction emission
order, which is what §12.3 is for. The men/dwarves inversion has an exact cause:

```
115.2s  Men  shard  8  objects=1     <- MenSpellBook, alone in its shard
122.9s  Dwarves shard 0  objects=2
```

Men's convert work is 1 283.6 core-s over 24 shards — 53 s per shard if
balanced — but its slowest shard was 115.2 s, a **2.2x imbalance**, because
sharding is a hash of the object id and the single most expensive object in
every faction lands wherever the hash puts it. That is why men finished 10.5 s
after dwarves despite being dispatched first.

### 14.3 Where the seconds go: the same descriptor is compiled three times

This is the finding, and it is structural rather than a hot loop. Grepping the
descriptor compile call sites in `faction_import.py`:

```
 684  compile_playable_structure_descriptor(   <- PLAN
 721  compile_spellbook_descriptor(            <- PLAN
 761  compile_playable_unit_descriptor(        <- PLAN
1277  compile_playable_structure_descriptor(   <- CONVERT
1431  compile_spellbook_descriptor(            <- CONVERT draft
1480  compile_spellbook_descriptor(            <- CONVERT final
1544  compile_playable_unit_descriptor(        <- CONVERT draft
1581  compile_playable_unit_descriptor(        <- CONVERT final
```

For a **unit** — 56 % of the work — the descriptor is compiled **three times**
per object per run:

1. `_plan_one` (761), to get `descriptorSha256` and `sourceDocumentPaths`;
2. the convert **draft** (1544), to discover which media and strings the object
   needs;
3. the convert **final** (1581), the same compile again with the resolved
   images/audio/strings injected.

**Calls 1 and 2 are the identical call.** Same `object_id`, same `documents`,
same `faction_graph`, same `prepared`, same `game`, same
`engine_spawned_banner_carrier` — no argument differs. Only call 3 differs, and
only by the three `resolved_*` injections. Spellbooks have the same shape (721 /
1431 / 1480), which is why every faction's spellbook is a top-20 object: its
descriptor is the whole faction spell store and it is built three times.
Structures compile twice (684 / 1277).

This is §1's "Sink B — the plan stage is a second, serial, uncached full
compile" restated at object level. `a5fc47c`'s durable plan-row cache made it
free on a *warm* run; it does nothing on the cold run that this bar is about,
because both the plan row and the object entry miss together.

That the plan stage measures **8.95 s/object** — essentially nothing but that
compile plus cache bookkeeping — is the size of the duplicate.

### 14.4 Proposed cut, with predicted numbers (NOT implemented)

**Cut 1 — collapse the plan compile and the convert draft (in-process memo).**
The redundant pair is compiled in the same process for the same object in both
the pooled worker (it plans and converts its own shard) and the serial parent.
A memo keyed on `(object_id, id(documents), id(prepared), game, banner_flag)`,
single-flighted like §1's other shared caches and cleared with the existing
prepared-compiler memo, returns the plan's descriptor to the convert draft.
The compilers already fail closed when `prepared.documents is not documents`,
so identity — not equality — is the right key and the existing guard backs it.
*Saving: the whole plan stage, ~3 382 core-s of 9 426 (36 %).* Scaled to N=24:
~3 640 of 10 152 core-s. **Pool 423 s -> ~271 s.**

**Cut 2 — hoist the per-job corpus digests to per-process.** `setup` is 980
core-s (10.4 %), ~7 s on every job after the first. `document_hashes`,
`full_corpus_closure` and `durable_non_ini_assets_fingerprint` are recomputed
inside `build_faction_conversion` on every call, and they are functions of the
*corpus*, not the faction — identical across all seven factions in one worker.
Memoize on `id(documents)` / the effective root. *Saving: ~700-800 core-s.*
**Pool ~271 s -> ~242 s.**

**Cut 3 — the parent's 162 s of serial overhead.** ~63 s process start-up
(catalog load + effective-assets verification), ~42 s short-circuit probe,
~19 s census fan-out, ~38 s ledger-summary and batch-report writes. The probe is
seven independent lookups done in a loop and is trivially parallel; the
effective-assets verification result is a pure function of a sealed tree and
could be cached against its own manifest digest; the ledger summary is written
after every faction row rather than streamed. *Saving: ~75 s if all three land.*

| stage | today | after cuts 1+2 | after 1+2+3 |
|---|---|---|---|
| pool | 423 s | 242 s | 242 s |
| parent serial | 162 s | 162 s | ~87 s |
| **seven-faction compiler-edit cold** | **522 s** | **~404 s / 6.7 min** | **~330 s / 5.5 min** |

**Answer to the question this diagnosis was commissioned to settle: the
5-minute bar is reachable, but not by any one change.** Cut 1 alone lands ~6.9
min. All three land ~5.5 min — close enough that the remaining half-minute would
have to come from a fourth item (most likely the ~2 300 core-s of
`playable-unit` convert that is not descriptor compile: recipes, visual closure,
runtime documents), and I would not promise 5 min without measuring cut 1 first.
Every number above is a projection from measured stage shares and is labelled as
such; the only way to know is to build cut 1 and re-run the same seven-faction
compiler-edit batch.

**Cut 0 — cheap, orthogonal, and it fixes §12.3's inversion.** Shard by
*predicted cost* rather than by id hash: put each faction's spellbook in its own
shard and distribute the rest largest-first (prior-run `convertElapsedMs` is
already in the durable object cache and in every coverage document, so the
predictor is free). Men's slowest shard was 115.2 s against a 53 s balanced
ideal; balancing would pull men's completion from 308 s toward ~260 s and make
largest-first dispatch actually produce largest-first *completion*. This does
not move the batch wall — §14.1 — it moves when the publish lane can start.

### 14.5 One hypothesis I could NOT confirm, and the cheap test for it

`GondorArcher` 53.3 s and `GondorArcherHorde` 52.2 s; `GondorFighter` 47.1 s and
`GondorFighterHorde` 46.2 s; `DwarvenGuardianHorde` 50.9 s. Horde objects cost
about the same as their member unit, which is consistent with the horde's
compile re-doing the member's compile from scratch — shared work that an
intra-process memo on the member id would collapse. **I did not verify this**
and I am not going to assert it from a coincidence of timings. The cheap test:
instrument `compile_playable_unit_descriptor` with a per-process call counter
keyed by `object_id` and run one faction; if `GondorArcher` is compiled more
than three times (plan, draft, final), the horde is re-entering it and cut 1's
memo will pick that up for free. That measurement is two minutes of a window,
not a lane.

### 14.6 An evidence-hygiene defect in my own tooling (FIXED in §15)

`reports/produce-workers/produce-worker<i>.log` is **not run-scoped**, so the
N=20 run overwrote workers 0-19 of the N=24 run and only workers 20-23 survived.
My first pass at the stage split silently mixed the two runs (173 jobs / 450
objects — impossible for either run alone) before I noticed. The numbers above
are the clean N=20-only subset (140 jobs / 378 objects), cross-checked against
the N=24 shard payloads, which *are* run-scoped. The log path should carry the
run id. Not fixed here — §14 is analysis only.

## 15. The §14 cuts, implemented (ninth lane, on cced800)

Authorized by the coordinator after §14. **Code and unit tests only — no
seven-faction measurement has been taken on this code yet**, so every wall-time
number in this section is either a micro-measurement of the specific thing
changed, or a projection carried forward from §14 and labelled as one.

### 15.1 Cut 1 — the plan/draft descriptor memo

The duplicate §14.3 found is now collapsed. `_memoized_descriptor` is owned by
one `(documents, prepared, faction_graph)` triple, holds strong references to
all three so their ids cannot be recycled under a live entry, and drops the
whole memo when any of the three changes identity. Identity is the right key
for exactly the reason the coordinator gave: the compilers already fail closed
when `prepared.documents is not documents`.

Two properties that make it hard to get wrong:

- **An entry is consumed on read.** There is exactly one consumer (the convert
  draft) per producer (the plan), so no descriptor is aliased by two callers
  and peak memory is bounded by planned-but-not-yet-converted objects.
- **A miss just compiles.** Every failure mode degrades to the old behaviour,
  so the memo cannot change what is produced — only how often.

Plan and convert never run concurrently for the same object (the plan stage
completes before the convert loop starts), so a plain lock suffices and there is
no pending slot to strand — §11.3's failure mode is absent by construction, not
by care.

**Structures needed a different key.** §14.3 said the plan and convert call
sites pass the same values; reading them properly showed they pass *different
locals* — the plan's `engine_spawned_roots` come from the census graph's roots,
the convert's `spawned` from `implicit_object_roots`. Those are frequently equal
and not guaranteed to be. So the structure key carries the actual policy values
(roots, roles, wall templates, source-null sets) and the memo hits only when
they genuinely match; when they do not, both sides compile exactly as before.
Units and spellbooks are literally the same call and key trivially.

**Byte-identity, proved before it was allowed to be fast**
(`test_descriptor_memo_output_is_byte_identical_to_recompiling`): the same
fixture converted with `OPENBFME_NO_DESCRIPTOR_MEMO=1` and without — every
artifact byte-for-byte, the coverage document field-for-field, and both
aggregates equal.

**The §14.5 call counter, and what it did and did not answer.** The test wraps
*both* `faction_import.compile_playable_unit_descriptor` and the compiler
module's own symbol, so a horde re-entering its member's compile inside the
compiler would be counted too. On the fixture:

```
object          before  after
heroeight            3      2
heroseven            3      2
TOTAL                6      4
max compiles for any single object, memo off: 3
```

Exactly the predicted collapse: three compiles per unit (plan, draft, final)
become two, and `max = 3` means nothing re-enters. **But the hero-roster fixture
contains no horde objects, so this does NOT answer §14.5's actual question.**
That needs the retail corpus and is queued for the window — and it is cheap
there: compile one `GondorArcherHorde` with the counter attached, no batch
required.

### 15.2 Cut 2 — per-job work hoisted to per-process, and a bigger fish

The authorized cut was the corpus digests, keyed on the corpus identity I
already hold. Done: `_corpus_digests` is a single-entry memo on the corpus
object with a strong reference, returning `(document_hashes,
full_corpus_closure)`. Measured on the real corpus (792 documents, 29.1 MB),
these turned out to be **cheap** — 0.02 s and 0.00 s — so the cut is real but
small.

Measuring rather than assuming found the actual cost. Timing every component of
the ~7 s per-job setup against the real RotWK data:

```
catalog load          0.94s
effective catalog     0.59s
resolve #1 (men)      3.63s
resolve (dwarves)     4.32s      <- every call, every job
resolve (elves)       4.59s
resolve (men)         4.37s
corpus #1             0.84s  (792 docs, 29.1 MB)
corpus #2 (memoized)  0.00s
document_hashes       0.02s
full_corpus_closure   0.00s
```

**`resolve_playable_faction` costs ~4.3 s and was never memoized.** It re-parses
the effective PlayerTemplate document and re-resolves every object's inherited
Side on every call — and it is called once per convert job plus three times per
faction in the batch parent. That is essentially the entire per-job setup, and
it is ~720 core-seconds of a pooled seven-faction run plus ~90 s of the parent's
*serial* time.

`discover_playable_factions` is now memoized per catalog **object**, holding a
strong reference so the id cannot be recycled underneath the entry.
Measured on the real catalog: **4.38 s -> 0.0000 s**, byte-identical
`PlayableFaction` result, all seven factions discovered.
`test_faction_discovery_memo_holds_the_catalog_so_ids_cannot_recycle` churns 200
allocations and asserts no stale entry is ever served.

### 15.3 Cut 3 — the parent's serial overhead

- **Probe parallelised.** Seven independent lookups, each dominated by hashing
  that faction's artifact tree, now run on a thread pool. Faction discovery is
  warm before the threads start (the caller resolves every short name first), so
  they cannot stampede it. `PRODUCE_PROBE ms=` reports the cost directly instead
  of leaving it to be derived by subtraction.
- **Ledger streaming.** `ConversionLedger.record` did `mkdir` + `open(mode="a")`
  + write + `close` **per event**. At ~385 events per seven-faction convert
  that is ~1 150 filesystem calls on a Windows box with a scanner in the path.
  The handle is now opened lazily and kept, and **flushed after every event** —
  durability is unchanged, a crash still leaves a complete JSONL up to the last
  recorded event, and `test_ledger_keeps_one_handle_and_still_flushes_every_event`
  asserts both (one append-mode open; each event readable *between* records).
  A broken sink still fails open and still recovers.
- **Effective-assets verification caching: DECLINED, deliberately.** The
  coordinator offered two bounded options. I took neither cache and chose
  option (b): leave it. The publish agent established that manifest-mode
  verification never binds the manifest to tree bytes
  (`effective_assets_identity.py:394` gates `_verify_tree_bytes` behind
  `verify != "manifest"`, and the cook call sites take the manifest default).
  Caching a verification that verifies nothing byte-level would make a real gap
  cheaper to hit and harder to see, and building a sampled byte re-derivation
  here would be inventing a second, weaker verification path next to the one
  that needs fixing. **~63 s of process startup therefore stays**, and this cut
  is blocked on fixing manifest-mode verification, which is not this lane.

  Note that the two durable fingerprint memos in `faction_object_cache` are a
  different thing and are safe: they are keyed on the manifest's own
  `(mtime_ns, size)` stamp, and those functions only ever read the manifest in
  the first place, so the memo neither adds nor removes trust. Measured
  0.234 s -> 0.0002 s;
  `test_durable_fingerprints_memoize_but_follow_the_manifest` proves a changed
  manifest still changes both, and
  `test_fingerprint_memo_is_skipped_without_a_manifest` proves the tree-walk
  fallback is never memoized.

### 15.4 Cut 0 — cost-predicted sharding

`balanced_shard_assignment` does longest-processing-time-first bin packing over
the previous run's own `convertElapsedMs`, read free from the last coverage
document; objects with no prior measurement get the **median** of those that
have one, so a new object is neither assumed free nor assumed catastrophic. The
parent writes the assignment beside the shipped graph and each job references
it.

The safety property that matters: **assignment is a hint, coverage is total.**
`assigned_shard_selector` sends an id to its assigned shard if it has one and to
the same stable hash otherwise, so the compiler's banner-carrier expansion —
which adds ids the parent never saw at census time — stays covered exactly once,
which is what the assembler's completeness check demands.
`test_unassigned_ids_still_fall_to_the_hash_exactly_once` pins that directly,
and `test_balanced_sharding_covers_every_id_exactly_once` checks 1/3/8/24-way
splits.

Against the measured pathology (60 units at 8 s plus one 115 s spellbook over 24
shards), balanced assignment puts the spellbook alone and `max(load)` is the
spellbook itself — the 2.2x imbalance of §14.2 becomes 1.0x by construction.
**This does not move the batch wall** (§14.1); it moves when each faction's
coverage lands, which is what the publish lane overlaps against.

### 15.5 §14.6 fixed

Worker logs are now written to `reports/produce-workers/<runId>/`, so a later
run can no longer overwrite an earlier run's evidence.
`test_worker_logs_are_run_scoped` pins it.

### 15.6 MEASURED: seven factions, quiet box, all cuts

Machine confirmed idle before each run (`Get-Process python` count 0); no other
job started mid-run. Compiler edit is the same one-line trailing comment to
`playable_unit_compiler.py` and `playable_structure_pack_compiler.py`, distinct
marker per run (D, E). Process wall is bracketed with `date`, so it includes
interpreter start, catalog load and effective-assets verification —
`BATCH_WALL_MS` does not.

| Run | N | jobs | probe | census | pool | parent tail | BATCH | **process wall** |
|---|---|---|---|---|---|---|---|---|
| compiler-edit cold, marker D | 24 | 1 | 9.5 s | 0.2 s | 373.2 s | 0.04 s | 404.2 s | **410 s / 6.83 min** |
| compiler-edit cold, marker E | 16 | default | 12.0 s | 0.1 s | 402.6 s | 0.03 s | 428.7 s | **434 s / 7.23 min** |
| repeat, nothing changed | 16 | default | 9.9 s | — | skipped | 0.02 s | 9.9 s | **15 s** |
| populate (men+dwarves already warm — not a headline) | 24 | 1 | 7.9 s | 16.9 s | 290.2 s | 0.03 s | 331.9 s | 337 s |

Against §12.4, same shape, same machine:

| Scenario | §12 (Option C) | §15 (with cuts) | change |
|---|---|---|---|
| compiler-edit cold, N=24 `j=1` | 522.3 s | **404.2 s** | **-22.6 %** |
| compiler-edit cold, N=16 | 525.6 s | **428.7 s** | -18.4 % |
| repeat run | 69.0 s | **9.9 s** | **-86 %** |

**The parent is now free.** Probe 42.7 s -> 9.5 s, census 18.8 s -> 0.2 s,
parent tail 37.6 s -> 0.04 s, startup ~63 s -> ~6 s. §14's cut 3 targeted 162 s
of parent serial time; what remains is **~16 s of 410**. That over-delivered,
and most of it came from the faction-discovery memo rather than from the three
things cut 3 named — including the ~63 s of startup I had declined to cache,
which turned out to be mostly `_discover_factions`, not verification.

**Best measured seven-faction compiler-edit cold: 404 s BATCH / 410 s process
wall = 6.8 minutes. The owner's 5-minute bar is still NOT met**, by ~110 s.

### 15.7 Where §14's prediction was wrong, with the evidence

§14.4 predicted cut 1 alone would take the pool 423 s -> ~271 s by removing the
plan stage's 3 382 core-seconds. **It did not.** Measured stage split on the
marker-D run's (now run-scoped) worker logs, 168 jobs / 378 objects:

| | §14 (before) | §15 (after) |
|---|---|---|
| per-job setup | 980 core-s (10.4 %) | **71 core-s (0.9 %)** |
| PLAN stage | 3 382 core-s, 8.95 s/obj | **4 199 core-s, 11.11 s/obj** |
| CONVERT loop | 3 934 core-s, 10.41 s/obj | **3 869 core-s, 10.24 s/obj** |

Cut 2 did everything §14 hoped and more — setup fell 93 %, and later jobs cost
**0.03 s** each against 6.94 s. Cut 1 removed the compile it was supposed to
remove and saved almost nothing.

**The memo works; the duplicate was cheap.** Verified directly on retail men
with the object cache disabled, three objects, counting every call to
`compile_playable_unit_descriptor` at both the call sites and the compiler
module's own symbol:

```
warm-up (discard)    wall= 85.0s  calls=9
memo OFF             wall= 23.1s  calls=9
memo ON              wall= 20.6s  calls=6
memo OFF again       wall= 36.3s  calls=9
```

Nine calls become six — exactly the designed 3-per-object -> 2. But the wall
barely moves, and the first line is why: **the same three objects cost 85 s on
first touch in a process and ~23 s once the corpus-wide lazy caches are warm.**
A "descriptor compile" is not a fixed cost. The plan stage's seconds are
overwhelmingly the *first-touch corpus scans*, which have to happen once per
process no matter what; the draft the memo deletes runs afterwards, against warm
caches, and is the cheap one. §14 read 8.95 s/object as "one removable duplicate
compile" when it was really "first-touch amortised over the two or three objects
in that job".

The single-object probe says the same thing from the other side: in a fresh
process `GondorArcher` compiles in **24.6 s** and `GondorArcherHorde`
immediately after in **3.9 s**. That also **refutes §14.5's hypothesis** —
`GondorArcherHorde` makes exactly **one** call with **max nesting 1**, so a
horde does not re-enter its member's compile. The near-equal 53.3 s / 52.2 s
timings in §14.1's top-20 were an artifact of the two objects landing in
different shard processes and each paying its own first-touch.

**So the remaining floor is a SHARING problem, not a per-object one.** 24 worker
processes each independently re-derive the same corpus-wide scans. That is the
next diagnosis round, and it is a different shape from anything tried so far:
not more parallelism, not fewer compiles, but getting the per-process warm-up
computed once and shared — which on Windows means either a serialisable prepared
form on disk or fewer, longer-lived processes each doing more objects.

### 15.8 Cut 0 fixed the emission inversion

§12.3 reported men finishing 10.5 s *after* dwarves despite being dispatched
first, because hash sharding left `MenSpellBook` alone in a 115.2 s shard.
Cost-balanced sharding, measured:

```
N=24: men 184.2  dwarves 216.8  isengard 253.8  mordor 290.5
      elves 325.0  angmar 367.5  wild 398.2
N=16: men 188.6  dwarves 227.2  isengard 259.7  mordor 303.5
      elves 357.7  angmar 377.0  wild 425.0
```

Both runs complete in **exactly** largest-first order with no inversion, and the
smallest faction is the tail. men's coverage lands 214-236 s before the batch
ends. `PRODUCE_BALANCE` logs the predicted spread per faction
(men `predicted_max=312.8s predicted_mean=306.1s` — max within 2 % of mean,
against the 2.2x imbalance hash sharding produced).

### 15.9 Retail byte-identity — §12.8 closed

Serial parent-recompute (the oracle) versus pooled Option C with all cuts, on
retail, with `OPENBFME_NO_COVERAGE_SHORTCIRCUIT=1` on **both** sides so neither
could reuse a stored document instead of doing the work.

```
===== men : serial oracle vs pooled Option C (N=12)
ARTIFACT_FILES a=183 b=183   ONLY_A 0   ONLY_B 0   DIFFERING 0
COVERAGE_AGGREGATE_EQUAL True   PLAN_AGGREGATE_EQUAL True
COVERAGE_ROWS a=61 b=61 differing=0 []
COVERAGE_ROW_ORDER_EQUAL True   COVERAGE_DOCUMENT_EQUAL True
IDENTICAL True

===== dwarves : serial oracle vs pooled Option C (N=12)
ARTIFACT_FILES a=183 b=183   ONLY_A 0   ONLY_B 0   DIFFERING 0
COVERAGE_AGGREGATE_EQUAL True   PLAN_AGGREGATE_EQUAL True
COVERAGE_ROWS a=57 b=57 differing=0 []
COVERAGE_ROW_ORDER_EQUAL True   COVERAGE_DOCUMENT_EQUAL True
IDENTICAL True

===== men : pooled N=12 vs pooled N=16 (worker-count invariance)
ARTIFACT_FILES a=183 b=183   ONLY_A 0   ONLY_B 0   DIFFERING 0
COVERAGE_AGGREGATE_EQUAL True   PLAN_AGGREGATE_EQUAL True
COVERAGE_ROWS a=61 b=61 differing=0 []
IDENTICAL True
```

So on retail data: **serial oracle ≡ pooled N=12 ≡ pooled N=16** for men, and
**serial oracle ≡ pooled N=12** for dwarves — artifacts byte-for-byte, coverage
document field-for-field, both aggregates. Comparison script:
`%TEMP%\optionc-oracle\compare_coverage.py`; logs `optionc-cuts-identity-*.log`.

All seven factions on the final tree: `converted` 48/55/50/53/59/53/46,
`gaps=0 complete=True`, one shared `compilerIdentityToken 3e44a6d677bb`.

### 15.10 Tests

`importer/tests/test_faction_convert_cuts.py`, 20 tests, all new:

- identity: `test_descriptor_memo_output_is_byte_identical_to_recompiling`;
- the §14.5 counter: `test_descriptor_memo_removes_the_duplicate_unit_compile`;
- memo mechanics: consumption, owner-change invalidation, env kill switch,
  structure key carrying its policy arguments;
- cut 2: corpus digests per corpus object (including that a *different* corpus
  can never be served one another's closure), fingerprint memo following the
  manifest, no memo without a manifest;
- discovery memo: computed once per catalog object, and the id-recycling test;
- cut 0: expensive-object isolation, exactly-once coverage at 1/3/8/24 shards,
  unassigned ids falling to the hash, median for unmeasured objects, empty
  assignment with no priors, and cost-table parsing of a real coverage document;
- cut 3: ledger one-open-and-flush-per-event, fail-open sink, run-scoped logs.

Named suite: **428 passed, 8 skipped, 0 failed** across the eight files.

**Whole importer suite** (`importer/tests`, 23 min, sequential):
**3 681 passed, 3 failed, 243 skipped, 5 errors, 975 subtests passed.** Judged
by NAME against `29a3dc9`, not by count — the same three files were run on the
baseline and produce the **identical eight names**:

```
FAILED test_module_census.py::test_committed_census_statuses_match_current_importer_source
FAILED test_w3d_chunk_backlog.py::...::test_committed_census_dates_itself_and_is_current
FAILED test_w3d_chunk_backlog.py::...::test_committed_census_decode_corpus_figures_are_dated_fresh
ERROR  test_locomotor_compiler.py::test_four_oracle_templates_are_exact
ERROR  test_locomotor_compiler.py::test_effective_retail_census
ERROR  test_locomotor_compiler.py::test_every_referenced_locomotor_resolves_to_a_compiled_template
ERROR  test_locomotor_compiler.py::test_object_normal_binding_keeps_object_speed[GondorFighter-HumanLocomotor]
ERROR  test_locomotor_compiler.py::test_object_normal_binding_keeps_object_speed[GondorTrebuchet-CatapultLocomotor]
```

Baseline `29a3dc9` on those three files: `3 failed, 50 passed, 2 skipped,
5 errors` — same names, same count. **Zero regressions; all eight pre-existing.**
`test_module_census` was the one I expected I might have broken by adding
functions to importer modules — it fails identically on the baseline, so it did
not notice and was already stale.

A process note worth recording: my first attempt at this suite reported **exit
code 0 with a zero-byte log**, because the backgrounded shell returned before
pytest ran. Exit 0 plus an empty artifact is not a pass. Re-run against the
artifact, not the exit code.

## 16. The sharing problem — spec for the next round (analysis only, no code)

Branch frozen at `5d74c28a`. Nothing here is implemented; this is the design
brief for a fresh branch after merge. Every number is derived from measurements
already in §15 and is labelled with which one.

### 16.1 What "first touch" actually is

`prepare_playable_unit_compiler` is already memoized on the **content** identity
of the corpus (`_documents_identity`) — but only *within a process*. Twenty-four
worker processes therefore each rebuild the same state from the same 792
documents / 29.1 MB, and nothing crosses between them.

Four distinct pieces, all pure functions of the corpus:

| # | What | Where | Measured |
|---|---|---|---|
| 1 | `PlayableUnitCompilerInputs` — parsed objects, command sets, command buttons, player templates, numeric/token defines + provenance, parse errors | `_prepare_playable_unit_compiler_uncached` | **11.2 s** per process (§15 probe, direct) |
| 2 | `flat_kind_cache: dict[str, tuple[IniBlock, ...]]` — **every miss re-parses every document in the corpus for one INI kind** | `_flat_blocks_for_kind` | the bulk of the ~50 s below |
| 3 | `named_definition_cache: dict[(kind, identifier), ...]` | `_named_definition_values` | tail |
| 4 | weapon nugget cache, same dict, key `("weapon-nugget:<kind>", identifier)` | `_weapon_damage_nuggets` | tail |

(2)-(4) share one dict and one `threading.Condition` on the prepared inputs, and
are explicitly documented there as "not part of identity" — which is exactly why
they are shareable.

**Envelope, from §15's probes:**

- fresh process, three retail men objects: **85.0 s**; the same three with every
  shared cache warm: **23.1 s** → **~61.9 s of first touch**, of which 11.2 s is
  (1) and therefore **~50.7 s is (2)-(4)**;
- corroborating single-object probe: `GondorArcher` **24.6 s** as the first
  compile in a process, `GondorArcherHorde` **3.9 s** immediately after.

**Stated uncertainty, and it cuts in the favourable direction.** 61.9 s came from
converting three men units. A real worker handles ~15 objects across all seven
factions and will touch more INI kinds, so 61.9 s is a **lower bound** on
per-process first touch. I am not going to use the flattering end: §16.3 uses
the lower bound, and §16.6 names the two-minute measurement that would settle it.

### 16.2 How much of the pool it is

From §15.6/§15.7 at N=24: pool wall 373.2 s, budget 24 x 373.2 = **8 957
core-s**; accounted setup 71 + plan 4 199 + convert 3 869 = 8 139; idle 818
(9.1 %).

At the lower bound, per-process first touch is 24 x 61.9 = **~1 486 core-s,
16.6 % of the pool budget**. The residual — ~7 487 core-s over 378 objects,
**~19.8 s/object** — is genuine per-object compile work that no sharing scheme
touches.

### 16.3 Predicted N=24 cold number if workers start warm

```
pool          373.2 s x (8957 - 1486)/8957  = 311 s
probe                                          9.5 s   (measured §15.6)
census                                         0.2 s   (measured)
parent tail                                    0.04 s  (measured)
process startup                               ~6 s     (measured)
                                              ------
seven-faction compiler-edit cold             ~327 s = 5.45 min
```

**~5.5 minutes. Still over the 5-minute bar, by ~27 s** — and that is with the
sharing problem solved perfectly and using the lower-bound first-touch figure.
Recovering half the 9.1 % tail idle (finer sharding on the last faction) buys
another ~17 s, landing ~310 s / 5.2 min. **The bar is not reachable by fixing
sharing alone.** What would have to give after that is the ~19.8 s/object of
real compile work, which is a different lane from anything attempted in this
report.

Worth stating for the owner: the case that is already comfortably inside the bar
is the everyday one — **repeat run 9.9 s** (§15.6). The 6.8 min case is narrowly
"the first run after editing the unit or structure compiler".

### 16.4 Option per piece, with the trust each adds

Ranked by value; (i) ship from parent, (ii) durable identity-keyed disk cache,
(iii) rebuild cheaply from a compact precomputed index.

**(2) `flat_kind_cache` — do this first. Option (ii), shape already proven.**
Each entry is "every block of kind K in the corpus", a pure function of (corpus
identity, K). That is the *same shape* as the plan-row cache that already exists
and is already trusted, keyed on `full_corpus_closure["sha256"]` (which the
convert already computes, and §15's cut 2 memoizes) plus the parsing lane's
`compiler_dependency_identity`. The kind set a run needs is small and stable, so
the parent can additionally pre-warm it once and ship the list, which removes the
concurrent-miss stampede §9.3 observed for census.
*Trust added:* one more durable artifact on the §9/§10 chain — key on the full
identity, store a content digest in the envelope, re-derive it on load, refuse on
any mismatch, and treat every refusal as a full re-parse. Same discipline, no new
kind of trust.

**(1) `PlayableUnitCompilerInputs` — option (iii), not (i).**
Shipping the live object is the wrong move: it carries a `threading.Condition`,
holds the 29.1 MB `documents` mapping, and — decisively — the compilers **fail
closed on `prepared.documents is not documents`**, an identity check, so an
unpickled copy in a worker would not satisfy it and the guard must not be
weakened to make it. The right shape is a compact, digest-sealed **derived
index** on disk (the parsed tables only, not the corpus), loaded by each worker
and re-bound to that worker's own `documents` object so the identity guard still
holds against real bytes.
*Trust added:* this is the largest new surface in the proposal, because these
tables feed the compilers **directly** rather than merely keying a cache. It
should land second, behind (2), and only with §16.5 rule 3 satisfied.

**(3)+(4) `named_definition_cache` / weapon nuggets — option (iii), and possibly
nothing at all.** These are keyed on `(kind, identifier)` — thousands of tiny
entries, which is the wrong shape for a per-entry disk cache on Windows. They are
lookups *into* the same parsed blocks (2) provides, so once (2) is shared they
should collapse on their own. **Measure after (2) before building anything for
them.**

### 16.5 Fail-closed rules the next round must not bend

The §9/§10 discipline transfers, with one genuinely new hazard:

1. **Key on the full identity chain**, never on `id()` across processes and never
   on mtime alone: corpus closure digest + catalog identity + effective-assets
   fingerprint + the parsing lane's `compiler_dependency_identity`.
2. **Verify on load**: the envelope carries its own content digest and re-derives
   it, exactly as `faction_census_cache` does with `graphSha256`. Any doubt is a
   clean miss and a full re-parse — never a corrupted index.
3. **The §9.1 type-stability trap is worse here than it was for the census
   graph.** The graph was *measured* JSON-stable — no tuples, no sets, no
   non-string keys. `IniBlock` and `SageObject` are dataclasses that **do** carry
   tuples, and `flat_kind_cache` values are `tuple[IniBlock, ...]`. A JSON round
   trip would silently turn every one into a list. For the census graph that
   would only have moved a cache key (a miss); here it would change what the
   compilers *read*, so it would change **output**. Therefore: the storage format
   must be type-preserving, the digest must be computed over a type-sensitive
   encoding, and `test_type_sensitive_canonicalization_detects_tuple_drift`
   (§11.4) must be extended to the real stored objects **before** the format is
   trusted for anything.
4. **Do not weaken the `prepared.documents is not documents` guard.** It is a
   real fail-closed check that has already caught real drift. Any shipped or
   persisted prepared form re-establishes it against the worker's own corpus
   object; it does not get relaxed to an equality check to make sharing easier.
5. **A kill switch per cache**, matching `OPENBFME_NO_OBJECT_CACHE` /
   `OPENBFME_NO_CENSUS_CACHE` / `OPENBFME_NO_COVERAGE_SHORTCIRCUIT`, so the A/B
   that proves byte-identity can be run without editing code — and so it can be
   turned off in production if it ever misbehaves.
6. **Byte-identity gate before any timing is quoted**: serial oracle vs pooled,
   retail, at least two factions, artifacts byte-for-byte — the §15.9 procedure,
   with the short-circuit disabled on both sides.

### 16.6 First measurement of the next round, before any code

Instrument `_prepare_playable_unit_compiler_uncached` and `_flat_blocks_for_kind`
with a per-process cumulative timer and a per-kind counter, run **one** pooled
seven-faction batch, and read the run-scoped worker logs (§15.5 made them
run-scoped for exactly this). That gives, directly rather than by inference:
per-process first-touch seconds, which kinds dominate, and how many distinct
kinds a worker actually touches. It replaces the lower-bound 61.9 s with a
measured figure and tells you whether §16.3's ~327 s is pessimistic. **Two
minutes of machine time, and it decides whether the rest of the work is worth
doing** — which is the same census-before-loop rule this report has had to
relearn twice.
