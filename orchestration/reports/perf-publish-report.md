# Publish-path wall-clock optimisation — profile, changes, before/after

Lane: importer PACK-PROOF + PUBLISH speed.
Measured faction: `elves` only (another agent held `men` for convert profiling on the
same shared state root).
Date: 2026-08-20. Machine: this box, with a second agent's RotWK `men` convert running
for part of the session — see *Measurement hygiene*.

Worktree: `C:\Users\Jonathan\Desktop\open-bfme\.claude\worktrees\agent-aab664ec7e1e9602d`
Shared state root: `C:\Users\Jonathan\Desktop\open-bfme\workspace\retail-work`
Pinned python: `C:\Users\Jonathan\Desktop\open-bfme\workspace\retail-work\tools\python-3.12-env\Scripts\python.exe`

---

## 1. Headline

| Arm | elves publish, warm caches | Notes |
|---|---|---|
| HEAD (`eb8f55b`) | **244.6 s / 223.2 s** | two runs, interleaved |
| This branch | **109.3 s / 105.9 s** | two runs, interleaved |

**2.17x faster, ~126 s saved per faction.** Projected seven-faction pack proof:
**~27.3 min → ~12.6 min** (7 x median). That is a large win and it is *not* the
"couple of minutes" the brief asked for; section 6 names exactly what still stands
between here and there, with evidence.

The optimised cook is **byte-identical** to the HEAD cook: all 3,126 inventory files
match hash-for-hash (section 5).

---

## 2. Profile evidence — the suspects were wrong

The brief listed five suspects. Four of them are not where the time goes. I measured
before changing anything, by monkeypatching timers around every candidate and driving
`cli.main` in-process with exactly the argv `tools/rotwk_faction_pack_proof.py` builds.

Harness: `<worktree>/workspace/scratch/profile_publish.py` (scratch, gitignored, not committed).
Raw logs: `C:\Users\Jonathan\Desktop\open-bfme\workspace\logs\perf-publish-elves-*.log`
and `...\perf-ab-{base,opt}-{1,2}.log`.

### Suspects, adjudicated

| Suspect | Verdict | Evidence |
|---|---|---|
| (a) cold A/B reproducibility double-build | **Does not exist.** `--single-build` is a *provenance string*, nothing more | `single_build` reaches only `rebuild_execution_provenance()` (`importer/openbfme_importer/incremental_rebuild.py:382`), which writes `mode: single-build` / `attested: false`. There is no second build anywhere in `ImportPipeline.build`. Enabling the flag saves **0 s** |
| (b) hashing the tree more than once | **Real, but small** — ~13 s of 223 s | `build` audit 4.7 s + `cli.audit_pack` 5.8 s + `cli.bundle_digest` 2.3 s + publish's two passes 10.6 s |
| (c) full byte copies instead of links | **Not a sink** — 727 MB / 3,128 files copies in 2.9-4.1 s | `pipeline._copy_tree_with_digest 2.859s` |
| (d) durable-root mirror double copy | **Not in this path.** `publish-faction-to-slice` never mirrors; `publish-durable-pack.ps1` is a separate operator step | — |
| (e) per-file fsync / small-file IO | **Real, inside the conversion caches** — see below | — |

### Where the time actually went (HEAD, elves, warm caches, `perf-ab-base-2.log`)

```
 292.258s  x2844  ImportPipeline._convert_resource        (aggregate, incl. 16-wide workers)
 226.994s  x31669 sha256_file                             (aggregate)
 183.847s  x2433  _convert_resource[audio].worker
 165.828s  x1     ImportPipeline.build                    (wall)
 119.875s  x2511  _copy_media_cache_hit                   (aggregate)
 115.261s  x22    _convert_w3d_chunk                      (aggregate)
 111.446s  x175   _prepare_w3d_bundle_job                 (aggregate)
 103.627s  x87    _convert_resource[sage-particle-definition].main   <-- SERIAL, main thread
  41.875s  x1     _convert_w3d_resources                  (wall)
  31.657s  x1     compose_faction_profile                 (wall)
  19.828s  x1     _prepare_w3d_execution_tools            (wall, serial)
  11.640s  x2     audit_pack   /  5.781s cli.audit_pack  /  2.344s cli.bundle_digest
  10.562s  x1     publish_to_godot                        (wall)
 222.328s         TOTAL
```

**The single biggest sink was not in the publish machinery at all.**
`sage-particle-definition` conversion ran **87 times on the main thread, 1.19 s each,
103.6 s of a 222 s run — 47%.** Each call re-parsed a whole retail particle document
end to end to pull out *one* named definition
(`pipeline._convert_sage_particle_definition` -> `sage_particles.parse_particle_definition`).
Classic O(n·m): 87 requests, the same handful of documents, parsed 87 times.

Second: every warm conversion-cache hit read its payload **three times** — hash the
cache entry, copy it, hash the copy — for 2,511 media hits and 175 W3D hits. That is
the bulk of the 31,669 `sha256_file` calls.

Third: the cook's own bytes were fully audited and hashed **three times over** —
once inside `build`, then `cli.audit_pack`, then `cli.bundle_digest`, then again on
the publish side.

---

## 3. Changes

All in this worktree, staged by explicit path.

### 3.1 Memoise particle-document parsing — `importer/openbfme_importer/sage_particles.py`

`parse_particle_definition` now goes through an `lru_cache(maxsize=8)` wrapper keyed on
**the full source bytes**. `parse_particle_definitions` is a pure function of those
bytes and every value it returns is a frozen dataclass, so a cached result is
indistinguishable from a fresh parse. The key is the bytes themselves — not a path,
size or mtime — so there is no staleness surface to get wrong.

**103.6 s → 2.6 s.**

### 3.2 One read per conversion-cache hit — `importer/openbfme_importer/pipeline.py`

New `_copy_file_with_digest(source, destination) -> (written, sha256)`: copies and
hashes in the same pass. `_copy_media_cache_hit` and `_copy_w3d_cache_hit` now verify
with the digest of **the bytes actually written to the destination**, which is the
*stronger* of the two checks they used to run (it proves the destination, not merely
that the source was intact before the copy began). Only on a mismatch — which never
happens in a healthy run — does the code pay a second read, to keep the two diagnoses
distinct: a rotten cache entry is self-healing (discard, count a miss, convert cold),
a bad copy is still a hard `RuntimeError`.

**Media hits 119.9 s → 53.7 s aggregate; W3D hits 5.5 s.**

