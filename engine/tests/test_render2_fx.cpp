// OpenBFME. RENDER-2 tests: simple.fx surfaces drawn as simple.fxo (Shaders.big, decoded with tools/render/d3d9_disasm.py: ps_1_1 blob 0 `tex t0; mul r0.xyz, t0, v0;
// mov r0.w, t0.w` with v0 = oD0 = ColorEmissive; vs_1_1 blob 1 `mad oT0.xy, v1, TexCoordTransform_0.xy, c4` with the preshader c4 = Time * TexCoordTransform_0.zw).
// No retail files.
#include "doctest.h"

#include "Libraries/WWVegas/WW3D2/shader.h"
#include "Libraries/WWVegas/WW3D2/w3dshadergen.h"

#include <string>

TEST_CASE("render2 fx: the simple.fxo UV transform is uv * xy + Time * zw")
{
	const float t[4] = { 2.0f, 0.5f, 0.25f, -1.0f };
	float r0[4], r1[4];
	W3D_SimpleFx_Uv_Matrix(t, 4.0f, r0, r1);
	// rows applied to (u, v, 1, 0): u' = 2 u + 4 * 0.25, v' = 0.5 v + 4 * -1
	CHECK(r0[0] == 2.0f);
	CHECK(r0[1] == 0.0f);
	CHECK(r0[2] == 1.0f);
	CHECK(r0[3] == 0.0f);
	CHECK(r1[0] == 0.0f);
	CHECK(r1[1] == 0.5f);
	CHECK(r1[2] == -4.0f);
	CHECK(r1[3] == 0.0f);
}

TEST_CASE("render2 fx: a simple.fx surface is unlit Texture_0 * saturate(ColorEmissive) with the texel alpha, its own shader permutation")
{
	W3DShaderKey k;
	k.Texturing = true;
	k.Tex[0] = true;
	k.PriGradient = W3D_GRADIENT_DISABLE;
	W3DShaderKey s = k;
	s.SimpleFx = true;
	CHECK(s.Id() != k.Id());
	CHECK(s.Id().find("_sfx1") != std::string::npos);
	const std::string code = Generate_W3D_Shader_Code(s);
	CHECK(code.find("cur = vec4(t0.rgb * clamp(w3d_emissive, 0.0, 1.0), t0.a);") != std::string::npos);
	CHECK(code.find("w3d_spec_color * sp") == std::string::npos); // not the normalmapped.fxo lighting
	// the plain unshaded classic surface keeps its own colour rule
	CHECK(Generate_W3D_Shader_Code(k).find("cur = t0;") != std::string::npos);
}
