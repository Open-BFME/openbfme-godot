# Q56f Minas Tirith wall-layer data repair — report

Date: 2026-08-21 · Owner: agent-ad750096d43a727ce · Binding brief:
`workspace/orchestration/fable-wave/castle-lanes/brief-q56f-minas-tirith-walls.md`
(read from the main checkout, read-only)

Baseline HEAD: `cc750000e9d0b95bf4a948c9f6cfb83435aa5cec` · Final HEAD: the commit
containing this report · Selected pack for the baseline/full-matrix measurements:
`rotwk-playable-maps-private/abc27325672bd5712d7785e23d5ff858133c86056f173032deb8c1de9141d4d1`.
The local proof cook is named separately below. Main `selection.json` was not
written; its final SHA-256 is
`b84722ecd81df7cd5c4c342d8a9acc342929d96e35daa80c70feae488295a645`.

## Result

Implemented and locally proven; awaiting fresh-context verification. Five of
the 24 unresolved Minas Tirith role instances were not absent retail geometry:
the real proxies live in authored sibling/damage-state W3Ds and now survive the
closure, recipe, map binding, cook, and runtime path. Thirty-one of the 36
ramp roles that previously found no ground endpoint now bind a distinct
authored wall endpoint to nearby ground without synthesizing a wall cell.
The local cook improves Minas from 2,302 to 2,968 walk cells, 10 to 42 portals,
and 60 to 24 named gaps. The map boots 8/0 and the wall runner is 22/0. Minas
AI remains the accepted sole 104/1 red: its 7,496-cell AI pocket and the
player's 89,391-cell component each reach portals, but the authored wall graph
has zero connected portal pairs between them. The remaining retail absences
and elevated-only proxy meshes make a synthetic bridge impermissible.

## Retail diagnosis

The initial 24 missing runtime role instances split as follows.

| Retail finding | Instances | Resolution |
|---|---:|---|
| `MinisTop1` P1/P2 | 2 | authored in `art/w3d/gb/gbmtop1_d1.w3d` |
| `MinisGate2` P1/P2 | 2 | authored in exact HLOD child container `art/w3d/gb/gbmgate2.w3d`; intact `gbmingate2.w3d` references `GBMINGATE2.GBMGATE2` |
| `MinisGate3` P2 | 1 | authored in `art/w3d/gb/gbmingate3_d2.w3d` |
| `MinisBridge2/8/9` P3 | 3 | genuinely absent from their retail W3Ds |
| `MinisGate1` P1/P2/P3 | 3 | genuinely absent from every retail Gate1 state |
| `MinisGate3` P1 | 1 | genuinely absent from every retail Gate3 state |
| `MinisWallC` P2 | 12 | genuinely absent; retail `gbmwallc.w3d` carries P1 only |

Thus five instances are repaired from exact retail bytes and nineteen remain
named `*-missing` receipts. No proxy name was normalized or substituted.

The 36 old `no-ground-endpoint` receipts were an endpoint-model defect: the
loader required the authored low ramp cell to also be ground-walkable. Castle
footprints deliberately block those wall cells. Runtime now retains
`{wall_cell, ground_cell}` and admits the nearest ground cell only when it is
within that ramp mesh's own authored horizontal reach. Thirty-one instances
close. Five remain named because retail does not author a ground-reaching ramp:

| Source indices / type / role | Nearest ground | Authored reach |
|---|---:|---:|
| 478, 481, 482, 483 · `MinisWallBT` · P2 | 5.7748–7.6656 | 2.5793–2.5794 |
| 1989 · `MinisWallAR` · P1 | 5.3289 | 4.6945 |

All distances are local map units from
`workspace/logs/q56f/q56f-local-endpoint-proof.txt`. Connecting these five
would require a portal longer than the retail proxy itself, so they remain
fail-closed receipts.

## Implementation

- `playable_structure_compiler.py` scans default, damage-state, and exact HLOD
  child W3Ds and emits role-to-source evidence while preserving true absences.
- `retail_visual_closure.py` admits exact referenced sibling containers only
  when they carry hidden meshes. `playable_structure_pack_compiler.py` retains
  those meshes in lifecycle or dedicated hierarchical GLBs and emits a sealed
  source table.
- `map_prop_bindings.py`, `castle_fixtures.py`, and `sage_map.py` carry and
  validate that source table into `object-bindings.json`.
- `retail_map_data.gd` validates and consumes role-specific GLBs, derives
  distinct authored wall/ground endpoint pairs, and routes layer transitions
  through the ground side. `retail_slice_sim.gd` is untouched.

