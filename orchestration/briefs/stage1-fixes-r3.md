# Stage 1 fixes, round 3 — final four items

Evidence: `orchestration/reports/stage1-verify.md` round-3 section. Same hard
rules as round 2: surgical edits only, no sweeps, stage by explicit path,
`git add -A` and `git reset/restore/clean/stash/amend` BANNED.

## Items

1. **Fix the three stale call sites** of the renamed workspace helper (currently
   calling `_private_workspace()`, which no longer exists — runtime `NameError`):
   - `importer/tests/test_livingmap_regions.py:434`
   - `importer/tests/test_livingmap_regions.py:472`
   - `importer/tests/test_living_world_autoresolve_bindings.py:669`
   Look at the helper's current definition in each module (or its import) to get
   the correct new name — do not guess; match the definition.

2. **Restore the generated profile's pinned identity.** The untracked file
   `workspace/retail-work/profiles/men-fords-v0-complete.generated.json` had 3
   `virtualPath` strings rewritten `\.private/retail-work` → `workspace/retail-work`,
   which moved its sha256 off the tracked pin in
   `importer/profiles/men-fords-v1.json`. The verifier proved that reverting
   those 3 strings to `.private/retail-work` reproduces the pinned
   `0bc2e767...` exactly. Revert exactly those 3 strings in the generated file.
   **Do NOT edit the pin file — that is self-oracling and is forbidden.**
   (Yes, the virtualPath strings will say `.private` — they are frozen inside a
   hash-pinned historical artifact, same rule as evidence CSVs. A future
   regeneration through the real pipeline may re-mint them; not this lane.)

3. **Untrack two stray artifacts** (git rm --cached, keep or delete bytes as
   noted): `gate-wiring-final.log` (10.1 MB codex transcript — untrack AND
   delete from disk) and `workspace-rename-baseline-status.txt` (untrack AND
   delete; the original brief designated it a temp file).

4. **Commit**: one commit, message
   `fix(layout): repair stale helper call sites, restore pinned profile identity`,
   containing exactly the two test files and the two removals. The generated
   profile is untracked — it is NOT part of the commit.

## Definition of Done (verbatim outputs in your report)

1. Pinned interpreter + BFME2_INSTALL
   (`<repo>\workspace\retail-work\editions\rotwk\layered-install\layer-1-bfme2`),
   run pytest on exactly:
   `importer/tests/test_livingmap_regions.py`
   `importer/tests/test_living_world_autoresolve_bindings.py`
   `importer/tests/test_men_fords_profile.py` (or whichever module the verifier's
   round-3 report names as the profile-identity test — check the report; run all
   three affected modules it lists)
   → zero failures/errors that were not present pre-rename per the verifier's
   report; the NameError and identity failures are gone.
2. `git ls-files | grep -iE "gate-wiring-final|baseline-status"` → no hits.
3. `git status --porcelain` clean after the commit (untracked workspace/ paths
   excepted — they are ignored).
4. One commit, exact file set as specified.

Write `orchestration/reports/stage1-fixes-r3.md` with each item and DoD result.
