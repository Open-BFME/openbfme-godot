# v0.2.5 finish — select 7 recooked faction packs, checkpoint, re-pin, ship dist\v0.2.5 alpha (Grok)

Repo: C:\Users\Jonathan\Desktop\open-bfme. Read AGENTS.md ("Publishing a build",
rules 1/5/9). The v0.2.5 recook is DONE (log workspace/logs/v025-recook-r2.txt:
`PACK_PROOF ready=7/7 exit=0`); 7 new faction packs are published (both roots —
VERIFY) and selection is unchanged. Claim Q44 in orchestration/queue.md
(owner=grok-v025). Standard rules: explicit-path git; BANNED git add -A / reset /
restore / clean / stash / amend; logs under workspace/logs/v025fin-*.txt;
sequential Godot; one heavy job at a time; detached long jobs (Publish-DistBuild
~30-50 min) via Start-Process -WindowStyle Hidden -RedirectStandardOutput.
Pinned interpreter and Godot via tools\resolve-godot.bat. Never hand-edit
selection.json; only the SELECTION pins may be edited.

## Scope
Swap ONLY the 7 faction packs. Keep EVA overlays, maps pack, neutral, music,
cursors, bfme2-men, and the 79 missing-physical batches at their present
digests. VERSION 0.2.4.1 -> 0.2.5.

## New faction digests (v0.2.5 recook receipts — VERIFY each dir exists in BOTH roots)
- men      rotwk-men-vslice/f177d1bd6c43def75b2bcfccde368e3e179d3de67c182351c83e9968b942e514   (ACTIVE)
- elves    rotwk-elves-vslice/d41df402a90f8ea6aa699547262564ed72d2baf5836939fbbfbe279340d9f1c5
- dwarves  rotwk-dwarves-vslice/fbc7a61c96d98e754314af216a31a76692b96d8875c82206edf0f67dd83d93f5
- isengard rotwk-isengard-vslice/d987154db1484f08543519de16b0d402038fbfa56c9c4fa03dd6490a0925637a
- mordor   rotwk-mordor-vslice/30f82d9d1cd7ab5797dcc2f260a6223a72550072d39eb98eb0cf9a16d5196968
- wild     rotwk-wild-vslice/b96c36c24b90f43afeec77fe3fba027584492b3fd7c954624ce25a328cc82091
- angmar   rotwk-angmar-vslice/84d1cd77e64ef9b1a96df3ecbd4fec866d5e7e66ad6a8091a7d0e08d93fcf316
Every OTHER supplemental keeps its CURRENT digest (read selection.json — 99
supplemental; replace only the 6 non-men rotwk faction vslices; set active men).

## Steps
0. Baseline: `python tools\check_pack_addresses.py` (PASS packs=200 roots=2);
   record selection sha (4e3e7024…).
1. Mirror the 7 packs to the durable root (%APPDATA%\Godot\app_userdata\Open
   BFME\content-packs) via tools\publish-durable-pack.ps1 then -Verify;
   `python tools\seal_published_packs.py`; check_pack_addresses PASS.
2. ONE apply-selection-transaction: active=new men; supplemental = current list
   with the 6 faction vslices swapped. Both roots verified. check_pack_addresses
   PASS (200 packs, same ids).
3. Checkpoint (sequential; log each to workspace/logs/v025fin-<runner>.txt):
   retail_spellbook_runner 218/0; retail_member_combat_runner 115/0;
   projectile_table_runtime_runner 4/0; retail_authored_movement_runner 13/0;
   retail_formation_movement_runner 34/0; retail_formation_toggle_runner 18/0;
   retail_production_queue_runner 29/0; horse_commandset_runner 11/0;
   retail_hero_footstep_runner 26/0; placement_ghost_visuals_runner 12/0;
   retail_four_unit_hud_runner 118/0; retail_radial_layout_runner 41/0;
   slice_start_roster_presentation_runner 22/0; retail_lockstep_determinism 5/0;
   retail_state_pin_runner (record hash — the recook changed movement/combat
   content so it MAY move: record old 0e4bcdbf… -> new and WHY; do NOT
   re-mint); retail_slice_runner (failure NAMES vs workspace/logs/locoA-merge-*
   / q14fin-* — name any new); boot_startup_runner (solo). STOP and report
   (no re-pin) if any runner loses a previously-passing NAMED check not
   explained by the new content, or a pack fails to mount.
4. Re-pin selection: tools/gate-retail.ps1 ($expectedSelectionSha256,
   $expectedSelectionActivePack, $expectedSelectionSupplementalPacks) +
   importer/tests/test_retail_gate_script.py, dated comment "RE-MEASURED
   2026-08-18 v0.2.5 recook (7 faction packs; owner playtest fixes)"; pytest that
   file green.
5. Ship: VERSION 0.2.4.1 -> 0.2.5; tools\Write-BuildInfo.ps1; config/version in
   game\project.godot + label in game\scenes\boot.tscn; commit together.
   docs\patch-notes\v0.2.5.md — ALPHA, player-facing: mounted heroes show their
   horse model (Eomer/Theoden/Gandalf/Faramir); formations only for hordes
   retail authors (Tower Guard porcupine, Gondor soldier shield wall) with the
   real two-image icons; production queues uncapped per retail (only
   ThrallMaster/BattleWagon cap at 1); unit movement uses retail Locomotor
   values (invented accel/turn constants deleted); HUD laid out from Palantir.apt
   coordinates (money/CP/palantir), hero roster with curved health + level
   badge + select-all; in-world structure command radial; placement ghost 50%
   + white claim disc, no green outline; hero footsteps at authored
   volume/limit + mounted sound state; trees sway (engine-default
   approximation, wind from DownwindAngle); skirmish setup hero picker
   populated on open; retail oracle re-extracted pure 2.01.
   `## Known gaps`: EVA overlays are the prior build (Q3); maps pack unchanged;
   turn model / movement feel (Loco B) not yet; formation FORMATION-modifier
   plumbing to unit descriptors open (toggle works; crush-decel only where
   authored on the horde); select-all faction icon has no texture source yet;
   HUD dish frame ornament composition; state pin does not cover projectile
   combat (Q16); dead M2 gate (Q15).
   `powershell -File tools\Test-DistPipeline.ps1` PASS, then detached
   `tools\Publish-DistBuild.ps1 -Godot <godot> -AllowEnvDependentContent
   -AllowPackOwnedWotrData -AllowMissingWotrData -Zip` -> dist\v0.2.5\ (+zip).
   Record the exact refusal if it refuses.
6. PROVE THE SHIPPED EXPORT: run slice_start_roster_presentation_runner with
   OPENBFME_CONTENT=dist\v0.2.5\content-packs — both surfaces PASS, mounts men
   f177d1bd…, 0 Invalid access, 0 RETAIL SLICE UNAVAILABLE; and confirm
   dist\v0.2.5\content-packs\rotwk-men-vslice\f177d1bd…\assets\models\units\
   rohaneomer\ contains 00.glb AND 01.glb.
7. Close Q44 with evidence (digests, selection sha, dist path + zip sha256,
   checkpoint table). Commits `chore(content):` / `chore(release): v0.2.5
   alpha` / `docs(patch-notes):`. Report orchestration/reports/v025-finish.md.
