// OpenBFME unit tests (lane MODULES-2): TheEmotionSystem, EmotionTrackerUpdate and RadiateFearUpdate in a synthetic world.
//
// Expected values follow the binary's steps by hand: the EmotionNugget block and its fields (RW 0x8E0D59 / table RW 0xC77FD0), the request (RW 0x8B4E75), the
// choice (RW 0x8B54DD), the nugget's start / update / stop (RW 0x8E0ED7 / 0x8E11A1 / 0x8E168A), the per-source inactivity (RW 0x8E139A), the quarrel draw
// (RW 0x8B5E18), the fear scan (RW 0x8B5ADC ..), the RadiateFearUpdate pulse (RW 0x89F9A8) and the SCARY / HERO list (RW 0x835D55 / 0x835B0A).

#include "doctest.h"

#include "MoveTestUtil.h"

#include "Common/StateHash.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/Module/EmotionModules.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/System/EmotionSystem.h"

#include <string>
#include <vector>


namespace
{
const char kNuggets[] =
	"EmotionNugget TestFear\n"
	"  Type = FEAR\n"
	"  ModelConditions = EMOTION_AFRAID\n"
	"  Duration = 1000\n"                    // 5 frames
	"  InactiveDurationSameObject = 2000\n"  // 10 frames
	"  AIState = BACK_AWAY\n"
	"End\n"
	"EmotionNugget TestFear\n"               // a second definition is parsed and dropped (RW 0x8E0D59)
	"  Type = TERROR\n"
	"End\n"
	"EmotionNugget TestTerror\n"
	"  CopyFrom = TestFear\n"
	"  Type = TERROR\n"
	"  ModelConditions = EMOTION_TERROR EMOTION_AFRAID\n"
	"  ModelConditionsClear = MOVING\n"
	"  OnlyIfEnemyThreatBelow = 100\n"
	"  OnlyIfFriendThreatAbove = 3\n"
	"  AIState = NOT_A_STATE\n"
	"End\n"
	"EmotionNugget TestOdd\n"
	"  Type = NOT_A_TYPE\n"
	"End\n";

const char kObjects[] =
	"Object Scared\n"
	"  KindOf = SELECTABLE INFANTRY\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 5\n"
	"  GeometryHeight = 10\n"
	"  ShroudClearingRange = 300\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  Behavior = EmotionTrackerUpdate ModuleTag_Emotion\n"
	"    TauntAndPointDistance = 100\n"
	"    TauntAndPointUpdateDelay = 1000\n"
	"    AfraidOf = NONE +Troll\n"
	"    FearScanDistance = 100\n"
	"    AddEmotion = TestTerror\n"
	"    AddEmotion = OVERRIDE TestFear\n"
	"      Duration = 2000\n"
	"    End\n"
	"  End\n"
	"End\n"
	"Object Troll\n"
	"  KindOf = SELECTABLE MONSTER SCARY\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 10\n"
	"  GeometryHeight = 20\n"
	"  Behavior = RadiateFearUpdate ModuleTag_Fear\n"
	"    InitiallyActive = Yes\n"
	"    GenerateFear = Yes\n"
	"    EmotionPulseRadius = 150\n"
	"    EmotionPulseInterval = 1000\n"
	"    VictimFilter = ALL ENEMIES\n"
	"  End\n"
	"End\n"
	"Object Champion\n"
	"  KindOf = SELECTABLE INFANTRY HERO\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 5\n"
	"  GeometryHeight = 10\n"
	"End\n";

struct EmoFx : movetest::MoveWorld
{
	EmotionSystem emotions;
	EmotionSystem *saved = TheEmotionSystem;
	void load(const std::string &text)
	{
		const std::string err = w.load(text, INI_LOAD_OVERWRITE, "emotion.ini");
		REQUIRE_MESSAGE(err.empty(), err);
	}
	EmoFx(bool withObjects = true)
	{
		TheEmotionSystem = &emotions;
		emotions.registerBlock(w.fx.env.blocks);
		load(kNuggets);
		if (withObjects)
		{
			load(kObjects);
		}
		buildMap();
		logic->partition().setRegion(0.0f, 0.0f, 2000.0f, 2000.0f);
	}
	~EmoFx() { TheEmotionSystem = saved; }
	Object *at(const char *name, const char *owner, float x, float y)
	{
		Object *o = make(name, teamOf(owner));
		REQUIRE(o);
		const Coord3D p{ x, y, 0.0f };
		o->setPosition(&p);
		return o;
	}
	static int mc(const char *name)
	{
		for (int i = 0; TheModelConditionNames[i]; ++i)
		{
			if (std::string(TheModelConditionNames[i]) == name)
			{
				return i;
			}
		}
		return -1;
	}
};
} // namespace

