// OpenBFME. GPL-3.0.
// Lane BFME2CAMP1, milestone 4 (MAP GOOD High Pass and Ettenmoors to their victories): the script orders the missions need, each against the RotWK body it
// is ported from.

#include "doctest.h"
#include "CombatTestUtil.h"
#include "ScriptTestUtil.h"

#include "Common/Player.h"
#include "Common/StateHash.h"
#include "Common/Team.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameClient/TutorialSettings.h"
#include "GameLogic/AI/AIGuard.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/AI/AIHunt.h"
#include "GameLogic/AI/AIStateMachine.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/ScriptEngine/ScriptActions.h"
#include "GameLogic/ScriptEngine/ScriptConditions.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"

#include <string>
#include <vector>

using namespace scripttest;

namespace
{

void startEngine(GameLogic &logic)
{
	logic.partition().setRegion(0.0f, 0.0f, 1000.0f, 1000.0f);
	ScriptEngine::SideScripts side;
	side.sideName = "";
	static ScriptList empty;
	side.lists.push_back(&empty);
	logic.scriptEngine().newGame({ side }, nullptr, nullptr, false);
}

std::string qual(Team *t)
{
	return t->getControllingPlayer()->getPlayerName() + "/" + t->getName();
}

const AIAttackSquadState *squadState(AIUpdateInterface &ai)
{
	return dynamic_cast<const AIAttackSquadState *>(ai.stateMachine().currentState());
}

std::vector<ObjectID> idsOf(const std::vector<Object *> &objects)
{
	std::vector<ObjectID> out;
	for (const Object *o : objects)
	{
		out.push_back(o->getID());
	}
	return out;
}

} // namespace

TEST_CASE("bfme2camp1 TEAM_ATTACK_TEAM (RW 0x7BF7EC -> AI command 0xD, privateAttackTeam RW 0x66C03D): the machine's goal squad is the victim team's "
		  "members in member order (RW 0x751E97 -> RW 0x8DB149), the AI enters AIAttackSquadState (0x17) and chooseVictim (RW 0x75277F) takes the nearest "
		  "live member by squared 2D distance; when it dies the next one; with none left the state ends (STATE_SUCCESS: idle); the machine's clear deletes "
		  "the squad (RW 0x75464E)")
{
	combattest::CombatWorld w;
	startEngine(*w.logic);
	ScriptEngine &e = w.logic->scriptEngine();
	Object *u = w.unit("Swordsman", 'A', 500.0f, 500.0f);
	Object *far = w.unit("Swordsman", 'B', 500.0f, 700.0f);
	Object *near = w.unit("Swordsman", 'B', 500.0f, 600.0f);
	w.frames(2);
	AIUpdateInterface *ai = u->getAIUpdateInterface();
	REQUIRE(ai != nullptr);
	REQUIRE(ai->stateMachine().goalSquad() == nullptr);
	ScriptActions::execute(e, act("TEAM_ATTACK_TEAM", { sp(3, qual(w.teamA())), sp(3, qual(w.teamB())) }));
	REQUIRE(ai->currentStateId() == (unsigned)AI_ATTACK_SQUAD);
	CHECK(ai->lastCommandSource() == CMD_FROM_SCRIPT);
	const AISquad *squad = ai->stateMachine().goalSquad();
	REQUIRE(squad != nullptr);
	CHECK(squad->objectIDs == idsOf(ScriptConditions::members(*w.teamB()))); // the team's member order (RW 0x66362D)
	CHECK(squad->objectIDs.size() == 2);
	const AIAttackSquadState *st = squadState(*ai);
	REQUIRE(st != nullptr);
	REQUIRE(st->attackMachine() != nullptr);
	// onEnter (RW 0x75345E): the nearest (100 against 200 away; neither within the sword's reach, so no 0.25 scaling) is the attack machine's goal, the
	// machine's goal and the AI's current victim
	CHECK(st->attackMachine()->goalObjectID() == near->getID());
	CHECK(ai->stateMachine().goalObjectID() == near->getID());
	CHECK(ai->currentVictimId() == near->getID());
	// the victim dies: the idle attack machine takes the next live member (RW 0x7535FC -> RW 0x75277F; a dead one is not selectable, RW 0x68DE58)
	near->kill(DAMAGE_UNRESISTABLE, 0);
	ObjectID goal = INVALID_ID;
	for (int i = 0; i < 30 && goal != far->getID(); ++i)
	{
		w.frames(1);
		st = squadState(*ai);
		goal = st && st->attackMachine() ? st->attackMachine()->goalObjectID() : INVALID_ID;
	}
	CHECK(goal == far->getID());
	CHECK(ai->stateMachine().goalObjectID() == far->getID());
	// no live member left: STATE_SUCCESS (RW 0x753607), the state's success is idle (RW 0x753DF2); the squad stays (only the clear deletes it)
	far->kill(DAMAGE_UNRESISTABLE, 0);
	for (int i = 0; i < 30 && ai->currentStateId() == (unsigned)AI_ATTACK_SQUAD; ++i)
	{
		w.frames(1);
	}
	CHECK(ai->currentStateId() != (unsigned)AI_ATTACK_SQUAD);
	CHECK(ai->stateMachine().goalSquad() != nullptr);
	ai->aiIdle(CMD_FROM_SCRIPT); // the machine's clear (vslot 0x14)
	CHECK(ai->stateMachine().goalSquad() == nullptr);
	CHECK(e.stats().unportedActions.empty());
}

