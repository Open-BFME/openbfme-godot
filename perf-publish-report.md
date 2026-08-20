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
