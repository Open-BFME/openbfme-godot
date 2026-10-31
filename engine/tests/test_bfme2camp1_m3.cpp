// OpenBFME. GPL-3.0.
// Lane BFME2CAMP1, milestone 3 (MAP GOOD Rivendell to its victory): the script pieces and engine rules the mission needs, each against the RotWK body it
// is ported from.

#include "doctest.h"
#include "CombatTestUtil.h"
#include "Horde2TestUtil.h"
#include "ScriptTestUtil.h"

#include "Common/Player.h"
#include "Common/Team.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameClient/MapChunks.h"
#include "GameLogic/AI/AIClosestEnemy.h"
#include "GameLogic/AI/AIGuard.h"
#include "GameLogic/AI/AIHunt.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ScriptEngine/AttackPriority.h"
#include "GameLogic/ScriptEngine/ScriptActions.h"
#include "GameLogic/ScriptEngine/ScriptConditions.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"

#include <cmath>
#include <string>
#include <vector>

using namespace scripttest;

namespace
{

// one trigger "Box": the square (600, 600) .. (800, 800)
const std::vector<TriggerArea> &triggers()
{
	static const std::vector<TriggerArea> t = [] {
		TriggerArea a;
		a.name = "Box";
		a.points = { { 600.0f, 600.0f }, { 800.0f, 600.0f }, { 800.0f, 800.0f }, { 600.0f, 800.0f } };
		return std::vector<TriggerArea>{ a };
	}();
	return t;
}

void startEngine(GameLogic &logic)
{
	logic.partition().setRegion(0.0f, 0.0f, 1000.0f, 1000.0f);
	ScriptEngine::SideScripts side;
	side.sideName = "";
	static ScriptList empty;
	side.lists.push_back(&empty);
	logic.scriptEngine().newGame({ side }, &triggers(), nullptr, false);
}

std::string qual(Team *t)
{
	return t->getControllingPlayer()->getPlayerName() + "/" + t->getName();
}

std::vector<Object *> membersOf(GameLogic &logic, const Object *horde)
{
	std::vector<Object *> out;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getContainedBy() == horde)
		{
			out.push_back(o);
		}
	}
	return out;
}

} // namespace

TEST_CASE("bfme2camp1 UNIT_HAS_GAINED_LEVEL (RW 0x7E5A47): the unit's experience tracker's rank (+ 0x24) is above its base rank (RW 0x79D102: the rank "
		  "scalar helper's + 0x0C); no unit: false")
{
	combattest::CombatWorld w;
	startEngine(*w.logic);
	ScriptEngine &e = w.logic->scriptEngine();
	Object *u = w.unit("Swordsman", 'A', 100.0f, 100.0f);
	u->setName("U");
	ExperienceTracker *xp = u->getExperienceTracker();
	REQUIRE(xp != nullptr);
	const int base = xp->getBaseRank();
	CHECK(xp->getRank() == base); // initObject sets the base rank to the rank (RW 0x79D764)
	CHECK_FALSE(ScriptConditions::evaluate(e, cond("UNIT_HAS_GAINED_LEVEL", { sp(14, "U") })));
	ExperienceTrackerTestAccess::setRank(*xp, base + 1);
	REQUIRE(xp->getBaseRank() == base); // setRank (RW 0x79D72A) leaves the base
	CHECK(ScriptConditions::evaluate(e, cond("UNIT_HAS_GAINED_LEVEL", { sp(14, "U") })));
	xp->restoreBaseRank(base + 1); // RW 0x79D745 (setRank RW 0x79D72A ignores a rank of 0, so the base moves up instead)
	REQUIRE(xp->getRank() == xp->getBaseRank());
	CHECK_FALSE(ScriptConditions::evaluate(e, cond("UNIT_HAS_GAINED_LEVEL", { sp(14, "U") }))); // equal is not above (RW 0x7E5A77: jle)
	CHECK_FALSE(ScriptConditions::evaluate(e, cond("UNIT_HAS_GAINED_LEVEL", { sp(14, "Nobody") })));
	CHECK(e.stats().unportedConditions.empty());
}

