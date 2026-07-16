# Retail HUD Men/Fords reachability oracle

This fail-closed oracle challenges two HUD requirements against the exact
BFME2 1.06 executable, authored APT, retail INIs, and the current Men/Fords
selection sources. It emits hashes and control-flow facts only; retail payload
stays under `.private`.

## Decision

The side-command research blocker is closed enough to implement. The Palantir
selector blocker remains, and one deliberately narrow retail trace gate remains
for exact native-alias parity.

| Requirement | Decision | Exact reason |
|---|---|---|
| `palantir-nondefault-selection-not-bound` | Retain | The native chooser can return `_good`, `_goodSingle`, `_evil`, or `_evilSingle`. The Fords setup does not bind its unidentified selector bytes to Men-vs-Men. Forcing `_good` would be a guess. |
| `side-command-bar-fade-in-not-bound` | Delete the broad research blocker after binding | Existing typed simulation fields resolve all four battalions and five structures to nonempty authored command sets. Exact FadeIn target and completion behavior are sealed; this oracle does not edit the runtime binding. |
| `side-command-native-row-alias-trace` | Retain as one narrow gate | One retail trace must attach exact semantic aliases to definition bit `0x40`, local-player `+0x750`, and accepted-row byte `+0x101`. It does not block the typed Godot implementation, only a claim of exact native-alias parity. |
| "FadeIn must execute unconditionally at match load/start" | Delete this overbroad wording | The loaded callback only stores the movie root and changes native state `0 -> 1`. Eligible selection dispatches FadeIn; automatic first-tick selection is not proved. |

The active slice is a human Men versus AI Men skirmish on Fords of Isen II.
Network multiplayer is not used as a premise.

## Exact Palantir route

The chooser at `0x006d2e57` is called at `0x006d666b`. When its result differs
from owner offset `+0xe8`, the caller passes it to `0x007fea18`, which indexes
the state-string table and invokes APT `SetPalantirFrameState`.

The chooser priority is:

1. If `[0x00dfef10]` exists and `0x0044253a` sees both raw bytes `+0xb4` and
   `+0xb5` nonzero, raw byte `[[0x00e02d6c]+0x2c]` selects `_goodSingle`
   (zero, index 2) or `_evilSingle` (nonzero, index 4).
2. Otherwise, if `[0x00dfe78c]` exists and `0x00442219` succeeds, raw byte
   `((0x006a7e14([0x00dfeee8]))+0x34)+0x1bc` selects `_good` (zero, index 1)
   or `_evil` (nonzero, index 3).
3. Otherwise return `_good` (index 1).

Those offsets remain semantically unnamed. Static executable evidence proves
the branches, not the meanings of the raw selectors.

| Native index | APT state | Authored variant | APT frame (zero-based) |
|---:|---|---|---:|
| 1 | `_good` | `PalantirFrame_GoodDouble` | 19 |
| 2 | `_goodSingle` | `PalantirFrame_GoodSingle` | 9 |
| 3 | `_evil` | `PalantirFrame_EvilDouble` | 39 |
| 4 | `_evilSingle` | `PalantirFrame_EvilSingle` | 29 |

Minimum implementation is one typed four-result chooser and a fail-closed
trace/assertion for the two raw selectors. No generic APT dispatcher is needed.

## Exact side-command FadeIn route

The authored root timeline uses one-based bounds 12-22 for FadeIn and 32-42
for FadeOut. `FadeIn` is defined at APT offset 8809, body `[8836,9009)`.
Outside current frames `[32,42)`, it calls `gotoAndPlay("_fadeIn")`, targeting
one-based frame 12. Inside the range it calls
`gotoAndPlay(12 + 42 - currentframe)`, reversing FadeOut: 32 maps to 22, 37 to
17, and 41 to 13. Boundary frames 31 and 42 map to 12.

The exact interaction order is:

1. `OnAptInGameSideCommandBarLoaded` (`0x009283c9`) stores the root at owner
   `+0x18`, then writes state 1.
