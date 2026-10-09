// OpenBFME: faithful rebuild of The Battle for Middle-earth II: Rise of the Witch-king 2.01.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// W3dShaderStruct -> render state. Two layers:
//   1. Translate_W3D_Shader reproduces what ShaderClass::Apply does with the shader bits as D3D render state: the
//      blend factors, alpha test, depth function and mask, and the stage 0 / stage 1 colour combine.
//   2. Godot_Blend_For reduces the D3D blend pair to the nearest Godot 4 blend mode, flags every pair Godot cannot
//      express (the explicit divergence list of spec 3.2) and says what is lost.
//
// Target facts (BFME2 1.06 ShaderClass::Apply, Open-BFME-2 ShaderClassApply.cpp:905-1020, retail RVA 0x137590):
//  * the source blend table is {ZERO, ONE, SRCALPHA, DESTCOLOR}, the destination table
//    {ZERO, ONE, SRCCOLOR, INVSRCCOLOR, SRCALPHA, INVSRCALPHA}. Index 3 of the source table (the file's
//    ONE_MINUS_SRC_ALPHA) is DESTCOLOR, as in ZH shader.cpp:378-384.
//  * blending is on unless (ONE, ZERO).
//  * alpha test ENABLE uses reference 0x60 with GREATEREQUAL (ZH shader.cpp:467-482 the same).
//  * BFME2 ONLY (not in ZH): a shader whose blend pair is (SRCALPHA, INVSRCALPHA) or (INVSRCALPHA, SRCALPHA) also
//    alpha-tests, with reference 1 and GREATEREQUAL, whatever its AlphaTest field says (ShaderClassApply.cpp:993-998).
//    Source blend INVSRCALPHA turns the test into LESSEQUAL 0xFF - reference; the source table cannot produce it.
//  * depth function = DepthCompare + 1 as a D3DCMP value, depth write = DepthMask (ZH shader.cpp:1021-1024).
//  * ColorMask is forced to ENABLE and the fog function to DISABLE when a file shader is converted (ZH w3d_util.cpp:
//    Convert_Shader, BFME2 RVA 0x001A4529); DetailColorFunc / DetailAlphaFunc become the post-detail functions.
// The texture stage ops are ZH shader.cpp:536-910 (no BFME2 matched body of those exists; the BFME2 file differs only in
// the alpha test field width).

#pragma once

#include "Libraries/WWVegas/WW3D2/w3d_file.h"

#include <string>

// D3DBLEND values (d3d8types.h).
enum W3DD3DBlend
{
	W3D_D3DBLEND_ZERO = 1,
	W3D_D3DBLEND_ONE = 2,
	W3D_D3DBLEND_SRCCOLOR = 3,
	W3D_D3DBLEND_INVSRCCOLOR = 4,
	W3D_D3DBLEND_SRCALPHA = 5,
	W3D_D3DBLEND_INVSRCALPHA = 6,
	W3D_D3DBLEND_DESTCOLOR = 9,
};

// D3DCMPFUNC values.
enum W3DD3DCmp
{
	W3D_D3DCMP_NEVER = 1,
	W3D_D3DCMP_LESS = 2,
	W3D_D3DCMP_EQUAL = 3,
	W3D_D3DCMP_LESSEQUAL = 4,
	W3D_D3DCMP_GREATER = 5,
	W3D_D3DCMP_NOTEQUAL = 6,
	W3D_D3DCMP_GREATEREQUAL = 7,
	W3D_D3DCMP_ALWAYS = 8,
};

// ShaderClass::PriGradientType, DetailColorFuncType, DetailAlphaFuncType (ZH shader.h).
enum
{
	W3D_GRADIENT_DISABLE = 0,
	W3D_GRADIENT_MODULATE = 1,
	W3D_GRADIENT_ADD = 2,
	W3D_GRADIENT_BUMPENVMAP = 3,
	W3D_GRADIENT_BUMPENVMAPLUMINANCE = 4,
	W3D_GRADIENT_MODULATE2X = 5,

