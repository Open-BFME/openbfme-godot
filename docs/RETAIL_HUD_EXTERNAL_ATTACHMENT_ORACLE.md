# Retail HUD external attachment oracle

The four previously blocked Palantir movies are exact child attachments, not
independent HUD roots. `Palantir.InitialSetup` issues the retail loads in this
order:

1. `InGameSpellBook.swf` into `Palantir.root.frame0/SpellBookUI`;
2. `InGameSideCommandBar.swf` into `Palantir.root.frame0/SideCommandBar`;
3. `InGameHelpBox.swf` into `Palantir.root.frame0/helpBox`;
4. `InGameHeroSelect.swf` into `Palantir.root.frame0/HeroSelectUI`;
5. `InGamePlanningMode.swf` into `Palantir.root.frame0/planningModeUI`.

The oracle covers the four targets other than the already root-bound side bar.
Each target is an authored instance of Palantir character 41, an empty one-frame
sprite. Loading replaces that child clip's content while retaining its parent,
depth, transform, color transform, and name.

| Target | Depth | Palantir translation | Source entry | Normal Men-v-Men conclusion |
| --- | ---: | ---: | --- | --- |
| `SpellBookUI` | 3 | `(0, 0)` | `_hide` frame 0; stop frame 8 | Loaded but dormant until the spell-book path shows it |
| `helpBox` | 176 | `(585, 607)` | child `box._hide`; stop frame 0 | Loaded but dormant until help content calls `Show(height)` |
| `HeroSelectUI` | 174 | `(375, 700)` | `_hide` frame 0; stop frame 8 | Requires one trace because `ShowHeroSelectInterface()` is called unconditionally but its body is absent |
| `planningModeUI` | 180 | `(512, 30)` | `_init` frame 0; stop frame 8 | Loaded but dormant until planning mode calls `Open()` |

No general ActionScript VM is required to attach any of the four. The Godot
boundary should be four typed `RetailHudMovieSlot` children with the exact
movie-specific methods and lifecycle signals emitted by the contract. A
generic root loader would lose authored coordinate inheritance and teardown
semantics.

## Lifecycle and teardown

SpellBook owns its `OnAptInGameSpellBookLoaded` and `...Unloaded` FSCommands.
Help and Planning call their named parent methods on load and from source-owned
`onUnload` handlers. Hero Select calls its parent load method; Palantir then
installs the clip's unload closure before dispatching `OnHeroSelectLoaded`.

Retail x86 retains separate handles for Hero Select at owner offset `+0xC4`,
Help at `+0xC8`, and Planning at `+0xCC`. The exact native reset path clears
those handles Hero -> Help -> Planning. SpellBook uses a separate FSCommand
registration path, so its relative removal order is intentionally unresolved.

The Hero Select record at source offset `166756` remains separate from the
movie attachment. It has flags `0xB6`, a null clip-action pointer, and record
SHA-256
`7cf6432cbd91629acd5252c69aa957a08cadffd61214ae49ed0e078dec99a135`.
It must not acquire a fabricated initialize or unload handler.

`ControlBar.wnd` remains an active semantic/control companion. None of the four
APT target names appears in that WND payload, so it is not attachment authority
for them and must not be used to invent alternate roots.

## Remaining traces

Only four narrow runtime observations remain:

- callback completion order after the five no-wait `getURL2` instructions;
- Hero Select's frame/visibility immediately around `ShowHeroSelectInterface`;
- the four unload callbacks and child removals during one Palantir teardown;
- HelpBox clip and alternate-anchor coordinates during its load callback.

These traces do not block the exact target paths, placements, source entry
frames, lifecycle identities, or typed Godot attachment interfaces.

## Verification

```powershell
$env:PYTHONPATH = 'importer'
python -m pytest importer/tests/test_retail_hud_external_attachment_oracle.py -q
python -m ruff check importer/openbfme_importer/retail_hud_external_attachment_oracle.py importer/tests/test_retail_hud_external_attachment_oracle.py
python -m ruff format --check importer/openbfme_importer/retail_hud_external_attachment_oracle.py importer/tests/test_retail_hud_external_attachment_oracle.py
```

The private A/B outputs live only under
`.private/scratch/hud-external-attachment-oracle/` and contain hashes and typed
metadata, never retail payload bytes.
