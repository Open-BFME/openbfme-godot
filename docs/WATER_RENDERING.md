# Standing water rendering

Standing water now reads its map-selected W3D's WaterShader.FX properties.
The former stand-in used one bump sample, a fixed blue tint and 0.78 opacity,
repeated every 160 world units, and sampled the depth-colour LUT at one
constant coordinate. Different authored water materials therefore looked alike.

The implemented path uses the shipped RotWK `watershader.fxo` data, decoded
with `tools/render/fx_params.py` and `tools/render/d3d9_disasm.py`:

- Pixel shader blob 8 combines two bump samples as
  `2*(b0+b1) - (2,2,1)`, scales XY by BumpScale, normalizes, and adds
  diffuse/specular lighting to the environment colour plus the product of
  two WaveTexture samples.
- Vertex shader blob 9 derives the two stream directions from WindAngle
  plus/minus WaveAngle/2. Coordinates use world XY / 600 and the authored
  BumpUVScale, EnvUVScale and WaveUVScale; its streams use WindSpeed + WaveSpeed.
- Its preshader supplies colour `2 * DimmingFactor * MaterialColorDiffuse.rgb
  * DirectionalLight.Color`. The material alpha is used when terrain blending
  is disabled; with valid map depth parameters the alpha is
  `saturate(depth/(MaxAlphaDepth*DeepWaterAlpha^2))*DeepWaterAlpha`.

Defaults come from the effect parameter table; authored W3D properties override
them. In the mounted Harlindon map, WaterPlanePCA.W3D specifies diffuse
(0.15, 0.3, 0.35, 0.9) and BumpUVScale (15,15). Each area owns its material so
water height and authored settings cannot leak between areas sharing textures.
No simulation state, update order or random numbers change.

The next port step preserves the W3D effect's technique index (the byte after
its 32-byte shader name, previously discarded as part of a reserved word).
Names are resolved in the mounted fx_2_0 technique table, rather than guessing
from a model name or the presence of PCA textures. BFME2's matched
`Libraries/Source/WWVegas/WW3D2/fxshadersetup.cpp` Load_W3D (RVA 0x152E4D)
provides the donor meaning of this byte. The raw index is retained independently
of the version/number byte and padding.

PCA and PCA_U now reconstruct animated surface normals using the material's
basis textures and the coefficient file derived from WaterPCATexture1:
`Art/Textures/<stem>.basis`. The donor is
`GameEngine/Source/GameLogic/Map/StandingWaterAreaLoadPCA.cpp` (0x3093F6),
plus `GameEngineDevice/Source/W3DDevice/GameClient/Rva003093F6BasesSamplers.cpp`
(0x30822F..0x308509) and `Rva000806F3BasesParams.cpp` (0x80858 onward).
These paths are relative to the local Open-BFME-2 matching-source checkout.
Target facts are the mounted RotWK effect/material/texture/basis data; no
RotWK executable-equivalence claim is made from this BFME2 donor.

The coefficient file's mean and twelve basis rows store axes in X/Z/Y order,
normalized by 255. All ten four-component tracks are retained (mean plus three
basis groups, each X/Y/Z). Adjacent keys are interpolated and the successor
wraps to zero. Mean.w is initialized to zero; the water shader uses only xyz.
The GPU receives a floating-point track texture, avoiding a render-frame CPU
upload. Its time rate follows the donor callback's seconds * 30 client frames
per second * file speed; the Godot TIME bridge and GPU float operation order
remain unverified. Harlindon's PCA_U material loads 30 frames at Speed 0.7,
so the effective rate is 21 keys per second.

Normal reconstruction follows the shipped effect's pixel blobs 0 (PCA) and 3
(PCA_U), including the latter's third basis-texture contribution and noise
flattening. The supported path includes authored environment
perturbation, directional/specular lighting, and baseline froth. Required
textures and malformed coefficient/effect tables produce builder errors;
compiled TGA lookup now precedes packed JPEG so the shipped PCA basis
textures resolve. Other technique families/LODs explicitly report their
bump approximation in water.material_stops.

The mesh now follows BFME2's matched `W3DWater.cpp` allocation (RVA
0x8304A): a shared 20-unit grid, doubled until at most 60000 cells, selected
by polygon cell centres. Grid origins truncate toward zero. The matched
`W3DWaterStandingWaveQuery.cpp` (0x8026B) selects enabled PCA shoreline
segments by nearest midpoint, preserving source order and strict depth and
radius gates. Degenerate segments are ignored. CUSTOM0/1 carry midpoint,
ground height, half-length and wave direction without consuming simulation RNG.

Compiled effect VS13/14 provides three authored GWAmpFreqPhaseSteepness
vectors. The shader applies the source 0.075 amplitude scale, frequency
jitter, rotated secondary directions, technique-specific steepness (0.15 or
0.33), and PCA_U's 0.75 horizontal scale. Interpolated geometric normals,
crest/shallow-water froth and displaced depth alpha now accompany moving
vertices. Culling bounds include horizontal and vertical displacement.
Zero amplitude/frequency contributes no wave, avoiding undefined division.

