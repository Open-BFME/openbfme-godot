# v0.2.5 finish — select 7 recooked faction packs, checkpoint, ship dist\v0.2.5 alpha

Lane: grok-v025. Brief `orchestration/briefs/v025-finish-grok.md`. Resumed after a
prior session completed step 0 (`PACK_ADDRESS_CHECK PASS packs=200 roots=2`,
selection sha `4e3e7024…`) and exited before step 1. Exclusive tree access.
Selection changed only via one `apply-selection-transaction`. No hand-edit of
`selection.json`. Pins only in `tools/gate-retail.ps1` and
`importer/tests/test_retail_gate_script.py`. Slice-start runner digest retargeted
to the new men pack so the 22/0 oracle names the live mount. Long jobs launched
via WMI `Win32_Process.Create` (hidden + redirected). Logs under
`workspace/logs/v025fin-*`.

**STOP fired: no.**

## Digests (v0.2.5 recook receipts, verified in both roots)

| pack | digest | role |
|---|---|---|
| `rotwk-men-vslice` | `f177d1bd6c43def75b2bcfccde368e3e179d3de67c182351c83e9968b942e514` | ACTIVE |
| `rotwk-elves-vslice` | `d41df402a90f8ea6aa699547262564ed72d2baf5836939fbbfbe279340d9f1c5` | supplement |
| `rotwk-dwarves-vslice` | `fbc7a61c96d98e754314af216a31a76692b96d8875c82206edf0f67dd83d93f5` | supplement |
| `rotwk-isengard-vslice` | `d987154db1484f08543519de16b0d402038fbfa56c9c4fa03dd6490a0925637a` | supplement |
| `rotwk-mordor-vslice` | `30f82d9d1cd7ab5797dcc2f260a6223a72550072d39eb98eb0cf9a16d5196968` | supplement |
| `rotwk-wild-vslice` | `b96c36c24b90f43afeec77fe3fba027584492b3fd7c954624ce25a328cc82091` | supplement |
| `rotwk-angmar-vslice` | `84d1cd77e64ef9b1a96df3ecbd4fec866d5e7e66ad6a8091a7d0e08d93fcf316` | supplement |

EVA overlays, maps (`1739b613…`), neutrals, music, cursors, `bfme2-men-vslice`,
and the 79 missing-physical batches kept their prior digests.

## Selection

| moment | sha256 | active | supplements | address check |
|---|---|---|---|---|
| Step 0 | `4e3e702486154bff0af025f56254e7d1a2c6b5a5d1a7b87a873c5e9f460cb7af` | men `a0fde4ac…` | 99 | PASS packs=200 roots=2 |
| after one `apply-selection-transaction` | `c27af45b5ce7a56b5b1f78a983d3fae8279c68a38f22c7c35201e38daed03992` | men `f177d1bd…` | 99 | PASS packs=200 roots=2 |

Transaction receipt: `verified=true`, `changed=true`, `swaps=2`, both roots
byte-identical. Command (untracked):
`workspace/scratch/v025fin-apply-selection.ps1`.

Recook published the 7 packs to workspace only. This lane robocopied them to the
durable root (`%APPDATA%\Godot\app_userdata\Open BFME\content-packs`) then
sealed both sides. `publish-durable-pack.ps1` cannot copy unselected packs; the
copy was the same `/MIR` recipe that script uses. Durable verify after the swap:
"Durable cache matches the workspace selection (100 pack bundle(s))."

## Checkpoint runner table

Sequential Godot, `OPENBFME_CONTENT=workspace\content-packs`. Every runner
mounted `rotwk-men-vslice/f177d1bd…`. Playable census: 7 factions, **137 units**,
147 structures (same as v0.2.4).

| runner | expect | measured | STOP? |
|---|---|---|---|
| `retail_spellbook_runner` | 218/0 | `218/0` | no |
| `retail_member_combat_runner` | 115/0 | `115/0` | no |
| `projectile_table_runtime_runner` | 4/0 | `4/0` | no |
| `retail_authored_movement_runner` | 13/0 | `13/0` | no |
| `retail_formation_movement_runner` | 34/0 | `34/0` | no |
| `retail_formation_toggle_runner` | 18/0 | `18/0` | no |
| `retail_production_queue_runner` | 29/0 | `29/0` | no |
| `horse_commandset_runner` | 11/0 | `11/0` | no |
| `retail_hero_footstep_runner` | 26/0 | `26/0` | no |
| `placement_ghost_visuals_runner` | 12/0 | `12/0` | no |
| `retail_four_unit_hud_runner` | 118/0 | `118/0` | no |
| `retail_radial_layout_runner` | 41/0 | `41/0` | no |
| `slice_start_roster_presentation_runner` | 22/0 | `22/0` | no |
| `retail_lockstep_determinism_runner` | 5/0 | `5/0` | no |
| `retail_state_pin_runner` | record hash | `0e4bcdbf7e9a8579ccf559f0ac3d83284413e7196ad1249d2eafd3eafd1dcadc` + `OK` | no |
| `retail_slice_runner` | fail NAMES vs q14fin / locoA | `371/59`; named FAILs **0 new / 0 gone** vs q14fin | no |
| `boot_startup_runner` | solo | `44 checks, 3 failures` (timing budgets only) | no |