### 3.3 One read for W3D job staging — `pipeline._stage_w3d_sources` / `_prepare_w3d_bundle_job`

Staging copies every source into a private job root and then hashed the whole job root
again to build the conversion cache key. `_stage_w3d_sources` now hands back the
digests it computed *during* the copy, and the job uses them **only when all three
preparation steps declined to run** (`no_motion`, `secondary_skin`, `texture_overrides`
each return `None` before writing anything). Any preparation at all and the job root is
re-hashed from disk exactly as before — the cache key must describe the bytes Blender
is actually handed. The duplicate-basename collision check also stopped reading both
files end to end.

Proof it did not change the key: the optimised run still reports **175 W3D cache hits,
0 misses** — a different `source_hashes` value would have produced 175 misses.

### 3.4 Stop auditing and hashing the same cook three times — `pipeline.py`, `cli.py`

- `ImportPipeline.build` folds the pack's bundle digest out of the inventory the
  full `audit_pack(staging, light=False)` it *just ran* verified byte-for-byte, plus
  the two provenance documents the inventory excludes (hashed there and then). Nothing
  writes into staging between `audit.json` and the rename, and a rename does not touch
  bytes.
- `ImportPipeline.build_verification_for(pack_root)` hands that audit + digest to a
  caller **only** if `build()` finished that exact root in this process, and never for
  a dev/light run. `OPENBFME_FULL_REVERIFY=1` refuses the hand-over and forces the old
  behaviour everywhere.
- `cli._audit_and_address` uses it in `publish-faction-to-slice` and records how the
  answer was reached on the result as `bundle_digest_source`
  (`build-verified-this-run` / `recomputed` / `dev-skipped`), so a receipt can never
  imply a full re-verification that did not happen. `rotwk_faction_pack_proof.py`
  copies that field into the publication receipt.
- `audit_pack(..., known_digests=...)` accepts digests the caller read off the same
  tree in the same run. Publication uses it: `_bundle_digest_with_files` hashes the
  destination once, folds the address, and hands the per-file digests to the audit
  instead of the audit reading every byte again. Any path absent from the mapping is
  still hashed; the values are still compared against the provenance inventory.

**Removed: `cli.audit_pack` 5.8 s, `cli.bundle_digest` 2.3 s; publish 10.6 s → 6.8 s.**

### 3.5 `--single-build` wired on pack proof — `tools/rotwk_faction_pack_proof.py`

Added as an explicit, off-by-default, forwarding flag, with its measured value stated
in its own `--help`: **zero seconds**, because there is no second build to skip; it
only downgrades the reproducibility claim in provenance. The receipt now records
`singleBuild`. No caller passes it, and `tools/rotwk_full_content.py` was not changed
to pass it. Wiring it makes the waiver reachable and explicit rather than something
invented later under time pressure; using it to make a recook *look* faster would be
dishonest and buys nothing anyway.

---

## 4. Before / after

### 4.1 Interleaved A/B (the number to quote)

Both arms ran the same harness, alternating, so the second agent's background convert
load hit both equally. Baseline arm = a detached `git worktree` of `eb8f55b` under
`%TEMP%\obf-baseline-perf`, never inside the main tree.

```
AB base round=1 seconds=244.6 exit=0
AB opt  round=1 seconds=109.3 exit=0
AB base round=2 seconds=223.2 exit=0
AB opt  round=2 seconds=105.9 exit=0
```

| | base median | opt median | delta |
|---|---|---|---|
| elves publish, warm | 233.9 s | 107.6 s | **-126.3 s, 2.17x** |
| projected 7 factions | ~27.3 min | ~12.6 min | **-14.7 min** |

### 4.2 Uncontended single runs (same code, quieter machine)

| Run | Seconds | Log |
|---|---|---|
| HEAD, W3D cache cold (175 conversions) | 383.8 | `perf-publish-elves-before.log` |
| HEAD, all 2,686 cache hits | 188.4 | `perf-publish-elves-before-warm4.log` |
| This branch, all 2,686 cache hits | **80.0** | `perf-publish-elves-after1.log` |

### 4.3 Phase deltas (aggregate seconds, `perf-ab-base-2` -> `perf-ab-opt-2`)

| Phase | base | opt |
|---|---|---|
| `sage-particle-definition` (serial) | 103.6 | **2.6** |
| `_copy_media_cache_hit` | 119.9 | **53.7** |
| `_prepare_w3d_bundle_job` | 111.4 | **78.1** |
| `sha256_file` calls | 31,669 | **15,302** |
| `cli.audit_pack` + `cli.bundle_digest` | 8.1 | **0 (reused)** |
| `publish_to_godot` (wall) | 10.6 | **6.8** |
| TOTAL | 222.3 | **104.8** |

---

## 5. Verification of the published bundle

Same profile (`profile_sha256 fc2d3d8c…`), same coverage, same inputs, both arms:

- `conversion_cache`: **hits 2686, misses 0** in both arms — the cache keys are unchanged.
- `conversion_failures: 0`, `valid: true`, `checked_files: 3126`, `playable_unit_count: 21` in both arms.

**Content equivalence.** Comparing the two published bundles' provenance inventories:

```
base files 3126 opt files 3126
identical path set: True
content-differing inventory files: 0 []
base-only: []   opt-only: []
```

Every one of the 3,126 inventory files is hash-identical. The two bundle *addresses*
differ (`84fe67e2…` vs `af4b13cd…`) for the expected and correct reason: the importer
source changed, so `importer_recipe_sha256` changed
(`f3c72fcb…` -> `6a976ed7…`), and that lives in `provenance/manifest.json`, which is
inside the bundle digest.

**Independent address + audit check, run with the PRISTINE HEAD importer as the
verifier** (`PYTHONPATH=%TEMP%\obf-baseline-perf\importer`), so the code under test did
not grade its own work:

```json
{
  "addressHonest": true,
  "auditValid": true,
  "checkedFiles": 3126,
  "computedBundleSha256": "af4b13cdf5916ff00e23b1305413b570a5bf196ad17838c9cdc02b1c9946819c",
  "declaredAddress":     "af4b13cdf5916ff00e23b1305413b570a5bf196ad17838c9cdc02b1c9946819c",
  "errors": [],
  "verifierImporter": "C:\\Users\\Jonathan\\AppData\\Local\\Temp\\obf-baseline-perf\\importer\\openbfme_importer\\__init__.py"
}
```