## WaterLOD: which file, which technique, which shader (lane WATER-LOD)

RotWK picks the water's W3D file and effect technique from the player's WaterLOD (the static detail level's, GameLODManager + 0x1784: Low 0, Medium 1,
High 2, UltraHigh 3), not from the raw technique index the W3D stores:

- `StandingWaterArea::loadFXShaderSetup` (RW 0x70A3F1, BFME2 0x308C31) reads `<name>L`, `<name>M` or `<name>` by the level (suffix table RW 0xC1E3C4):
  Low tries L, then M, then the base file; Medium M, then the base; High and UltraHigh the base. `WaterGeometry::standingShaderStem`.
- the stored index names a technique of RotWK's `watershader.fxo` (DefaultTechnique, BumpSpecular, DefaultTechnique_L, BumpSpecular_L, DepthOnly, PCA_U,
  PCA, PCA_L, PCA_M, BumpSpecular_M), and `FXShaderSetup::InitializeShader` (body RW 0x552269..0x5524DE, BFME2 0x15288F) is run again with lodLimit = WaterLOD: the
  authored name's `_L` / `_M` / `_U` suffix is its own level (a bare name is 2; UltraHigh lifts it to 3), capped by the WaterLOD, searched downward and then upward.
  `WaterGeometry::resolveTechnique`. So wtr_riv01 and the other BumpSpecular files draw BumpSpecular at High and BumpSpecular_M at Medium (their `m` files author
  plain BumpSpecular too); waterplanepca and the other authored PCA_U water draw PCA at High, PCA_U only at UltraHigh and PCA_M at Medium; Low reads the `l`
  files, PCA_L.
- ported: BumpSpecular (ps blob 4, vs blob 19), BumpSpecular_M (blobs 8 / 9), PCA (0 / 13), PCA_U (2 / 14). DefaultTechnique, the `_L` techniques, PCA_M and
  DepthOnly are reported per area in `water.material_stops` and drawn with the BumpSpecular_M path (S-035).

BumpSpecular's pixel shader lights `DirectionalLight[0].Color * (env * 2 * Dimming * Diffuse * N.L + SpecularColor * N.H^SpecularExponent)`, blends that toward
WaterReflectionTexture by ReflectionStrength, adds the two wave samples, fogs and applies the shroud. The environment lookup is offset by
ReflectionPerturbation times the scaled bump normal's xy. N.H uses the half-vector of the eye and **DirectionalLight[1].Direction** (a struct {Color,
Direction} has two registers per element: the vertex shader's base c14 puts it at c17, BumpSpecular_M's base c13 at c16, PCA's pixel shader base c7 at c10, PCA_U's
c10); BumpSpecular sends it through oD1, a colour register that D3D9 clamps to 0..1, and `UseMySpecularDirection` replaces it with the authored angles.
2 * Dimming * Diffuse leaves a vs_2_0 shader through oD0 and is clamped to 0..1 (PCA_U's vs_3_0 does not clamp). Slot 1 of the scene lights is the second light
the object light set accepts (S-390), (0, 0, 1) when there is none.

## The planar reflection (lane WATER-REFLECT)

The reflection texture is the rendered reflection only when WaterLOD >= 2 (GlobalData + 0x5E) and the WaterTransparency block's `ReflectionOn` holds (water.ini:
No; 66 of the 123 map.ini files say Yes), else retail binds a black texture (RW 0x47FAAD, BFME2 Rva0007EBFBWaterDraw.cpp:419). PCA water blends toward it by
(1 - eye.n)^2 and BumpSpecular by ReflectionStrength.

What retail renders (RotWK 2.01; every address is pinned in `tools/render/test_water_binary_facts.py`):
- ONE texture per map, not per water height: 512 x 512, default format, in the water object at + 0xF8 (RW 0x47E1BD). Standing waters share it.
- The plane is `ReflectionPlaneZ` of the WaterTransparency setting (water.ini 59.0; map.ini overrides: Adorn River 13, Anfalas 69, Fords of Isen II 294, Harlindon
  90, Argonath 89, ...), normal (0, 0, 1). The camera is the viewing camera with its axes and position reflected in the plane and the reflected up negated, so the
  matrix stays a rotation (RW 0x47EA2B, BFME2 Rva0007F0FB), and an oblique near plane on the water plane clips everything below it.
- Each frame, after the shadow map and before the main Begin_Render (RW 0x449E38), `IsReflectionVisible` (RW 0x47F695) decides: WaterLOD >= 2, ReflectionOn, and some
  standing water whose FX shader loaded at level >= 2 has its polygon inside the viewing frustum. Then the pass (RW 0x47F1AC) sets the texture as the target, clears
  it to black, and draws the scene through the mirror camera under the projection override byte (RW 0xDD3445).
- Under the override: the water, the roads and floors, the terrain tracks do not draw; the terrain draws its base texture alone (ST_TERRAIN_BASE); a drawable draws only
  when its mirror flag (FLAG_DRAWS_IN_MIRROR on a placed map object) or KindOf CAN_CAST_REFLECTIONS holds (RW 0x470D50); and a KindOf SKYBOX drawable (the
  `WaterReflectionSkydome_*` low-resolution domes) is hidden in every other pass (RW 0x470D53).
