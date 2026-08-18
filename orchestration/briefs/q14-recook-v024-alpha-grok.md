# Q14 — full RotWK recook (+Q3 EVA, +Q4 Men) → select → v0.2.4 alpha in dist/

Repo: C:\Users\Jonathan\Desktop\open-bfme. Read AGENTS.md first — especially
"Publishing a build" (release artifacts live ONLY in dist\v<version>\; a cook
that changes selection is not done until it ships one) and rules 1/5/9. Claim
Q14 (and Q3, Q4 which this lane absorbs) in orchestration/queue.md as your
first commit (owner=grok-q14). Exclusive tree access. Surgical edits only, git
add by explicit path, BANNED: git add -A / reset / restore / clean / stash /
amend, any sweep. Long output → workspace/logs/q14-*.txt (never rely on
console tails; builds run 40+ min per faction — launch detached via PowerShell
Start-Process where the Bash 10-min timeout would cut you, and poll the log
mtime). Pinned interpreter
workspace\retail-work\tools\python-3.12-env\Scripts\python.exe. Godot via
tools\resolve-godot.bat (fallback C:\Users\Jonathan\Downloads\godot47\).
Machine caution: process handles were exhausted once today — run ONE heavy
job at a time; no parallel pytest, no concurrent Godot sweeps.

## Goal
Q13 (projectiles + warhead radius components) changed COMPILER output but no
pack was rebuilt, so shipped content does not carry `projectileSpeed`,
projectile ids, `radiusDamageAffects`, or radius/taper `damageComponents`.
Recook every faction so the game plays the new combat, fold in the two
outstanding content items (Q3 EVA overlays, Q4 Men republish — Q4 is subsumed
by the full recook), select the result transactionally, re-pin the selection,
and ship `dist\v0.2.4` (alpha) so the owner can play it.

## Inputs (canonical)
- RotWK install (layered oracle): `workspace\retail-work\editions\rotwk\layered-install\layer-0-rotwk`
  (BFME2 base: `...\layer-1-bfme2`). Read tools/rotwk_full_content.py and
  run_rotwk_full_content.bat to confirm the exact args (`--install`,
  `--bfme2-install`, `--state-root workspace\retail-work`, `--map-limit 10`).
  Do NOT pass `--select` — selection happens once, transactionally, in step 3.
- Recook contract: docs/state/recook-checklist.md — the recook MUST emit the
  typed rows it names (ModelConditionUpgrade, AnimationState, ParticleSysBone,
  EnteringStateFX, FXEvent) AND Q13's combat fields. Verify presence in the
  cooked packs before selecting; if absent, STOP and report (do not select a
  pack that fails the checklist).
- EVA: tools/compose-eva-overlay.py per faction (7 sides), census + catalog
  under workspace/retail-work; prior receipts/logs and the independent checker
  live in workspace/orchestration/fable-wave/eva/ (verify_eva_overlay.py) — read
  them; the recompose must yield packs that carry `semanticFieldCoverage`
  (today 6 of 7 lack it). Runners to re-run against the new bundles:
  game/tests/eva_overlay_closure_runner.gd, eva_fidelity_runner.gd.

## Steps
0. Baseline: `python tools\check_pack_addresses.py` (PASS packs=200 roots=2),
   selection sha (04229f76…), and record HEAD.
1. **Recook**: run the one-button full content build WITHOUT --select. Log to
   workspace/logs/q14-recook.txt. Expect hours. On completion, list every new
   pack id/digest it published (both roots — check; mirror with
   tools\publish-durable-pack.ps1 then -Verify if needed) and confirm each new
   faction pack contains the Q13 fields (grep a compiled unit doc for
   `projectileSpeed` and `damageComponents` with `radius`) and the recook
   checklist rows. Pack proof receipts must show auditValid/conversionFailures 0
   (or the documented residuals — GondorKnightsofDol AutoHealBehavior is a known
   named gap; anything else new = STOP).
