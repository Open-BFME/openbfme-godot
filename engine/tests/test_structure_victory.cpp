// OpenBFME. COMBAT-2 tests: the skirmish victory rules (VictoryConditions). Synthetic data (combattest::CombatWorld), no retail files. Expected frames are derived by hand in the comments:
// the victory update runs in phase 1 of every frame, so a structure that dies in frame d (the frame counter after runLogicFrame) is seen by the update of frame d + 1; nothing is judged
// before frame ftol(5 * SecondsBeforeBaseCheckActive) = 25.
#include "doctest.h"
#include "CombatTestUtil.h"

#include "GameLogic/VictoryConditions.h"

using namespace combattest;

namespace
{
const char kVictoryObjects[] =
	"Object VKeep\n"
	"  KindOf = STRUCTURE IMMOBILE SELECTABLE SCORE\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 20\n"
	"  GeometryMinorRadius = 20\n"
	"  GeometryHeight = 30\n"
	"  Body = StructureBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_Destroy\n"
	"  End\n"
	"End\n"
	// a wall segment: a structure the victory filter does not count (IGNORE_FOR_VICTORY)
	"Object VWall\n"
	"  KindOf = STRUCTURE IMMOBILE IGNORE_FOR_VICTORY\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 10\n"
	"  GeometryMinorRadius = 10\n"
	"  GeometryHeight = 10\n"
	"  Body = StructureBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_Destroy\n"
	"  End\n"
	"End\n"
	"Object VBuilder\n"
	"  KindOf = INFANTRY SELECTABLE DOZER SCORE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 50\n"
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_Destroy\n"
	"  End\n"
	"End\n";

const char kGameData[] =
	"GameData\n"
	"  ForceModelsToFollowTimeOfDay = Yes\n"
	"  ForceModelsToFollowWeather = Yes\n"
	"  UnitDamagedThreshold = 0.5\n"
	"  UnitReallyDamagedThreshold = 0.25\n"
	"  DefaultStartingCash = 1000\n"
	"  VictoryConditionStructureObjectFilter = NONE +STRUCTURE -IGNORE_FOR_VICTORY -UNATTACKABLE\n"
	"  VictoryConditionUnitObjectFilter = ANY -DOZER\n"
	"End\n";

DamageInfo killShot(const Object *source)
{
	DamageInfo d;
	d.m_input.m_sourceID = source ? source->getID() : 0;
	d.m_input.m_amount = 100000.0f;
	d.m_input.m_damageType = DAMAGE_UNRESISTABLE;
	return d;
}

struct VictoryWorld : CombatWorld
{
	VictoryWorld()
		: CombatWorld(kVictoryObjects)
	{
		GameLogicSettings scratch;
		std::string err;
		REQUIRE_MESSAGE(GameLogicSettingsLoader::scanGameData(kGameData, scratch, &err), err);
		logic->settings().victoryRulesLoaded = scratch.victoryRulesLoaded;
		logic->settings().victoryStructureFilter = scratch.victoryStructureFilter;
		logic->settings().victoryUnitFilter = scratch.victoryUnitFilter;
		logic->settings().secondsBeforeBaseCheckActive = scratch.secondsBeforeBaseCheckActive;
		logic->victory().init();
	}
	void kill(Object *o, Object *by) { DamageInfo d = killShot(by); o->attemptDamage(d); }
};
} // namespace

TEST_CASE("victory: GameData's two object filters and the default delay are read (RW rows 0xC00C50, 0xC00C60, 0xC00EF0)")
{
	GameLogicSettings s;
	std::string err;
	REQUIRE_MESSAGE(GameLogicSettingsLoader::scanGameData(kGameData, s, &err), err);
	CHECK(s.victoryRulesLoaded);
	REQUIRE(s.victoryStructureFilter.get() != nullptr);
	REQUIRE(s.victoryUnitFilter.get() != nullptr);
	CHECK(s.secondsBeforeBaseCheckActive == 5.0f); // RW 0x6438FD: the constructor's default, gamedata.ini sets none
}

