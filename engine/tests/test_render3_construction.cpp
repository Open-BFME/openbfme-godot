// OpenBFME. RENDER-3 tests: the construction look of retail structures. Every retail draw module that names a construction model condition (AWAITING_CONSTRUCTION,
// PARTIALLY_CONSTRUCTED, ACTIVELY_BEING_CONSTRUCTED) is built through the draw module runtime with the real W3D files and the real drawable Lua state, then given
// the flags GettingBuiltBehavior / the foundation path set (RW 0x68D607: AWAITING; then PARTIALLY | ACTIVELY), and the build-up frame is driven as RW 0x4B51B5
// does. Runs only when ROTWK_INSTALL and BFME2_INSTALL are set (prints SKIP otherwise).
#include "doctest.h"

#include "BuildTestUtil.h"
#include "RetailTestMount.h"
#include "W3DDrawRetail.h"
#include "W3DDrawTestUtil.h"

#include "Common/AsciiString.h"
#include "Common/BuildAssistant.h"
#include "Common/Player.h"
#include "GameClient/Drawable.h"
#include "GameClient/DrawableManager.h"
#include "GameLogic/BuildPlacement.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Module/DozerAIUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DLuaDrawScriptHost.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DScriptedModelDraw.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "Libraries/WWVegas/WW3D2/hanim.h"
#include "Libraries/WWVegas/WW3D2/w3dshadergen.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace drawtest;

namespace
{
class FirstRandom : public W3DDrawRandom
{
public:
	int value(int lo, int) override { return lo; }
	float real(float lo, float) override { return lo; }
	std::vector<std::string> unverified() const override { return { "S-093: test double source" }; }
};

bool hasConstructionBit(const ModelConditionFlags &f)
{
	static const int bits[3] = { ModelCondition::indexOf("AWAITING_CONSTRUCTION"), ModelCondition::indexOf("PARTIALLY_CONSTRUCTED"),
		ModelCondition::indexOf("ACTIVELY_BEING_CONSTRUCTED") };
	for (int b : bits)
	{
		if (b >= 0 && f.test(b))
		{
			return true;
		}
	}
	return false;
}

struct Look
{
	std::string model, state, clip;
	int frames = 0;
	bool adjustHeight = false;
	std::vector<std::string> hidden;
};

struct Site
{
	ArchiveW3DFileSource source;
	WW3DAssetManager assets;
	WW3DDrawAssets drawAssets;
	FirstRandom random;
	W3DLuaDrawScriptHost host;
	std::unique_ptr<W3DScriptedModelDraw> draw;

	explicit Site(retailtest::Mount &mount) : source(*mount.fs), assets(source), drawAssets(assets) {}

	Look look() const
	{
		Look l;
		const W3DDrawFrame f = draw->frame();
		l.model = f.model ? f.modelName : std::string();
		l.state = f.animationStateName;
		if (f.trackCount > 0 && f.tracks[0].anim)
		{
			l.clip = f.tracks[0].clipName;
			l.frames = f.tracks[0].anim->Get_Num_Frames();
		}
		l.adjustHeight = draw->adjustsHeightByConstruction();
		l.hidden = f.hiddenSubObjects;
		return l;
	}
};

const RetailDrawModule *findModule(const std::string &object, const std::string &tag)
{
	for (const RetailDrawModule &m : retailDrawScan().modules)
	{
		if (m.data && m.object == object && m.tag == tag)
		{
			return &m;
		}
	}
	return nullptr;
}
} // namespace

