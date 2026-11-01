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
	// A classic surface that retail converts to DefaultW3D.fx (w3dfxconvert.h: W3D_Fx_Conversion_Refusal empty, and the static level's ShaderMaterialReplacement
	// on): lit by defaultw3d.fxo's pixel shaders, whose secondary texture and house colour combines differ from the fixed-function stage ops the other
	// classic surfaces keep (ShaderClassApply.cpp). Set by the material factory only for MATERIAL_CLASSIC surfaces.
	bool ConvertedFx = false;
	// The shader's secondary gradient (ShaderClass::Get_Secondary_Gradient, the W3dShaderStruct field): D3DRS_SPECULARENABLE (RW 0x537AE5). A fixed-function
	// surface (not ConvertedFx; the conversion needs 0) has a vertex specular only with it; see the house colour combine (stop S-6023)
	int SecGradient = 0;
	bool Tex[2] = { false, false };
	bool Uv2 = false; // stage 1 reads its own coordinates
	int TexGen[2] = { 0, 0 };           // W3DTexGen
	bool ViewDependent[2] = { false, false };
	bool Projection[2] = { false, false };
	bool ClampU[2] = { false, false };
	bool ClampV[2] = { false, false };
	bool NoLod[2] = { false, false };
	bool Tangents = false;
	// Stops S-022 / S-119: the stage 0 texture of a classic surface has a housecolor.ini texture (w3d_hc_tex; its alpha is the team region) and the instance
	// carries a team colour in INSTANCE_CUSTOM.z (W3D_Pack_House_Color). The texture is recoloured with that colour as RotWK's kind 1 (RW 0x531C77) and
	// combined AFTER the light, as retail does (defaultw3d.fxo HouseColorEnable permutation, or the fixed-function BLENDTEXTUREALPHA: ConvertedFx picks
	// the rule); the surface's own second texture is dropped for such an instance. Classic surfaces only (not Fx / SimpleFx).
	bool HouseColor = false;
	// lane CAH-2 (S-1408): w3d_hc_tex is a house colour texture already recoloured texel by texel (RotWK RW 0x531C77, a colour set such as a Create-a-Hero's
	// kind 3 or a player's kind 1: W3DInstancer::add_model_colored), combined as above for every instance (no team colour in INSTANCE_CUSTOM.z is read)
	bool HouseColorBaked = false;
	// lane RENDER-4 (S-1651): the map's hardware fog, W3DHardwareFog::FogMode (0 = none): ShaderClass::Enable_Fog's choice for the surface's blend
	int Fog = 0;
	// lane BUILD-5 r3: a drawable drawn at an opacity below 1 (the placement ghost's 0.45, RW 0xC12408): DONOR ZH dx8renderer.cpp:1884-1916 and
	// RTS3DScene::flushTranslucentObjects (W3DScene.cpp:1606-1626): the translucent drawables render after the scene with their shader's blend switched to
	// SRC_ALPHA / ONE_MINUS_SRC_ALPHA (depth writes kept, theAlphaShader), the material's opacity set to the override and the alpha test reference scaled by it;
	// an additive surface keeps its blend with its diffuse scaled. Here an opaque surface becomes blend_mix with ALPHA = alpha * the instance fade (v_fade
	// carries the opacity), no dither; the other blends already scale by v_fade. Only instancers that ask for it (W3DInstancer::set_alpha_override) use it
	bool AlphaOverride = false;
	// S-5017: OpenBFME BFME1 floor-overlay lift; not a retail shader setting.
	bool FloorOverlay = false;
	// lane OCCLUDE-1: one of the behind-building marker passes chained after an opaque surface (W3DInstancer::set_instance_occlusion; RotWK
	// RTS3DScene::flushOccludedObjectsIntoStencil RW 0x470176, GameClient/RTS3DSceneOcclusion.h). The vertex code is the surface's own; an instance whose
	// INSTANCE_CUSTOM.w occlusion bits (W3D_OCCLUSION_*) do not ask for the pass collapses to a point. Both passes keep the surface's alpha test and dither.
	//  * W3D_OCCLUSION_PASS_MARK: an occluder's pixels in front (depth test as usual) write W3D_OCCLUSION_STENCIL_MARK into the stencil, nothing visible;
	//  * W3D_OCCLUSION_PASS_COLOR: an occludee's pixels BEHIND what is drawn (depth_test_inverted) where the stencil holds the mark get the player's colour
	//    (global w3d_occl_colors[slot], gamma space, blended SRC_ALPHA / ONE_MINUS_SRC_ALPHA as retail's quad) once: the pixel's stencil becomes
	//    W3D_OCCLUSION_STENCIL_DONE, which no longer passes the compare. Off while the global w3d_occl_on is 0
	int OcclusionPass = 0;

	std::string Id() const;
};

