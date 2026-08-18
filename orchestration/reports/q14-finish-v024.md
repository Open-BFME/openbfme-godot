# Q14 finish — select 7 r4 faction packs, checkpoint, ship dist\v0.2.4 alpha

Lane: grok-q14-finish. Brief `orchestration/briefs/q14-finish-v024-grok.md`.
Exclusive tree access. Selection changed only via one `apply-selection-transaction`.
No hand-edit of `selection.json`. Pins only in `tools/gate-retail.ps1` and
`importer/tests/test_retail_gate_script.py`. Long jobs launched via WMI
`Win32_Process.Create` (hidden + redirected) so they survive the agent Job
Object. Logs under `workspace/logs/q14fin-*`.

**STOP fired: no.**

## Digests (r4 publication receipts, verified in both roots)

| pack | digest | role |
|---|---|---|
| `rotwk-men-vslice` | `a0fde4ac89596cab4d34beae3ebce0e33aa30323f99a73727706a29c85d315c0` | ACTIVE |
| `rotwk-elves-vslice` | `99f31f9d308b46abc6e0f74554bb534afd378b654eda1dc31407d49188bd3d61` | supplement |
| `rotwk-dwarves-vslice` | `ba3e0d12ac34f17d02249bd335db9a3d6a3f3a062a5c45d4ec8108ba14ff2633` | supplement |
| `rotwk-isengard-vslice` | `fab985f0f28bd28cad8d1e1ca8db0d7763946a66177319d27cf51983f55f64d8` | supplement |
| `rotwk-mordor-vslice` | `36238eaa8db3bb781e3433e23293d1c365186d4073cb93ce95e5f82968d9a8b2` | supplement |
| `rotwk-wild-vslice` | `e2ce0dd29a265053c13b4cd4edcc122238b9c1c4fca6b776c6286ff30ef9f987` | supplement |
| `rotwk-angmar-vslice` | `15720e1f102d3f8ae97beb0ba26329254f517516a180e3b8a07e8c8c5ec71663` | supplement |

EVA overlays, maps (`1739b613…`), neutrals, music, cursors, `bfme2-men-vslice`,
and the 79 missing-physical batches kept their prior digests.

## Selection

| moment | sha256 | active | supplements | address check |
|---|---|---|---|---|
| Step 0 | `04229f763fd6f41d3637d6747a73fe5a2b8e198dd6b3325907de90e1b08fa41e` | men `4f92c8a4…` | 99 | PASS packs=200 roots=2 |
| after one `apply-selection-transaction` | `4e3e702486154bff0af025f56254e7d1a2c6b5a5d1a7b87a873c5e9f460cb7af` | men `a0fde4ac…` | 99 | PASS packs=200 roots=2 |

Transaction receipt: `verified=true`, `changed=true`, `swaps=2`, both roots
byte-identical. Command (untracked):
`workspace/scratch/q14fin-apply-selection.ps1`.

r4 published the 7 packs to workspace only. This lane robocopied them to the
durable root (`%APPDATA%\Godot\app_userdata\Open BFME\content-packs`) then
sealed both sides (`SEAL_PACKS DONE packs=14 files_changed=44434`).
`publish-durable-pack.ps1` cannot copy unselected packs; the copy was the
same `/MIR` recipe that script uses.

## Checkpoint runner table

Sequential Godot, `OPENBFME_CONTENT=workspace\content-packs`. Every runner
mounted `rotwk-men-vslice/a0fde4ac…` (not the stale `4f92c8a4…`). Playable
census: 7 factions, **137 units** (was 136 after Q1/Q2 — NoldorWarrior
restored), 147 structures.

| runner | expect | measured | STOP? |
|---|---|---|---|
| `retail_spellbook_runner` | 218/0 | `passed=218 failed=0` | no |
| `retail_member_combat_runner` | 115/0 | `passed=115 failed=0` | no |
| `projectile_table_runtime_runner` | 4/0 | `passed=4 failed=0` | no |
| `goal_production_matrix_runner` | (Q1/Q2 after = 299/0) | `passed=299 failed=0` | no |
| `retail_slice_runner` | fail NAMES vs q13verify; no NEW names | `passed=370 failed=59`; 87 named FAILs, **0 new / 0 gone** vs q13verify | no |
| fortress men | Q1/Q2 after 113/1 | `passed=113 failed=1` (`men_precompiled_page_selector_fallback_is_named`) | no |
| fortress angmar | Q1/Q2 after 93/2 | `passed=93 failed=2` (`angmar_the_second_fortress_finishes_construction`, `runner_ran_every_section`) | no |
| `retail_lockstep_determinism_runner` | 5/0 | `passed=5 failed=0` | no |
| `retail_state_pin_runner` | `0e4bcdbf…` unchanged | `hash=0e4bcdbf7e9a8579ccf559f0ac3d83284413e7196ad1249d2eafd3eafd1dcadc` + `OK` | no |
| `boot_startup_runner` | solo | `44 checks, 0 failures` | no |

