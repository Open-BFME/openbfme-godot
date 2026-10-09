// OpenBFME. RENDER-2 tests (file name: runs next to the BUILD-1 retail tests while their start world is current): the construction look. The build-up frame of a MANUAL animation from the construction percent (RW 0x4B51B5) and the height offset of an
// ADJUST_HEIGHT_BY_CONSTRUCTION_PERCENT state (RW 0x4B686D) on a synthetic module, worked out by hand; then a retail GondorBarracks built by a Porter through the lockstep
// commands: the model condition states the building shows while it waits, rises and stands (GBBarracks_A + GBBarracks_ABLD, then GBBarracks_SKN), the build-up frame at
// 0 / 25 / 50 / 75 / 100 percent, the foundation floor (W3DFloorDraw HideIfModelConditions) hidden while it is built, and the logic hashes untouched by the client.
#include "doctest.h"
#include "BuildTestUtil.h"
#include "W3DDrawTestUtil.h"
#include "W3dSynth.h"

#include "Common/AsciiString.h"
#include "Common/BuildAssistant.h"
#include "Common/Player.h"
#include "GameClient/Drawable.h"
#include "GameClient/DrawableManager.h"
#include "GameLogic/BuildPlacement.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Module/ConstructionModules.h"
#include "GameLogic/Module/DozerAIUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DScriptedModelDraw.h"
#include "Libraries/WWVegas/WW3D2/hanim.h"

#include <cmath>
#include <cstdio>
#include <deque>
#include <map>
#include <string>
#include <vector>

using namespace w3dsynth;

namespace
{
class NoRandom : public W3DDrawRandom
{
public:
	int value(int lo, int) override { return lo; }
	float real(float lo, float) override { return lo; }
	std::vector<std::string> unverified() const override { return { "S-093: test double source" }; }
};

// a one bone model R2BLD_SKN with an 11 frame build-up animation R2BLD_ABLD (frames 0 .. 10)
struct Site
{
	MemoryFileSource fs;
	std::unique_ptr<WW3DAssetManager> assets;
	std::unique_ptr<WW3DDrawAssets> drawAssets;
	drawtest::Harness harness;
	NoRandom random;
	W3DModelDrawModuleData *data = nullptr;
	std::unique_ptr<W3DScriptedModelDraw> draw;

	explicit Site(const std::string &body)
	{
		fs.Add("art\\w3d\\r2\\r2bld_skl.w3d", hierarchyChunk("R2BLD_SKL", { pivot("ROOTTRANSFORM", 0xFFFFFFFFu, 0, 0, 0), pivot("BASE", 0, 0, 0, 0) }));
		std::vector<std::uint8_t> bytes;
		SynthMesh m;
		m.Vertices = { { 0, 0, 0 }, { 1, 0, 0 }, { 1, 1, 0 } };
		m.Triangles = { 0, 1, 2 };
		m.Container = "R2BLD_SKN";
		m.Name = "BODY";
		append(bytes, meshChunk(m));
		append(bytes, hlodChunk("R2BLD_SKN", "R2BLD_SKL", { { "R2BLD_SKN.BODY", 1 } }));
		fs.Add("art\\w3d\\r2\\r2bld_skn.w3d", bytes);
		fs.Add("art\\w3d\\r2\\r2bld_abld.w3d", rawAnimChunk("R2BLD_ABLD", "R2BLD_SKL", 11, 30, { { ANIM_CHANNEL_Z, 1, 0, 1, { 0 } } }));
		assets.reset(new WW3DAssetManager(fs));
		drawAssets.reset(new WW3DDrawAssets(*assets));
		data = harness.parse(body);
		REQUIRE_MESSAGE(data, harness.error);
		W3DScriptedModelDraw::Options o;
		o.buildBones = false;
		draw.reset(new W3DScriptedModelDraw(*data, *drawAssets, random, nullptr, o));
	}
	void flags(const char *names)
	{
		const ModelConditionFlags f = harness.parseFlags(names);
		REQUIRE_MESSAGE(harness.error.empty(), harness.error);
		draw->setModelConditionFlags(f);
	}
	float frame() const { return draw->frame().tracks[0].frame; }
};

const char *kBuildUp =
	"DefaultModelConditionState\n  Model = R2BLD_SKN\nEnd\n"
	"AnimationState = ACTIVELY_BEING_CONSTRUCTED PARTIALLY_CONSTRUCTED\n"
	"  Animation = Bld\n    AnimationName = R2BLD_SKL.R2BLD_ABLD\n    AnimationMode = MANUAL\n    AnimationBlendTime = 0\n  End\n"
	"  Flags = START_FRAME_FIRST\n"
	"End\n"
	"AnimationState = AWAITING_CONSTRUCTION\n"
	"  Animation = Bld\n    AnimationName = R2BLD_SKL.R2BLD_ABLD\n    AnimationMode = MANUAL\n    AnimationBlendTime = 0\n  End\n"
	"  Flags = START_FRAME_FIRST ADJUST_HEIGHT_BY_CONSTRUCTION_PERCENT\n"
	"End\n"
	"End\n";
} // namespace

