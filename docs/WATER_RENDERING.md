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

S-035 remains open: the high-detail choice, Godot TIME/trigonometry and terrain
height sampling are presentation bridges, not verified target numeric/LOD
parity. Planar reflections, shadow filtering, other PCA LODs, legacy shoreline
texture tracks and full fog parity remain unported.
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