TEST_CASE("bfme2camp1 Object::setTeam (RW 0x69954A) hands a horde's new team to its members (horde interface slot 0x170, RW 0x870667), so UNIT_SET_TEAM "
		  "on a horde gives its members to the new owner")
{
	combattest::CombatWorld w;
	startEngine(*w.logic);
	ScriptEngine &e = w.logic->scriptEngine();
	Object *h = w.unit("SwordHorde", 'B', 500.0f, 500.0f);
	w.frames(2);
	h->setName("H");
	const std::vector<Object *> before = membersOf(*w.logic, h);
	REQUIRE_FALSE(before.empty());
	for (Object *m : before)
	{
		REQUIRE(m->getTeam() == w.teamB());
	}
	ScriptActions::execute(e, act("UNIT_SET_TEAM", { sp(14, "H"), sp(3, qual(w.teamA())) }));
	CHECK(h->getTeam() == w.teamA());
	const std::vector<Object *> after = membersOf(*w.logic, h);
	CHECK(after.size() == before.size());
	for (Object *m : after)
	{
		CHECK(m->getTeam() == w.teamA());
		CHECK(m->getControllingPlayer() == w.teamA()->getControllingPlayer());
	}
	// a null team changes nothing of the members (the pass runs for a non-null team only, RW 0x69962F)
	h->setTeam(nullptr);
	for (Object *m : after)
	{
		CHECK(m->getTeam() == w.teamA());
	}
	CHECK(e.stats().unportedActions.empty());
}

TEST_CASE("bfme2camp1 TEAM_ATTACK_AREA / NAMED_ATTACK_AREA (RW 0x7BF9B3 / 0x7C8DED -> AI command 0x23, privateAttackArea RW 0x66C24E): the area is kept "
		  "(AI + 0x6C) and the AI enters AIAttackAreaState (0x1F), whose scan (RW 0x748230 -> RW 0x700F52) takes the enemy inside the area, not a nearer one "
		  "outside it; an empty area ends the state (STATE_SUCCESS: idle)")
{
	combattest::CombatWorld w;
	startEngine(*w.logic);
	ScriptEngine &e = w.logic->scriptEngine();
	Object *u = w.unit("Swordsman", 'A', 500.0f, 500.0f);
	u->setName("U");
	Object *near = w.unit("Swordsman", 'B', 540.0f, 540.0f);  // outside the box, nearer
	Object *inside = w.unit("Swordsman", 'B', 700.0f, 700.0f); // inside the box
	w.frames(2);
	AIUpdateInterface *ai = u->getAIUpdateInterface();
	REQUIRE(ai != nullptr);
	CHECK(ai->areaToGuard() == nullptr);
	ScriptActions::execute(e, act("NAMED_ATTACK_AREA", { sp(14, "U"), sp(9, "Box") }));
	REQUIRE(ai->areaToGuard() == &triggers()[0]);
	CHECK(ai->currentStateId() == (unsigned)AI_ATTACK_AREA);
	CHECK(ai->lastCommandSource() == CMD_FROM_SCRIPT);
	// the first scan is within GameLogicRandomValue(0, 5) frames (RW 0x743D0F); the attack machine's goal is the enemy in the box
	ObjectID goal = INVALID_ID;
	for (int i = 0; i < 8 && goal == INVALID_ID; ++i)
	{
		w.frames(1);
		const AIAttackAreaState *st = dynamic_cast<const AIAttackAreaState *>(ai->stateMachine().currentState());
		if (st && st->attackMachine())
		{
			goal = st->attackMachine()->goalObjectID();
		}
	}
	CHECK(goal == inside->getID());
	CHECK(goal != near->getID());
	// an unknown area: nothing (RW 0x7C8E1F: the trigger lookup fails)
	Object *v = w.unit("Swordsman", 'A', 300.0f, 300.0f);
	v->setName("V");
	w.frames(2);
	ScriptActions::execute(e, act("NAMED_ATTACK_AREA", { sp(14, "V"), sp(9, "Nowhere") }));
	CHECK(v->getAIUpdateInterface()->areaToGuard() == nullptr);
	CHECK(v->getAIUpdateInterface()->currentStateId() != (unsigned)AI_ATTACK_AREA);
	// the team form on an area without enemies: the state ends at its first scan (no victim: STATE_SUCCESS, the machine's idle)
	inside->kill(DAMAGE_UNRESISTABLE, 0);
	w.frames(2);
	ScriptActions::execute(e, act("TEAM_ATTACK_AREA", { sp(3, qual(w.teamA())), sp(9, "Box") }));
	CHECK(v->getAIUpdateInterface()->areaToGuard() == &triggers()[0]);
	w.frames(8);
	CHECK(v->getAIUpdateInterface()->currentStateId() != (unsigned)AI_ATTACK_AREA);
	CHECK(v->getAIUpdateInterface()->areaToGuard() == &triggers()[0]); // AI + 0x6C stays
	CHECK(e.stats().unportedActions.empty());
}

