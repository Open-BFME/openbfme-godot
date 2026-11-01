// OpenBFME. GPL-3.0.
//
// ScreenHilightFilter: retail's glow filter, W3DFilters[4] (lane GLOW-1): the gaussian kernel, the blur passes' constants and the reference arithmetic of
// the glow shaders. Client / draw code only; GodotGlowEffect.cpp runs the same arithmetic on the GPU.
//
// TARGET FACTS (RotWK game.dat, stop S-001 caveat; BFME2 decomp ScreenHilightFilterPreRender.cpp / ScreenHilightFilter.cpp /
// W3DShaderManagerPerformGlow.cpp / W3DShaderGaussianVector.cpp for the donors, RotWK addresses read here):
//  - Kernel (W3DShaderManager::createGaussianVector, RW 0x477969, called by the filter's init RW 0x4FA394 and by its preRender RW 0x4FA423 / on the
//    dirty byte RW 0x4FA8E7): parameters (components 2, taps = GlowDiameter, coefficient0 = RadiusScale1, amplitude0 = Amplitude1, coefficient1 =
//    RadiusScale2, amplitude1 = Amplitude2; RW 0x4FA907 .. 0x4FA944). scale = 15.0 (RW 0xBDC6CC) / taps; above 1.0 it becomes (1 - scale) * 0.5 + scale.
//    center = (taps - 1) * 0.5. For i in [0, taps): x = i - center - 0.1 (RW 0xBD83D4), norm = x * x / (center * center), z = the sum over the
//    components of amplitude_j * scale * (1 / exp(norm * coefficient_j)); a sample (x, 0, z) is kept when z > 0.01 (RW 0xBE5600). The list is padded with
//    (0, 0, 0) to a multiple of 4. The arithmetic is x87 (24-bit precision, a float operation at every step) around the CRT exp (RW 0xA3D6B6).
//  - W3DShaderManager::performGlow (RW 0x476C3F; weightScale = GlowIntensity, RW 0x4FA806): two passes over a square texture of GlowTextureWidth texels,
//    each in groups of 4 samples, one full-target quad per group with shaders\Glow.vso / Glow.pso (RW 0x477934 / 0x47794F load them from the archives;
//    the files are in shaders.big). The sampler states are CLAMP (u, v) and LINEAR (mag, min, mip) on the four stages (RW 0x476CBC .. 0x476D4E:
//    SetSamplerState types 1 and 2 = 3, types 5 / 6 / 7 = 2). Per group the pixel constants c0 .. c3 are z * weightScale and the vertex constants c10 ..
//    c13 the offsets (x / size, y / size) in the first (horizontal) pass and (y / size, x / size) in the second. The first group writes with no blending
//    (render state 0x1B = 0), every later one with ALPHABLENDENABLE = 1, SRCBLEND = ONE (0x13 = 2), DESTBLEND = INVSRCCOLOR (0x14 = 4) (RW 0x476EC5 ..
//    0x476ED9, 0x476F24 .. 0x476F2E): dst' = src + dst * (1 - src). The second pass reads the first pass's result and renders into the filter's main
//    texture.
//  - shaders\Glow.vso (vs_1_1): oPos = v0; oT0 .. oT3 = v1 + c10 .. c13. shaders\Glow.pso (ps_1_1): tex t0 .. t3; r0 = t0 * c0; r0 = t1 * c1 + r0; r0 =
//    t2 * c2 + r0; r0 = t3 * c3 + r0; r1 = dp3(r0, c4); r0.a = r1.a. The pixel shader's registers saturate (ps_1_x), so every step stops at 1. c4 is
//    not set by performGlow (the alpha is never read: the blend uses colours only).
//  - postRender (RW 0x4FA7C0 .. 0x4FA8CF): after performGlow the main texture is drawn over the 3D view: render states SRCBLEND 2 (ONE), DESTBLEND 4
//    (INVSRCCOLOR), ALPHABLENDENABLE 1, texture stage 0 with white diffuse (W3DShaderManager::drawViewport(-1, ...), RW 0x475031): the screen blend
//    dst' = glow + frame * (1 - glow). The render targets are A8R8G8B8 (the filter's CreateTexture format 21): every pass result is 8 bit.
//
// INFERENCE (reported by S-5231): a fixed-function stage that modulates the glow texture with the white diffuse (the texture itself); the half-texel
// placement of the quads (the screen quad is placed at -0.5, RW 0x47514F, the standard D3D9 pixel centre fix); that the glow scene render is point
// sampled here from a larger render.

#pragma once

#include "GameEngineDevice/W3DDevice/GameClient/GlowEffect.h"

#include <array>
#include <string>
#include <vector>

namespace ScreenHilightFilter
{

constexpr int kSamplesPerGroup = 4;

struct Sample
{
	float x, y, z;
};

// Device limits (not retail: RW 0x4FA90C passes GlowEffect + 0x04 to createGaussianVector RW 0x477969 unchecked, and that loops `taps` times). A map or
// mod GlowDiameter of 2147483647 would stall the load on the main thread or exhaust memory, so the device refuses a block past these and reports it (stop
// S-5231), never clamps it. 128 taps keeps every retail value (environment.ini 15, maps\shellmap1 20, maps\shellmapbackup 75).
constexpr int kMaxGlowDiameter = 128;
constexpr int kMaxGlowTextureWidth = 4096;
// Why the device cannot build this GlowEffect's pass (GlowTextureWidth or GlowDiameter out of range), checked before the kernel is built; empty = usable.
std::string unusableReason(const GlowEffect &effect);

// W3DShaderManager::createGaussianVector (RW 0x477969). `components` is 2 in retail (RW 0x4FA900).
std::vector<Sample> createGaussianVector(int taps, float coefficient0, float amplitude0, float coefficient1, float amplitude1, int components = 2);
// the parameters RW 0x4FA8E7 builds from the active GlowEffect (taps = GlowDiameter, RadiusScale1 / Amplitude1, RadiusScale2 / Amplitude2)
std::vector<Sample> createGaussianVector(const GlowEffect &effect);

// One draw of performGlow: the four taps' pixel weights (c0 .. c3) and texture coordinate offsets (c10 .. c13).
struct Group
{
	std::array<float, 4> weight;                // z * weightScale
	std::array<std::array<float, 2>, 4> offset; // texture coordinate offsets, in UV units
	bool blend;                                 // false for the first group of a pass (render state 0x1B = 0)
};

// The groups of one performGlow pass: `vertical` false = the horizontal first pass (offset = (x, y) / size), true = the second ((y, x) / size).
std::vector<Group> blurPass(const std::vector<Sample> &kernel, float weightScale, int textureSize, bool vertical);

// 8 bit render target store (A8R8G8B8): round to nearest of a value clamped to [0, 1]
float store8(float v);

// Glow.pso on one pixel: the four sampled colours (rgb) and the group's weights -> the pixel shader's output rgb (saturating).
std::array<float, 3> glowPixel(const std::array<std::array<float, 3>, 4> &taps, const std::array<float, 4> &weight);
// the render state blend of the passes and of the composite: ONE / INVSRCCOLOR
std::array<float, 3> blendOneInvSrcColor(const std::array<float, 3> &src, const std::array<float, 3> &dst);

// The items of the retail pass this port does not reproduce (S-5231), one line each.
const std::vector<const char *> &unverified();

} // namespace ScreenHilightFilter
