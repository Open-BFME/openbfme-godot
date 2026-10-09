// OpenBFME. FX-3 tests (QA-1 U6 .. U9): ParticleSysBone names resolve in the FXParticleSystem store alone (retail never loads ParticleSystem.ini), a
// bone the model lacks plays the system at the drawable's origin, the draw scripts' unknown transition is retail's logged data defect, and their target
// questions answer from the object's target record. The retail cases SKIP when ROTWK_INSTALL / BFME2_INSTALL are unset.
#include "doctest.h"
#include "HudTestUtil.h"

#include "Common/AsciiString.h"
#include "Common/ModelState.h"
#include "GameClient/DrawableScriptTarget.h"
#include "GameClient/FXPlayback.h"
#include "GameClient/LiveFX.h"
#include "GameClient/ParticleDraw.h"
#include "GameClient/ParticleTexture.h"
#include "GameClient/MapObjectRuntime.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/ObjectTemplateInfo.h"

#include <algorithm>
#include <cmath>
#include <regex>
#include <set>
#include <string>
#include <vector>

namespace
{
const std::set<std::string> kOnlyOldFormat = { "AndruilEfxParent", "AndruilFlare", "AragornGrndGlow", "InfantryDustTrails", "LightningBolt", "RainOfFireFlame",
	"RainOfFireFlare", "RainOfFireFlareNoTrails", "RainOfFireImpactLight", "RainOfFireImpactSparkles", "RainOfFireProjectileSmoke", "RainOfFireSmoke" };

KindOfMaskType kinds(std::initializer_list<const char *> names)
{
	KindOfMaskType m{};
	for (const char *n : names)
	{
		const int bit = ObjectTemplateInfoBuilder::kindOfIndex(n);
		REQUIRE(bit >= 0);
		m[(size_t)bit / 32] |= 1u << ((unsigned)bit % 32);
	}
	return m;
}
} // namespace

TEST_CASE("fx3 retail: ParticleSystem.ini's old-format names are not FXParticleSystem templates only in 12 cases, which retail resolves to nothing")
{
	if (!hudtest::haveWorld("fx3 retail ParticleSystem.ini"))
	{
		return;
	}
	hudtest::SharedWorld &s = hudtest::shared();
	std::vector<std::uint8_t> bytes;
	std::string err;
	REQUIRE_MESSAGE(s.mount->fs->readFile("Data\\INI\\ParticleSystem.ini", bytes, &err), err);
	// an independent line scan of the file's block headers (the engine has no parser for it, as retail has none: tools/fx/test_fx3_binary_facts.py)
	const std::string text(bytes.begin(), bytes.end());
	const std::regex header("(^|\\n)ParticleSystem[ \\t]+([^ \\t\\r\\n]+)");
	std::vector<std::string> names;
	for (std::sregex_iterator it(text.begin(), text.end(), header), end; it != end; ++it)
	{
		names.push_back((*it)[2].str());
	}
	CHECK(names.size() == 329);
	FXPlayback playback(*s.mount->fs, RandomAlgorithm::RotWK_GameDat_LCG);
	CHECK(playback.loadRetailData().empty());
	std::set<std::string> unresolved;
	for (const std::string &n : names)
	{
		if (!playback.particleTemplates().findTemplate(n))
		{
			unresolved.insert(n);
		}
	}
	CHECK(unresolved == kOnlyOldFormat);
}

