// OpenBFME unit tests, lane RENDER-1: the light environment the retail effects light objects with (W3DObjectLighting) and the W3D
// shader text that applies it (w3dshadergen.cpp). The retail numbers come from tools/render/map_lighting.py, an independent reader
// (the spec author's map parser in tools/maps/oracle, not the engine); the shader formulas from tools/render/d3d9_disasm.py
// over Shaders.big shaders\compiled\defaultw3d.fxo / normalmapped.fxo.

#include "doctest.h"
#include "RetailTestMount.h"

#include "GameClient/MapChunks.h"
#include "GameClient/MapUtil.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameEngineDevice/W3DDevice/GameClient/W3DObjectLighting.h"
#include "Libraries/WWVegas/WW3D2/meshrender.h"
#include "Libraries/WWVegas/WW3D2/w3dshadergen.h"

#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace
{

void setLight(GlobalLight &l, float a, float d, float px, float py, float pz)
{
	for (int c = 0; c < 3; ++c)
	{
		l.ambient[c] = a;
		l.diffuse[c] = d;
	}
	l.lightPos[0] = px;
	l.lightPos[1] = py;
	l.lightPos[2] = pz;
}

bool has(const std::string &text, const std::string &needle)
{
	return text.find(needle) != std::string::npos;
}

} // namespace

TEST_CASE("render lighting: the effect inputs double the map's colours when the multiplier exceeds 1 (RW 0x4AC97F, 0x54CF3B)")
{
	GlobalLightingData g;
	g.version = 8;
	g.timeOfDay = 2;
	g.terrainLightingMultiplier = 2.0f;
	setLight(g.tod[1].objects[0], 0.25f, 0.5f, 0.1f, 0.2f, -0.9f);
	setLight(g.tod[1].objects[1], 1.5f, 0.3f, -1.0f, 0.0f, 0.0f); // ambient of lights 1 / 2 is not the scene ambient (ZH)
	setLight(g.tod[1].objects[2], 0.0f, 0.04f, 0.0f, 1.0f, 0.0f); // below 0.05 in every channel: dropped (ZH Add_Light)
	setLight(g.tod[1].infantry[0], 0.6f, 0.7f, 0.0f, 0.0f, -1.0f);
	W3DObjectLighting out;
	std::string err;
	REQUIRE_MESSAGE(W3DObjectLightingUtil::fromMap(g, out, &err), err);
	CHECK(out.timeOfDay == 2);
	CHECK(out.colorScale == 2.0f);
	CHECK(out.objects.count == 2);
	CHECK(out.objects.ambient[0] == 0.5f);
	CHECK(out.objects.color[0][1] == 1.0f);
	CHECK(out.objects.color[1][2] == 0.6f);
	CHECK(out.objects.color[2][0] == 0.0f); // an unused slot is black
	CHECK(out.objects.toLight[0][0] == -0.1f); // toward the light = -lightPos
	CHECK(out.objects.toLight[0][1] == -0.2f);
	CHECK(out.objects.toLight[0][2] == 0.9f);
	CHECK(out.objects.toLight[1][0] == 1.0f);
	CHECK(out.infantry.count == 1);
	CHECK(out.infantry.ambient[2] == 1.2f);
	CHECK(out.infantry.color[0][0] == 1.4f);

	// multiplier 1 (5 of the 181 retail maps): no doubling; the ambient is clamped to [0, 1] before the scale
	g.terrainLightingMultiplier = 1.0f;
	setLight(g.tod[1].objects[0], 1.5f, 0.5f, 0.0f, 0.0f, -1.0f);
	REQUIRE(W3DObjectLightingUtil::fromMap(g, out, &err));
	CHECK(out.colorScale == 1.0f);
	CHECK(out.objects.ambient[1] == 1.0f);
	CHECK(out.objects.color[0][0] == 0.5f);

	// a time of day outside 1..4 is an error, not a clamp
	g.timeOfDay = 0;
	CHECK_FALSE(W3DObjectLightingUtil::fromMap(g, out, &err));
	CHECK(err.find("time of day 0") != std::string::npos);
	CHECK(W3DObjectLightingUtil::stopLines().front().rfind("[S-390]", 0) == 0);
}