TEST_CASE("victory: both players are cached, nobody is judged before frame 25, and a game with no defeat has no winner")
{
	VictoryWorld w;
	const VictoryConditions &v = w.logic->victory();
	CHECK(v.cachedPlayers() == 2);
	CHECK(v.localSlot() == 0); // the fixture's local player is the first one (Alice)
	CHECK_FALSE(v.isObserver());
	CHECK(v.rule() == VictoryConditions::RULE_BASE_OR_BUILDERS);
	Object *a = w.unit("VKeep", 'A', 300, 300);
	Object *b = w.unit("VKeep", 'B', 600, 300);
	w.kill(b, a);
	CHECK(b->isEffectivelyDead());
	w.frames(20); // frame 20 < 25: Bob has no structure and no builder but is not judged yet
	CHECK_FALSE(v.isDefeated(1));
	CHECK_FALSE(v.singleAllianceRemaining());
	CHECK_FALSE(w.logic->victory().hasAchievedVictory(w.playerOf('A')));
}

TEST_CASE("victory: a player without a structure of the filter and without a builder is eliminated at the first update from frame 25, the winner is the last alliance")
{
	VictoryWorld w;
	VictoryConditions &v = w.logic->victory();
	Object *a = w.unit("VKeep", 'A', 300, 300);
	Object *b = w.unit("VKeep", 'B', 600, 300);
	Object *soldier = w.unit("Swordsman", 'B', 620, 300);
	Object *wall = w.unit("VWall", 'B', 640, 300); // IGNORE_FOR_VICTORY: does not keep Bob alive
	const ObjectID soldierId = soldier->getID(), wallId = wall->getID();
	w.frames(30);
	REQUIRE(w.logic->getFrame() == 30);
	CHECK_FALSE(v.singleAllianceRemaining());
	w.kill(b, a);
	const unsigned diedAt = w.logic->getFrame(); // the keep died between frames: the next update (frame + 1) sees it
	w.frames(1);
	const Player *alice = w.playerOf('A');
	const Player *bob = w.playerOf('B');
	CHECK(v.isDefeated(1));
	CHECK_FALSE(v.isDefeated(0));
	CHECK(v.singleAllianceRemaining());
	CHECK(v.endFrame() == diedAt + 1);
	CHECK(bob->isDefeated());
	CHECK(bob->getDefeatFrame() == diedAt + 1);
	CHECK_FALSE(alice->isDefeated());
	CHECK(v.defeatCounter() > 0);
	CHECK(v.hasAchievedVictory(alice));
	CHECK_FALSE(v.hasAchievedVictory(bob));
	CHECK(v.hasBeenDefeated(bob));
	CHECK_FALSE(v.hasBeenDefeated(alice));
	// killPlayer: Bob's soldier and wall died with him, Alice's keep stands
	// (the Swordsman's SlowDeathBehavior and the wall's DestroyDie: the wall is already gone at the end of that frame, the soldier sinks and is dead)
	CHECK((w.byId(soldierId) == nullptr || w.byId(soldierId)->isEffectivelyDead()));
	CHECK((w.byId(wallId) == nullptr || w.byId(wallId)->isEffectivelyDead()));
	CHECK_FALSE(a->isEffectivelyDead());
	// the client is told once: the elimination and the end
	REQUIRE(v.events().size() == 2);
	CHECK(v.events()[0].kind == VictoryConditions::Event::ALLIANCE_VICTORY);
	CHECK(v.events()[1].kind == VictoryConditions::Event::PLAYER_DEFEATED);
	CHECK(v.events()[1].playerIndex == bob->getPlayerIndex());
	CHECK(v.events()[1].frame == diedAt + 1);
	// nothing changes afterwards
	const unsigned end = v.endFrame();
	w.frames(10);
	CHECK(v.endFrame() == end);
	CHECK(v.events().size() == 2);
}