TEST_CASE("fx3 retail: an orc warrior's MOVING InfantryDustTrails plays nothing (RW 0x73AECB: NULL) and the Uruk pit's construction dust plays at the pit's origin "
		  "(RW 0x4C6514: no CONSTDUSTBONE01 bone in IBUrukPit_A)")
{
	if (!hudtest::haveWorld("fx3 retail particle bones"))
	{
		return;
	}
	hudtest::SharedWorld &s = hudtest::shared();
	hudtest::Rig rig(s);
	FXPlayback playback(*s.mount->fs, RandomAlgorithm::RotWK_GameDat_LCG);
	CHECK(playback.loadRetailData().empty());
	LiveFX fx(*rig.game, playback);
	const auto context = s.world->enterContext();
	std::string err;
	Object *man = rig.game->createObject("MordorFighter", rig.index(), Coord3D{ 1500.0f, 1500.0f, 0.0f }, 0.0f, &err);
	REQUIRE_MESSAGE(man != nullptr, err);
	const int enemy = rig.game->players().findPlayerWithName("Player_2")->getPlayerIndex();
	Object *pit = rig.game->createObject("IsengardUrukPit", enemy, Coord3D{ 1900.0f, 1500.0f, 0.0f }, 0.0f, &err);
	REQUIRE_MESSAGE(pit != nullptr, err);
	man->setModelConditionState(ModelCondition::indexOf("MOVING"), true);
	// a foundation that starts to rise (Construction.cpp startRise, RW 0x858931 ff)
	pit->setConstructionPercent(0.0f);
	pit->setStatus(OBJECT_STATUS_UNDER_CONSTRUCTION, true);
	pit->setModelConditionState(ModelCondition::indexOf("ACTIVELY_BEING_CONSTRUCTED"), true);
	pit->setModelConditionState(ModelCondition::indexOf("PARTIALLY_CONSTRUCTED"), true);
	const ObjectID pitId = pit->getID();
	rig.game->advance(0.2);
	fx.flushPending();
	fx.updateAttachedSystems();

	CHECK(fx.stats().unresolvedParticleSystems.count("InfantryDustTrails") == 1);
	for (const std::string &n : fx.stats().unresolvedParticleSystems)
	{
		CHECK_MESSAGE(kOnlyOldFormat.count(n) == 1, n); // nothing else the two objects name is missing from the store
	}
	const Drawable *pitDrawable = rig.game->drawables().findByObject(pitId);
	REQUIRE(pitDrawable != nullptr);
	int dust = 0, trails = 0;
	playback.particles().forEachSystem([&](const FXParticleSystem::ParticleSystem &sys) {
		if (sys.getTemplate().getName() == "InfantryDustTrails")
		{
			++trails;
		}
		if (sys.getTemplate().getName() == "BuildingContructDust" && sys.attachedDrawableID() == pitDrawable->getID())
		{
			++dust;
			// the bone index 0 leaves the local position at the origin (RW 0x4C6644 .. 0x4C665B)
			CHECK(sys.position().x == 0.0f);
			CHECK(sys.position().y == 0.0f);
			CHECK(sys.position().z == 0.0f);
		}
	});
	CHECK(trails == 0);
	CHECK(dust == 1);
	bool pitBoneMiss = false;
	for (const std::string &b : fx.stats().missingBones)
	{
		pitBoneMiss = pitBoneMiss || AsciiStringUtil::lowered(b) == "iburukpit_a:constdustbone01";
	}
	CHECK(pitBoneMiss);
	CHECK(fx.stats().boneMisses >= 1);
}

TEST_CASE("fx3: retail's unknown-transition log line is a data defect of the draw report, not an error (RW 0x4BE7F5 .. 0x4BE843)")
{
	const DrawMessageClass c = classifyDrawMessage("W3DScriptedModelDraw::adjustAnimation: Unable to find transition state named 'TRANS_Sprout' (asked by state Idle)");
	CHECK(c.kind == DrawMessageClass::DATA_DEFECT);
	CHECK(classifyDrawMessage("script asked for the unknown model condition FOO").kind == DrawMessageClass::ERROR);
}

