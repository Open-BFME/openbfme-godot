// OpenBFME unit tests: which classic W3D surfaces retail converts to DefaultW3D.fx (w3dfxconvert.h). The expected refusals are the checks of the BFME2
// decomp's MeshMatDescFXShaderConversion.cpp (rva00199FFB), in its order, and the mapper bodies of TextureMapperFXShaderConvert.cpp; nothing is read back
// from the code under test.

#include "doctest.h"

#include "Libraries/WWVegas/WW3D2/mapper.h"
#include "Libraries/WWVegas/WW3D2/shader.h"
#include "Libraries/WWVegas/WW3D2/w3dfxconvert.h"

#include <string>

namespace
{
// the commonest corpus shader (spec 3.2): LEQUAL, depth write, ONE / ZERO, modulate, texturing, no detail
W3dShaderStruct opaqueShader()
{
	W3dShaderStruct s = {};
	s.DepthCompare = 3;
	s.DepthMask = 1;
	s.ColorMask = 1;
	s.SrcBlend = 1;
	s.DestBlend = 0;
	s.PriGradient = W3D_GRADIENT_MODULATE;
	s.Texturing = 1;
	return s;
}

W3DFxConversionSurface surfaceOf(const W3dShaderStruct &shader)
{
	W3DFxConversionSurface c;
	c.Shader = &shader;
	c.StageTexture[0] = true;
	return c;
}
} // namespace

TEST_CASE("fx conversion: a single pass, single material, opaque modulated surface converts")
{
	const W3dShaderStruct sh = opaqueShader();
	CHECK(W3D_Fx_Conversion_Refusal(surfaceOf(sh)) == "");
}

TEST_CASE("fx conversion: the refusals of rva00199FFB, each with its message")
{
	{
		W3dShaderStruct sh = opaqueShader();
		W3DFxConversionSurface c = surfaceOf(sh);
		c.PassCount = 2;
		CHECK(W3D_Fx_Conversion_Refusal(c) == "Cannot convert multi pass material");
	}
	{
		W3dShaderStruct sh = opaqueShader();
		W3DFxConversionSurface c = surfaceOf(sh);
		c.Pass0Surfaces = 2;
		CHECK(W3D_Fx_Conversion_Refusal(c) == "Cannot convert multiple materials on one mesh");
	}
	{
		W3dShaderStruct sh = opaqueShader();
		W3DFxConversionSurface c = surfaceOf(sh);
		c.StageTexture[0] = false;
		c.StageTexture[1] = true;
		CHECK(W3D_Fx_Conversion_Refusal(c) == "Texture stage 1 used without stage 0");
	}
	{
		W3dShaderStruct sh = opaqueShader();
		sh.DepthCompare = 7; // ALWAYS
		CHECK(W3D_Fx_Conversion_Refusal(surfaceOf(sh)) == "Depth comparison function is not less-equal (Uses 7)");
		sh.DepthCompare = 1; // LESS is accepted
		CHECK(W3D_Fx_Conversion_Refusal(surfaceOf(sh)) == "");
	}
	{
		W3dShaderStruct sh = opaqueShader();
		sh.PriGradient = W3D_GRADIENT_ADD;
		CHECK(W3D_Fx_Conversion_Refusal(surfaceOf(sh)) == "Primary gradient is not 'Modulate' or 'Disable' (Uses 2)");
		sh.PriGradient = W3D_GRADIENT_DISABLE; // Disable is accepted
		CHECK(W3D_Fx_Conversion_Refusal(surfaceOf(sh)) == "");
	}
	{
		W3dShaderStruct sh = opaqueShader();
		sh.SecGradient = 1;
		CHECK(W3D_Fx_Conversion_Refusal(surfaceOf(sh)) == "Secondary gradient is not 'Disable' (Uses 1)");
	}
	{
		W3dShaderStruct sh = opaqueShader();
		sh.DetailAlphaFunc = W3D_DETAILALPHA_SCALE; // a surface like gbforbidpool
		CHECK(W3D_Fx_Conversion_Refusal(surfaceOf(sh)) == "DetailAlpha is not 'Disable' (Uses 2)");
	}
	{
		W3dShaderStruct sh = opaqueShader();
		sh.SrcBlend = 1;
		sh.DestBlend = 4; // ONE / SRCALPHA
		CHECK(W3D_Fx_Conversion_Refusal(surfaceOf(sh)) == "Unsupported SrcBlend/DestBlend combination. Supported: One/Zero, SrcAlpha/InvSrcAlpha, One/One (Uses 1/4)");
		sh.SrcBlend = 2;
		sh.DestBlend = 5; // SRCALPHA / INVSRCALPHA
		CHECK(W3D_Fx_Conversion_Refusal(surfaceOf(sh)) == "");
		sh.SrcBlend = 1;
		sh.DestBlend = 1; // ONE / ONE
		CHECK(W3D_Fx_Conversion_Refusal(surfaceOf(sh)) == "");
	}
	{
		W3dShaderStruct sh = opaqueShader();
		W3DFxConversionSurface c = surfaceOf(sh);
		c.HasVertexMaterial = false;
		CHECK(W3D_Fx_Conversion_Refusal(c) == "no material or shader in pass 0 (refused without a message)");
	}
}

