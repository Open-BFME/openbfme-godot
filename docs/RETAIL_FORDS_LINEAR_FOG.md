# Fords exact linear fog surface

`FordsLinearFog` is a Godot 4.7 Forward+ compositor effect implementing the
source Fords of Isen II hardware-fog curve without mapping it to Godot's
exponential environment fog.

## Exact contract

- Source color: RGB `(220, 226, 235)`.
- Source start/end: `350` / `2000`.
- Local start/end: the source values multiplied by an explicit, positive,
  finite uniform map scale supplied by the caller.
- Depth: negative camera-view Z reconstructed from the sampled resolved depth
  and the current frame's per-view inverse projection matrix, passed directly
  from `RenderSceneData.get_view_projection(view).inverse()`.
- Curve: `clamp((camera_depth - start) / (end - start), 0, 1)`.
- Blend: linear interpolation of scene RGB to the exact fog RGB. Alpha is not
  changed.

The effect rejects different color or distance values and refuses to create a
`Compositor` before a scale is supplied. It does not contain the current Fords
fixture scale and does not infer a SAGE-to-Godot coordinate conversion.

The private vertical slice configures and attaches this effect only after the
selected runtime map has provided its validated `local_transform_scale`:

```gdscript
var fog := FordsLinearFog.new()
var error := fog.configure_fords(map_data.local_transform_scale)
if error == "":
	world_environment.environment.compositor = fog.create_compositor()
```

Godot's built-in `Environment.fog_enabled` must remain disabled; enabling it
would layer an exponential approximation over this pass.

## Explicit remaining renderer blockers

The compute shader and exact opaque camera-depth curve are executable, but this
surface alone is not a 1:1 fog-parity claim:

- The post-transparent callback samples the resolved opaque depth buffer.
  Transparent fragments (notably water/particle surfaces) do not write their
  own depth there, so their retail fog interaction still needs a proven
  material or transparent-pass solution.
- A depth sample with a zero homogeneous divisor is left unchanged. No sky or
  clear-buffer fog behavior is guessed while the exact retail skybox selection
  remains unresolved.
- The selected private Fords runtime now attaches the compositor. A non-headless
  Forward+ dispatch and rendered retail-oracle comparison remain integration
  gates.

## Focused acceptance

```powershell
& 'C:\Users\Jonathan\Downloads\godot47\Godot_v4.7-stable_win64_console.exe' `
  --headless --editor --quit --path game
& 'C:\Users\Jonathan\Downloads\godot47\Godot_v4.7-stable_win64_console.exe' `
  --headless --path game --script res://tests/retail_linear_fog_runner.gd
```

Expected runner result: `RETAIL_LINEAR_FOG_RESULT passed=23 failed=0`.

The installed Godot 4.7 console uses a rendering backend with no
`RenderingDevice` under `--headless`. The runner therefore proves imported
SPIR-V compilation, exact parameter/curve behavior, projection-column packing,
and that the compositor reports the precise `RenderingDevice unavailable`
blocker without dispatching. If a headless environment does expose a
`RenderingDevice`, the same runner instead requires at least one real compute
dispatch. The effect is integrated, but a non-headless Forward+ rendered
comparison is still required before it can pass the retail-parity gate.