TEST_CASE("render2 construction: the build-up frame is percent * 0.01 * (frames - 1), never running back, only while ACTIVELY_BEING_CONSTRUCTED (RW 0x4B51B5)")
{
	Site s(kBuildUp);
	s.flags("ACTIVELY_BEING_CONSTRUCTED PARTIALLY_CONSTRUCTED");
	REQUIRE(s.draw->frame().tracks[0].anim != nullptr);
	CHECK(s.frame() == 0.0f); // START_FRAME_FIRST
	// 0 / 25 / 50 / 75 / 100 percent, no sub-frame: 10 frames * p / 100
	const float expect[5] = { 0.0f, 2.5f, 5.0f, 7.5f, 10.0f };
	for (int i = 0; i < 5; ++i)
	{
		s.draw->updateConstructionFrame(25.0f * (float)i, true, 0.0f, 0.0f);
		CHECK(s.frame() == expect[i]);
	}
	bool reported = false;
	for (const W3DStopHit &h : s.draw->stops())
	{
		reported = reported || h.Id == "S-461";
	}
	CHECK(reported); // the inferred parts are reported
	// beyond 100 (or a large sub-frame term) the fraction is clamped to 1
	s.draw->updateConstructionFrame(140.0f, true, 0.0f, 0.0f);
	CHECK(s.frame() == 10.0f);
	// a lower percent never moves the frame back (damage during construction lowers the percent, RW 0x4B529A compares)
	s.draw->updateConstructionFrame(60.0f, true, 0.0f, 0.0f);
	CHECK(s.frame() == 10.0f);
	// a percent of exactly 0 resets frame and previous frame
	CHECK(s.draw->updateConstructionFrame(0.0f, true, 0.0f, 0.0f));
	CHECK(s.frame() == 0.0f);
	CHECK(s.draw->frame().tracks[0].prevFrame == 0.0f);
	// the sub-frame term: 50 % plus alpha 0.5 of a rate of 0.1 per logic frame: (0.05 + 0.5) ... the fraction is alpha * rate + percent * 0.01 = 0.55
	s.draw->updateConstructionFrame(50.0f, true, 0.1f, 0.5f);
	CHECK(s.frame() == doctest::Approx(5.5f).epsilon(1e-6));
	// the percent is cached: without a refresh the new value is not read (RW 0x63252F refreshes on the first client frame of a logic frame)
	s.draw->updateConstructionFrame(90.0f, false, 0.1f, 0.5f);
	CHECK(s.frame() == doctest::Approx(5.5f).epsilon(1e-6));
	CHECK(s.draw->cachedConstructionPercent() == 50.0f);
	// without ACTIVELY_BEING_CONSTRUCTED in the draw's flags nothing moves (RW 0x4B51BE tests +0x184 bit 5 = flag 0x45)
	s.flags("PARTIALLY_CONSTRUCTED");
	const float before = s.frame();
	CHECK_FALSE(s.draw->updateConstructionFrame(90.0f, true, 0.0f, 0.0f));
	CHECK(s.frame() == before);
}

