# HOTFIX v0.2.4.1 — "construction facts and phase animation disagree" blocks skirmish start (Grok)

Repo: C:\Users\Jonathan\Desktop\open-bfme. Read AGENTS.md. Claim row Q31 in
orchestration/queue.md (owner=grok-hotfix-0241). Standard rules: explicit-path
git, no sweeps, logs under workspace/logs/hotfix-0241-*.txt, sequential Godot,
detached long jobs (hidden window + redirected output). NO pack builds, NO
selection change — this is runtime-only; the shipped packs are correct.

## The bug (owner hit it in v0.2.4 within minutes; two screenshots)
1. Skirmish start: "RETAIL SLICE UNAVAILABLE — capability validation failed:
   structure_retail_visuals[2001:buildingLifecycle construction facts and
   phase animation disagree, 90001:missing-node, 90002:missing-node,
   90003:missing-node, 90004:missing-node]".
2. BFME2 Men "Destroy the enemy fortress" scenario: "Faction roster
   presentation validation failed: bfme2.object.men-fortress lifecycle contract
   failed: buildingLifecycle construction facts and phase animation disagree".

## Root cause (orchestrator diagnosis, verified against pack bytes)
- `bfme2-men-vslice/7de517bf…/data/objects.json` registers
  `bfme2.object.men-fortress` with `presentation.buildingLifecycle.
  simulationFacts.construction == null` (absent) while
  `phases[0].animation == {"clip":"gbfortress_abl","mode":"manual-progress"}`.
  The per-structure docs (data/playable-structures/menfortress*.json) are
  internally consistent — the LEGACY registry doc is the one ContentDB
  registers for that id (`ContentDB.get_bundle_object`), and it lacks the facts.
- Commit `34e6cfa` (2026-08-17 06:25, "fix(importer): preserve retail static
  construction") added in `game/src/retail_slice/retail_structure.gd`
  (~:721-730, `validate_lifecycle_contract`) and mirrored in
  `game/src/content/content_db.gd` (~:3578-3645):
      if not construction_omitted:
          var construction: Dictionary = facts["construction"]   # ← Invalid access when absent (this IS Q29)
          ... expected_phase_mode = "manual-progress" if animationMode=="MANUAL" else "none"
          if phase0.animation.mode != expected → "construction facts and phase animation disagree"
  With construction absent: Invalid-access error → null → expected "none" →
  "none" != "manual-progress" → fail-closed. `9000x:missing-node` is the
  cascade (presentation build aborted before neutral structure nodes existed).
- Why gates missed it: the dist verify booted to the MENU only; the fortress
  surface runner + trebuchet playtest enter the slice by paths that skip the
  fail-closed roster validation `_load_required_presentation_definitions`
  (retail_vertical_slice.gd:1064) → `_validate_retail_structure_lifecycle`
  (:1377). The commit's own runner (playable_structure_runtime_consumer_runner)
  was updated in the same commit, so it agreed with itself.

## Fix (surgical, runtime-only)
1. `retail_structure.gd validate_lifecycle_contract` (~:700-731): compute
   `construction_omitted` for ALL docs (not only `composed`): treat
   `facts.get("construction")` that is null/absent OR a Dictionary carrying
   `status` as "no construction facts to cross-check". Only when a real
   construction Dictionary WITHOUT `status` exists do the strict
   `_valid_construction_facts` + phase-0 cross-check run. Never index
   `facts["construction"]` unguarded (this closes Q29 too). Keep every OTHER
   check exactly as-is — legacy docs must still fail on genuinely wrong phase
   chains; we are only refusing to invent a construction fact that isn't
   authored.
2. `content_db.gd` (~:3578-3645): the same guard, same semantics — a doc with
   absent construction facts is not rejected by the phase cross-check.
3. Do NOT edit packs, `objects.json`, or the importer in this lane. (Follow-up
   Q32: make the legacy objects.json men-fortress carry construction facts on
   the next BFME2 men recook, or retire the legacy registry doc in favour of
   playable-structures/menfortress.json — file it, don't do it.)

## Tests — failing-first (this is the verification hole; close it)
- NEW `game/tests/slice_start_roster_presentation_runner.gd`: boots the
  vertical slice the way the MENU does (find the exact code path the skirmish
  start / M2 scenario uses; reuse the boot pattern of a menu-driven runner) on
  the LIVE selection (OPENBFME_CONTENT=<repo>\workspace\content-packs) for
  BOTH: (a) rotwk skirmish Men host, (b) OPENBFME_SLICE_MAP=bfme2.map.fords-of-
  isen-ii BFME2 Men. Assert `_load_required_presentation_definitions()` returns
  "" and the capability validation reports no `structure_retail_visuals`
  failures and no `missing-node`. Print `SLICE_START_ROSTER_RESULT passed=N
  failed=M`. It must FAIL on HEAD before your fix (paste that run) and PASS
  after. Add it to tools/gate-m2-focused.ps1 (repo-root copy) with a dated
  comment AND to the play-smoke `-Quick` list if that exists yet (it does not —
  note it in the Q22 brief instead).
- Extend `game/tests/playable_structure_runtime_consumer_runner.gd` (34e6cfa's
  own runner) with a fixture doc that has construction ABSENT + phase-0
  manual-progress (the exact objects.json shape) and assert it validates
  without error and without a SCRIPT ERROR line; and a fixture with construction
  PRESENT-and-disagreeing that still FAILS (the strict check survives).
- Q29 regression check: boot the slice once and grep the log for
  `Invalid access to property or key 'construction'` → must be 0.

## Ship v0.2.4.1 (runtime-only rebuild; same packs, same selection)
- Checkpoint (sequential): the new runner (a+b PASS), retail_spellbook_runner
  218/0, retail_member_combat_runner 115/0, projectile_table_runtime_runner
  4/0, fortress surface men + angmar (same known fails), retail_slice_runner
  (failure NAMES vs workspace/logs/q14fin-* — no new), retail_lockstep_
  determinism_runner 5/0, retail_state_pin_runner 0e4bcdbf unchanged,
  boot_startup_runner 44/0.
- VERSION 0.2.4 → 0.2.4.1; Write-BuildInfo; project.godot + boot.tscn label
  (the v0.2.4 label was one commit stale — fix that too); commit together.
- docs\patch-notes\v0.2.4.1.md — ALPHA HOTFIX: "v0.2.4 could not start a
  skirmish or the Fords scenario (RETAIL SLICE UNAVAILABLE: construction facts
  and phase animation disagree). Cause: a stricter structure-lifecycle check
  rejected the legacy Men fortress registry entry, which authors no
  construction facts. Fixed in the runtime; content unchanged." + Known gaps
  carried from v0.2.4.
- Test-DistPipeline PASS → detached Publish-DistBuild -Zip → dist\v0.2.4.1\.
- PROVE IT THE WAY THE OWNER HIT IT: from the SHIPPED export
  (dist\v0.2.4.1\run-with-log.bat env), start a rotwk skirmish AND the BFME2
  Fords scenario headless via the new runner pointed at the export's
  content-packs; both PASS; log shows a0fde4ac mounted; 0 "Invalid access"
  lines; 0 "RETAIL SLICE UNAVAILABLE".
- Close Q29 (subsumed) and Q31 in queue.md with evidence; file Q32. Commits
  `fix(game):` / `test(game):` / `chore(release): v0.2.4.1` / `docs(patch-notes)`.
  Report orchestration/reports/hotfix-v0241.md.
