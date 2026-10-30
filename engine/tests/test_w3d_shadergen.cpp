// OpenBFME unit tests: the generated Godot shader text (w3dshadergen.cpp). Review finding P2: v_wnrm was the model-space normal while
// v_wview is a world-space direction, so a view-dependent environment mapper gave different results for a rotated instance.
// Godot's NORMAL in vertex() is model-space; the instance rotation lives in MODEL_MATRIX (the MultiMesh transform is folded in), so the
// world normal is MODEL_NORMAL_MATRIX * NORMAL. Expected strings are the GLSL the spec fixes; nothing is read back from a run.

#include "doctest.h"

#include "Libraries/WWVegas/WW3D2/w3dshadergen.h"

#include <string>
#include <map>
#include <set>
#include <sstream>
#include <fstream>

namespace
{
W3DShaderKey envKey(bool viewDependent, W3DTexGen gen)
{
	W3DShaderKey k;
	k.Texturing = true;
	k.Tex[0] = true;
	k.TexGen[0] = gen;
	k.ViewDependent[0] = viewDependent;
	return k;
}

bool has(const std::string &text, const std::string &needle)
{
	return text.find(needle) != std::string::npos;
}
} // namespace

TEST_CASE("shader text: the world-space normal varying includes the instance rotation (MODEL_NORMAL_MATRIX)")
{
	const std::string code = Generate_W3D_Shader_Code(envKey(true, W3D_TEXGEN_CAMERA_REFLECTION));
	CHECK(has(code, "v_wnrm = normalize(MODEL_NORMAL_MATRIX * NORMAL);"));
	CHECK_FALSE(has(code, "v_wnrm = NORMAL;"));
	// v_wview is already world space: the view vector is rotated by the inverse view matrix
	CHECK(has(code, "v_wview = mat3(INV_VIEW_MATRIX) * normalize(v_vpos);"));
	// and the reflection is taken between the two world-space vectors
	CHECK(has(code, "reflect(v_wview, normalize(v_wnrm))"));
}

TEST_CASE("shader text: the camera-normal mapper's world path reads the same world-space normal")
{
	const std::string code = Generate_W3D_Shader_Code(envKey(true, W3D_TEXGEN_CAMERA_NORMAL));
	CHECK(has(code, "vec4(vec3(v_wnrm.x, -v_wnrm.z, v_wnrm.y), 1.0)"));
	CHECK(has(code, "v_wnrm = normalize(MODEL_NORMAL_MATRIX * NORMAL);"));
}

TEST_CASE("shader text: the view-space mappers keep the model-view normal")
{
	const std::string code = Generate_W3D_Shader_Code(envKey(false, W3D_TEXGEN_CAMERA_REFLECTION));
	CHECK(has(code, "v_vnrm = normalize((MODELVIEW_NORMAL_MATRIX * NORMAL));"));
	CHECK(has(code, "reflect(normalize(v_vpos), normalize(v_vnrm))"));
}