TEST_CASE("bfme2camp1 NAMED_ATTACK_TEAM (RW 0x7C8EBB): the named unit attacks the team; a missing unit or team does nothing; the goal squad is hashed "
		  "and saved with the machine (version 2, written only with a squad)")
{
	combattest::CombatWorld w;
	startEngine(*w.logic);
	ScriptEngine &e = w.logic->scriptEngine();
	Object *u = w.unit("Swordsman", 'A', 500.0f, 500.0f);
	u->setName("U");
	Object *enemy = w.unit("Swordsman", 'B', 500.0f, 650.0f);
	w.frames(2);
	AIUpdateInterface *ai = u->getAIUpdateInterface();
	REQUIRE(ai != nullptr);
	// a machine without a squad keeps version 1 (the first byte after the version is the current state's id either way)
	{
		XferSave before;
		ai->stateMachine().xfer(before);
		REQUIRE(before.data().size() > 1); // tolerance: a precondition (the save has a version byte and more), its size is not what is tested
		CHECK(before.data()[0] == 1);
	}
	ScriptActions::execute(e, act("NAMED_ATTACK_TEAM", { sp(14, "Nobody"), sp(3, qual(w.teamB())) }));
	ScriptActions::execute(e, act("NAMED_ATTACK_TEAM", { sp(14, "U"), sp(3, "Bob/NoSuchTeam") }));
	CHECK(ai->currentStateId() != (unsigned)AI_ATTACK_SQUAD);
	ScriptActions::execute(e, act("NAMED_ATTACK_TEAM", { sp(14, "U"), sp(3, qual(w.teamB())) }));
	REQUIRE(ai->currentStateId() == (unsigned)AI_ATTACK_SQUAD);
	REQUIRE(ai->stateMachine().goalSquad() != nullptr);
	CHECK(ai->stateMachine().goalSquad()->objectIDs == std::vector<ObjectID>{ enemy->getID() });
	CHECK(ai->currentVictimId() == enemy->getID());
	// the squad is part of the machine's hash
	StateHasher h1;
	ai->stateMachine().crc(h1);
	ai->stateMachine().goalSquad()->objectIDs.push_back(u->getID());
	StateHasher h2;
	ai->stateMachine().crc(h2);
	CHECK(h1.value() != h2.value());
	ai->stateMachine().goalSquad()->objectIDs.pop_back();
	// saved and loaded back: version 2, the squad's ids
	XferSave saved;
	ai->stateMachine().xfer(saved);
	REQUIRE(saved.data().size() > 1); // tolerance: a precondition (the save has a version byte and more), its size is not what is tested
	CHECK(saved.data()[0] == 2);
	ai->stateMachine().goalSquad()->objectIDs.clear();
	{
		AIStateMachine::LoadGuard guard;
		XferLoad load(saved.data());
		ai->stateMachine().xfer(load);
	}
	REQUIRE(ai->stateMachine().goalSquad() != nullptr);
	CHECK(ai->stateMachine().goalSquad()->objectIDs == std::vector<ObjectID>{ enemy->getID() });
	StateHasher h3;
	ai->stateMachine().crc(h3);
	CHECK(h3.value() == h1.value());
	CHECK(e.stats().unportedActions.empty());
}

