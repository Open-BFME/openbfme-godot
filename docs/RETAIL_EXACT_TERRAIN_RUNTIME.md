# Exact Fords terrain runtime

The private Fords battlefield now renders the complete cooked SAGE height grid.
The former `TERRAIN_STEP = 8` preview mesh is gone.

## Exact geometry contract

For the retail Fords of Isen II cook, the runtime requires and builds:

- grid: `415 x 353`;
- vertices: `415 * 353 = 146,495`;
- source quads: `414 * 352 = 145,728`;
- triangles: `145,728 * 2 = 291,456`; and
- passability-colored vertices: all `18,325` source-impassable samples.

Vertices are row-major and use the exact local position returned by
`RetailMapData.terrain_local_at(x, y)`. UVs remain integer source-grid
coordinates `(x, y)`, which is the coordinate system consumed by the retail
terrain shader's tile, primary-blend, three-way-blend, and cliff lookups. Each
quad uses the source diagonal:

```text
top-left, top-right, bottom-left
bottom-left, top-right, bottom-right
```

Normals average the two triangles in each source quad and then the four quads
surrounding each vertex. Missing boundary quads contribute an upward normal.
This keeps the full mesh smooth without changing any source height sample.

The mesh is pre-sized before construction. This avoids hundreds of thousands
of incremental packed-array reallocations; the focused headless result builds
the full terrain, bound props, roads, water, and diagnostics in under one
second on the current workstation.

## Fail-closed bounds

`RetailFordsBattlefield` rejects a grid before allocating geometry when any of
these facts are inconsistent:

- dimensions cannot form a quad;
- more than `262,144` vertices or `524,288` triangles;
- height bytes are not exactly two bytes per declared sample;
- passability stride/payload does not cover every row;
- tile, primary blend, three-way blend, or cliff grids do not contain exactly
  one record per height sample;
- terrain or local transform scales are non-finite/non-positive; or
- the validated passability popcount disagrees with its declared count.

These are private-slice runtime bounds, not a claim that larger future maps are
unsupported by the importer. A larger map must deliberately raise the runtime
budget and add a corresponding fixture instead of silently decimating it.

## No parity placeholders

Ford gates remain exact gameplay/source facts, but the former tan `BoxMesh`
strips are no longer rendered. Their names, edges, elevation, source river ID,
and source section index live in `ford_gate_diagnostics` and node metadata.
`ford_marker_count` remains as the slice controller's compatibility count; it
now means validated source gates, not visible marker meshes.

Likewise, unresolved vegetation and rock samples no longer create cylinders,
spheres, trunks, materials, or `MultiMeshInstance3D` nodes. The runtime keeps:

- the complete unresolved type ID list and placement count;
- the bounded diagnostic sample count;
- vegetation/rock sample counts; and
- non-rendered diagnostic nodes for existing audit consumers.

The diagnostic nodes deliberately contain no renderable child. Missing retail
art therefore remains measurable without appearing to be a converted asset.

## Focused acceptance

Once the composed main pack containing road materials is selected, run:

```powershell
$env:OPENBFME_CONTENT = (Resolve-Path '.private\content-packs').Path
Remove-Item Env:OPENBFME_TERRAIN_PACK -ErrorAction SilentlyContinue
& 'C:\Users\Jonathan\Downloads\godot47\Godot_v4.7-stable_win64_console.exe' `
  --headless --path game --script res://tests/retail_full_terrain_runner.gd
```

During pre-composition integration, the selected `a88...` base pack has exact
terrain and road control points but not the road-material document required by
`RetailMapData`. The same runner can use the already-audited, non-published road
fixture while requiring its terrain and material documents to equal the
selected pack:

```powershell
$env:OPENBFME_CONTENT = (Resolve-Path '.private\content-packs').Path
$env:OPENBFME_TERRAIN_PACK = (Resolve-Path `
  '.private\retail-work\packs\bfme2-men-vslice-roads-private').Path
& 'C:\Users\Jonathan\Downloads\godot47\Godot_v4.7-stable_win64_console.exe' `
  --headless --path game --script res://tests/retail_full_terrain_runner.gd
```

Verified pre-composition result:

```text
RETAIL_FULL_TERRAIN_RESULT passed=29 failed=0
RETAIL_FULL_TERRAIN_METRICS width=415 height=353 vertices=146495 quads=145728 triangles=291456 impassable=18325 build_ms=945 roads=71 unresolved=1239
```

The independent road runner remains `27/27`. The integrated slice runner now
asserts these full terrain counts and requires ford/unresolved diagnostics to
have zero visible placeholder geometry.
