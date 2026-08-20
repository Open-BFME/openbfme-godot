# Q51 — walkable wall tops: layered pathing runtime — report

Date: 2026-08-20 · Owner: agent-a6e9faff0ef40248d · Brief: `C:\Users\Jonathan\Desktop\open-bfme\workspace\orchestration\fable-wave\castle-lanes\brief-q51-nav-layer.md`
Baseline HEAD: `9561edbce5bfaf77c9dd836f52afc8f5a185a3a2` · Implementation HEAD: `3d6684eb2dd1abab57c50d83813a6778969634ce` · Selected pack used for every Godot measurement: `rotwk-playable-maps-private/abc27325672bd5712d7785e23d5ff858133c86056f173032deb8c1de9141d4d1` (from `C:\Users\Jonathan\Desktop\open-bfme\workspace\content-packs\selection.json`)

## Result (one paragraph)

Implemented; awaiting fresh-context verification. The runtime now consumes shipped `walkSurfaces`, transforms the exact named GLB triangles through their authored HLOD pivots, rasterizes a second deterministic navigation layer, permits transitions only at authored ramp low ends, carries layer/elevation through simulation, and presents units at the authored surface height. The pack-backed Erebor vertical slice is closed (`20/0`, six authored roles, zero gaps), all required regression gates meet their named baselines, and ordinary valid ground routes still return before the wall-bridge query; the main state hash remains unchanged. Minas Tirith remains the sole castle-AI failure at `104/1`: its Player_1 pocket reaches zero authored ramp portals while the main side reaches one, and the shipped map data also reports 24 missing mesh roles plus 36 ramp roles with no ground endpoint. That is a pack/importer/map-topology follow-up, not a justification for synthesized geometry or a map hack.

## Commits

- `3d6684eb2dd1abab57c50d83813a6778969634ce` — Implement layered castle wall pathing

## Design choices

- `walkSurfaces` is authoritative geometry, not a hint. Each shipped role is resolved against the bound structure GLB, composed through authored fixture/HLOD transforms, triangulated, and rasterized without synthesized surfaces. Missing GLBs, missing named mesh roles, or ramps with no ground endpoint produce named gap receipts and fail closed.
- Deck roles (`wallBoundsMesh`, `raisedWallMesh`) form the second navigation layer. Ramp roles (`rampMesh1`, `rampMesh2`) are the only layer portals. The portal is the authored ramp's low-elevation endpoint adjacent to ground; deck edges and wall faces never become implicit portals. This follows the spike's retail evidence in `helmsdeepbuildings.ini:1315-1316,1469-1470,1859-1861`, `minastirithbuilding.ini:270,1131-1132`, and `campsandcastles.ini:4362,4482-4484` that P surfaces are wall/deck pathing proxies and R surfaces are ramp proxies.
- Deck connectivity is derived from overlapping rasterized authored triangles. Disconnected deck components refuse with `disconnected-wall-deck`; direct ground-to-deck requests without a ramp refuse with `wall-face-ascent-refused`; a deck with no usable descent refuses with `wall-descent-unavailable`.
- The simulation asks the existing ground grid first and returns its valid route unchanged. Only a failed ground route may attempt ground → ramp → connected deck → ramp → ground bridging. Units that never touch a walk surface therefore retain the pre-lane route bytes, confirmed by the unchanged 3,000-tick state pin.
- Route waypoints carry deterministic layer, role, and authored elevation metadata. Simulation owns these values; presentation reads the simulation's surface elevation instead of sampling terrain beneath a wall top.
- `install_walk_surface_cells_for_test` is a test seam into the same layer builder, not a second implementation. The final runner also loads the selected Erebor fixture/GLB data and drives a real simulation unit through an authored ramp onto an authored deck.

## Definition of Done — verbatim

