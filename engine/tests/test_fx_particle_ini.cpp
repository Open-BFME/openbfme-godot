// OpenBFME unit tests: FXParticleSystem and FXList INI parsing (lane FX-1). GPL-3.0.
//
// Synthetic cases carry expectations derived by hand from the INI text and the RW field tables (lane notes
// fx-ini-parse.md / fx-fxlist.md, spec fx-and-particles.md). The retail cases (ROTWK_INSTALL and BFME2_INSTALL set; otherwise
// they print SKIP) parse every FXParticleSystem and FXList block of the pure 2.01 mount with zero errors and compare the
// block, class and nugget counts with the independent Python scan (tools/fx/fx_ini_scan.py, golden
// tests/data/fx/fx_ini_counts.json).

#include "doctest.h"

#include "Common/MiniJson.h"
#include "GameClient/FXList.h"
#include "GameClient/FXParticleSystem.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DDrawServices.h"
#include "IniTestUtil.h"
#include "RetailTestMount.h"

#include <fstream>
#include <map>
#include <sstream>

using namespace FXParticleSystem;
using doctest::Approx;

namespace
{

struct FxHarness
{
	initest::Fixture fixture;
	FXParticleSystemTemplateStore particles;
	FXListStore lists;

	FxHarness()
	{
		fixture.env.blocks.registerBlock("FXParticleSystem", [this](INI *ini) {
			FXParticleSystemTemplateStore *saved = TheFXParticleSystemTemplateStore;
			TheFXParticleSystemTemplateStore = &particles;
			try
			{
				ParseFXParticleSystemDefinitionGlobal(ini);
			}
			catch (...)
			{
				TheFXParticleSystemTemplateStore = saved;
				throw;
			}
			TheFXParticleSystemTemplateStore = saved;
		});
		fixture.env.blocks.registerBlock("FXList", [this](INI *ini) {
			FXListStore *saved = TheFXListStore;
			TheFXListStore = &lists;
			try
			{
				ParseFXListDefinitionGlobal(ini);
			}
			catch (...)
			{
				TheFXListStore = saved;
				throw;
			}
			TheFXListStore = saved;
		});
	}

	std::string load(const std::string &text, INILoadType type = INI_LOAD_OVERWRITE, int *code = nullptr)
	{
		return initest::loadError(fixture.env, "fx.ini", text, type, code);
	}
};

const char *kFullBlock = R"(FXParticleSystem TestSys
  System
    Priority = HIGH_OR_ABOVE
    Shader = ALPHA
    Type = DRAWABLE
    ParticleName = foo.tga
    Lifetime = 75 80
    SystemLifetime = 2500
    SortLevel = 1
    Size = 10 12 GAUSSIAN
    BurstDelay = 0.8 0.8
    BurstCount = 1 3
    InitialDelay = 5 6
    IsGroundAligned = Yes
    IsOneShot = Yes
    SlaveSystem = Other
    SlavePosOffset = X:1 Y:2 Z:3
  End
  Color = DefaultColor
    Color1 = R:255 G:0 B:51 0
    Color3 = R:0 G:255 B:0 25
    ColorScale = -0.5 0.5
  End
  Alpha = DefaultAlpha
    Alpha2 = 0.15 0.35 15
  End
  Update = DefaultUpdate
    SizeRate = 10 15
    Rotation = ROTATE_Z
  End
  Physics = DefaultPhysics
    Gravity = -0.48
    DriftVelocity = X:0 Y:0 Z:0.5
    Swirly = Yes
  End
  EmissionVelocity = OrthoEmissionVelocity
    X = 0 0.5
  End
  EmissionVolume = CylinderEmissionVolume
    Radius = 5
    Length = 7
    Offset = X:1 Y:2 Z:3
    IsHollow = Yes
  End
  Draw = DefaultDraw
  End
  Wind = DefaultWind
    WindMotion = PingPong
    WindStrength = 0.1
  End
  Event = TerrainCollision
    EventFX = FX_Something
    HeightOffset = 1 2
  End
End
)";

} // namespace

