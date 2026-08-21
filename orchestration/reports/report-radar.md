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
| Camera view edge art | Retail draws a bright, thick yellow view quadrilateral with a soft edge. | Fresh capture draws a pale, thin procedural line (`Color(0.92,0.84,0.55,0.9)`, width `1.6`). | `mappedimages/handcreated/handcreatedmappedimages.ini:1680-1687`: `RadarViewBoxEdge`, `radar_view_box_edge.tga`, 8x8 atlas with crop `Left:1 Top:0 Right:8 Bottom:8`. Compiled DDS SHA-256 `c9cbb9902c08456617a77e545e14501e2069133536cb51bc48e115e8223bfba7`. | **Named pack gap.** The runtime seam and importer reference are present, but both selected interface-art indexes lack the crop. `a5b8e1a1` therefore bound a validated seam plus a loud procedural fallback; its subject line's “authored view box edge” was an overclaim about what the live selected pack could render. |
| Camera footprint geometry | Retail's 500s view box is a sane four-corner gold box about one fifth of the disc. | Round 1 independently clamped each off-map ray hit to `map_bounds`; two far hits can collapse onto one corner, producing the verifier-rejected pale wedge with only three distinct vertices. | Camera frustum plus the measured one-fifth-disc reference; no authored INI geometry row exists. | **Round 2 blocker fixed.** Cap far-ray projection at the camera-focus distance, retain all four ray directions, fit the intact quad to the measured 0.20 longest-axis fraction, then translate the whole quad inside the map. No per-corner clamp remains. |
| Camera rotation | Retail map art is map-axis-aligned; only the view outline changes between the two references. | `_world_to_radar` converts local battlefield positions back into source-grid coordinates, so map ink and blips do not rotate with `camera_user_yaw`; the frustum alone changes. | Map grid plus camera projection; existing `minimap_parchment_runner` pins the 35.76-degree Fords local/grid difference. | **Closed before this lane.** No rotation change. |
| Fog / shroud | The retail 500s world view is fogged on the right, but the whole parchment terrain remains visible. Blips, not the paper, carry visibility state. | Terrain is never blackened. Units require `unit_visible`; structures require explored visibility through `structure_visible`. | Retail capture plus the live shroud contract. | **Closed before this lane.** Adding a dark fog texture would regress the observed retail look. |
| Radar pings / attack alerts | Retail authors a 16-frame looping `RadarInfoAlert` (`animation2d.ini:313-334`) and `RadarMarkerClientUpdate`; `civilianprop.ini:38-39` authors 1000ms pulse frequency / 500ms duration. | No generic radar ping/attack-flash renderer is present in `RetailMinimap`. | `RadarInfoAlert_00000..00015` in `mappedimages/texturesize_512/radaralerts.ini`, plus the module/timing rows above. | **Named gap, not folded into the blip fix.** The 16 frames are absent from mounted interface-art, and there is no authoritative EVA/radar-event queue feeding the minimap. It needs an interface-art cook and a separately pinned event seam; a guessed flash is forbidden. |
| Water colour fallback | Retail authors `water.ini:74 RadarWaterColor = R:140 G:140 B:255`. | The fallback for a map without `art` uses sepia `PAPER_INK` at alpha 0.30. | `water.ini:74`. | **Dormant on the selected corpus:** every selected playable map has authored art. Keep named; do not spend a Q62 republish on a fallback no selected map reaches. |

## Search limits and honest gaps

- Mapped-image census found `RadarViewBoxEdge` and `RadarInfoAlert_00000..00015`, but no generic unit or structure blip art. The only defensible current shape source is the engine-level `RadarPriority` category plus the retail capture; exact blip pixel dimensions are not authored in the oracle and will not be guessed.
- The selected interface-art indexes contain neither `RadarViewBoxEdge` nor `RadarInfoAlert`; direct runtime consumption cannot become live until the relevant pack is recooked. This lane will add tests and the source/runtime seam, but will not select, publish, or mutate a pack.
- The first render attempt failed because the copied script-class cache was removed during startup; `Godot --headless --path game --import` repaired the worktree cache. A second 1280x720 attempt correctly failed because the renderer returned the project-fixed 1920x1080 viewport. The final default-viewport run is the green evidence cited above; failures remain in `workspace/logs/radar-look/`.