TEST_CASE("fx conversion: the DetailColor functions the conversion maps, and the ones it refuses")
{
	// MeshMatDescFXShaderConversion.cpp:416-433: Scale 2 -> mode 0, Scale2X 11 -> 1, DetailBlend 8 -> 2, ModAlphaAddColor 12 -> 3, HouseColor 13 -> 3 (+ HouseColorEnable)
	CHECK(W3D_Fx_Secondary_Blend_Mode(W3D_DETAILCOLOR_SCALE) == 0);
	CHECK(W3D_Fx_Secondary_Blend_Mode(W3D_DETAILCOLOR_SCALE2X) == 1);
	CHECK(W3D_Fx_Secondary_Blend_Mode(W3D_DETAILCOLOR_DETAILBLEND) == 2);
	CHECK(W3D_Fx_Secondary_Blend_Mode(W3D_DETAILCOLOR_MODALPHAADDCOLOR) == 3);
	CHECK(W3D_Fx_Secondary_Blend_Mode(W3D_DETAILCOLOR_HOUSECOLOR) == 3);
	CHECK(W3D_Fx_Secondary_Blend_Mode(W3D_DETAILCOLOR_DISABLE) == -1);
	for (int f : { W3D_DETAILCOLOR_DETAIL, W3D_DETAILCOLOR_INVSCALE, W3D_DETAILCOLOR_ADD, W3D_DETAILCOLOR_SUB, W3D_DETAILCOLOR_SUBR, W3D_DETAILCOLOR_BLEND,
		W3D_DETAILCOLOR_ADDSIGNED, W3D_DETAILCOLOR_ADDSIGNED2X })
	{
		CAPTURE(f);
		CHECK(W3D_Fx_Secondary_Blend_Mode(f) == -1);
		W3dShaderStruct sh = opaqueShader();
		sh.DetailColorFunc = (uint8)f;
		CHECK(W3D_Fx_Conversion_Refusal(surfaceOf(sh)) ==
			"Unsupported DetailColor mode. Supported: 'Disable', 'Scale', 'Scale2X', 'DetailBlend', 'ModAlphaAddColor' or 'HouseColor' (Uses " + std::to_string(f) + ")");
	}
	for (int f : { W3D_DETAILCOLOR_DISABLE, W3D_DETAILCOLOR_SCALE, W3D_DETAILCOLOR_SCALE2X, W3D_DETAILCOLOR_DETAILBLEND, W3D_DETAILCOLOR_MODALPHAADDCOLOR,
		W3D_DETAILCOLOR_HOUSECOLOR })
	{
		CAPTURE(f);
		W3dShaderStruct sh = opaqueShader();
		sh.DetailColorFunc = (uint8)f;
		CHECK(W3D_Fx_Conversion_Refusal(surfaceOf(sh)) == "");
	}
}

