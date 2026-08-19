# v0.2.6 alpha finish — 2026-08-19

Shipped `dist\v0.2.6\` + `dist\v0.2.6.zip` (13,874,274,004 bytes, sha256
`f5cebbf075e86c8e33e96359ed15a7cd812cfb4029f6094fcf2fc9dff715507a`).
Publish-DistBuild exit 0 after 47 min (`workspace/logs/v026fin-publish.txt`).
Test-DistPipeline 25/25 PASS. Tree was clean at publish (commits 827a643 /
0edb19c / 332185d).

## What this build carries

- 7 recooked faction packs (`tools/rotwk_full_content.py`, 113 min,
  `workspace/logs/v026-recook.txt`, PACK_PROOF 7/7; angmar `gaps=1` =
  AngmarWallPosternGateSmall, the same honest residual as v0.2.5):
  men `9dd4a5ed`, elves `223dcc5c`, dwarves `b176d67d`, isengard `223ab28b`,
  mordor `85887ce1`, wild `59bafb9d`, angmar `f978128b`.
- Republished playable-maps pack `rotwk-playable-maps-private/6f6be4db`
  (`tools/rotwk_multimap_skirmish.py --full-profile --build --publish`, 44 min,
  74 maps, `fixtures.json` + castleSiege v2 on every castle map).
- Selection sha `c27af45b` -> `f8c3b255` via ONE apply-selection-transaction
  (swaps=2, verified both roots). `check_pack_addresses` PASS,
  `publish-durable-pack.ps1 -Verify` PASS, packs sealed both roots.
- Runtime (already on main before the swap): castle maps admit (fcd0cbd,
  d0aa715), W3D texture mappers (eeea984), CAH class armor (1dfdc58), and
  the three seams the real packs exposed (827a643: map-fixture bindings as
  props, castle_fixture rows bound-map-prop, no-render intact phase).

## Disk incident

C: hit 2.5 GB free during the durable mirror (robocopy exit 9 on angmar).
Pruned 72 UNSELECTED durable-root mirrors (56.2 GB) whose pack.json was
byte-identical to the workspace original (`workspace/scratch/
v026fin-durable-prune.py`, log `workspace/logs/v026fin-durable-prune.txt`).
18 durable dirs without a workspace twin were left alone. `dist\` untouched.

## Checkpoint (sequential, OPENBFME_CONTENT=workspace\content-packs)

| runner | result |
|---|---|
| retail_state_pin_runner | 0e4bcdbf… OK (unchanged) |
| castle_map_live_boot_runner x10 castle maps | 8/0 each, fixtures seeded (Erebor 587 + 2 scenario-overlap = 589) |
| castle_map_admission_runner | 47/0 (v2 contracts) |
| castle_lobby_admission_runner | 11/0 |
| castle_fixture_spawn_runner | 32/0 |
| fortress_plot_presentation_runner | 24/0 (Q48 green) |
| retail_spellbook / four_unit_hud / radial | 218/0, 121/0, 48/0 |
| slice_start_roster / mounted_hero / w3d_texture_mapper | 22/0, 9/0, 21/0 |
| member_combat / production_queue / formation_toggle | 115/0, 29/0, 18/0 |
| scenario_map_placement_live / retail_bound_props / boot_startup | 80/0, 30/0, 44/0 |
| cah_match_runner | 72/3 (the 3 pre-existing gondor-archer-range/skin rows; baseline 69/3) |
| retail_slice_runner | identical 87 named failures to v0.2.5 (none new, none lost) |

## Shipped-export proof (OPENBFME_CONTENT=dist\v0.2.6\content-packs)

- slice_start_roster_presentation_runner 22/0, mounts men `9dd4a5ed`.
- castle_map_live_boot_runner (Erebor) 8/0 from the shipped packs.
- `rotwk-men-vslice/9dd4a5ed…/assets/models/units/rohaneomer/` has 00.glb + 01.glb.
- `rotwk-playable-maps-private/6f6be4db…/maps/wor-erebor/fixtures.json` present.

## Known gaps carried (see docs/patch-notes/v0.2.6.md)

Castle siege features partial per map (gates, garrisons, wall defenses,
walkable walls, siege AI = lanes L3-L10, now unblocked on playable maps);
destroyed castle walls keep their prop mesh until L9 presentation lands;
Loco B; formation modifier plumbing; EVA overlays prior build.
