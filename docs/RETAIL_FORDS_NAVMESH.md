# Fords navigation and footprint contract

`retail_fords_navmesh.py` produces a deterministic evidence contract for the
private Fords of Isen II slice. It deliberately does **not** generate or bless a
Godot navmesh. The contract separates facts preserved from BFME2 retail data
from converter-derived diagnostics and behavior that still needs a BFME2
oracle.

## Generate the private contract

From the repository root:

```powershell
$env:PYTHONPATH = 'importer'
$python = '.private/retail-work/tools/python-3.12-env/Scripts/python.exe'
$map = '.private/content-packs/bfme2-five-maps-106-private/maps/fords-of-isen-ii'
$assets = '.private/retail-work/cache/effective-assets'

& $python -m openbfme_importer.retail_fords_navmesh `
  --map-dir $map `
  --effective-assets $assets `
  --runtime-source game/src/retail_slice/retail_map_data.gd `
  --output .private/scratch/fords-navmesh/contract.json
```

The output contains only derived JSON evidence. Keep it under `.private`
because its file hashes and exact retail declarations attest the local retail
inputs.

## Proven facts

For the current BFME2 1.06 Fords cook, the contract proves:

- a 415 by 353 source grid, 10 SAGE world units per sample;
- a 20-sample playable inset, inclusive grid bounds `(20,20)` through
  `(395,333)`;
- an exact LSB-first passability grid with 18,325 impassable samples and SHA-256
  `11e911c6ba50a0d8dcf7fc3a71242b013b5dfdce1169ae86c939d2ddd5e654b9`;
- exact player starts at rounded grid cells `(303,69)` and `(70,237)`;
- exact authored cross sections for `ford1`, `ford2`, and `ford3`;
- zero authored waypoint edges (`empty-no-authored-navmesh`);
- exact collision geometry declarations for the four first-gate Men units,
  their four hordes, and the five first-gate Men buildings;
- all authored `RankInfo` positions for those hordes, without guessing which
  alternate formation state the executable selects.

The passability-only diagnostic finds that both player starts are in the same
four-neighbor component. That is useful converter evidence, but it is explicitly
marked `converter-derived-not-sage-routing-oracle`. It does not prove that BFME2
would select the same route, neighbors, costs, clearance, tie-break, or smoothing.

## Footprint interpretation

The INI geometry is kept as exact declarations, including all named and
additional geometry pieces. It is not rasterized into the terrain grid.

- Individual infantry and cavalry objects declare cylinder collision geometry.
- Horde objects declare box geometry, authored formation positions, and
  `LARGE_RECTANGLE_PATHFIND` in `KindOf`.
- Buildings can have multiple geometry pieces selected by upgrade or lifecycle
  state. The farm inherits geometry from `FarmInterface`; the fortress uses
  separate foundation and citadel objects.
- A `TERRAIN_BOUNDS` geometry name is evidence, not by itself a proven
  buildability rule.

This distinction matters: collision geometry is not equivalent to the
placement mask, unit clearance, or the executable's dynamic pathfinder shape.

## Required runtime conformance

A future Godot routing implementation should consume the contract in layers:

1. Preserve every source impassability bit. Never clear a source-blocked cell.
2. Preserve the exact player-start and ford source geometry transforms.
3. Run the emitted exact passability vectors before route tests.
4. Treat water masking, ford traversal, geometry clearance, diagonal movement,
   smoothing, and dynamic obstacles as policy inputs whose retail parity is
   still unproven.
5. Add BFME2-recorded route and placement vectors before claiming 1:1 routing
   or buildability.

The optional current-runtime observation records that the checked implementation
uses `AStarGrid2D`, Manhattan/four-neighbor movement, water polygon
rasterization, and ford-corridor dilation. Those are implementation observations,
not retail evidence.

## Blockers to 1:1 routing/buildability

The generated contract fails closed on seven unresolved areas:

1. No authored source buildability grid is present.
2. Water and named-ford traversal semantics are not proven per unit class.
3. Unit/horde clearance and `LARGE_RECTANGLE_PATHFIND` rasterization are not
   proven.
4. Blocking map-object collision/lifecycle closure is incomplete.
5. Building geometry-state selection is not proven.
6. Construction, damage, destruction, and rebuild nav updates are not proven.
7. Route costs, neighbors, tie-breaks, and smoothing are not proven.

The shortest path to closing these gaps is a small BFME2 oracle corpus: fixed
start/destination route captures for infantry and cavalry; placement
accept/reject probes around terrain, water, other buildings, and map props; and
the same probes before, during, and after building destruction. Those results
can become behavioral vectors without changing the source-fact layer.

## Verification

```powershell
$env:PYTHONPATH = 'importer'
& .private/retail-work/tools/python-3.12-env/Scripts/python.exe `
  -m unittest importer.tests.test_retail_fords_navmesh -v

& C:/Users/Jonathan/AppData/Local/Programs/Python/Python312/python.exe `
  -m ruff check importer/openbfme_importer/retail_fords_navmesh.py `
  importer/tests/test_retail_fords_navmesh.py
```

Generate two outputs with the same command and compare their file SHA-256 values.
The focused test also performs this byte-identical CLI check.
