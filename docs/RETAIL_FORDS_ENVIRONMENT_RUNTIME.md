# Fords retail environment runtime

The Men/Fords runtime now consumes the exact `AFTERNOON` / `NORMAL` facts from
the bounded retail environment oracle with aggregate SHA-256
`c1f300fcf6fed6f225d1b04f50b14fab04883641d8b5b36762be6cfcb58e9a59`.
This is a parameter-binding checkpoint, not a claim of rendered 1:1 parity.

## Exact runtime mappings

- The active time and weather metadata are `AFTERNOON` and `NORMAL`.
- Hardware fog retains source RGB `(220, 226, 235)`, start `350`, and end
  `2000`. Distances are multiplied by the validated map-local uniform scale.
- The camera uses source relative-height limits `120..300`, pitch `37.5`
  degrees off top-down, yaw `0`, map ground bounds `260..380`, scroll scalar
  `1.0`, and a ten-source-unit wheel step. The initial skirmish zoom is the
  source maximum height.
- Both source named cameras encode the same field of view,
  `0.8726646304130554` radians (`50` degrees), which is the exact source FOV
  now bound to the tactical camera.
- Source Z-up vectors use the already validated transform
  `godot=(sage.x,sage.z,-sage.y)` and then the map's player-start local basis.
  Camera yaw-zero placement and all light directions go through that transform.
- The active lighting configuration remains three separate domains: terrain,
  object, and infantry. Each domain has its exact sun plus two accent diffuse
  colors/directions and a distinct Godot visibility/light mask. Source
  parameters are retained in node metadata for audit.
- Camera focus is constrained inside the cooked playable bounds using the
  source-derived 95%-screen-height ground-plane inset instead of allowing the
  target point to reach the map edge.

On the current Fords fixture the validated local transform scale is
`0.026492327`, so fog start/end become approximately `9.272315` and
`52.984655` local units. The focused runner derives these numbers from the
selected private map; it does not hard-code them.

## Deliberately unresolved renderer gaps

The runtime does not hide unresolved retail rendering behind generic art:

- `SkyEnv.tga` has no exact effective-tree candidate. The invented procedural
  sky was removed and the background is neutral black. No `Sky` or
  `ProceduralSkyMaterial` is installed.
- SAGE hardware fog is linear between an exact start and end. Godot 4.7's
  built-in environment fog is exponential/height based, so it remains
  disabled. The runtime instead attaches the exact post-transparent linear
  depth compositor after validating the selected map scale. Transparent-depth,
  sky-depth, and non-headless rendered-oracle equivalence remain explicit.
- SAGE evaluates ambient per light and per material domain. The exact ambient
  vectors are retained on each source light, but world ambient is zero because
  silently collapsing the terrain/object/infantry ambient terms would be
  incorrect.
- Retail uses shadow volumes and decals, while the available Godot path is
  shadow mapping. All nine source lights therefore keep shadows disabled and
  mark renderer equivalence unresolved.
- The source scroll scalar is exact, but the base keyboard translation rate is
  not in the current oracle. The existing OpenBFME playability rate is labeled
  unresolved and is not claimed as a retail parameter.
- A common named-camera FOV is source-exact, but the map does not encode a
  separate tactical-default FOV semantic. A rendered retail oracle comparison
  is still required.

The old guessed sky colors, guessed exponential fog density/height, filmic
grading, guessed sun rotation/color/energy, zoom smoothing, and guessed camera
height/depth ranges are gone.

## Focused acceptance

Run this gate against the selected composed pack. Older terrain-only and road
fixtures predate the current lifecycle/map schema and are intentionally not
accepted by the runtime loader:

```powershell
$env:OPENBFME_CONTENT = (Resolve-Path '.private\content-packs').Path
& 'C:\Users\Jonathan\Downloads\godot47\Godot_v4.7-stable_win64_console.exe' `
  --headless --path game --script res://tests/retail_environment_runner.gd
```

Expected focused result:

```text
RETAIL_ENVIRONMENT_RESULT passed=32 failed=0
```

The runner also parses the production source to reject reintroduction of
`ProceduralSkyMaterial`, exponential fog guesses, the old invented fog/sun
colors, and the old invented light rotation. It verifies the exact oracle
identity, scale conversion, exact linear-fog compositor binding, camera
geometry, domain light parameters, explicit unresolved metadata, and absence
of source-incompatible shadow claims.