| # | Criterion | Result | Evidence (log path + the actual line) |
|---|---|---|---|
| 1 | “wall-top walk surfaces form a SECOND pathing layer. Entry/exit is ONLY via authored ramp surfaces” | PASS | `workspace/logs/q51-castle-wall-walk.txt`: `CASTLE_WALL_WALK_RESULT passed=20 failed=0`; pack-backed receipt: `RETAIL_MAP_DATA_WALK_SURFACES authored=6 cells=536 ramp_cells=96 portals=3 gaps=0` |
| 2 | “the existing ground-layer pathing behavior must be BYTE-IDENTICAL ... the main state pin ... must stay UNTOUCHED and green” | PASS | `workspace/logs/q51-gate-retail-state-pin.txt`: `RETAIL_STATE_PIN ticks=3000 hash=b025d16237ff644d66211a9cc26872f18b61520b9a377f11e9e99c6eceb43f58`; `RETAIL_STATE_PIN OK hash matches the pinned value` |
| 3 | “a NEW pin runner ... covering a deterministic wall-top scenario (measure twice, document in-file)” | PASS | `workspace/logs/q51-gate-pathing-pin-1.txt` and `q51-gate-pathing-pin-2.txt`: `RETAIL_PATHING_PIN ticks=5 hash=2e5ad58054d28dc93f37ef4728549bb538f6d4a1c22be922ec19b59fb2d1b12d`; both end `RETAIL_PATHING_PIN OK hash matches the pinned value` |
| 4 | “Failing-first tests ... ordered-onto-deck arrives via ramp ... wall-face ascent refused ... deck-to-disconnected-deck refused; unit on deck can be ordered back down.” | PASS | `workspace/logs/q51-failing-first.txt`: `CASTLE_WALL_WALK_RESULT passed=4 failed=8`; final `workspace/logs/q51-castle-wall-walk.txt`: `CASTLE_WALL_WALK_RESULT passed=20 failed=0` |
| 5 | “castle_gate_runner 47/0” | PASS | `workspace/logs/q51-gate-castle-gate.txt`: `CASTLE_GATE_RESULT passed=47 failed=0` |
| 6 | “castle_map_live_boot_runner 8/0” | PASS | `workspace/logs/q51-gate-castle-map-live-boot.txt`: `CASTLE_LIVE_BOOT_RESULT passed=8 failed=0` |
| 7 | “castle_skirmish_ai_runner (104/1 or better, no new reds)” | PASS AT ALLOWED BASELINE | `workspace/logs/q51-castle-skirmish-ai.txt`: `CASTLE_SKIRMISH_AI_RESULT passed=104 failed=1 checks=105`; sole red remains `Minas Tirith_ai_issued_attack_order` |
| 8 | “retail_lockstep_determinism_runner 5/0” | PASS | `workspace/logs/q51-gate-retail-lockstep-determinism.txt`: `RETAIL_LOCKSTEP_RESULT passed=5 failed=0` |
| 9 | “retail_turn_model_runner 17/0” | PASS | `workspace/logs/q51-gate-retail-turn-model.txt`: `RETAIL_TURN_MODEL_RESULT passed=17 failed=0` |
| 10 | “retail_formation_movement_runner 34/0” | PASS | `workspace/logs/q51-gate-retail-formation-movement.txt`: `RETAIL_FORMATION_MOVEMENT passed=34 failed=0` |
| 11 | “retail_slice_runner failure-NAME set identical to ... v027fin-retail_slice_runner.txt” | PASS | `workspace/logs/q51-retail-slice-name-compare.txt`: `Q51_RETAIL_SLICE_FAILURE_NAMES baseline=87 current=87 identical=True`; added and removed sets are empty |
| 12 | “If a needed piece of data is NOT in the shipped packs ... name it ... and fail closed” | PASS | `workspace/logs/q51-minas-bridge-probe.txt`: `authored=144 ... portals=10 gaps=60` and `start_portals=0 ... end_portals=1 ... connected_pairs=0 ... reason=\"no-wall-bridge-route\"` |

## Runner table (before → after)

