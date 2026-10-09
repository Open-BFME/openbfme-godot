// OpenBFME. COMBAT-2 review round 1: one regression test per finding. Synthetic data (combattest::CombatWorld), no retail files.
//   1 client queries do not change the hashed state      2 local identity is not hashed       3 the castle breach is recorded for every owner
//   4 an INI InitialHealth applies its damage state      5 rule 3's unit scan exclusions       6 the defeat kill's death type is the retail one
//   7 (the Godot report's `defeated` list is player indices: VictoryConditions::cachedPlayerIndex)
#include "doctest.h"
#include "CombatTestUtil.h"

#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Module/CastleModules.h"
#include "GameLogic/Module/StructureModules.h"
#include "GameLogic/VictoryConditions.h"

using namespace combattest;

namespace
{
const char kObjects[] =
	"Object RKeep\n"
	"  KindOf = STRUCTURE IMMOBILE SELECTABLE\n"
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
	"Object RWall\n"
	"  KindOf = STRUCTURE IMMOBILE IGNORE_FOR_VICTORY\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 10\n"
	"  GeometryMinorRadius = 10\n"
	"  GeometryHeight = 10\n"
	"  Body = StructureBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"End\n"
	"Object RIgnored\n"
	"  KindOf = INFANTRY IGNORE_FOR_VICTORY\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 50\n"
	"  End\n"
	"End\n"
	"Object RShell\n"
	"  KindOf = PROJECTILE\n"
	"  Geometry = SPHERE\n"
	"  GeometryMajorRadius = 3\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 50\n"
	"  End\n"
	"End\n"
	"Object RSoldier\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 50\n"
	"  End\n"
	"End\n"
	// the death-type probes: one die module that answers only to NORMAL deaths, one only to SUICIDED ones
	"Object RNormalOnly\n"
	"  KindOf = INFANTRY\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 50\n"
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_Destroy\n"
	"    DeathTypes = NONE +NORMAL\n"
	"  End\n"
	"End\n"
	"Object RSuicideOnly\n"
	"  KindOf = INFANTRY\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 50\n"
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_Destroy\n"
	"    DeathTypes = NONE +SUICIDED\n"
	"  End\n"
	"End\n"
	// InitialHealth: below the really damaged fraction; and zero
	"Object RWounded\n"
	"  KindOf = STRUCTURE IMMOBILE\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 10\n"
	"  GeometryMinorRadius = 10\n"
	"  GeometryHeight = 10\n"
	"  Body = StructureBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"    MaxHealthDamaged = 60\n"
	"    MaxHealthReallyDamaged = 30\n"
	"    InitialHealth = 25\n"
	"  End\n"
	"  Behavior = CastleMemberBehavior ModuleTag_Castle\n"
	"    CountsForEvaCastleBreached = Yes\n"
	"  End\n"
	"End\n"
	"Object RRuin\n"
	"  KindOf = STRUCTURE IMMOBILE\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 10\n"
	"  GeometryMinorRadius = 10\n"
	"  GeometryHeight = 10\n"
	"  Body = StructureBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"    InitialHealth = 0\n"
	"  End\n"
	"  Behavior = CastleMemberBehavior ModuleTag_Castle\n"
	"  End\n"
	"End\n"
	// two bodies: the LAST one (RW 0x69A3C8) is the object's body and the one that applies its initial state
	"Object RTwoBodies\n"
	"  KindOf = STRUCTURE IMMOBILE\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 10\n"
	"  GeometryMinorRadius = 10\n"
	"  GeometryHeight = 10\n"
	"  Body = ActiveBody ModuleTag_B1\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Body = StructureBody ModuleTag_B2\n"
	"    MaxHealth = 100\n"
	"    MaxHealthDamaged = 60\n"
	"    MaxHealthReallyDamaged = 30\n"
	"    InitialHealth = 50\n"
	"  End\n"
	"End\n";

std::string gameData(const char *unitFilter)
{
	return std::string("GameData\n  ForceModelsToFollowTimeOfDay = Yes\n  ForceModelsToFollowWeather = Yes\n  UnitDamagedThreshold = 0.5\n  UnitReallyDamagedThreshold = 0.25\n  DefaultStartingCash = 1000\n") +
		"  VictoryConditionStructureObjectFilter = NONE +STRUCTURE -IGNORE_FOR_VICTORY -UNATTACKABLE\n  VictoryConditionUnitObjectFilter = " + unitFilter + "\n" + "End\n";
}

struct World : CombatWorld
{
	// `local`: 'A' / 'B' the local player, 'N' the neutral player (an observed game)
	explicit World(char local = 'A', const char *unitFilter = "ANY -DOZER")
		: CombatWorld(kObjects)
	{
		GameLogicSettings scratch;
		std::string err;
		REQUIRE_MESSAGE(GameLogicSettingsLoader::scanGameData(gameData(unitFilter), scratch, &err), err);
		logic->settings().victoryRulesLoaded = scratch.victoryRulesLoaded;
		logic->settings().victoryStructureFilter = scratch.victoryStructureFilter;
		logic->settings().victoryUnitFilter = scratch.victoryUnitFilter;
		logic->settings().secondsBeforeBaseCheckActive = scratch.secondsBeforeBaseCheckActive;
		if (local == 'N')
		{
			players.setLocalPlayer(nullptr);
		}
		else
		{
			players.setLocalPlayer(playerOf(local));
		}
		logic->victory().init();
	}
	void kill(Object *o) { DamageInfo d; d.m_input.m_amount = 1.0e9f; d.m_input.m_damageType = DAMAGE_UNRESISTABLE; o->attemptDamage(d); }
};
} // namespace

