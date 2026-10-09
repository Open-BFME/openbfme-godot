// OpenBFME. GPL-3.0.
//
// W3DLookupTablePostEffect: the map's colour grade (PostEffectsChunk "LookupTablePostEffect", lane RENDER-4, stop S-1650; S-031 kept it unapplied).
// Client / draw code only.
//
// TARGET FACTS (RotWK game.dat, stop S-001 caveat):
//  - RW 0x511763 builds the effect from the chunk's parameters "BlendFactor" (a real) and "LookupTexture" (a file name; the chunk writer RW 0x4AF7A9
//    writes "Default_vol.tga" when the map names none) and the effect file PostFX_LookupTable.fx, technique "Default".
//  - shaders\compiled\postfx_lookuptable.fxo, decoded with tools/render/d3d9_disasm.py: ps_2_0 `texld r2, t0, s0` (the frame buffer), `texld r1, r2,
//    s1` (the volume lookup at the frame buffer's rgb), `lrp r0, c0.x, r1, r2` (BlendFactor), i.e. out = lerp(frame, lut(frame.rgb), BlendFactor),
//    alpha included; vs_1_1 passes a full-screen quad. LookupTableSampler: MinFilter / MagFilter LINEAR, MipFilter POINT, AddressU / V / W CLAMP
//    (the effect's state blocks). The frame buffer is retail's displayed (gamma-space) image.
//  - The retail lookup files (art\compiledtextures\..\*_vol.tga) are 1024 x 32 strips of 32 slices of 32 x 32; Default_vol.tga is the identity: in
//    picture orientation (top row first) the texel at (slice * 32 + x, y) holds R = x * 255 / 31, G = y * 255 / 31, B = slice * 255 / 31.
// INFERENCE (reported by S-1650): the engine's own conversion of the strip into the volume texture (D3DX volume loading) was not read: the volume is
// laid out so that the identity file is the identity (u = R along x, v = G along the rows from the top, w = B along the slices); the effect is applied
// to the 3D view only (after the transparent pass, before the interface), the order of RW 0x449CF8's view and interface draws.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace W3DLookupTablePostEffect
{

constexpr int kSize = 32; ///< the cube edge of the retail lookup files

// The strip (RGBA8 rows, top row first, width = kSize * kSize, height = kSize) as the RGBA8 volume, texel (x, y, z) at ((z * kSize + y) * kSize + x) * 4.
// Fails (with the reason) on another size.
bool VolumeFromStrip(const std::uint8_t *rgba, int width, int height, std::vector<std::uint8_t> &volume, std::string *error);

// The reference of the shader: the volume sampled LINEAR / CLAMP at `rgb` (0..1, texel centres at (i + 0.5) / kSize), lerped with `rgb` by `blend`.
void Apply(const std::vector<std::uint8_t> &volume, const float rgb[3], float blend, float out[3]);

// The stop line (S-1650) reported while a map's lookup is applied.
const char *StopText();

} // namespace W3DLookupTablePostEffect