TEST_CASE("bfme2camp1 chooseVictim's target test RW 0x747AFE: an UNATTACKABLE member, or a STEALTHED one that is not DETECTED, is not a victim")
{
	combattest::CombatWorld w;
	startEngine(*w.logic);
	Object *enemy = w.unit("Swordsman", 'B', 500.0f, 650.0f);
	w.frames(2);
	CHECK(AIAttackSquadState::canTargetSquadMember(*enemy));
	static const int stealthed = ObjectTemplateInfoBuilder::objectStatusIndex("STEALTHED");
	static const int detected = ObjectTemplateInfoBuilder::objectStatusIndex("DETECTED");
	REQUIRE(stealthed == 0xF);
	REQUIRE(detected == 0x11);
	enemy->setStatus((unsigned)stealthed, true);
	CHECK_FALSE(AIAttackSquadState::canTargetSquadMember(*enemy));
	enemy->setStatus((unsigned)detected, true);
	CHECK(AIAttackSquadState::canTargetSquadMember(*enemy));
}

TEST_CASE("bfme2camp1 TEAM_GUARD_TEAM (RW 0x7C34BE -> AI command 0x20, privateGuardTeam RW 0x664B93): a unit that never guarded gets the TEAM target type "
		  "and the team's id (AI + 0x68) and enters the guard state, whose machine guards the team's members' mean position (RW 0x949BD3 -> RW 0x7A0ECD)")
{
	combattest::CombatWorld w;
	startEngine(*w.logic);
	ScriptEngine &e = w.logic->scriptEngine();
	Object *a1 = w.unit("Swordsman", 'A', 300.0f, 300.0f);
	Object *a2 = w.unit("Swordsman", 'A', 500.0f, 340.0f);
	w.frames(2);
	AIUpdateInterface *ai = a1->getAIUpdateInterface();
	REQUIRE(ai != nullptr);
	ScriptActions::execute(e, act("TEAM_GUARD_TEAM", { sp(3, qual(w.teamA())), sp(3, "Bob/NoSuchTeam") }));
	CHECK(ai->guardTeam() == 0);
	ScriptActions::execute(e, act("TEAM_GUARD_TEAM", { sp(3, qual(w.teamA())), sp(3, qual(w.teamA())) }));
	CHECK(ai->guardTargetType() == GUARDTARGET_TEAM);
	CHECK(ai->guardTeam() == w.teamA()->getID());
	CHECK(ai->guardMode() == GUARDMODE_NORMAL);
	CHECK(ai->lastCommandSource() == CMD_FROM_SCRIPT);
	REQUIRE(ai->currentStateId() == (unsigned)AI_GUARD);
	const AIGuardState *gs = dynamic_cast<const AIGuardState *>(ai->stateMachine().currentState());
	REQUIRE(gs != nullptr);
	REQUIRE(gs->guardMachine() != nullptr);
	CHECK(gs->guardMachine()->teamToGuard() == w.teamA()->getID());
	// the mean of every member's position (RW 0x7A0ECD: the sums in member order times 1 / n)
	const Coord3D spot = gs->guardMachine()->guardPosition();
	Coord3D mean;
	REQUIRE(ScriptConditions::teamCentreAllMembers(*w.teamA(), mean));
	CHECK(spot.x == mean.x);
	CHECK(spot.y == mean.y);
	CHECK(spot.x == doctest::Approx(0.5 * (a1->getPosition()->x + a2->getPosition()->x)).epsilon(1e-6)); // tolerance: the float rounding of the SSE mean
	CHECK(e.stats().unportedActions.empty());
}

