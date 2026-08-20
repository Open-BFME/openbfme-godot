# Faction convert wall-clock optimization (men lane)

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

## 8. Not verified

- Only `men` was run, per the lane brief (another agent held `elves`). The
  seven-faction numbers are projections from measured per-faction and
  in-batch-position costs, not a measured seven-faction batch.
- The publish stage and the full `rotwk_full_content` pipeline were not run.
- No cook, pack, or selection change was made or verified.
- The plan cache has only ever been exercised on RotWK/`men`. The BFME2 lane
  and the `--plan-only` path go through `plan_faction_import`, which passes no
  cache and is therefore unchanged, but that is reasoning, not a measurement.
