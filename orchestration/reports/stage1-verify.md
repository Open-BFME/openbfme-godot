# Stage 1 workspace-rename — adversarial verification

Verifier: fresh-context Opus 5 review of `orchestration/briefs/stage1-workspace-rename.md`.
Baseline commit: `7b503ea`. HEAD at verification: `17d29f5`.
All commands re-run by the verifier; no implementor output was trusted.

## DoD table

| # | Criterion | Verdict | Evidence |
|---|-----------|---------|----------|
| 1 | Zero tracked `.private` / `_bfme2_extract` outside `docs/patch-notes/` | **FAIL** | 60 tracked files still match `.private`, 3 match `_bfme2_extract` outside patch-notes. Includes 36 newly-tracked files under `game/.private/`, newly-tracked agent transcripts `.codex-log.txt` / `.codex-brief.md`, un-swept `FINAL_STATUS.txt` and `gate-wiring-final-brief.md`, and 4 Python files that the fix sweep re-corrupted back to `.private_root`. |
| 2 | `check_pack_addresses.py` PASS `packs=42 roots=2` | **PASS** | `PACK_ADDRESS_CHECK PASS packs=42 roots=2` (exit 0) |
| 3 | Full importer run ≤ 45 failed / 23 errors | **PASS on the letter, FAIL on intent** | `= 23 failed, 3708 passed, 17 skipped, 2 warnings, 6 errors, 975 subtests passed in 2388.91s (0:39:48) =`. Under threshold, but **11 of those 23 failures were GREEN at baseline** — they are new regressions this lane caused, masked by an aggregate that improved for unrelated reasons. Log: `workspace/logs/stage1-verify-importer-full.txt` |
| 4 | `retail_spellbook_runner.gd` → `passed=218 failed=0` | **PASS** | `RETAIL_SPELLBOOK_RESULT passed=218 failed=0`, `RUNNER_EXIT=0`. Pack provenance confirmed non-stale: `[ModLoader] content source=external active=.../workspace/content-packs/rotwk-men-vslice/3be646b0...` Log: `workspace/logs/stage1-verify-spellbook.txt` |
| 5 | `Test-DistPipeline.ps1` no new failures | **PASS** | `DIST PIPELINE GATE PASSED - 25 checks`, exit 0. Log: `workspace/logs/stage1-verify-distpipeline.txt` |
| 6 | Clean tree, one rename commit after the WIP commit | **FAIL** | 6 commits, not 2. Working tree is clean, but the rename commit tracked 71 files that were never tracked at `7b503ea`. |

## New importer regressions (green at baseline, red now)

Baseline `workspace/logs/root-archive-2026-08/.lane-importer-full.txt` (UTF-16) has
45 FAILED / 23 ERROR and contains **none** of the following:

- `test_product_contracts.py::test_tracked_product_contracts_are_bound_and_fail_closed` — contracts swept but `policy_digest` never re-sealed (brief step 4 required re-sealing in the same commit). `tools/check-product-contracts.py` now raises `ValueError: product contract: policy digest mismatch`.
- `test_rotwk_faction_convert_batch.py` (2 tests) — module is a syntax error.
- `test_rotwk_faction_pack_proof.py` (2 tests) — module is a syntax error.
- `test_rotwk_full_content.py` (5 tests) — module is a syntax error.

## Latent corruption the hand-fixes missed