- The water samples it with the main view's clip coordinates: u = (x/w + 1)/2, v = (y/w + 1)/2 (vs blob 19 oT2, ps blob 4), plus ReflectionPerturbation times the scaled
  bump normal's xy (blob 0 / 2: the normalised normal's xy). The sampler clamps and filters linearly without mips.

The port: `GameClient/WaterReflection.*` holds the maths (mirror camera, oblique projection, frustum test, UV) and `GodotDevice/GodotWaterReflection.*` the node
(`WaterReflectionView`: a SubViewport sharing the world, a mirror Camera3D whose cull mask is visual layer 19 alone). The terrain and every W3DInstancer model that has a
mirror instance carry that layer; shaders tell the pass by `CAMERA_VISIBLE_LAYERS == 262144` and clip below the global `w3d_reflect_y`; the instancer carries the mirror and
mirror-only bits in INSTANCE_CUSTOM.w. `MapTerrainBuilder` makes the node (beside StandingWater) when the conditions above hold and reports it in `water.reflection`.
The viewport texture holds the gamma-space bytes the shaders hand over, as retail's 8-bit target does, so the water shader samples it without a conversion.

Differences from RW 0x47F1AC, reported in `water.reflection_stops` and registered under S-035: the oblique near plane is a fragment clip (Godot cannot take a custom
projection matrix); the texture is 512 rows by 512 x the viewing aspect (a Godot camera takes its aspect from its viewport; retail stretches the aspect into 512 x 512); the
scene is the terrain (with its normal-map lighting; no cloud or macro texture) and the mirror drawables, without whatever else the render list holds under the override
(particles, projectiles, bridges, the shroud); the pose of a model outside the viewing frustum is not evaluated for the reflection.

`MapTerrainBuilder` takes the level as its `water_lod` option (`GameWorld.set_water_lod`, set by `game_options.gd` from the detail state); without it the
level is High and the report says so.

Every ported water shader fogs toward the map's hardware fog colour (the same range fog as the terrain), then applies the shroud. The fog factor is computed per
vertex and interpolated (vs blob 19 oD1.w, blob 9 oT6.w, blob 13 oD1.x, ps `lrp` by the interpolated value), and so is BumpSpecular_M's half-vector (vs blob 9 `nrm`,
pixel blob 8 does not normalise it again): `water_vertex_test.gd`. Water drawn from a bare polygon (bump water; PCA water uses the 20-unit grid) therefore
interpolates across big triangles, where retail's vertices are denser. Rivers do not: the retail
river pixel shader never reads a fog factor and every pass sets FogEnable = 0.

Rivers: the WaterSet `DiffuseColor` is packed `A<<24|R<<16|G<<8|B` and `drawRiverWater` reads its low byte as red and its third byte as blue (RW 0x47E9D5 /
0x500400), so a white river's red and blue channels trade places (`WaterGeometry::riverVertexColour`; latent while the retail colours are grey).

Pinned by `tools/render/test_water_binary_facts.py` (game.dat, watershader.fxo), `engine/tests/test_water_retail.cpp` (Adorn River, Fords of Isen II,
Harlindon, Anfalas, Evendim) and `godot/tests/standing_water_test.gd`. Adorn River's single 97-point standing-water polygon is rejected by the ear clipper and is
not drawn (`water.triangulation_failed`); retail fills every standing water from the grid PCA water already uses.

S-035 remains open: the high-detail choice, Godot TIME/trigonometry and terrain
height sampling are presentation bridges, not verified target numeric/LOD
parity. Shadow filtering, the DefaultTechnique / `_L` / PCA_M /
DepthOnly techniques and legacy shoreline texture tracks remain unported.
Legacy areas without a
WaterShader.FX material retain their explicit reported approximation.
Malformed `.basis` input is bounds-checked and rejected; reproducing the
original loader's permissive atof/atoi behavior on malformed mods is not claimed.
No simulation state or RNG is consumed by this rendering path.

Validation uses exact native material/geometry assertions and a mounted-map
Godot regression. Run `godot/tests/standing_water_test.gd` without `--headless`
for Metal/Forward+ shader compilation; set OPENBFME_WATER_SCREENSHOT to save
a Harlindon screenshot. The headless
run checks material bindings only. The GPU regression additionally verifies
that a zero-rate coefficient track stays pixel-stable while an animated track
changes the rendered water. Native cases cover channel order, normalization,
interpolation/wraparound, malformed data and the stored technique byte.
These are data and renderer checks, not a
claim of retail pixel parity.

The GPU test also freezes coefficient tracks and verifies that moving Gerstner
vertices change the rendered silhouette while frozen wave time stays stable.

## Upstream port

This branch starts from Open-BFME main and contains only water rendering, loader
metadata, tests and documentation. The fork-integration merge, Lua/gameplay rules,
replay/LAN versions and fork map-extension changes are excluded. Upstream river
geometry and shading remain in place.
