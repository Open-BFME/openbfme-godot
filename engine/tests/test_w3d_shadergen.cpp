// OpenBFME unit tests: the generated Godot shader text (w3dshadergen.cpp). Review finding P2: v_wnrm was the model-space normal while
// v_wview is a world-space direction, so a view-dependent environment mapper gave different results for a rotated instance.
// Godot's NORMAL in vertex() is model-space; the instance rotation lives in MODEL_MATRIX (the MultiMesh transform is folded in), so the
// world normal is MODEL_NORMAL_MATRIX * NORMAL. Expected strings are the GLSL the spec fixes; nothing is read back from a run.

#include "doctest.h"

#include "Common/ShaderVaryings.h"
#include "Libraries/WWVegas/WW3D2/w3dshadergen.h"

#include <algorithm>
#include <functional>
#include <iterator>
#include <string>
#include <map>
#include <set>
#include <vector>
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
			// lane INGAMEBG-1: x also carries the instance's fogless scene (+ 2, W3D_NO_SCENE_FOG_BIT), in the same slot
			CHECK(has(code, "v_instance.x = (px < 0.0 ? 1.0 : 0.0) + ((wi & " + std::to_string(W3D_NO_SCENE_FOG_BIT) + ") != 0 ? 2.0 : 0.0);"));
			CHECK(has(code, "v_instance.y = (wi & 16384) != 0 ? w3d_fogged_frac : 1.0;"));
			CHECK(has(code, "v_instance.z = INSTANCE_CUSTOM.z;") == house);
		}
	}
}

TEST_CASE("shader text: the normal-mapped building permutation fits Intel Arc's 29 Forward+ locations")
{
	// Godot reported "Too many varyings used in shader (30 used, maximum supported is 29)" on an Intel Arc A770 (Mesa ANV, Vulkan 1.4)
	// for the selection-decal permutation; this building permutation declared 14 and sat at 29 of Intel's 29 (Forward+ itself takes
	// the other 15). The smooth varyings are packed into vec4s, so it now declares 12 and uses 27, and the worst permutation uses 28.
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
	CHECK(varyings == 12); // includes the fragment-to-light shadow colour; v_fade rides in w of the packed view position
	// the two UV sets share one interpolator, and the world position is a vertex() local
	CHECK(has(code, "varying vec4 v_uv;"));
	CHECK(has(code, "v_uv = vec4(UV, UV2);"));
	CHECK_FALSE(has(code, "varying vec3 v_wpos;"));
	CHECK(has(code, "vec3 wpos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;"));
}

// -------------------------------------------------------------------------------------------------------------------------------
// The varying budget: Godot 4.7.2 Forward+ spatial takes 15 of Intel's 29 locations, the generator declares 6 more (the smooth
// v_fade rides in w of the packed view position) and either 2 classic or 5 normal-mapped (Fx) varyings, plus object_shadow's 1 when
// the surface is a shadow receiver and its extra 1 when the wall selection decals draw over that shadow (SELWALL-1). The varying
// declarations are emitted only for k.Fx and shadowReceiver (!OcclusionPass && Blend == OPAQUE && !AlphaOverride): that cross
// product is enumerated exactly, and every other key field is swept through every value in each context so a field that starts
// emitting a varying turns the test red.

