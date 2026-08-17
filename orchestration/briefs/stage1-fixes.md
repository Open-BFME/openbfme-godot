# Stage 1 fixes — repair the rename lane's sweep damage

An adversarial verifier reviewed the Stage-1 workspace rename and returned
FIX-FIRST. Its full report is `orchestration/reports/stage1-verify.md` — READ IT
FIRST. This brief is the fix contract.

## Hard constraints (violations sink the lane)
- **NO global find-replace / sed sweeps of any kind.** Every edit is a targeted
  edit to a named file. The damage you are fixing was caused by sweeps.
- **The oracle for every disputed name is `git show 7b503ea:<path>`** (the
  pre-rename commit). If an identifier/row existed at 7b503ea containing the
  word `workspace` (e.g. `workspace_root`), it was ALWAYS `workspace` — restore
  it exactly. Only genuine references to the old DIRECTORIES `.private/` and
  `_bfme2_extract/` map to `workspace/` and `workspace/retail-extract/`.
- **Stage files explicitly by path. `git add -A` / `git add .` are BANNED.**
- BANNED as before: git reset/restore/clean/stash/amend.
- Work at repo top level, exclusive tree access.

## Fix list (from the verifier — cross-check its report for exact lines)

1. **Restore corrupted `workspace_root` identifiers** (currently `.private_root`
   nonsense; four files fail to compile under the pinned interpreter):
   - tools/rotwk_faction_convert_batch.py (lines ~46,58)
   - tools/rotwk_faction_pack_proof.py (~29,50,270)
   - tools/rotwk_full_content.py (~23,91,142,211,338)
   - tools/seal_published_packs.py (~40,80,97 — `def default_.private_root()`,
     `args..private_root`)
   Verify each against `git show 7b503ea:<path>`.
2. **Repair the six string/prose corruptions** the verifier lists, including
   `tools/orphan-runners-manifest.csv` evidence rows
   (`.private_content_runner.gd` → `workspace_content_runner.gd`, etc.) —
   restore to what 7b503ea recorded, since these are captured evidence, not
   forward-looking paths.
3. **Untrack the 71 wrongly-added files.** `git rm -r --cached` (NOT plain rm —
   keep bytes on disk) for: everything under `game/.private/` (36 files incl.
   24 PNG captures), `.codex-log.txt`, and the four root debris files the
   verifier names. Then rename the directory `game/.private` → `game/workspace`
   (physical move) so it matches the swept `game/workspace/` .gitignore rule,
   and confirm `git status` shows nothing from it.
4. **Re-seal the two product contracts**: run/inspect
   `tools/check-product-contracts.py`, regenerate each swept contract's
   `policy_digest` per that tool's convention, commit the reseal with the fix.
5. **Add the missing `/.lane-*` rule to .gitignore** (brief step 3 of the
   original lane; currently absent).
6. **Finish the reference sweep honestly**: `git grep -n -F ".private"` and
   `-F "_bfme2_extract"` over tracked files. For each hit decide by oracle rule:
   real old-directory reference → update to workspace path (targeted edit);
   legitimate identifier/prose that always said something else → restore per
   7b503ea; historical evidence under docs/patch-notes/ → leave. End state:
   zero tracked hits outside docs/patch-notes/.
7. **Do NOT touch** the 12 pre-existing importer failures, the retail_state_pin
   red, or anything outside the fix list.

## Definition of Done — all must hold, with output pasted in your report
1. Pinned interpreter `python -m compileall` (or py_compile) passes on every
   .py file changed since 7b503ea: zero errors.
2. Targeted pytest (pinned interpreter, BFME2_INSTALL set) on exactly these,
   all collected tests: importer/tests/test_product_contracts.py,
   test_rotwk_faction_convert_batch.py, test_rotwk_faction_pack_proof.py,
   test_rotwk_full_content.py → the 11 named regressions are green; any other
   failure in these files must be shown present in the baseline log
   (workspace/logs/lane-logs/ has the UTF-16 baseline; the verifier's run is
   workspace/logs/stage1-verify-importer-full.txt).
3. `python tools\check_pack_addresses.py` → PASS packs=42 roots=2.
4. `python tools\check-product-contracts.py` (adjust invocation as the tool
   requires) → no digest mismatch.
5. `git grep -c -F ".private"` / `-F "_bfme2_extract"` tracked hits outside
   docs/patch-notes/ → 0.
6. `git ls-files workspace game/workspace | wc -l` → 0; `git status --porcelain`
   clean after your commits.
7. Commits: as few as coherent (target 1–2), messages prefixed `fix(layout):`.
   No transcript/log files staged.

## Report
Write `orchestration/reports/stage1-fixes.md`: per fix-list item what you did,
per DoD item the verbatim output. Honest reds stay red — say so.