	W3D_DETAILCOLOR_DISABLE = 0,
	W3D_DETAILCOLOR_DETAIL = 1,
	W3D_DETAILCOLOR_SCALE = 2,
	W3D_DETAILCOLOR_INVSCALE = 3,
	W3D_DETAILCOLOR_ADD = 4,
	W3D_DETAILCOLOR_SUB = 5,
	W3D_DETAILCOLOR_SUBR = 6,
	W3D_DETAILCOLOR_BLEND = 7,
	W3D_DETAILCOLOR_DETAILBLEND = 8,
	W3D_DETAILCOLOR_ADDSIGNED = 9,
	W3D_DETAILCOLOR_ADDSIGNED2X = 10,
	W3D_DETAILCOLOR_SCALE2X = 11,
	W3D_DETAILCOLOR_MODALPHAADDCOLOR = 12,

	W3D_DETAILALPHA_DISABLE = 0,
	W3D_DETAILALPHA_DETAIL = 1,
	W3D_DETAILALPHA_SCALE = 2,
	W3D_DETAILALPHA_INVSCALE = 3,
};

enum W3DGodotBlend
{
	W3D_GODOT_BLEND_OPAQUE,  // no blending: (ONE, ZERO)
	W3D_GODOT_BLEND_MIX,     // blend_mix: (SRCALPHA, INVSRCALPHA)
	W3D_GODOT_BLEND_ADD,     // blend_add with ALPHA = 1: (ONE, ONE)
	W3D_GODOT_BLEND_ADD_ALPHA, // blend_add: (SRCALPHA, ONE)
	W3D_GODOT_BLEND_MUL,     // blend_mul: result = src * dst, for (ZERO, SRCCOLOR) and (DESTCOLOR, ZERO)
	W3D_GODOT_BLEND_PREMUL,  // blend_premul_alpha: (ONE, INVSRCALPHA)
	W3D_GODOT_BLEND_NONE,    // (ZERO, ONE): colour output is dropped, the surface draws nothing visible
	W3D_GODOT_BLEND_APPROX_ADD, // no Godot equivalent; drawn as blend_add (see W3DRenderState::Divergence)
};

struct W3DRenderState
{
	// D3D state ShaderClass::Apply produces
	int SrcBlend = W3D_D3DBLEND_ONE;
	int DstBlend = W3D_D3DBLEND_ZERO;
	bool BlendEnabled = false;
	bool AlphaTest = false;
	unsigned char AlphaRef = 0;
	bool AlphaTestLessEqual = false; // true: LESSEQUAL against AlphaRef (source blend INVSRCALPHA); false: GREATEREQUAL
	int DepthFunc = W3D_D3DCMP_LESSEQUAL;
	bool DepthWrite = true;
	bool Texturing = true;
	int PriGradient = W3D_GRADIENT_MODULATE;
	int SecGradient = 0;
	int DetailColorFunc = W3D_DETAILCOLOR_DISABLE;
	int DetailAlphaFunc = W3D_DETAILALPHA_DISABLE;

	// Godot translation
	W3DGodotBlend Blend = W3D_GODOT_BLEND_OPAQUE;
	bool DepthTestDisabled = false; // DepthFunc ALWAYS maps to depth_test_disabled
	bool NeverDraws = false;        // DepthFunc NEVER: nothing passes the depth test
	bool Divergent = false;         // the Godot output differs from retail; Divergence says how
	std::string Divergence;
};

// Shader fields -> D3D state (see the header for the rules and sources) -> Godot state.
W3DRenderState Translate_W3D_Shader(const W3dShaderStruct &shader);

// ZH ShaderClass::Guess_Sort_Level / WW3D static sort: true when a mesh with this pass 0 shader is placed in the
// sorted (blended) list, spec 3.2: destination blend not ZERO, alpha test off.
bool W3D_Shader_Is_Sorted(const W3dShaderStruct &shader);

// Names for the report.
const char *W3D_Godot_Blend_Name(W3DGodotBlend blend);