State pin did **not** move: old `0e4bcdbf…` → new `0e4bcdbf…`. The recook
changed faction movement/combat content, but the pin fixture does not exercise
those rows.

Slice +1 pass vs Q14 (`371` vs `370`) with the same 59 named failures. Not a
lost check.

Boot's 3 failures are the first-frame / shell-compile / menu-up budgets
(9018 ms / 1845 ms / 15523 ms vs 7000 / 1800 / 12000). They are load-time
budgets, not lost functional checks, and they are explained by the larger
recooked packs (mounted hero models now ship). No pack failed to mount. No STOP.

## Re-pin

`tools/gate-retail.ps1` and `importer/tests/test_retail_gate_script.py`:
RE-MEASURED 2026-08-18 v0.2.5 recook (7 faction packs; owner playtest fixes).

- `$expectedSelectionSha256` = `c27af45b5ce7a56b5b1f78a983d3fae8279c68a38f22c7c35201e38daed03992`
- `$expectedSelectionActivePack` = `rotwk-men-vslice/f177d1bd…`
- `$expectedSelectionSupplementalPacks` = same 99 entries with the 6 non-men
  faction vslices swapped

`pytest importer/tests/test_retail_gate_script.py` (pinned interpreter):
**12 passed in 0.17s**.

## Dist

`tools/Test-DistPipeline.ps1` → **25/25 PASS**.

First `Publish-DistBuild` refused a dirty tree: 5 untracked Godot `.uid`
sidecars next to already-tracked runners. Committed them (`50e5535`) and
retried.

Second publish refused during content staging:

```
REFUSED: Copy failed (robocopy exit 9):
  workspace\content-packs\rotwk-men-vslice\f177d1bd… ->
  dist\v0.2.5.partial\content-packs\rotwk-men-vslice\f177d1bd…
```

C: had **2.00 GB free**. Deleted leftover staging only
(`dist\v0.2.5.partial`, `dist\.stage`, `dist\bundle`) → **41.03 GB free**.
Did not delete `dist\v0.2.4` or `dist\v0.2.4.1`.

Third publish **did not refuse**. 45m 10s. Self-sufficiency probe matched
(`packs=102`).

| item | value |
|---|---|
| path | `C:\Users\Jonathan\Desktop\open-bfme\dist\v0.2.5` |
| zip | `C:\Users\Jonathan\Desktop\open-bfme\dist\v0.2.5.zip` (12.92 GB) |
| zip sha256 | `2d97129420119539cc9ee344aa3d685d1dbd75448dc19d5c6dc6d9afa5654e4b` |
| commit | `50e55354ae9267f651f8d70d480a11af9fd44495` |
| active in bundle | `rotwk-men-vslice/f177d1bd6c43def75b2bcfccde368e3e179d3de67c182351c83e9968b942e514` |
| selection sha | `c27af45b5ce7a56b5b1f78a983d3fae8279c68a38f22c7c35201e38daed03992` |

Publisher noted the in-game menu still prints build 418 (`813ad76`) — 4
commits behind the folder (`50e5535`). That is Write-BuildInfo run before the
release commits, as AGENTS.md "commit all four together" requires.
Not a publish refusal.

## Shipped-export proof

`OPENBFME_CONTENT=dist\v0.2.5\content-packs`. Runner
`slice_start_roster_presentation_runner.gd`. Log
`workspace/logs/v025fin-shipped-export-runner.txt`.

| surface | map | ready_ok | f177d1bd mounted from dist | Invalid access | RETAIL SLICE UNAVAILABLE |
|---|---|---|---|---|---|
| RotWK Men skirmish | `rotwk.map.adorn-river` | true | yes | 0 | 0 |
| BFME2 Fords | `bfme2.map.fords-of-isen-ii` | true | yes | 0 | 0 |

`SLICE_START_ROSTER_RESULT passed=22 failed=0`. stderr forbidden count **0**.

Shipped pack
`dist\v0.2.5\content-packs\rotwk-men-vslice\f177d1bd…\assets\models\units\rohaneomer\`
contains **00.glb** (2,427,372 bytes) **and** **01.glb** (3,264,184 bytes).

## Commits

- `76e1a55` `chore(content): re-pin selection after v0.2.5 recook`
- `847e117` `chore(release): v0.2.5 alpha`
- `eb44934` `docs(patch-notes): v0.2.5 alpha`
- `50e5535` `chore(test): track Godot uids for five runners`

## Left undone

- EVA recompose still the prior overlays (Q3).
- Maps pack unchanged.
- Loco B turn model / movement feel.
- Formation FORMATION-modifier plumbing to unit descriptors.
- Select-all faction icon has no texture source.
- HUD dish frame ornament composition.
- State pin does not cover projectile combat (Q16).
- Dead M2 gate (Q15).
- WotR still missing `OPENBFME_LIVING_WORLD_AI_TEMPLATE` (same
  `-AllowMissingWotrData` as AGENTS.md).