TEST_CASE("render3 retail: every structure draw with construction states selects its build-up model and MANUAL animation; no construction state sinks")
{
	retailtest::Mount *mount = retailtest::pureMount();
	if (!mount)
	{
		retailtest::printSkip("render3 retail: construction states");
		return;
	}
	REQUIRE_MESSAGE(mount->fs, mount->error);
	int modules = 0, animated = 0;
	std::vector<std::string> problems;
	for (const RetailDrawModule &m : retailDrawScan().modules)
	{
		if (!m.data || m.className != "W3DScriptedModelDraw")
		{
			continue;
		}
		bool construction = false;
		for (const ModelConditionInfo &s : m.data->m_conditionStates)
		{
			construction = construction || hasConstructionBit(s.conditions);
		}
		for (const AnimationStateInfo &s : m.data->m_animationStates)
		{
			construction = construction || hasConstructionBit(s.conditions);
		}
		if (!construction)
		{
			continue;
		}
		++modules;
		Site site(*mount);
		W3DScriptedModelDraw::Options o;
		o.buildBones = false;
		try
		{
			site.draw.reset(new W3DScriptedModelDraw(*m.data, site.drawAssets, site.random, &site.host, o));
		}
		catch (const std::exception &e)
		{
			problems.push_back(m.object + " " + m.tag + ": " + e.what());
			continue;
		}
		site.draw->setModelConditionFlags(flagsOf({ "AWAITING_CONSTRUCTION" }));
		const Look waiting = site.look();
		site.draw->setModelConditionFlags(flagsOf({ "PARTIALLY_CONSTRUCTED", "ACTIVELY_BEING_CONSTRUCTED" }));
		const Look rising = site.look();
		std::string line = m.object + " " + m.tag + ": waiting [" + waiting.model + " " + waiting.clip + "] rising [" + rising.model + " " + rising.state + " " +
			rising.clip + " " + std::to_string(rising.frames) + "f";
		for (const std::string &h : rising.hidden)
		{
			line += " -" + h;
		}
		line += "]";
		std::printf("  info: %s\n", line.c_str());
		if (rising.adjustHeight)
		{
			problems.push_back(m.object + " " + m.tag + ": a construction state carries ADJUST_HEIGHT_BY_CONSTRUCTION_PERCENT");
		}
		animated += rising.frames > 1 ? 1 : 0;
		for (const std::string &e : site.draw->errors())
		{
			problems.push_back(m.object + " " + m.tag + ": " + e);
		}
	}
	for (const std::string &p : problems)
	{
		std::printf("  problem: %s\n", p.c_str());
	}
	std::printf("  info: %d construction draw modules, %d with a build-up animation, %zu problems\n", modules, animated, problems.size());
	// pinned against the retail data (2.01 INI + W3D): the problems are retail data defects (sub objects a non-permanent hide names that the model lacks,
	// animations named without a skeleton, e.g. ElvenBarracksBFME1's NBElvnBarx_A.NBElvnBarx_A); they may only shrink
	CHECK(modules == 269);
	CHECK(animated == 221);
	CHECK(problems.size() <= 63);
}

