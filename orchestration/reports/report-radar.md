# Q64 radar fidelity — diagnosis-first report

Date: 2026-08-21
Lane: `codex-radar-look`
Diagnosis commit basis: `140538c6`
Live content root: `C:\Users\Jonathan\Desktop\open-bfme\workspace\content-packs` (read-only; selection not changed)

## Evidence set

- Retail tactical references: `workspace/retail-work/oracle/captures/bfme2-fords-men-reference-youtube-z6ZI6wY_LYE-{500s,510s}.png`. The 500-second frame has the clearest populated radar; the 510-second frame proves the map ink stays axis-aligned while the camera view changes.
- Current composed capture: worktree `workspace/scratch/radar-diagnosis-current.png`, produced at 1920x1080 from `140538c6`. Rerun with `OPENBFME_CONTENT=C:\Users\Jonathan\Desktop\open-bfme\workspace\content-packs`, `OPENBFME_CAPTURE_PATH=<worktree>\workspace\scratch\radar-diagnosis-current.png`, and `Godot_v4.7-stable_win64_console.exe --path game --script res://tests/retail_render_capture_runner.gd`. Log: `workspace/logs/radar-look/diagnosis-current-capture-r3.txt`; result `RETAIL_RENDER_CAPTURE_OK`.
- The binding brief's `workspace/retail-work/captures/chrome-*` folders are War of the Ring browser captures, not tactical matches. They contain the strategic map and cannot prove tactical-radar appearance. They were inspected and rejected as radar evidence rather than silently cited.
- Pure authored oracle: `workspace/retail-work/editions/rotwk/cache/effective-assets`.
- Exact mounted map inputs during diagnosis: `bfme2-skirmish-maps-private/f9c14cfa4c25e68509373390741fc82e5892f050a2305a19fa3efaca0f39a5b0` and `rotwk-playable-maps-private/abc27325672bd5712d7785e23d5ff858133c86056f173032deb8c1de9141d4d1`. This differs from the older preamble's maps-pack digest; these are the live `selection.json` values observed on 2026-08-21. No selection file was edited.

## Delta table — committed before any fix