TEST_CASE("shader text: W3D sampler permutations share configurations and Metal never addresses slot 16 or 17")
{
	// Godot 4.7.2 scene_forward_clustered_inc.glsl declares six preceding samplers, then these
	// shared configurations. rendering_shader_container_metal.cpp assigns slots before dead-code removal.
	const std::map<std::string, int> slots = {
		{"filter_nearest, repeat_disable", 6},
		{"filter_linear, repeat_disable", 7},
		{"filter_linear_mipmap, repeat_disable", 9},
		{"filter_linear_mipmap_anisotropic, repeat_disable", 11},
		{"filter_linear, repeat_enable", 13},
		{"filter_linear_mipmap, repeat_enable", 15},
		{"filter_linear_mipmap_anisotropic, repeat_enable", 17},
	};
	for (const auto policy : { W3DSamplerPolicy::Anisotropic, W3DSamplerPolicy::Metal16 })
	{
		for (unsigned bits = 0; bits != 1024; ++bits)
		{
			CAPTURE(bits);
			W3DShaderKey k;
			for (int stage = 0; stage != 2; ++stage)
			{
				k.Tex[stage] = (bits & (1u << (stage * 4))) != 0;
				k.NoLod[stage] = (bits & (2u << (stage * 4))) != 0;
				k.ClampU[stage] = (bits & (4u << (stage * 4))) != 0;
				k.ClampV[stage] = (bits & (8u << (stage * 4))) != 0;
			}
			k.NormalMap = (bits & 256u) != 0;
			k.HouseColor = (bits & 512u) != 0;
			const auto code = Generate_W3D_Shader_Code(k, policy);
			std::set<int> used;
			std::istringstream lines(code);
			std::string line;
			unsigned uniforms = 0;
			while (std::getline(lines, line))
			{
				if (line.find("uniform sampler2D ") != 0) continue;
				++uniforms;
				const auto start = line.find(" : ") + 3;
				const auto hint = line.substr(start, line.find(';', start) - start);
				CAPTURE(hint);
				const auto slot = slots.find(hint);
				REQUIRE(slot != slots.end());
				used.insert(slot->second);
				if (policy == W3DSamplerPolicy::Metal16)
				{
					CHECK(slot->second < 16); // tolerance: Metal's hard per-stage sampler index limit is 0..15
				}
			}
			CHECK(uniforms == unsigned(1 + k.Tex[0] + k.Tex[1] + k.NormalMap + k.HouseColor));
			CHECK(6 + used.size() <= 16); // tolerance: six Godot reserved samplers plus W3D's distinct live configurations fit 16 on every driver
			for (int stage = 0; stage != 2; ++stage)
			{
				if (!k.Tex[stage]) continue;
				const bool clamp = k.ClampU[stage] && k.ClampV[stage];
				const std::string filter = k.NoLod[stage] ? "filter_linear" :
					(policy == W3DSamplerPolicy::Metal16 && !clamp ? "filter_linear_mipmap" : "filter_linear_mipmap_anisotropic");
				CHECK(has(code, "uniform sampler2D w3d_tex" + std::to_string(stage) + " : " + filter + (clamp ? ", repeat_disable;" : ", repeat_enable;")));
			}
			const std::string repeatFilter = policy == W3DSamplerPolicy::Metal16 ? "filter_linear_mipmap" : "filter_linear_mipmap_anisotropic";
			if (k.NormalMap) CHECK(has(code, "uniform sampler2D w3d_normal : " + repeatFilter + ", repeat_enable;"));
			if (k.HouseColor) CHECK(has(code, "uniform sampler2D w3d_hc_tex : " + repeatFilter + ", repeat_enable;"));
			if (policy == W3DSamplerPolicy::Anisotropic) CHECK(code == Generate_W3D_Shader_Code(k));
		}
	}
}

TEST_CASE("shader text: instance lighting and colour share one flat interpolation slot")
{
	for (bool fx : {false, true})
	{
		for (bool house : {false, true})
		{
			W3DShaderKey key;
			key.Fx = fx;
			key.HouseColor = house;
			key.Tex[0] = key.Texturing = true;
			const auto code = Generate_W3D_Shader_Code(key, W3DSamplerPolicy::Metal16);
			std::istringstream lines(code);
			std::string line;
			int flatSlots = 0;
			while (std::getline(lines, line))
			{
				if (line.find("varying flat ") == 0) ++flatSlots;
			}
			// Godot assigns each separately declared flat scalar its own location;
			// the normal-mapped permutation overflowed the Forward+ budget with three.
			CHECK(flatSlots == 1);
			CHECK(has(code, "varying flat vec3 v_instance;"));
			CHECK(has(code, "v_instance.x = px < 0.0 ? 1.0 : 0.0;"));
			CHECK(has(code, "v_instance.y = (wi & 16384) != 0 ? w3d_fogged_frac : 1.0;"));
			CHECK(has(code, "v_instance.z = INSTANCE_CUSTOM.z;") == house);
		}
	}
}

TEST_CASE("shader text: the normal-mapped building permutation fits Intel Arc's 29 Forward+ locations")
{
	// Godot reported "Too many varyings used in shader (30 used, maximum supported is 29)" on an Intel Arc A770 (Mesa ANV, Vulkan 1.4)
	// for this permutation with 16 declared varyings: Forward+ itself takes the other 14 locations. 14 declared varyings leave one spare.
	W3DShaderKey key;
	key.Fx = key.NormalMap = key.Tangents = key.HouseColor = key.Uv2 = true;
	key.Tex[0] = key.Tex[1] = key.Texturing = true;
	const auto code = Generate_W3D_Shader_Code(key, W3DSamplerPolicy::Metal16);
	std::istringstream lines(code);
	std::string line;
	int varyings = 0;
	while (std::getline(lines, line))
	{
		if (line.find("varying ") == 0) ++varyings;
	}
	std::ifstream shadowInclude(std::string(OPENBFME_DOCS_DIR) + "/../godot/shaders/object_shadow.gdshaderinc");
	REQUIRE(shadowInclude.good());
	while (std::getline(shadowInclude, line))
	{
		if (line.find("varying ") == 0) ++varyings;
	}
	CHECK(varyings == 14); // includes the fragment-to-light shadow colour; unused v_col was removed
	// the two UV sets share one interpolator, and the world position is a vertex() local
	CHECK(has(code, "varying vec4 v_uv;"));
	CHECK(has(code, "v_uv = vec4(UV, UV2);"));
	CHECK_FALSE(has(code, "varying vec3 v_wpos;"));
	CHECK(has(code, "vec3 wpos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;"));
}