namespace
{
struct Expected
{
	const char *object;
	const char *flags; // the construction flags the logic sets (BUILD_VARIATION_ONE: the plot's BuildVariation 1, RW 0x858701)
	const char *model;
	const char *clip;
	int frames; // the animation header (W3D chunk 0x201 / 0x281 NumFrames), read from the files by hand
};

// the first Draw module of each structure (the INI quoted in the brief's survey: AnimationState ACTIVELY_BEING_CONSTRUCTED PARTIALLY_CONSTRUCTED, AnimationMode MANUAL,
// Flags START_FRAME_FIRST, StateName BeingConstructed; the fortress expansion's states also name BUILD_VARIATION_ONE / TWO)
const Expected kFactions[] = {
	{ "GondorBarracks", "ACTIVELY_BEING_CONSTRUCTED PARTIALLY_CONSTRUCTED", "GBBarracks_A", "GBBarracks_ASKL.GBBarracks_ABLD", 1000 },          // Men
	{ "ElvenBarracks", "ACTIVELY_BEING_CONSTRUCTED PARTIALLY_CONSTRUCTED", "NBElvnBarx_A", "NBElvnBarx_ASKL.NBElvnBarx_ABLD", 1000 },           // Elves
	{ "DwarfBarracks", "ACTIVELY_BEING_CONSTRUCTED PARTIALLY_CONSTRUCTED", "EBBarracks_ASKN", "EBBarracks_ASKL.EBBarracks_ABLD", 500 },         // Dwarves (compressed, skinned)
	{ "IsengardUrukPit", "ACTIVELY_BEING_CONSTRUCTED PARTIALLY_CONSTRUCTED", "IBUrukPit_A", "IBUrukPit_ASKL.IBUrukPit_ABLD", 1000 },            // Isengard
	{ "MordorOrcPit", "ACTIVELY_BEING_CONSTRUCTED PARTIALLY_CONSTRUCTED", "MBOrcpit_A", "MBOrcpit_ASKL.MBOrcpit_ABLD", 500 },                   // Mordor
	{ "GoblinCave", "ACTIVELY_BEING_CONSTRUCTED PARTIALLY_CONSTRUCTED", "WBCave_ASKN", "WBCave_ASKL.WBCave_ABLD", 1001 },                       // Wild (skinned)
	{ "AngmarBarracks", "ACTIVELY_BEING_CONSTRUCTED PARTIALLY_CONSTRUCTED", "KBHall_A", "KBHall_ASKL.KBHall_ABLD", 1001 },                      // Angmar
	{ "MenArrowTowerExpansion", "ACTIVELY_BEING_CONSTRUCTED PARTIALLY_CONSTRUCTED BUILD_VARIATION_ONE", "GBFARTOWA_A", "GBFARTOWA_ASKL.GBFARTOWA_ABLD", 1000 },
	{ "MenArrowTowerExpansion", "ACTIVELY_BEING_CONSTRUCTED PARTIALLY_CONSTRUCTED BUILD_VARIATION_TWO", "GBFARTOWB_A", "GBFARTOWB_ASKL.GBFARTOWB_ABLD", 1000 },
};

bool sameNoCase(const std::string &a, const std::string &b) { return AsciiStringUtil::compareNoCase(a, b) == 0; }

std::unique_ptr<Site> start(retailtest::Mount &mount, const char *object)
{
	const RetailDrawModule *m = nullptr;
	for (const RetailDrawModule &x : retailDrawScan().modules)
	{
		if (!m && x.data && x.object == object && x.className == "W3DScriptedModelDraw")
		{
			m = &x; // the first Draw module of the object: the building itself
		}
	}
	REQUIRE_MESSAGE(m != nullptr, object);
	std::unique_ptr<Site> s(new Site(mount));
	W3DScriptedModelDraw::Options o;
	o.buildBones = false;
	s->draw.reset(new W3DScriptedModelDraw(*m->data, s->drawAssets, s->random, &s->host, o));
	return s;
}

ModelConditionFlags flagsFrom(const char *names)
{
	Harness h;
	const ModelConditionFlags f = h.parseFlags(names);
	REQUIRE_MESSAGE(h.error.empty(), h.error);
	return f;
}
} // namespace