TEST_CASE("bfme2camp1 the polygon's centre (RW 0x70CE93 / 0x6E4CA7) and radius (RW 0x70CE80: RW 0x70CB46's half diagonal of the bounds)")
{
	combattest::CombatWorld w;
	startEngine(*w.logic);
	const TriggerArea &box = triggers()[0];
	const Coord3D c = ScriptConditions::triggerCenter(*w.logic, box);
	CHECK(c.x == 700.0f);
	CHECK(c.y == 700.0f);
	CHECK(c.z == w.logic->getGroundHeight(700.0f, 700.0f));
	CHECK(ScriptConditions::triggerRadius(box) == (float)std::sqrt(20000.0)); // (100, 100): sqrt(100 * 100 + 100 * 100), rounded to a float
}

TEST_CASE("bfme2camp1 UNIT_GUARD_AREA / TEAM_GUARD_AREA (RW 0x7C94B1 / 0x7C02A6 -> privateGuardAreaFromPosition RW 0x664C11): a unit that never guarded gets "
		  "the AREA target type, the area's centre as its location and the guard state; its guard machine keeps the area (AIGuardStateOnEnter) and finds an "
		  "enemy inside the area's radius around its centre; a unit that guarded before keeps its LOCATION type (retail's second slot AI + 0x50 takes AREA)")
{
	combattest::CombatWorld w;
	startEngine(*w.logic);
	ScriptEngine &e = w.logic->scriptEngine();
	Object *u = w.unit("Swordsman", 'A', 660.0f, 640.0f);
	u->setName("U");
	w.frames(2);
	AIUpdateInterface *ai = u->getAIUpdateInterface();
	REQUIRE(ai != nullptr);
	REQUIRE(ai->guardTargetType() == GUARDTARGET_LOCATION); // the port's start (retail: NONE)
	REQUIRE(ai->guardLocation().x == 0.0f);
	ScriptActions::execute(e, act("UNIT_GUARD_AREA", { sp(14, "U"), sp(9, "Box") }));
	CHECK(ai->guardTargetType() == GUARDTARGET_AREA);
	CHECK(ai->areaToGuard() == &triggers()[0]);
	CHECK(ai->guardLocation().x == 700.0f);
	CHECK(ai->guardLocation().y == 700.0f);
	CHECK(ai->guardMode() == GUARDMODE_NORMAL);
	REQUIRE(ai->currentStateId() == (unsigned)AI_GUARD);
	const AIGuardState *gs = dynamic_cast<const AIGuardState *>(ai->stateMachine().currentState());
	REQUIRE(gs != nullptr);
	REQUIRE(gs->guardMachine() != nullptr);
	CHECK(gs->guardMachine()->areaToGuard() == &triggers()[0]);
	CHECK(gs->guardMachine()->guardPosition().x == 700.0f); // RW 0x949BD3: the stored centre (+ 0x54, the AI's location)
	// an enemy inside the box within the box's radius of its centre (and within that range of the unit: the search's canSee filter, RW 0x6612AC, measures
	// from the unit)
	Object *enemy = w.unit("Swordsman", 'B', 720.0f, 700.0f);
	ObjectID nemesis = INVALID_ID;
	for (int i = 0; i < 60 && nemesis == INVALID_ID; ++i)
	{
		w.frames(1);
		const AIGuardState *g = dynamic_cast<const AIGuardState *>(ai->stateMachine().currentState());
		if (g && g->guardMachine())
		{
			nemesis = g->guardMachine()->nemesis();
		}
	}
	CHECK(nemesis == enemy->getID());
	// a unit that guarded its spot before (the stances' guardHere, RW 0x662A21): the type stays LOCATION, the location is the area's centre
	Object *v = w.unit("Swordsman", 'A', 200.0f, 200.0f);
	v->setName("V");
	w.frames(2);
	v->getAIUpdateInterface()->guardHere(GUARDMODE_NORMAL);
	REQUIRE(v->getAIUpdateInterface()->guardTargetType() == GUARDTARGET_LOCATION);
	ScriptActions::execute(e, act("TEAM_GUARD_AREA", { sp(3, qual(w.teamA())), sp(9, "Box") }));
	CHECK(v->getAIUpdateInterface()->guardTargetType() == GUARDTARGET_LOCATION);
	CHECK(v->getAIUpdateInterface()->areaToGuard() == &triggers()[0]);
	CHECK(v->getAIUpdateInterface()->guardLocation().x == 700.0f);
	CHECK(e.stats().unportedActions.empty());
}