// The packed instance team colour of INSTANCE_CUSTOM.z: 0 = none, else 1 + (r << 16 | g << 8 | b) with 8 bit sRGB channels (every value up
// to 2^24 is exact in a float).
inline float W3D_Pack_House_Color(int r, int g, int b)
{
	return 1.0f + (float)(((r & 255) << 16) | ((g & 255) << 8) | (b & 255));
}

// lane OCCLUDE-1: the occlusion passes and their INSTANCE_CUSTOM.w bits (above the rigid bone, the sway type and the fogged bit; every value stays below
// 2^24, exact in a float): bits 15 .. 19 the occludee's colour slot (0 = not an occludee), bit 20 the occluder
constexpr int W3D_OCCLUSION_PASS_MARK = 1;
constexpr int W3D_OCCLUSION_PASS_COLOR = 2;
constexpr int W3D_OCCLUSION_SLOT_SHIFT = 15;
constexpr int W3D_OCCLUSION_SLOTS = 32;
constexpr int W3D_OCCLUSION_OCCLUDER_BIT = 1 << 20;
// lane INGAMEBG-1: bit 21, the instance belongs to a scene without fog (the Apt View3D's own scene, RW 0x5424E0: SceneClass's FogEnabled false), so the
// map's hardware fog (S-1651) is not applied to it
constexpr int W3D_NO_SCENE_FOG_BIT = 1 << 21;
constexpr int W3D_OCCLUSION_STENCIL_MARK = 255;
constexpr int W3D_OCCLUSION_STENCIL_DONE = 1;
// the render priorities of the passes: after the opaque scene, before the other blended surfaces (retail draws the quads before its translucent list);
// the marks before the colours
constexpr int W3D_OCCLUSION_PRIORITY_MARK = -128;
constexpr int W3D_OCCLUSION_PRIORITY_COLOR = -127;

enum class W3DSamplerPolicy { Anisotropic, Metal16 };

std::string Generate_W3D_Shader_Code(const W3DShaderKey &key, W3DSamplerPolicy samplers = W3DSamplerPolicy::Anisotropic);

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
//  * the fixed-function house colour of one lit pixel (the GLSL of Generate_W3D_Shader_Code for a non-converted HouseColor key; docs/HOUSE_COLOR_PORT.md):
//    stage 0 MODULATE2X(texel, diffuse), stage 1 BLENDTEXTUREALPHA(house texel, current), then with the secondary gradient set the specular is added twice,
//    cur.a * spec (stage 2, MODULATEALPHA_ADDCOLOR) and spec (D3DRS_SPECULARENABLE). `diffuse` is v_lit0.rgb, `specular` v_lit1 (both carry the effect's 0.5).
//    out = rgb (saturation is the frame buffer's, not applied here); *alpha = t0.a * v_lit0.a
void W3D_FixedFunction_House_Color(const float t0[4], const float diffuse[3], float diffuseAlpha, const float specular[3], bool secondaryGradient,
	const float hc[4], float rgb[3], float *alpha);
// The stop line ("[S-6023] ...") of a fixed-function surface whose house colour specular rests on assumptions about the D3D runtime and the caps. Empty
// when the surface is not fixed-function lit with the secondary gradient set.
std::string W3D_FixedFunction_House_Color_Stop(bool converted, bool lit, int secGradient);