TEST_CASE("render3 retail: each faction's structure selects its build-up model and MANUAL animation while built, frame 0 waiting; the finished model after")
{
	retailtest::Mount *mount = retailtest::pureMount();
	if (!mount)
	{
		retailtest::printSkip("render3 retail: per-faction construction states");
		return;
	}
	REQUIRE_MESSAGE(mount->fs, mount->error);
	for (const Expected &x : kFactions)
	{
		CAPTURE(x.object);
		CAPTURE(x.flags);
		std::unique_ptr<Site> s = start(*mount, x.object);
		s->draw->setModelConditionFlags(flagsFrom(x.flags));
		const Look l = s->look();
		CHECK(sameNoCase(l.model, x.model));
		CHECK(sameNoCase(l.clip, x.clip));
		CHECK(l.frames == x.frames);
		CHECK_FALSE(l.adjustHeight); // the build-up states never sink the model (RW 0x4B686D: only states with the flag)
		CHECK(s->draw->frame().tracks[0].mode == W3D_ANIM_MODE_MANUAL);
		CHECK(s->draw->frame().tracks[0].frame == 0.0f); // START_FRAME_FIRST
		// the frame follows the percent (RW 0x4B51B5) and never runs back
		s->draw->updateConstructionFrame(40.0f, true, 0.0f, 0.0f);
		const volatile float fraction = 40.0f * 0.01f; // one float32 rounding per stored value, as RW 0x4B5232 stores
		const volatile float last = (float)(x.frames - 1);
		CHECK(s->draw->frame().tracks[0].frame == fraction * last);
		CHECK_FALSE(s->draw->updateConstructionFrame(30.0f, true, 0.0f, 0.0f));
		// complete (RW 0x857BDA: the construction bits cleared, CONSTRUCTION_COMPLETE set): the finished model, not the build-up one
		s->draw->setModelConditionFlags(flagsFrom(sameNoCase(x.object, "MenArrowTowerExpansion") ? "CONSTRUCTION_COMPLETE BUILD_VARIATION_ONE" : "CONSTRUCTION_COMPLETE"));
		CHECK_FALSE(sameNoCase(s->look().model, x.model));
	}
}

TEST_CASE("render3 retail: GondorBarracks' build-up frame at 0 / 25 / 50 / 75 / 100 % is percent * 0.01 * 999 (RW 0x4B51B5), the sub-frame term adds alpha * rate")
{
	retailtest::Mount *mount = retailtest::pureMount();
	if (!mount)
	{
		retailtest::printSkip("render3 retail: GondorBarracks build-up frames");
		return;
	}
	REQUIRE_MESSAGE(mount->fs, mount->error);
	std::unique_ptr<Site> s = start(*mount, "GondorBarracks");
	s->draw->setModelConditionFlags(flagsFrom("ACTIVELY_BEING_CONSTRUCTED PARTIALLY_CONSTRUCTED"));
	// GBBarracks_ABLD has 1000 frames: the last is 999; by hand 0, 249.75, 499.5, 749.25, 999 (each a float32 product of the float32 fraction and 999)
	const float expect[5] = { 0.0f, 249.75f, 499.5f, 749.25f, 999.0f };
	for (int i = 0; i < 5; ++i)
	{
		s->draw->updateConstructionFrame(25.0f * (float)i, true, 0.0f, 0.0f);
		CHECK(s->draw->frame().tracks[0].frame == expect[i]);
	}
	// a fresh site: 50 % with a build rate of 1/150 per logic frame at alpha 0.5: (0.5 / 150 + 0.5) * 999 = 502.83
	std::unique_ptr<Site> t = start(*mount, "GondorBarracks");
	t->draw->setModelConditionFlags(flagsFrom("ACTIVELY_BEING_CONSTRUCTED PARTIALLY_CONSTRUCTED"));
	t->draw->updateConstructionFrame(50.0f, true, 1.0f / 150.0f, 0.5f);
	CHECK(t->draw->frame().tracks[0].frame == doctest::Approx(502.83f).epsilon(1e-4));
}