TEST_CASE("bfme2camp1 TEAM_SET_MODELCONDITION_FOR_DURATION (RW 0x7C649A): every member gets the timed bit (RW 0x68B581 -> RW 0x8E2C0F) for _ftol2(5 * "
		  "seconds) frames; the fraction is not read; an unknown condition name does nothing")
{
	combattest::CombatWorld w;
	startEngine(*w.logic);
	ScriptEngine &e = w.logic->scriptEngine();
	Object *a1 = w.unit("Swordsman", 'A', 300.0f, 300.0f);
	Object *a2 = w.unit("Swordsman", 'A', 400.0f, 300.0f);
	w.frames(2);
	int bit = -1;
	for (int i = 0; i < 591 && bit < 0; ++i)
	{
		if (std::string(TheModelConditionNames[i]) == "USER_1")
		{
			bit = i;
		}
	}
	REQUIRE(bit >= 0); // tolerance: a precondition (the name is one of the 591), its index is not what is tested
	ScriptActions::execute(e, act("TEAM_SET_MODELCONDITION_FOR_DURATION", { sp(3, qual(w.teamA())), sp(58, "user_1"), rp(1.5f), rp(0.0f) }));
	CHECK(!a1->testModelCondition(bit)); // the name's case is kept (RW 0x4065AA)
	ScriptActions::execute(e, act("TEAM_SET_MODELCONDITION_FOR_DURATION", { sp(3, qual(w.teamA())), sp(58, "USER_1"), rp(1.5f), rp(0.0f) }));
	CHECK(a1->testModelCondition(bit)); // a fraction of 0 still sets every member's
	CHECK(a2->testModelCondition(bit));
	w.frames(7); // 1.5 s * 5 = 7.5 -> _ftol2 truncates to 7 frames
	CHECK(!a1->testModelCondition(bit));
	CHECK(!a2->testModelCondition(bit));
	CHECK(e.stats().unportedActions.empty());
}

TEST_CASE("bfme2camp1 TutorialSettings: GameData's Basic / Advanced tutorial records (rows RW 0xC00E00 .. 0xC00ED0): a quoted Map loses its quotes, the "
		  "objective appends, the fade-up is a duration in logic frames; OnTutorial's argument picks the record by exact name (RW 0x91B825)")
{
	const std::string text = "GameData\n"
							 "  BasicTutorialMap = \"Maps\\MAP Beginner Tutorial\\MAP Beginner Tutorial.map\"\n"
							 "  BasicTutorialLoadScreenStillImage = CampaignGoodBasic_Tutorial_LoadScreen\n"
							 "  BasicTutorialLoadScreenMusicTrack = Shell2Music\n"
							 "  BasicTutorialObjective = SCRIPT:A\n"
							 "  BasicTutorialObjective = SCRIPT:B\n"
							 "  BasicTutorialMillisecondsAfterStartToStartFadeUp = 1400\n"
							 "  AdvancedTutorialMap = \"Maps\\MAP Advanced Tutorial\\MAP Advanced Tutorial.map\"\n"
							 "  SomeOtherField = 3\n"
							 "End\n";
	TutorialSettings t;
	std::string error;
	REQUIRE_MESSAGE(TutorialSettings::scan(text, t, &error), error);
	CHECK(t.basic.map == "Maps\\MAP Beginner Tutorial\\MAP Beginner Tutorial.map");
	CHECK(t.basic.loadScreenStillImage == "CampaignGoodBasic_Tutorial_LoadScreen");
	CHECK(t.basic.loadScreenMusicTrack == "Shell2Music");
	CHECK(t.basic.objective == std::vector<std::string>{ "SCRIPT:A", "SCRIPT:B" });
	CHECK(t.basic.fadeUpFrames == 7u); // 1400 ms: ceil(1400 * 0.005) = 7 frames (parseDurationUnsignedInt RW 0x73A429, LOGICFRAMES_PER_SECOND 5)
	CHECK(t.basic.loadMovie.empty());
	CHECK(t.advanced.map == "Maps\\MAP Advanced Tutorial\\MAP Advanced Tutorial.map");
	CHECK(t.record("Basic") == &t.basic);
	CHECK(t.record("Advanced") == &t.advanced);
	CHECK(t.record("basic") == nullptr);     // strcmp: the case is kept
	CHECK(t.record("Strategic") == nullptr); // the War of the Ring tutorial is not a GameData record
}