TEST_CASE("fx3: the draw scripts' target record: KindOf of the recorded victim (RW 0x73667D) and the bearing of RW 0x4B3D8D / 0x644FD0")
{
	DrawableScriptTarget t;
	CHECK_FALSE(t.isTargetKindOf("MONSTER")); // no object
	t.hasObject = true;
	t.ownPosition = Coord3D{ 100.0f, 100.0f, 0.0f };
	t.ownAngle = 0.0f; // facing +x
	CHECK_FALSE(t.isTargetKindOf("MONSTER")); // no victim
	t.targetExists = true;
	t.targetKindOf = kinds({ "MONSTER", "SELECTABLE" });
	CHECK(t.isTargetKindOf("MONSTER"));
	CHECK(t.isTargetKindOf("monster")); // RW 0x6AAD1A compares case insensitively
	CHECK_FALSE(t.isTargetKindOf("STRUCTURE"));
	CHECK_FALSE(t.isTargetKindOf("NOT_A_KINDOF"));

	// the bearing: 0 at the object's own position, +pi/2 to its left (counter-clockwise), -pi/2 to its right, pi behind
	t.targetPosition = t.ownPosition;
	CHECK(t.bearing() == 0.0f);
	t.targetPosition = Coord3D{ 100.0f, 200.0f, 50.0f };
	CHECK(std::fabs(t.bearing() - 1.5707964f) < 1e-6f);
	t.targetPosition = Coord3D{ 100.0f, 0.0f, 0.0f };
	CHECK(std::fabs(t.bearing() + 1.5707964f) < 1e-6f);
	t.targetPosition = Coord3D{ 0.0f, 100.0f, 0.0f };
	CHECK(std::fabs(std::fabs(t.bearing()) - 3.1415927f) < 1e-6f);
	t.ownAngle = 1.5707964f; // facing +y: the target at -x is now to the left
	CHECK(std::fabs(t.bearing() - 1.5707964f) < 1e-5f);
}

TEST_CASE("fx3 retail: an Ent's attack script asks the recorded victim's KindOf (CurDrawableIsCurrentTargetKindof) without a draw error")
{
	if (!hudtest::haveWorld("fx3 retail ent target"))
	{
		return;
	}
	hudtest::SharedWorld &s = hudtest::shared();
	hudtest::Rig rig(s, "map mp fall back 4p", "FactionElves");
	const auto context = s.world->enterContext();
	std::string err;
	Object *ent = rig.game->createObject("RohanEntBirch", rig.index(), Coord3D{ 1500.0f, 1500.0f, 0.0f }, 0.0f, &err);
	REQUIRE_MESSAGE(ent != nullptr, err);
	const int enemy = rig.game->players().findPlayerWithName("Player_2")->getPlayerIndex();
	Object *troll = rig.game->createObject("MordorMountainTroll", enemy, Coord3D{ 1530.0f, 1500.0f, 0.0f }, 3.14159f, &err);
	REQUIRE_MESSAGE(troll != nullptr, err);
	rig.game->advance(0.2);
	REQUIRE(ent->getAIUpdateInterface() != nullptr);
	REQUIRE(ent->getAIUpdateInterface()->aiAttackObject(troll, CMD_FROM_PLAYER));
	bool recorded = false;
	for (int i = 0; i < 60 && !recorded; ++i)
	{
		rig.game->advance(0.2);
		recorded = ent->getWeapons() && ent->getWeapons()->drawTargetID() == troll->getID();
	}
	REQUIRE(recorded); // RW 0x69213E recorded the victim
	const Drawable *d = rig.game->drawables().findByObject(ent->getID());
	REQUIRE(d != nullptr);
	CHECK(d->scriptTarget().hasObject);
	CHECK(d->scriptTarget().isTargetKindOf("MONSTER"));
	for (const DrawEntry &e : d->entries())
	{
		if (!e.draw)
		{
			continue;
		}
		for (const std::string &m : e.draw->errors())
		{
			CHECK_MESSAGE(m.find("no target information") == std::string::npos, m);
			CHECK_MESSAGE(classifyDrawMessage(m).kind != DrawMessageClass::ERROR, m);
		}
	}
}