TEST_CASE("FXParticleSystem: a full block, values derived by hand from the INI text and the RW tables")
{
	FxHarness h;
	CHECK(h.load(kFullBlock).empty());
	const ParticleSystemTemplate *t = h.particles.findTemplate("TestSys");
	REQUIRE(t != nullptr);
	CHECK(h.particles.findTemplate("testsys") == nullptr); // names are case sensitive (RW hash 0x42b6c1 + memcmp)

	const ParticleSystemInfo &s = t->info();
	CHECK(s.m_priority == PARTICLE_PRIORITY_HIGH_OR_ABOVE); // index 2 of the RW list 0xc31ae8
	CHECK(s.m_shaderType == PARTICLE_SHADER_ALPHA);         // index 3 of 0xc31a54
	CHECK(s.m_particleType == PARTICLE_TYPE_DRAWABLE);      // index 2 of 0xc31a84
	CHECK(s.m_particleTypeName == "foo.tga");
	CHECK(s.m_lifetime.m_type == GameClientRandomVariable::UNIFORM); // no third token -> UNIFORM (RW 0x73a396)
	CHECK(s.m_lifetime.m_low == 75.0f);
	CHECK(s.m_lifetime.m_high == 80.0f);
	CHECK(s.m_systemLifetime == 2500u); // stored as written, no duration conversion
	CHECK(s.m_sortLevel == 1u);
	CHECK(s.m_size.m_type == GameClientRandomVariable::GAUSSIAN);
	CHECK(s.m_burstDelay.m_low == Approx(0.8f));
	CHECK(s.m_burstCount.m_high == 3.0f);
	CHECK(s.m_initialDelay.m_low == 5.0f);
	CHECK(s.m_isGroundAligned);
	CHECK(s.m_isOneShot);
	CHECK(!s.m_isEmitAboveGroundOnly);
	CHECK(s.m_slaveSystemName == "Other");
	CHECK(s.m_slavePosOffset.x == 1.0f);
	CHECK(s.m_slavePosOffset.z == 3.0f);
	// an unspecified field keeps the constructor default: StartSizeRate CONSTANT 0 0
	CHECK(s.m_startSizeRate.m_type == GameClientRandomVariable::CONSTANT);

	const DefaultColorModuleData *c = static_cast<const DefaultColorModuleData *>(t->module(MODULE_CATEGORY_COLOR));
	REQUIRE(c != nullptr);
	CHECK(c->classId == MODULE_DEFAULT_COLOR);
	CHECK(c->m_colorKey[0].color.red == 1.0f);   // 255 * (1/255) at PC24 = exactly 1
	CHECK(c->m_colorKey[0].color.blue == Approx(0.2f).epsilon(1e-5));  // 51/255
	CHECK(c->m_colorKey[0].frame == 0u);
	CHECK(c->m_colorKey[1].frame == 0u); // Color2 was not written: the slot keeps {0,0,0,0}
	CHECK(c->m_colorKey[2].color.green == 1.0f);
	CHECK(c->m_colorKey[2].frame == 25u);
	CHECK(c->m_colorScale.m_low == -0.5f);

	const DefaultAlphaModuleData *a = static_cast<const DefaultAlphaModuleData *>(t->module(MODULE_CATEGORY_ALPHA));
	REQUIRE(a != nullptr);
	CHECK(a->m_alphaKey[1].var.m_type == GameClientRandomVariable::UNIFORM);
	CHECK(a->m_alphaKey[1].var.m_low == Approx(0.15f));
	CHECK(a->m_alphaKey[1].var.m_high == Approx(0.35f));
	CHECK(a->m_alphaKey[1].frame == 15u);

	const DefaultUpdateModuleData *u = static_cast<const DefaultUpdateModuleData *>(t->module(MODULE_CATEGORY_UPDATE));
	REQUIRE(u != nullptr);
	CHECK(u->m_sizeRate.m_high == 15.0f);
	CHECK(u->m_rotation == PARTICLE_ROTATION_Z); // ROTATE_Z is index 4 of 0xc31b1c
	CHECK(u->m_sizeRateDamping.m_low == 1.0f);   // default RV(1,1,1) (RW 0x96aff4)
	CHECK(u->m_angularDamping.m_high == 1.0f);

	const DefaultPhysicsModuleData *p = static_cast<const DefaultPhysicsModuleData *>(t->module(MODULE_CATEGORY_PHYSICS));
	REQUIRE(p != nullptr);
	CHECK(p->m_gravity == Approx(-0.48f));
	CHECK(p->m_driftVelocity.z == 0.5f);
	CHECK(p->m_swirly);
	CHECK(!p->m_particlesAttachToBone);

	const CylinderEmissionVolumeModuleData *v = static_cast<const CylinderEmissionVolumeModuleData *>(t->module(MODULE_CATEGORY_EMISSION_VOLUME));
	REQUIRE(v != nullptr);
	CHECK(v->classId == MODULE_CYLINDER_EMISSION_VOLUME);
	CHECK(v->m_radius == 5.0f);
	CHECK(v->m_length == 7.0f);
	CHECK(v->m_offset.y == 2.0f);
	CHECK(v->m_isHollow);

	CHECK(t->module(MODULE_CATEGORY_DRAW)->classId == MODULE_DEFAULT_DRAW);
	const DefaultWindModuleData *w = static_cast<const DefaultWindModuleData *>(t->module(MODULE_CATEGORY_WIND));
	REQUIRE(w != nullptr);
	CHECK(w->m_windMotion == WIND_MOTION_PING_PONG);
	CHECK(w->m_windStrength == Approx(0.1f));
	CHECK(w->m_windZeroStrengthDist == 200.0f); // constructor default (RW 0x966821)

	REQUIRE(t->events().size() == 1);
	const TerrainCollisionModuleData *e = static_cast<const TerrainCollisionModuleData *>(t->events()[0].get());
	CHECK(e->m_eventFX == "FX_Something");
	CHECK(e->m_heightOffset.m_high == 2.0f);
	CHECK(e->m_perParticle);     // Event base defaults are TRUE (RW 0x7a9039)
	CHECK(e->m_killAfterEvent);
}