TEST_CASE("render2 construction: ADJUST_HEIGHT_BY_CONSTRUCTION_PERCENT sinks the model by height * (alpha * rate + percent) * 0.01 - height (RW 0x4B686D)")
{
	Site s(kBuildUp);
	s.flags("AWAITING_CONSTRUCTION");
	REQUIRE(s.draw->adjustsHeightByConstruction());
	CHECK(s.draw->constructionHeightOffset(25.0f, true, 0.0f, 0.0f, 40.0f) == -30.0f);
	CHECK(s.draw->constructionHeightOffset(100.0f, true, 0.0f, 0.0f, 40.0f) == 0.0f);
	CHECK(s.draw->constructionHeightOffset(0.0f, true, 0.0f, 0.0f, 40.0f) == -40.0f);
	// retail adds the fraction rate to the PERCENT here (the sub-frame term is a hundredth of what RW 0x4B51B5 uses): 40 * (0.5 * 0.1 + 25) * 0.01 - 40
	CHECK(s.draw->constructionHeightOffset(25.0f, true, 0.1f, 0.5f, 40.0f) == doctest::Approx(40.0f * 25.05f * 0.01f - 40.0f));
	// an instant build's percent of -1: no offset
	CHECK(s.draw->constructionHeightOffset(-1.0f, true, 0.0f, 0.0f, 40.0f) == 0.0f);
	// a state without the flag: no offset
	s.flags("ACTIVELY_BEING_CONSTRUCTED PARTIALLY_CONSTRUCTED");
	CHECK_FALSE(s.draw->adjustsHeightByConstruction());
	CHECK(s.draw->constructionHeightOffset(25.0f, true, 0.0f, 0.0f, 40.0f) == 0.0f);
}

