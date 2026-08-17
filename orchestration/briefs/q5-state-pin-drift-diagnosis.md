# Q5 — diagnose the retail_state_pin drift (read-only bisect + root cause)

Repo: C:\Users\Jonathan\Desktop\open-bfme. Queue row Q5 (orchestration/queue.md).
Owner ruled 2026-08-17: diagnose BEFORE any re-mint (option A) so the eventual
re-mint names every cause. This lane is READ-ONLY on the tree except: claiming
Q5 in queue.md (owner=opus-q5), your report, and logs under workspace/logs/.
You MAY use `git worktree add` into a path OUTSIDE the repo (e.g.
%TEMP%\q5-bisect) to run historical commits — never checkout/reset inside the
main tree, another lane may be active there. Godot: tools\resolve-godot.bat
(fallback C:\Users\Jonathan\Downloads\godot47\Godot_v4.7-stable_win64_console.exe).

## Facts
- Runner: game/tests/retail_state_pin_runner.gd. `EXPECTED_HASH` (line ~177) =
  `0e4bcdbf7e9a8579ccf559f0ac3d83284413e7196ad1249d2eafd3eafd1dcadc`,
  `PIN_TICKS := 3000`. Pin last consciously minted 2026-08-04 (header ~:58).
  Runner text: "A DIFFERING HASH IS A DEFECT, NEVER A NEW BASELINE."
- Observed since at least 2026-08-16 01:33 (workspace/logs/lane-logs/
  retail_state_pin_runner.txt.err) and again 2026-08-17 04:43:
  `RETAIL_STATE_PIN FAIL behaviour moved: got f5579dd90fedea8b379403a5955491fd6b9f0f1cb6475738c4ea71c34b36fca9, pinned 0e4bcdbf…` — the drifted hash is STABLE
  (same value both days), so this is a deterministic behaviour change, not
  flakiness.
- Suspects (recent commits touching game/src/retail_slice/retail_slice_sim.gd):
  819b3e3 fix(combat): close structure armor fallback defect (+ runtime
  validation), 629b72f sim: publish live SAGE weapon-cycle model conditions,
  3dbbc9f fix(sim): keep lazy contract misses state-neutral, 604fc71 bind
  capturable armor contracts exactly, 2bbcfe6 execute RefundDie contracts,
  ebc60aa cancel hostile weather by source, d78a8f3 rank-ladder spell points,
  6fa4bb6 IncomeInterval consumer, 3c67c7c typed retail contracts. Also every
  recent runner log floods `retail_slice_sim.gd:_record_structure_armor_provisionals`:
  "structure armor: kind 'barracks'/'stable'/'farm'/... has no compiled armor
  contract; structure damage will be refused" and `_add_battalion`: "missing
  selected-pack unit rule for bfme2.object.gondor-archer/..." — a fixture-floor
  defect that could itself move a combat-covering pin.
- The pin runner's fixture is deliberately self-contained (duplicated from
  retail_lockstep_determinism_runner.gd) — check whether it reads the selected
  content pack at all; if it does, selection changes (Q1/Q2 lane may have
  changed selection today — check `git log -- tools/gate-retail.ps1` and
  workspace/content-packs/selection.json mtime) are ALSO a suspect and must be
  separated from code causes by running the runner with OPENBFME_CONTENT
  pointed at a pack root that matches the 08-04 selection if you can identify it,
  or by noting the dependency explicitly.

## Method
1. Confirm current state: run the runner at HEAD (log to workspace/logs/
   q5-pin-HEAD.txt), record hash. Run it TWICE to prove stability.
2. Find the last green commit: check out the 2026-08-04 mint commit (find via
   `git log -S"0e4bcdbf7e9a8579ccf559f0ac3d83284413e7196ad1249d2eafd3eafd1dcadc"
   -- game/tests/retail_state_pin_runner.gd`) in an out-of-tree worktree, run
   the runner, confirm it printed the pinned hash there (green). If it does NOT
   (environment/content dependency), STOP and report that the pin depends on
   external content — that is itself the finding.
3. `git bisect` between last-green and HEAD in the out-of-tree worktree, using
   the runner's `RETAIL_STATE_PIN OK|FAIL` as the test (write a tiny bisect
   script; log each step). Note: multiple commits may each move the hash — after
   the first culprit, re-run bisect from culprit..HEAD to find whether the hash
   moved again (the drifted value must be reached; count the distinct hashes).
4. For each culprit commit: read its diff and state whether the behaviour change
   is (a) an intended retail-fidelity fix (hash SHOULD move → legitimate re-mint
   cause), (b) a defect (hash moved because something broke — e.g. structure
   damage now refused where retail applies it), or (c) incidental state-shape
   change (a new key in `_authoritative_state()` without EMPTY-IS-ABSENT guard).
   For (b) name the fix; for (c) name the guard to add.
5. Determine whether the "no compiled armor contract → damage refused" floods
   are in the pin fixture's path and whether they are one of the culprits.

## Report — orchestration/reports/q5-state-pin-drift.md
- Table: commit → hash before/after → classification (a/b/c) → one-line why.
- Verdict for the owner: RE-MINT-READY (all causes are (a)/(c) with guards
  specified) or FIX-FIRST (list (b) defects to fix before minting).
- The exact re-mint procedure (which constant, which file, the statement the
  runner header requires) so the owner can sign it in one commit AFTER the
  projectile lane also lands (both causes will be named in one mint).
- Update Q5 in queue.md with the verdict and evidence paths. Commit only
  queue.md + your report (explicit paths). Do NOT change the pin.