TEST_CASE("FXParticleSystem: constructor defaults of the modules (RW constructors)")
{
	FxHarness h;
	CHECK(h.load("FXParticleSystem Defaults\n  Draw = GpuDraw\n  End\n  Wind = DefaultWind\n  End\n  EmissionVolume = TerrainFireEmission\n  End\n  Draw = RenderObjectDraw\n  End\nEnd\n").empty());
	const ParticleSystemTemplate *t = h.particles.findTemplate("Defaults");
	REQUIRE(t != nullptr);
	// the last Draw replaced the GpuDraw (a repeated category replaces, RW 0x5f3b0a)
	const RenderObjectDrawModuleData *r = static_cast<const RenderObjectDrawModuleData *>(t->module(MODULE_CATEGORY_DRAW));
	REQUIRE(r != nullptr);
	CHECK(r->m_shader[0] == PARTICLE_SHADER_W3D_DIFFUSE); // RW 0x9649d6: Shader1..3 default 8
	CHECK(r->m_shader[2] == PARTICLE_SHADER_W3D_DIFFUSE);
	CHECK(r->m_numObjects[1] == 0);
	const DefaultWindModuleData *w = static_cast<const DefaultWindModuleData *>(t->module(MODULE_CATEGORY_WIND));
	CHECK(w->m_windMotion == WIND_MOTION_NOT_USED);
	CHECK(w->m_windStrength == 2.0f);
	CHECK(w->m_windFullStrengthDist == 75.0f);
	CHECK(w->m_windPingPongStartAngleMax == Approx(0.785398f));
	CHECK(w->m_windPingPongEndAngleMin == Approx(5.49779f));
	const TerrainFireEmissionModuleData *f = static_cast<const TerrainFireEmissionModuleData *>(t->module(MODULE_CATEGORY_EMISSION_VOLUME));
	CHECK(f->m_cellEmissionChance == Approx(0.7f)); // RW 0xbde0a8
	// System defaults (RW 0x5f44f4)
	CHECK(t->info().m_priority == PARTICLE_PRIORITY_ULTRA_HIGH_ONLY);
	CHECK(t->info().m_shaderType == PARTICLE_SHADER_ADDITIVE);
	CHECK(t->info().m_particleType == PARTICLE_TYPE_PARTICLE);
	CHECK(t->module(MODULE_CATEGORY_COLOR) == nullptr); // missing categories stay NULL
	CHECK(t->events().empty());
}

