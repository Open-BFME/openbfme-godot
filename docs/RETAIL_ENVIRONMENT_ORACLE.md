# Retail environment oracle and runtime

Retail evidence for map environment presentation — sky, fog, lighting, water and
reflection — together with the profile and Godot runtime bindings that consume
it. The investigations were carried out on Fords of Isen II, so the worked
examples name that map; the engine behaviour they document is general.

The highest-value section here is the world-sky trace, which reads the retail
`SkyboxTextureSet` constructor defaults directly out of the executable. The
skybox section is a deliberate record of a **proof of absence**: the world-sky
texture set is not proven, and the shortcuts that would have guessed it are
explicitly refused.

> **Consolidation note.** This document absorbs several former standalone
> documents, listed under their own headings below and preserved verbatim.
> Counts, blocker totals and hashes inside those sections are snapshots taken
> when that investigation was written and are **not** current status; they are
> kept because the surrounding evidence depends on them. Live gate results
> live only in [STATUS.md](../STATUS.md).

---

<!-- merged from docs/RETAIL_FORDS_ENVIRONMENT.md -->

## Fords retail environment evidence

`openbfme_importer.sage_environment` is a bounded oracle for the BFME II 1.06
Fords of Isen II environment. It reports source-authored rendering facts; it
does not convert, publish, or invent replacement values. The report must stay
under `.private/retail-work` because it contains decoded retail parameters.

### Exact inputs

The extractor reads these effective-winner files and records a SHA-256 and byte
count for each:

- `maps/map mp fords of isen ii/map mp fords of isen ii.map`
- `maps/map mp fords of isen ii/map.ini`
- `data/ini/weather.ini`
- `data/ini/water.ini`
- `data/ini/watertextures.ini`
- `data/ini/gamedata.ini`

The map is first accepted by the strict multiplayer map parser. The oracle then
decodes only the evidenced BFME II layouts: `WorldInfo v1`, `GlobalLighting v8`,
`EnvironmentData v3`, `NamedCameras v2`, empty `PostEffectsChunk v1`, and empty
`CameraAnimationList v3`. Wrong versions, truncated payloads, non-finite floats,
ambiguous duplicate chunks, malformed selected INI blocks, and duplicate
effective assignments fail closed.

### Retail findings

The current effective tree reports:

- active time `AFTERNOON` and map weather `NORMAL`;
- four time-of-day configurations, each retaining separate terrain, object,
  and infantry sun/accent light records;
- map-local hardware fog fields, reflection height/on overrides, and binary
  macro/cloud/water-alpha facts;
- four standing-water areas and eight river areas with exact material fields;
- two source named cameras plus map-specific camera bounds in `WorldInfo`;
- exact global shadow mode flags and map shadow color;
- one active missing asset reference: `SkyEnv.tga` has no exact filename or
  compiled-DDS stem counterpart in the effective tree.

Six items intentionally remain unresolved in the current report: the active
`SkyEnv.tga` leaf, the absent `FogSettings` chunk (fog is instead explicit in
map-local `Weather`), the unknown 44-byte BFME lighting field, the unnamed float
in each source named-camera record, the SAGE-to-Godot light/camera basis
transform, and exact Godot equivalence for retail shadow volumes/decals. Their
chunk/section evidence and hashes are retained; no values are inferred.

### Runtime handoff

| Runtime target | Authoritative report data | Gate before binding |
|---|---|---|
| Time/weather | `GlobalLighting.activeTimeOfDay`, `WorldInfo.weather` | Use the source enums exactly. |
| Terrain/object/infantry light rigs | Active `GlobalLighting.configuration` | Prove the coordinate-basis transform; do not collapse the three lighting domains silently. |
| Fog | Map `Weather` over global `Weather` | Bind the exact RGB, enabled flag, start, and end. |
| Sky/cloud/macro | `EnvironmentData`, global weather cloud controls, GameData sky flags | Resolve exact leaves and validate motion/scale against the retail oracle. |
| Water | Active `WaterSet`, effective `WaterTransparency`, `EnvironmentData`, and map water material rows | Require one source-proven asset candidate per active reference; water geometry stays in the existing cooked map. |
| Camera | `WorldInfo`, `NamedCameras`, GameData defaults | Prove basis and angle conventions before applying anchors. |
| Shadows | `GlobalLighting.shadowColor`, GameData shadow flags | Render-compare the chosen Godot implementation; the source does not assert equivalence. |