TEST_CASE("victory: a living builder keeps a player in the game; the loss comes with the last builder")
{
	VictoryWorld w;
	VictoryConditions &v = w.logic->victory();
	Object *a = w.unit("VKeep", 'A', 300, 300);
	Object *bk = w.unit("VKeep", 'B', 600, 300);
	Object *builder = w.unit("VBuilder", 'B', 620, 300);
	w.frames(30);
	w.kill(bk, a);
	w.frames(5);
	CHECK_FALSE(v.isDefeated(1)); // no structure, but a DOZER lives
	CHECK_FALSE(v.singleAllianceRemaining());
	const unsigned diedAt = w.logic->getFrame();
	w.kill(builder, a);
	w.frames(1);
	CHECK(v.isDefeated(1));
	CHECK(v.singleAllianceRemaining());
	CHECK(v.endFrame() == diedAt + 1);
}

TEST_CASE("victory: a game of one player is not won at the start (the defeat counter must be above 0)")
{
	VictoryWorld w;
	VictoryConditions &v = w.logic->victory();
	Object *a = w.unit("VKeep", 'A', 300, 300);
	(void)a;
	// Bob has nothing at all: eliminated at frame 25, but only then
	w.frames(40);
	CHECK(v.isDefeated(1));
	CHECK(v.singleAllianceRemaining());
	CHECK(v.endFrame() == 25); // frame 25: the first judgement (ftol(5 * 5.0) = 25 frames)
}

TEST_CASE("victory: the state is hashed and every field of it changes the hash (mutation)")
{
	VictoryWorld w;
	VictoryConditions &v = w.logic->victory();
	w.unit("VKeep", 'A', 300, 300);
	w.unit("VKeep", 'B', 600, 300);
	const std::uint32_t base = w.hash();
	v.setScriptFlag90(true);
	CHECK(w.hash() != base);
	v.setScriptFlag90(false);
	CHECK(w.hash() == base);
	v.setScriptFlag91(true);
	CHECK(w.hash() != base);
	v.setScriptFlag91(false);
	v.setRule(VictoryConditions::RULE_BASE_OR_ARMY);
	CHECK(w.hash() != base);
	v.setRule(VictoryConditions::RULE_BASE_OR_BUILDERS);
	CHECK(w.hash() == base);
	// the elimination moves the hash (flags, counter, end frame, events, the player's defeat frame and flag)
	w.frames(30);
	const std::uint32_t before = w.hash();
	Object *bkeep = nullptr;
	for (Object *o = w.logic->getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() == w.playerOf('B'))
		{
			bkeep = o;
		}
	}
	REQUIRE(bkeep != nullptr);
	w.kill(bkeep, nullptr);
	w.frames(2);
	CHECK(w.hash() != before);
}

TEST_CASE("victory: the same game twice gives the same hash in every frame")
{
	std::vector<std::uint32_t> hashes[2];
	for (int run = 0; run < 2; ++run)
	{
		VictoryWorld w;
		Object *a = w.unit("VKeep", 'A', 300, 300);
		Object *b = w.unit("VKeep", 'B', 600, 300);
		w.unit("Swordsman", 'B', 620, 300);
		for (int f = 0; f < 60; ++f)
		{
			if (f == 30)
			{
				w.kill(b, a);
			}
			w.logic->runLogicFrame();
			hashes[run].push_back(w.hash());
		}
	}
	REQUIRE(hashes[0].size() == hashes[1].size());
	for (size_t i = 0; i < hashes[0].size(); ++i)
	{
		REQUIRE_MESSAGE(hashes[0][i] == hashes[1][i], "frame " << i);
	}
	CHECK(hashes[0][29] != hashes[0][59]);
}

TEST_CASE("structure stops: S-344 is in GameLogic::report().stops exactly once, with its counters line")
{
	VictoryWorld w;
	const GameLogic::Report report = w.logic->report();
	int stop = 0, counters = 0;
	for (const std::string &line : report.stops)
	{
		stop += line.rfind("[S-344] ", 0) == 0 ? 1 : 0;
		counters += line.rfind("[S-344 counters]", 0) == 0 ? 1 : 0;
	}
	CHECK(stop == 1);
	CHECK(counters == 1);
}