TEST_CASE("bfme2camp1 the scripts' attack priority sets: SET_DEFAULT_ATTACK_PRIORITY / SET_ATTACK_PRIORITY_THING / _KIND_OF (RW 0x605D32 / 0x605A47 / 0x605C78 "
		  "through RW 0x6058B8) make and edit a set; NAMED_ / TEAM_APPLY_ATTACK_PRIORITY_SET (RW 0x7BBC5B / 0x7BF864) give it to AI + 0x70, and a later member "
		  "of the team takes the prototype's set when it joins (RW 0x697D32)")
{
	combattest::CombatWorld w;
	startEngine(*w.logic);
	ScriptEngine &e = w.logic->scriptEngine();
	AttackPriorityStore &store = e.attackPriorities();
	REQUIRE_FALSE(store.edited());
	// an unknown thing: the walk ends before the set is made (RW 0x605A7D: the template first)
	e.executeAction(act("SET_ATTACK_PRIORITY_THING", { sp(28, "Wave"), sp(61, "NoSuchThing"), ip(0, 150) }));
	CHECK_FALSE(store.edited());
	e.executeAction(act("SET_DEFAULT_ATTACK_PRIORITY", { sp(28, "Wave"), ip(0, 5) }));
	CHECK(store.edited());
	e.executeAction(act("SET_ATTACK_PRIORITY_THING", { sp(28, "Wave"), sp(61, "Swordsman"), ip(0, 150) }));
	Object *hunter = w.unit("Swordsman", 'B', 100.0f, 100.0f);
	Object *sword = w.unit("Swordsman", 'A', 300.0f, 100.0f);
	Object *dummy = w.unit("Dummy", 'A', 300.0f, 300.0f);
	w.frames(2);
	const AttackPriorityInfo *wave = store.getAttackInfo("Wave");
	REQUIRE(wave != nullptr);
	CHECK(wave->getName() == "Wave");
	CHECK(wave->getDefaultPriority() == 5);
	CHECK(wave->getPriority(*hunter, *sword, false) == 150.0f);
	CHECK(wave->getPriority(*hunter, *dummy, false) == 5.0f); // the default for a thing without its own
	// NAMED_APPLY with a set nobody made: the default info (RW 0x605938's fallback, entry 0)
	hunter->setName("H");
	e.executeAction(act("NAMED_APPLY_ATTACK_PRIORITY_SET", { sp(14, "H"), sp(28, "Nobody") }));
	REQUIRE(hunter->getAIUpdateInterface()->hasAttackInfoOverride());
	CHECK(hunter->getAIUpdateInterface()->attackInfo() == store.getDefaultAttackInfo());
	// TEAM_APPLY: every member and the prototype's name; a later member takes it when it joins
	e.executeAction(act("TEAM_APPLY_ATTACK_PRIORITY_SET", { sp(3, qual(w.teamB())), sp(28, "Wave") }));
	CHECK(hunter->getAIUpdateInterface()->attackInfo() == wave);
	CHECK(w.teamB()->getPrototype()->attackPriorityName() == "Wave");
	Object *later = w.unit("Swordsman", 'B', 120.0f, 100.0f);
	w.frames(1);
	REQUIRE(later->getAIUpdateInterface()->hasAttackInfoOverride());
	CHECK(later->getAIUpdateInterface()->attackInfo() == wave);
	CHECK_FALSE(sword->getAIUpdateInterface()->hasAttackInfoOverride()); // another team's units keep their module data's set
	CHECK(e.stats().unportedActions.empty());
}

TEST_CASE("bfme2camp1 the script engine snapshot: version 5 (CASTLEAREA-1's trigger list extent, MODCOMPAT-AI's completed upgrades) until a script "
		  "edits the attack priorities, then version 6 (the table after 5's fields); both load back to their end")
{
	combattest::CombatWorld w;
	startEngine(*w.logic);
	ScriptEngine &e = w.logic->scriptEngine();
	XferSave plain;
	e.xfer(plain);
	REQUIRE_FALSE(plain.data().empty());
	CHECK(plain.data()[0] == 5);
	XferLoad plainBack(plain.data());
	CHECK_NOTHROW(e.xfer(plainBack));
	CHECK(plainBack.atEnd());
	e.executeAction(act("SET_DEFAULT_ATTACK_PRIORITY", { sp(28, "Wave"), ip(0, 5) }));
	REQUIRE(e.attackPriorities().edited());
	XferSave edited;
	e.xfer(edited);
	REQUIRE_FALSE(edited.data().empty());
	CHECK(edited.data()[0] == 6);
	CHECK(edited.data().size() > plain.data().size());
	XferLoad back(edited.data());
	CHECK_NOTHROW(e.xfer(back));
	CHECK(back.atEnd());
}

