# Retail HUD external attachment runtime

The 261-source retail HUD converter now binds the four source-proven external
movies as replacements for the exact authored Palantir frame-0 child slots.
They are not independent HUD roots and do not require a general ActionScript
VM. `InGameSideCommandBar` remains the existing root-bound layer.

## Exact child-slot contract

All four placeholders are Palantir character 41. The runtime creates direct
`RetailHudAptRuntime` children with the authored name, depth, transform, and
identity color transform (`tint=[1,1,1,1]`, `additive=[0,0,0,0]`). It stages
all four before adding any child, so a malformed or colliding slot binds none.

| Load | Movie and typed interface | Exact path | Depth | Matrix; translation | Root frames, labels, initial stop | Default |
| ---: | --- | --- | ---: | --- | --- | --- |
| 0 | `InGameSpellBook` / `RetailSpellBookSlot` | `Palantir.root.frame0/SpellBookUI` | 3 | `[0.9998626708984375,0,0,0.9999847412109375]`; `(0,0)` | 18; `_hide=0`, `_show=9`; 8 | hidden, dormant |
| 2 | `InGameHelpBox` / `RetailHelpBoxSlot` | `Palantir.root.frame0/helpBox` | 176 | identity; `(585,607)` | 1; no root labels; 0 | hidden, dormant |
| 3 | `InGameHeroSelect` / `RetailHeroSelectSlot` | `Palantir.root.frame0/HeroSelectUI` | 174 | identity; `(375,700)` | 29; `_hide=0`, `_fadein=9`, `_show=19`; 8 | hidden pending captured show result |
| 4 | `InGamePlanningMode` / `RetailPlanningModeSlot` | `Palantir.root.frame0/planningModeUI` | 180 | identity; `(512,30)` | 27; `_init=0`, `_open=9`, `_close=19`; 8 | hidden, closed and dormant |

Initial setup remains the source order SpellBook, SideCommandBar, HelpBox,
HeroSelect, PlanningMode. The typed callback pairs are respectively
`OnAptInGameSpellBookLoaded/Unloaded`,
`AptPalantir::OnHelpBoxLoaded/Unloaded`,
`AptPalantir::OnHeroSelectLoaded/Unloaded`, and
`AptPalantir::OnPlanningModeUILoaded/Unloaded`. The converter records them but
the runtime does not dispatch them until lifecycle capture is complete.

Native evidence remains explicit: HeroSelect, HelpBox, and PlanningMode use
owner handles `+0xC4`, `+0xC8`, and `+0xCC`; the proven native reset order is
HeroSelect -> HelpBox -> PlanningMode. SpellBook is a separate FSCommand path.
Reset removes all four direct children atomically without guessing asynchronous
completion or unload order. The HeroSelect flags-`0xB6` null clip-action pointer
at source offset 166756 remains a separate diagnostic and is never fabricated
into a callback.

## Remaining blocker

The former four `external-movie-target-attachment-not-bound` blockers are gone.
Exactly one `external-movie-lifecycle-capture-not-passed` blocker remains and
carries these four unresolved retail traces:

- completion order for the five no-wait APT loads;
- HeroSelect visibility immediately around `ShowHeroSelectInterface()`;
- unload callback and child-removal order during Palantir teardown;
- HelpBox clip and alternate-anchor runtime coordinates.

No HeroSelect show state, asynchronous completion order, or unload order is
guessed. WND, timeline playback/selection, side-bar fade, rendered-text capture,
and the other existing bounded blockers are unchanged. Production counts remain
74 action scripts (60 supported), 22 timelines / 640 frames / 51 instances, 28
draws / 31 display items, and 28 clip actions / 28 events (27 executable). The
blocker count is now 22.

## Deterministic evidence and verification

Private A/B bundles are under
`.private/scratch/hud-external-attachment-runtime/bundle-a` and `bundle-b`.
Both contain 26 byte-identical outputs from 261 inputs. Their contract aggregate
SHA-256 is
`910238c83b07269b2637bb4bfb9247c14407d781f632e61673400c0eb2d2fa6b`;
the canonical source aggregate is
`f62347fb78065726715618ed9c73f152c678fec5646ddf7b0855825d1cb23599`.

```powershell
$env:PYTHONPATH = 'importer'
python -m pytest importer/tests/test_retail_hud_apt_convert.py -q
python -m ruff check importer/openbfme_importer/retail_hud_apt_convert.py importer/tests/test_retail_hud_apt_convert.py
python -m ruff format --check importer/openbfme_importer/retail_hud_apt_convert.py importer/tests/test_retail_hud_apt_convert.py

& '<HOME>\Downloads\godot47\Godot_v4.7-stable_win64_console.exe' --headless --path game --script res://tests/retail_hud_apt_runtime_runner.gd
& '<HOME>\Downloads\godot47\Godot_v4.7-stable_win64_console.exe' --headless --editor --path game --quit
```

The private runtime gate uses the same runner plus
`--retail-hud-apt-contract=<bundle-a contract>` and
`--retail-hud-apt-pack-root=<bundle-a root>` after Godot's `--` separator.
