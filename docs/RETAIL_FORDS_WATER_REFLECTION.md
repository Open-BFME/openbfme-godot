# Fords of Isen II water-reflection oracle

This oracle separates three retail concepts which must not be collapsed into
one Godot sky:

1. The world-sky model is `art/w3d/ne/new_skybox.w3d`.
2. The water-reflection skydome is the placed object
   `WaterReflectionSkydome_GapOfRohan`, backed by
   `art/w3d/wt/wtrsky_grohan.w3d`.
3. Every cooked `StandingWaterArea` separately names `SkyEnv.tga` as its
   `skyTexture`. BFME2's effective tree contains neither that exact file nor a
   `skyenv.dds` compiled-stem counterpart.

There is no retail evidence that these inputs are interchangeable. In
particular, showing the Gap of Rohan reflection skydome as the visible world
sky would be a classification error.

## Exact authored contract

The map-local `WaterTransparency` section overrides the global defaults:

| Field | Global `water.ini` | Fords `map.ini` | Effective |
|---|---:|---:|---:|
| `ReflectionPlaneZ` | `59.0` | `294.0` | `294.0` |
| `ReflectionOn` | `No` | `Yes` | `Yes` |

The strict retail map parser and the current cooked `objects.json` and
`water.json` agree byte-for-byte on the relevant source data:

- 4 standing-water areas, 25 polygon points total;
- area IDs/heights/point counts: `(1,294,13)`, `(3,364,4)`, `(4,365,4)`,
  `(5,245,4)`;
- only area ID 1 is authored at the single reflection plane Z value 294;
- one `WaterReflectionSkydome_GapOfRohan` placement, source object index 6;
- SAGE position `(1955.4375, 1806.58154296875, 0.0)`;
- sampled/world Z `291.9921875` and Godot position
  `(1955.4375, 291.9921875, -1806.58154296875)`;
- object layer `Skybox`, yaw 0, civilian owner, enabled and untargetable.

The source object declaration selects `WtrSky_GRohan` and has exactly the
relevant `KindOf` flags `SKYBOX INERT CAN_CAST_REFLECTIONS`. Its resolved W3D
is 5,717 bytes with SHA-256
`5fb32f7ba487dd320a87c742dce917fb05d3a6ad0f497c486dfcaaee3321375f`.
The model is one W3D v5 mesh named `WTRSKY_GROHAN`, with 57 vertices, 98
faces, and one material.

The W3D `Simple.fx` material has technique 0 and exactly these properties:

- `Texture_0 = WtrSkydome_GapOfRohan.tga`
- `ColorEmissive = (1,1,1,0)`
- `TexCoordTransform_0 = (1,1,0,0)`
- `DepthWriteEnable = true`
- `AlphaBlendingEnable = false`
- `FogEnable = false`

The authored TGA identifier resolves by exact stem to the one effective retail
DDS `art/compiledtextures/wt/wtrskydome_gapofrohan.dds`: 174,904 bytes,
SHA-256
`733f3c6a2b535e0f3b134dda59cb95ba830c3919e27f2c3322e4ff346d26901c`,
512x512, DXT1, 10 mip levels. The existing static-prop plan already gives the
native asset outputs:

- `assets/models/props/wtrsky-grohan-2880c8d6b111.glb`
- `assets/textures/props/wtrskydome-gapofrohan-ce02a4d881c9.png`

That proves the source mesh/texture closure. It does not prove a Godot
reflection renderer.

## Godot handoff and remaining renderer evidence

The coordinate transform makes the authored SAGE plane Z=294 a Godot Y=294
plane. This is only an exact coordinate remap. It does not select a rendering
technique.

Four renderer questions remain genuinely unresolved:

1. The retail reflection camera, clip plane, culling, and render-target rules.
2. Whether and how `SKYBOX CAN_CAST_REFLECTIONS` excludes the placed skydome
   from the main camera while including it in the reflection pass.
3. How one Z=294 reflection result is composited across standing-water areas
   at heights 294, 364, 365, and 245.
4. How the unresolved per-area `SkyEnv.tga` input participates in the water
   shader.

Until original-render or executable evidence closes those questions, the
contract intentionally leaves `techniqueSelected = null` and
`parityReady = false`. SSR, a reflection probe, a mirrored SubViewport, or a
custom planar pass would each be a guess at this stage.

## Reproduce

From the repository root:

```powershell
$env:PYTHONPATH = 'importer'
$python = 'C:\Users\Jonathan\AppData\Local\Programs\Python\Python312\python.exe'
& $python -m openbfme_importer.retail_fords_water_reflection_oracle `
  --effective-assets-root .private\retail-work\cache\effective-assets `
  --manifest .private\retail-work\cache\effective-assets\.openbfme\manifest.json `
  --catalog .private\retail-work\catalog\bfme2.json `
  --environment-report .private\retail-work\reports\retail-fords-environment-c1f300fcf6fed6f2.json `
  --visual-closure .private\retail-work\reports\retail-visual-closure-0d51ad8d31ca6e6c.json `
  --static-prop-plan .private\retail-work\reports\retail-static-prop-plan-0d51ad8d31ca6e6c.json `
  --cooked-map-directory .private\retail-work\packs\bfme2-men-vslice\maps\fords-of-isen-ii `
  --output .private\scratch\fords-water-reflection\contract-a.json
```

The current contract aggregate is
`9c70403da8aa2a4dbe9915b5855f056526ec7719b280df079f6464bafeccdfa2`.
Generating a second contract from the same sealed sources produces identical
pretty-printed bytes.
