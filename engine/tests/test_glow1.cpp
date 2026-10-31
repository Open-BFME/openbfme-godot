// OpenBFME tests, lane GLOW-1 (retail's glow pass and the effects-LOD gates on the post chain, stop S-5231). GPL-3.0.
//
// Expected values are RotWK's, read from the binary independently of the code (tools/render/test_glow_binary_facts.py pins the same facts on game.dat) and
// from the retail archives (tools/fx/bigfs.py):
//  - the GlowEffect field table (RW 0xC1E810), parser (RW 0x70B734) with its load type rule, reset (RW 0x70B5B1), getter (RW 0x70B5CC);
//  - environment.ini: GlowEnabled Yes, GlowDiameter 15, GlowIntensity 1.3, GlowTextureWidth 256, RadiusScale1 0.18, Amplitude1 0.06, RadiusScale2 4.5,
//    Amplitude2 0.11, TerrainGlow No, MultipassGlowEnabled No; maps\shellmap1\map.ini overrides GlowDiameter 20 and GlowIntensity 0.5;
//  - the gaussian kernel (RW 0x477969), evaluated in float32 by a separate script (numpy, the binary's constants 15.0, 0.1, 0.01) for 15 and 20 taps;
//  - the gating order of the view draw (RW 0x487897 .. 0x487A5C).
// The retail tests SKIP loudly without the installs.

#include "doctest.h"

#include "IniTestUtil.h"
#include "StartTestUtil.h"

#include "Common/INI.h"
#include "GameEngineDevice/W3DDevice/GameClient/GlowEffect.h"
#include "GameEngineDevice/W3DDevice/GameClient/ScreenHilightFilter.h"
#include "GameEngineDevice/W3DDevice/GameClient/W3DViewDrawPostPass.h"

#include <array>
#include <cstring>
#include <string>
#include <vector>

namespace
{
const char *const kEnvironmentBlock =
	"GlowEffect\n"
	"GlowEnabled = Yes\n"
	"GlowDiameter = 15\n"
	"GlowIntensity = 1.3\n"
	"GlowTextureWidth = 256\n"
	"RadiusScale1 = 0.18\n"
	"Amplitude1 = 0.06\n"
	"RadiusScale2 = 4.5\n"
	"Amplitude2 = 0.11\n"
	"TerrainGlow = No\n"
	"MultipassGlowEnabled = No\n"
	"End\n";

// maps\shellmap1\map.ini's block, as it reads
const char *const kShellMapBlock =
	"GlowEffect\n"
	"GlowEnabled = Yes\n"
	"GlowDiameter = 20\n"
	"GlowIntensity = .5\n"
	"GlowTextureWidth = 256\n"
	"RadiusScale1 = 0.18\n"
	"Amplitude1 = 0.06\n"
	"RadiusScale2 = 4.5\n"
	"Amplitude2 = 0.11\n"
	"TerrainGlow = No\n"
	"MultipassGlowEnabled = No\n"
	"End\n";

struct GlowEnv
{
	INIEnvironment env;
	GlowEffectStore store;
	GlowEnv() { store.registerBlock(env.blocks); }
	std::string load(const std::string &text, INILoadType type)
	{
		return initest::loadError(env, "glow.ini", text, type);
	}
};

void checkRetailDefaults(const GlowEffect &g)
{
	CHECK(g.glowEnabled);
	CHECK(g.glowDiameter == 15);
	CHECK(g.glowIntensity == 1.3f);
	CHECK(g.glowTextureWidth == 256);
	CHECK(g.radiusScale1 == 0.18f);
	CHECK(g.amplitude1 == 0.06f);
	CHECK(g.radiusScale2 == 4.5f);
	CHECK(g.amplitude2 == 0.11f);
	CHECK(!g.terrainGlow);
	CHECK(!g.multipassGlowEnabled);
}
} // namespace