### Reproduce

From the repository root:

```powershell
$env:PYTHONPATH = 'importer'
$python = '.private\retail-work\tools\python-3.12-env\Scripts\python.exe'
$root = '.private\retail-work\cache\effective-assets'
& $python -m openbfme_importer.sage_environment `
  --effective-root $root `
  --output '.private\retail-work\reports\retail-fords-environment-a.json'
& $python -m openbfme_importer.sage_environment `
  --effective-root $root `
  --output '.private\retail-work\reports\retail-fords-environment-b.json'
Compare-Object `
  (Get-Content -Raw '.private\retail-work\reports\retail-fords-environment-a.json') `
  (Get-Content -Raw '.private\retail-work\reports\retail-fords-environment-b.json')
& $python -m unittest importer.tests.test_sage_environment -v
& $python -m ruff check `
  importer/openbfme_importer/sage_environment.py `
  importer/tests/test_sage_environment.py
```

Two reports from an unchanged effective tree must be byte-identical and carry
the same top-level `aggregateSha256`. This oracle does not modify a profile,
build a pack, change content selection, or write retail data outside `.private`.

---

<!-- merged from docs/RETAIL_FORDS_WORLD_SKY_TRACE.md -->

## Fords world-sky activation trace

### Result

Static evidence still cannot name the active BFME II 1.06 Fords of Isen II
world-sky texture set. The result remains fail-closed:

- `worldSkySelectionProven=false`;
- `selectedTextureSet=null`;
- `selectedFaceClosure=[]`;
- neither `Morning` nor `DefaultSky` is promoted;
- the placed water-reflection skydome remains separate.

This trace does prove that the global `DrawSkyBox = Yes` setting is authored.
That enables the sky-background render mode; it does not select a named
texture scheme.

### Three different defaults that must not be conflated

#### `TSMorning*` executable constructor defaults

The retail executable's `SkyboxTextureSet` record constructor begins at VA
`0x0060D4E3`. It initializes the five record string fields before INI parsing:

| Face | Record offset | Constructor literal | Push VA |
|---|---:|---|---:|
| N | `+0x04` | `TSMorningN.tga` | `0x0060D516` |
| E | `+0x08` | `TSMorningE.tga` | `0x0060D524` |
| S | `+0x0C` | `TSMorningS.tga` | `0x0060D530` |
| W | `+0x10` | `TSMorningW.tga` | `0x0060D53C` |
| T | `+0x14` | `TSMorningT.tga` | `0x0060D549` |

There is exactly one direct call to this constructor, at `0x0060D755`, on
the parsed-record creation path. These values are fallback initialization for
every newly allocated `SkyboxTextureSet` record. They are not an active set
selection.

#### The named `Morning` INI record

`environment.ini` independently authors a named `SkyboxTextureSet Morning`
record. Its five authored values happen to match the constructor fallbacks.
That agreement proves the record's contents, not that a no-override map
selects the record.

The exact ASCII tokens `Morning`, `DefaultSky`, and `new_skybox` do not occur
as NUL-terminated selection names in this `game.dat`. Only the five
`TSMorning*.tga` constructor literals occur.

#### The `DefaultSky` comment

The `environment.ini` comment says `DefaultSky` must exist and its names must
match the textures assigned in `new_skybox.w3d`. That is a model-placeholder
compatibility requirement. It does not state that a map lacking
`SkyboxSettings` selects `DefaultSky`. The existing skybox oracle also proves
that the five `DefaultSky` leaves are absent from the effective retail tree.

### Parser-to-registry trace

The executable parser descriptor at VA `0x00DB9850` binds the
`SkyboxTextureSet` type literal to callback `0x0060D6F5`. The callback creates
or finds records in the registry at `0x00DFF494`.

An exact scan of the retail `.text` section found only three direct absolute
references to that registry:

| Instruction VA | Classification |
|---:|---|
| `0x0060D720` | parser create/find record |
| `0x00BAE6E6` | static registry initialization |
| `0x00BB7AE1` | static registry teardown thunk |

There is no direct absolute registry reference at a renderer call site. This
rejects the claim that the constructor's `TSMorning*` values are statically
proven to reach the active Fords renderer.

This is an exact direct-reference result, not a proof that no indirect path
exists. An aliased registry pointer or a copied record could evade this bounded
scan. That is why the result remains unresolved rather than declaring that the
game draws no world sky.

### Draw-mode evidence is not selection evidence

`data/ini/gamedata.ini` line 6483 authors `DrawSkyBox = Yes`. `game.dat` also
contains `DRAW_SKYBOX_BEGIN` and `DRAW_SKYBOX_END` render-mode telemetry. These
prove that a sky-background draw mode exists and is globally enabled. None of
these values names `Morning`, `DefaultSky`, or five texture leaves.

### Secondary OpenSAGE check

OpenSAGE commit `588ac477367a0022adf29f20a084e8873014e6ce` stores and parses
`SkyboxTextureSet` assets and parses/writes the optional map
`SkyboxSettings` record. A complete production-source token scan at that
commit found no `SkyboxTextureSet`, `SkyboxTextureSets`, or `SkyboxSettings`
reference under its rendering or graphics code. This corroborates the parser
boundary but is secondary implementation evidence, not proof of retail
behavior.

### Water-reflection boundary

The separately attested `WaterReflectionSkydome_GapOfRohan` remains a placed
reflection source with contract aggregate
`9c70403da8aa2a4dbe9915b5855f056526ec7719b280df079f6464bafeccdfa2`.
It is not the main-camera `new_skybox.w3d` world sky and cannot close this
selection blocker.

### Smallest closing runtime trace

Use the installed x86 `cdb` or WinDbg against the real BFME II 1.06
`game.dat`. Two runs are necessary because x86 exposes only four hardware
watchpoint slots and the record has five face fields.

1. Break at `0x0060D76F`, immediately after each parsed
   `SkyboxTextureSet` insertion. Identify the `Morning` map key and retain its
   record pointer.
2. Before Fords loads, set read watchpoints on the record fields at `+0x04`,
   `+0x08`, `+0x0C`, and `+0x10`.
3. Run through the first rendered in-match frame. Record every reader
   instruction, copied destination, resolved texture name, and GPU resource
   bound to the main-camera sky draw.
4. Repeat with a watchpoint on `+0x14` for the top face.
5. Only if the same named record supplies all five main-camera bindings should
   the converter resolve those names to effective retail winners and record
   their virtual paths and hashes.

The success criterion is one named record and all five resolved retail
textures observed on the active main-camera sky draw for Fords. A similar
looking screenshot alone is not sufficient.

### Deterministic contract

The payload-free contracts are:

- `.private/scratch/fords-world-sky-trace/contract-a.json`;
- `.private/scratch/fords-world-sky-trace/contract-b.json`.

Both have file SHA-256
`78fe995a00384537569178e2549e9d26aab5f6c155a6d57d16483836f402473c`
and declared aggregate SHA-256
`b32056916fde73f3a182b29deee8dc50fc3c6522de4e5c7d4d429897e7fe683c`.

Reproduce from the repository root:

```powershell
$env:PYTHONPATH = 'importer'
$python = 'C:\Users\Jonathan\AppData\Local\Programs\Python\Python312\python.exe'
$common = @(
  '--effective-assets-root', '.private\retail-work\cache\effective-assets',
  '--manifest', '.private\retail-work\cache\effective-assets\.openbfme\manifest.json',
  '--catalog', '.private\retail-work\catalog\bfme2.json',
  '--game-dat', 'F:\BFME2\game.dat',
  '--skybox-oracle', '.private\scratch\fords-skybox-oracle\contract-a.json',
  '--water-reflection-contract', '.private\scratch\fords-water-reflection\contract-a.json',
  '--opensage-root', '.private\scratch\fords-skybox-oracle\OpenSAGE'
)
& $python -m openbfme_importer.retail_fords_world_sky_trace @common `
  --output '.private\scratch\fords-world-sky-trace\contract-a.json'