**selection.json was never touched.** The shared content root
`workspace\content-packs\selection.json` still reads `8/19/2026 4:38:07 PM` and the
newest `rotwk-elves-vslice` bundle there is still `8/19/2026 2:01:40 PM`. Every publish
in this lane landed in a worktree-local content root, which is also why nothing needed
unsealing.

### Fast gates

New: `importer/tests/test_publish_fast_path.py` — 8 tests, **0.43 s**.

- the memo parses a document exactly once and returns the same objects a fresh parse does;
- different bytes are never served from the memo;
- the digest `build()` folds from the inventory equals `bundle_digest()` walking the tree;
- `_bundle_digest_with_files` agrees with `bundle_digest`;
- `audit_pack(known_digests=...)` returns the identical verdict to a full re-hash;
- **a wrong supplied digest is still reported as `hash mismatch`** (the inventory
  comparison is not bypassed);
- tampered bytes still fail a plain audit;
- `_copy_file_with_digest` reports exactly the bytes it wrote.

### Full importer suite — no regressions, judged by name

`23 failed, 3563 passed, 247 skipped, 5 errors` on this branch.
The same 23 names + 5 errors fail **identically on a pristine detached worktree of
`eb8f55b`** (`23 failed, 551 passed, 13 skipped, 5 errors` over the same five files).
They are `test_module_contracts_batch` / `test_locomotor_compiler` /
`test_module_census` / `test_w3d_chunk_backlog` oracle tests that need
`workspace/cache/effective-assets` beside the repo root, which no worktree has.
**Zero of them are mine.** This is *not* the AGENTS.md `run_importer_tests.bat`
baseline of 6/0 — that baseline is for the main tree, which has the oracle; the
comparison above is the like-for-like one.

---

## 6. What is still in the way of "a couple of minutes for all seven"

Honest remainder, per faction, from `perf-ab-opt-2.log`:

| Cost | Wall | Why it is still there |
|---|---|---|
| W3D lane (`_convert_w3d_resources`) | ~35 s | 175 jobs still stage + prepare a private job root each. The remaining hashing is in jobs where a preparation step *did* rewrite the root, so the fast path correctly declines |
| `compose_faction_profile` | ~31 s | Untouched this lane. Profiled but not attacked; the projectile-art builder re-reads the oracle INI tree |
| Blender tool attestation (`_prepare_w3d_execution_tools` + `_end_w3d_conversion_batch`) | ~25 s | Deliberate fail-closed full re-hash of the Blender tree before and after every batch. **Not weakened.** Paid once per process — and pack proof spawns one process per faction, so a recook pays it seven times |
| media cache hits | ~8 s | Already one pass |
| cook tail (inventory + audit + output hashing) | ~15 s | One audit remains, which is the one that proves the pack |
| publish | ~7 s | Copy-or-verify, one pass either way |

Two structural wins are available and were **not** taken here because both are larger
than this lane:

1. **Publish all seven factions in one process.** `rotwk_faction_pack_proof.py` shells
   out per faction, so the ~25 s Blender attestation, the ~2 s archive attestation and
   the catalog load are paid seven times — roughly **3.3 min of the projected 12.6**
   is pure per-process re-attestation.
2. **A build-level identity short-circuit.** Nothing today can know a cook would be
   identical without doing it. A recorded (resolved-profile identity + source identity
   + tool identity) -> pack-digest receipt would make an unchanged faction nearly free,
   but it is a new fail-closed surface and needs its own design and gate. Note that an
   importer source edit changes `importer_recipe_sha256` and therefore the bundle
   address, so "unchanged media" alone never means "unchanged pack".

## 7. Measurement hygiene / things I could not verify

- A second agent's RotWK `men` convert ran on this machine for part of the session.
  Absolute numbers drift with it — an early optimised run measured 80.0 s and a later
  identical one 104.4 s. That is why the headline number is the **interleaved** A/B,
  where both arms take the same load.
- Measured **`elves` only**, as instructed. The other six factions are assumed to
  behave alike; `men` is the largest and is untested here.
- The coverage reports on disk were produced by an older compiler, so every profiling
  run needed `--allow-stale-coverage --allow-incomplete-coverage`. Those waivers affect
  *which descriptors* get cooked, not how fast the cook is, and both A/B arms used
  them identically. **No production recook should use them.**
- One self-inflicted incident, repaired: my first run pointed `--install` at
  `layered-install/layer-1-bfme2` instead of `layered-install`, which made the CLI
  rebuild the shared `workspace/retail-work/catalog/rotwk.json` against `F:\BFME2` and
  refuse. Subsequent correct runs rebuilt it against the layered root; it now reads
  `install_root = ...\editions\rotwk\layered-install`, 213 archives. Verified.
- Untested by me: `rotwk_multimap_skirmish.py --build --publish`. It reaches the same
  `pipeline.build`/`publish_to_godot`/`audit_pack` code, so it inherits 3.2-3.4, but I
  did not time it.
- Untested by me: a real seven-faction `rotwk_full_content.py` run. The 12.6 min figure
  is 7 x the measured elves median, not an observation.
---

# PART TWO — concurrent seven-faction pack proof (`--publish-jobs`)

Follow-up lane on top of `e35be41`. Same worktree, same shared state root.

## 8. Headline