TEST_CASE("glow1 ini: the field table is RW 0xC1E810's ten rows in the binary's order")
{
	const FieldParse *t = GlowEffectStore::fieldTable();
	const char *const names[] = { "GlowEnabled", "GlowDiameter", "GlowIntensity", "GlowTextureWidth", "RadiusScale1", "RadiusScale2", "Amplitude1",
		"Amplitude2", "TerrainGlow", "MultipassGlowEnabled" };
	const int offsets[] = { 0x00, 0x04, 0x08, 0x0C, 0x10, 0x18, 0x14, 0x1C, 0x20, 0x21 };
	for (size_t i = 0; i < 10; ++i)
	{
		REQUIRE(t[i].token != nullptr);
		CHECK(std::string(t[i].token) == names[i]);
		CHECK(t[i].offset == offsets[i]);
	}
	CHECK(t[10].token == nullptr);
	CHECK(sizeof(GlowEffect) == 0x24); // RW 0x70B73B copies nine dwords
}

TEST_CASE("glow1 ini: before any INI the record is zero (RW 0xDA28C0 / 0xDA28E4 have no constructor)")
{
	GlowEffectStore s;
	CHECK(!s.active().glowEnabled);
	CHECK(s.active().glowDiameter == 0);
	CHECK(s.active().glowIntensity == 0.0f);
	CHECK(s.active().glowTextureWidth == 0);
	CHECK(!s.dirty());
}

TEST_CASE("glow1 ini: environment.ini's block parses into the saved and the active copy and sets the dirty byte")
{
	GlowEnv e;
	REQUIRE(e.load(kEnvironmentBlock, INI_LOAD_OVERWRITE).empty());
	checkRetailDefaults(e.store.active());
	checkRetailDefaults(e.store.saved());
	CHECK(e.store.dirty()); // RW 0xDE4ED4
	e.store.clearDirty();
	CHECK(!e.store.dirty());
}

TEST_CASE("glow1 ini: a map.ini block (load type 2) overrides the active copy only; reset restores it (RW 0x70B5B1)")
{
	GlowEnv e;
	REQUIRE(e.load(kEnvironmentBlock, INI_LOAD_OVERWRITE).empty());
	e.store.clearDirty();
	REQUIRE(e.load(kShellMapBlock, INI_LOAD_CREATE_OVERRIDES).empty());
	CHECK(e.store.active().glowDiameter == 20);
	CHECK(e.store.active().glowIntensity == 0.5f);
	CHECK(e.store.active().glowTextureWidth == 256);
	CHECK(e.store.saved().glowDiameter == 15);
	CHECK(e.store.saved().glowIntensity == 1.3f);
	CHECK(e.store.dirty());
	e.store.clearDirty();
	e.store.reset();
	checkRetailDefaults(e.store.active());
	CHECK(e.store.dirty());
}

TEST_CASE("glow1 ini: a block names only some fields: the rest keep the saved values (maps\\shellmapbackup\\map.ini has no GlowTextureWidth)")
{
	GlowEnv e;
	REQUIRE(e.load(kEnvironmentBlock, INI_LOAD_OVERWRITE).empty());
	REQUIRE(e.load("GlowEffect\nGlowDiameter = 75\nGlowIntensity = .3\nEnd\n", INI_LOAD_CREATE_OVERRIDES).empty());
	CHECK(e.store.active().glowDiameter == 75);
	CHECK(e.store.active().glowIntensity == 0.3f);
	CHECK(e.store.active().glowTextureWidth == 256);
	CHECK(e.store.active().glowEnabled);
}

TEST_CASE("glow1 ini: a ChildObject-type load (4) leaves the saved copy alone too (RW 0x70B75F)")
{
	GlowEnv e;
	REQUIRE(e.load(kEnvironmentBlock, INI_LOAD_OVERWRITE).empty());
	REQUIRE(e.load("GlowEffect\nGlowDiameter = 33\nEnd\n", INI_LOAD_CHILD_OBJECT).empty());
	CHECK(e.store.active().glowDiameter == 33);
	CHECK(e.store.saved().glowDiameter == 15);
}

TEST_CASE("glow1 ini: a later main-INI block replaces the saved copy (load type 1 and 3)")
{
	GlowEnv e;
	REQUIRE(e.load(kEnvironmentBlock, INI_LOAD_OVERWRITE).empty());
	REQUIRE(e.load("GlowEffect\nGlowDiameter = 40\nEnd\n", INI_LOAD_MULTIFILE).empty());
	CHECK(e.store.saved().glowDiameter == 40);
	CHECK(e.store.active().glowDiameter == 40);
	CHECK(e.store.saved().glowIntensity == 1.3f);
}

