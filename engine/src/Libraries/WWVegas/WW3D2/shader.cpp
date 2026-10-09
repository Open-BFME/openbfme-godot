// OpenBFME: faithful rebuild of The Battle for Middle-earth II: Rise of the Witch-king 2.01.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// See shader.h for sources. ZH Libraries/Source/WWVegas/WW3D2/shader.cpp ShaderClass::Apply, with the BFME2
// ShaderClassApply.cpp differences (the extra alpha test for the SRCALPHA / INVSRCALPHA pair).

#include "Libraries/WWVegas/WW3D2/shader.h"

namespace
{
// ZH shader.cpp:378-394 / BFME2 ShaderClassApply.cpp:910-928.
const int kSrcBlendLUT[4] = { W3D_D3DBLEND_ZERO, W3D_D3DBLEND_ONE, W3D_D3DBLEND_SRCALPHA, W3D_D3DBLEND_DESTCOLOR };
const int kDstBlendLUT[6] = { W3D_D3DBLEND_ZERO, W3D_D3DBLEND_ONE, W3D_D3DBLEND_SRCCOLOR, W3D_D3DBLEND_INVSRCCOLOR,
	W3D_D3DBLEND_SRCALPHA, W3D_D3DBLEND_INVSRCALPHA };

const char *d3dBlendName(int b)
{
	switch (b)
	{
	case W3D_D3DBLEND_ZERO: return "ZERO";
	case W3D_D3DBLEND_ONE: return "ONE";
	case W3D_D3DBLEND_SRCCOLOR: return "SRCCOLOR";
	case W3D_D3DBLEND_INVSRCCOLOR: return "INVSRCCOLOR";
	case W3D_D3DBLEND_SRCALPHA: return "SRCALPHA";
	case W3D_D3DBLEND_INVSRCALPHA: return "INVSRCALPHA";
	case W3D_D3DBLEND_DESTCOLOR: return "DESTCOLOR";
	}
	return "?";
}

void setDivergent(W3DRenderState &st, W3DGodotBlend blend, const std::string &note)
{
	st.Blend = blend;
	st.Divergent = true;
	st.Divergence = note;
}
} // namespace

