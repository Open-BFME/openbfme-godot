# Retail HUD external movies and renderer callbacks

This oracle closes the two bounded gaps left by the Palantir host-bridge
inventory: five authored external movie loads and five privileged native draw
callbacks. It is evidence and a typed API proposal only. No movie converter,
callback implementation, generic ActionScript bridge, or profile change is
included.

The payload-free contract is generated twice at
`.private/scratch/hud-external-movies/contract-{a,b}.json`. The two files are
byte-identical. Retail payloads stay under `.private`.

## Exact movie loads

`Palantir.InitialSetup` executes five unconditional `getURL2` / Flash
`loadMovie` operations in this exact authored order:

| Order | Movie | Exact target | Current sealed closure |
|---:|---|---|---|
| 0 | `InGameSpellBook.swf` | `SpellBookUI` | add archive |
| 1 | `InGameSideCommandBar.swf` | `SideCommandBar` | already present |
| 2 | `InGameHelpBox.swf` | `helpBox` | add archive |
| 3 | `InGameHeroSelect.swf` | `HeroSelectUI` | add archive |
| 4 | `InGamePlanningMode.swf` | `planningModeUI` | add archive |

All five are load-reachable in the Men/Fords slice. “Optional movie” was an
incorrect shorthand for feature visibility: spellbook, help, hero-select, and
planning-mode may remain hidden until invoked, but their packages are still
loaded during Palantir initialization. The side command bar is both loaded and
visibly used by normal selection play.

The lifecycle behavior is not one generic unload hook:

- Spellbook and side bar call their exact `OnApt...Loaded(GetFullName(this))`
  FSCommand and install a corresponding `onUnload` notification.
- Help box calls `_parent.OnHelpBoxMovieLoaded(this)` and installs
  `_parent.OnHelpBoxMovieUnloaded(this)`.
- Hero select calls `_parent.OnHeroSelectMovieLoaded(this)`; the Palantir
  parent installs the clip-specific unload callback.
- Planning mode calls `_parent.OnPlanningModeUILoaded(this)` and installs the
  matching parent unload callback.

The runtime must preserve those individual paths and the authored order. It
must not route a guessed shared load/unload event.

## Exact source delta

Only four new BIG closures are required. Their imports already resolve into
the sealed `libInGameImagesMain`, `libInGameUI`, and `PalantirExport` libraries.

| Archive | Files | Payload bytes | Contents |
|---|---:|---:|---|
| `apt/ingamespellbook.big` | 4 | 30,453 | APT, CONST, DAT, 1 RU geometry |
| `apt/ingamehelpbox.big` | 13 | 9,377 | APT, CONST, DAT, 10 RU geometry |
| `apt/ingameheroselect.big` | 27 | 2,282,905 | APT, CONST, DAT, 23 RU geometry, 1 TGA |
| `apt/ingameplanningmode.big` | 28 | 1,083,153 | APT, CONST, DAT, 24 RU geometry, 1 TGA |

The exact 72 path/size/SHA rows, archive SHA-256 values, catalog directory
hashes, parsed APT roots/imports, DAT image mappings, RU identities, and TGA
identities are in the private contract. The four archives add 3,405,888 source
bytes.

The profile extends the existing
`men-hud-apt-runtime-bundle`; a second generic movie resource would duplicate
the same APT libraries and lifecycle. The measured result is:

- prior closure: 189 sources;
- added: 72 sources;
- sealed runtime bundle: 261 sources / 10,700,284 bytes;
- sealed source aggregate:
  `f62347fb78065726715618ed9c73f152c678fec5646ddf7b0855825d1cb23599`.

The profile planner validates the exact private oracle contract, all 72
path/size/SHA rows, and the real catalog before emitting the source closure.
The production converter now consumes all 261 sources, emits the two added
atlases, and inventories the five authored load edges. The side command bar is
already a bound root layer. The four new movie targets remain exact
`external-movie-target-attachment-not-bound` blockers; they are not flattened
as guessed independent layers. Their measured newly runtime-reachable draw,
timeline, action, and clip-action counts are zero until attachment executes.
The completion plan is source-resolvable with zero missing inputs. Renderer-
callback parity remains a separate fail-closed gate.

## Renderer callbacks

Retail `game.dat` registers all five callbacks as 32-bit native member
functions with 16 bytes of stack arguments. The oracle hashes each exact code
range and binds it to its exact Palantir clip action.

| Callback | APT target | Proven responsibility | Slice reachability |
|---|---|---|---|
| `AptPalantir::ClipRadar` | `Radar/RadarClip` | Round two float2 rectangles and store clip bounds; draws no pixels | active with radar |
| `AptPalantir::RenderRadar` | `Radar` | Render radar/minimap inside the authored rectangle | active |
| `AptPalantir::RenderRadarViewBox` | `RadarPings` view-box child | Dispatch the camera view-box overlay renderer | active with radar overlay |
| `AptPalantir::RenderMovie` | `MoviePlayback` | Fit and render the current movie frame | surface loaded; dormant until playback |
| `AptPalantir::RenderGlobe` | `GlobeSwirlRender`, `BigGlobeSwirlRender` | Dispatch the engine globe renderer in the authored rectangle | reachable when either `_show` timeline is active |

The static APT records attach each `_type` callback through an event record
decoded by the existing parser as `unload`. That exact event identity is
retained in the oracle; implementation must not silently reinterpret its
timing.

Local OpenSAGE is observation evidence, not retail authority. It confirms the
radar surface split by binding the APT `RadarClip` to
`Scene3D.RadarDrawUtil.DrawRadarMinimap` and the sibling surface to
`DrawRadarOverlay`. Its radar utility consumes the map minimap texture,
heightmap, visible radar items/game objects, and camera for the view footprint.
The retail code-range hashes remain the authority for callback ABI and bounds.

## Smallest typed Godot boundary

The proposed interface is intentionally five typed calls, with no dynamic
callback name dispatch:

```text
set_radar_clip(origin: Vector2, size: Vector2)
render_radar(origin: Vector2, size: Vector2, state: RadarFrameState)
render_radar_view_box(state: CameraRadarFootprint)
render_movie(origin: Vector2, size: Vector2, frame: MovieFrameHandle)
render_globe(origin: Vector2, size: Vector2, state: GlobeRenderState)
```

`RadarFrameState` and `CameraRadarFootprint` can be supplied from authoritative
map/radar/camera state. The retail internal layouts behind `GlobeRenderState`
and `MovieFrameHandle` are not exposed by static names; those two remain
precise typed blockers rather than guessed dictionaries.

## Verification

```powershell
$env:PYTHONPATH = 'importer'
python -m pytest importer/tests/test_retail_hud_external_movies_oracle.py -q
python -m ruff check importer/openbfme_importer/retail_hud_external_movies_oracle.py importer/tests/test_retail_hud_external_movies_oracle.py
```

The focused gate is three passing tests plus clean Ruff. Two private CLI runs
must remain byte-identical.