TEST_CASE("FXParticleSystem: rejections match RW (codes and texts)")
{
	int code = 0;
	{
		FxHarness h;
		const std::string msg = h.load("FXParticleSystem A\n  Emitter = DefaultEmitter\n  End\nEnd\n", INI_LOAD_OVERWRITE, &code);
		CHECK(code == 5); // `Emitter` has no row in the RW table
		CHECK(msg.find("Unknown field 'Emitter'") != std::string::npos);
	}
	{
		FxHarness h;
		h.load("FXParticleSystem A\n  Alpha\n  End\nEnd\n", INI_LOAD_OVERWRITE, &code);
		CHECK(code == 3); // a category line needs its class token; there is no default class (RW 0x5f7c81 -> 0x42dc9f)
	}
	{
		FxHarness h;
		h.load("FXParticleSystem A\n  Alpha = NoSuchAlpha\n  End\nEnd\n", INI_LOAD_OVERWRITE, &code);
		CHECK(code == 5); // RW faults; refused as an error (rule 10)
	}
	{
		FxHarness h;
		h.load("FXParticleSystem A\n  Draw = DefaultDraw\n    Foo = 1\n  End\nEnd\n", INI_LOAD_OVERWRITE, &code);
		CHECK(code == 5); // the empty table 0xc84858 accepts no field
	}
	{
		FxHarness h;
		const std::string msg = h.load("FXParticleSystem A\n  Color = DefaultColor\n    Color1 = R:300 G:0 B:0 0\n  End\nEnd\n", INI_LOAD_OVERWRITE, &code);
		CHECK(code == 3);
		CHECK(msg.find("color value R=300 out of range (0..255)") != std::string::npos);
	}
	{
		FxHarness h;
		h.load("FXParticleSystem A\n  System\n    Lifetime = 5\n  End\nEnd\n", INI_LOAD_OVERWRITE, &code);
		CHECK(code == 3); // the second token of a random variable is mandatory
	}
	{
		FxHarness h;
		h.load("FXParticleSystem A\n  System\n    Priority = BOGUS\n  End\nEnd\n", INI_LOAD_OVERWRITE, &code);
		CHECK(code == 3);
	}
	{
		FxHarness h;
		h.load("FXParticleSystem A\n  System\n    IsOneShot = true\n  End\nEnd\n", INI_LOAD_OVERWRITE, &code);
		CHECK(code == 3); // only Yes / No
	}
	{
		FxHarness h;
		h.load("FXParticleSystem A\n  System\n    Lifetime = 1 2\n", INI_LOAD_OVERWRITE, &code);
		CHECK(code == 4); // Missing 'END'
	}
}

TEST_CASE("FXParticleSystem: a redefined template is rebuilt from defaults for every load type; load type 2 sets the override flag")
{
	FxHarness h;
	CHECK(h.load("FXParticleSystem A\n  Physics = DefaultPhysics\n    Gravity = 3\n  End\nEnd\n").empty());
	CHECK(h.load("FXParticleSystem A\n  System\n    IsOneShot = Yes\n  End\nEnd\n", INI_LOAD_MULTIFILE).empty());
	const ParticleSystemTemplate *t = h.particles.findTemplate("A");
	REQUIRE(t != nullptr);
	CHECK(t->module(MODULE_CATEGORY_PHYSICS) == nullptr); // no merge (RW 0x5fc82f-0x5fc848)
	CHECK(t->info().m_isOneShot);
	CHECK(h.particles.templateCount() == 1);
	CHECK(!h.particles.overrideLoaded());
	CHECK(h.load("FXParticleSystem B\nEnd\n", INI_LOAD_CREATE_OVERRIDES).empty());
	CHECK(h.particles.overrideLoaded()); // RW 0x5fc896
}