// ---------------------------------------------------------------------------------------------------------------------
// Retail: a Porter builds GondorBarracks
// ---------------------------------------------------------------------------------------------------------------------
namespace
{
GameMessage selectMsg(int player, ObjectID id)
{
	GameMessage m(MSG_CREATE_SELECTED_GROUP, player);
	m.appendBooleanArgument(true);
	m.appendObjectIDArgument(id);
	return m;
}
GameMessage dozerMsg(int player, const ThingTemplate &t, const Coord3D &loc)
{
	GameMessage m(MSG_DOZER_CONSTRUCT, player);
	m.appendIntegerArgument((int)t.getTemplateID());
	m.appendLocationArgument(loc);
	m.appendRealArgument(0.0f);
	return m;
}

struct Sample
{
	int frame = 0;
	float percent = -1.0f;
	std::string model, state;
	float buildFrame = -1.0f;
	int buildFrames = 0;
	bool floorHidden = false;
	bool floorPresent = false;
	std::string flags;
};

struct BuildRun
{
	std::vector<Sample> samples;
	std::vector<std::uint32_t> hashes;
	std::vector<std::string> problems;
};

BuildRun build(starttest::Shared &s, const char *buildingName, bool client, int maxFrames = 2400)
{
	BuildRun out;
	const auto worldContext = s.world->enterContext(); // this world's process-wide stores (the command sets) whatever test ran before
	std::string error;
	buildtest::Game g;
	if (!buildtest::startGame(s, "FactionMen", "FactionMen", 5150, g, &error))
	{
		out.problems.push_back(error);
		return out;
	}
	LiveGame &live = *g.live;
	GameLogic &logic = live.logic();
	Player *player = live.players().findPlayerWithName(live.report().startSlotPlayers[0]);
	const int pi = player->getPlayerIndex();
	const ThingTemplate *tt = logic.things().findTemplate(buildingName)->getFinalOverride();
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
	if (!centre || !porter)
	{
		out.problems.push_back("no fortress or no porter");
		return out;
	}
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
	if (!found)
	{
		out.problems.push_back("no legal site");
		return out;
	}
	live.commands().append(selectMsg(pi, porter->getID()));
	live.commands().append(dozerMsg(pi, *tt, site));
	ObjectID buildingId = INVALID_ID;
	bool done = false;
	for (int f = 0; f < maxFrames && !done; ++f)
	{
		logic.runLogicFrame();
		out.hashes.push_back(logic.computeStateHash());
		if (client)
		{
			// six client frames per logic frame (30 / 5), the sub-frame fraction 0, 1/6 .. 5/6 (the last one is what the sample sees)
			for (int c = 0; c < 6; ++c)
			{
				live.refreshClient(1000.0 / 30.0, (double)c / 6.0); // SMOOTH-1: the snapshot of the completed frame, its events applied
			}
		}
		if (buildingId == INVALID_ID)
		{
			for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
			{
				if (o->getTemplate() == tt && o->getControllingPlayer() == player)
				{
					buildingId = o->getID();
				}
			}
		}
		Object *b = buildingId != INVALID_ID ? logic.findObjectByID(buildingId) : nullptr;
		if (!b)
		{
			continue;
		}
		Sample smp;
		smp.frame = (int)logic.getFrame();
		smp.percent = b->getConstructionPercent();
		if (const Drawable *d = live.drawables().findByObject(b->getID()))
		{
			for (const DrawEntry &e : d->entries())
			{
				if (e.kind == W3D_DRAWKIND_MODEL && e.draw && smp.model.empty())
				{
					const W3DDrawFrame fr = e.draw->frame();
					smp.model = fr.modelName;
					smp.state = fr.animationStateName;
					smp.buildFrame = fr.trackCount > 0 ? fr.tracks[0].frame : -1.0f;
					smp.buildFrames = (fr.trackCount > 0 && fr.tracks[0].anim) ? fr.tracks[0].anim->Get_Num_Frames() : 0;
				}
				if (!e.hideIf.empty())
				{
					smp.floorPresent = true;
					smp.floorHidden = e.conditionHidden;
				}
			}
		}
		out.samples.push_back(smp);
		done = !b->isUnderConstruction() && smp.percent < 0.0f && out.samples.size() > 5;
	}
	return out;
}
} // namespace