| Runner | Baseline | After | Pin (file:line) |
|---|---|---|---|
| `castle_wall_walk_runner.gd` | Failing-first `4/8` | `20/0` | New behavioral runner; no golden hash |
| `retail_state_pin_runner.gd` | `b025d16237ff644d66211a9cc26872f18b61520b9a377f11e9e99c6eceb43f58` | Same, OK | `game/tests/retail_state_pin_runner.gd` `EXPECTED_HASH` unchanged |
| `retail_pathing_pin_runner.gd` | N/A | `2e5ad58054d28dc93f37ef4728549bb538f6d4a1c22be922ec19b59fb2d1b12d` twice, OK | `game/tests/retail_pathing_pin_runner.gd` `EXPECTED_HASH` |
| `castle_gate_runner.gd` | `47/0` | `47/0` | Named runner baseline |
| `castle_map_live_boot_runner.gd` | `8/0` | `8/0` | Named runner baseline |
| `castle_skirmish_ai_runner.gd` | `104/1` | `104/1` | Sole named red unchanged |
| `retail_lockstep_determinism_runner.gd` | `5/0` | `5/0` | Named runner baseline |
| `retail_turn_model_runner.gd` | `17/0` | `17/0` | Named runner baseline |
| `retail_formation_movement_runner.gd` | `34/0` | `34/0` | Named runner baseline |
| `retail_slice_runner.gd` | 87 parsed failure names | 87, identical set | Shared `v027fin-retail_slice_runner.txt` comparison |

## Failure-by-name delta

- `castle_skirmish_ai_runner.gd`: new failures = `{}`; gone failures = `{}`; retained = `{Minas Tirith_ai_issued_attack_order}`.
- `retail_slice_runner.gd`: new failure names = `{}`; gone failure names = `{}`. `workspace/logs/q51-retail-slice-name-compare.txt` records `baseline=87 current=87 identical=True`.
- The retail-slice aggregate remains expected-red at `RETAIL_SLICE_RESULT passed=371 failed=59` and `RETAIL_SLICE_ACCEPTANCE FAIL`; acceptance here is failure-name identity, not a claim that the suite is green.

## Pins moved

| Pin | Old | New | Why | Re-minted by |
|---|---|---|---|---|
| Main retail state pin | `b025d16237ff644d66211a9cc26872f18b61520b9a377f11e9e99c6eceb43f58` | Unchanged | Ground-only behavior must remain byte-identical | Not re-minted |
| Wall pathing pin | N/A | `2e5ad58054d28dc93f37ef4728549bb538f6d4a1c22be922ec19b59fb2d1b12d` | Q51 requires a new deterministic wall-top scenario | Measured twice in `q51-pathing-pin-remeasure-1.txt` and `q51-pathing-pin-remeasure-2.txt`, then asserted twice in the gate logs |

## Named unsupported semantics / follow-ups

- Minas Tirith needs pack/importer/map-topology repair before the Player_1 pocket can use the authored layer: 24 shipped role names do not resolve to GLB meshes and 36 authored ramp roles have no adjacent ground endpoint. The precise probe found `start_portals=0`, `end_portals=1`, `connected_pairs=0`, and `deck_pairs=0`; no runtime geometry was synthesized.
- Helm's Deep retains three fail-closed data gaps: GatehouseRight `raisedWallMesh:P3-missing`, GatehouseLeft missing its bound GLB, and HelmsDeepCulvert missing its bound GLB. The usable shipped roles still compile to `authored=57 cells=5356 ramp_cells=1685 portals=25 gaps=3` (`workspace/logs/q51-helm-live-boot.txt`).
- Other selected-pack castle maps may likewise expose named missing-role or no-ground-endpoint receipts. Q51 implements the consumer and a closed Erebor vertical slice; it does not recook or mutate immutable packs.

## Not verified / left undone

- No fresh-context verifier has rerun the Definition of Done yet.
- No manual player-input playtest was performed; the working vertical slice was exercised headlessly through the real selected-pack fixture/GLB loader and simulation.
- Minas Tirith was not forced to `105/0`; the exact shipped-data/topology blocker above remains.
- No pack was recooked, selected, published, or modified. No distribution build was produced. Nothing was pushed.
- Full wall-layer construction cost was observed during the ten-map AI sweep but was not subjected to a dedicated performance budget benchmark.