TEST_CASE("render lighting: retail maps against tools/render/map_lighting.py")
{
	retailtest::Mount *mount = retailtest::pureMount();
	if (!mount)
	{
		retailtest::printSkip("render lighting retail maps");
		return;
	}
	REQUIRE_MESSAGE(mount->fs != nullptr, mount->error);
	struct Expect
	{
		const char *path;
		int tod;
		float ambient[3];
		float infantryAmbient[3];
		int lights;
		float color0[3];
		float toward0[3];
	};
	// printed by `python3 tools/render/map_lighting.py "map mp fall back 4p" "map mp amon sul fortress"` (9 significant digits)
	const Expect cases[] = {
		{ "maps\\map mp fall back 4p\\map mp fall back 4p.map", 2, { 0.101960786f, 0.101960786f, 0.101960786f }, { 0.101960786f, 0.101960786f, 0.101960786f },
			2, { 1.37254906f, 1.17647064f, 1.0196079f }, { -0.364847869f, -0.686179101f, 0.629320502f } },
		{ "maps\\map mp amon sul fortress\\map mp amon sul fortress.map", 3, { 0.180392161f, 0.141176477f, 0.0862745121f },
			{ 0.580392182f, 0.611764729f, 0.580392182f }, 3, { 1.67058825f, 1.10588241f, 0.745098054f }, { -0.673028111f, -0.545007408f, 0.50000006f } },
	};
	for (const Expect &e : cases)
	{
		CAPTURE(e.path);
		std::vector<std::uint8_t> bytes;
		std::string err;
		REQUIRE_MESSAGE(mount->fs->readFile(e.path, bytes, &err), err);
		LoadedMap m;
		REQUIRE_MESSAGE(MapReader::load(bytes, e.path, MapReadOptions(), m, &err), err);
		REQUIRE(m.chunks.hasGlobalLighting);
		W3DObjectLighting out;
		REQUIRE_MESSAGE(W3DObjectLightingUtil::fromMap(m.chunks.lighting, out, &err), err);
		CHECK(out.timeOfDay == e.tod);
		CHECK(out.colorScale == 2.0f);
		CHECK(out.objects.count == e.lights);
		for (int c = 0; c < 3; ++c)
		{
			CHECK(out.objects.ambient[c] == doctest::Approx(e.ambient[c]).epsilon(1e-6));
			CHECK(out.infantry.ambient[c] == doctest::Approx(e.infantryAmbient[c]).epsilon(1e-6));
			CHECK(out.objects.color[0][c] == doctest::Approx(e.color0[c]).epsilon(1e-6));
			CHECK(out.objects.toLight[0][c] == doctest::Approx(e.toward0[c]).epsilon(1e-6));
		}
	}
}

TEST_CASE("render lighting: classic W3D surfaces are lit like defaultw3d.fxo, in gamma space")
{
	W3DShaderKey k;
	k.Texturing = true;
	k.Tex[0] = true;
	k.PriGradient = W3D_GRADIENT_MODULATE;
	k.HasDCG = true;
	const std::string code = Generate_W3D_Shader_Code(k);
	CHECK(has(code, "ambient_light_disabled"));
	CHECK(has(code, "instance uniform bool w3d_receive_shadows : instance_index(1) = false;"));
	CHECK_FALSE(has(code, "source_color")); // the retail arithmetic runs on the stored texel values
	CHECK(has(code, "global uniform vec3 w3d_amb_obj;"));
	CHECK(has(code, "global uniform mat3 w3d_lcol_inf;"));
	CHECK(has(code, "int base = px < 0.0 ? int(-px + 0.5) - 1 : int(px + 0.5);"));
	CHECK(has(code, "vec4 vc = COLOR;"));
	CHECK(has(code, "v_lit0 = clamp(vec4((dsum * w3d_diffuse.rgb + amb * w3d_ambient_mat + w3d_emissive) * vc.rgb * 0.5, vc.a * w3d_diffuse.a), 0.0, 1.0);"));
	CHECK(has(code, "v_lit1 = clamp(ssum * w3d_spec_color * 0.5, 0.0, 1.0);"));
	CHECK(has(code, "cur = vec4(t0.rgb * diff.rgb * 2.0, t0.a * diff.a);"));
	CHECK(has(code, "ALBEDO = w3d_to_linear(clamp(cur.rgb, 0.0, 1.0));"));
	CHECK_FALSE(has(code, "ROUGHNESS"));

	// an unlit surface (primary gradient disabled) keeps the texel
	W3DShaderKey u = k;
	u.PriGradient = W3D_GRADIENT_DISABLE;
	u.Unshaded = true;
	const std::string unlit = Generate_W3D_Shader_Code(u);
	CHECK(has(unlit, "\tcur = t0;"));
	CHECK(has(unlit, "ALBEDO = w3d_to_linear(clamp(cur.rgb, 0.0, 1.0));"));
}

