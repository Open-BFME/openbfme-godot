# Map-edge lane report

## Review fixes

The property-only fix in `db7b37f` was inert in rendered output because the
post-transparent linear-fog compute shader treated reverse-Z clear depth as a
finite far surface and saturated it to the Fords fog color. The shader now
returns before inverse projection when `raw_depth <= 0.0`, preserving the
environment backdrop for genuine clear pixels while leaving geometry fog on
the exact source curve.

The runtime contract no longer claims that a non-finite homogeneous depth is
the sky condition. It records the observed reverse-Z clear-depth behavior.

The black backdrop is an **owner-directed dark-neutral placeholder**, not a
retail equivalence claim and not per-map fog data. Named follow-up: close the
retail skybox path by converting and binding `new_skybox.w3d` with the texture
set selected from `environment.ini`, then replace the placeholder. The two
reviewed frames did not contain a retail map edge, so the former comments that
attributed neutral edge presentation to those captures were removed.

This lane also deleted the previous `Approved equivalence` annotation that
called the fog-color horizon equivalent to retail. The owner must explicitly
re-ratify any future equivalence claim after the retail skybox closure.

### Rendered red-first and green proof

Acceptance is an opt-in assertion in `retail_render_capture_runner.gd`. It
reads the composed 1920x1080 viewport image at the known off-map sample
`(960,100)`; it does not inspect the configured `Environment` property.

- RED on `db7b37f` plus the new gate, before the shader fix: exit 1, rendered
  pixel `(0.9373, 0.9490, 0.9647, 1.0)`, equivalent to `(239,242,246)`.
- GREEN after the shader fix: exit 0 and
  `RETAIL_RENDER_MAP_EDGE_BACKDROP_OK pixel=(960, 100) color=(0,0,0,1)`.
- Saved private proof frame:
  `.private/scratch/map-edge-green-c6663855f4634d5bbb705da5caa28b9c.png`.
- Godot had to run one editor import pass after the GLSL edit; otherwise the
  capture process loaded the stale imported `RDShaderFile`. The successful
  capture used the recompiled shader and reported 18 fog dispatches.

Godot prints known render-resource leak diagnostics while shutting down this
capture runner even after `RETAIL_RENDER_CAPTURE_OK`; the successful process
exit was nevertheless 0. The red run exited 1 at the rendered-pixel assertion.

### Reproduction

From this worktree in PowerShell, resolve the operator-owned content outside
the worktree and set both content variables. `OPENBFME_TERRAIN_PACK` is
required by `retail_environment_runner.gd` and must not be omitted:

```powershell
$godot = '<Godot 4.7 console executable>'
$content = '<operator checkout>\.private\content-packs'
$env:OPENBFME_CONTENT = $content
$env:OPENBFME_TERRAIN_PACK = Join-Path $content 'bfme2-men-vslice\ce02105e952ce91faa2b2cab429e2be01200c939e6553ef8be5b2deb8e591383'

& $godot --headless --editor --path game --quit

$env:OPENBFME_CAPTURE_PATH = (Join-Path (Resolve-Path .private\scratch) 'map-edge-proof-<unique>.png')
$env:OPENBFME_CAPTURE_VIEWPORT = '1920x1080'
$env:OPENBFME_CAPTURE_UNCLAMPED = '1'
$env:OPENBFME_CAPTURE_CAMERA_FOCUS = '200,200'
$env:OPENBFME_CAPTURE_ASSERT_MAP_EDGE_BACKDROP = '1'
& $godot --path game --script res://tests/retail_render_capture_runner.gd

& $godot --headless --path game --script res://tests/retail_environment_runner.gd
& $godot --headless --path game --script res://tests/retail_linear_fog_runner.gd
& $godot --headless --path game --script res://tests/retail_state_pin_runner.gd
& $godot --headless --path game --script res://tests/retail_slice_runner.gd
```

Use a unique `%TEMP%` log for each invocation and read it after the process
exits. The render command must remain non-headless.

### Verification results

- Rendered map-edge gate: PASS; black backdrop reached the composed void pixel.
- `retail_environment_runner.gd`: `passed=33 failed=0` with the explicit
  `OPENBFME_TERRAIN_PACK` above.
- `retail_linear_fog_runner.gd`: `passed=23 failed=0`; exact source color
  `(220,226,235)` and source distances `350/2000` are unchanged. The only
  expectation text change replaces the false sky-depth status with the
  reverse-Z clear-depth contract; geometry curve expectations did not move.
- `retail_state_pin_runner.gd`: 3000 ticks,
  `a436bb5989a026ee0be6674ac514c1035784dbe6fc92281ddfbb78cc79e0a05a`,
  unchanged.
- `retail_slice_runner.gd`: accepted baseline `passed=373 failed=32`.
  The ordered 32 failure-name lists from this branch and current main were
  byte-identical; both hash to
  `4e8b86bd150ccf5c90b304f3a320538c1b28f8efc2041e6305b835c4070d6978`.

The runtime pack reported by the successful capture was the immutable Men
Fords bundle ending
`ce02105e952ce91faa2b2cab429e2be01200c939e6553ef8be5b2deb8e591383`.