TEST_CASE("FXParticleSystem: the module registry is the binary's 27 classes")
{
	const std::vector<ModuleClassInfo> &reg = ModuleClassRegistry();
	CHECK(reg.size() == (size_t)MODULE_CLASS_COUNT);
	CHECK(reg.size() == 27u);
	int perCategory[MODULE_CATEGORY_COUNT] = {};
	for (const ModuleClassInfo &c : reg)
	{
		++perCategory[c.category];
		CHECK(FindModuleClass(c.category, c.token) == &c);
		CHECK(c.createDefault()->classId == c.id);
	}
	// RW notes 2.4: Color 1, Alpha 1, Update 2, Physics 1, EmissionVelocity 5, EmissionVolume 7, Draw 7, Wind 1, Event 2
	const int expected[MODULE_CATEGORY_COUNT] = { 1, 1, 2, 1, 5, 7, 7, 1, 2 };
	for (int i = 0; i < MODULE_CATEGORY_COUNT; ++i)
	{
		CHECK(perCategory[i] == expected[i]);
	}
	CHECK(FindModuleClass(MODULE_CATEGORY_DRAW, "DefaultColor") == nullptr); // a class is only found in its own category
}

TEST_CASE("GameClientRandomVariable: parse and draw")
{
	FxHarness h;
	CHECK(h.load("FXParticleSystem A\n  System\n    Size = 2 4\n    BurstCount = 7 7 CONSTANT\n  End\nEnd\n").empty());
	const ParticleSystemInfo &s = h.particles.findTemplate("A")->info();
	CHECK(s.m_size.m_type == GameClientRandomVariable::UNIFORM);
	CHECK(s.m_burstCount.m_type == GameClientRandomVariable::CONSTANT);
	W3DClientRandom rng(RandomAlgorithm::RotWK_GameDat_LCG);
	CHECK(s.m_burstCount.getValue(rng) == 7.0f); // CONSTANT low == high: no draw
	const float d = s.m_size.getValue(rng);
	CHECK(d >= 2.0f);
	CHECK(d <= 4.0f);
}