W3DRenderState Translate_W3D_Shader(const W3dShaderStruct &shader)
{
	W3DRenderState st;

	// The loader forces ColorMask to ENABLE (Convert_Shader), so the "no colour" arm of Apply never runs.
	st.SrcBlend = kSrcBlendLUT[shader.SrcBlend < 4 ? shader.SrcBlend : 0];
	st.DstBlend = kDstBlendLUT[shader.DestBlend < 6 ? shader.DestBlend : 0];
	const bool srcOk = shader.SrcBlend < 4;
	const bool dstOk = shader.DestBlend < 6;

	st.BlendEnabled = st.SrcBlend != W3D_D3DBLEND_ONE || st.DstBlend != W3D_D3DBLEND_ZERO;

	// BFME2 ShaderClassApply.cpp:987-1010. Alpha test: ENABLE -> ref 0x60; the 2-bit field's value 2 -> test with ref 1;
	// else the (SRCALPHA, INVSRCALPHA) pair in either order -> test with ref 1.
	unsigned char alphaRef = 1;
	if (shader.AlphaTest == W3DSHADER_ALPHATEST_ENABLE)
	{
		alphaRef = 0x60;
		st.AlphaTest = true;
	}
	else if (shader.AlphaTest == 2)
	{
		st.AlphaTest = true;
	}
	else if ((st.SrcBlend == W3D_D3DBLEND_SRCALPHA && st.DstBlend == W3D_D3DBLEND_INVSRCALPHA) ||
		(st.SrcBlend == W3D_D3DBLEND_INVSRCALPHA && st.DstBlend == W3D_D3DBLEND_SRCALPHA))
	{
		st.AlphaTest = true;
	}
	if (st.AlphaTest)
	{
		if (st.SrcBlend == W3D_D3DBLEND_INVSRCALPHA)
		{
			st.AlphaRef = (unsigned char)(0xFF - alphaRef);
			st.AlphaTestLessEqual = true;
		}
		else
		{
			st.AlphaRef = alphaRef;
			st.AlphaTestLessEqual = false;
		}
	}

	st.DepthFunc = (int)shader.DepthCompare + 1; // ZH shader.cpp:1021
	st.DepthWrite = shader.DepthMask != 0;
	st.Texturing = shader.Texturing != 0;
	st.PriGradient = shader.PriGradient;
	st.SecGradient = shader.SecGradient;
	// Convert_Shader copies the detail functions into the post-detail functions Apply reads.
	st.DetailColorFunc = shader.DetailColorFunc;
	st.DetailAlphaFunc = shader.DetailAlphaFunc;

	// ---- Godot ----
	if (!srcOk || !dstOk)
	{
		setDivergent(st, W3D_GODOT_BLEND_OPAQUE, "blend field outside the file's enumeration (src " + std::to_string(shader.SrcBlend) + ", dst " +
			std::to_string(shader.DestBlend) + "); drawn opaque");
	}
	else if (!st.BlendEnabled)
	{
		st.Blend = W3D_GODOT_BLEND_OPAQUE;
	}
	else
	{
		const int s = st.SrcBlend;
		const int d = st.DstBlend;
		if (s == W3D_D3DBLEND_SRCALPHA && d == W3D_D3DBLEND_INVSRCALPHA)
		{
			st.Blend = W3D_GODOT_BLEND_MIX;
		}
		else if (s == W3D_D3DBLEND_ONE && d == W3D_D3DBLEND_ONE)
		{
			st.Blend = W3D_GODOT_BLEND_ADD;
		}
		else if (s == W3D_D3DBLEND_SRCALPHA && d == W3D_D3DBLEND_ONE)
		{
			st.Blend = W3D_GODOT_BLEND_ADD_ALPHA;
		}
		else if ((s == W3D_D3DBLEND_ZERO && d == W3D_D3DBLEND_SRCCOLOR) || (s == W3D_D3DBLEND_DESTCOLOR && d == W3D_D3DBLEND_ZERO))
		{
			st.Blend = W3D_GODOT_BLEND_MUL;
		}
		else if (s == W3D_D3DBLEND_ONE && d == W3D_D3DBLEND_INVSRCALPHA)
		{
			st.Blend = W3D_GODOT_BLEND_PREMUL;
		}
		else if (s == W3D_D3DBLEND_ZERO && d == W3D_D3DBLEND_ONE)
		{
			st.Blend = W3D_GODOT_BLEND_NONE; // dst * 1: the colour buffer is left alone
		}
		else
		{
			// Spec 3.2: (ONE, SRCALPHA) x26, (ONE, INVSRCCOLOR) x14, (ONE, SRCCOLOR) x6 and anything else. Godot 4 has no
			// blend mode with these factors; the nearest is additive. Exact output needs the framebuffer as a texture.
			setDivergent(st, W3D_GODOT_BLEND_APPROX_ADD,
				std::string("no Godot blend mode for (") + d3dBlendName(s) + ", " + d3dBlendName(d) + "); drawn as blend_add");
		}
	}

	if (st.DepthFunc == W3D_D3DCMP_ALWAYS)
	{
		st.DepthTestDisabled = true;
	}
	else if (st.DepthFunc == W3D_D3DCMP_NEVER)
	{
		st.NeverDraws = true;
	}
	else if (st.DepthFunc != W3D_D3DCMP_LESSEQUAL)
	{
		// Godot's depth test is fixed (greater-or-equal reversed Z, i.e. LESSEQUAL). LESS differs only on coplanar
		// geometry; EQUAL / GREATER / NOTEQUAL / GREATEREQUAL do not occur in the retail corpus (spec 3.2 lists 3, 1, 7, 0).
		const bool less = st.DepthFunc == W3D_D3DCMP_LESS;
		std::string note = less ? "depth function LESS drawn as LESSEQUAL (differs on coplanar surfaces only)"
								: "unsupported depth function " + std::to_string(st.DepthFunc) + " drawn as LESSEQUAL";
		st.Divergent = true;
		st.Divergence += (st.Divergence.empty() ? "" : "; ") + note;
	}
	return st;
}

bool W3D_Shader_Is_Sorted(const W3dShaderStruct &shader)
{
	return shader.DestBlend != W3DSHADER_DESTBLENDFUNC_ZERO && shader.AlphaTest == W3DSHADER_ALPHATEST_DISABLE;
}

const char *W3D_Godot_Blend_Name(W3DGodotBlend blend)
{
	switch (blend)
	{
	case W3D_GODOT_BLEND_OPAQUE: return "opaque";
	case W3D_GODOT_BLEND_MIX: return "blend_mix";
	case W3D_GODOT_BLEND_ADD: return "blend_add (alpha 1)";
	case W3D_GODOT_BLEND_ADD_ALPHA: return "blend_add";
	case W3D_GODOT_BLEND_MUL: return "blend_mul";
	case W3D_GODOT_BLEND_PREMUL: return "blend_premul_alpha";
	case W3D_GODOT_BLEND_NONE: return "none";
	case W3D_GODOT_BLEND_APPROX_ADD: return "blend_add (approximation)";
	}
	return "?";
}