TEST_CASE("glow1 ini: an unknown field is an INI error, never skipped")
{
	GlowEnv e;
	const std::string error = e.load("GlowEffect\nGlowEnabled = Yes\nGlowRadius = 3\nEnd\n", INI_LOAD_OVERWRITE);
	CHECK(error.find("GlowRadius") != std::string::npos);
}

TEST_CASE("glow1 ini: a bad value is an INI error")
{
	GlowEnv e;
	CHECK(!e.load("GlowEffect\nGlowDiameter = wide\nEnd\n", INI_LOAD_OVERWRITE).empty());
	CHECK(!e.load("GlowEffect\nGlowEnabled = maybe\nEnd\n", INI_LOAD_OVERWRITE).empty());
}

TEST_CASE("glow1 gating: lowLOD is EffectsLOD VeryLow or Low (RW 0x48789F: cmp [mgr + 0x1778], 1; jg)")
{
	using namespace W3DViewDrawPostPass;
	CHECK(lowEffectsLOD(0));
	CHECK(lowEffectsLOD(1));
	CHECK(!lowEffectsLOD(2));
	CHECK(!lowEffectsLOD(3));
	CHECK(!lowEffectsLOD(4));
}

TEST_CASE("glow1 gating: the special filters are the view filters 6, 7 and 8 (RW 0x4878B1 .. 0x4878C2)")
{
	using namespace W3DViewDrawPostPass;
	for (int f = 0; f < 12; ++f)
	{
		CHECK(specialFilter(f) == (f == 6 || f == 7 || f == 8));
	}
}

TEST_CASE("glow1 gating: the whole table - low effects LOD runs neither pass, an active grade pre-empts the glow, otherwise the glow runs")
{
	using namespace W3DViewDrawPostPass;
	struct Row
	{
		int lod;
		bool grade, glow;
		Choice expected;
	};
	// EffectsLOD 0 / 1 (VeryLow, Low): None whatever the grade and the glow say; 2 / 3 / 4: grade wins, then glow, else none
	std::vector<Row> rows;
	for (int lod = 0; lod < 5; ++lod)
	{
		for (int grade = 0; grade < 2; ++grade)
		{
			for (int glow = 0; glow < 2; ++glow)
			{
				Choice c = Choice::None;
				if (lod >= 2)
				{
					c = grade ? Choice::ColorGrade : glow ? Choice::Glow : Choice::None;
				}
				rows.push_back({ lod, grade != 0, glow != 0, c });
			}
		}
	}
	REQUIRE(rows.size() == 20);
	for (const Row &r : rows)
	{
		Inputs in;
		in.effectsLOD = r.lod;
		in.colorGradeActive = r.grade;
		in.glowEnabled = r.glow;
		INFO("effectsLOD " << r.lod << " grade " << r.grade << " glow " << r.glow);
		CHECK(choose(in) == r.expected);
	}
}

TEST_CASE("glow1 gating: the special filter branch comes first and is not gated by the effects LOD; the filter 8 needs GlowEnabled (RW 0x4878E9)")
{
	using namespace W3DViewDrawPostPass;
	for (int lod = 0; lod < 5; ++lod)
	{
		for (int grade = 0; grade < 2; ++grade)
		{
			for (int glow = 0; glow < 2; ++glow)
			{
				Inputs in;
				in.effectsLOD = lod;
				in.colorGradeActive = grade != 0;
				in.glowEnabled = glow != 0;
				in.viewFilter = 6;
				CHECK(choose(in) == Choice::SpecialFilter);
				in.viewFilter = 7;
				CHECK(choose(in) == Choice::SpecialFilter);
				in.viewFilter = 8;
				CHECK(choose(in) == (glow ? Choice::SpecialFilter : Choice::None));
				in.viewFilter = 5; // not a special filter: the ordinary rule
				CHECK(choose(in) != Choice::SpecialFilter);
			}
		}
	}
}