namespace
{
// DXT5 alpha of mip 0 (the 8-byte alpha block: two endpoints, 3-bit indices; DirectX's BC3 palette), row-major
std::vector<int> dxt5Alpha(const std::vector<std::uint8_t> &dds, int &w, int &h)
{
	auto u32 = [&](size_t o) { return (std::uint32_t)dds[o] | ((std::uint32_t)dds[o + 1] << 8) | ((std::uint32_t)dds[o + 2] << 16) | ((std::uint32_t)dds[o + 3] << 24); };
	h = (int)u32(12);
	w = (int)u32(16);
	std::vector<int> a((size_t)(w * h), 0);
	size_t bi = 128;
	for (int by = 0; by < h / 4; ++by)
	{
		for (int bx = 0; bx < w / 4; ++bx, bi += 16)
		{
			const int a0 = dds[bi], a1 = dds[bi + 1];
			int pal[8] = { a0, a1, 0, 0, 0, 0, 0, 0 };
			if (a0 > a1)
			{
				for (int i = 1; i <= 6; ++i)
				{
					pal[i + 1] = ((7 - i) * a0 + i * a1) / 7;
				}
			}
			else
			{
				for (int i = 1; i <= 4; ++i)
				{
					pal[i + 1] = ((5 - i) * a0 + i * a1) / 5;
				}
				pal[6] = 0;
				pal[7] = 255;
			}
			std::uint64_t bits = 0;
			for (int k = 0; k < 6; ++k)
			{
				bits |= (std::uint64_t)dds[bi + 2 + (size_t)k] << (8 * k);
			}
			for (int i = 0; i < 16; ++i)
			{
				a[(size_t)((by * 4 + i / 4) * w + bx * 4 + i % 4)] = pal[(bits >> (3 * i)) & 7];
			}
		}
	}
	return a;
}
} // namespace