TEST_CASE("render lighting: FX surfaces are lit like normalmapped.fxo (frame -binormal, -tangent, normal)")
{
	W3DShaderKey k;
	k.Fx = true;
	k.Texturing = true;
	k.Tex[0] = true;
	k.NormalMap = true;
	k.Tangents = true;
	k.PriGradient = W3D_GRADIENT_MODULATE;
	const std::string code = Generate_W3D_Shader_Code(k);
	CHECK(has(code, "nt.xy *= w3d_bump;"));
	CHECK(has(code, "vec3 fx = -normalize(mat3(MODEL_MATRIX) * w3d_to_godot(bn));"));
	CHECK(has(code, "vec3 fy = -normalize(mat3(MODEL_MATRIX) * w3d_to_godot(tg));"));
	// blob 22 computes L_t and the normalized H_t per vertex; the pixel shader uses the interpolated values as they are
	CHECK(has(code, "v_lt0 = vec3(dot(ld[0], fx), dot(ld[0], fy), dot(ld[0], wn));"));
	CHECK(has(code, "v_ht2 = normalize(vec3(dot(vv + ld[2], fx), dot(vv + ld[2], fy), dot(vv + ld[2], wn)));"));
	CHECK(has(code, "float ndl = dot(nt, lts[i]);"));
	CHECK(has(code, "float ndh = dot(nt, hts[i]);"));
	CHECK_FALSE(has(code, "normalize(v_ht"));
	CHECK_FALSE(has(code, "normalize(hts"));
	CHECK(has(code, "vec3 col = t0.rgb * clamp(amb * w3d_ambient_mat * 0.5, 0.0, 1.0) * 2.0;"));
	CHECK(has(code, "col += lc[i] * (base * ndl * on + w3d_spec_color * sp);"));
	CHECK(has(code, "float sp = (ndh > 0.0 && ndl > 0.0) ? pow(ndh, w3d_shininess) : 0.0;"));
	CHECK(has(code, "cur = vec4(col, t0.a);"));
	CHECK_FALSE(has(code, "hint_normal"));
	CHECK_FALSE(has(code, "v_lit0"));
}