| Surface | Retail evidence | Open-BFME at `140538c6` | Authored source | Verdict / ranked action |
|---|---|---|---|---|
| Terrain / map ink | The Fords reference shows the pale parchment disc with the authored river/coast drawing, not a live terrain photograph. The drawing keeps the same map-grid orientation between 500s and 510s. | The fresh capture uses the same pale parchment treatment and the Fords river drawing. `RetailMinimap` binds the map row's `art`, never its `preview`, and maps it in source-grid space. | Oracle `maps/map mp fords of isen ii/map mp fords of isen ii_art.tga`, SHA-256 `b3c285cffd89f7949bee0a62cffbdb0d180f93cf1f940fb9c078ac6a2434795d`; mounted conversion `assets/ui/maps/fords-of-isen-ii-art.png`, SHA-256 `470b61e8b7f5608537b34df6c4f32fb4f3ee7b35005f709b7e5df33d933c301e`. All 8 selected BFME2 maps and all 74 selected RotWK maps carry a `*-art.png`. | **Closed before this lane.** No map importer work and no Q62 maps republish are owed for radar terrain. Do not replace the authored bitmap with generated texture-class colours. |
| Parchment / frame / mask | Retail shows the parchment clipped inside the Palantir metal, with the metal frame and three upper orb buttons outside the map surface. | Fresh capture has the authored parchment vignette and APT frame with no square spill. The radar disc clips its paper, ink, markers, and camera footprint to the bezel. | `Palantir.apt` placement via `retail_hud_stage.gd`; `apt-palantirexport-17-fb63d3d26008.png`; parchment region `Rect2i(4,4,214,214)` of `apt-palantir-1-d9888d52cd89.png`. | **Closed before this lane.** Geometry/placement remains out of scope exactly as the brief directs. |
| Unit / structure blip colour | Retail's populated Fords radar uses house-colour blue/red marks. RotWK authors its player colours in `data/ini/multiplayer.ini:52-151`; the first two `RGBColor` rows are blue `(70,91,156)` and red `(158,56,42)`. | `_draw` hard-codes neon `56b5ff` / `ff6259`, ignoring the match's selected house colours, then adds invented dark halos around every unit circle and structure square. The current capture makes those outlines read as debug markers. | `multiplayer.ini` `MultiplayerColor` blocks; match-selected colours already travel through `GameState` to `RetailHouseColor.team_color_overrides`. Object visibility category is authored by `RadarPriority` (`default/object.ini:159`; e.g. `fortress.ini:710`, `trebuchet.ini:469`). | **Priority 1.** Use the same authored/match-selected house-colour source as the models and delete the invented halo. Preserve circle-vs-square only as the existing engine-primitive approximation. No generic unit/structure blip bitmap or authored pixel-size setting exists in the mapped-image/INI census, so exact sizes remain a named engine-behaviour gap. |
| Blip admission | Retail `RadarPriority=INVALID` generally means absent; authored rows explicitly opt units/structures in or set `NOT_ON_RADAR`. | Every live sim entity and non-castle structure is drawn, regardless of its source object's radar priority. | `default/object.ini:159` plus per-object `RadarPriority` rows. | **Named content gap.** Selected playable documents contain no `radarPriority` field, so truthful filtering needs importer compilation plus faction recook. Do not invent classification from sim kind. |
| Camera view outline | Retail draws a bright, thick yellow view quadrilateral with a soft edge. The 500s and 510s frames show that it changes with the camera while the map does not. | Fresh capture draws a pale, thin procedural polygon (`Color(0.92,0.84,0.55,0.9)`, width `1.6`) and the fallback can become an oversized schematic wedge. | `mappedimages/handcreated/handcreatedmappedimages.ini:1680-1687`: `RadarViewBoxEdge`, `radar_view_box_edge.tga`, 8x8 atlas with crop `Left:1 Top:0 Right:8 Bottom:8`. Compiled DDS SHA-256 `c9cbb9902c08456617a77e545e14501e2069133536cb51bc48e115e8223bfba7`. | **Priority 2.** Bind and tile the authored edge texture. It is absent from both mounted interface-art indexes, so this requires an interface-art importer row and a future Men/UI pack recook; the current pack must fail visibly to the existing procedural fallback until then. |
| Camera rotation | Retail map art is map-axis-aligned; only the view outline changes between the two references. | `_world_to_radar` converts local battlefield positions back into source-grid coordinates, so map ink and blips do not rotate with `camera_user_yaw`; the frustum alone changes. | Map grid plus camera projection; existing `minimap_parchment_runner` pins the 35.76-degree Fords local/grid difference. | **Closed before this lane.** No rotation change. |
| Fog / shroud | The retail 500s world view is fogged on the right, but the whole parchment terrain remains visible. Blips, not the paper, carry visibility state. | Terrain is never blackened. Units require `unit_visible`; structures require explored visibility through `structure_visible`. | Retail capture plus the live shroud contract. | **Closed before this lane.** Adding a dark fog texture would regress the observed retail look. |
| Radar pings / attack alerts | Retail authors a 16-frame looping `RadarInfoAlert` (`animation2d.ini:313-334`) and `RadarMarkerClientUpdate`; `civilianprop.ini:38-39` authors 1000ms pulse frequency / 500ms duration. | No generic radar ping/attack-flash renderer is present in `RetailMinimap`. | `RadarInfoAlert_00000..00015` in `mappedimages/texturesize_512/radaralerts.ini`, plus the module/timing rows above. | **Named gap, not folded into the blip fix.** The 16 frames are absent from mounted interface-art, and there is no authoritative EVA/radar-event queue feeding the minimap. It needs an interface-art cook and a separately pinned event seam; a guessed flash is forbidden. |
| Water colour fallback | Retail authors `water.ini:74 RadarWaterColor = R:140 G:140 B:255`. | The fallback for a map without `art` uses sepia `PAPER_INK` at alpha 0.30. | `water.ini:74`. | **Dormant on the selected corpus:** every selected playable map has authored art. Keep named; do not spend a Q62 republish on a fallback no selected map reaches. |

## Search limits and honest gaps

- Mapped-image census found `RadarViewBoxEdge` and `RadarInfoAlert_00000..00015`, but no generic unit or structure blip art. The only defensible current shape source is the engine-level `RadarPriority` category plus the retail capture; exact blip pixel dimensions are not authored in the oracle and will not be guessed.
- The selected interface-art indexes contain neither `RadarViewBoxEdge` nor `RadarInfoAlert`; direct runtime consumption cannot become live until the relevant pack is recooked. This lane will add tests and the source/runtime seam, but will not select, publish, or mutate a pack.
- The first render attempt failed because the copied script-class cache was removed during startup; `Godot --headless --path game --import` repaired the worktree cache. A second 1280x720 attempt correctly failed because the renderer returned the project-fixed 1920x1080 viewport. The final default-viewport run is the green evidence cited above; failures remain in `workspace/logs/radar-look/`.

