// OpenBFME unit tests. GPL-3.0.
//
// Tests for tree and shrub foliage lighting (lane LOCAL-49):
//   * RW 0x4E6753 (W3DVegetationBufferBase::allocateBuffers), RW 0x4E699E (W3DShrubBuffer), RW 0x4E9BD5 (W3DTreeBuffer),
//     DONOR BFME2 Rva000E6D94AllocateBuffers.cpp: trees and shrubs draw with Vegetation.fx ("Default" technique).
//   * RotWK Shaders.big vegetation.fxo vs_1_1 blobs 6/7: the three directional lights use abs(dot(N, L)) rather than
//     max(dot(N, L), 0.0), lighting both sides of foliage leaves; no specular, and the VertexMaterial ColorDiffuse /
//     ColorAmbient / ColorEmissive are ignored (oD0.rgb = (dsum + amb) * vc.rgb * 0.5, oD0.a = vc.a).

#include "doctest.h"

#include "Common/ShaderVaryings.h"
#include "Libraries/WWVegas/WW3D2/w3dshadergen.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <string>

namespace
{

bool hasSubstr(const std::string &text, const std::string &needle)
{
	return text.find(needle) != std::string::npos;
}

std::string readGodotShaderFile(const std::string &rel)
{
	std::ifstream file(std::string(OPENBFME_DOCS_DIR) + "/../godot/" + rel, std::ios::binary);
	if (!file.good())
	{
		return std::string();
	}
	return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

ShaderVaryings::Result countW3DVaryings(const W3DShaderKey &key)
{
	const std::string code = Generate_W3D_Shader_Code(key, W3DSamplerPolicy::Anisotropic);
	return ShaderVaryings::Count(code, "shaders/w3d.gdshader", [](const std::string &rel, std::string &out) {
		out = readGodotShaderFile(rel);
		return !out.empty();
	});
}

float dot3(float ax, float ay, float az, float bx, float by, float bz)
{
	return ax * bx + ay * by + az * bz;
}

// Reference evaluation of one directional light plus ambient: vegetation.fxo blob 6 (abs(N.L)) against defaultw3d.fxo blob 151 (max(N.L, 0.0))
float evaluateFoliageDiffuse(float nx, float ny, float nz, float sunDirX, float sunDirY, float sunDirZ, float sunCol, float ambient, bool twoSidedAbs)
{
	const float ndl = dot3(nx, ny, nz, sunDirX, sunDirY, sunDirZ);
	const float sunTerm = sunCol * (twoSidedAbs ? std::abs(ndl) : std::max(ndl, 0.0f));
	return std::min(1.0f, std::max(0.0f, (sunTerm + ambient) * 0.5f));
}

} // namespace

TEST_CASE("LOCAL-49 shader: W3DShaderKey::Vegetation uses abs(N.L) for the directional lights, omits specular, and keeps the varying budget")
{
	W3DShaderKey veg;
	CHECK_FALSE(veg.Vegetation); // default is non-vegetation (classic / FX objects)
	veg.Texturing = true;
	veg.Tex[0] = true;
	veg.AlphaTest = true;
	veg.Vegetation = true;

	W3DShaderKey prop;
	prop.Texturing = true;
	prop.Tex[0] = true;
	prop.AlphaTest = true;
	prop.Vegetation = false;

	CHECK(veg.Id() != prop.Id()); // the generated shader gets its own cache entry
	CHECK(hasSubstr(veg.Id(), "_veg"));
	CHECK_FALSE(hasSubstr(prop.Id(), "_veg"));

	const std::string vegCode = Generate_W3D_Shader_Code(veg);
	const std::string propCode = Generate_W3D_Shader_Code(prop);

	// 1. Directional lights 0..2: vegetation.fxo blobs 6/7 use abs(ndl); defaultw3d keeps max(ndl, 0.0)
	CHECK(hasSubstr(vegCode, "dsum += lc[i] * abs(ndl);"));
	CHECK_FALSE(hasSubstr(vegCode, "dsum += lc[i] * max(ndl, 0.0);"));
	CHECK(hasSubstr(propCode, "dsum += lc[i] * max(ndl, 0.0);"));
	CHECK_FALSE(hasSubstr(propCode, "abs(ndl)"));

	// 2. No specular and ignores the VertexMaterial diffuse / ambient / emissive factors, while keeping the vertex colour VC / DCG
	CHECK(hasSubstr(vegCode, "v_lit0 = clamp(vec4((dsum + amb) * vc.rgb * 0.5, vc.a), 0.0, 1.0);"));
	CHECK(hasSubstr(vegCode, "v_lit1 = vec3(0.0);"));
	CHECK(hasSubstr(propCode, "v_lit1 = clamp(ssum * w3d_spec_color * 0.5, 0.0, 1.0);"));

	W3DShaderKey vegDcg = veg;
	vegDcg.HasDCG = true;
	const std::string vegDcgCode = Generate_W3D_Shader_Code(vegDcg);
	CHECK(hasSubstr(vegDcgCode, "vec4 vc = COLOR;"));
	CHECK(hasSubstr(vegDcgCode, "v_lit0 = clamp(vec4((dsum + amb) * vc.rgb * 0.5, vc.a), 0.0, 1.0);"));

	// 3. The varying budget does not change
	const ShaderVaryings::Result varyings = countW3DVaryings(veg);
	const ShaderVaryings::Result propVaryings = countW3DVaryings(prop);
	REQUIRE(varyings.ok);
	REQUIRE(propVaryings.ok);
	CHECK(varyings.used == propVaryings.used);
}

TEST_CASE("LOCAL-49 lighting parity: back-facing foliage matches front-facing foliage under a directional light")
{
	// Unit sun (0.6, 0, 0.8), ambient 0.25
	const float vegFront = evaluateFoliageDiffuse(0.0f, 0.0f, 1.0f, 0.6f, 0.0f, 0.8f, 1.0f, 0.25f, true);
	const float vegBack = evaluateFoliageDiffuse(0.0f, 0.0f, -1.0f, 0.6f, 0.0f, 0.8f, 1.0f, 0.25f, true);
	CHECK(vegFront == 0.525f);
	CHECK(vegBack == vegFront);

	// Contrast: single-sided defaultw3d drops the directional diffuse when N.L < 0, leaving only ambient * 0.5
	const float oneSideFront = evaluateFoliageDiffuse(0.0f, 0.0f, 1.0f, 0.6f, 0.0f, 0.8f, 1.0f, 0.25f, false);
	const float oneSideBack = evaluateFoliageDiffuse(0.0f, 0.0f, -1.0f, 0.6f, 0.0f, 0.8f, 1.0f, 0.25f, false);
	CHECK(oneSideFront == 0.525f);
	CHECK(oneSideBack == 0.125f);
}