// 1: RW 0x809572 bumps the defeat counter inside the queries; the port's client queries are const
TEST_CASE("review 1: polling the victory queries at any rate leaves the state hash alone, for the winner, the loser and the local player")
{
	World w('B'); // the local player is Bob, the loser; the winner (Alice) is first in the cache
	Object *a = w.unit("RKeep", 'A', 300, 300);
	(void)a;
	Object *b = w.unit("RKeep", 'B', 600, 300);
	w.frames(30);
	w.kill(b);
	w.frames(2);
	VictoryConditions &v = w.logic->victory();
	REQUIRE(v.singleAllianceRemaining());
	const std::uint32_t before = w.hash();
	const int counter = v.defeatCounter();
	for (int i = 0; i < 25; ++i)
	{
		CHECK(v.hasAchievedVictory(w.playerOf('A')));
		CHECK_FALSE(v.hasAchievedVictory(w.playerOf('B')));
		CHECK(v.hasBeenDefeated(w.playerOf('B')));
		CHECK_FALSE(v.hasBeenDefeated(w.playerOf('A')));
		CHECK(v.wouldBeDefeated(w.playerOf('B')));
	}
	CHECK(v.defeatCounter() == counter);
	CHECK(w.hash() == before);
	// two worlds polled at different rates agree frame by frame
	World p, q;
	for (World *x : { &p, &q })
	{
		x->unit("RKeep", 'A', 300, 300);
		x->unit("RKeep", 'B', 600, 300);
	}
	for (int f = 0; f < 40; ++f)
	{
		p.logic->runLogicFrame();
		q.logic->runLogicFrame();
		for (int k = 0; k < (f % 5); ++k)
		{
			p.logic->victory().hasBeenDefeated(p.playerOf('B'));
			p.logic->victory().hasAchievedVictory(p.playerOf('A'));
		}
		REQUIRE_MESSAGE(p.hash() == q.hash(), "frame " << f);
	}
}

// 2: the local slot / observer flags are this peer's identity
TEST_CASE("review 2: identical worlds with different local players, or an observer, hash the same before and after an elimination")
{
	World wa('A'), wb('B'), wn('N');
	CHECK(wn.logic->victory().isObserver());
	CHECK(wa.logic->victory().localSlot() != wb.logic->victory().localSlot());
	for (World *x : { &wa, &wb, &wn })
	{
		x->unit("RKeep", 'A', 300, 300);
		x->unit("RKeep", 'B', 600, 300);
	}
	CHECK(wa.hash() == wb.hash());
	CHECK(wa.hash() == wn.hash());
	for (int f = 0; f < 40; ++f)
	{
		if (f == 30)
		{
			for (World *x : { &wa, &wb, &wn })
			{
				for (Object *o = x->logic->getFirstObject(); o; o = o->getNextObject())
				{
					if (o->getControllingPlayer() == x->playerOf('B'))
					{
						x->kill(o);
					}
				}
			}
		}
		wa.logic->runLogicFrame();
		wb.logic->runLogicFrame();
		wn.logic->runLogicFrame();
		REQUIRE_MESSAGE(wa.hash() == wb.hash(), "frame " << f);
		REQUIRE_MESSAGE(wa.hash() == wn.hash(), "frame " << f);
	}
	CHECK(wa.logic->victory().singleAllianceRemaining());
	CHECK(wn.logic->victory().singleAllianceRemaining());
}