TEST_CASE("emotions: the EmotionNugget block keeps the first definition, CopyFrom copies, ModelConditions also fill the exit flags, the threat pairs (RW 0x8E0D59)")
{
	EmoFx f(false);
	REQUIRE(f.emotions.nuggets().size() == 3);
	const EmotionNuggetTemplate *fear = f.emotions.find("TestFear");
	REQUIRE(fear);
	CHECK(fear->type == EMOTION_FEAR); // the second definition (TERROR) was dropped
	CHECK(fear->duration == 5);
	CHECK(fear->inactiveDurationSameObject == 10);
	CHECK(fear->aiState == EMOTION_AI_BACK_AWAY);
	CHECK(fear->modelConditions == fear->modelConditionsClearOnExit); // RW 0x8E092B
	const EmotionNuggetTemplate *terror = f.emotions.find("TestTerror");
	REQUIRE(terror);
	CHECK(terror->type == EMOTION_TERROR);
	CHECK(terror->duration == 5); // copied
	CHECK(terror->inactiveDurationSameObject == 10);
	CHECK(terror->aiState == -1); // an unknown AI state name is -1 (RW 0x8E0983)
	CHECK(terror->modelConditionsClear == terror->modelConditionsSetOnExit);
	CHECK(terror->enemyThreatScale == 1);
	CHECK(terror->enemyThreatOffset == -100);
	CHECK(terror->friendThreatScale == -1);
	CHECK(terror->friendThreatOffset == 3);
	CHECK(fear->enemyThreatScale == 0);
	CHECK(fear->enemyThreatOffset == -1);
	CHECK(f.emotions.find("TestOdd")->type == -1);
	// AddEmotion of a nugget that does not exist is an error
	CHECK_FALSE(f.w.load("Object Bad\n  Behavior = EmotionTrackerUpdate ModuleTag_E\n    AddEmotion = Nope\n  End\nEnd\n").empty());
}

TEST_CASE("emotions: a request starts the nugget (model conditions), it runs Duration frames, then the same source is refused for InactiveDurationSameObject")
{
	EmoFx f;
	Object *s = f.at("Scared", "Alice", 500.0f, 500.0f);
	Object *src = f.at("Champion", "Bob", 900.0f, 900.0f); // far away: no scan finds it
	Object *src2 = f.at("Champion", "Bob", 950.0f, 900.0f);
	EmotionTrackerUpdate *t = EmotionTrackerUpdate::of(*s);
	REQUIRE(t);
	REQUIRE(t->nuggets().size() == 2);
	CHECK(t->nuggets()[0]->tmpl().name == "TestTerror");
	CHECK_FALSE(t->nuggets()[0]->tmpl().overridden); // the system's nugget
	CHECK(t->nuggets()[1]->tmpl().overridden);       // the OVERRIDE copy: Duration 2000 ms
	CHECK(t->nuggets()[1]->tmpl().duration == 10);
	f.frames(1);
	const int afraid = EmoFx::mc("EMOTION_AFRAID");
	REQUIRE(afraid >= 0);
	CHECK_FALSE(s->testModelCondition(afraid));
	EmotionTrackerUpdate::requestEmotion(*s, EMOTION_FEAR, src, 1);
	CHECK(t->requested(EMOTION_FEAR));
	f.frames(1);
	const UnsignedInt started = f.logic->getFrame();
	REQUIRE(t->current());
	CHECK(t->current()->tmpl().name == "TestFear");
	CHECK(t->current()->sourceID() == src->getID());
	CHECK(t->current()->endFrame() == started + 10);
	CHECK(s->testModelCondition(afraid));
	CHECK(s->getAIUpdateInterface()->stateMachine().temporaryStateId() == 48u); // BACK_AWAY: RW 0x662FC8 runs state 48 (lane MODULES-3)
	CHECK(f.logic->emotions().unportedAI() == 0);
	CHECK_FALSE(t->requested(EMOTION_FEAR));             // the request ran out (end = request frame + 1)
	f.frames(9);
	CHECK(t->current());
	f.frames(1);
	CHECK_FALSE(t->current());
	CHECK_FALSE(s->testModelCondition(afraid));
	// the same source within 10 frames: refused; another source: taken
	EmotionTrackerUpdate::requestEmotion(*s, EMOTION_FEAR, src, 1);
	f.frames(1);
	CHECK_FALSE(t->current());
	EmotionTrackerUpdate::requestEmotion(*s, EMOTION_FEAR, src2, 1);
	f.frames(1);
	REQUIRE(t->current());
	CHECK(t->current()->sourceID() == src2->getID());
}

TEST_CASE("emotions: every tracker draws the quarrel number every frame (GameLogicRandomValueReal(0, 1) line 0x25E, RW 0x8B5E18)")
{
	EmoFx f;
	f.at("Scared", "Alice", 500.0f, 500.0f);
	f.at("Scared", "Alice", 700.0f, 500.0f);
	f.frames(1);
	f.logic->random().enableCallLog(true);
	f.frames(3);
	int draws = 0;
	for (const auto &c : f.logic->random().callLog())
	{
		if (c.file == "EmotionTrackerUpdate.cpp" && c.line == 0x25E)
		{
			++draws;
		}
	}
	CHECK(draws == 6);
}