// ---- FXList ----------------------------------------------------------------------------------------------------
TEST_CASE("FXList: nuggets, defaults and field values derived by hand from the INI text")
{
	FxHarness h;
	const char *text = R"(FXList FX_One
  ParticleSystem
    Name = PS_A
    Offset = X:1 Y:2 Z:3
    OrientToObject = Yes
    InitialDelay = 100 200 UNIFORM
    RotateZ = 90
    Weather = SNOWY
    StopIfNuggetPlayed = Yes
  End
  Sound
    Name = Snd_Boom
    SourceObjectFilter = NONE +Orc
  End
  ViewShake
    Type = STRONG
  End
  TerrainScorch
    Type = RANDOM
    RandomRange = X:4 Y:6
  End
  LightPulse
    Color = R:255 G:255 B:128
    Radius = 25
    IncreaseTime = 0
    DecreaseTime = 500
  End
  TintDrawable
    Color = R:-255 G:0 B:255
  End
  ParticleSysBone = B_SWORDBONE LightningCharge FollowBone:Yes FXTrigger:CATAPULT_ROCK
  PlayEvenIfShrouded = Yes
  CullingInfo = TrackingSeconds:5.0 StartCullingAbove:4 CullAllAbove:8
End
FXList FX_Two
  FXListAtBonePos
    FX = FX_One
    BoneName = HAND
  End
  EvaEvent
    EvaEventOwner = Eva_Owner
  End
End
)";
	CHECK(h.load(text).empty());
	const FXList *one = h.lists.findFXList("FX_One");
	REQUIRE(one != nullptr);
	CHECK(h.lists.findFXList("fx_one") == nullptr); // case sensitive
	CHECK(h.lists.findFXList("None") == nullptr);
	CHECK(h.lists.findFXList("nOnE") == nullptr);
	REQUIRE(one->nuggets().size() == 7);
	CHECK(one->hasParticleSysBone());
	CHECK(one->playEvenIfShrouded());
	CHECK(one->cullWindowFrames() == 25);   // 5.0 s * 5 logic frames
	CHECK(one->startCullingAbove() == 4);
	CHECK(one->cullAllAbove() == 8);

	const ParticleSystemFXNugget *ps = static_cast<const ParticleSystemFXNugget *>(one->nuggets()[0].get());
	CHECK(ps->m_type == FX_NUGGET_PARTICLE_SYSTEM);
	CHECK(ps->m_name == "PS_A");
	CHECK(ps->m_count == 1);
	CHECK(ps->m_offset.y == 2.0f);
	CHECK(ps->m_orientToObject);
	CHECK(ps->m_initialDelay.m_high == 200.0f);
	CHECK(ps->m_rotateZ == Approx(1.5707964f).epsilon(1e-6)); // 90 degrees in radians
	CHECK(ps->m_weather == 1);
	CHECK(ps->m_stopIfNuggetPlayed);
	CHECK(ps->m_systemLife == -1);
	CHECK(ps->m_targetCoeff == 1.0f);

	const SoundFXNugget *snd = static_cast<const SoundFXNugget *>(one->nuggets()[1].get());
	CHECK(snd->m_soundName == "Snd_Boom");
	CHECK(snd->m_sourceObjectFilter != nullptr);
	CHECK(snd->m_weather == 2);
	CHECK(static_cast<const ViewShakeFXNugget *>(one->nuggets()[2].get())->m_type == 2);
	const TerrainScorchFXNugget *sc = static_cast<const TerrainScorchFXNugget *>(one->nuggets()[3].get());
	CHECK(sc->m_scorchType == -1);
	CHECK(sc->m_randomRange.x == 4);
	CHECK(sc->m_randomRange.y == 6);
	const LightPulseFXNugget *lp = static_cast<const LightPulseFXNugget *>(one->nuggets()[4].get());
	CHECK(lp->m_color.green == 1.0f);
	CHECK(lp->m_color.blue == Approx(128.0f / 255.0f));
	CHECK(lp->m_decreaseFrames == 3u); // 500 ms -> ceil(500 * 0.005) = 3 logic frames (PLAN rule 2)
	const TintDrawableFXNugget *tint = static_cast<const TintDrawableFXNugget *>(one->nuggets()[5].get());
	CHECK(tint->m_color.red == -1.0f); // tint colours accept negative components (RW 0x5df33e)
	CHECK(tint->m_preColorTime == 2000u);
	const ParticleSysBoneFXNugget *bone = static_cast<const ParticleSysBoneFXNugget *>(one->nuggets()[6].get());
	CHECK(bone->m_boneName == "b_swordbone"); // lower-cased (RW 0x4363b0)
	CHECK(bone->m_particleSystemName == "LightningCharge");
	CHECK(bone->m_followBone);
	CHECK(bone->m_fxTrigger == 1);

	const FXList *two = h.lists.findFXList("FX_Two");
	REQUIRE(two != nullptr);
	CHECK(static_cast<const FXListAtBonePosFXNugget *>(two->nuggets()[0].get())->m_fx == one); // resolved at parse time
	CHECK(h.lists.unverified().size() == 2); // Sound and Eva names were not validated: stop S-190
}

TEST_CASE("FXList: rejections match RW")
{
	int code = 0;
	{
		FxHarness h;
		const std::string msg = h.load("FXList A\n  Tracer\n  End\nEnd\n", INI_LOAD_OVERWRITE, &code);
		CHECK(code == 5); // RW has no Tracer nugget
		CHECK(msg.find("Unknown field 'Tracer'") != std::string::npos);
	}
	{
		FxHarness h;
		const std::string msg = h.load("FXList A\n  FXListAtBonePos\n    FX = NoSuchList\n  End\nEnd\n", INI_LOAD_OVERWRITE, &code);
		CHECK(code == 3);
		CHECK(msg.find("iniParseFXList -- FXList NoSuchList not found!") != std::string::npos);
	}
	{
		FxHarness h;
		CHECK(h.load("FXList A\n  FXListAtBonePos\n    FX = None\n  End\nEnd\n").empty()); // None -> NULL, no error
		CHECK(static_cast<const FXListAtBonePosFXNugget *>(h.lists.findFXList("A")->nuggets()[0].get())->m_fx == nullptr);
	}
	{
		FxHarness h;
		h.load("FXList A\n  ViewShake\n    Type = BOGUS\n  End\nEnd\n", INI_LOAD_OVERWRITE, &code);
		CHECK(code == 3);
	}
	{
		FxHarness h;
		h.load("FXList A\n  CullingInfo = Bogus:1\nEnd\n", INI_LOAD_OVERWRITE, &code);
		CHECK(code == 3);
	}
	{
		FxHarness h;
		h.load("FXList A\n  CullingInfo = StartCullingAbove:0\nEnd\n", INI_LOAD_OVERWRITE, &code);
		CHECK(code == 3); // m_cullTrackingMin == 0
	}
	{
		FxHarness h;
		h.load("FXList A\n  Sound\n    Bogus = 1\n  End\nEnd\n", INI_LOAD_OVERWRITE, &code);
		CHECK(code == 5);
	}
}