// 3: the breach is recorded whoever owns the member
TEST_CASE("review 3: the same castle death gives the same hash for a local owner and a local enemy; the breach event names its owner")
{
	std::uint32_t hash[2];
	size_t events[2];
	for (int run = 0; run < 2; ++run)
	{
		World w(run == 0 ? 'A' : 'B');
		Object *wounded = w.unit("RWounded", 'A', 300, 300); // starts REALLYDAMAGED (InitialHealth 25 of 100)
		CHECK(w.combat().castleBreaches().empty());
		w.kill(wounded);
		events[run] = w.combat().castleBreaches().size();
		hash[run] = w.hash();
		if (events[run] == 1)
		{
			CHECK(w.combat().castleBreaches()[0].ownerPlayerIndex == w.playerOf('A')->getPlayerIndex());
		}
	}
	CHECK(events[0] == 1);
	CHECK(events[1] == 1);
	CHECK(hash[0] == hash[1]);
}

// 4: an INI InitialHealth below the thresholds shows its damage state from the first frame
TEST_CASE("review 4: InitialHealth applies the damage state's model condition, damage modules and structure effects once, for the last body of a template")
{
	World w;
	const int damaged = CombatNames::modelCondition("DAMAGED"), really = CombatNames::modelCondition("REALLYDAMAGED"), rubble = CombatNames::modelCondition("RUBBLE");
	Object *wounded = w.unit("RWounded", 'B', 300, 300);
	REQUIRE(wounded->getBodyModule()->getHealth() == 25.0f);
	CHECK(wounded->getBodyModule()->getDamageState() == BODY_REALLYDAMAGED);
	CHECK(wounded->testModelCondition(really));
	CHECK_FALSE(wounded->testModelCondition(damaged));
	// a further hit that stays in the same state changes no condition and applies no second initial state
	DamageInfo d;
	d.m_input.m_amount = 5.0f;
	d.m_input.m_damageType = DAMAGE_UNRESISTABLE;
	wounded->attemptDamage(d);
	CHECK(wounded->testModelCondition(really));
	// zero: RUBBLE with NO_COLLISIONS and the castle member's breached flag
	Object *ruin = w.unit("RRuin", 'B', 400, 300);
	CHECK(ruin->getBodyModule()->getDamageState() == BODY_RUBBLE);
	CHECK(ruin->testModelCondition(rubble));
	CHECK(ruin->testStatus((unsigned)CombatNames::statuses().noCollisions));
	CHECK(dynamic_cast<CastleMemberBehavior *>(ruin->findModule("CastleMemberBehavior"))->breached());
	// two bodies: the last one (StructureBody, InitialHealth 50 of 100: DAMAGED) is the body and applies its state
	Object *two = w.unit("RTwoBodies", 'B', 500, 300);
	CHECK(two->getBodyModule()->getHealth() == 50.0f);
	CHECK(two->getBodyModule()->getDamageState() == BODY_DAMAGED);
	CHECK(two->testModelCondition(damaged));
	// a pristine object has none of them
	Object *fine = w.unit("RKeep", 'B', 600, 300);
	CHECK_FALSE(fine->testModelCondition(damaged));
	CHECK_FALSE(fine->testModelCondition(really));
	CHECK_FALSE(fine->testModelCondition(rubble));
}