2. **EVA recompose**: 7 overlays via compose-eva-overlay.py; publish (no
   select); confirm `semanticFieldCoverage` present in all 7; run
   verify_eva_overlay.py; run the two EVA runners against the new bundles.
3. **Select** — ONE apply-selection-transaction: new active men pack, new
   faction packs (all 7), new EVA overlays (7), keep neutral/music/cursors/maps
   packs at their current digests unless the recook republished them (then use
   the new digest), keep the 79 missing-physical batches. Both roots verified.
   `check_pack_addresses.py` PASS (packs count = live selection × 2 roots).
4. **Checkpoint** (STOP conditions before re-pin): run sequentially, log each:
   retail_spellbook_runner (218/0), retail_member_combat_runner (115/0),
   projectile_table_runtime_runner (4/0), retail_slice_runner (compare failure
   NAMES vs workspace/logs/q13verify-* / q1q2-after-slice.txt — no new names),
   goal_production_matrix_runner, fortress surface runners men+angmar,
   eva_overlay_closure_runner, eva_fidelity_runner, boot_startup_runner (solo,
   quiet machine — 44/0 expected), retail_state_pin_runner (0e4bcdbf… — must
   not move; it is synthetic), retail_lockstep_determinism_runner (5/0).
   STOP if any runner loses a previously-passing named check not explained by
   the recook's documented residuals, or if any pack fails the recook checklist.
   Then re-pin selection in tools/gate-retail.ps1 + importer/tests/
   test_retail_gate_script.py (dated comment "RE-MEASURED 2026-08-17 Q14
   recook"), pytest that test file green.
5. **Ship v0.2.4 alpha** (AGENTS.md "Publishing a build"):
   a. VERSION 0.2.2 → 0.2.4 (0.2.3 was never cut; say so in notes).
   b. `powershell -File tools\Write-BuildInfo.ps1`; match `config/version` in
      game\project.godot and the label in game\scenes\boot.tscn; commit all
      four together.
   c. docs\patch-notes\v0.2.4.md — owner-facing plain language, title it an
      ALPHA: what changed for the player (projectiles with flight time and
      retail splash for siege/AoE, hero cleave removed, Men fortress pads +
      cavalry fix, Angmar citadel, 38k missing assets, EVA announcer overlays
      with suppression/hero voices, layout/infra invisible to players), then
      `## Known gaps` (scatter/HitPercentage not modelled, structure-splash and
      ally/neutral splash paths untested, 6 known importer reds, slice runner
      known failures, state pin does not cover projectile combat, dead M2 gate
      Q15, anything from the recook residuals). Publish refuses without it.
   d. `powershell -File tools\Test-DistPipeline.ps1` (15 s) → PASS before the
      long build.
   e. `powershell -File tools\Publish-DistBuild.ps1 -Godot <godot.exe>
      -AllowEnvDependentContent -AllowPackOwnedWotrData -AllowMissingWotrData
      -Zip` → lands in dist\v0.2.4\ (+zip). ~90 min. Record the exact refusal
      reason if it refuses; do not add switches beyond those documented in
      AGENTS.md without reporting why.
   f. Prove the artifact: the wrapped build boots the export headless twice
      (the publish does this); additionally run the export's run-with-log.bat
      once and confirm the log shows the NEW men digest mounted (not a stale
      durable fallback) and no `no compiled armor contract` flood beyond the
      known fixture message set.
6. Finish: queue rows Q3/Q4/Q14 CLOSED with evidence (digests, selection sha,
   dist path + zip sha256, runner table); commits `chore(content):` /
   `chore(release): v0.2.4 alpha` / `docs(patch-notes)`, explicit paths, no
   logs, nothing under workspace/ or dist/. Report
   orchestration/reports/q14-recook-v024.md with the full runner table, every
   digest, timings, and anything left undone.