namespace
{
std::string w3dProjectFile(const std::string &rel)
{
	std::ifstream file(std::string(OPENBFME_DOCS_DIR) + "/../godot/" + rel, std::ios::binary);
	if (!file.good())
	{
		return std::string();
	}
	return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

ShaderVaryings::Result countW3D(const W3DShaderKey &key)
{
	const std::string code = Generate_W3D_Shader_Code(key, W3DSamplerPolicy::Anisotropic);
	return ShaderVaryings::Count(code, "shaders/w3d.gdshader", [](const std::string &rel, std::string &out) {
		out = w3dProjectFile(rel);
		return !out.empty();
	});
}

std::vector<std::string> varyingNames(const ShaderVaryings::Result &result)
{
	std::vector<std::string> names;
	for (const ShaderVaryings::Varying &varying : result.varyings)
	{
		names.push_back(varying.name);
	}
	std::sort(names.begin(), names.end());
	return names;
}

std::vector<std::string> expectedW3DVaryings(bool fx, bool shadow, bool selectionDecals)
{
	std::vector<std::string> names = { "v_instance", "v_pack_pos", "v_uv", "v_vnrm", "v_wnrm", "v_wview" };
	if (fx)
	{
		names.push_back("v_light0");
		names.push_back("v_light1");
		names.push_back("v_light2");
		names.push_back("v_light3");
		names.push_back("v_light4");
	}
	else
	{
		names.push_back("v_lit0");
		names.push_back("v_lit1");
	}
	if (shadow)
	{
		names.push_back("v_shadow_color");
	}
	if (selectionDecals && shadow)
	{
		names.push_back("v_shadow_over");
	}
	std::sort(names.begin(), names.end());
	return names;
}

bool shadowReceiver(const W3DShaderKey &key)
{
	return key.OcclusionPass == 0 && key.Blend == W3D_GODOT_BLEND_OPAQUE && !key.AlphaOverride;
}
} // namespace

TEST_CASE("shader budget: the Fx, occlusion, blend and alpha-override cross product fits Intel's 29")
{
	int checked = 0;
	int worst = -1;
	W3DShaderKey worstKey;
	for (bool fx : { false, true })
	{
		for (int occlusion : { 0, 1, 2 })
		{
			for (int blend = W3D_GODOT_BLEND_OPAQUE; blend <= W3D_GODOT_BLEND_APPROX_ADD; ++blend)
			{
				for (bool alphaOverride : { false, true })
				{
					W3DShaderKey key;
					key.Fx = fx;
					key.OcclusionPass = occlusion;
					key.Blend = blend;
					key.AlphaOverride = alphaOverride;
					key.Texturing = key.Tex[0] = true;
					key.NormalMap = fx;
					key.Tangents = fx;
					const bool shadow = shadowReceiver(key);
					const int expected = 15 + 6 + (fx ? 5 : 2) + (shadow ? 1 : 0);
					const ShaderVaryings::Result result = countW3D(key);
					CAPTURE(fx);
					CAPTURE(occlusion);
					CAPTURE(blend);
					CAPTURE(alphaOverride);
					REQUIRE(result.ok);
					CHECK(result.used == expected);
					CHECK(varyingNames(result) == expectedW3DVaryings(fx, shadow, false));
					if (result.used > worst)
					{
						worst = result.used;
						worstKey = key;
					}
					++checked;
				}
			}
		}
	}
	CHECK(checked == 2 * 3 * 8 * 2);
	CHECK(worst == 27); // Fx + shadow receiver; every count above is exact, so this is the cross product's largest
	std::fprintf(stderr, "w3d shader budget: %d varying-bearing variants, the most is %d/%d (Fx %d, occlusion %d, blend %d, alpha override %d)\n",
		checked, worst, ShaderVaryings::kMaxVaryings, worstKey.Fx, worstKey.OcclusionPass, worstKey.Blend, worstKey.AlphaOverride);
}

TEST_CASE("shader budget: no other W3D key field changes the varying set")
{
	struct Field
	{
		const char *name;
		int values;
		std::function<void(W3DShaderKey &, int)> set;
	};
	std::vector<Field> fields = {
		{ "Skin", 2, [](W3DShaderKey &k, int v) { k.Skin = v != 0; } },
		{ "Dual", 2, [](W3DShaderKey &k, int v) { k.Dual = v != 0; } },
		{ "Camera", 3, [](W3DShaderKey &k, int v) { k.Camera = v; } },
		{ "CullDisabled", 2, [](W3DShaderKey &k, int v) { k.CullDisabled = v != 0; } },
		{ "DepthWrite", 2, [](W3DShaderKey &k, int v) { k.DepthWrite = v != 0; } },
		{ "DepthTestDisabled", 2, [](W3DShaderKey &k, int v) { k.DepthTestDisabled = v != 0; } },
		{ "AlphaTest", 2, [](W3DShaderKey &k, int v) { k.AlphaTest = v != 0; } },
		{ "AlphaTestLessEqual", 2, [](W3DShaderKey &k, int v) { k.AlphaTestLessEqual = v != 0; } },
		{ "Unshaded", 2, [](W3DShaderKey &k, int v) { k.Unshaded = v != 0; } },
		{ "Texturing", 2, [](W3DShaderKey &k, int v) { k.Texturing = v != 0; } },
		{ "PriGradient", 6, [](W3DShaderKey &k, int v) { k.PriGradient = v; } },
		{ "DetailColor", 14, [](W3DShaderKey &k, int v) { k.DetailColor = v; } },
		{ "DetailAlpha", 4, [](W3DShaderKey &k, int v) { k.DetailAlpha = v; } },
		{ "HasDCG", 2, [](W3DShaderKey &k, int v) { k.HasDCG = v != 0; } },
		{ "SimpleFx", 2, [](W3DShaderKey &k, int v) { k.SimpleFx = v != 0; } },
		{ "NormalMap", 2, [](W3DShaderKey &k, int v) { k.NormalMap = v != 0; } },
		{ "ConvertedFx", 2, [](W3DShaderKey &k, int v) { k.ConvertedFx = v != 0; } },
		{ "SecGradient", 2, [](W3DShaderKey &k, int v) { k.SecGradient = v; } },
		{ "Tex0", 2, [](W3DShaderKey &k, int v) { k.Tex[0] = v != 0; } },
		{ "Tex1", 2, [](W3DShaderKey &k, int v) { k.Tex[1] = v != 0; } },
		{ "Uv2", 2, [](W3DShaderKey &k, int v) { k.Uv2 = v != 0; } },
		{ "TexGen0", 4, [](W3DShaderKey &k, int v) { k.TexGen[0] = v; } },
		{ "TexGen1", 4, [](W3DShaderKey &k, int v) { k.TexGen[1] = v; } },
		{ "ViewDependent0", 2, [](W3DShaderKey &k, int v) { k.ViewDependent[0] = v != 0; } },
		{ "ViewDependent1", 2, [](W3DShaderKey &k, int v) { k.ViewDependent[1] = v != 0; } },
		{ "Projection0", 2, [](W3DShaderKey &k, int v) { k.Projection[0] = v != 0; } },
		{ "Projection1", 2, [](W3DShaderKey &k, int v) { k.Projection[1] = v != 0; } },
		{ "ClampU0", 2, [](W3DShaderKey &k, int v) { k.ClampU[0] = v != 0; } },
		{ "ClampU1", 2, [](W3DShaderKey &k, int v) { k.ClampU[1] = v != 0; } },
		{ "ClampV0", 2, [](W3DShaderKey &k, int v) { k.ClampV[0] = v != 0; } },
		{ "ClampV1", 2, [](W3DShaderKey &k, int v) { k.ClampV[1] = v != 0; } },
		{ "NoLod0", 2, [](W3DShaderKey &k, int v) { k.NoLod[0] = v != 0; } },
		{ "NoLod1", 2, [](W3DShaderKey &k, int v) { k.NoLod[1] = v != 0; } },
		{ "Tangents", 2, [](W3DShaderKey &k, int v) { k.Tangents = v != 0; } },
		{ "HouseColor", 2, [](W3DShaderKey &k, int v) { k.HouseColor = v != 0; } },
		{ "HouseColorBaked", 2, [](W3DShaderKey &k, int v) { k.HouseColorBaked = v != 0; } },
		{ "Fog", 4, [](W3DShaderKey &k, int v) { k.Fog = v; } },
		{ "SelectionDecals", 2, [](W3DShaderKey &k, int v) { k.SelectionDecals = v != 0; } },
	};
	int checked = 0;
	int worst = -1;
	std::string worstField;
	W3DShaderKey worstKey;
	for (bool fx : { false, true })
	{
		for (int occlusion : { 0, 1, 2 })
		{
			for (int blend = W3D_GODOT_BLEND_OPAQUE; blend <= W3D_GODOT_BLEND_APPROX_ADD; ++blend)
			{
				for (bool alphaOverride : { false, true })
				{
					W3DShaderKey base;
					base.Fx = fx;
					base.OcclusionPass = occlusion;
					base.Blend = blend;
					base.AlphaOverride = alphaOverride;
					base.Texturing = base.Tex[0] = base.Tex[1] = true;
					base.NormalMap = fx;
					base.Tangents = fx;
					const bool shadow = shadowReceiver(base);
					for (const Field &field : fields)
					{
						for (int value = 0; value < field.values; ++value)
						{
							W3DShaderKey key = base;
							field.set(key, value);
							const std::vector<std::string> expected = expectedW3DVaryings(fx, shadow, key.SelectionDecals && shadow);
							const ShaderVaryings::Result result = countW3D(key);
							CAPTURE(field.name);
							CAPTURE(value);
							CAPTURE(fx);
							CAPTURE(occlusion);
							CAPTURE(blend);
							CAPTURE(alphaOverride);
							REQUIRE(result.ok);
							CHECK(result.used == 15 + (int)expected.size());
							CHECK(varyingNames(result) == expected);
							if (result.used > worst)
							{
								worst = result.used;
								worstField = std::string(field.name) + " " + std::to_string(value);
								worstKey = key;
							}
							++checked;
						}
					}
				}
			}
		}
	}
	CHECK(checked == 2 * 3 * 8 * 2 * 101);
	CHECK(worst == 28); // the new worst: Fx + opaque + selection decals + shadow receiver (see the next test)
	std::fprintf(stderr, "w3d shader budget: %d single-field variants counted, the most is %d/%d (%s; Fx %d, occlusion %d, blend %d, alpha override %d)\n",
		checked, worst, ShaderVaryings::kMaxVaryings, worstField.c_str(), worstKey.Fx, worstKey.OcclusionPass, worstKey.Blend, worstKey.AlphaOverride);
}

TEST_CASE("shader budget: the Fx selection-decal shadow receiver uses 28 and fits Intel's 29")
{
	// The generator drew both shadow varyings when a normal-mapped (Fx) surface is also a selection-decal receiver
	// (selection_decals.gdshaderinc's v_shadow_over, SELWALL-1). With the packed smooth varyings it is 28 of Intel's 29:
	// 15 Forward+ base + v_uv, v_pack_pos (v_fade in w), v_vnrm, v_wnrm, v_wview, the flat v_instance and the five packed
	// Fx light vec4s + v_shadow_color + v_shadow_over. The runtime guard must not report it. The enumeration above pins the
	// generator's exact sets; this case pins the one permutation that used to be over the limit.
	W3DShaderKey key;
	key.Fx = true;
	key.SelectionDecals = true;
	key.Blend = W3D_GODOT_BLEND_OPAQUE;
	key.Texturing = key.Tex[0] = true;
	key.NormalMap = true;
	key.Tangents = true;
	const std::string code = Generate_W3D_Shader_Code(key, W3DSamplerPolicy::Anisotropic);
	const ShaderVaryings::Result result = ShaderVaryings::Count(code, "shaders/w3d.gdshader", [](const std::string &rel, std::string &out) {
		out = w3dProjectFile(rel);
		return !out.empty();
	});
	REQUIRE(result.ok);
	CHECK(result.used == 28);
	std::vector<std::string> reported;
	const bool fits = ShaderVaryings::Guard("W3D material Fx selection decals", code, "shaders/w3d.gdshader",
		[](const std::string &rel, std::string &out) {
			out = w3dProjectFile(rel);
			return !out.empty();
		},
		[&reported](const std::string &message) { reported.push_back(message); });
	CHECK(fits);
	CHECK(reported.empty());
}