TEST_CASE("glow1 gating: only the scene's ordinary pass mode 0 runs a pass (RW 0x4878C4)")
{
	using namespace W3DViewDrawPostPass;
	Inputs in;
	in.glowEnabled = true;
	in.colorGradeActive = true;
	for (int mode = 1; mode < 8; ++mode)
	{
		in.passMode = mode;
		CHECK(choose(in) == Choice::None);
		in.viewFilter = 6;
		CHECK(choose(in) == Choice::None);
		in.viewFilter = 0;
	}
}

TEST_CASE("glow1 kernel: RotWK's 15 taps (environment.ini) - 15 samples kept, one zero pad, exact float32 values")
{
	GlowEffect g;
	g.glowDiameter = 15;
	g.radiusScale1 = 0.18f;
	g.amplitude1 = 0.06f;
	g.radiusScale2 = 4.5f;
	g.amplitude2 = 0.11f;
	const std::vector<ScreenHilightFilter::Sample> k = ScreenHilightFilter::createGaussianVector(g);
	const float xs[16] = { -7.0999999f, -6.0999999f, -5.0999999f, -4.0999999f, -3.0999999f, -2.0999999f, -1.10000002f, -0.100000001f, 0.899999976f,
		1.89999998f, 2.9000001f, 3.9000001f, 4.9000001f, 5.9000001f, 6.9000001f, 0.0f };
	const float zs[16] = { 0.0509308726f, 0.0559427403f, 0.0646251887f, 0.079900071f, 0.103428707f, 0.132403284f, 0.158165097f, 0.169896811f,
		0.161936015f, 0.138170242f, 0.10898675f, 0.0839511305f, 0.0670622215f, 0.0572959259f, 0.0517611392f, 0.0f };
	REQUIRE(k.size() == 16);
	for (size_t i = 0; i < 16; ++i)
	{
		INFO("sample " << i);
		CHECK(k[i].x == xs[i]);
		CHECK(k[i].y == 0.0f);
		CHECK(k[i].z == zs[i]);
	}
}

TEST_CASE("glow1 kernel: 20 taps (shellmap1) - the 15 / taps scale of 0.75, twenty samples, no pad")
{
	const std::vector<ScreenHilightFilter::Sample> k = ScreenHilightFilter::createGaussianVector(20, 0.18f, 0.06f, 4.5f, 0.11f);
	const float xs[20] = { -9.60000038f, -8.60000038f, -7.5999999f, -6.5999999f, -5.5999999f, -4.5999999f, -3.5999999f, -2.5999999f, -1.60000002f,
		-0.600000024f, 0.400000006f, 1.39999998f, 2.4000001f, 3.4000001f, 4.4000001f, 5.4000001f, 6.4000001f, 7.4000001f, 8.39999962f, 9.39999962f };
	const float zs[20] = { 0.0382774808f, 0.0408932269f, 0.0447345711f, 0.0506566204f, 0.0595445819f, 0.0718642771f, 0.0870842636f, 0.103291079f,
		0.117384464f, 0.126000017f, 0.126830086f, 0.119643271f, 0.106390685f, 0.0903325975f, 0.0747166425f, 0.0617330596f, 0.0521723963f,
		0.0457224809f, 0.0415388942f, 0.038736172f };
	REQUIRE(k.size() == 20);
	for (size_t i = 0; i < 20; ++i)
	{
		INFO("sample " << i);
		CHECK(k[i].x == xs[i]);
		CHECK(k[i].z == zs[i]);
	}
}

TEST_CASE("glow1 kernel: samples at or under 0.01 are dropped and the list pads to a multiple of 4 (RW 0x477A8D / 0x477AB0)")
{
	// one component, a flat amplitude 0.0099: nothing is kept, no pad (the size is 0)
	CHECK(ScreenHilightFilter::createGaussianVector(15, 0.0f, 0.0099f, 0.0f, 0.0f).empty());
	// 11 taps of a flat 0.5: all kept (11), padded with one zero sample to 12 (the 15 / 11 scale folds to above 1)
	const std::vector<ScreenHilightFilter::Sample> k = ScreenHilightFilter::createGaussianVector(11, 0.0f, 0.5f, 0.0f, 0.0f);
	REQUIRE(k.size() == 12);
	CHECK(k[11].x == 0.0f);
	CHECK(k[11].y == 0.0f);
	CHECK(k[11].z == 0.0f);
}

