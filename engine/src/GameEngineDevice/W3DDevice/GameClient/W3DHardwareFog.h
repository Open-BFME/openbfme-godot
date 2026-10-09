// OpenBFME. GPL-3.0.
//
// W3DHardwareFog: the map's hardware fog as retail applies it to the W3D models and the CPU particles (lane RENDER-4, stops S-1651 / S-1652).
// Client / draw code only: no simulation state.
//
// TARGET FACTS (RotWK game.dat, stop S-001 caveat):
//  - RW 0x537CE0 is ShaderClass::Enable_Fog (ZH shader.cpp): from the ShaderClass word's source blend (bits 14-15: 0 ZERO, 1 ONE, 2 SRC_ALPHA,
//    3 ONE_MINUS_SRC_ALPHA) and destination blend (bits 5-7: 0 ZERO, 1 ONE, 2 SRC_COLOR, 3 ONE_MINUS_SRC_COLOR, 4 SRC_ALPHA, 5 ONE_MINUS_SRC_ALPHA) it
//    sets the fog field (bits 8-9): ONE / ZERO, SRC_ALPHA / ONE_MINUS_SRC_ALPHA and ONE_MINUS_SRC_ALPHA / SRC_ALPHA -> FOG_ENABLE (toward the fog
//    colour); ONE / ONE and ONE / ONE_MINUS_SRC_COLOR -> FOG_SCALE_FRAGMENT (toward black); ZERO / SRC_COLOR -> FOG_WHITE (toward white); every other
//    pair keeps the field (FOG_DISABLE: no fog). The CPU particle draw calls it for its ShaderClass word (RW 0x9620B7).
//  - The retail effects fog in the vertex shader with the eye RANGE (normalmapped.fxo vs_2_0 blob 22, simple.fxo vs_1_1 blob 1, decoded with
//    tools/render/d3d9_disasm.py): f = 1 - FogEnable * clamp((|eye - world position| - Start) / (End - Start), 0, 1), and the colour is
//    lerp(FogColor, colour, f); terrain.fxo the same (terrain_common.gdshaderinc). The values are the map's Weather block (map.ini over
//    weather.ini, MapWeather).
// INFERENCE (reported by S-1651): the fixed-function vertex fog of the CPU particles and the classic W3D materials uses the same range distance and
// linear factor as the effects (D3DRS_RANGEFOGENABLE and the fog vertex mode were not read); the FX materials (normalmapped.fxo, which output oFog to the
// fixed-function fog stage) take the same per-blend fog colour as the ShaderClass path; the port evaluates the factor per pixel from the interpolated
// view position instead of per vertex.

#pragma once

#include <cstdint>
#include <string>

namespace W3DHardwareFog
{

enum FogMode
{
	FOG_DISABLE = 0,        ///< no fog
	FOG_ENABLE = 1,         ///< toward the fog colour
	FOG_SCALE_FRAGMENT = 2, ///< toward black (an additive surface fades out)
	FOG_WHITE = 3           ///< toward white (a multiply surface fades out)
};

// RW 0x537CE0 for the blend fields of a ShaderClass word (the word's own fog field is not read: Enable_Fog only ever raises it from FOG_DISABLE).
FogMode ModeForBlend(unsigned srcBlend, unsigned dstBlend);
// The same for a whole ShaderClass word (src = bits 14-15, dst = bits 5-7).
FogMode ModeForShaderWord(std::uint32_t word);

// The fog factor of the effects (1 = no fog, 0 = all fog colour) at eye range `range`.
float Factor(bool enabled, float start, float end, float range);

// GLSL of the factor and of the per-mode colour, shared by the generated W3D shaders and the particle shaders: declares the global uniforms
// w3d_fog (x enable, y start, z end) and w3d_fog_color (gamma-space colour) and `vec3 w3d_apply_fog(vec3 c, float range)` for `mode`.
std::string GlslFor(FogMode mode);

// The stop line (S-1651) the device layer reports while the fog is applied.
const char *StopText();

} // namespace W3DHardwareFog