2. Update (`0x009287ea`) resolves the selected ID and validates an eligible,
   locally owned object plus predicate `0x009285ef`.
3. In a state other than 2 or 3, `0x00928349` invokes `FadeIn`, then writes
   state 2.
4. APT `FadeIn` runs `root.gotoAndPlay(target)`.
5. On zero-based frame 21 (one-based 22), authored bytecode issues
   `FSCommand:OnAptInGameSideCommandBarFadeInComplete`.
6. Callback `0x00928240` changes state 2 to settled-visible state 3. Playback
   continues through settled frames and stops at zero-based 30 (one-based 31).

This proves FadeIn is selection-reachable, not unconditional at load/start.

## Exact selection and command eligibility

Fanout `0x006d363e` passes the selected object to `0x0092854c`, which copies ID
`+0x74` into side-command owner `+0x1c`. Update resolves the ID through
`[0x00dfe78c]+0xb4`, requires definition byte `+0x109 & 0x40`, resolves owner
through object `+0x304`, requires the local player, and requires local-player
dword `+0x750 == 0`.

Eligibility function `0x009285ef` scans 32 command candidates from
`[0x00e01cfc]+0xdc` through `+0x158`. Candidate `+0x2c` and then row `+0x14`
resolve the row; byte `+0x101` must be nonzero. At most 15 rows are materialized
and shown. It returns true exactly when at least one row is accepted.

The typed Godot contract uses existing mutually exclusive selection state:
sorted `selected_ids` plus entity `team`, `health`, `unit_type`; or
`selected_structure_id` plus structure `team`, `health`, `structure_kind`; and
`winner` plus local team 0. Living, local, in-progress selections map as follows:

| Godot selector | Retail object | Retail command set |
|---|---|---|
| `bfme2.object.gondor-fighter-horde` | `GondorFighterHorde` | `GondorFighterHordeCommandSet` |
| `bfme2.object.gondor-tower-guard` | `GondorTowerShieldGuardHorde` | `GondorTowerShieldGuardCommandSet` |
| `bfme2.object.gondor-archer` | `GondorArcherHorde` | `GondorArcherHordeCommandSet` |
| `bfme2.object.gondor-knight` | `GondorKnightHorde` | `GondorKnightHordeCommandSet` |
| `fortress` | `MenFortressCitadel` | `MenFortressCommandSet` |
| `farm` | `GondorFarm` | `SellableCommandSet` |
| `barracks` | `GondorBarracks` | `GondorBarracksCommandSet` |
| `archery_range` | `GondorArcherRange` | `GondorArcheryCommandSet` |
| `stable` | `GondorStable` | `GondorStablesCommandSet` |

All nine object-to-command-set links and every referenced command button are
validated against pinned retail INIs. Each set has an `InPalantir=Yes` row.
The four battalion sets share authored `OK_FOR_MULTI_SELECT` Toggle Stance,
Attack Move, and Stop rows, so mixed battalion selection is also eligible.

## Run

```powershell
$env:PYTHONPATH = "importer"
python -m openbfme_importer.retail_hud_men_fords_reachability_oracle `
  --frame-contract .private/scratch/hud-frame-selection/contract-a.json `
  --map .private/content-packs/bfme2-five-maps-106-private/maps/fords-of-isen-ii/map.json `
  --setup .private/content-packs/bfme2-five-maps-106-private/maps/fords-of-isen-ii/setup.json `
  --game-dat F:/BFME2/game.dat `
  --side-apt .private/retail-work/cache/effective-assets/InGameSideCommandBar.apt `
  --output .private/scratch/hud-men-fords-reachability/contract.json
python -m pytest -q importer/tests/test_retail_hud_men_fords_reachability_oracle.py
python -m ruff check importer/openbfme_importer/retail_hud_men_fords_reachability_oracle.py importer/tests/test_retail_hud_men_fords_reachability_oracle.py
```

The oracle pins `game.dat`, relevant native ranges, Fords map/setup, HUD frame
contract, nine retail object/command-set sources, current Godot selection
sources, and side-command APT FadeIn/completion bytecode. Any drift fails closed.
