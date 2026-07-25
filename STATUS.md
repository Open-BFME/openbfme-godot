# OpenBFME status

**Owns:** what is verified right now, and what is blocked.
**Does not own:** product scope ([DIRECTION.md](DIRECTION.md)) or engineering
sequence ([PLAN.md](PLAN.md)).
**Audited:** 2026-07-25, on branch `claude/rts-codebase-audit-overhaul-0be13f`.

Every number on this page was produced by running the command shown, in this
worktree, during this audit. Nothing is carried forward from an older status
document. If a figure is not here, it was not measured — treat any number found
elsewhere in the tree as historical, including the counts preserved inside the
consolidated `docs/RETAIL_*` references.

## Gate results

Run as `<godot> --headless --path game --script res://tests/<runner>.gd`. All
runs resolved content through the workspace root
(`[ModLoader] Using workspace content root`), **not** a stale durable user pack.

| Runner | Passed | Failed |
|---|---:|---:|
| `retail_slice_runner.gd` | 352 | 0 |
| `retail_spellbook_runner.gd` | 195 | 0 |
| `retail_pack_runner.gd` | 178 | 0 |
| `retail_hero_ability_runner.gd` | 134 | 0 |
| `retail_map_script_runner.gd` | 85 | 0 |
| `menu_skirmish_runner.gd` | 80 | **2** |
| `options_pause_runner.gd` | 15 | 0 |
| `retail_builder_construction_runner.gd` | 12 | 0 |

Importer suite — `python -m pytest importer/tests -q`:

```
2 failed, 1711 passed, 131 skipped, 916 subtests passed
```

## Known failures

Four failing assertions, all long-standing and all honest signals rather than
flaky tests.

| Failure | Where |
|---|---|
| `map_list_player_counts_from_pack` — the pack reports player counts `[2, 0, 0, 0, 0]` | `menu_skirmish_runner.gd` |
| `registered_maps_available` | `menu_skirmish_runner.gd` |
| `test_conversion_converts_units_and_structures_and_is_deterministic` | `importer/tests/test_faction_import.py` |
| `test_foundation_without_visuals_is_excluded_with_descriptor_evidence` | `importer/tests/test_faction_import.py` |

The two menu failures are map-content gaps, not shell defects: the maps the menu
enumerates do not all carry the player-count metadata the shell asks for.

## Content

Four packs are present under the workspace content root:

| Pack | Contents |
|---|---|
| `bfme2-men-elves-dwarves-isengard-mordor-wild-vslice` | The six BFME2 factions, composed |
| `rotwk-angmar-vslice` | Angmar, from RotWK 2.01 |
| `bfme2-skirmish-maps-private` | The cooked skirmish maps |
| `bfme2-men-vslice` | The original single-faction pack, retained |

**Seven faction slugs** are declared in
`game/src/retail_slice/retail_faction_manifest.gd`: `men`, `elves`, `dwarves`,
`isengard`, `mordor`, `wild`, `angmar`.

**Eight skirmish maps are cooked**: Evendim, Fords of Isen II, Grey Mountains,
Harlindon, Tournament Hills, Tournament Udun, Weather Hills, Withered Heath.

Coverage across the seven factions is uneven. **Mordor is the largest known
conversion gap** — its per-faction slice suite carries real failures where the
other six run clean. No figure is quoted here because the per-faction suites
were not re-run during this audit; run them to get a current number rather than
trusting a written one.

## Bounds that used to be single-faction sized

Cross-faction skirmish was blocked for a long time by limits sized for one
faction's content. Those have been raised. Current values in
`importer/openbfme_importer/profile.py`:

- `MAX_RESOURCES` = 32,768
- `MAX_PROFILE_BYTES` = 64 MiB

Composed packs are identified as `bfme2-<a>-<b>-…-vslice`. Note that
`DEFAULT_PACK_ID` in `retail_faction_manifest.gd` is still the literal
`bfme2-men-vslice`.

## Where the implementation differs from the stated target

Real gaps between [DIRECTION.md](DIRECTION.md)'s product constraints and the
shipped tree. These are unfinished sequencing, not bugs, and they are recorded
so nobody reads the direction document as a description of today.

| Direction says | Tree does |
|---|---|
| Deterministic authoritative simulation, separable from presentation | The simulation is GDScript under `game/src/retail_slice/`. The C# solution in `engine/` is a parallel lane that the Godot project does not load — `game/` contains no `.csproj`. |
| Presentation-independent simulation at the production rate | `game/src/core/sim_clock.gd` runs a fixed 10 Hz tick (`TICK_HZ := 10.0`). |
| RotWK 2.01 is the baseline; campaigns are in scope | `contracts/bfme2-106-product-scope.json` is still the 1.06 contract and still marks `retail-campaigns` as `excluded`. It carries a policy digest and validation tooling, so retargeting it is a deliberate, separate change. |

## Legacy scaffolding still wired in

`game/src/stage1..stage9`, `game/src/proof_stage3..proof_stage9`, the nine
`scenes/stage*_{arena,lab}.tscn`, and the stage 1-9 proof/visual runners are the
original proof ladder — roughly 13,000 lines of `game/src`. They are **not**
dead code:

- `tools/gate-stage10.ps1` chains `gate-stage9.ps1` → … → `gate-stage1.ps1`, and
  each of those invokes its stage's proof and visual runners.
- `game/scenes/boot.tscn` carries `Center/Stage1`…`Center/Stage9` buttons.
  `main_menu.gd::_collect_stage_buttons()` requires them to exist (it uses
  `get_node`, not `get_node_or_null`) and `_on_stage()` navigates straight to
  the stage scenes.
- `tests/stage10_boot_runner.gd` asserts every one of those nine buttons loads
  its real scene with a script attached.

Retiring this ladder is a legitimate cleanup, but it has to start at the boot
menu and the gate chain, not at the source directories.

Note that `tests/stage15_menu_runner.gd` is a *current* menu test despite its
name, and is part of `tools/gate-retail.ps1`. So are `stage11_12_runner.gd`,
`stage14_15_sim_runner.gd`, and `cli_runner.gd`.

## How to re-verify

```bat
set OPENBFME_GODOT=C:\Tools\Godot\Godot_v4.7-stable_win64_console.exe
%OPENBFME_GODOT% --headless --path game --script res://tests/retail_slice_runner.gd
%OPENBFME_GODOT% --headless --path game --script res://tests/menu_skirmish_runner.gd
%OPENBFME_GODOT% --headless --path game --script res://tests/options_pause_runner.gd
python -m pytest importer/tests -q
```

A runner that prints `Loading DURABLE user pack` instead of
`Using workspace content root` is reading stale content and its numbers are
void. A runner reporting failures is a failure even when it exits 0.