TEST_CASE("FXList: a redefined name replaces the entry; load type 5 flags the old list superseded")
{
	FxHarness h;
	CHECK(h.load("FXList A\n  ViewShake\n  End\nEnd\n").empty());
	const FXList *first = h.lists.findFXList("A");
	CHECK(h.load("FXList A\n  ViewShake\n  End\n  ViewShake\n  End\nEnd\n").empty());
	const FXList *second = h.lists.findFXList("A");
	CHECK(second != first);
	CHECK(second->nuggets().size() == 2);
	CHECK(!first->superseded()); // load type 1: the old object stays playable (RW leaks it)
	CHECK(h.load("FXList A\n  ViewShake\n  End\nEnd\n", INI_LOAD_RELOAD).empty());
	CHECK(h.lists.findFXList("A")->nuggets().size() == 1);
	CHECK(second->superseded()); // load type 5 flags (+0x24) the erased one
	CHECK(h.lists.listCount() == 1);
}

// ---- retail ----------------------------------------------------------------------------------------------------
namespace
{
bool readGolden(JsonValue &out)
{
	std::ifstream f(retailtest::dataDir() + "/fx/fx_ini_counts.json", std::ios::binary);
	if (!f)
	{
		return false;
	}
	std::stringstream ss;
	ss << f.rdbuf();
	std::string err;
	return JsonValue::parse(ss.str(), out, &err);
}

int jsonInt(const JsonValue *v)
{
	REQUIRE(v != nullptr);
	return (int)v->number;
}
} // namespace