Root cause: the repo already contained a legitimate identifier `workspace_root`
(the importer's per-game state root, unrelated to `.private`). The corrective
reverse sweep `workspace` → `.private` destroyed it.

Compile check over every tracked `.py` with the pinned interpreter — 4 files fail:

1. `tools/rotwk_faction_convert_batch.py:46,58` — `.private_root` → `workspace_root`
2. `tools/rotwk_faction_pack_proof.py:29,50,270` — `.private_root` → `workspace_root`
3. `tools/rotwk_full_content.py:23,91,142,211,338` — `.private_root` → `workspace_root`
4. `tools/seal_published_packs.py`
   - `:40` `def default_.private_root()` → `def default_workspace_root()`
   - `:80` `parser.add_argument("--.private-root", ..., default=default_.private_root())` → `"--workspace-root"`, `default_workspace_root()`
   - `:97` `args..private_root` → `args.workspace_root`

Non-fatal but wrong (no syntax error, so no test caught them):

5. `tools/repair_goal_pack_map_scripts.py:81` — `The runtime is .private-first:` → `workspace-first`
6. `tools/test-private-workspace.ps1:93` — `"workspace/retail-work/private-.private-test.probe"` → `private-workspace-test.probe`
7. `tools/test-private-workspace.ps1:95` — `"The repository workspace workspace is not ignored by git."` (doubled word)
8. `tools/test-wotr-data-staging.ps1:88` — `A .private-shaped directory` → `A workspace-shaped directory`
9. `game/src/core/diag_log.gd:485` — `"...the repository workspace workspace/content-packs selection won."` (doubled word; original read `the repository workspace .private/content-packs`)
10. `tools/orphan-runners-manifest.csv` — captured evidence rows corrupted in place: `.private_content_runner.gd` (should be `workspace_content_runner.gd`), `.private-content-test-6114918`, `envless_launch_uses_.private_selection`, `broken_.private_falls_back_to_durable`, `broken_.private_fallback_diagnosed`, `selectionless_.private_diagnosed`, `selectionless_.private_still_falls_back`, `.private_selection_preferred`, `.private-pack`. This is an evidence artifact whose contents are now falsified.

Verified NOT corrupted: `plan.private_plan_sha256` / `private_hash_basis` call sites in
`importer/openbfme_importer/w3d_*.py`, and the `openbfme.private-hud-*` schema string
constants in `importer/` and `game/src/retail_slice/retail_hud_wnd_runtime.gd` — these
legitimately contain `.private` and correctly survived. All tracked JSON still parses.

## Tree pollution (asset-firewall relevant)

`git diff --diff-filter=A 7b503ea..HEAD` = **71 newly-tracked files**, none authorized by the brief:

- `game/.private/**` — 36 files, **0 tracked at `7b503ea`**. Includes 24 PNG captures under `game/.private/scratch/jobs/wotr-player-command-loop/capture/` and scratch JSON/logs. Commit `798ad36` removed them; commit `72a5665` **re-added all 36**. Net effect at HEAD: tracked.
  - Cause: `.gitignore` previously ignored `game/.private/`; the sweep rewrote that rule to `game/workspace/`, un-ignoring the real directory, and a blanket `git add` then absorbed it.
- `.codex-log.txt` (the implementor's own agent transcript, ~4k lines), `.codex-brief.md`, `FAILURE_TRIAGE_TABLE.md`, `FINAL_STATUS.txt`, `PHANTOMS.md`.
- ~30 `game/**/*.gd.uid` files (Godot-generated; arguably legitimate, but unrelated to this lane).

Confirmed clean: `git ls-files workspace` → **0** tracked paths under `workspace/`.
`git status --porcelain` → empty, before and after all verification runs.

## .gitignore

- `/workspace/` present — PASS
- `/dist/` present — PASS
- `build/` present — PASS
- `workspace/retail-extract/` + `**/workspace/retail-extract/` present — PASS
- **`/.lane-*` MISSING** — brief step 3 explicitly required it. No `.lane-*` files are tracked and none remain at root today, so this is latent, not active.
- `game/workspace/` rule is now dead weight and `game/.private/` is no longer ignored (see pollution above).

## Verdict

**FIX-FIRST.**

The physical move, the manifest, the batch-file sweep, the pack-address gate, the
spellbook runner and the dist pipeline are all genuinely good. But the lane ships
four Python files that do not compile, 11 new importer failures, a broken product-contract
digest, and 71 files of untracked debris — including 24 game captures — committed
into git because the sweep disabled the ignore rule protecting them.

Required before acceptance:

1. Restore `workspace_root` in the 4 broken files (items 1–4 above); confirm with
   `python -m py_compile` over all tracked `.py` using the pinned interpreter.
2. Re-seal `contracts/bfme2-106-product-scope.json` and `contracts/rotwk-201-product-scope.json`
   per `tools/check-product-contracts.py`; the tool must exit 0.
3. `git rm -r --cached game/.private` and restore a `game/.private/` ignore rule
   (keep `game/workspace/` too). Untrack `.codex-log.txt`, `.codex-brief.md`,
   `FAILURE_TRIAGE_TABLE.md`, `FINAL_STATUS.txt`, `PHANTOMS.md` unless the owner
   wants them.
4. Fix the cosmetic/string corruptions (items 5–10), including the falsified
   `tools/orphan-runners-manifest.csv` evidence rows.
5. Add `/.lane-*` to `.gitignore`.
6. Sweep `FINAL_STATUS.txt` and `gate-wiring-final-brief.md` for the remaining
   `.private` path references, or delete them if they are stale debris.
7. Re-run the importer suite and require the 11 named tests to be green — the
   aggregate threshold is not a sufficient gate, as this lane demonstrates.

## What I could not verify (round 1)

- Whether the 23 remaining importer failures / 6 errors other than the 11 named
  regressions are all pre-existing: I confirmed the 11 by name against the baseline
  log but did not diff the full failure sets, because the baseline's own 45/23 set
  is largely disjoint (its 23 errors were `test_castle_fixtures_oracle.py`, which
  passes now).
- Only one Godot runner (the DoD-specified spellbook runner) was executed. Other
  runners that read swept paths were not exercised.
- `tools/gate-rotwk-systems.ps1` and `tools/rotwk-systems.ps1` call the broken
  Python entry points and are therefore certainly broken, but I did not run them.

---

# Round 2 verification — commit `6980249`

Fix contract: `orchestration/briefs/stage1-fixes.md`. No `orchestration/reports/stage1-fixes.md`
was written. Verifier re-ran everything below; nothing was taken on the implementor's word.

## Round-2 results

| Item | Verdict | Evidence |
|---|---|---|
| 1. Targeted pytest on the 4 regression modules (implementor SKIPPED this) | **10 of 11 green, 1 red** | `= 1 failed, 11 passed in 0.72s =`. Green: all 5 `test_rotwk_full_content`, both `test_rotwk_faction_convert_batch`, both `test_rotwk_faction_pack_proof`. Red: `test_product_contracts.py::test_tracked_product_contracts_are_bound_and_fail_closed`. Log: `workspace/logs/stage1-verify-r2-targeted.txt` |
| 2. Contract digest — implementor calls it "a tool compatibility issue" | **FALSE. It is a wrong value.** | See diagnosis below |
| 3a. `py_compile` over every tracked `.py` | **PASS** | Zero failures (round 1 had 4) |
| 3b. `git ls-files workspace` / `game/workspace` / `game/.private` | **PASS** | 0 / 0 / 0 |
| 3c. `orphan-runners-manifest.csv` restoration | **PASS** | `cmp` byte-identical to `7b503ea` |
| 3d. Untracked-file bytes preserved on disk | **PASS** | `game/workspace/` still holds 36 files incl. 25 captures; `.codex-log.txt`, `.codex-brief.md`, `FINAL_STATUS.txt`, `PHANTOMS.md`, `FAILURE_TRIAGE_TABLE.md`, `gate-wiring-final-brief.md` all present, all untracked |
| 3e. `/.lane-*` in `.gitignore` | **PASS** | line 63 |
| 3f. `check_pack_addresses.py` | **PASS** | `PACK_ADDRESS_CHECK PASS packs=42 roots=2` |
| 3g. Tracked `.private` / `_bfme2_extract` hits | **MOSTLY PASS, 2 real regressions** | see below |

## Item 2 — digest diagnosis (the implementor's explanation is wrong)

The tool's recipe (`tools/check-product-contracts.py:25-33`) is unambiguous: take the whole
document, replace `policy_digest` with a copy that has `value` removed (all other
`policy_digest` fields stay), then
`json.dumps(..., sort_keys=True, separators=(",",":"), ensure_ascii=False)`, UTF-8, SHA-256.
It is whitespace- and key-order-independent, so reformatting the file cannot change it.

Measured on the working tree:

- `contracts/openbfme-modding-contract.json` — stored == computed. Correct.
- The embedded binding in `product_domains[openbfme-modern-modding].policy_contract.policy_digest`
  == the modding digest. Correct.
- **`contracts/bfme2-106-product-scope.json` — stored == computed (`f336693b...`). Correctly resealed.**
- `contracts/rotwk-201-product-scope.json` — stored `fb82e9d2474d0b28280888fc10e1687cdc8bc2946d7b350c3bb25d10a747ae52`,
  computed **`2a4f3fa12aecadbe64ca73064d696e0d67fe0c33ab3499decdc663cd7d8108c5`**. Mismatch.

Because bfme2-106 does verify under the tool's own recipe, the recipe is demonstrably usable
and there is no tool compatibility issue. The defect is isolated to one string in one file.

I brute-forced seven plausible alternative recipes against the stored value —
`ensure_ascii=True`, dropping the `policy_digest` key entirely, empty-string `value`, unsorted
keys, `indent=2`, raw file bytes, and hashing with the stale baseline value left in the
envelope. **None reproduces `fb82e9d2...`.** It also is not the baseline digest (`fc6ac92c...`,
which was self-consistent at `7b503ea`). So the stored value corresponds to no state this
document has ever been in; it was computed against something else or not computed at all.

`git show 6980249 -- contracts/rotwk-201-product-scope.json` is a **single-line change**: the
`value` string only. No content changed, which is why the computed digest is identical at
`17d29f5` and at HEAD.

**Correct reseal procedure** (no new tooling needed):

1. Finish all content edits to `contracts/rotwk-201-product-scope.json` first.
2. Reseal the modding contract first if it changed, then copy its digest into
   `product_domains[openbfme-modern-modding].policy_contract.policy_digest` — that field sits
   inside the product envelope, so it must be settled before step 3
   (`check-product-contracts.py:143-152`).
3. Compute with the tool's own function and write the result into `policy_digest.value`.
   Load the JSON, shallow-copy the document, shallow-copy `policy_digest`, `pop("value")`,
   reassign, then
   `hashlib.sha256(json.dumps(env, sort_keys=True, separators=(",",":"), ensure_ascii=False).encode("utf-8")).hexdigest()`.
   Importing `_policy_digest` from `tools/check-product-contracts.py` directly is the
   least error-prone route.
4. For this tree that yields `2a4f3fa12aecadbe64ca73064d696e0d67fe0c33ab3499decdc663cd7d8108c5`.
5. Verify with `python tools/check-product-contracts.py` (must print `PRODUCT_CONTRACTS PASS`).

Note `contracts/bfme2-106-product-scope.json` is **not** validated by the tool at all
(`check-product-contracts.py:15` marks it superseded). Round 2 reformatted ~200 lines of it
for no functional gain; harmless but unreviewed churn.

## New regressions introduced BY the round-2 fix (over-correction)

The implementor restored several files wholesale from `7b503ea` instead of surgically
reverting only the corrupted identifier. That undid the intended rename.

1. **`tools/seal_published_packs.py` is byte-identical to `7b503ea`** — `git diff --numstat 7b503ea..HEAD`
   reports no change at all. Line 41 reads `return REPO_ROOT / ".private" / "content-packs"`.
   The identifier `default_workspace_root` is correct again, but the path points at a directory
   that no longer exists on disk. Should be `REPO_ROOT / "workspace" / "content-packs"`.
2. **`tools/test-private-workspace.ps1:93,95` reverted to `.private`** while lines 5/40 kept
   `workspace` — the file is now internally inconsistent, and it **fails when run**:
   `At tools\test-private-workspace.ps1:95 char:9 + throw "The repository .private workspace is not ignored by gi ...`, `EXIT=1`.
   Verified empirically: `git check-ignore -q -- ".private/retail-work/..."` exits 1 (not ignored,
   because `.gitignore` no longer names `.private`), whereas the `workspace/retail-work/...`
   probe exits 0. This script was green at baseline; it is red now.

Correctly fixed and confirmed carrying the rename: `tools/rotwk_full_content.py`,
`tools/rotwk_faction_convert_batch.py`, `tools/rotwk_faction_pack_proof.py`,
`tools/repair_goal_pack_map_scripts.py`, `tools/test-wotr-data-staging.ps1`,
`game/src/core/diag_log.gd` (doubled word gone).

## Remaining tracked `.private` hits — classification

Acceptable, no action:

- 22 `importer/openbfme_importer/*.py` + 8 `importer/tests/*.py` + 4 `game/**/*.gd` — these are
  `plan.private_plan_sha256` attributes and `openbfme.private-hud-*` schema string constants.
  Genuine language/schema tokens, never paths. Unchanged from baseline.
- `tools/record-retail-window.ps1:121` — `openbfme.private-retail-window-capture` schema,
  unchanged from baseline.
- `tools/orphan-runners-manifest.csv` — captured evidence from 2026-08-15, restored byte-identical
  to baseline. A pre-rename capture legitimately records pre-rename paths; rewriting it would
  falsify evidence. Correct as-is.
- `orchestration/briefs/stage1-workspace-rename.md`, `orchestration/reports/stage1-workspace-rename.md`
  (and this report) — the rename is their subject matter, so they must quote the old paths.
  These are also the only two remaining `_bfme2_extract` hits.
  **Acceptable historical evidence, not unfinished work.**

Not acceptable — the 2 regressions above (`tools/seal_published_packs.py`,
`tools/test-private-workspace.ps1`).

## Housekeeping

- Repo-root debris created by the round-2 lane: a file literally named
  `C:UsersJonathanDesktopopen-bfmeworkspacelogsstage1-fixes-final-report.txt`
  (a Windows path collapsed into a filename by a bad redirect). Untracked; should be deleted.
- Commit count is now 7 since `7b503ea` against a DoD of 2. History is noisy but recoverable.

## Round-2 verdict

**FIX-FIRST.** Real progress: every tracked Python file compiles, 10 of the 11 named
regressions are green, the evidence CSV is faithfully restored, tracked-file hygiene is clean,
and no bytes were lost. Three items remain:

1. `contracts/rotwk-201-product-scope.json` — set `policy_digest.value` to
   `2a4f3fa12aecadbe64ca73064d696e0d67fe0c33ab3499decdc663cd7d8108c5`; then
   `python tools/check-product-contracts.py` must print `PRODUCT_CONTRACTS PASS`, and
   `test_product_contracts.py` must go green. This is a wrong value, not a tool problem.
2. `tools/seal_published_packs.py:41` — `REPO_ROOT / "workspace" / "content-packs"`.
3. `tools/test-private-workspace.ps1:93,95` — `workspace/retail-work/private-workspace-test.probe`
   and drop `.private` from the throw message. Re-run the script; it must exit 0.

Optional: delete the malformed root debris file.

## What I could not verify (round 2)

- I ran only the 4 targeted modules, not the full importer suite. The round-2 edits touched
  `tools/*.py` that other suites import, so a full run is still owed before final acceptance —
  but the aggregate threshold must not be the gate (see round 1).
- I did not re-run the Godot spellbook runner or `Test-DistPipeline.ps1` after round 2; both
  were green at `17d29f5` and round 2 did not touch their inputs, but that is inference,
  not measurement.
- `tools/gate-rotwk-systems.ps1` / `tools/rotwk-systems.ps1` should now work (their Python
  entry points compile), but I did not execute them.

---

# Round 3 — final acceptance pass, commit `a1059de`

Fix report: `orchestration/reports/stage1-fixes-r2.md`. All five implementor claims verified
independently; the still-owed full importer run was executed and judged failure-by-failure.

## Acceptance criteria

| # | Criterion | Verdict | Evidence |
|---|---|---|---|
| 1a | `check-product-contracts.py` PASS | **PASS** | `PRODUCT_CONTRACTS PASS product_policy_sha256=2a4f3fa12aecadbe64ca73064d696e0d67fe0c33ab3499decdc663cd7d8108c5 modding_policy_sha256=7ef975b4...`, exit 0. Stored value equals the digest I independently computed in round 2. |
| 1b | `test_product_contracts` 2/2 green | **PASS** | `2 passed in 0.16s` |
| 1c | `test-private-workspace.ps1` exit 0 | **PASS** | `PRIVATE_WORKSPACE_TEST PASS`, `EXIT=0` |
| 1d | `seal_published_packs.py` compiles, no `.private` literals | **PASS** | `return REPO_ROOT / "workspace" / "content-packs"`; zero `.private` matches; `py_compile` OK |
| 1e | One 3-file commit, debris deleted | **PASS** | `a1059de` touches exactly `contracts/rotwk-201-product-scope.json`, `tools/seal_published_packs.py`, `tools/test-private-workspace.ps1` (4+/4-). Malformed root debris file absent. |
| 2 | Full importer: zero failures attributable to the rename/fix lanes | **FAIL** | `= 13 failed, 3718 passed, 17 skipped, 2 warnings, 6 errors, 975 subtests passed in 2409.20s (0:40:09) =`. **13 of the 19 red items are rename-caused and were green pre-rename.** Log: `workspace/logs/stage1-verify-r3-importer-full.txt` |
| 3a | Spellbook runner `passed=218 failed=0` from `workspace/content-packs` | **PASS** | `RETAIL_SPELLBOOK_RESULT passed=218 failed=0`, `RUNNER_EXIT=0`, `[ModLoader] content source=external active=.../workspace/content-packs/rotwk-men-vslice/3be646b0...` |
| 3b | `Test-DistPipeline.ps1` 25 checks | **PASS** | `DIST PIPELINE GATE PASSED - 25 checks`, exit 0 |
| 4 | No stray staged artifacts across Stage-1 commits | **FAIL** | Two log/temp artifacts are still tracked at HEAD (below) |

## Criterion 2 — failure-by-failure judgement

Set comparison across three full runs (baseline `7b503ea`, round-1 `17d29f5`, round-3 `a1059de`):

- Round-1 regressions now fixed: **all 10** (`test_product_contracts` x1, `test_rotwk_faction_convert_batch` x2, `test_rotwk_faction_pack_proof` x2, `test_rotwk_full_content` x5).
- Red in round 3 that was green in round 1: **none**. No new breakage from the fix lanes.
- *(Correction to my round-1 report: I wrote "11 regressions". The correct count is 10 — I
  miscounted by including the sibling `test_product_contracts` test that was green throughout.
  23 baseline-unrelated − 10 = 13, which matches round 3 exactly.)*

**Pre-existing, present at the pre-rename baseline — acceptable (6 failures):**
`test_playable_unit_import`, `test_rotwk_official_map_corpus`, `test_scripts_converter`,
`test_special_disguise_prerequisite`, `test_w3d_chunk_backlog` (x2).

**Rename-caused, green pre-rename — NOT acceptable (7 failures + 6 errors):**

**(A) `_private_workspace` NameError — 6 failures + 6 errors.** The sweep renamed the helper's
*definition* to `workspace_root` but left three *call sites* on the old name. A `NameError` is a
runtime fault, so `py_compile` (my round-1 gate) cannot see it — this is why it survived all
three rounds.

- `importer/tests/test_livingmap_regions.py:405` defines `workspace_root`; calls at **:434** and **:472** still say `_private_workspace()` → 6 failures
- `importer/tests/test_living_world_autoresolve_bindings.py:44` defines `workspace_root`; call at **:669** still says `_private_workspace()` → 6 setup errors

Baseline `7b503ea` had `def _private_workspace()` at line 44/405 with matching call sites, and
both files were fully green.

**(B) Hash-pin drift on a generated profile — 1 failure.**
`test_m3_pack_expansion.py::test_effective_trebuchet_contract_and_object_bindings_are_source_complete`
fails with `ValueError: private base profile identity changed`
(`importer/openbfme_importer/m3_pack_expansion.py:2775`).

Cause: brief step 4 told the lane to sweep untracked configs under `workspace/`. That sweep
rewrote **3 `virtualPath` strings** inside the hash-pinned derived artifact
`workspace/retail-work/profiles/men-fords-v0-complete.generated.json`, changing its sha256 from
`0bc2e767...` to `f7d80868...`, while the tracked pin in `importer/profiles/men-fords-v1.json`
(`baseProfile.sha256 = 0bc2e767...`) was correctly left untouched.

Proved by reconstruction: replacing `workspace/retail-work` → `.private/retail-work` in that
file (3 occurrences) reproduces `0bc2e76708d3c13b0aeac45afe375e4f120acdf329344b79d683f42e5d667c9d`
**exactly** — the pinned value.

Do **not** fix by editing the pin; that is self-oracling and the same class of trap as the
contract-digest reseal. Correct options: regenerate the profile through the importer so both the
artifact and its pin are re-derived together, or — if those `virtualPath` entries are pure
provenance labels — revert those 3 strings so the derived artifact matches its pin again.

## Criterion 4 — stray artifacts

Net tracked change `7b503ea..HEAD` is 31 added / 39 deleted. The deletions are all intended
(`.lane-*`, `.lane-logs/`, `test-output.txt`, `importer-test-run.txt`, `.codex-brief.md`).
Of the additions, 27 are `game/**/*.gd.uid` (Godot-generated UID sidecars, normally committed —
unrelated to this lane but harmless) and 2 are the lane's own brief and report (legitimate per
brief step 7). **Two are stray artifacts that must not be tracked:**

1. **`gate-wiring-final.log` — 10,104,990 bytes (10.1 MB)** of UTF-16 Codex CLI transcript,
   added by `9c19da5`. Exactly the class of artifact this criterion asks about.
2. **`workspace-rename-baseline-status.txt`** (10,988 bytes), added by `9c19da5`. Brief step 0
   explicitly designated this a temp file: *"(temp file, delete before finishing)"*.

Both blobs are already permanent in history via `9c19da5`; untracking them at HEAD is the
actionable remedy.

Working tree is otherwise clean. New untracked debris this round: `.grok-stage1-r2.log`
(ignored-by-nothing but untracked; delete at leisure).

## Round-3 verdict

**FIX-FIRST.** Four exact items:

1. `importer/tests/test_livingmap_regions.py:434` and `:472` — `_private_workspace()` → `workspace_root()`.
2. `importer/tests/test_living_world_autoresolve_bindings.py:669` — `_private_workspace()` → `workspace_root()`.
3. `workspace/retail-work/profiles/men-fords-v0-complete.generated.json` — restore identity
   `0bc2e767...` (regenerate via the importer, or revert the 3 swept `virtualPath` strings).
   Do not edit the pin in `importer/profiles/men-fords-v1.json`.
4. `git rm --cached gate-wiring-final.log workspace-rename-baseline-status.txt` (keep the bytes
   on disk if wanted; both are already covered by no ignore rule, so add them or move them under
   `workspace/logs/`).

Re-verification needed after the fix: the 4 affected test modules only
(`test_livingmap_regions`, `test_living_world_autoresolve_bindings`, `test_m3_pack_expansion`) —
minutes, not a full suite. A full run is not required again unless items 1–3 change shared code.

Everything else is genuinely done: contracts seal, all path fixes, both Godot/dist gates,
tracked-file hygiene under `workspace/`, and no regression introduced by the fix lanes.

## What I could not verify (round 3)

- I did not re-run `tools/gate-rotwk-systems.ps1` / `tools/rotwk-systems.ps1`.
- The 6 pre-existing failures were matched by test ID against the baseline log; I did not
  re-confirm that each still fails for the *same underlying reason* as pre-rename.
- `game/**/*.gd.uid` additions: I classified these as benign Godot sidecars but did not confirm
  with the owner that they belong in this commit.

---

# Round 4 — final confirmation, commit `db3491c`. **ACCEPT.**

All four round-3 items independently confirmed. Stage 1 is complete.

## Round-4 checks

| Check | Verdict | Evidence |
|---|---|---|
| 3 affected modules re-run | **PASS** | `67 passed in 8.31s`, exit 0, zero FAILED/ERROR lines (`test_livingmap_regions`, `test_living_world_autoresolve_bindings`, `test_m3_pack_expansion`) |
| No residual `_private_workspace` call sites | **PASS** | repo-wide grep over `importer/`, `tools/` → none |
| `db3491c` diff is exactly the 3 call-site renames | **PASS** | three `-    root = _private_workspace()` / `+    root = workspace_root()` hunks, nothing else in `importer/tests/` |
| Generated profile sha vs pin | **PASS** | actual `0bc2e76708d3c13b0aeac45afe375e4f120acdf329344b79d683f42e5d667c9d` == pin at `.pack.m3Recipe.baseProfile.sha256`. 0 residual `workspace/retail-work`, 3 restored `.private/retail-work`. |
| Pin file unmodified across ALL Stage-1 commits | **PASS** | `git log 7b503ea..HEAD -- importer/profiles/men-fords-v1.json` → empty. The oracle was never touched; the artifact was moved back to it. |
| Both artifacts gone from git AND disk | **PASS** | `gate-wiring-final.log` → tracked:NO disk:absent; `workspace-rename-baseline-status.txt` → tracked:NO disk:absent |
| `db3491c` touches exactly 4 paths | **PASS** | 2 test files + the 2 deleted artifacts (`Bin 10104990 -> 0`, `Bin 10988 -> 0`), 3 insertions / 3 deletions. The generated profile is untracked under `workspace/`, so its revert correctly does not appear. |

**Adversarial extra I ran unprompted:** reverting the generated profile changed a file that six
*other* modules also read, any of which could have been green in round 3 only because of the
swept content. I enumerated every consumer of `men-fords-v0-complete.generated` and ran them:
`test_retail_archer_projectile_profile`, `test_retail_fords_completion_profile`,
`test_retail_gate_script`, `test_retail_hud_external_movies_oracle`,
`test_retail_men_damage_effects`, `test_rotwk_full_content` → **`46 passed`, exit 0.** No
collateral damage.

## Final state of every original DoD criterion

| # | Original criterion | Final | Evidence |
|---|---|---|---|
| 1 | Zero tracked `.private` / `_bfme2_extract` outside `docs/patch-notes/` | **PASS** | Zero genuine `.private` *path* references remain in code. Residual `.private` tokens are `plan.private_plan_sha256` attributes and `openbfme.private-hud-*` schema constants (language tokens, unchanged from baseline), plus the restored evidence CSV. `_bfme2_extract` survives only in this lane's own brief and report, whose subject matter it is. |
| 2 | `check_pack_addresses.py` | **PASS** | `PACK_ADDRESS_CHECK PASS packs=42 roots=2` |
| 3 | Full importer ≤ baseline, no rename-caused reds | **PASS** | Last full run (`a1059de`): `13 failed, 3718 passed, 17 skipped, 6 errors`. All 13 rename-caused items proven green by targeted run at `db3491c` (67/67 + 46/46). Projected full-suite state: **6 failed, 0 errors, every one pre-existing at the pre-rename baseline** — down from `45 failed / 23 errors`. See caveat below. |
| 4 | Spellbook runner `passed=218 failed=0` | **PASS** | `RETAIL_SPELLBOOK_RESULT passed=218 failed=0`, loading from `workspace/content-packs/...` (non-stale pack confirmed) |
| 5 | `Test-DistPipeline.ps1` | **PASS** | `DIST PIPELINE GATE PASSED - 25 checks` |
| 6 | Clean tree, minimal commits | **PASS on substance, FAILED on letter** | Working tree clean; net tracked additions are exactly this lane's brief and report (plus 27 benign Godot `.gd.uid` sidecars). But the DoD asked for 2 commits and Stage 1 took **9**. History is noisy; content is correct. |

Also green: `PRODUCT_CONTRACTS PASS`, `PRIVATE_WORKSPACE_TEST PASS`, full `py_compile` sweep over
every tracked `.py`, and `git ls-files` returning 0 for `workspace/`, `game/workspace/`,
`game/.private/`.

## Verdict

**ACCEPT. Stage 1 is complete and the tree is safe for the next lane.**

The rename landed: `.private/` → `workspace/`, `_bfme2_extract/` → `workspace/retail-extract/`,
manifest written, `.gitignore` correct (`/workspace/`, `/.lane-*`, `/dist/`, `build/` all
present), no retail-derived payload tracked, and the importer is materially *greener* than
before the lane started.

## Honest caveats — carried forward, not blockers

1. **The last full-suite measurement was at `a1059de`, not `db3491c`.** The 6-failure projection
   is inference from targeted runs (113 tests across 9 modules), not a measured 40-minute run.
   The delta since that full run is 3 call-site renames, 2 file deletions, and the untracked
   profile revert — and I traced every consumer of each. Low risk, but it is a projection.
   The next lane that runs the full suite should expect **6 failed / 0 errors** and treat any
   deviation as new.
2. **The 3 `virtualPath` labels inside the generated profile now read `.private/retail-work`,
   a directory that no longer exists.** This was the correct trade — the artifact's identity is
   a pinned oracle and must not be hand-edited — but the labels are stale until the profile is
   regenerated through the importer. Worth a follow-up ticket, not a Stage-1 blocker.
3. **Pre-existing reds inherited, not introduced:** `test_playable_unit_import`,
   `test_rotwk_official_map_corpus`, `test_scripts_converter`,
   `test_special_disguise_prerequisite`, `test_w3d_chunk_backlog` (x2). Red before the rename,
   red now, unrelated to it.
4. **9 commits against a DoD of 2**, and two large artifacts (a 10.1 MB transcript and the temp
   baseline file) are permanently in history via `9c19da5` even though both are untracked at
   HEAD. Cosmetic unless the repo is ever size-audited.
5. Untracked agent debris at root (`.codex-log.txt`, `.grok-stage1-r*.log`, `FINAL_STATUS.txt`,
   `PHANTOMS.md`, `FAILURE_TRIAGE_TABLE.md`, `gate-wiring-final-brief.md`) — harmless, unignored,
   deletable at leisure.
