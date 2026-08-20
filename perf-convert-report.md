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

## 6. Not verified

- Only `men` was run, per the lane brief (another agent held `elves`). The
  seven-faction numbers above are projections from the measured per-faction
  and in-batch-position costs, not a measured seven-faction batch.
- The publish stage and the full `rotwk_full_content` pipeline were not run.
- No cook, pack, or selection change was made or verified.