## Implemented presentation delta

- Radar units, structures, and castle fixtures now resolve through `RetailHouseColor.color_for_team`, including the match-selected `team_color_overrides` seam. Round 1's `a3f1f7fe` correctly shared the seam and removed halos, but incorrectly called its inherited `(45,77,172)/(166,32,28)` defaults authored. Round 2 replaces that zero-oracle-hit table with all ten exact `multiplayer.ini:52-151` `RGBColor` rows and makes it the setup, lobby, GameState, model-mask, and radar default source.
- The interface-art reference census now includes engine-owned `RadarViewBoxEdge` whenever the retail mapped-image corpus defines it. A live pure-oracle plan resolves `radar_view_box_edge.tga` to `art/compiledtextures/ra/radar_view_box_edge.dds`, crop `(1,0) 7x8`, output `radarviewboxedge-1ba43f8b.png`, with no gap.
- `RetailMinimap` accepts only that exact 7x8 crop, tiles it along the bounded camera quad, and records `RadarViewBoxEdge` as the source. `RetailHud` resolves it through the mounted interface-art index and records a loud `authored-radar-fallback` diagnostic when it is absent; the round-2 runner behaviorally gates that diagnostic.
- Terrain, APT placement, map orientation, shroud simulation, and every simulation rule remain untouched. No map cook is needed: selected map art coverage was already complete.

## Incremental commits

1. `de623a96` — diagnosis delta table and Q64 claim, before any test or production change.
2. `2d1ff7e4` — failing-first blip/importer contracts.
3. `a3f1f7fe` — shared match-color seam and halo removal; its “authored” default claim was rejected and is corrected by `f20c62b7`.
4. `d92cb133` — interface-art engine-owned radar reference.
5. `3f065bce` — failing-first exact camera-edge crop contract.
6. `a5b8e1a1` — runtime/HUD camera-edge validation seam and named fallback; its subject overclaimed live authored rendering because selected packs lack the crop.
7. `209eb077` — round-2 failing-first default-palette, off-map-footprint, behavioral blip-layer, and fallback-diagnostic contracts (`10/6` red).
8. `f20c62b7` — exact ten-row authored default palette across every setup/runtime seam.
9. `7b78f484` — bounded four-corner camera footprint; no independent corner clamping.

Every commit carries `Co-Authored-By: Codex Sol <noreply@openai.com>`. Round 2 used the registered worktree at `C:\Users\Jonathan\Desktop\open-bfme\.claude\worktrees\radar-fix`; `.git` is the required pointer file to the parent repository's `worktrees/radar-fix` entry.

## Failing-first and final evidence

- Red importer: `1 failed`, `RadarViewBoxEdge` reference count `0`; `workspace/logs/radar-look/red-interface-art-radar.txt`.
- Red radar look: `0/4` before the blip fix, then `4/3` before the camera-edge binding; `red-radar-look.txt`, `red-radar-view-box.txt`.
- Green focused radar: `7/0`; `green-radar-view-box.txt`.
- Round-2 blocking red: `10/6`; the four default-palette checks failed, and the off-map 1024x768 pose collapsed to three distinct corners (`(60,45)` duplicated); `workspace/logs/radar-look-round2/red-blocking-tests.txt`.
- Round-2 focused green before the full sweep: `16/0`; `workspace/logs/radar-look-round2/green-blocking-tests.txt`.
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

### Round-2 raw gate sweep (`7b78f484` + `6b5c3b12`)

| Gate | Raw result | Log |
|---|---:|---|
| `radar_look_runner` | `16 passed / 0 failed` | `workspace/logs/radar-look-round2/gate-radar-look.txt` |
| `retail_four_unit_hud_runner` | `124 passed / 0 failed` | `workspace/logs/radar-look-round2/gate-retail-four-unit-hud.txt` |
| `retail_radial_layout_runner` | `48 passed / 0 failed` | `workspace/logs/radar-look-round2/gate-retail-radial-layout.txt` |
| `castle_map_live_boot_runner` (`rotwk.map.wor-erebor`) | `8 passed / 0 failed` | `workspace/logs/radar-look-round2/gate-castle-map-live-boot.txt` |
| `retail_state_pin_runner` | `b025d16237ff644d66211a9cc26872f18b61520b9a377f11e9e99c6eceb43f58` / match | `workspace/logs/radar-look-round2/gate-retail-state-pin.txt` |
| `retail_projectile_pin_runner` | `709def7cadaf6c91079697a343f437f71d6a2b10d71238248f57b171f8486e7f` / match | `workspace/logs/radar-look-round2/gate-retail-projectile-pin.txt` |
| `retail_pathing_pin_runner` | `2e5ad58054d28dc93f37ef4728549bb538f6d4a1c22be922ec19b59fb2d1b12d` / match | `workspace/logs/radar-look-round2/gate-retail-pathing-pin.txt` |