TEST_CASE("render3 retail: the build-up BeginScript's HideSubObjectPermanently(V1 / V2) is recorded on the build-up model and hides the finished model's V1 / V2 (RW 0x4C3B25)")
{
	retailtest::Mount *mount = retailtest::pureMount();
	if (!mount)
	{
		retailtest::printSkip("render3 retail: permanent hides across the model change");
		return;
	}
	REQUIRE_MESSAGE(mount->fs, mount->error);
	auto hiddenEnding = [](const Look &l, const char *suffix) {
		for (const std::string &h : l.hidden)
		{
			const std::string lower = AsciiStringUtil::lowered(h);
			const std::string want = AsciiStringUtil::lowered(suffix);
			if (lower.size() >= want.size() && lower.compare(lower.size() - want.size(), want.size(), want) == 0)
			{
				return true;
			}
		}
		return false;
	};
	// GondorBarracks: the BeingConstructed BeginScript hides V1, V1FLAG and V2 permanently; GBBarracks_A has none of them, GBBarracks_SKN has V1 and V2
	std::unique_ptr<Site> s = start(*mount, "GondorBarracks");
	s->draw->setModelConditionFlags(flagsFrom("ACTIVELY_BEING_CONSTRUCTED PARTIALLY_CONSTRUCTED"));
	for (const std::string &e : s->draw->errors())
	{
		CHECK_MESSAGE(e.find("not found") == std::string::npos, e); // retail records the request without looking at the render object
	}
	bool s830 = false;
	for (const W3DStopHit &h : s->draw->stops())
	{
		s830 = s830 || h.Id == "S-830";
	}
	CHECK(s830); // the script's request reached only this draw module: reported
	s->draw->setModelConditionFlags(flagsFrom("CONSTRUCTION_COMPLETE"));
	const Look done = s->look();
	REQUIRE(sameNoCase(done.model, "GBBarracks_SKN"));
	CHECK(hiddenEnding(done, ".V1"));
	CHECK(hiddenEnding(done, ".V2"));
	// V1 / V2 hang on the root bone (HLOD sub object bone 0): RW 0x4B31C9 hides them alone, the building (BARRACKS_STRUCT, bone 17) stays
	CHECK_FALSE(hiddenEnding(done, ".BARRACKS_STRUCT"));
	CHECK_FALSE(hiddenEnding(done, ".SPEAR"));
	CHECK(done.hidden.size() == 2);
	// a later ShowSubObjectPermanently of the same name (the level upgrade) updates the record and shows it
	s->draw->showSubObjectPermanently("V1");
	CHECK_FALSE(hiddenEnding(s->look(), ".V1"));
	CHECK(hiddenEnding(s->look(), ".V2"));
	// and a further model change keeps the updated records
	s->draw->setModelConditionFlags(flagsFrom("DAMAGED"));
	s->draw->setModelConditionFlags(flagsFrom("CONSTRUCTION_COMPLETE"));
	CHECK_FALSE(hiddenEnding(s->look(), ".V1"));
	CHECK(hiddenEnding(s->look(), ".V2"));
}

TEST_CASE("render3 retail: ADJUST_HEIGHT_BY_CONSTRUCTION_PERCENT is on the walls' idle states only, never on a construction state")
{
	retailtest::Mount *mount = retailtest::pureMount();
	if (!mount)
	{
		retailtest::printSkip("render3 retail: the height adjustment states");
		return;
	}
	REQUIRE_MESSAGE(mount->fs, mount->error);
	std::set<std::string> objects;
	int construction = 0;
	for (const RetailDrawModule &m : retailDrawScan().modules)
	{
		if (!m.data)
		{
			continue;
		}
		for (const AnimationStateInfo &a : m.data->m_animationStates)
		{
			if (a.testFlag(W3D_ACF_ADJUST_HEIGHT_BY_CONSTRUCTION_PERCENT))
			{
				objects.insert(m.object);
				construction += hasConstructionBit(a.conditions) ? 1 : 0;
				CHECK_MESSAGE(a.kind == AnimationStateInfo::KIND_IDLE, m.object);
			}
		}
	}
	CHECK(construction == 0);
	CHECK(objects.count("GondorCastleWallHub") == 1);
	CHECK(objects.count("DwarvenCastleWallSegment") == 1);
	CHECK(objects.count("GondorBarracks") == 0);
	CHECK(objects.count("GondorFarm") == 0); // FarmInterface: "Flags = START_FRAME_FIRST ;;M Lo Sez 'no' ADJUST_HEIGHT..." - the comment is not a flag
	std::printf("  info: %zu objects carry ADJUST_HEIGHT_BY_CONSTRUCTION_PERCENT on an idle state\n", objects.size());
}