& $python -m openbfme_importer.retail_fords_world_sky_trace @common `
  --output '.private\scratch\fords-world-sky-trace\contract-b.json'
Get-FileHash -Algorithm SHA256 `
  '.private\scratch\fords-world-sky-trace\contract-a.json', `
  '.private\scratch\fords-world-sky-trace\contract-b.json'

& $python -m pytest importer\tests\test_retail_fords_world_sky_trace.py -q
& $python -m ruff check `
  importer\openbfme_importer\retail_fords_world_sky_trace.py `
  importer\tests\test_retail_fords_world_sky_trace.py
```

No retail payload is written outside `.private`. This trace does not edit a
profile, converter pipeline, content pack, selection, renderer, or gate.

---

<!-- merged from docs/RETAIL_FORDS_SKYBOX_ORACLE.md -->

## Fords world-sky selection oracle

### Result

The exact BFME II 1.06 Fords of Isen II world-sky texture set is **not yet
proven**. The contract stays fail-closed: `selectedTextureSet=null`, no
candidate face files are added to the asset closure, and the four blockers are
retained explicitly.

This rejects both tempting shortcuts:

- `DefaultSky` is not selected merely because it is the first declared set;
- `Morning` is not selected merely because all five leaves exist or because
  their names occur in `game.dat`.

### Direct retail evidence

The bounded oracle binds the current effective-winner manifest and catalog to
these retail sources:

- Fords `.map`, SHA-256
  `fa3b460c23f72821c7e19127bb90f7d97c27a7a2c485a44936b9c291173c0800`;
- Fords `map.ini`, SHA-256
  `9555088bf9d0b6ebbe92d1591535dde532e3fbe8ca8db3ede582a5e9fdb14d46`;
- `data/ini/environment.ini`, SHA-256
  `124837dd4fd2a708a783cc36bb8602a6ca35e81306fe346f37dc3b14a60b826e`;
- `art/w3d/ne/new_skybox.w3d`, SHA-256
  `42761e75a105cece575583756393c2cde090e856b4cc87f285e77328eb4c775d`;
- BFME II `game.dat`, 10,969,600 bytes, SHA-256
  `f008b587570bad693981dc7218588c81d192a1e064b0f7f861539c51156a7640`.

The map has 20 top-level chunks. `SkyboxSettings` is not among them, and
`map.ini` contains no skybox token. `new_skybox.w3d` names exactly four side
placeholders: `SkyBox_01.tga` through `SkyBox_04.tga`.

`environment.ini` says that the `DefaultSky` scheme must be present and that
its names must match the textures assigned in `new_skybox.w3d`. That proves a
model-to-scheme name-compatibility contract. It does **not** say that a map with
no override selects `DefaultSky`. All five `DefaultSky` requested leaves
(`SkyBox_01.tga` through `SkyBox_05.tga`) also lack an exact or compiled-stem
counterpart in the effective tree.

The retail executable contains the `SkyboxTextureSet` parser vocabulary and
the five `TSMorningN/E/S/W/T.tga` literals. Bounded disassembly shows the
texture-set record constructor at virtual address `0x0060D4E3` initializing
five string fields from those literals; its observed construction call at
`0x0060D755` is on the parsed-record creation path. This is useful parser
default evidence, but it does not prove that the renderer chooses that record
when Fords has no map override. The generated contract deliberately records
only literal presence and requires a renderer control-flow trace or original
runtime trace before promotion.

### Secondary source check

A temporary OpenSAGE checkout at commit
`588ac477367a0022adf29f20a084e8873014e6ce` was kept strictly under
`.private/scratch/fords-skybox-oracle/OpenSAGE`. It independently recognizes
`SkyboxSettings` as an optional top-level map record containing position,
scale, rotation, and a texture-scheme string. It does not implement a renderer
selection path that could establish BFME II behavior. Therefore it supports
the chunk classification only and is not treated as retail proof.

### Reflection skydome is not the world sky