Extra original-brief boot coverage is honestly red on timing only: `boot_startup_runner` completed all 44 functional checks but reported two budget failures on each attempt. Warm attempt: first frame `8280ms > 7000ms`, shell visible `14871ms > 12000ms`; `workspace/logs/radar-look-round2/gate-boot-startup-warm.txt`. Post-import cold attempt: `10652ms` / `20403ms`; `gate-boot-startup-post-import.txt`. No functional check failed.

Palette knock-on probe: `menu_match_cycle_runner` passed the authored selection/reset checks and finished `70/2`; its two failures were the existing object-count growth checks (`+42`, `+84`), log `workspace/logs/radar-look-round2/knockon-menu-match-cycle.txt`. `menu_skirmish_runner` could not reach its later palette assertion on this branch because it aborts at the pre-existing missing `FACTIONS_BLOCKED_FROM_PLAY` property after mounted-map validation failures; raw partial log `knockon-menu-skirmish.txt`. The focused radar runner directly gates the ten lobby/default values and GameState/radar consumers.

## Eyes-required result and remaining gaps

Round-2 replacement capture: `workspace/scratch/radar-look-round2-7b78f484.png` (`1920x1080`, 3,227,521 bytes); gate log `workspace/logs/radar-look-round2/capture-radar-look-round2.txt`, `RETAIL_RENDER_CAPTURE_OK`. The former wedge is a small four-corner gold box and the default blip is the exact authored blue `(70,91,156)`. The line remains the named procedural fallback because the selected interface-art packs still lack `RadarViewBoxEdge`; the geometry is fixed independently of that pack gap.

The final 1920x1080 Forward+ capture is `workspace/scratch/radar-look-final.png`; its gate log is `workspace/logs/radar-look/final-render-capture.txt`. Visual review confirms that the parchment/map ink, frame/mask, and map-axis registration remain intact, while the clustered blue unit markers no longer carry the heavy debug-like dark outline.

The selected BFME2/RotWK interface-art packs still do not contain `RadarViewBoxEdge`, so the final capture truthfully shows the named thin procedural camera-line fallback. This lane did not recook, select, or publish a pack. Closing that last visible camera-edge delta requires a future immutable interface-art pack cook that includes `radarviewboxedge-1ba43f8b.png`; it is not a maps-pack/Q62 republish. `RadarPriority` admission and `RadarInfoAlert` event animation also remain the named data/event gaps from the diagnosis table.

## Round 3 (coordinator, per verifier G1/G2)

- G2: `menu_skirmish_runner.gd` stale assertions fixed — dropdown check is now
  `color_dropdowns_offer_the_ten_authored_colors` (10, was 8), the old invented
  Green `Color8(46,125,50)` selections now use the authored Green
  `Color8(62,152,100)` (multiplayer.ini:82-83), and
  `_select_option_by_metadata_value` fails loudly on a metadata miss instead of
  silently no-opping (the mechanism that hid the dead assertion).
- KNOCK-ON (named, per verifier): the authored palette changes the skirmish
  color dropdowns from 8 invented rows to the 10 authored rows, lobby
  names/indices/validation, GameState defaults, model house-color masks 0-9,
  and radar blips.
- G1: the camera footprint is now the TRUE camera quad clipped against the map
  rectangle via Geometry2D.intersect_polygons — no fitted size constant; the
  box tracks the real camera and is edge-clipped exactly like retail's. The
  0.20 centroid-fit heuristic and its area test are gone; the tests now pin
  clip-truthfulness (positive area < map, convex, boundary-inclusive).