TEST_CASE("emotions: RadiateFearUpdate pulses FEAR at the enemies within its radius every (id % 5) + interval frames; the fear scan finds a SCARY object it is afraid of")
{
	EmoFx f;
	Object *near = f.at("Scared", "Alice", 500.0f, 600.0f);   // 100 from the troll
	Object *far = f.at("Scared", "Alice", 500.0f, 700.0f);    // 200: out of the pulse
	Object *friendOf = f.at("Scared", "Bob", 500.0f, 560.0f); // the troll's own side
	Object *troll = f.at("Troll", "Bob", 500.0f, 500.0f);
	RadiateFearUpdate *r = nullptr;
	for (const auto &m : troll->modules())
	{
		r = r ? r : dynamic_cast<RadiateFearUpdate *>(m.get());
	}
	REQUIRE(r);
	CHECK(r->isAlreadyUpgraded()); // InitiallyActive
	// the troll is SCARY: it is on TheEmotionSystem's list
	CHECK(f.logic->emotions().scaryOrHeroes() == std::vector<ObjectID>{ troll->getID() });
	f.frames(2);
	CHECK(r->pulses() >= 1);
	EmotionTrackerUpdate *tn = EmotionTrackerUpdate::of(*near);
	EmotionTrackerUpdate *tf = EmotionTrackerUpdate::of(*far);
	EmotionTrackerUpdate *tb = EmotionTrackerUpdate::of(*friendOf);
	REQUIRE(tn);
	REQUIRE(tf);
	REQUIRE(tb);
	CHECK(tn->current());
	CHECK(tn->current()->tmpl().type == EMOTION_FEAR);
	CHECK(tn->current()->sourceID() == troll->getID());
	CHECK_FALSE(tf->current());
	CHECK_FALSE(tb->current());
	CHECK(r->requests() >= 1);
	// the pulses come every (id % 5) + 5 frames
	const unsigned p0 = r->pulses();
	f.frames((int)(troll->getID() % 5u) + 5);
	CHECK(r->pulses() == p0 + 1);
}

TEST_CASE("emotions: the SCARY / HERO list loses a dead member by swapping the last into its place (RW 0x835B0A); the emotion state is hashed")
{
	EmoFx f;
	Object *a = f.at("Troll", "Bob", 100.0f, 100.0f);
	Object *b = f.at("Champion", "Bob", 200.0f, 100.0f);
	Object *c = f.at("Troll", "Bob", 300.0f, 100.0f);
	CHECK(f.logic->emotions().scaryOrHeroes() == std::vector<ObjectID>{ a->getID(), b->getID(), c->getID() });
	a->kill(0);
	CHECK(f.logic->emotions().scaryOrHeroes() == std::vector<ObjectID>{ c->getID(), b->getID() });
	Object *s = f.at("Scared", "Alice", 900.0f, 900.0f);
	f.frames(1);
	const std::uint32_t h0 = f.logic->computeStateHash();
	EmotionTrackerUpdate::requestEmotion(*s, EMOTION_FEAR, b, 1);
	CHECK(f.logic->computeStateHash() != h0);
}

TEST_CASE("emotions: stops S-1021 / S-1022 are in the logic report")
{
	EmoFx f;
	const GameLogic::Report r = f.logic->report();
	bool s1021 = false, s1022 = false;
	for (const std::string &l : r.stops)
	{
		s1021 = s1021 || l.rfind("[S-1021]", 0) == 0;
		s1022 = s1022 || l.rfind("[S-1022]", 0) == 0;
	}
	CHECK(s1021);
	CHECK(s1022);
}

TEST_CASE("emotions: RW 0x6634BF's edge distance at a radius-100 boundary (Sol review): PC24 radius subtractions give exactly 100, which retail's scan rejects")
{
	// centres 153 apart in PC24 (the double root 152.9999936 rounds there after the radius subtractions), radii 3 and 50: retail 100.0, binary64 99.9999924
	const Coord3D a{ 0.7649999856948853f, 152.99807739257812f, 0.0f };
	const Coord3D b{ 0.0f, 0.0f, 0.0f };
	const float d2 = CombatQueries::edgeDistanceSquared2D(a, 3.0f, b, 50.0f);
	CHECK(d2 == 10000.0f);
	CHECK_FALSE(d2 < 100.0f * 100.0f); // the scan's "within 100" test
	// overlap: 0, and a NaN-free clamp only for negative wide distances
	CHECK(CombatQueries::edgeDistanceSquared2D(b, 3.0f, b, 50.0f) == 0.0f);
	const Coord3D c{ 60.0f, 0.0f, 0.0f };
	CHECK(CombatQueries::edgeDistanceSquared2D(c, 3.0f, b, 50.0f) == 49.0f);
}