TEST_CASE("bfme2camp1 ScriptEngine::transferObjectName (RW 0x759467): the receiver takes the name and the cache entry of that name points at it; a named "
		  "receiver's own entry first points at nothing (RW 0x603912); an unknown name adds no entry; an empty name does nothing")
{
	combattest::CombatWorld w;
	startEngine(*w.logic);
	ScriptEngine &e = w.logic->scriptEngine();
	Object *a = w.unit("Swordsman", 'A', 300.0f, 300.0f);
	Object *b = w.unit("Swordsman", 'A', 400.0f, 300.0f);
	Object *c = w.unit("Swordsman", 'A', 500.0f, 300.0f);
	a->setName("Lair");
	c->setName("Other");
	REQUIRE(e.getUnitNamed("Lair") == a);
	e.transferObjectName("Lair", *b);
	CHECK(b->getName() == "Lair");
	CHECK(e.getUnitNamed("Lair") == b);
	CHECK(a->getName() == "Lair"); // the former holder keeps its own name (retail only resets its colour, client)
	e.transferObjectName("Lair", *c); // a named receiver: its "Other" entry points at nothing
	CHECK(c->getName() == "Lair");
	CHECK(e.getUnitNamed("Lair") == c);
	CHECK(e.getUnitNamed("Other") == nullptr);
	CHECK(e.didUnitExist("Other"));
	e.transferObjectName("Fresh", *a); // no entry of that name: the object is named, the cache gets nothing
	CHECK(a->getName() == "Fresh");
	CHECK(e.getUnitNamed("Fresh") == nullptr);
	e.transferObjectName("", *b);
	CHECK(b->getName() == "Lair");
}

TEST_CASE("bfme2camp1 AIUpdateInterface::transferAttack (RW 0x66826E): the current victim and the machine's goal object move from the first object to the "
		  "second; another victim stays")
{
	combattest::CombatWorld w;
	startEngine(*w.logic);
	Object *u = w.unit("Swordsman", 'A', 500.0f, 500.0f);
	Object *v = w.unit("Swordsman", 'B', 500.0f, 620.0f);
	Object *x = w.unit("Swordsman", 'B', 500.0f, 700.0f);
	w.frames(2);
	AIUpdateInterface *ai = u->getAIUpdateInterface();
	REQUIRE(ai != nullptr);
	REQUIRE(ai->aiAttackObject(v, CMD_FROM_SCRIPT, 0x7FFFFFFF));
	REQUIRE(ai->currentVictimId() == v->getID());
	REQUIRE(ai->stateMachine().goalObjectID() == v->getID());
	ai->transferAttack(x->getID(), v->getID()); // not the victim: nothing changes
	CHECK(ai->currentVictimId() == v->getID());
	ai->transferAttack(v->getID(), x->getID());
	CHECK(ai->currentVictimId() == x->getID());
	CHECK(ai->stateMachine().goalObjectID() == x->getID());
}