## Implemented presentation delta

- Radar units, structures, and castle fixtures now resolve through `RetailHouseColor.color_for_team`, including the match-selected `team_color_overrides` seam. The former neon-only literals and all invented brown marker underlays are gone.
- The interface-art reference census now includes engine-owned `RadarViewBoxEdge` whenever the retail mapped-image corpus defines it. A live pure-oracle plan resolves `radar_view_box_edge.tga` to `art/compiledtextures/ra/radar_view_box_edge.dds`, crop `(1,0) 7x8`, output `radarviewboxedge-1ba43f8b.png`, with no gap.
- `RetailMinimap` accepts only that exact 7x8 crop, tiles it along the clipped camera polygon, and records `RadarViewBoxEdge` as the source. `RetailHud` resolves it through the mounted interface-art index and records a loud `authored-radar-fallback` diagnostic when it is absent.
- Terrain, APT placement, map orientation, shroud simulation, and every simulation rule remain untouched. No map cook is needed: selected map art coverage was already complete.

## Incremental commits

1. `de623a96` — diagnosis delta table and Q64 claim, before any test or production change.
2. `2d1ff7e4` — failing-first blip/importer contracts.
3. `a3f1f7fe` — authored match-color blips and halo removal.
4. `d92cb133` — interface-art engine-owned radar reference.
5. `3f065bce` — failing-first exact camera-edge crop contract.
6. `a5b8e1a1` — runtime/HUD authored camera-edge binding and named fallback.

Every commit carries `Co-Authored-By: Codex Sol <noreply@openai.com>`. The lane used the pre-existing `worktree-agent-ab31abe8952bfb670` branch ref because this supplied worktree directory has no `.git` registration; an isolated Git index preserved the concurrently advancing parent checkout.

## Failing-first and final evidence

- Red importer: `1 failed`, `RadarViewBoxEdge` reference count `0`; `workspace/logs/radar-look/red-interface-art-radar.txt`.
- Red radar look: `0/4` before the blip fix, then `4/3` before the camera-edge binding; `red-radar-look.txt`, `red-radar-view-box.txt`.
- Green focused radar: `7/0`; `green-radar-view-box.txt`.
- Interface-art focused suites: `33 passed`, two pre-existing Pillow deprecation warnings; `green-interface-art-radar.txt`.
- Minimap authored parchment/registration: `32/0`, including Amon Sul correlation `0.3159` versus full-grid `0.0296` and vertical-flip `0.0681`; `precommit-minimap-parchment.txt`. The runner still prints its existing missing-directory diagnostics for packs that do not own palantir art.
- Required HUD: `124/0`; `precommit-retail-four-unit-hud.txt`.
- Required radial: `48/0`; `gate-retail-radial-layout.txt`.
- Required boot: first run `44/1` because `shell_visible` arrived at `12421ms` against the `12000ms` wall-clock budget; immediate unchanged warm rerun `44/0`. Logs: `gate-boot-startup.txt`, `gate-boot-startup-r2.txt`. The runner's deliberate `no_such_screen.gd` negative probe still emits expected engine errors.
- Required castle live boot on `rotwk.map.wor-erebor`: `8/0`; `gate-castle-map-live-boot.txt`. Existing named gaps remain environment fog, trebuchet locomotor data, and castle AI script libraries.
- Simulation pins are byte-unmoved:
  - state: `b025d16237ff644d66211a9cc26872f18b61520b9a377f11e9e99c6eceb43f58`
  - projectile: `709def7cadaf6c91079697a343f437f71d6a2b10d71238248f57b171f8486e7f`
  - pathing: `2e5ad58054d28dc93f37ef4728549bb538f6d4a1c22be922ec19b59fb2d1b12d`

## Eyes-required result and remaining gaps

The final 1920x1080 Forward+ capture is `workspace/scratch/radar-look-final.png`; its gate log is `workspace/logs/radar-look/final-render-capture.txt`. Visual review confirms that the parchment/map ink, frame/mask, and map-axis registration remain intact, while the clustered blue unit markers no longer carry the heavy debug-like dark outline.

The selected BFME2/RotWK interface-art packs still do not contain `RadarViewBoxEdge`, so the final capture truthfully shows the named thin procedural camera-line fallback. This lane did not recook, select, or publish a pack. Closing that last visible camera-edge delta requires a future immutable interface-art pack cook that includes `radarviewboxedge-1ba43f8b.png`; it is not a maps-pack/Q62 republish. `RadarPriority` admission and `RadarInfoAlert` event animation also remain the named data/event gaps from the diagnosis table.