TEST_CASE("fx conversion: the texture mappers the conversion accepts (TextureMapperFXShaderConvert.cpp)")
{
	std::string why;
	// no mapper object: plain UV, SILHOUETTE, an undefined type
	CHECK(W3D_Mapper_Fx_Convertible(W3D_MAPPING_UV, 0, "", &why));
	CHECK(W3D_Mapper_Fx_Convertible(W3D_MAPPING_SILHOUETTE, 1, "", &why));
	CHECK(W3D_Mapper_Fx_Convertible(99, 0, "", &why));
	// Scale: stage 0 or 1
	CHECK(W3D_Mapper_Fx_Convertible(W3D_MAPPING_SCALE, 1, "UScale=2", &why));
	CHECK(why.empty());
	CHECK_FALSE(W3D_Mapper_Fx_Convertible(W3D_MAPPING_SCALE, 2, "UScale=2", &why));
	CHECK(why == "Scale texture mapper supported in stage 0 or 1 only");
	// LinearOffset: stage 0 or 1, |UOffset|, |VOffset| <= 0.001, no ClampFix
	CHECK(W3D_Mapper_Fx_Convertible(W3D_MAPPING_LINEAR_OFFSET, 0, "UPerSec=0.5\nVPerSec=0", &why));
	CHECK(W3D_Mapper_Fx_Convertible(W3D_MAPPING_LINEAR_OFFSET, 0, "UOffset=0.0005", &why));
	CHECK_FALSE(W3D_Mapper_Fx_Convertible(W3D_MAPPING_LINEAR_OFFSET, 0, "UOffset=0.25", &why));
	CHECK(why == "Can't convert LinearOffset texture mapper due to non-zero UOffset or VOffset value");
	CHECK_FALSE(W3D_Mapper_Fx_Convertible(W3D_MAPPING_LINEAR_OFFSET, 1, "VOffset=-0.5", &why));
	CHECK_FALSE(W3D_Mapper_Fx_Convertible(W3D_MAPPING_LINEAR_OFFSET, 0, "ClampFix=true", &why));
	CHECK(why == "Can't convert LinearOffset texture mapper due to usage of ClampFix option");
	CHECK_FALSE(W3D_Mapper_Fx_Convertible(W3D_MAPPING_LINEAR_OFFSET, 2, "", &why));
	CHECK(why == "LinearOffset texture mapper supported in stage 0 or 1 only");
	// Grid: stage 0 only
	CHECK(W3D_Mapper_Fx_Convertible(W3D_MAPPING_GRID, 0, "FPS=8\nLog2Width=2\nLast=16", &why));
	CHECK_FALSE(W3D_Mapper_Fx_Convertible(W3D_MAPPING_GRID, 1, "FPS=8", &why));
	CHECK(why == "Grid texture mapper supported in stage 0 only");
	// every other mapper class reaches no conversion body
	for (int t : { W3D_MAPPING_ENVIRONMENT, W3D_MAPPING_CHEAP_ENVIRONMENT, W3D_MAPPING_SCREEN, W3D_MAPPING_ROTATE, W3D_MAPPING_SINE_LINEAR_OFFSET,
		W3D_MAPPING_STEP_LINEAR_OFFSET, W3D_MAPPING_ZIGZAG_LINEAR_OFFSET, W3D_MAPPING_WS_CLASSIC_ENV, W3D_MAPPING_WS_ENVIRONMENT, W3D_MAPPING_GRID_CLASSIC_ENV,
		W3D_MAPPING_GRID_ENVIRONMENT, W3D_MAPPING_RANDOM, W3D_MAPPING_EDGE, W3D_MAPPING_BUMPENV, W3D_MAPPING_GRID_WS_CLASSIC_ENV, W3D_MAPPING_GRID_WS_ENVIRONMENT })
	{
		CAPTURE(t);
		CHECK_FALSE(W3D_Mapper_Fx_Convertible(t, 0, "", &why));
		CHECK(why.find("has no FX conversion") != std::string::npos);
	}
	// and a refused stage refuses the surface, naming the stage
	W3dShaderStruct sh = opaqueShader();
	W3DFxConversionSurface c = surfaceOf(sh);
	c.StageTexture[1] = true;
	c.MapperType[1] = W3D_MAPPING_ENVIRONMENT;
	CHECK(W3D_Fx_Conversion_Refusal(c) == "Cannot convert texture mapper in stage 1 (texture mapper " + std::string(W3D_Mapper_Type_Name(W3D_MAPPING_ENVIRONMENT)) +
		" has no FX conversion)");
}

TEST_CASE("fx conversion: the S-6020 note names a Grid mapper stage and the class the surface falls in")
{
	W3dShaderStruct sh = opaqueShader();
	W3DFxConversionSurface c = surfaceOf(sh);
	CHECK(W3D_Fx_Conversion_Stop(c) == "");
	c.MapperType[0] = W3D_MAPPING_GRID;
	c.MapperArgs[0] = "FPS=8";
	CHECK(W3D_Fx_Conversion_Stop(c) == "[S-6020] stage 0 has a Grid mapper: whether retail converts this surface to DefaultW3D.fx depends on which class table reports "
		"Mapper_ID 9 (a second one, RW 0xBEB8D0, is unmatched); it is classed converted");
	sh.DetailAlphaFunc = W3D_DETAILALPHA_SCALE; // refused for another reason: classed fixed-function
	CHECK(W3D_Fx_Conversion_Stop(c).find("it is classed fixed-function") != std::string::npos);
	// a mapper on a stage without a texture is not built
	W3DFxConversionSurface d = surfaceOf(sh);
	d.MapperType[1] = W3D_MAPPING_GRID;
	CHECK(W3D_Fx_Conversion_Stop(d) == "");
}

TEST_CASE("fx conversion: the S-6022 note names a surface retail's fixed-function house colour branch skips")
{
	// bfme2_mesh_material_pass.cpp RecolorHouseColor: the legacy branch needs Get_Pass_Count() == 1 and no stage 0 texture array; a converted surface takes the FX branch
	W3dShaderStruct sh = opaqueShader();
	W3DFxConversionSurface c = surfaceOf(sh);
	CHECK(W3D_House_Color_Stop(c, false) == "");
	c.PassCount = 2;
	CHECK(W3D_House_Color_Stop(c, false) ==
		"[S-6022] retail gives no team colour to this surface: its fixed-function house colour branch needs a single pass (2 here) and no stage 0 texture array (none here); it is drawn with one");
	CHECK(W3D_House_Color_Stop(c, true) == "");
	c.PassCount = 1;
	c.Pass0TexturesDiffer = true;
	CHECK(W3D_House_Color_Stop(c, false) ==
		"[S-6022] retail gives no team colour to this surface: its fixed-function house colour branch needs a single pass (1 here) and no stage 0 texture array (this mesh has one); it is drawn with one");
	CHECK(W3D_House_Color_Stop(c, true) == "");
}
