# Retail Road runtime

The Fords runtime consumes only the private cook's exact `roads.json` and
`road-materials.json`. It does not search for similarly named definitions or
textures and it does not manufacture a fallback road when a retail dependency
is missing.

## Trust boundary

`RetailMapData` requires the map's `roadMaterials` reference to remain inside
the selected map and pack. It validates:

- `openbfme.sage-road-materials` schema version `0`;
- exact Road ID membership and order against `roads.json`;
- the 64-character source closure aggregate;
- canonical positive `RoadWidth` and `RoadWidthInTexture` values within the
  profile's bounds;
- safe pack-relative PNG paths and decodable PNG payloads; and
- each PNG's byte length and SHA-256 against the exact path in
  `provenance/manifest.json` (`openbfme.retail-import-provenance-v1`).

The GPU builder re-reads and re-hashes each PNG after map validation. A file
changed between validation and material creation therefore fails closed. Road
materials preserve the retail alpha channel, use alpha blending, cull neither
side, depth-test without writing depth, repeat texture coordinates, and use
anisotropic mipmapped filtering.

## Fords topology evidence

The 142 wire records form 71 exact Start (`2`) to End (`4`) source edges. They
are not 71 unrelated quads. Exact endpoint identity produces:

- 120 unique endpoint positions;
- 21 shared nodes (20 degree-two and one degree-three across Road IDs);
- 18 nodes with exactly two edges of the same Road ID and therefore 18 curve
  candidates;
- no same-template degree-three/four crossing candidate; and
- no Angled, TightCurve, EndCap, bridge, or unknown modifier flag.

Applying OpenSAGE's `MinCurveAngle`, broad-curve radius, tangent length, and
overlap tests in source order generates 11 BroadCurve nodes and leaves seven
straight/angled fallbacks. The generated edge pairs are:

```text
2/3, 7/8, 11/12, 14/15, 19/20, 21/22,
24/25, 27/28, 34/35, 41/42, 50/51
```

The fallback pairs are:

```text
0/1, 28/29, 32/33, 46/47, 62/63, 66/67, 69/70
```

Six fall below the minimum curve angle. Pair `28/29` passes the angle test but
its computed tangent exceeds the available edge length. The battlefield keeps
per-node evidence (source node, edge pair, angles, source-unit tangent and edge
lengths, rejection reason, and generated strip count) so future conversions
can be audited without guessing from a rendered frame.

## Meshing rules

The runtime follows the proven OpenSAGE road rules needed by this map:

- connected edge orientation is aligned before UV generation;
- physical half-width is
  `RoadWidth * RoadWidthInTexture / 2`, then scaled by the map's uniform local
  transform;
- the straight atlas uses a longitudinal repeat length of `RoadWidth * 4` and
  the authored transverse span centered on `0.166` with half-span
  `0.125 * RoadWidthInTexture`;
- BroadCurve geometry is split into 30-degree atlas strips with the source
  overlap rule and uses the BroadCurve atlas crop;
- centerlines are sampled approximately every ten source world units;
- a cross-section is raised to the maximum sampled terrain height across its
  full width plus the source `0.1` height bias; and
- interior vertex pairs survive only when their height differs from linear
  neighbor interpolation by more than the source `0.001` threshold.

Consequently, a fixed four-vertices-per-edge assertion is incorrect. The
current exact Fords cook produces 24 curve strips and 95 total render strips,
with 534 adaptive vertices and 344 triangles across five retail-textured mesh
instances. The focused gate also proves the general identity
`triangles = vertices - 2 * render_strips` rather than relying only on those
snapshot counts.

Unsupported same-template crossings or nonzero Road modifier flags remain a
hard failure. They must be implemented from source evidence before a future
map containing them can be called source-driven.

## Focused acceptance

Point the runner directly at an audited, non-published private pack; it does
not depend on `ContentDB` selection order:

```powershell
$env:OPENBFME_ROAD_PACK = (Resolve-Path `
  '.private\retail-work\packs\bfme2-men-vslice-roads-private').Path

& 'C:\Users\Jonathan\Downloads\godot47\Godot_v4.7-stable_win64_console.exe' `
  --headless --path game --script res://tests/retail_road_visual_runner.gd
```

The verified result is `27/27`, with no Godot `ERROR` or `WARNING`. The
integrated retail-slice runner independently requires the same material,
topology, curve-evidence, and adaptive-mesh facts once the integration owner
publishes/selects the combined audited pack.