TEST_CASE("fx3 retail: the construction dust (BuildingContructDust) draws as retail does: soft textured billboards of 10 .. 110 units at the ground, ALPHA, deferred, "
		  "depth tested without depth writes (RW 0xD9B310 = 0x1180B3)")
{
	if (!hudtest::haveWorld("fx3 retail construction dust"))
	{
		return;
	}
	hudtest::SharedWorld &s = hudtest::shared();
	FXPlayback playback(*s.mount->fs, RandomAlgorithm::RotWK_GameDat_LCG);
	REQUIRE(playback.loadRetailData().empty());
	playback.seedClientRandom(1);
	REQUIRE(playback.playParticleSystem("BuildingContructDust", Coord3D{ 0.0f, 0.0f, 0.0f }) != FXParticleSystem::INVALID_PARTICLE_SYSTEM_ID);
	for (int i = 0; i < 90; ++i)
	{
		playback.step(); // 3 s of 30 Hz client steps: past the 75-step lifetime, so births and deaths balance
	}
	FXParticleSystem::ParticleDrawBuilder builder(playback.clientRandom()); // DefaultDraw samples no random numbers
	const std::vector<FXParticleSystem::DrawBatch> batches = builder.build(playback.particles(), FXParticleSystem::DrawCamera());
	REQUIRE(batches.size() == 1u);
	const FXParticleSystem::DrawBatch &b = batches[0];
	CHECK(b.kind == FXParticleSystem::DrawBatch::SPRITES);
	CHECK(b.texture == "EXsnowcloud02.tga");
	CHECK(b.shader == FXParticleSystem::PARTICLE_SHADER_ALPHA);
	CHECK(b.deferred);           // SortLevel 1 (RW 0x44C84A: drawn last, reverse list order, sorting renderer off)
	CHECK_FALSE(b.groundAligned); // billboards: Set_Billboard(!IsGroundAligned), edge = size (PointGroup corners +-0.5, RW 0x57DAB0)
	// BurstCount 1 every step (no BurstDelay), Lifetime 75 steps; alpha keys 0 @0, 0.15 .. 0.35 @15, 0 @75 (the first is the never-born particle's 0, invisible)
	CHECK(b.sprites.size() >= 70u);
	CHECK(b.sprites.size() <= 76u);
	int atOrBelowGround = 0;
	float maxSize = 0.0f, maxAlpha = 0.0f, maxZ = -1e9f;
	for (const FXParticleSystem::SpriteInstance &p : b.sprites)
	{
		CHECK(p.r == 1.0f);
		CHECK(p.g == 1.0f);
		CHECK(p.b == 1.0f);
		CHECK(p.size >= 10.0f);
		CHECK(p.size <= 110.0f); // Size 10 + SizeRate 10 .. 15 damped by 0.8 .. 0.85 per step (RW 0x96AF99): at most 10 + 15 / 0.15
		maxSize = std::max(maxSize, p.size);
		maxAlpha = std::max(maxAlpha, p.a);
		maxZ = std::max(maxZ, p.z);
		CHECK(p.z >= -5.0f);
		atOrBelowGround += p.z - 0.5f * p.size < 0.0f ? 1 : 0; // the billboard's lower edge (half the edge below the centre) is under the ground
	}
	CHECK(maxAlpha <= 0.35f);
	CHECK(maxSize >= 60.0f); // grown puffs: at least 10 + 10 / 0.2
	CHECK(maxZ <= 5.0f + 75.0f * 0.5f + 0.5f / 0.05f); // hollow box half height 5, drift 0.5 per step, the damped emission speed
	// the puffs rise slower than they grow: most reach below the emitter's ground plane, where the depth test (LEQUAL, no depth write) cuts them along a
	// straight line, as in retail (no soft-particle path: the CPU particles use the fixed-function ShaderClass word, Shaders.big has no particle effect but GpuDraw's)
	CHECK(atOrBelowGround * 2 > (int)b.sprites.size());
	const std::vector<std::string> notes = builder.unverified();
	CHECK(std::find(notes.begin(), notes.end(), std::string("S-1440: ") + FXParticleSystem::SpriteCompositeText()) != notes.end());
	MESSAGE("fx3 dust: " << b.sprites.size() << " sprites, largest " << maxSize << ", alpha at most " << maxAlpha << ", highest centre " << maxZ << ", " << atOrBelowGround
						 << " reach below the ground");

	// the texture: art\compiledtextures\ex\exsnowcloud02.dds (BFME2 textures1.big), DXT5 128 x 128 whose alpha falls to (almost) 0 at the border, so a textured
	// sprite shows no square edge; a hard-edged square is retail's 1x1 white default texture (RW 0x532962), i.e. a texture that did not resolve
	const ParticleTextureResolution res = ResolveParticleTexture(b.texture, [&](const std::string &p) { return s.mount->fs->doesFileExist(p); });
	REQUIRE(res.Found);
	CHECK(res.Path == "art\\compiledtextures\\ex\\exsnowcloud02.dds");
	std::vector<std::uint8_t> dds;
	std::string err;
	REQUIRE_MESSAGE(s.mount->fs->readFile(res.Path, dds, &err), err);
	REQUIRE(dds.size() > 128u);
	CHECK(std::string(dds.begin() + 84, dds.begin() + 88) == "DXT5");
	int w = 0, h = 0;
	const std::vector<int> alpha = dxt5Alpha(dds, w, h);
	CHECK(w == 128);
	CHECK(h == 128);
	int border = 0, centre = 0;
	for (int i = 0; i < w; ++i)
	{
		border = std::max({ border, alpha[(size_t)i], alpha[(size_t)((h - 1) * w + i)], alpha[(size_t)(i * w)], alpha[(size_t)(i * w + w - 1)] });
	}
	for (int y = 56; y < 72; ++y)
	{
		for (int x = 56; x < 72; ++x)
		{
			centre = std::max(centre, alpha[(size_t)(y * w + x)]);
		}
	}
	CHECK(border <= 3);
	CHECK(centre >= 200);
}