## Failing-first evidence

| Check | Red first | Green after |
|---|---|---|
| Minas retail role/absence pytest | `2 failed, 6 passed` | `8 passed` |
| Wall endpoint/runtime runner | `20 passed, 2 failed` | `22 passed, 0 failed` |

Logs:
`workspace/logs/q56f/q56f-failing-first-pytest.txt`,
`q56f-walk-surfaces-unit-current.txt`,
`q56f-failing-first-wall-runner.txt`, and
`q56f-wall-runner-current.txt`.

## Local cook and runtime proof

The scratch-only profile contains Minas Tirith and 314 resource rules. It was
cooked under this worktree at
`workspace/q56f-local/editions/rotwk/packs/rotwk-playable-maps-private` with a
clean worktree-local extraction of the pinned Blender archive. Nothing was
published.

- Profile SHA-256:
  `61e9e44c8ec0f99686383224884a7cd2e6f3fb8438c4ce596599e7aabe42af65`.
- Bundle SHA-256:
  `349a613a71567280590107610c2afbe4797f4c0eedb85e741815b1fb07daa37c`.
- Audit: 562 checked files, 560 outputs, zero conversion failures.
- Cooked bindings prove Top1 P1/P2 → `gbmtop1_d1`, Gate2 P1/P2 →
  `gbmgate2`, Gate3 P2 → `gbmingate3_d2`.
- Minas live boot: 8/0; `authored=144 cells=2968 ramp_cells=1248 portals=42 gaps=24`.
- Component census: AI source `(38.00792,-8.666157)` is in a 7,496-cell
  component; player builder `(-38.0,-0.499996)` is in the 89,391-cell main
  component. Layered query reports `start_portals=3`, `end_portals=4`,
  `connected_pairs=0`, `deck_pairs=0`.

Evidence:
`workspace/logs/q56f/q56f-local-build.txt`,
`q56f-real-minas-recipes.txt`,
`q56f-local-minas-live-boot.txt`, and
`q56f-local-component-proof.txt`.

## Definition of Done

| Criterion | Result | Evidence |
|---|---|---|
| Diagnose all 24 missing role instances at retail source | PASS | exact 5 repairable / 19 absent split above; real recipe log |
| Diagnose 36 endpoint failures without invented geometry | PASS | 31 repaired; five distance/reach refusals above |
| Failing-first importer/runtime checks | PASS | 2/6 → 8/0 and 20/2 → 22/0 |
| Related importer pytest | PASS | `280 passed, 4 skipped, 183 subtests passed` in `q56f-related-importer-tests.txt` |
| Scratch cook, never publish or change main selection | PASS | valid local bundle; paths and digests above |
| Minas castle live boot | PASS | `CASTLE_LIVE_BOOT_RESULT passed=8 failed=0` |
| Castle AI | EXPECTED RED | selected ten-map gate `104/1`; only `Minas Tirith_ai_issued_attack_order`, exact component proof above |
| Main deterministic state pin | PASS | `b025d16237ff644d66211a9cc26872f18b61520b9a377f11e9e99c6eceb43f58` |
| Pathing pin | PASS | `2e5ad58054d28dc93f37ef4728549bb538f6d4a1c22be922ec19b59fb2d1b12d` |
| Sim source unchanged | PASS | no diff under `retail_slice_sim.gd` |

Pin logs:
`workspace/logs/q56f/q56f-state-pin.txt` and
`workspace/logs/q56f/q56f-pathing-pin.txt`. Full selected AI log:
`workspace/logs/q56f/q56f-selected-castle-ai.txt`.

## Failure-by-name delta

- Importer new-test failures gone:
  `test_minas_proxy_roles_resolve_from_authored_sibling_models`,
  `test_minas_true_retail_absences_stay_named_receipts`.
- Wall runner new-test failures gone:
  `distinct_ground_anchor_is_derived_from_authored_low_end`,
  `distinct_ground_anchor_routes_without_synthesized_wall_cells`.
- Castle AI unchanged: only `Minas Tirith_ai_issued_attack_order` remains red.
  No new selected-matrix failure name was introduced.

## Named follow-up

Publishing is deliberately not part of Q56f. A later batched maps-pack cook may
publish this importer/runtime repair. Even after that cook, Minas AI will stay
red unless source evidence supplies a wall connection between the 7,496-cell
seat pocket and the 89,391-cell main component; this lane refuses to invent one.