// 5: rule 3's own scan skips STRUCTURE, PROJECTILE and IGNORE_FOR_VICTORY objects before the unit filter applies (RW 0x7A089B)
TEST_CASE("review 5: rule 3 ignores walls, shells and IGNORE_FOR_VICTORY units even when the unit filter admits them, and counts a live soldier")
{
	for (const char *candidate : { "RWall", "RIgnored", "RShell" })
	{
		World w('A', "ALL");
		w.logic->victory().setRule(VictoryConditions::RULE_BASE_OR_ARMY);
		w.unit("RKeep", 'A', 300, 300);
		w.unit(candidate, 'B', 600, 300);
		w.frames(30);
		CHECK_MESSAGE(w.logic->victory().isDefeated(1), candidate << " must not keep Bob alive");
	}
	World w('A', "ALL");
	w.logic->victory().setRule(VictoryConditions::RULE_BASE_OR_ARMY);
	w.unit("RKeep", 'A', 300, 300);
	w.unit("RWall", 'B', 600, 300);
	w.unit("RSoldier", 'B', 620, 300);
	w.frames(30);
	CHECK_FALSE(w.logic->victory().isDefeated(1)); // a live soldier the filter accepts
	// the structure filter still saves a player with only a keep
	World k('A', "ALL");
	k.logic->victory().setRule(VictoryConditions::RULE_BASE_OR_ARMY);
	k.unit("RKeep", 'A', 300, 300);
	k.unit("RKeep", 'B', 600, 300);
	k.frames(30);
	CHECK_FALSE(k.logic->victory().isDefeated(1));
}

// 6: RW 0x7A4CAE pushes 0 and 8 into Object::kill (RW 0x698EC3), whose FIRST argument is the damage type (UNRESISTABLE = 8, DamageInfo + 0x10) and whose second the death type (0 =
// NORMAL, DamageInfo + 0x1C): the defeat kill is a NORMAL death of UNRESISTABLE damage. (The review expected SUICIDED; the argument order says otherwise: this test pins the binary's.)
TEST_CASE("review 6: the defeat kill is a NORMAL death: a die module for NORMAL deaths runs, one for SUICIDED deaths does not")
{
	World w;
	w.unit("RKeep", 'A', 300, 300);
	w.unit("RKeep", 'B', 600, 300);
	Object *normal = w.unit("RNormalOnly", 'B', 620, 300);
	Object *suicide = w.unit("RSuicideOnly", 'B', 640, 300);
	const ObjectID nid = normal->getID(), sid = suicide->getID();
	w.frames(30);
	for (Object *o = w.logic->getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() == w.playerOf('B') && o->isKindOfName("STRUCTURE"))
		{
			w.kill(o);
			break;
		}
	}
	w.frames(2);
	REQUIRE(w.logic->victory().isDefeated(1));
	CHECK(w.byId(nid) == nullptr);                          // DestroyDie for NORMAL deaths ran
	REQUIRE(w.byId(sid) != nullptr);                        // DestroyDie for SUICIDED deaths did not
	CHECK(w.byId(sid)->isEffectivelyDead());                // ... but the object was killed
}

// 7: the Godot report lists real player indices
TEST_CASE("review 7: cachedPlayerIndex gives the real player index of a cached player, not its cache position")
{
	World w;
	const VictoryConditions &v = w.logic->victory();
	REQUIRE(v.cachedPlayers() >= 2);
	for (int i = 0; i < v.cachedPlayers(); ++i)
	{
		const int index = v.cachedPlayerIndex(i);
		REQUIRE(index >= 0);
		CHECK(w.players.getNthPlayer(index) != nullptr);
	}
	CHECK(v.cachedPlayerIndex(v.cachedPlayers()) == -1);
	// filtered players before the combatants: initWithPlayers caches Bob first, Alice second: positions 0, 1 are players 2, 1 (or whichever the list holds), never the positions
	World x;
	x.logic->victory().initWithPlayers({ x.playerOf('B'), x.playerOf('A') });
	CHECK(x.logic->victory().cachedPlayerIndex(0) == x.playerOf('B')->getPlayerIndex());
	CHECK(x.logic->victory().cachedPlayerIndex(1) == x.playerOf('A')->getPlayerIndex());
	CHECK(x.playerOf('B')->getPlayerIndex() != 0); // the neutral player holds index 0: an index is not a cache position
}