TEST_CASE("render2 construction: a Porter builds GondorBarracks - the waiting, rising and standing looks, the build-up frame at 0/25/50/75/100 %, the floor, and the logic untouched")
{
	OPENBFME_REQUIRE_START(s);
	const BuildRun r = build(*s, "GondorBarracks", true);
	for (const std::string &p : r.problems)
	{
		FAIL_CHECK(p);
	}
	REQUIRE(r.problems.empty());
	REQUIRE(r.samples.size() > 10);
	// waiting for the Porter: AWAITING_CONSTRUCTION -> Model GBBarracks_A, the ABLD animation held at frame 0 (START_FRAME_FIRST, MANUAL); the floor hidden
	const Sample &first = r.samples.front();
	CHECK(AsciiStringUtil::compareNoCase(first.model, "GBBarracks_A") == 0);
	CHECK(first.buildFrame == 0.0f);
	CHECK(first.floorPresent);
	CHECK(first.floorHidden);
	// rising: state BeingConstructed; the frame follows the percent cached on the first client frame of the logic frame plus the sub-frame term of the
	// last client frame (alpha 5/6 of 1 / buildFrames); never backwards
	int rising = 0;
	float lastFrame = -1.0f;
	bool monotonic = true;
	std::map<int, float> atPercent; // 0 / 25 / 50 / 75 / 100 -> frame seen at the first sample at or above it
	int buildFrames = 0;
	for (const Sample &smp : r.samples)
	{
		if (smp.state != "BeingConstructed")
		{
			continue;
		}
		++rising;
		buildFrames = smp.buildFrames;
		CHECK(AsciiStringUtil::compareNoCase(smp.model, "GBBarracks_A") == 0);
		CHECK(smp.floorHidden);
		monotonic = monotonic && smp.buildFrame >= lastFrame;
		lastFrame = smp.buildFrame;
		for (int p = 0; p <= 100; p += 25)
		{
			if (smp.percent >= (float)p && atPercent.count(p) == 0)
			{
				atPercent[p] = smp.buildFrame;
				std::printf("  info: GondorBarracks at %.2f %% (frame %d): GBBarracks_ABLD frame %.3f of %d\n", smp.percent, smp.frame, smp.buildFrame, smp.buildFrames - 1);
			}
		}
	}
	CHECK(rising > 20);
	CHECK(monotonic);
	REQUIRE(buildFrames > 1);
	const float last = (float)(buildFrames - 1);
	for (const auto &kv : atPercent)
	{
		// the sample's percent p0 <= frame / last * 100 <= p0 + one logic frame's share: the fraction is percent * 0.01 + alpha * rate (alpha < 1)
		CHECK(kv.second >= (float)kv.first * 0.01f * last - 1e-3f);
	}
	// standing: GBBarracks_SKN, the floor shown
	const Sample &end = r.samples.back();
	CHECK(end.percent < 0.0f);
	CHECK(AsciiStringUtil::compareNoCase(end.model, "GBBarracks_SKN") == 0);
	CHECK_FALSE(end.floorHidden);
	// the client never writes the logic: the same run without any client frame has the same hashes
	const BuildRun bare = build(*s, "GondorBarracks", false, (int)r.hashes.size());
	REQUIRE(bare.hashes.size() == r.hashes.size());
	CHECK(bare.hashes == r.hashes);
}

TEST_CASE("render2 launch (live report): the launch-bone provider's S-460 stop and its data problems reach LiveGame::report")
{
	OPENBFME_REQUIRE_START(s);
	const auto worldContext = s->world->enterContext();
	std::string error;
	buildtest::Game g;
	REQUIRE_MESSAGE(buildtest::startGame(*s, "FactionMen", "FactionMen", 5150, g, &error), error);
	LiveGame &live = *g.live;
	// no launch yet: no S-460 line
	for (const std::string &line : live.report().stops)
	{
		CHECK(line.rfind("[S-460]", 0) != 0);
	}
	// a valid query (every object of the start) and the defective data of the whole template store (missing models, bones, pose animations)
	int answered = 0;
	for (Object *o = live.logic().getFirstObject(); o; o = o->getNextObject())
	{
		float m[12];
		answered += live.launchBones().launchOffset(*o, 0, 0, m) ? 1 : 0;
	}
	for (const ThingTemplate *t : s->world->things().templates())
	{
		float m[12];
		live.launchBones().getProjectileLaunchOffset(*t, ModelConditionFlags(), 1.0f, 0.0f, 0, 0, m);
	}
	const LiveGame::Report r = live.report();
	std::string s460;
	for (const std::string &line : r.stops)
	{
		if (line.rfind("[S-460]", 0) == 0)
		{
			s460 = line;
		}
	}
	REQUIRE(!s460.empty());
	CHECK(s460.find(std::to_string(live.launchBones().queries()) + " launch queries") != std::string::npos);
	REQUIRE(!r.launchBoneProblems.empty());
	CHECK(r.launchBoneProblems == live.launchBones().problems());
	CHECK(s460.find(std::to_string(r.launchBoneProblems.size()) + " launch-bone data problems") != std::string::npos);
	std::printf("  info: live report: %d of the start's objects name a launch bone; %zu launch-bone data problems in the report\n", answered,
		r.launchBoneProblems.size());
}