// ---------------------------------------------------------------------------------------------------------------------
// Live: the build-up frame between logic frames (the owner's presentation policy: fluid at any client rate; the logic percent stays retail's)
// ---------------------------------------------------------------------------------------------------------------------
namespace
{
GameMessage r3Select(int player, ObjectID id)
{
	GameMessage m(MSG_CREATE_SELECTED_GROUP, player);
	m.appendBooleanArgument(true);
	m.appendObjectIDArgument(id);
	return m;
}
GameMessage r3Construct(int player, const ThingTemplate &t, const Coord3D &loc)
{
	GameMessage m(MSG_DOZER_CONSTRUCT, player);
	m.appendIntegerArgument((int)t.getTemplateID());
	m.appendLocationArgument(loc);
	m.appendRealArgument(0.0f);
	return m;
}
} // namespace

TEST_CASE("render3 live: a Porter builds GondorBarracks - at 12 client frames per logic frame the build-up frame rises in even steps, never back, no jump at the logic frames")
{
	OPENBFME_REQUIRE_START(s);
	const auto worldContext = s->world->enterContext();
	std::string error;
	buildtest::Game g;
	REQUIRE_MESSAGE(buildtest::startGame(*s, "FactionMen", "FactionMen", 5150, g, &error), error);
	LiveGame &live = *g.live;
	GameLogic &logic = live.logic();
	Player *player = live.players().findPlayerWithName(live.report().startSlotPlayers[0]);
	const int pi = player->getPlayerIndex();
	const ThingTemplate *tt = logic.things().findTemplate("GondorBarracks")->getFinalOverride();
	Object *centre = nullptr;
	for (const StartingBase::Placed &p : live.report().startingObjects)
	{
		Object *o = p.structure ? logic.findObjectByID(p.id) : nullptr;
		if (o && o->getControllingPlayer() == player)
		{
			centre = o;
		}
	}
	Object *porter = nullptr;
	for (Object *o = logic.getFirstObject(); o && !porter; o = o->getNextObject())
	{
		if (o->getControllingPlayer() == player && o->isKindOfName("DOZER") && dynamic_cast<DozerAIUpdate *>(o->getAIUpdateInterface()) &&
			BuildAssistant::canMakeUnit(*o, tt, -1) == CANMAKE_OK)
		{
			porter = o;
		}
	}
	REQUIRE(centre);
	REQUIRE(porter);
	Coord3D site = *centre->getPosition();
	bool found = false;
	for (float r = 180.0f; r <= 520.0f && !found; r += 40.0f)
	{
		for (int k = 0; k < 16 && !found; ++k)
		{
			Coord3D c = *centre->getPosition();
			c.x += r * std::cos((float)k * 0.3927f);
			c.y += r * std::sin((float)k * 0.3927f);
			c.z = logic.getGroundHeight(c.x, c.y);
			if (BuildPlacement::isLocationLegalToBuild(logic, c, *tt, 0.0f, LLF_TERRAIN_RESTRICTIONS | LLF_NO_OBJECT_OVERLAP, porter, player) == LBC_OK)
			{
				site = c;
				found = true;
			}
		}
	}
	REQUIRE(found);
	live.commands().append(r3Select(pi, porter->getID()));
	live.commands().append(r3Construct(pi, *tt, site));
	const int kClient = 12;
	std::vector<float> frames;    // the build-up frame after every client frame while BeingConstructed
	std::vector<bool> logicStart; // the client frame is the first one of a logic frame
	ObjectID id = INVALID_ID;
	for (int f = 0; f < 2400; ++f)
	{
		logic.runLogicFrame();
		if (id == INVALID_ID)
		{
			for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
			{
				if (o->getTemplate() == tt && o->getControllingPlayer() == player)
				{
					id = o->getID();
				}
			}
		}
		Object *b = id != INVALID_ID ? logic.findObjectByID(id) : nullptr;
		if (b && !b->isUnderConstruction() && !frames.empty())
		{
			break;
		}
		for (int c = 0; c < kClient; ++c)
		{
			// SMOOTH-1: the client follows the logic through its snapshot and ordered events (refreshClient publishes the frame the test ran)
			if (c == 0)
			{
				live.refreshClient(1000.0 / (5.0 * kClient), 0.0);
			}
			else
			{
				live.drawables().advance(1000.0 / (5.0 * kClient));
				live.drawables().syncTransforms(*live.presentedSnapshot(), (double)c / kClient, live.renderInterpolation());
			}
			const Drawable *d = b ? live.drawables().findByObject(b->getID()) : nullptr;
			if (!d)
			{
				continue;
			}
			for (const DrawEntry &e : d->entries())
			{
				if (e.kind == W3D_DRAWKIND_MODEL && e.draw)
				{
					const W3DDrawFrame fr = e.draw->frame();
					if (fr.animationStateName == "BeingConstructed" && fr.trackCount > 0 && b->getConstructionPercent() > 0.0f)
					{
						frames.push_back(fr.tracks[0].frame);
						logicStart.push_back(c == 0);
					}
					break;
				}
			}
		}
	}
	REQUIRE(frames.size() > (size_t)(10 * kClient));
	// steps between consecutive client frames (the last logic frame's samples excluded: the clamp at the end)
	double sum = 0.0, maxStep = 0.0, maxAtLogic = 0.0;
	int n = 0;
	bool monotonic = true;
	for (size_t i = 1; i + kClient < frames.size(); ++i)
	{
		const double step = (double)frames[i] - (double)frames[i - 1];
		monotonic = monotonic && step >= 0.0;
		sum += step;
		++n;
		maxStep = std::max(maxStep, step);
		if (logicStart[i])
		{
			maxAtLogic = std::max(maxAtLogic, step);
		}
	}
	const double mean = sum / n;
	std::printf("  info: GondorBarracks build-up: %zu client samples, mean step %.4f frames, max %.4f, max at a logic frame start %.4f\n", frames.size(), mean, maxStep,
		maxAtLogic);
	CHECK(monotonic);
	CHECK(mean > 0.0);
	// retail's extrapolation (RW 0x4B51B5: alpha * 1 / calcTimeToBuild + percent * 0.01) meets the next logic frame's percent: no visible jump
	CHECK(maxStep <= mean * 1.5 + 1e-3);
	CHECK(maxAtLogic <= mean * 1.5 + 1e-3);
}