// The sign chain of the normal-map frame from the W3D file to the pixel (stop S-023). Retail (FXShaderGeometry.cpp:557-558, normalmapped.fxo vertex
// shader blob 22): the file's tangent T and bitangent B go raw into the vertex buffer; oT1.xyz = (L . -nrm(World B), L . -nrm(World T), L . nrm(World N)).
// The port: the instancer gives Godot TANGENT = (T, Godot_Tangent_Sign), Godot's BINORMAL = cross(N, T.xyz) * w (godot/tests/tangent_convention_test.gd
// measures it on the running Godot) and the generated vertex shader takes fx = -BINORMAL, fy = -TANGENT. Identity world here (a rotation keeps every
// dot product).
namespace
{
using V3 = std::array<float, 3>;
V3 cross3(const V3 &a, const V3 &b)
{
	return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
}
V3 unit3(const V3 &a)
{
	const float l = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
	return { a[0] / l, a[1] / l, a[2] / l };
}
float dot3(const V3 &a, const V3 &b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
V3 neg3(const V3 &a)
{
	return { -a[0], -a[1], -a[2] };
}
// L_t of oT1 for the light L (toward the light) in the frame (fx, fy, n)
V3 lightInFrame(const V3 &l, const V3 &fx, const V3 &fy, const V3 &n)
{
	return { dot3(l, fx), dot3(l, fy), dot3(l, n) };
}
} // namespace

TEST_CASE("render lighting: the normal-map frame of the port is retail's (-bitangent, -tangent, normal) for either handedness of the file")
{
	struct Vertex
	{
		V3 n, t, b;
		float wExpected;
	};
	const Vertex vertices[] = {
		{ { 0, 0, 1 }, { -1, 0, 0 }, { 0, -1, 0 }, +1.0f }, // the file's bitangent along cross(N, T)
		{ { 0, 0, 1 }, { 1, 0, 0 }, { 0, -1, 0 }, -1.0f },  // a mirrored UV island: against cross(N, T)
		{ { 0, 1, 0 }, { 0, 0, -1 }, { -1, 0, 0 }, +1.0f },
		{ { 0, 1, 0 }, { 0, 0, -1 }, { 1, 0, 0 }, -1.0f },
	};
	const V3 light = unit3({ 0.2f, 0.6f, 0.77f });
	// a normal-map texel (2 * rgb - 1, normalized) tilted towards -x of the frame
	const V3 texel = unit3({ -0.71f, -0.059f, 0.702f });
	for (const Vertex &v : vertices)
	{
		CAPTURE(v.wExpected);
		const float w = Godot_Tangent_Sign(v.n.data(), v.t.data(), v.b.data());
		CHECK(w == v.wExpected);
		// Godot (measured): BINORMAL = cross(N, T) * w, normalized
		V3 binormal = unit3(cross3(v.n, v.t));
		for (float &c : binormal)
		{
			c *= w;
		}
		// the generated shader: fx = -BINORMAL, fy = -TANGENT, fz = N
		const V3 portFx = neg3(binormal);
		// retail's blob 22: x = -nrm(B), y = -nrm(T), z = nrm(N)
		const V3 retailFx = neg3(unit3(v.b));
		CHECK(portFx == retailFx);
		const V3 lp = lightInFrame(light, portFx, neg3(v.t), v.n);
		const V3 lr = lightInFrame(light, retailFx, neg3(v.t), v.n);
		CHECK(lp == lr);
		// the mirrored frame (the sign the port had before: BINORMAL = -B) changes N.L of a tilted texel
		const V3 lm = lightInFrame(light, neg3(portFx), neg3(v.t), v.n);
		CHECK(std::fabs(dot3(texel, lm) - dot3(texel, lr)) > 0.2f); // tolerance: a margin, the mirror moves N.L by 2 * 0.71 * L.x of the frame (0.28 to 0.86 here)
	}
	// B is not exactly cross(N, T) (a texture seam's skew): the sign follows the side of cross(N, T) it lies on
	const V3 n{ 0, 1, 0 }, t{ 0, 0, -1 }, skewed{ -0.96f, 0, 0.281f }, skewedMirror{ 0.96f, 0, 0.281f };
	CHECK(Godot_Tangent_Sign(n.data(), t.data(), skewed.data()) == +1.0f);
	CHECK(Godot_Tangent_Sign(n.data(), t.data(), skewedMirror.data()) == -1.0f);
	// the numbers of the first vertex: retail L_t = (0.6, 0.2, 0.77) / |L| gives N.L 0.10309 for the tilted texel (the mirrored frame: 0.95796)
	const V3 lt = lightInFrame(light, neg3(vertices[0].b), neg3(vertices[0].t), vertices[0].n);
	CHECK(dot3(texel, lt) == doctest::Approx(0.103087f)); // tolerance: float rounding of a three-term dot product (the default relative epsilon)
}

TEST_CASE("render lighting: the generated FX vertex shader takes the file's bitangent from Godot's BINORMAL and negates it")
{
	W3DShaderKey k;
	k.Fx = true;
	k.Texturing = true;
	k.Tex[0] = true;
	k.NormalMap = true;
	k.Tangents = true;
	k.PriGradient = W3D_GRADIENT_MODULATE;
	const std::string code = Generate_W3D_Shader_Code(k);
	CHECK(has(code, "vec3 tg = w3d_dir(base, b, TANGENT);"));
	CHECK(has(code, "vec3 bn = w3d_dir(base, b, BINORMAL);"));
	CHECK(has(code, "vec3 fx = -normalize(mat3(MODEL_MATRIX) * w3d_to_godot(bn));"));
	CHECK(has(code, "vec3 fy = -normalize(mat3(MODEL_MATRIX) * w3d_to_godot(tg));"));
}

TEST_CASE("render lighting: the normalmapped specular uses the interpolated half vector unrenormalized (review r1)")
{
	// a flat edge: the texel normal (0, 0, 1), the two vertices' unit half vectors 22.5 degrees either side of it, light along the
	// normal. Midway the interpolated H_t is (0, 0, cos 22.5) (length < 1): retail's pow(0.92388, 50) * SpecularColor 0.8 = 0.01527;
	// a per-pixel renormalized H would give the full 0.8
	const float pi = 3.14159265358979f;
	const float a = 22.5f * pi / 180.0f;
	const float nt[3] = { 0.0f, 0.0f, 1.0f };
	const float lt[3] = { 0.0f, 0.0f, 1.0f };
	const float h0[3] = { std::sin(a), 0.0f, std::cos(a) }, h1[3] = { -std::sin(a), 0.0f, std::cos(a) };
	const float mid[3] = { (h0[0] + h1[0]) * 0.5f, (h0[1] + h1[1]) * 0.5f, (h0[2] + h1[2]) * 0.5f };
	CHECK(W3D_NormalMapped_Specular(nt, lt, mid, 50.0f) * 0.8f == doctest::Approx(0.01527f).epsilon(0.002));
	const float unit[3] = { 0.0f, 0.0f, 1.0f };
	CHECK(W3D_NormalMapped_Specular(nt, lt, unit, 50.0f) * 0.8f == doctest::Approx(0.8f));
	// back facing light or half vector: no highlight
	const float away[3] = { 0.0f, 0.0f, -1.0f };
	CHECK(W3D_NormalMapped_Specular(nt, away, unit, 50.0f) == 0.0f);
	CHECK(W3D_NormalMapped_Specular(nt, lt, away, 50.0f) == 0.0f);
}

TEST_CASE("render lighting: the classic lit power is clamped to +-127.9961 like D3D9 lit (review r1)")
{
	CHECK(W3D_LIT_POWER_LIMIT == 127.9961f);
	const float clamped = W3D_Lit_Specular(0.5f, 0.99f, 127.9961f);
	CHECK(W3D_Lit_Specular(0.5f, 0.99f, 200.0f) == clamped);
	CHECK(W3D_Lit_Specular(0.5f, 0.99f, 200.0f) != doctest::Approx(std::pow(0.99f, 200.0f)));
	CHECK(W3D_Lit_Specular(0.5f, 0.99f, -500.0f) == W3D_Lit_Specular(0.5f, 0.99f, -127.9961f));
	CHECK(W3D_Lit_Specular(0.5f, 0.99f, 50.0f) == doctest::Approx(std::pow(0.99f, 50.0f)));
	CHECK(W3D_Lit_Specular(-0.1f, 0.99f, 50.0f) == 0.0f); // N.L <= 0: no specular
	W3DShaderKey k;
	k.Texturing = true;
	k.Tex[0] = true;
	k.PriGradient = W3D_GRADIENT_MODULATE;
	const std::string code = Generate_W3D_Shader_Code(k);
	CHECK(has(code, "pow(ndh, clamp(w3d_shininess, -127.996101, 127.996101))"));
}

TEST_CASE("render lighting: terrain SAS sun scale respects the overbright version gate")
{
	GlobalLightingData g;
	g.version = 8;
	g.terrainLightingMultiplier = 2.0f;
	CHECK(W3DObjectLightingUtil::effectColorScale(g) == 2.0f);
	g.terrainLightingMultiplier = 1.0f;
	CHECK(W3DObjectLightingUtil::effectColorScale(g) == 1.0f);
	g.terrainLightingMultiplier = 1.5f;
	CHECK(W3DObjectLightingUtil::effectColorScale(g) == 2.0f);
	g.version = 4;
	CHECK(W3DObjectLightingUtil::effectColorScale(g) == 1.0f);
}
