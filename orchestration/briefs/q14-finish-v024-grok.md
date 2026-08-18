# Q14 finish — select 7 new faction packs, checkpoint, re-pin, ship dist\v0.2.4 alpha (Grok)

Repo: C:\Users\Jonathan\Desktop\open-bfme. Read AGENTS.md ("Publishing a build",
rules 1/5/9). The r4 recook is DONE: 7/7 faction packs + a new maps pack were
published (both roots — VERIFY) but selection is unchanged. This lane selects
the new faction packs and ships the alpha. Claim continues under Q14. Standard
rules: explicit-path git, BANNED git add -A/reset/restore/clean/stash/amend,
logs under workspace/logs/q14fin-*.txt, sequential Godot, one heavy job at a
time. Detached long jobs (Publish-DistBuild ~90 min): launch via Start-Process
-WindowStyle Hidden -RedirectStandardOutput (NOT a shell that can be
console-killed — three cooks died that way tonight). Pinned interpreter and
Godot via tools\resolve-godot.bat.

## Owner scope decision (2026-08-18, ratified)
- Swap ONLY the 7 faction packs. KEEP current EVA overlays, maps pack, neutral,
  music, cursors, bfme2-men, and the 79 missing-physical batches at their
  present digests. EVA recompose (Q3) and the new maps pack are DEFERRED to
  v0.2.5 (de-risk the alpha). Note both in Known Gaps.
- 0.2.3 was never cut; v0.2.4 is the next number.

## New faction digests (from r4 publication receipts — VERIFY each dir exists in BOTH roots before selecting)
- men     rotwk-men-vslice/a0fde4ac89596cab4d34beae3ebce0e33aa30323f99a73727706a29c85d315c0   (ACTIVE)
- elves   rotwk-elves-vslice/99f31f9d308b46abc6e0f74554bb534afd378b654eda1dc31407d49188bd3d61
- dwarves rotwk-dwarves-vslice/ba3e0d12ac34f17d02249bd335db9a3d6a3f3a062a5c45d4ec8108ba14ff2633
- isengard rotwk-isengard-vslice/fab985f0f28bd28cad8d1e1ca8db0d7763946a66177319d27cf51983f55f64d8
- mordor  rotwk-mordor-vslice/36238eaa8db3bb781e3433e23293d1c365186d4073cb93ce95e5f82968d9a8b2
- wild    rotwk-wild-vslice/e2ce0dd29a265053c13b4cd4edcc122238b9c1c4fca6b776c6286ff30ef9f987
- angmar  rotwk-angmar-vslice/15720e1f102d3f8ae97beb0ba26329254f517516a180e3b8a07e8c8c5ec71663
Every OTHER supplemental pack keeps its CURRENT digest (read them from
workspace/content-packs/selection.json — 99 supplemental entries; replace only
the 6 non-men rotwk faction vslices + set active men; men also moves from the
supplemental? NO — men is active. Keep bfme2-men-vslice as-is.)

## Steps
0. Baseline: check_pack_addresses PASS packs=200 roots=2; record current
   selection sha (should be 04229f76…).
1. Mirror the 7 new faction packs to the durable root
   (%APPDATA%\Godot\app_userdata\Open BFME\content-packs) if the cook did not —
   tools\publish-durable-pack.ps1 then -Verify. check_pack_addresses PASS.
2. Seal the 7 new packs (they are freshly published — verify read-only in both
   roots; `python tools\seal_published_packs.py` if not; a prior lane shipped
   unsealed packs).
3. ONE apply-selection-transaction: active=new men, supplemental = current 99
   list with the 6 non-men faction vslices swapped to the new digests, all else
   unchanged. Both roots verified. check_pack_addresses PASS (packs count
   unchanged at 200 — same pack IDs, new digests).
4. Checkpoint (STOP conditions before re-pin), sequential, log each to
   workspace/logs/q14fin-<runner>.txt: retail_spellbook_runner (218/0),
   retail_member_combat_runner (115/0), projectile_table_runtime_runner (4/0),
   goal_production_matrix_runner, retail_slice_runner (failure NAMES vs
   workspace/logs/q13verify-* — no NEW names), fortress surface men + angmar,
   retail_lockstep_determinism_runner (5/0), retail_state_pin_runner (0e4bcdbf…
   unchanged), boot_startup_runner (solo). STOP and report (do not re-pin) if
   any runner loses a previously-passing NAMED check not explained by the new
   content, or a pack fails to mount. The new faction packs now carry Q13
   projectile fields + the elves NoldorWarrior units — a runner that ASSERTS on
   those is an expected change; name it.
5. Re-pin selection: tools/gate-retail.ps1 ($expectedSelectionSha256,
   $expectedSelectionActivePack, $expectedSelectionSupplementalPacks) +
   importer/tests/test_retail_gate_script.py, dated comment "RE-MEASURED
   2026-08-18 Q14 v0.2.4 recook (7 faction packs, Q13 projectiles, elves
   NoldorWarrior)". pytest test_retail_gate_script.py green.
6. Ship v0.2.4 (AGENTS.md Publishing a build):
   a. VERSION 0.2.2 → 0.2.4.
   b. tools\Write-BuildInfo.ps1; match config/version in game\project.godot and
      the label in game\scenes\boot.tscn; commit all four together.
   c. docs\patch-notes\v0.2.4.md — ALPHA. Player-facing: projectiles with flight
      time + retail splash for siege/AoE, invented hero-cleave removed, all 7
      factions recooked, elves Noldor Warriors restored, Men fortress pads +
      cavalry fix, Angmar citadel. `## Known gaps`: EVA overlays are the prior
      build (recompose deferred, Q3); maps pack unchanged; scatter/HitPercentage
      not modelled; structure-splash + ally/neutral splash untested (Q17); 6
      known importer reds (Q6); state pin does not cover projectile combat (Q16);
      dead M2 focused gate (Q15); typed-module over-strictness cluster (Q27);
      100+ mounted packs add ~3s to startup.
   d. tools\Test-DistPipeline.ps1 → PASS (fast) before the long build.
   e. Detached: tools\Publish-DistBuild.ps1 -Godot <godot> -AllowEnvDependent
      Content -AllowPackOwnedWotrData -AllowMissingWotrData -Zip → dist\v0.2.4\.
      Record the exact refusal if it refuses; do not add undocumented switches.
   f. Prove the artifact: run the export's run-with-log.bat once; confirm the
      log mounts the NEW men digest a0fde4ac (not a stale fallback) and shows no
      armor-contract flood beyond the known Q11 fixture message set.
7. Close Q14 in queue.md with: 7 new digests, new selection sha, dist path +
   zip sha256, checkpoint runner table. Commits `chore(content):` /
   `chore(release): v0.2.4 alpha` / `docs(patch-notes):`. Report
   orchestration/reports/q14-finish-v024.md.