The map places exactly one `WaterReflectionSkydome_GapOfRohan`. Retail INI
data binds that type to `WtrSky_GRohan`; the W3D shader material binds
`WtrSkydome_GapOfRohan.tga`; and the effective tree resolves the compiled DDS
at `art/compiledtextures/wt/wtrskydome_gapofrohan.dds`.

That independently proven four-file evidence closure has canonical SHA-256
`0b253fb3b2153e12274133431ae3a1c007ffe80e430d9713ab94779ae739e226`.
It remains classified as a placed water-reflection skydome and cannot satisfy
the unresolved `new_skybox.w3d` world-sky contract.

### Blockers

1. No map-authored `SkyboxSettings` record or map-INI skybox override exists.
2. The global `DefaultSky` comment defines compatibility, not the no-override
   selection rule.
3. The five `DefaultSky` leaves are absent from the effective asset tree.
4. Executable Morning parser defaults have not been traced into the active
   Fords renderer.

The smallest acceptable closing proof is a BFME II 1.06 renderer trace at map
load showing the active named scheme and all five texture substitutions. An
independent original-game capture that identifies the exact five retail faces
would also close selection, but similarity to one candidate is not enough.

### Reproduce

From the repository root:

```powershell
$env:PYTHONPATH = 'importer'
$python = 'C:\Users\Jonathan\AppData\Local\Programs\Python\Python312\python.exe'
$common = @(
  '--effective-assets-root', '.private\retail-work\cache\effective-assets',
  '--manifest', '.private\retail-work\cache\effective-assets\.openbfme\manifest.json',
  '--catalog', '.private\retail-work\catalog\bfme2.json',
  '--game-dat', 'F:\BFME2\game.dat'
)
& $python -m openbfme_importer.retail_fords_skybox_oracle @common `
  --output '.private\scratch\fords-skybox-oracle\contract-a.json'
& $python -m openbfme_importer.retail_fords_skybox_oracle @common `
  --output '.private\scratch\fords-skybox-oracle\contract-b.json'
Get-FileHash -Algorithm SHA256 `
  '.private\scratch\fords-skybox-oracle\contract-a.json', `
  '.private\scratch\fords-skybox-oracle\contract-b.json'

& $python -m pytest importer\tests\test_retail_fords_skybox_oracle.py -q
& $python -m ruff check `
  importer\openbfme_importer\retail_fords_skybox_oracle.py `
  importer\tests\test_retail_fords_skybox_oracle.py
```

The two contracts must be byte-identical. No retail payload is written outside
`.private`, and this bounded oracle does not edit profiles, converters,
runtime code, or gates.

---

<!-- merged from docs/RETAIL_FORDS_WATER_REFLECTION.md -->

## Fords of Isen II water-reflection oracle

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

### Exact authored contract

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

### Godot handoff and remaining renderer evidence

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

### Reproduce

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

---

<!-- merged from docs/RETAIL_FORDS_LINEAR_FOG.md -->

## Fords exact linear fog surface

`FordsLinearFog` is a Godot 4.7 Forward+ compositor effect implementing the
source Fords of Isen II hardware-fog curve without mapping it to Godot's
exponential environment fog.

### Exact contract

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

### Explicit remaining renderer blockers

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

### Focused acceptance

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

---

<!-- merged from docs/RETAIL_FORDS_ENVIRONMENT_PROFILE.md -->

## Fords retail environment profile

`openbfme_importer.retail_fords_environment_profile` turns the bounded Fords
environment oracle into a deterministic, composable profile fragment. It does
not replace an unknown retail rule with a Godot approximation.

### Proven active contract

The current BFME II 1.06 effective-winner tree proves:

- time-of-day `AFTERNOON` and weather `NORMAL`;
- linear hardware fog enabled, RGB `(220, 226, 235)`, start `350`, end `2000`;
- nine separate terrain/object/infantry sun and accent rows from the map's
  `GlobalLighting v8` chunk, with their source ambient, color, and direction
  vectors preserved independently;
- packed shadow color `0x40000000`, shadow volumes enabled, shadow decals
  enabled, and shadow mapping disabled;