TEST_CASE("glow1 blur: four groups per pass for 15 taps; the first writes, the rest blend; weights = z * GlowIntensity; offsets in UV")
{
	GlowEffect g;
	g.glowDiameter = 15;
	g.radiusScale1 = 0.18f;
	g.amplitude1 = 0.06f;
	g.radiusScale2 = 4.5f;
	g.amplitude2 = 0.11f;
	const auto kernel = ScreenHilightFilter::createGaussianVector(g);
	const auto horizontal = ScreenHilightFilter::blurPass(kernel, 1.3f, 256, false);
	const auto vertical = ScreenHilightFilter::blurPass(kernel, 1.3f, 256, true);
	REQUIRE(horizontal.size() == 4);
	REQUIRE(vertical.size() == 4);
	for (size_t i = 0; i < 4; ++i)
	{
		const bool blends = i == 0 ? false : true; // the first group is stored, every later one blends
		CHECK(horizontal[i].blend == blends);
		CHECK(vertical[i].blend == blends);
		for (size_t j = 0; j < 4; ++j)
		{
			const auto &s = kernel[i * 4 + j];
			CHECK(horizontal[i].weight[j] == s.z * 1.3f);
			CHECK(horizontal[i].offset[j][0] == s.x * (1.0f / 256.0f));
			CHECK(horizontal[i].offset[j][1] == 0.0f);
			CHECK(vertical[i].weight[j] == s.z * 1.3f);
			CHECK(vertical[i].offset[j][0] == 0.0f);
			CHECK(vertical[i].offset[j][1] == s.x * (1.0f / 256.0f));
		}
	}
	// the zero pad has weight 0 and offset 0
	CHECK(horizontal[3].weight[3] == 0.0f);
	CHECK(horizontal[3].offset[3][0] == 0.0f);
}

TEST_CASE("glow1 pixel: Glow.pso is t0 * c0 + t1 * c1 + t2 * c2 + t3 * c3 with every step saturating (ps_1_1)")
{
	using namespace ScreenHilightFilter;
	const std::array<std::array<float, 3>, 4> taps = { { { 1.0f, 0.5f, 0.0f }, { 1.0f, 0.5f, 0.25f }, { 0.0f, 0.5f, 0.25f }, { 0.0f, 0.5f, 0.0f } } };
	const std::array<float, 4> w = { 0.25f, 0.5f, 0.75f, 0.5f };
	const auto out = glowPixel(taps, w);
	CHECK(out[0] == 0.25f + 0.5f);
	CHECK(out[1] == 0.5f * 0.25f + 0.5f * 0.5f + 0.5f * 0.75f + 0.5f * 0.5f);
	CHECK(out[2] == 0.25f * 0.5f + 0.25f * 0.75f);
	// saturation: a sum above 1 stops at 1
	const std::array<std::array<float, 3>, 4> hot = { { { 1, 1, 1 }, { 1, 1, 1 }, { 1, 1, 1 }, { 1, 1, 1 } } };
	const auto sat = glowPixel(hot, { 0.5f, 0.5f, 0.5f, 0.5f });
	CHECK(sat[0] == 1.0f);
	// constants saturate at 1
	const auto big = glowPixel(hot, { 4.0f, 0.0f, 0.0f, 0.0f });
	CHECK(big[0] == 1.0f);
}

TEST_CASE("glow1 blend: ONE / INVSRCCOLOR is src + dst * (1 - src); a 8 bit store rounds to nearest")
{
	using namespace ScreenHilightFilter;
	const auto out = blendOneInvSrcColor({ 0.5f, 0.0f, 1.0f }, { 0.5f, 0.25f, 0.75f });
	CHECK(out[0] == 0.75f);
	CHECK(out[1] == 0.25f);
	CHECK(out[2] == 1.0f);
	CHECK(store8(0.5f) == 128.0f / 255.0f);
	CHECK(store8(1.7f) == 1.0f);
	CHECK(store8(-1.0f) == 0.0f);
	CHECK(store8(0.5f / 255.0f) == 1.0f / 255.0f); // exactly half a step rounds up
}