TEST_CASE("bfme2camp1 PLAYER_TRANSFER_OWNERSHIP_PLAYER (RW 0x7BC0C3): the second player takes the first one's assets (transferAssetsFromThat RW 0x6AF598, "
		  "inherited 0: no INHERITED_FROM_ALLY_TEAM); REFRESH_RADAR is the client's (RW 0x7BCC53: TheRadar)")
{
	combattest::CombatWorld w;
	startEngine(*w.logic);
	ScriptEngine &e = w.logic->scriptEngine();
	Object *b = w.unit("Swordsman", 'B', 300.0f, 300.0f);
	Object *a = w.unit("Swordsman", 'A', 600.0f, 600.0f);
	w.frames(2);
	const Player *alice = w.teamA()->getControllingPlayer();
	REQUIRE(b->getControllingPlayer() != alice);
	ScriptActions::execute(e, act("PLAYER_TRANSFER_OWNERSHIP_PLAYER", { sp(11, "Bob"), sp(11, "Alice") }));
	CHECK(b->getControllingPlayer() == alice);
	CHECK(b->getTeam() == alice->getDefaultTeam());
	const int inherited = ObjectTemplateInfoBuilder::objectStatusIndex("INHERITED_FROM_ALLY_TEAM");
	REQUIRE(inherited == 0x50); // RW 0x62684D(0x50, 1): the status transferAssetsFromThat sets for an inheritance
	CHECK_FALSE(b->testStatus((unsigned)inherited));
	CHECK(a->getControllingPlayer() == alice); // Alice's own unit stays hers
	ScriptActions::execute(e, act("REFRESH_RADAR", {}));
	CHECK(e.stats().unportedActions.empty());
}

TEST_CASE("bfme2camp1 AI::findClosestEnemyInArea (RW 0x700F52): without a priority set the closest enemy in the area; with one the best score max(1, p - "
		  "trunc(d / AttackPriorityDistanceModifier)), not the closest; SET_ATTACK_PRIORITY_KIND_OF (RW 0x605C78) gives every template of the KindOf its priority")
{
	combattest::CombatWorld w;
	startEngine(*w.logic);
	ScriptEngine &e = w.logic->scriptEngine();
	REQUIRE(w.logic->settings().attackPriorityDistanceModifier == 80.0f); // retail aidata.ini (LogicTestUtil retailTargetSearchData)
	Object *hunter = w.unit("Swordsman", 'A', 650.0f, 700.0f);
	Object *dummy = w.unit("Dummy", 'B', 670.0f, 700.0f);   // inside the box, 20 away
	Object *sword = w.unit("Swordsman", 'B', 780.0f, 700.0f); // inside the box, 130 away
	Object *outside = w.unit("Dummy", 'B', 640.0f, 560.0f); // outside the box, 140 away
	w.frames(2);
	const TriggerArea &box = triggers()[0];
	CHECK(AI::findClosestEnemyInArea(box, *hunter, nullptr) == dummy); // the partition's closest inside the bounds and the polygon
	e.executeAction(act("SET_ATTACK_PRIORITY_THING", { sp(28, "Scored"), sp(61, "Dummy"), ip(0, 10) }));
	e.executeAction(act("SET_ATTACK_PRIORITY_THING", { sp(28, "Scored"), sp(61, "Swordsman"), ip(0, 150) }));
	const AttackPriorityInfo *scored = e.attackPriorities().getAttackInfo("Scored");
	REQUIRE(scored->getName() == "Scored");
	// Dummy: 10 - trunc(~20 / 80) = 10; Swordsman: 150 - trunc(~130 / 80) = 149 (the edge distances are a little shorter: the same truncations)
	CHECK(AI::findClosestEnemyInArea(box, *hunter, scored) == sword);
	(void)outside;
	// KIND_OF: the INFANTRY templates (Swordsman, Dummy) get 77; a HORDE template keeps the set's default (1)
	const int infantry = ObjectTemplateInfoBuilder::kindOfIndex("INFANTRY");
	REQUIRE(infantry >= 0); // tolerance: a KindOf index of the binary's table (its value is not the point)
	e.executeAction(act("SET_ATTACK_PRIORITY_KIND_OF", { sp(28, "Kinds"), ip(27, infantry), ip(0, 77) }));
	Object *horde = w.unit("SwordHorde", 'B', 300.0f, 300.0f);
	w.frames(1);
	const AttackPriorityInfo *kinds = e.attackPriorities().getAttackInfo("Kinds");
	CHECK(kinds->getPriority(*hunter, *dummy, false) == 77.0f);
	CHECK(kinds->getPriority(*hunter, *sword, false) == 77.0f);
	CHECK(kinds->getPriority(*hunter, *horde, false) == 1.0f);
}