TEST_CASE("render3 gamma pass: a blended W3D surface hands its gamma-space colour to the gamma-space transparent pass, an opaque one converts once (S-831)")
{
	W3DShaderKey k;
	k.Texturing = true;
	k.Tex[0] = true;
	k.PriGradient = W3D_GRADIENT_MODULATE;
	k.Blend = W3D_GODOT_BLEND_OPAQUE;
	const std::string opaque = Generate_W3D_Shader_Code(k);
	CHECK(opaque.find("ALBEDO = w3d_to_linear(clamp(cur.rgb, 0.0, 1.0));") != std::string::npos);
	for (int b : { (int)W3D_GODOT_BLEND_MIX, (int)W3D_GODOT_BLEND_ADD, (int)W3D_GODOT_BLEND_ADD_ALPHA, (int)W3D_GODOT_BLEND_PREMUL })
	{
		CAPTURE(b);
		k.Blend = b;
		const std::string code = Generate_W3D_Shader_Code(k);
		CHECK(code.find("ALBEDO = clamp(cur.rgb, 0.0, 1.0);") != std::string::npos);
		CHECK(code.find("ALBEDO = w3d_to_linear(") == std::string::npos);
	}
	k.Blend = W3D_GODOT_BLEND_MUL;
	const std::string mul = Generate_W3D_Shader_Code(k);
	CHECK(mul.find("ALBEDO = mix(vec3(1.0), clamp(cur.rgb, 0.0, 1.0), v_fade);") != std::string::npos);
	CHECK(mul.find("ALBEDO = w3d_to_linear(") == std::string::npos);
}
