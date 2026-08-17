# Stage 1 — rename `.private` → `workspace/`, kill the firewall friction

Owner-approved restructure (2026-08-17). You have EXCLUSIVE access to the tree —
no other lane runs while you do. Work at repo top level. **No branches, no
worktrees.** BANNED commands: `git reset`, `git restore`, `git clean`,
`git stash`, `git commit --amend`, any checkout that touches paths you did not
create. These have destroyed concurrent-lane work before.

## Goal

The directory `.private/` is renamed to `workspace/` (internal structure
UNCHANGED — do not reorganize subdirectories), `_bfme2_extract/` becomes
`workspace/retail-extract/`, and every tracked reference to the old paths is
updated. The repo's checks pass at least as well as they did before the move.

## Steps, in order

### 0. Baseline (record BEFORE touching anything)
- `git status --porcelain > workspace-rename-baseline-status.txt` (temp file, delete before finishing)
- Run `python tools\check_pack_addresses.py` with today's env; record output.
- Record current importer baseline from `.lane-importer-full.txt` (last full run:
  45 failed / 3602 passed / 23 errors — that is the pre-existing red; your job is
  to not add to it).

### 1. Carry in-flight WIP as its own commit
Three tracked files are modified by a prior in-flight lane:
`importer/tests/test_retail_ini_coverage.py`, `tools/gate-retail.ps1`,
`tools/retail-ini-coverage.py`. Commit exactly these three first, message
`wip(coverage): carry in-flight coverage tool edits before workspace rename`,
so the rename commit is pure. Do NOT revert or edit their content in this step.

### 2. Physical moves (same volume — instant renames)
- `Move-Item .private workspace`
- `Move-Item _bfme2_extract workspace\retail-extract`
- Move the ~53 root `.lane-*` files into `workspace\logs\root-archive-2026-08\`
- `.lane-logs/` is TRACKED in git: `git rm -r --cached .lane-logs` then move its
  contents to `workspace\logs\lane-logs\`. Same for tracked root debris that is
  pure log output: `test-output.txt`, `importer-test-run.txt` (git rm --cached,
  move into workspace\logs\root-archive-2026-08\). Do NOT touch other tracked
  root files (briefs/reports stay for a later phase).

### 3. .gitignore
Replace the `/.private/` entry and both `_bfme2_extract` entries with a single
`/workspace/` entry (keep the explanatory comment, updated). Remove `.lane-*`
debris patterns if present; add `/.lane-*` to ignore any stragglers new tools
write to root.

### 4. Reference sweep (the bulk of the lane)
276 tracked files reference `.private`; 5 reference `_bfme2_extract`.
Mechanically update every tracked-file reference, all variants:
`.private/`, `.private\`, `.private\\`, bare `.private`, `_bfme2_extract`.
Replacement: `workspace` and `workspace/retail-extract` respectively.
Notable targets:
- `importer/` (113 files), `game/` (62), `tools/` (47), `docs/` (16),
  `launcher/` (7), `engine/` (3), `contracts/` (2), `.github/` (2 — the
  publication-boundary CI scan must now guard `workspace/` instead)
- All root `run_*.bat` / `import_*.bat` (the `BFME2_INSTALL` default becomes
  `<repo>\workspace\retail-work\editions\rotwk\layered-install\layer-1-bfme2`)
- Pinned python path becomes
  `workspace\retail-work\tools\python-3.12-env\Scripts\python.exe`
- `AGENTS.md`, `README.md`, `CONTRIBUTING.md`, `DIRECTION.md`: string-sweep only
  (a full rewrite happens in a later phase — do not editorialize)
- `contracts/*.json`: if a swept file carries a `policy_digest`, re-seal per
  `tools/check-product-contracts.py` in the SAME commit.
Also sweep UNTRACKED config inside `workspace/` that stores repo-relative or
absolute old paths: `workspace/INDEX.json`, `workspace/onboard.config.json`,
`workspace/retail-work/**` job/profile configs IF they reference `.private` or
`_bfme2_extract` (grep inside workspace for both strings, bounded to text/json/
ps1/py/bat files; skip pack payload bytes).

### 5. Manifest
Write `workspace/manifest.json`:
```json
{
  "retailInstall": "workspace/retail-work/editions/rotwk/layered-install/layer-1-bfme2",
  "pinnedPython": "workspace/retail-work/tools/python-3.12-env/Scripts/python.exe",
  "packsRoot": "workspace/content-packs",
  "retailExtract": "workspace/retail-extract",
  "logsRoot": "workspace/logs"
}
```
Data only — do not wire resolvers to it in this lane.

### 6. Verification (Definition of Done — all must hold)
1. `git grep -c -F ".private"` → **zero tracked hits** except `docs/patch-notes/`
   (historical release notes may keep old paths). Same for `_bfme2_extract`.
2. `python tools\check_pack_addresses.py` → same PASS as baseline
   (`packs=42 roots=2` or better).
3. `run_importer_tests.bat` (full run, ~40 min, pinned interpreter + new
   BFME2_INSTALL): failed+error count **≤ 45 failed / 23 errors** baseline.
   Log to `workspace\logs\stage1-importer-full.txt` — log to file, never tail.
4. One currently-green Godot runner passes:
   `<godot> --headless --path game --script res://tests/retail_spellbook_runner.gd`
   → expect `passed=218 failed=0` (resolve godot via `tools\resolve-godot.bat`;
   fallback `C:\Users\Jonathan\Downloads\godot47\`). Log to
   `workspace\logs\stage1-spellbook-runner.txt`.
5. `powershell -File tools\Test-DistPipeline.ps1` completes without NEW failures
   vs its pre-move behavior (it checks publish steps 1–3 in ~15 s).
6. Working tree clean except intended changes; ONE rename commit (after the WIP
   commit), message
   `refactor(layout): rename .private -> workspace, retire asset-firewall paths`.

### 7. Report
Write `orchestration/reports/stage1-workspace-rename.md`: what moved, sweep hit
counts per directory, verification outputs verbatim (the DoD numbers), anything
you could NOT update and why. Honest reds stay red — say so.

## Traps (all have bitten us)
- Bare `pytest` lies (wrong Pillow, ~40 fake failures) — pinned interpreter only.
- Published pack dirs under `workspace/content-packs/` are READ-ONLY sealed. The
  rename of the parent is fine; never chmod or edit inside a `<sha256>` dir.
- If a path falls back silently (stale durable pack in user profile), the run is
  invalid — confirm which pack root a run actually loaded.
- Zero grep hits ≠ dead: script handlers load by directory scan.
- The durable pack mirror in the user profile is OUTSIDE the repo — do not touch.
