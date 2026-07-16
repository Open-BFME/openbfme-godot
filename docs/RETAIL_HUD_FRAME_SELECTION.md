# Retail HUD frame selection

`retail_hud_frame_selection.py` is the bounded oracle for the BFME2 Men HUD
frame choice. It does not execute ActionScript and it does not guess a frame.
It validates all 188 files in the sealed five-bundle HUD plan, validates
`window/controlbar.wnd`, and then decodes only the finite timeline and
bytecode ranges needed for the choice.

## Proven choice

The authored `InitialSetup` function passes `_good` to
`PalantirFrame.gotoAndPlay`. Character 105, the placed `PalantirFrame` sprite,
maps that label to frame 19. Frame 19 places local imported character 102,
which imports `PalantirExport::PalantirFrame_GoodDouble`; that export resolves
to character 19 in `PalantirExport.apt`.

The same exact sprite contains the other finite choices:

| State | Frame | Local import | Retail symbol | Export character |
|---|---:|---:|---|---:|
| `_good` | 19 | 102 | `PalantirFrame_GoodDouble` | 19 |
| `_goodSingle` | 9 | 101 | `PalantirFrame_GoodSingle` | 22 |
| `_evil` | 39 | 104 | `PalantirFrame_EvilDouble` | 13 |
| `_evilSingle` | 29 | 103 | `PalantirFrame_EvilSingle` | 16 |

`SetPalantirFrameState` accepts those four states and passes its original
`state` parameter, held in register 4, to `PalantirFrame.gotoAndPlay`. The
contract records the exact function body ranges and SHA-256 values as
provenance. Unknown state handling is fail-closed.

The APT bytes do not decide whether a later engine call should use `_good` or
`_goodSingle`. They prove both mappings and prove that the authored default is
`_good` (Good Double). The engine-side Men/alignment and single-versus-double
policy remains an explicit runtime integration requirement.

## Bounded production integration

`retail_hud_apt_convert.py` consumes only the proven authored default. Its
runtime contract declares policy `bounded-retail-initial-setup-only`, selects
character 105 frame 19, and flattens the resolved
`PalantirFrame_GoodDouble` geometry. This increases both planner and production
contracts from 26 to 28 draws; the two new draws are exact textured retail
triangles. The planner blocker count changes from 130 to 131 and the production
bundle count changes from 131 to 132 because each retains explicit blockers
for non-default Palantir states and the side-bar fade.

The Godot binder validates every identity in that chain. It rejects a contract
that substitutes `_goodSingle`, claims the side bar has faded in, or omits
either bounded-selection blocker. This is not a general APT timeline executor.

## Side command bar initial state

`InGameSideCommandBar.apt` begins at frame 0 with `ButtonSet` translated to
`[1048.300048828125, 361.29998779296875]`, outside the authored 1024-pixel
width. Frame 1 is labeled `_hide`; frames 1 through 10 do not mutate the
display list; frame 10 executes `Stop`. Therefore the authored initial state is
hidden/offscreen. Frame 11 is `_fadeIn`, and reaching it requires the runtime
to call `FadeIn` (normally after a unit selection). The WND contract agrees:
`ControlBar.wnd:CommandWindow` starts `ENABLED+HIDDEN+SEE_THRU`.

## Deterministic output

Run from the repository root:

```powershell
$env:PYTHONPATH = 'importer'
python -m openbfme_importer.retail_hud_frame_selection `
  --apt-plan .private/scratch/hud-apt-profile/plan.json `
  --asset-root .private/retail-work/cache/effective-assets `
  --output .private/scratch/hud-frame-selection/contract.json
```

The output is payload-free JSON. It records source hashes, byte ranges,
timeline identities, exact resolved symbols, the default selection, and the
remaining runtime blockers. It deliberately reports `parityReady: false`
until the external Men state call and selection-driven side-command visibility
are bound and tested.

Acceptance checks:

```powershell
$env:PYTHONPATH = 'importer'
python -m pytest importer/tests/test_retail_hud_frame_selection.py -q
python -m ruff check `
  importer/openbfme_importer/retail_hud_frame_selection.py `
  importer/tests/test_retail_hud_frame_selection.py
```

The finite APT record layout was cross-checked against OpenSAGE commit
`588ac477367a0022adf29f20a084e8873014e6ce`, but the emitted result is derived
from and attested to the local BFME2 retail bytes. OpenSAGE is not a runtime
dependency of this oracle.