TEST_CASE("FXParticleSystem + FXList: every retail block parses with zero errors and matches the independent scan")
{
	retailtest::Mount *mount = retailtest::pureMount();
	if (!mount)
	{
		retailtest::printSkip("FX retail INI");
		return;
	}
	REQUIRE(mount->fs != nullptr);
	JsonValue golden;
	REQUIRE(readGolden(golden));

	FxHarness h;
	h.fixture.env.fileSystem = mount->fs.get();
	{
		// retail defines its #define macros (5,975 of them) in gamedata.ini before any other content file loads (spec 3.4)
		INI macros(h.fixture.env);
		macros.preprocessFile("Data\\INI\\GameData.ini", INI_LOAD_OVERWRITE);
	}
	{
		INI ini(h.fixture.env);
		ini.load("Data\\INI\\FXParticleSystem.ini", INI_LOAD_OVERWRITE);
	}
	{
		INI ini(h.fixture.env);
		ini.load("Data\\INI\\Default\\FXList.ini", INI_LOAD_OVERWRITE);
		INI ini2(h.fixture.env);
		ini2.load("Data\\INI\\FXList.ini", INI_LOAD_OVERWRITE);
	}

	const JsonValue *ps = golden.get("fxParticleSystem");
	REQUIRE(ps != nullptr);
	CHECK((int)h.particles.templateCount() == jsonInt(ps->get("blocks")));
	CHECK((int)h.particles.templateCount() == jsonInt(ps->get("uniqueNames")));
	std::map<std::string, int> classes;
	for (const std::string &name : h.particles.templateNames())
	{
		const ParticleSystemTemplate *t = h.particles.findTemplate(name);
		for (int c = 0; c < MODULE_CATEGORY_EVENT; ++c)
		{
			if (const ModuleData *m = t->module((ModuleCategory)c))
			{
				classes[std::string(ModuleCategoryKeys[c]) + "=" + ModuleClassRegistry()[m->classId].token]++;
			}
		}
		for (const std::shared_ptr<ModuleData> &e : t->events())
		{
			classes[std::string("Event=") + ModuleClassRegistry()[e->classId].token]++;
		}
	}
	const JsonValue *cls = ps->get("classes");
	REQUIRE(cls != nullptr);
	CHECK(classes.size() == cls->object.size());
	for (const auto &kv : cls->object)
	{
		CHECK_MESSAGE(classes[kv.first] == (int)kv.second.number, kv.first);
	}

	const JsonValue *fl = golden.get("fxList");
	REQUIRE(fl != nullptr);
	CHECK((int)h.lists.listCount() == jsonInt(fl->get("blocks")));
	CHECK((int)h.lists.listCount() == jsonInt(fl->get("uniqueNames")));
	CHECK(h.lists.supersededCount() == 0u);
	static const char *const typeNames[] = { "ParticleSystem", "Sound", "EvaEvent", "DynamicDecal", "TerrainScorch", "CameraShakerVolume", "FXListAtBonePos", "ViewShake",
		"BuffNugget", "TintDrawable", "LightPulse", "AttachedModel", "RayEffect", "CursorParticleSystem", "Laser" };
	std::map<std::string, int> nuggets;
	int particleSysBone = 0;
	for (const std::string &name : h.lists.listNames())
	{
		const FXList *l = h.lists.findFXList(name);
		for (const std::unique_ptr<FXNugget> &n : l->nuggets())
		{
			switch (n->m_type)
			{
			case FX_NUGGET_PARTICLE_SYSTEM: nuggets["ParticleSystem"]++; break;
			case FX_NUGGET_SOUND: nuggets["Sound"]++; break;
			case FX_NUGGET_EVA_EVENT: nuggets["EvaEvent"]++; break;
			case FX_NUGGET_DYNAMIC_DECAL: nuggets["DynamicDecal"]++; break;
			case FX_NUGGET_TERRAIN_SCORCH: nuggets["TerrainScorch"]++; break;
			case FX_NUGGET_CAMERA_SHAKER_VOLUME: nuggets["CameraShakerVolume"]++; break;
			case FX_NUGGET_FXLIST_AT_BONE_POS: nuggets["FXListAtBonePos"]++; break;
			case FX_NUGGET_VIEW_SHAKE: nuggets["ViewShake"]++; break;
			case FX_NUGGET_BUFF: nuggets["BuffNugget"]++; break;
			case FX_NUGGET_TINT_DRAWABLE: nuggets["TintDrawable"]++; break;
			case FX_NUGGET_LIGHT_PULSE: nuggets["LightPulse"]++; break;
			case FX_NUGGET_ATTACHED_MODEL: nuggets["AttachedModel"]++; break;
			case FX_NUGGET_RAY_EFFECT: nuggets["RayEffect"]++; break;
			case FX_NUGGET_CURSOR_PARTICLE_SYSTEM: nuggets["CursorParticleSystem"]++; break;
			case FX_NUGGET_LASER: nuggets["Laser"]++; break;
			case FX_NUGGET_PARTICLE_SYS_BONE: ++particleSysBone; break;
			}
		}
	}
	(void)typeNames;
	const JsonValue *ng = fl->get("nuggets");
	REQUIRE(ng != nullptr);
	CHECK(nuggets.size() == ng->object.size());
	for (const auto &kv : ng->object)
	{
		CHECK_MESSAGE(nuggets[kv.first] == (int)kv.second.number, kv.first);
	}
	// the single-line ParticleSysBone form is a nugget in the list; the scan counts it as a list field
	CHECK(particleSysBone == jsonInt(fl->get("listFieldUses")->get("ParticleSysBone")));
	// 59 FXListAtBonePos.FX references are resolved at parse time (RW 0x73a302)
	int resolved = 0;
	for (const std::string &name : h.lists.listNames())
	{
		for (const std::unique_ptr<FXNugget> &n : h.lists.findFXList(name)->nuggets())
		{
			if (n->m_type == FX_NUGGET_FXLIST_AT_BONE_POS && static_cast<const FXListAtBonePosFXNugget *>(n.get())->m_fx)
			{
				++resolved;
			}
		}
	}
	CHECK(resolved == nuggets["FXListAtBonePos"]);
	// Sound / Eva validation is another lane's registry: reported, not hidden
	CHECK(h.lists.unvalidatedSoundNames() > 0u);
	CHECK(h.lists.unverified().size() == 2u);
}
