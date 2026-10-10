// OpenBFME. GPL-3.0.
//
// Core (Godot-free) half of the device material layer: the compile-time shape of one W3D material (W3DShaderKey) and the
// generator of its Godot `spatial` shader text. Unit tests read the generated text; the Godot side only compiles it.

#pragma once

#include "Libraries/WWVegas/WW3D2/mapper.h"
#include "Libraries/WWVegas/WW3D2/meshrender.h"
#include "Libraries/WWVegas/WW3D2/shader.h"

#include <string>

// Palette layout shared by the shader and the instancer.
constexpr int W3D_PALETTE_WIDTH_LOG2 = 10;
constexpr int W3D_PALETTE_WIDTH = 1 << W3D_PALETTE_WIDTH_LOG2;
constexpr int W3D_PALETTE_TEXELS_PER_PIVOT = 3;

// The compile-time shape of one material. Equal keys share one Shader.
struct W3DShaderKey
{
	bool Skin = false;
	bool Dual = false;
	bool Rigid() const { return !Skin; }
	int Camera = 0; // MeshCameraMode
	bool CullDisabled = false;
	int Blend = 0;  // W3DGodotBlend
	bool DepthWrite = true;
	bool DepthTestDisabled = false;
	bool AlphaTest = false;
	bool AlphaTestLessEqual = false;
	bool Unshaded = false;
	bool Texturing = false;
	int PriGradient = 1;
	int DetailColor = 0;
	int DetailAlpha = 0;
	bool HasDCG = false;
	bool Fx = false;
	// RENDER-2: an FX material of simple.fx (simple.fxo, Shaders.big; tools/render/d3d9_disasm.py blobs 0 / 1): unlit, colour = Texture_0 * saturate(ColorEmissive),
	// alpha = Texture_0.a; the UV transform is the stage 0 matrix (W3D_SimpleFx_Uv_Matrix). Fx stays false (no normalmapped.fxo lighting).
	bool SimpleFx = false;
	bool NormalMap = false;
	bool Tex[2] = { false, false };
	bool Uv2 = false; // stage 1 reads its own coordinates
	int TexGen[2] = { 0, 0 };           // W3DTexGen
	bool ViewDependent[2] = { false, false };
	bool Projection[2] = { false, false };
	bool ClampU[2] = { false, false };
	bool ClampV[2] = { false, false };
	bool NoLod[2] = { false, false };
	bool Tangents = false;
	// Stop S-119 (lane MAPOBJ-1): the stage 0 texture has a housecolor.ini texture (a mask) and the instance carries a team colour in
	// INSTANCE_CUSTOM.z (W3D_HOUSE_COLOR_PACK). The combine is a HYPOTHESIS: base.rgb = mix(base.rgb, base.rgb * team, mask.a); the
	// defaultw3d.fxo permutations have no sampler for HouseColorTexture, so the real combine is not recoverable from the effect (S-022).
	bool HouseColor = false;
	// lane CAH-2 (S-1408): w3d_hc_tex is a house colour texture already recoloured texel by texel (RotWK RW 0x531C77, a colour set such as a
	// Create-a-Hero's kind 3: W3DInstancer::add_model_colored): it replaces the base where its alpha is set (INFERENCE: the combine is unrecovered, S-022)
	bool HouseColorBaked = false;
	// lane RENDER-4 (S-1651): the map's hardware fog, W3DHardwareFog::FogMode (0 = none): ShaderClass::Enable_Fog's choice for the surface's blend
	int Fog = 0;

	std::string Id() const;
};

// The packed instance team colour of INSTANCE_CUSTOM.z: 0 = none, else 1 + (r << 16 | g << 8 | b) with 8 bit sRGB channels (every value up
// to 2^24 is exact in a float).
inline float W3D_Pack_House_Color(int r, int g, int b)
{
	return 1.0f + (float)(((r & 255) << 16) | ((g & 255) << 8) | (b & 255));
}

std::string Generate_W3D_Shader_Code(const W3DShaderKey &key);

// RENDER-2: simple.fxo vs_1_1 (blob 1): oT0.xy = uv * TexCoordTransform_0.xy + Time * TexCoordTransform_0.zw (the preshader's c4 = Time * zw).
// Rows 0 and 1 of the stage matrix (applied to (u, v, 1, 0)) for `timeSeconds`. INFERENCE (S-023): Time is the effect's seconds clock.
void W3D_SimpleFx_Uv_Matrix(const float transform[4], float timeSeconds, float row0[4], float row1[4]);

// RENDER-1: the D3D9 `lit` instruction clamps its power operand to [-127.9961, 127.9961] (defaultw3d.fxo's specular).
constexpr float W3D_LIT_POWER_LIMIT = 127.9961f;

// Reference arithmetic of the generated lighting, for the numeric tests (the GLSL text is the same formula):
//  * defaultw3d.fxo `lit(N.L, N.H, -, power)`: the specular factor, 0 unless both dot products are positive, the power clamped.
float W3D_Lit_Specular(float ndl, float ndh, float power);
//  * normalmapped.fxo pixel specular of one light: the INTERPOLATED tangent-space vectors lt / ht (not renormalized, as the
//    pixel shader reads its texture coordinates) against the texel normal nt: [nt.ht > 0][nt.lt > 0] pow(nt.ht, exponent).
float W3D_NormalMapped_Specular(const float nt[3], const float lt[3], const float ht[3], float exponent);