- `DrawSkyBox`, cloud-map, and cloud-plane flags enabled;
- active `TSCloudMed.tga` and `TSNoiseUrb.tga` references, each resolving to one
  compiled retail DDS winner.

The fragment therefore converts the active cloud and macro DDS inputs to PNG.
It also retains `art/w3d/ne/new_skybox.w3d` as a source-only pack input so the
eventual sky converter uses the exact retail geometry rather than reconstructing
it. That W3D's exact four authored placeholder references are retained in the
runtime contract.

### Zero-guess sky result

`data/ini/environment.ini` declares 11 named five-face `SkyboxTextureSet`
blocks. Eight sets resolve all five face assignments in the effective tree;
three do not. There are 40 resolved face assignments covering 37 unique DDS
files.

Fords authors no named skybox-set override. The retail map and INIs also do not
encode the executable's default selection rule. Consequently the plan sets
`worldSky.selectedTextureSet` to `null`, marks the source model as source-only,
and does not add any candidate face set to the profile. Selecting `DefaultSky`,
`MidDay`, `Sunny`, or a time-of-day-derived set would be unsupported.

Closing the world sky requires an original executable trace or an independent
retail render oracle that identifies the selected named set. Once proven, the
composer can add exactly those five face resources and convert
`new_skybox.w3d` with explicit texture overrides.

### Renderer blockers retained

The runtime document remains `complete=false` and `parityReady=false` for four
specific reasons:

1. named world-sky texture-set selection is unresolved;
2. the SAGE-to-Godot light-vector basis is not proven;
3. Godot's built-in environment fog does not implement SAGE's linear
   start/end contract;
4. Godot directional-light shadows are shadow maps, while this map explicitly
   enables SAGE shadow volumes and decals and disables shadow mapping.

The exact values are still included in
`maps/fords-of-isen-ii/environment.json`; consumers must fail closed rather than
silently collapsing light domains or enabling approximate renderer features.

### Reproduce

From the repository root:

```powershell
$env:PYTHONPATH = 'importer'
$python = '.private\retail-work\tools\python-3.12-env\Scripts\python.exe'
$common = @(
  '--effective-assets-root', '.private\retail-work\cache\effective-assets',
  '--manifest', '.private\retail-work\cache\effective-assets\.openbfme\manifest.json',
  '--catalog', '.private\retail-work\catalog\bfme2.json',
  '--environment-report', '.private\retail-work\reports\retail-fords-environment-c1f300fcf6fed6f2.json'
)
& $python -m openbfme_importer.retail_fords_environment_profile @common `
  --output '.private\scratch\fords-environment-profile\plan-a.json'
& $python -m openbfme_importer.retail_fords_environment_profile @common `
  --output '.private\scratch\fords-environment-profile\plan-b.json'
Get-FileHash -Algorithm SHA256 `
  '.private\scratch\fords-environment-profile\plan-a.json', `
  '.private\scratch\fords-environment-profile\plan-b.json'

& 'C:\Users\Jonathan\AppData\Local\Programs\Python\Python312\python.exe' `
  -m pytest importer\tests\test_retail_fords_environment_profile.py -q
& 'C:\Users\Jonathan\AppData\Local\Programs\Python\Python312\python.exe' `
  -m ruff check `
  importer\openbfme_importer\retail_fords_environment_profile.py `
  importer\tests\test_retail_fords_environment_profile.py
```

Both plan files must be byte-identical. The planner writes no retail payload
outside `.private`, does not publish or select a pack, and never edits the
completion profile.

---

<!-- merged from docs/RETAIL_FORDS_ENVIRONMENT_RUNTIME.md -->

## Fords retail environment runtime

The Men/Fords runtime now consumes the exact `AFTERNOON` / `NORMAL` facts from
the bounded retail environment oracle with aggregate SHA-256
`c1f300fcf6fed6f225d1b04f50b14fab04883641d8b5b36762be6cfcb58e9a59`.
This is a parameter-binding checkpoint, not a claim of rendered 1:1 parity.

### Exact runtime mappings

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

### Deliberately unresolved renderer gaps

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

### Focused acceptance

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

---