Slice +1 pass vs q13verify (`370` vs `369`) with the same 59 named failures.
That is the extra playable unit (elves NoldorWarrior), not a lost check.
Godot wrote `RETAIL_SLICE FAIL` lines to stderr (`q14fin-retail_slice_runner.err`).

No pack failed to mount. No STOP.

## Re-pin

`tools/gate-retail.ps1` and `importer/tests/test_retail_gate_script.py`:
RE-MEASURED 2026-08-18 Q14 v0.2.4 recook (7 faction packs, Q13 projectiles,
elves NoldorWarrior).

- `$expectedSelectionSha256` = `4e3e702486154bff0af025f56254e7d1a2c6b5a5d1a7b87a873c5e9f460cb7af`
- `$expectedSelectionActivePack` = `rotwk-men-vslice/a0fde4ac…`
- `$expectedSelectionSupplementalPacks` = same 99 entries with the 6 non-men
  faction vslices swapped

`pytest importer/tests/test_retail_gate_script.py` (pinned interpreter):
**12 passed in 0.16s**.

## Dist

`tools/Test-DistPipeline.ps1` → **25/25 PASS**.

Detached `Publish-DistBuild.ps1 -Godot .tools\godot\Godot_v4.7-stable_win64_console.exe
-AllowEnvDependentContent -AllowPackOwnedWotrData -AllowMissingWotrData -Zip`.
Did **not** refuse. 50m 11s. Self-sufficiency probe this time matched the
env-set census (`packs=102`).

| item | value |
|---|---|
| path | `C:\Users\Jonathan\Desktop\open-bfme\dist\v0.2.4` |
| zip | `C:\Users\Jonathan\Desktop\open-bfme\dist\v0.2.4.zip` (12.91 GB) |
| zip sha256 | `cccacf0391005382b4d95544c6b12e1bad0af95ef7cd7824bae71dc244e910da` |
| commit | `b51be4dc45739d807e6aff70e4c08ee171520cf8` |
| active in bundle | `rotwk-men-vslice/a0fde4ac89596cab4d34beae3ebce0e33aa30323f99a73727706a29c85d315c0` |
| selection sha | `4e3e702486154bff0af025f56254e7d1a2c6b5a5d1a7b87a873c5e9f460cb7af` |

Artifact proof (export `OpenBFME.exe --headless --quit-after 600` with
`OPENBFME_CONTENT` = the bundle's `content-packs\`, same as `run-with-log.bat`):
mounted `a0fde4ac…`, no durable fallback, **0** armor-contract lines, **0**
`ERROR` lines. Log `workspace/logs/q14fin-artifact-run.txt`.

Publisher noted the in-game menu still prints build 363 (`328f2fe`) — 3
commits behind the folder (`b51be4d`). That is Write-BuildInfo run before the
three release commits, as AGENTS.md "commit all four together" requires.
Not a publish refusal.

## Commits

- `fdbb648` `chore(content): re-pin selection after Q14 v0.2.4 recook`
- `6a537f2` `chore(release): v0.2.4 alpha`
- `b51be4d` `docs(patch-notes): v0.2.4 alpha`

## Left undone

- EVA recompose still the prior overlays (Q3, deferred to v0.2.5).
- New maps pack `79bd9584…` exists from r4 but was **not** selected (owner
  ruling).
- Full `gate-retail.ps1` Section A not run.
- Q11 armor-contract gate-step regex still red on the state-pin *gate* (hash
  itself is green). Artifact boot emitted none of those lines.
- WotR still missing `OPENBFME_LIVING_WORLD_AI_TEMPLATE` (same `-AllowMissingWotrData`
  as AGENTS.md).