| Width | 7-faction pack proof, warm caches, quiet machine | vs serial |
|---|---|---|
| `--publish-jobs 1` (today's default) | **688.2 s** (11.5 min) | — |
| `--publish-jobs 4` | **266.9 s** (4.4 min) | 2.58x |
| `--publish-jobs 7` | **241.3 s** (4.0 min) | **2.85x** |

**Recommended default-recommended N: 4.** N=7 is 25 s faster on a *quiet* box, but
that gap is inside run-to-run noise (section 10), and N=4 does it with four
concurrent multi-GB cooks instead of seven. This machine has exhausted Windows
process handles under parallel load before, and the box is shared with other agents.
N=4 buys ~90% of the win for ~57% of the concurrent footprint.

`--publish-jobs` defaults to **1**, so no existing caller changes behaviour.
`tools/rotwk_full_content.py` was not modified.

**The 3 min target was not reached.** 4.0 min at N=7 is the honest floor here,
because the serial arm is only 11.5 min to begin with (Part One already took it
there from ~27 min) and the per-faction cook does not parallelise below ~150 s.

## 9. The bug this found — a silent digest divergence

The first N=1/N=4/N=7 comparison **failed**: `men` produced
`706d3efd…` at N=7 where N=1 and N=4 both produced `f9411e1b…`. Intermittent —
roughly one N=7 run in four.

The child log for the bad run named the cause immediately:

| | bad run | good run |
|---|---|---|
| `conversion_cache` | hits 3120, **misses 1**, populated 1 | hits 3121, **misses 0** |
| `checked_files` | **4795** | **4794** |
| `checked_outputs` | 4735 | 4735 |
| `provenance_entry_count` | 6124 | 6124 |
| `profile_sha256` / `importer_recipe_sha256` | identical | identical |

Every converted output was byte-identical. The pack had **one extra
non-output file**, exactly when there was one conversion-cache miss.

**Root cause.** `_copy_media_cache_hit` / `_copy_w3d_cache_hit` stage their copy
*inside the pack staging tree* as `<target>.media-cache-copying` /
`<target>.cache-copying`, then rename it into place. If the copy raises `OSError`
part way — a transient sharing violation, which concurrency makes far more likely —
the `except (FileNotFoundError, KeyError, OSError, TypeError, ValueError)` handler
discards the cache entry, counts a miss and returns, **without unlinking the
partial**. The cold conversion then writes the real output beside it, and
`_canonical_pack_inventory` swept the stray into the bundle. The audit passed
(inventory and bytes agreed), so the only symptom was a bundle that addressed to a
digest no clean run of the same inputs reproduces.

**This hazard is pre-existing, not introduced by Part One** — the original code
created the same temporary with `shutil.copyfile` inside the same `try` with the
same `except`. Concurrency is what made it fire.

**Two fixes, because the leak and the silence are separate failures:**

1. `finally`-based cleanup in both cache-hit paths. The in-flight path is tracked
   and unlinked on every exit that is not a successful `os.replace`.
2. **`_canonical_pack_inventory` now refuses** any file ending in a known in-flight
   suffix (`PACK_IN_FLIGHT_SUFFIXES` — `.cache-copying`, `.media-cache-copying`,
   `.openbfme-part`, `.tmp`). A leftover partial is now a build failure at the
   moment it happens instead of an address that quietly drifts. Fix 1 without fix 2
   would have left the next such leak just as silent.

## 10. Shared-state safety — each claim verified, not assumed

| Claim | Verdict |
|---|---|
| Pack outputs cannot collide | **True.** Each faction cooks its own `pack_id`, its own `<pack_id>.building` staging, and lands in its own immutable `<sha256>` directory |
| W3D job roots cannot collide | **True, and checked.** `jobs_root/<profile_id>/w3d/<asset>`; composed profile ids are content-hashed and distinct per faction (`faction-slice-a6a3be29…` vs `…cc91562d…`) |
| Catalog is a read-only input | **False as written — guarded.** `_load_or_build_catalog` *writes* whenever the cached catalog is stale or names another install, which is exactly the wrong-`--install` incident from Part One. The parent now resolves it ONCE up front via the `index` command (the real code path, not a reimplementation) and children run with `OPENBFME_CATALOG_NO_REBUILD=1`, which turns any child rebuild into a hard `catalog-rebuild-refused` error. That flag can only convert a silent rebuild into a refusal |
| Effective-assets tree is read-only | **True** on the publish path; it is read through `EffectiveAssetsCatalog` and the override lookup in `extract_sources` |
| Conversion-cache **writes** are atomic | **True** — `mkdtemp` + `_replace_directory_with_retry`, with an explicit peer-race branch that compares bytes and refuses non-identical output. Warm runs write nothing (`misses 0`) |
| Extraction cache writes are atomic | **False as written — fixed.** `BigArchive.extract` staged every entry through a *fixed* `<name>.openbfme-part`. Two processes extracting the same entry opened and wrote the same file, interleaved, and each renamed the result. Now process-unique (`pid` + `uuid4`); only the final name is shared, and a loser overwrites with identical bytes |
| Content-root publish lock | **Refusal, not a wait** — six of seven honest children would have died on contention *after paying a full cook*. Now a **bounded, opt-in** queue: `OPENBFME_SELECTION_LOCK_WAIT_SECONDS` (default **0** = today's instant refusal). Past the budget it raises the same error with the same message. It never deletes, steals or ignores a held lock, so a lock left by a killed process still fails the run |
| Batch report written only by the parent | **True.** Children are given a fixed argv that never carries `--output`; every receipt and the batch report are written by the parent from returned rows |
| Per-child logs are distinct | **True.** `workspace/logs/pack-proof-<runid>/<faction>.log`, full stdout+stderr, referenced from each report row |
| Diagnostics run dirs race | **Safe.** Run dirs embed `os.getpid()`; `prune_runs` swallows per-item `OSError` and the whole pass is wrapped |

### Item 3 — hoisting the per-process attestations: **declined, deliberately**

The Blender attestation compares against the pinned constant
`bootstrap.BLENDER_TREE_SHA256`. There is **no existing format for supplying an
attestation externally**, so per the brief I left it paid per child rather than
inventing a bypass. Concurrency hides it: ~25 s per child, overlapped.
`compose` and W3D staging are likewise unchanged. I did not edit the hash pin.

### Resource division

Each child otherwise sizes its converter pool from the *full* core count, so seven
children ask for seven whole machines. Concurrent runs now pass
`--conversion-jobs max(2, (cores-2)/N)` and `OPENBFME_HASH_WORKERS` likewise.

## 11. Correctness proof

Serial reference plus **four** concurrent runs (three at N=7, one at N=4), each into
a freshly emptied worktree-local content root — because the divergence was
intermittent and one clean run proves nothing:

```
faction    serialN1     parN7a       parN4        parN7b       parN7c        identical
angmar     6eb0c0fc4744 6eb0c0fc4744 6eb0c0fc4744 6eb0c0fc4744 6eb0c0fc4744  YES
dwarves    1cfc18dc2e7a 1cfc18dc2e7a 1cfc18dc2e7a 1cfc18dc2e7a 1cfc18dc2e7a  YES
elves      ccee1a7d980b ccee1a7d980b ccee1a7d980b ccee1a7d980b ccee1a7d980b  YES
isengard   6640cedff438 6640cedff438 6640cedff438 6640cedff438 6640cedff438  YES
men        2c69befc3d26 2c69befc3d26 2c69befc3d26 2c69befc3d26 2c69befc3d26  YES
mordor     2c882ce94a6f 2c882ce94a6f 2c882ce94a6f 2c882ce94a6f 2c882ce94a6f  YES
wild       875c3a1593a4 875c3a1593a4 875c3a1593a4 875c3a1593a4 875c3a1593a4  YES

factions compared: 7  mismatches: 0
```

**35 bundle digests, zero mismatches**, 7/7 publication-ready in every run.

> **Which run set:** these 35 are Part Two's concurrency stability matrix — 5 runs
> (1 serial + 4 concurrent) x 7 factions, at commit `6df2682`. Part Four's §25 count
> of 28 is a different, later set: 4 runs (1 serial + 3 overlap landing orders) x 7
> factions, at `02e104b3`. The two are not comparable and neither supersedes the
> other; they pin different properties.

Every bundle from the surviving N=7 run re-verified **by the unmodified HEAD
importer** (`PYTHONPATH=%TEMP%\obf-verify\importer`, a detached worktree of
`eb8f55b`):

```
OK   rotwk-angmar-vslice    addressHonest=True auditValid=True files=2582
OK   rotwk-dwarves-vslice   addressHonest=True auditValid=True files=2468
OK   rotwk-elves-vslice     addressHonest=True auditValid=True files=3126
OK   rotwk-isengard-vslice  addressHonest=True auditValid=True files=3203
OK   rotwk-men-vslice       addressHonest=True auditValid=True files=4794
OK   rotwk-mordor-vslice    addressHonest=True auditValid=True files=3281
OK   rotwk-wild-vslice      addressHonest=True auditValid=True files=2799
bundles checked: 7  bad: 0
```

`men` is back to **4794** files, the clean count.

`selection.json` in the shared content root is untouched — still `8/19/2026 4:38:07 PM`.
Every publish in this lane landed in a worktree-local content root.

### Gates

`importer/tests/test_publish_concurrency.py` — 11 tests, ~1.3 s:
serial default and zero-jobs rejection; the batch report/receipts are parent-only;
a forbidden child catalog rebuild raises `catalog-rebuild-refused` **and writes
nothing**; the lock still refuses instantly by default; a *bounded* budget still
refuses a lock that never clears and leaves the holder's file untouched; a waiter
acquires once the holder releases; the extraction partial name is process-unique;
**the inventory refuses each in-flight suffix**; and a media cache copy that fails
mid-way leaves nothing behind.

## 12. Wall times, all rounds — and the load confound

The other agent's `men` convert ran on this box during part of this work. It
matters a great deal, so all three rounds are given rather than the flattering one:

| Round | N=1 | N=4 | N=7 | N=1/N=7 |
|---|---|---|---|---|
| A (contended) | 1401.5 s | 260.8 s | 266.1 s | 5.27x |
| **B (quiet — the number to quote)** | **688.2 s** | **266.9 s** | **241.3 s** | **2.85x** |
| C (stability, contended again) | 1247.3 s | 356.0 s | 378.0 / 310.6 / 327.3 s | ~3.8x |

Round A's serial arm was **roughly double** round B's for identical work, and its
per-faction child times gave it away: serial children should be the *fastest*
individually (nothing competing), and they were not — `isengard` took 292 s serial
in round A versus 95 s serial in round B. **Round A's 5.4x is an artefact of a
contended serial baseline and should not be quoted.** I re-ran the whole matrix on a
quiet machine specifically to avoid shipping that number.

## 13. Still open / not verified

- **The 3 min target was not met** (4.0 min at N=7, quiet). Getting below that needs
  the per-faction cook itself to get faster, not more of them at once.
- `N=4` vs `N=7` is inside noise; the recommendation leans on footprint, not on the
  25 s.
- **Memory/IO under N=7 was not instrumented** — I measured wall time and correctness,
  not peak RSS or disk queue depth. The footprint argument for N=4 is reasoning from
  seven concurrent multi-GB cooks and this box's handle-exhaustion history, not from
  a measurement.
- `rotwk_multimap_skirmish.py --build --publish` still runs after pack proof and was
  **not** made concurrent or measured.
- The intermittent divergence was reproduced **once**, diagnosed from its child log,
  and fixed. I did not manage to reproduce it a second time before fixing, so the
  causal chain (OSError -> leaked partial -> extra inventory file) is inferred from
  the evidence rather than caught in the act. The regression tests pin both halves
  directly.
- All measurement runs required `--allow-stale-coverage --allow-incomplete-coverage
  --allow-incomplete`, because the on-disk coverage predates the current compiler.
  **No production recook should use them.** `--allow-stale-coverage` is newly
  forwardable from this batch tool; it is off by default and no caller passes it.

---

# PART THREE — sub-5-minute commission: floor cuts + convert/publish overlap

Follow-up on top of `6df2682`. Owner target: convert + publish, full cold, **under
5 minutes**, with convert being driven to ~4 min separately.

## 14. Headline — the target was NOT met, and the arithmetic says why

| | measured |
|---|---|
| Serial publish, 7 factions (before Part Three) | 688.2 s |
| Serial publish, 7 factions (after the compose cut) | **652.4 s** |
| **End-to-end, overlapped, simulated 240 s convert, N=4** | **346.2 s = 5.8 min** |
| Same, before the compose cut | 364.8 s = 6.1 min |

**5.8 min against a 5.0 min budget. Short by ~46 s.**

This was predictable from the structure and I flagged it before building: overlap
makes total wall `max over factions of (convert_i finish + publish_i)`. The last
faction's convert lands at 240 s by definition of a 4-minute convert, so the budget
allows its publish **60 s**. The measured tail publish is ~105 s. Overlap cannot
close a gap that lives entirely after the last convert finishes.

Observed landing timeline (simulated convert, evenly spread):

```
COVERAGE_LANDED men      after  36.2s -> dispatching publish
COVERAGE_LANDED elves    after  70.3s
COVERAGE_LANDED dwarves  after 104.5s
COVERAGE_LANDED isengard after 138.7s
COVERAGE_LANDED mordor   after 172.8s
COVERAGE_LANDED wild     after 206.9s
COVERAGE_LANDED angmar   after 240.9s -> last dispatch
PACK_PROOF ready=7/7 exit=0            -> 346.2s total
```

Everything up to `wild` is fully hidden behind convert. Only the last faction's
publish is exposed, and it costs ~105 s. Closing the gap needs the single-faction
floor at ~60 s — section 17 names the remaining sinks with numbers. More
concurrency will not do it.

## 15. Blender attestation — receipt hoist DECLINED, with measurements

The commission asked for a minimal signed-receipt format so children skip the tool
tree walk, *unless the walk is not the expensive part*. Measured, warm:

| attestation component | seconds |
|---|---|
| `_purge_python_caches` | 2.3 |
| `_reject_tree_links` | 1.1 |
| **`directory_tree_sha256`** (937 MB, 5,965 files) | **6.4** |
| same, parallel prototype at 4 / 8 / 16 workers | 4.6 / 4.8 / 4.7 (**1.35x only**) |

The walk is the largest single component but it is **6.4 s, not 25 s**, and it is
I/O bound — parallelising recovers 1.7 s. The commission's ~25 s is the *whole*
attestation across begin-of-batch and end-of-batch (~12–16 s + ~7–10 s).

A receipt would save ~6 s per child — **under 5% of a 150 s floor** — in exchange
for a TOCTOU window between the parent's walk and each child's use of its result,
on the one check standing between a cook and an unpinned Blender. I am not spending
a fail-closed guarantee at that exchange rate. Left paid per child; concurrency
already overlaps it. **The pin was not touched.**

One false alarm worth recording: a first probe reported `matches pin: False`. That
was my probe hashing the tree *before* the bytecode purge the real flow performs
first (`prepare_blender_portable_tree` purges, then attests). In the correct order
the digest is `81e0cfb0…` and matches `BLENDER_TREE_SHA256` exactly. No pin problem.

## 16. The compose cut that was worth taking

Profiling compose (~34 s, serial, per faction) found the sink was not compose at
all: **`retail_visual_closure._inventory_assets` walked the entire 46,130-file
effective-assets tree to answer a question about 3 projectiles and 4 textures.**

Per file it paid three filesystem round-trips — `is_symlink()`, `is_junction()`,
`stat().st_size` — roughly 120,000 syscalls, ~12 s, once per faction process.

Replaced `os.walk` + `Path` probes with `os.scandir`, whose `DirEntry` already
carries type, link status and size from the directory enumeration. **Every check is
still performed**: a path that is not a reparse point cannot be a symlink or a
junction, so the fast path and `_is_link_like` agree by construction, and anything
flagged as a reparse point falls through to the original function unchanged.

Verified against the shipped implementation, reproduced verbatim side by side:

```
legacy os.walk inventory :  12.14s  files=46130
scandir inventory        :   5.69s  files=46130
speedup                  : 2.13x
IDENTICAL TUPLE          : True
```

In-cook the saving is smaller than standalone (~5 s/faction: 688.2 s → 652.4 s over
seven) because the page cache is already warm from the same process.

## 17. What is still between here and 60 s per faction

Measured, single faction, and **not** attacked in this lane:

| sink | per faction | classification |
|---|---|---|
| `_inventory_assets` remaining | ~5.7 s | syscall-light now; next step is the sealed manifest (below) |
| `_definition_index` — decodes and regex-scans every SAGE source in the tree | ~3.8 s | same "whole tree for 3 objects" shape |
| `extend_profile_with_unit` / `_add_structure` **deepcopy per object** | ~8.4 s | quadratic: each of 43 objects deepcopies the whole growing profile (`playable_unit_import.py:1140`, `faction_slice_profile.py:275,307`). One copy up front plus in-place appends removes nearly all of it |
| Blender attestation (begin + end) | ~20–26 s | fail-closed; see §15 |
| W3D staging + prepare | ~30 s | partially cut in Part One |
| `ring_documents` rglob of the oracle INI tree | unmeasured, **Men only** | `cli.py:2875` reads every `.ini`/`.inc`, on the Men publish only |

**The highest-value unclaimed cut**: `cache/effective-assets/.openbfme/manifest.json`
is a 10.2 MB sealed document already carrying `{path, size, sha256}` for all 46,216
entries — and `_assets_root_fingerprint` already opens it for a cache key. A
manifest-backed inventory would make the walk near-free. I did **not** take it: the
walk's link-safety rejection is the one thing the manifest does not supply, and
deciding whether the tree's seal subsumes that check is a fail-closed judgement that
deserves its own lane, not a change at the end of a long session.

## 18. Overlap mode — design and refusals

`--watch-coverage` on `rotwk_faction_pack_proof.py` (off by default; nothing changes
for existing callers). Combines with `--publish-jobs`.

- **Completion signal**: `coverage_fingerprint()` = `(mtime_ns, size, aggregateSha256)`,
  and `None` for absent / unreadable / mid-write / wrong-schema / missing-aggregate.
  `None` means "not landed", never "close enough".
- **Stale coverage is refused by default.** Every faction has a *previous* run's
  coverage file on disk; treating existence as the signal would publish stale
  descriptors the instant the watcher started. A faction counts as landed only when
  its fingerprint **differs from the snapshot taken before the run began**. The
  aggregate digest is in the identity so a convert that reruns inside one mtime tick
  still registers. `--watch-accept-existing` is the explicit, loudly-announced
  opt-out for reruns and measurement.
- **Stability**: the fingerprint must hold across two consecutive polls before
  dispatch — belt-and-braces (coverage is written atomically) against dispatching a
  cook at a half-written report.
- **Dynamic dispatch, not fixed submission.** With N workers and seven waiting
  factions, submitting in order would have each of the first N block on *its*
  faction while another faction's coverage landed with no free worker — head-of-line
  blocking that defeats the whole mode. The parent watches and hands a faction to
  the pool only once it is ready.
- **A timed-out faction is a FAILED row, not a skipped one** — `assert len(rows) ==
  len(factions)`, exit 3, and the batch report accounts for all seven.
- Parent-only report and receipt writes; per-child logs unchanged.

## 19. Correctness

Serial `--publish-jobs 1` versus overlapped `--watch-coverage --publish-jobs 4`,
same branch:

```
faction    serialN1     overlapN4     identical
angmar     f7aa708734ee f7aa708734ee  YES
dwarves    e5b0c19bc898 e5b0c19bc898  YES
elves      3afc1b98a3a1 3afc1b98a3a1  YES
isengard   50582f379353 50582f379353  YES
men        b811e86a80b0 b811e86a80b0  YES
mordor     c1fb81bedb82 c1fb81bedb82  YES
wild       3048d8005170 3048d8005170  YES
factions compared: 7  mismatches: 0
```

7/7 publication-ready in both. The 266 tests covering `retail_visual_closure` and
projectile art pass. New gates in `test_publish_concurrency.py` cover the
fingerprint's five rejection cases, the same-mtime-tick rewrite, the stale-coverage
refusal, the accept-existing opt-out, a live landing, and the all-seven-rows rule.

## 20. The simulation, stated plainly

Option C is not landed, so convert completion was **simulated**:
`workspace/scratch/simulate_convert.py` rewrites each coverage document **with its
own existing bytes** at a scheduled time — content identical, only mtime moves — so
the packs stay directly comparable to a serial run while the watcher sees a faithful
"convert finished this faction" signal. Schedule: seven factions evenly spread to a
240 s finish, order `men,elves,dwarves,isengard,mordor,wild,angmar`.

**This flatters the result.** Men — the largest pack, ~150 s to publish — lands
*first* and is fully hidden. The adversarial order (Men last) would expose Men's
publish after the 240 s mark.

> **Superseded by Part Four.** At the time of writing this section that was an
> unverified prediction. It was measured in §21: men-last costs **385.7 s** against
> **311.0 s** largest-first — a 74.7 s penalty, close to the ~390 s predicted here.
> The recommendation that the convert lane emit largest-faction-first is therefore
> now an empirical result, not a guess.

Also unverified: real convert/publish contention. The simulator consumes almost no
CPU, whereas a real convert would be saturating the box while these publishes run.
**346.2 s is an optimistic floor, not a prediction.**

---

# PART FOUR — final commission: two fold cuts, a refusal, three landing orders

On top of `ae2dc02`. Last commission before freeze.

## 21. Final numbers

Serial publish, seven factions, as the cuts landed:

| build | serial 7-faction publish |
|---|---|
| before any Part-Three/Four cuts | 688.2 s |
| + scandir inventory (Part Three) | 652.4 s |
| **+ deepcopy fold + definition-index prefilter (Part Four)** | **579.5 s** |

End-to-end, publish overlapped against a simulated 240 s convert, `--publish-jobs 4`,
**all three landing orders**:

| landing order | end-to-end | vs 5 min budget |
|---|---|---|
| men-first (as in Part Three) | **313.4 s** = 5.22 min | over by 13 s |
| **largest-first** (the order the convert lane is being told to emit) | **311.0 s** = 5.18 min | **over by 11 s** |
| men-LAST worst case | **385.7 s** = 6.43 min | over by 86 s |

Part Three measured 346.2 s men-first; the two Part-Four cuts took that to 313.4 s.

**The 5-minute target is still not met — but it is now missed by 11 seconds, not 46.**

Largest-first and men-first are within noise of each other for the obvious reason:
both put Men, the dominant pack, first, so it is fully hidden behind convert either
way. The ordering that matters is the *negative* result — **men-last costs 74.7 s
more than largest-first**. That is the empirical case for the convert lane emitting
largest-faction-first, and it is now measured rather than argued.

The residual gap is exactly the shape named in Part Three: total is
`convert_finish + publish_of_last_faction`. With convert at 240 s the budget allows
the tail publish 60 s; largest-first leaves `dwarves` (the smallest, ~80 s) exposed.
Closing the last 11 s means either the convert lane finishing a few seconds early,
or the smallest faction's publish dropping under ~60 s.

## 22. Cut 1 — the deep-copy fold (~12 s/faction, 3.1–4.2x on compose)

Composing a faction is a fold: each converted object extends the profile the last
one returned. All three extenders deep-copied the entire growing document on entry,
so the profile was re-copied once per object — 43–59 times, quadratic in the
document's own size.

`compose_faction_profile` already deep-copies the base before the fold begins
(`faction_slice_profile.py:425`), so the accumulator is private to that function.
The three extenders now take a keyword-only `copy: bool = True` — **the default is
today's exact contract**, and every external caller (`playable_unit_import.py:1403`)
is untouched — and only the fold passes `copy=False`.

Proven by composing all seven factions **both ways in one process**, against the
shipped semantics restored by monkeypatch:

```
men       legacy= 18.38s  inplace=  4.42s  speedup=4.16x  identical=YES
elves     legacy= 14.08s  inplace=  3.45s  speedup=4.08x  identical=YES
dwarves   legacy= 17.89s  inplace=  4.53s  speedup=3.95x  identical=YES
isengard  legacy= 15.31s  inplace=  4.77s  speedup=3.21x  identical=YES
mordor    legacy= 20.14s  inplace=  5.08s  speedup=3.97x  identical=YES
wild      legacy= 17.08s  inplace=  4.12s  speedup=4.14x  identical=YES
angmar    legacy= 15.88s  inplace=  5.12s  speedup=3.10x  identical=YES
factions compared: 7  mismatches: 0
```

**Honest cost, documented in the docstring**: the in-place mode gives up exception
safety. A mid-fold failure can leave a partially extended profile behind. Every
failure there aborts the whole compose, so that value is unreachable today — but a
future caller must not assume otherwise.

## 23. Cut 2 — the definition-index prefilter (5.62 s → 3.38 s)

`_definition_index` decoded and regex-scanned every line of a 920-file INI corpus.
`strip_sage_comments` only ever *removes* characters, so a line whose raw form does
not contain `"object"` cannot contain it after stripping, and `_OBJECT_HEADER`
cannot match without it. Skipping those lines is a **superset filter, not a
semantic change**. Byte-identical index (4,683 keys), 1.66x.

## 24. Manifest-backed `_inventory_assets` — REFUSED, with the citation

Both decision criteria fail.

**(a) The batch-start verification does not bind the manifest to the tree bytes.**
`effective_assets_identity.py:394` gates the byte check behind
`if verify != "manifest":`. All three cook call sites — `pipeline.py:3454`, `3482`,
`3619` — pass no `verify=` argument and therefore take the `"manifest"` default. In
that mode `_verify_tree_bytes` **is never called**: not one file is statted, not one
byte hashed, and the `os.walk` that looks for unexpected entries never runs. The
verification reads the manifest document and the tree's recorded edition and catalog
identity, and trusts them.

**(b) Subsumption therefore fails too.** With no re-walk at batch start, a reparse
point inserted after sealing would not be caught by the identity check. And the
"retain it cheaply" escape is already satisfied by what shipped in Part Three: the
scandir walk takes link status from the directory enumeration at zero syscalls, so
there is no link-check cost left to remove.

The prize was also smaller than assumed. The residual 5.69 s is Python-level work —
46,130 `safe_relative_parts` validations and 46,130 frozen dataclasses — which a
manifest-backed version must still do; it would trade a scandir enumeration for
parsing a 10.2 MB JSON and building the same objects. Perhaps 2 s, for the only
thing that currently notices a post-seal reparse point in a default-mode cook.

**Walk kept.** Separately, the gap this exposed — batch-start verification trusting
the sealed manifest without binding it to tree bytes — is worth a queue row in its
own right.

## 25. Correctness

Serial `--publish-jobs 1` against all three overlapped orders, same branch:

```
faction    serialN1     menfirst     largestfirst menlast       identical
angmar     a72f6259cbc5 a72f6259cbc5 a72f6259cbc5 a72f6259cbc5  YES
dwarves    0cc07f48785c 0cc07f48785c 0cc07f48785c 0cc07f48785c  YES
elves      38feeaa9b1c5 38feeaa9b1c5 38feeaa9b1c5 38feeaa9b1c5  YES
isengard   d8698cb92396 d8698cb92396 d8698cb92396 d8698cb92396  YES
men        d97f03f0e3ec d97f03f0e3ec d97f03f0e3ec d97f03f0e3ec  YES
mordor     c28faff68e33 c28faff68e33 c28faff68e33 c28faff68e33  YES
wild       7d37fd319a2b 7d37fd319a2b 7d37fd319a2b 7d37fd319a2b  YES
factions compared: 7  mismatches: 0
```

**28 digests, zero mismatches**, 7/7 publication-ready in every run — including the
worst-case order, where the fold and the watcher are under the most pressure.

> **Which run set:** these 28 are Part Four's landing-order matrix — 4 runs
> (1 serial + men-first + largest-first + men-last) x 7 factions, at `02e104b3`.
> Part Two's §11 count of 35 is the earlier concurrency stability matrix (5 runs x 7)
> at `6df2682`. Different sets, different properties; neither supersedes the other.

## 26. Isolation and hygiene for this round

- **Coverage root isolated.** The simulator *rewrites* coverage documents, so both
  drivers now read `--coverage-root <worktree>\workspace\iso\faction-import` (a
  1,111-file, 156 MB private copy) and write their reports under
  `<worktree>\workspace\iso\`. The shared coverage tree is no longer touched by a
  measurement.
- **Content root** was already worktree-local; audited across all 12 earlier runs
  and every run in this round.
- **Conversion caches stay shared deliberately** — they are the product under test,
  an isolated empty cache would make every run a cold Blender cook, and a warm run
  writes nothing (`misses 0`).
- **Orphaned temporaries swept**: three found under the coverage tree, two dating
  from 8/3 — a standing convert-side leak, not this lane's. All removed; sweep now
  returns zero.
- Measurements ran in a granted window with the other heavy lane paused; the foreign
  burst was confirmed cleared (42 python processes → 2, 23% CPU) **before** the storm
  started, because an absolute number judged against a budget cannot be rescued by
  A/B interleaving the way a ratio can.

## 27. Where this leaves the sub-5-minute goal

Not met: **5.18 min** at best, against 5.00. The remaining 11 s is not hiding in the
publish path's redundant work any more — Parts One through Four removed 688 s → 579 s
of serial publish and 2.9x of per-faction duplicate reads. What is left is genuine:
Blender tool attestation (~20–26 s, fail-closed, correctly untouched), W3D staging,
and the irreducible cook of the one faction that converts last.

The three levers that remain, none of them mine to pull unilaterally:

1. **Convert emits largest-first** — worth 74.7 s versus men-last, now measured.
2. **Convert finishing under 240 s** — every second off convert is a second off the
   total, one-for-one.
3. **The residual per-faction floor** — the deepcopy fold was the last cheap win;
   further cuts mean the fail-closed attestation or the W3D lane.

---

# PART FIVE — pre-merge fixes from verification

Three fixes on top of `02e104b3`. No other changes; no re-measurement (none of
these alters a cooked byte, and the digest sets in §11 and §25 stand).

## 28. A late faction could destroy the whole batch report (behavioural)

`_await_coverage` was called in `proof_one` **before `row` existed and outside the
`try`**. Because `_await_coverage` requires the fingerprint to hold across two
consecutive polls, a faction landing within one poll interval of the deadline still
raises `TimeoutError` — and from there it escaped `future.result()` and killed
`main()`, discarding the batch report **including the rows of every faction that had
already succeeded**. That directly contradicted this lane's own rule that a
timed-out faction is a failed row, never a silent omission.

Fixed twice, because the trigger and the blast radius are separate problems:

1. The call moved **inside** `proof_one`'s `try`, after `row` is built, so a late
   faction becomes a failed row like any other failure.
2. New `collect_row()` wraps every `future.result()` — in both the watch dispatcher
   and the plain concurrent path — so *any* escaping worker exception costs that
   faction's row and nothing else. The batch report is the artifact of record.

Three gates: the deadline-boundary raise (the precondition), a structural assertion
that `_await_coverage` sits after `try:` inside `proof_one` (fails on the old
ordering), and a `collect_row` test proving a raising worker becomes a failed row
while a healthy one passes through untouched.

## 29. A comment that argued from a false premise

The `_definition_index` prefilter comment claimed `strip_sage_comments` "only ever
REMOVES characters". **That is false** — `sage_cst.py:449` (`value += quote`)
appends one in the malformed-unterminated-quote case.

The conclusion survives on a stronger invariant, now stated in the comment: every
removal path returns `raw[:index].rstrip()` or `raw.rstrip()`, so the result is
always a contiguous **prefix** of the raw line, and the only character that can be
appended is `"` or `'`. `"object"` contains no quote character, so a match can never
straddle the appended one — it must lie wholly inside the prefix, hence inside the
raw line. The letters are ASCII, which casefolds one-for-one, so this holds
case-insensitively.

Re-verified independently at this function's own scope (post `maps/` and
`_contains_object_header` filters): **497 files, 546,504 lines, 5,176 header
matches, 0 prefilter misses, append branch fired 0 times.** A wider-scope check
reported different file and line totals (683 / 921,786 / 5,148) but the same two
load-bearing results — 0 misses, 0 appends. The counts differ by corpus scoping;
the soundness result does not.

## 30. Report reconciliation

- §20's "I did not measure the adversarial order (Men last)" was stale once §21
  measured it. It now carries a superseded-by note pointing at the 385.7 s vs
  311.0 s result, and records that its own ~390 s prediction proved close.
- The **35** and **28** digest counts describe different run sets and each now says
  so: 35 = Part Two's stability matrix (5 runs x 7 factions, `6df2682`); 28 =
  Part Four's landing-order matrix (4 runs x 7 factions, `02e104b3`).