TEST_CASE("glow1 stop S-5231: the unverified list names the gaps and is pinned")
{
	const auto &lines = ScreenHilightFilter::unverified();
	REQUIRE(lines.size() == 4);
	for (const char *l : lines)
	{
		CHECK(std::strncmp(l, "S-5231: ", 8) == 0);
	}
	CHECK(std::string(lines[0]).find("point sampled") != std::string::npos);
	CHECK(std::string(lines[1]).find("MultipassGlowEnabled") != std::string::npos);
	CHECK(std::string(lines[1]).find("0xD34") != std::string::npos);
	CHECK(std::string(lines[2]).find("GlowEmissive") != std::string::npos);
	CHECK(std::string(lines[3]).find("device limit") != std::string::npos);
}

TEST_CASE("glow1 limits: GlowDiameter past 128 taps is refused before the kernel is built; every retail value passes")
{
	GlowEffect e;
	e.glowEnabled = true;
	e.glowIntensity = 1.3f;
	e.glowTextureWidth = 256;
	e.radiusScale1 = 0.18f;
	e.amplitude1 = 0.06f;
	e.radiusScale2 = 4.5f;
	e.amplitude2 = 0.11f;
	// environment.ini 15, maps\shellmap1 20, maps\shellmapbackup 75, and the boundary
	for (int taps : { 1, 15, 20, 75, 128 })
	{
		e.glowDiameter = taps;
		CHECK_MESSAGE(ScreenHilightFilter::unusableReason(e).empty(), "GlowDiameter ", taps);
	}
	for (int taps : { 129, 100000000, 2147483647 })
	{
		e.glowDiameter = taps;
		CHECK(ScreenHilightFilter::unusableReason(e) == "GlowDiameter " + std::to_string(taps) + " is above the device limit of 128 taps");
	}
	for (int taps : { 0, -1 })
	{
		e.glowDiameter = taps;
		CHECK(ScreenHilightFilter::unusableReason(e) == "GlowDiameter " + std::to_string(taps) + " is not a usable number of taps");
	}
	e.glowDiameter = 15;
	for (int width : { 0, 4097 })
	{
		e.glowTextureWidth = width;
		CHECK(ScreenHilightFilter::unusableReason(e) == "GlowTextureWidth " + std::to_string(width) + " is not a usable texture size");
	}
	e.glowTextureWidth = 4096;
	CHECK(ScreenHilightFilter::unusableReason(e).empty());
}

TEST_CASE("glow1 retail: environment.ini's GlowEffect is the active copy of the loaded world")
{
	OPENBFME_REQUIRE_START(s);
	checkRetailDefaults(s->world->glowEffect().active());
	checkRetailDefaults(s->world->glowEffect().saved());
}

TEST_CASE("glow1 retail: maps\\shellmap1\\map.ini overrides GlowDiameter and GlowIntensity for its map only; a reload restores environment.ini")
{
	if (!starttest::shared())
	{
		retailtest::printSkip("a GLOW-1 retail test (" __FILE__ ")");
		return;
	}
	std::unique_ptr<starttest::Shared> fresh = starttest::freshWorld();
	REQUIRE(fresh != nullptr);
	REQUIRE(fresh->world != nullptr);
	auto context = fresh->world->enterContext();
	const auto result = fresh->world->applyMapIni("maps\\shellmap1");
	REQUIRE(result.mapIniFound);
	CHECK(result.errors.empty());
	const GlowEffect &a = fresh->world->glowEffect().active();
	CHECK(a.glowEnabled);
	CHECK(a.glowDiameter == 20);
	CHECK(a.glowIntensity == 0.5f);
	CHECK(a.glowTextureWidth == 256);
	CHECK(fresh->world->glowEffect().saved().glowDiameter == 15);
	CHECK(fresh->world->glowEffect().saved().glowIntensity == 1.3f);
}
