// OpenBFME. GPL-3.0. See w3dfxconvert.h.

#include "Libraries/WWVegas/WW3D2/w3dfxconvert.h"

#include "Libraries/WWVegas/WW3D2/mapper.h"
#include "Libraries/WWVegas/WW3D2/shader.h"

int W3D_Fx_Secondary_Blend_Mode(int detailColorFunc)
{
	switch (detailColorFunc)
	{
	case W3D_DETAILCOLOR_SCALE: return 0;
	case W3D_DETAILCOLOR_SCALE2X: return 1;
	case W3D_DETAILCOLOR_DETAILBLEND: return 2;
	case W3D_DETAILCOLOR_MODALPHAADDCOLOR: return 3;
	case W3D_DETAILCOLOR_HOUSECOLOR: return 3;
	}
	return -1;
}

std::string W3D_Fx_Conversion_Stop(const W3DFxConversionSurface &s)
{
	for (int stage = 0; stage < 2; ++stage)
	{
		if (s.StageTexture[stage] && s.MapperType[stage] == W3D_MAPPING_GRID)
		{
			return "[S-6020] stage " + std::to_string(stage) + " has a Grid mapper: whether retail converts this surface to DefaultW3D.fx depends on which class table reports "
				"Mapper_ID 9 (a second one, RW 0xBEB8D0, is unmatched); it is classed " + (W3D_Fx_Conversion_Refusal(s).empty() ? "converted" : "fixed-function");
		}
	}
	return std::string();
}

std::string W3D_House_Color_Stop(const W3DFxConversionSurface &s, bool converted)
{
	if (converted || (s.PassCount == 1 && !s.Pass0TexturesDiffer))
	{
		return std::string();
	}
	return std::string("[S-6022] retail gives no team colour to this surface: its fixed-function house colour branch needs a single pass (") + std::to_string(s.PassCount) +
		" here) and no stage 0 texture array (" + (s.Pass0TexturesDiffer ? "this mesh has one" : "none here") + "); it is drawn with one";
}

std::string W3D_Fx_Conversion_Refusal(const W3DFxConversionSurface &s)
{
	if (s.PassCount != 1)
	{
		return "Cannot convert multi pass material";
	}
	if (!s.HasVertexMaterial || !s.Shader)
	{
		return "no material or shader in pass 0 (refused without a message)";
	}
	if (s.Pass0Surfaces != 1)
	{
		return "Cannot convert multiple materials on one mesh";
	}
	const W3dShaderStruct &sh = *s.Shader;
	for (unsigned stage = 0; stage < 2; ++stage)
	{
		std::string why;
		if (!W3D_Mapper_Fx_Convertible(s.MapperType[stage], stage, s.MapperArgs[stage], &why))
		{
			return "Cannot convert texture mapper in stage " + std::to_string(stage) + " (" + why + ")";
		}
	}
	if (!s.StageTexture[0] && s.StageTexture[1])
	{
		return "Texture stage 1 used without stage 0";
	}
	// the file's enumeration is ShaderClass's (PASS_NEVER 0, PASS_LESS 1, PASS_EQUAL 2, PASS_LEQUAL 3; the +1 of shader.cpp:82 is only the D3DCMP value)
	if (sh.DepthCompare != 3 && sh.DepthCompare != 1)
	{
		return "Depth comparison function is not less-equal (Uses " + std::to_string(sh.DepthCompare) + ")";
	}
	// the loader forces ColorMask to ENABLE (shader.cpp Translate_W3D_Shader), so "Color write is not enabled" never fires for a file surface
	if (sh.PriGradient != W3D_GRADIENT_DISABLE && sh.PriGradient != W3D_GRADIENT_MODULATE)
	{
		return "Primary gradient is not 'Modulate' or 'Disable' (Uses " + std::to_string(sh.PriGradient) + ")";
	}
	if (sh.SecGradient != 0)
	{
		return "Secondary gradient is not 'Disable' (Uses " + std::to_string(sh.SecGradient) + ")";
	}
	if (sh.DetailColorFunc != W3D_DETAILCOLOR_DISABLE && W3D_Fx_Secondary_Blend_Mode(sh.DetailColorFunc) < 0)
	{
		return "Unsupported DetailColor mode. Supported: 'Disable', 'Scale', 'Scale2X', 'DetailBlend', 'ModAlphaAddColor' or 'HouseColor' (Uses " +
			std::to_string(sh.DetailColorFunc) + ")";
	}
	if (sh.DetailAlphaFunc != W3D_DETAILALPHA_DISABLE)
	{
		return "DetailAlpha is not 'Disable' (Uses " + std::to_string(sh.DetailAlphaFunc) + ")";
	}
	// (src, dst) in ShaderClass values: ONE / ZERO, SRCALPHA / INVSRCALPHA, ONE / ONE (the file's blend enumerations are ShaderClass's)
	const bool oneZero = sh.SrcBlend == 1 && sh.DestBlend == 0;
	const bool alphaBlend = sh.SrcBlend == 2 && sh.DestBlend == 5;
	const bool additive = sh.SrcBlend == 1 && sh.DestBlend == 1;
	if (!oneZero && !alphaBlend && !additive)
	{
		return "Unsupported SrcBlend/DestBlend combination. Supported: One/Zero, SrcAlpha/InvSrcAlpha, One/One (Uses " + std::to_string(sh.SrcBlend) + "/" +
			std::to_string(sh.DestBlend) + ")";
	}
	return std::string();
}
