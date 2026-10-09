// OpenBFME unit tests (lane MODULES-3): the emotion AI states, the RotWK temporary state, aiDoCommand's gate, the safe path and the horde's emotion slots.
//
// Synthetic tests (no retail data) follow the binary's steps by hand: the temporary state RW 0x74168E with its -1 / -2 durations and the machine's refusals
// (RW 0x751E3A / 0x75464E / 0x751DFC), the command gate RW 0x667174, the nugget's AI hooks RW 0x662FC8 / 0x663053 / 0x66309C, the run-away-panic state (RW 0x74E3E7,
// requestSafePath RW 0x663C6B, findSafePath RW 0x6FCE1A), the back-up records (RW 0x878905) and the member pass branches (RW 0x873FE8). The retail scenarios run
// the real RotWK data when ROTWK_INSTALL and BFME2_INSTALL are set (otherwise SKIP): Aragorn's Horn of Elendil, the troll's fear aura, a terrified horde's orders.

#include "doctest.h"

#include "Mod2TestUtil.h"
#include "MoveTestUtil.h"

#include "Common/SpecialPower.h"
#include "Common/StateHash.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/AI/AIEmotionStates.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/Module/EmotionModules.h"
#include "GameLogic/Module/NotifyCrushModules.h"
#include "GameLogic/Module/SpecialAbilityModules.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/System/EmotionSystem.h"

#include <cmath>
#include <string>
#include <vector>

namespace
{
const char kExtraObjects[] =
	"Object Scarer\n"
	"  KindOf = INFANTRY SELECTABLE SCARY\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n"
	"Object CowerHorde\n"
	"  KindOf = HORDE MELEE_HORDE LARGE_RECTANGLE_PATHFIND SELECTABLE\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 30\n"
	"  GeometryMinorRadius = 45\n"
	"  GeometryHeight = 20\n"
	"  Behavior = HordeAIUpdate ModuleTag_AI\n"
	"  End\n"
	"  Behavior = HordeContain ModuleTag_Contain\n"
	"    RankInfo = RankNumber:1 UnitType:MemberOfHorde Position:X:50 Y:0 Position:X:50 Y:20 Position:X:50 Y:-20 Position:X:50 Y:40 Position:X:50 Y:-40\n"
	"    InitialPayload = MemberOfHorde 5\n"
	"    BackUpMinDelayTime = 1\n"
	"    BackUpMaxDelayTime = 1000\n"
	"    BackUpMinDistance = 1\n"
	"    BackUpMaxDistance = 3\n"
	"    BackupPercentage = 80%\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = HordeLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 50\n"
	"  End\n"
	"End\n";

struct EmoAI : movetest::MoveWorld
{
	EmoAI()
		: movetest::MoveWorld(200, 200, kExtraObjects)
	{
		buildMap();
		logic->partition().setRegion(0.0f, 0.0f, 2000.0f, 2000.0f);
		frames(1);
	}
	static float dist(const Object *a, const Object *b) { return movetest::dist2d(*a->getPosition(), *b->getPosition()); }
};
} // namespace

TEST_CASE("emotion AI: a locked temporary state (-1) refuses commands, setState, clear and reset; -2 is replaced; the saved goal returns (RW 0x74168E / 0x667174)")
{
	EmoAI f;
	Object *u = f.spawn("Walker", 500.0f, 500.0f);
	Object *s = f.spawn("Scarer", 700.0f, 500.0f, 0.0f, f.teamOf("Bob"));
	AIUpdateInterface *ai = f.aiOf(u);
	ai->aiMoveToPosition(Coord3D{ 900.0f, 900.0f, 0.0f }, CMD_FROM_PLAYER);
	f.frames(1);
	REQUIRE(ai->currentStateId() == (unsigned)AI_MOVE_TO);
	const Coord3D goal = ai->stateMachine().goalPosition();
	// IDLE -> state 42 (busy) locked (RW 0x662FC8)
	ai->emotionEnterAIState(EMOTION_AI_IDLE, s);
	AIStateMachine &m = ai->stateMachine();
	CHECK(m.temporaryStateId() == (unsigned)AI_BUSY);
	CHECK(m.temporaryStateLocked());
	CHECK(m.goalObjectID() == s->getID()); // the source is the goal object while the state runs
	CHECK_FALSE(ai->allowedToRespondToCommand(CMD_FROM_PLAYER, 0));
	CHECK_FALSE(ai->allowedToRespondToCommand(CMD_FROM_AI, -1));
	ai->aiMoveToPosition(Coord3D{ 100.0f, 100.0f, 0.0f }, CMD_FROM_PLAYER); // refused by the gate
	ai->aiIdle(CMD_FROM_AI);
	CHECK(m.temporaryStateId() == (unsigned)AI_BUSY);
	CHECK(ai->currentStateId() == (unsigned)AI_MOVE_TO);
	CHECK(m.setState(AI_IDLE) == STATE_CONTINUE); // RW 0x751E3A refuses
	m.clear();                                       // RW 0x75464E refuses
	CHECK(m.temporaryStateId() == (unsigned)AI_BUSY);
	// the same state again: nothing (RW 0x662FC8 compares the temporary state's id)
	ai->emotionEnterAIState(EMOTION_AI_IDLE, s);
	CHECK(m.temporaryStateLocked());
	// AILockDuration ran out: re-entered until replaced (-2)
	ai->emotionLockElapsed();
	CHECK(m.temporaryStateId() == (unsigned)AI_BUSY);
	CHECK_FALSE(m.temporaryStateLocked());
	CHECK(ai->allowedToRespondToCommand(CMD_FROM_PLAYER, 0));
	// the stop leaves it: the saved goal (no object, the move's position) returns and the move goes on
	ai->emotionLeaveAIState();
	CHECK(m.temporaryStateId() == (unsigned)AI_NO_STATE);
	CHECK(m.goalObjectID() == INVALID_ID);
	CHECK(m.goalPosition().x == goal.x);
	CHECK(m.goalPosition().y == goal.y);
	CHECK(ai->currentStateId() == (unsigned)AI_MOVE_TO);
	// FACE_OBJECT (59) runs until replaced: an order replaces it
	ai->emotionEnterAIState(EMOTION_AI_FACE_OBJECT, s);
	CHECK(m.temporaryStateId() == (unsigned)AI_FACE_OBJECT_IDLE);
	CHECK_FALSE(m.temporaryStateLocked());
	ai->aiIdle(CMD_FROM_PLAYER);
	CHECK(m.temporaryStateId() == (unsigned)AI_NO_STATE);
	CHECK(ai->currentStateId() == (unsigned)AI_IDLE);
	// AVOID_SCARER and unknown values do nothing
	ai->emotionEnterAIState(EMOTION_AI_AVOID_SCARER, s);
	ai->emotionEnterAIState(9, s);
	CHECK(m.temporaryStateId() == (unsigned)AI_NO_STATE);
}

TEST_CASE("emotion AI: PreventPlayerCommands refuses the player's orders only; a dead object refuses every order (RW 0x667174)")
{
	EmoAI f;
	Object *u = f.spawn("Walker", 500.0f, 500.0f);
	AIUpdateInterface *ai = f.aiOf(u);
	ai->setPreventPlayerCommands(true);
	CHECK_FALSE(ai->allowedToRespondToCommand(CMD_FROM_PLAYER, 0));
	CHECK(ai->allowedToRespondToCommand(CMD_FROM_SCRIPT, 0));
	CHECK(ai->allowedToRespondToCommand(CMD_FROM_AI, 0));
	ai->aiMoveToPosition(Coord3D{ 800.0f, 500.0f, 0.0f }, CMD_FROM_PLAYER);
	CHECK(ai->currentStateId() == (unsigned)AI_IDLE);
	ai->aiMoveToPosition(Coord3D{ 800.0f, 500.0f, 0.0f }, CMD_FROM_AI);
	CHECK(ai->currentStateId() == (unsigned)AI_MOVE_TO);
	ai->setPreventPlayerCommands(false);
	CHECK(ai->allowedToRespondToCommand(CMD_FROM_PLAYER, 0));
}

TEST_CASE("emotion AI r2: when a panic ends by itself the interrupted move state's slot 0x1C clears its wait flag and it asks for its goal again (RW 0x751D9A -> 0x740C97)")
{
	// Sol's probe (a panic interrupting a move whose path is still queued). The resume callback is ported: the move state stops waiting and requests a path to
	// (900, 500) in the frame the panic ends. RW's internal move update (RW 0x748E46) then tests the arrival against the AI's path, which is still the panic's safe
	// path (no step of RW 0x748D8A / 0x6627CB / 0x745BFE / 0x667ED1 destroys it), so the move ends where the safe path ends: pinned here as the binary reads (stop
	// S-1027); the probe's expectation that the unit walks on to (900, 500) does not follow from the decompiled flow
	movetest::MoveWorld f(200, 200);
	f.buildMap();
	Object *u = f.spawn("Walker", 500.0f, 500.0f);
	Object *s = f.spawn("Walker", 700.0f, 500.0f);
	f.frames(2);
	AIUpdateInterface *a = u->getAIUpdateInterface();
	f.logic->settings().repulsedDistance = 300.0f;
	a->aiMoveToPosition(Coord3D{ 900.0f, 500.0f, 0.0f }, CMD_FROM_PLAYER);
	REQUIRE(a->mover().isWaitingForPath());
	a->emotionEnterAIState(EMOTION_AI_RUN_AWAY_PANIC, s);
	REQUIRE(a->stateMachine().temporaryStateId() == (unsigned)AI_PANIC);
	int n = 0;
	while (a->stateMachine().inTemporaryState() && n++ < 100)
	{
		f.frames(1);
	}
	REQUIRE_FALSE(a->stateMachine().inTemporaryState());
	const Coord3D asked = a->mover().requestedDestination();
	const Coord3D end = *u->getPosition();
	MESSAGE("panic frames " << n << ", the move asked for " << asked.x << ", " << asked.y << ", the unit stands at " << end.x << ", " << end.y);
	// the resumed move state requested its own (cell-adjusted) goal: the callback cleared + 0x49; the panic's safe request had left the unit's own position here
	CHECK(movetest::dist2d(asked, Coord3D{ 900.0f, 500.0f, 0.0f }) < 1.0f);
	f.frames(160);
	CHECK(a->currentStateId() == (unsigned)AI_IDLE);
	CHECK(movetest::dist2d(*u->getPosition(), end) < 15.0f);
}

TEST_CASE("emotion AI r2: a group move order to a locked unit changes none of its reservations (RW 0x667174 before any change)")
{
	movetest::MoveWorld f;
	f.buildMap();
	Object *u = f.spawn("Walker", 500.0f, 500.0f);
	Object *s = f.spawn("Walker", 700.0f, 500.0f);
	f.frames(2);
	AIUpdateInterface *a = u->getAIUpdateInterface();
	a->emotionEnterAIState(EMOTION_AI_IDLE, s);
	REQUIRE(a->stateMachine().temporaryStateLocked());
	Pathfinder &pf = f.ai->pathfinder();
	Coord3D before{}, after{};
	const bool hadGoal = pf.goalPosition(a->adapter(), &before);
	const int p = f.playerIndex("Alice");
	f.select(p, { u });
	f.moveTo(p, 900.0f, 500.0f);
	f.frames(1);
	const bool hasGoal = pf.goalPosition(a->adapter(), &after);
	CHECK(hadGoal == hasGoal);
	CHECK(before.x == after.x);
	CHECK(before.y == after.y);
	CHECK(a->currentStateId() != (unsigned)AI_MOVE_TO);
}

TEST_CASE("emotion AI r2: a refused attack-move arms nothing; an armed attack-move does not continue while the gate refuses (RW 0x667174)")
{
	{
		movetest::MoveWorld f;
		f.buildMap();
		Object *u = f.spawn("Walker", 500.0f, 500.0f);
		Object *s = f.spawn("Walker", 700.0f, 500.0f);
		f.frames(2);
		AIUpdateInterface *a = u->getAIUpdateInterface();
		a->emotionEnterAIState(EMOTION_AI_IDLE, s);
		REQUIRE(a->stateMachine().temporaryStateLocked());
		const int p = f.playerIndex("Alice");
		f.select(p, { u });
		f.moveTo(p, 900.0f, 500.0f, MSG_DO_ATTACKMOVETO);
		f.frames(1);
		CHECK_FALSE(a->attackMoveActive());
	}
	{
		movetest::MoveWorld f;
		f.buildMap();
		Object *u = f.spawn("Walker", 500.0f, 500.0f);
		f.frames(2);
		AIUpdateInterface *a = u->getAIUpdateInterface();
		REQUIRE(a->armAttackMove(Coord3D{ 900.0f, 500.0f, 0.0f }));
		a->setPreventPlayerCommands(true);
		CHECK_FALSE(a->resumeAttackMove());
		CHECK(a->currentStateId() == (unsigned)AI_IDLE);
		CHECK(a->attackMoveActive()); // still armed: the march goes on once the gate allows it
	}
	{
		movetest::MoveWorld f;
		f.buildMap();
		Object *u = f.spawn("Walker", 500.0f, 500.0f);
		Object *s = f.spawn("Walker", 700.0f, 500.0f);
		f.frames(2);
		AIUpdateInterface *a = u->getAIUpdateInterface();
		REQUIRE(a->armAttackMove(Coord3D{ 900.0f, 500.0f, 0.0f }));
		a->emotionEnterAIState(EMOTION_AI_IDLE, s); // -1
		CHECK_FALSE(a->resumeAttackMove());
		CHECK(a->currentStateId() == (unsigned)AI_IDLE);
	}
}

TEST_CASE("emotion AI r2: an AI order replaces an unlocked emotion state")
{
	movetest::MoveWorld f;
	f.buildMap();
	Object *u = f.spawn("Walker", 500.0f, 500.0f);
	Object *s = f.spawn("Walker", 700.0f, 500.0f);
	f.frames(2);
	AIUpdateInterface *a = u->getAIUpdateInterface();
	a->emotionEnterAIState(EMOTION_AI_FACE_OBJECT, s);
	REQUIRE(a->stateMachine().temporaryStateId() == (unsigned)AI_FACE_OBJECT_IDLE);
	a->aiIdle(CMD_FROM_AI);
	CHECK(a->stateMachine().temporaryStateId() == (unsigned)AI_NO_STATE);
}

TEST_CASE("emotion AI: RUN_AWAY_PANIC runs the safe path out of vision range + RepulsedDistance from the source, PANICKING while it runs, then the unit stops (RW 0x74E3E7 / 0x6FCE1A)")
{
	std::uint32_t hashes[2] = {};
	for (int run = 0; run < 2; ++run)
	{
		EmoAI f;
		f.logic->settings().repulsedDistance = 120.0f; // AIData RepulsedDistance (the Walker has no VisionRange: the radius is 120)
		Object *u = f.spawn("Walker", 500.0f, 500.0f);
		Object *s = f.spawn("Scarer", 540.0f, 500.0f, 0.0f, f.teamOf("Bob"));
		AIUpdateInterface *ai = f.aiOf(u);
		const int panicking = AIUpdateInterface::modelConditionBit("PANICKING");
		CHECK(ai->safePathRadius() == 120.0f);
		ai->emotionEnterAIState(EMOTION_AI_RUN_AWAY_PANIC, s);
		AIStateMachine &m = ai->stateMachine();
		REQUIRE(m.temporaryStateId() == (unsigned)AI_PANIC);
		CHECK(m.temporaryStateLocked());
		CHECK(u->testModelCondition(panicking));
		CHECK(ai->mover().safeRequestPending());
		CHECK(ai->mover().repulsor(0) == (PathfindObjectID)s->getID());
		const float d0 = EmoAI::dist(u, s);
		int framesRun = 0;
		while (m.temporaryStateId() == (unsigned)AI_PANIC && framesRun < 200)
		{
			f.frames(1);
			++framesRun;
		}
		MESSAGE("panic ran " << framesRun << " frames, distance " << d0 << " -> " << EmoAI::dist(u, s));
		CHECK(framesRun < 200);
		CHECK(m.temporaryStateId() == (unsigned)AI_NO_STATE); // the move succeeded: the temporary state ends (no transition to 21)
		CHECK_FALSE(u->testModelCondition(panicking));
		CHECK(EmoAI::dist(u, s) > 110.0f); // the safe cell is beyond the radius (a cell's half diagonal of slack)
		CHECK(ai->currentStateId() == (unsigned)AI_IDLE);
		hashes[run] = f.logic->computeStateHash();
	}
	CHECK(hashes[0] == hashes[1]);
}

TEST_CASE("emotion AI: the panic state as a normal state goes on to the cower state, which draws Random(MinCowerTime, MaxCowerTime) (\"AIStates.cpp\" 0xFDA) and faces the source")
{
	EmoAI f;
	Object *u = f.spawn("Walker", 500.0f, 500.0f);
	Object *s = f.spawn("Scarer", 540.0f, 500.0f, 0.0f, f.teamOf("Bob"));
	AIUpdateInterface *ai = f.aiOf(u);
	AIStateMachine &m = ai->stateMachine();
	m.setGoalObject(s->getID());
	f.logic->random().enableCallLog(true);
	m.setState(AI_PANIC);
	int n = 0;
	while (ai->currentStateId() != (unsigned)AI_PANIC_COWER && n < 200)
	{
		f.frames(1);
		++n;
	}
	REQUIRE(ai->currentStateId() == (unsigned)AI_PANIC_COWER);
	const int afraid = AIUpdateInterface::modelConditionBit("EMOTION_AFRAID");
	CHECK(u->testModelCondition(afraid));
	n = 0;
	while (ai->currentStateId() == (unsigned)AI_PANIC_COWER && n < 200)
	{
		f.frames(1);
		++n;
	}
	CHECK(ai->currentStateId() == (unsigned)AI_IDLE); // MinCowerTime = MaxCowerTime = 0: the cower ends one frame after the walk
	CHECK_FALSE(u->testModelCondition(afraid));
	size_t cowerDraws = 0;
	for (const auto &c : f.logic->random().callLog())
	{
		cowerDraws += (c.file == "AIStates.cpp" && c.line == 0xFDA) ? 1u : 0u;
	}
	CHECK(cowerDraws == 1);
}

TEST_CASE("emotion AI: BACK_AWAY on a horde records its members' back-up points (HordeContain.cpp 0x1FD8 / 0x1FDD / 0x1FDE), they back away facing the scarer; a move drops the records (RW 0x878905 / 0x873155)")
{
	std::uint32_t hashes[2] = {};
	for (int run = 0; run < 2; ++run)
	{
		EmoAI f;
		Object *h = f.spawn("CowerHorde", 500.0f, 500.0f);
		Object *s = f.spawn("Scarer", 700.0f, 500.0f, 0.0f, f.teamOf("Bob"));
		f.frames(30); // the members reach their slots
		HordeContain *hc = f.hordeOf(h);
		REQUIRE(hc);
		const std::vector<Object *> members = f.membersOf(h);
		REQUIRE(members.size() == 5);
		AIUpdateInterface *ai = f.aiOf(h);
		f.logic->random().enableCallLog(true);
		ai->emotionEnterAIState(EMOTION_AI_BACK_AWAY, s);
		REQUIRE(ai->stateMachine().temporaryStateId() == (unsigned)AI_BACK_AWAY);
		CHECK(hc->isCowering());
		unsigned pct = 0, distance = 0, delay = 0;
		for (const auto &c : f.logic->random().callLog())
		{
			pct += (c.file == "HordeContain.cpp" && c.line == 0x1FD8) ? 1u : 0u;
			distance += (c.file == "HordeContain.cpp" && c.line == 0x1FDD) ? 1u : 0u;
			delay += (c.file == "HordeContain.cpp" && c.line == 0x1FDE) ? 1u : 0u;
		}
		CHECK(pct == 5); // CowerRadius 0: every member draws its chance
		CHECK(distance == hc->backUpRecords().size());
		CHECK(delay == hc->backUpRecords().size());
		REQUIRE_FALSE(hc->backUpRecords().empty());
		std::vector<float> before;
		for (Object *m : members)
		{
			before.push_back(EmoAI::dist(m, s));
		}
		// every record is 10 .. 30 further from the scarer along scarer -> member
		for (const auto &kv : hc->backUpRecords())
		{
			Object *m = f.logic->findObjectByID(kv.first);
			REQUIRE(m);
			const float away = movetest::dist2d(kv.second.position, *s->getPosition()) - EmoAI::dist(m, s);
			CHECK(away >= 9.9f);
			CHECK(away <= 30.1f);
		}
		f.frames(25);
		unsigned backed = 0;
		for (size_t i = 0; i < members.size(); ++i)
		{
			const bool recorded = hc->backUpRecords().count(members[i]->getID()) != 0;
			if (recorded && EmoAI::dist(members[i], s) > before[i] + 5.0f)
			{
				++backed;
			}
			// every member faces the scarer (the pass turns them; the locomotor finishes the turn)
			CHECK(std::fabs(emotionRelAngle(*members[i], *s->getPosition())) < 0.2f);
		}
		CHECK(backed == hc->backUpRecords().size());
		// the stop: the state leaves, the horde stops cowering, the records stay until the horde moves
		ai->emotionLeaveAIState();
		CHECK_FALSE(hc->isCowering());
		CHECK_FALSE(hc->backUpRecords().empty());
		ai->aiMoveToPosition(Coord3D{ 400.0f, 500.0f, 0.0f }, CMD_FROM_PLAYER);
		f.frames(2);
		CHECK(hc->backUpRecords().empty());
		hashes[run] = f.logic->computeStateHash();
	}
	CHECK(hashes[0] == hashes[1]);
}

TEST_CASE("emotion AI: FACE_OBJECT turns a horde's members to the source (face point, RW 0x86C1C6); QUARREL picks fighters and spectators (HordeContain.cpp 0x204F / 0x206A)")
{
	EmoAI f;
	Object *h = f.spawn("CowerHorde", 500.0f, 500.0f);
	Object *s = f.spawn("Scarer", 500.0f, 800.0f, 0.0f, f.teamOf("Bob"));
	f.frames(30);
	HordeContain *hc = f.hordeOf(h);
	AIUpdateInterface *ai = f.aiOf(h);
	ai->emotionEnterAIState(EMOTION_AI_FACE_OBJECT, s);
	REQUIRE(ai->stateMachine().temporaryStateId() == (unsigned)AI_FACE_OBJECT_IDLE);
	f.frames(20);
	CHECK(hc->hasFacePoint());
	for (Object *m : f.membersOf(h))
	{
		CHECK(std::fabs(emotionRelAngle(*m, *s->getPosition())) < 0.2f);
	}
	ai->emotionLeaveAIState();
	CHECK_FALSE(hc->hasFacePoint());
	// the quarrel
	f.logic->random().enableCallLog(true);
	ai->emotionEnterAIState(EMOTION_AI_QUARREL, s);
	REQUIRE(ai->stateMachine().temporaryStateId() == (unsigned)AI_QUARREL);
	CHECK(hc->quarrelFighter(0) != INVALID_ID);
	CHECK(hc->quarrelFighter(1) != INVALID_ID);
	CHECK(hc->quarrelFighter(0) != hc->quarrelFighter(1));
	CHECK(hc->quarrelDistances().size() == 3);
	for (const auto &kv : hc->quarrelDistances())
	{
		CHECK(kv.second >= 50);
		CHECK(kv.second <= 60);
	}
	unsigned pick = 0, spread = 0;
	for (const auto &c : f.logic->random().callLog())
	{
		pick += (c.file == "HordeContain.cpp" && c.line == 0x204F) ? 1u : 0u;
		spread += (c.file == "HordeContain.cpp" && c.line == 0x206A) ? 1u : 0u;
	}
	CHECK(pick == 1);
	CHECK(spread == 3);
	f.frames(20);
	const int fighting = AIUpdateInterface::modelConditionBit("QUARRELSOME_FIGHTING");
	Object *a = f.logic->findObjectByID(hc->quarrelFighter(0));
	Object *b = f.logic->findObjectByID(hc->quarrelFighter(1));
	REQUIRE(a);
	REQUIRE(b);
	MESSAGE("quarrel fighters " << EmoAI::dist(a, b) << " apart");
	CHECK(EmoAI::dist(a, b) < 25.0f); // A walks to 20 from B
	CHECK(a->testModelCondition(fighting));
	CHECK(b->testModelCondition(fighting));
	ai->emotionLeaveAIState(); // the quarrel ends: the conditions leave
	CHECK_FALSE(a->testModelCondition(fighting));
	CHECK(hc->quarrelDistances().empty());
	f.frames(1);
	CHECK(hc->quarrelFighter(1) == INVALID_ID); // the update clears B once A is gone (RW 0x8730F7)
}

// ---- retail scenarios ----------------------------------------------------------------------------------------------------------------------------------------------
namespace
{
SpecialPowerModule *powerOf(Object &o, const char *templateName)
{
	for (const auto &m : o.modules())
	{
		SpecialPowerModule *sp = dynamic_cast<SpecialPowerModule *>(m.get());
		if (sp && sp->getSpecialPowerTemplate() && sp->getSpecialPowerTemplate()->getName() == templateName)
		{
			return sp;
		}
	}
	return nullptr;
}

float meanMemberDistance(mod2test::RetailGame &g, Object *horde, const Object *from)
{
	float sum = 0.0f;
	const std::vector<Object *> ms = g.members(horde);
	for (Object *m : ms)
	{
		sum += movetest::dist2d(*m->getPosition(), *from->getPosition());
	}
	return ms.empty() ? 0.0f : sum / (float)ms.size();
}
} // namespace

TEST_CASE("emotion AI retail: Aragorn's Horn of Elendil (GenerateTerror) makes two orc hordes run away from him (Terror_Base: RUN_AWAY_PANIC, locked), then they recover")
{
	if (!hudtest::haveWorld("emotion AI retail"))
	{
		return;
	}
	hudtest::SharedWorld &sh = hudtest::shared();
	auto scope = sh.world->enterContext();
	std::uint32_t hashes[2] = {};
	for (int run = 0; run < 2; ++run)
	{
		mod2test::RetailGame g(sh);
		Object *aragorn = g.make("GondorAragorn", "Men", 500.0f, 500.0f);
		Object *orcs1 = g.make("MordorFighterHorde", "Mordor", 600.0f, 500.0f);
		Object *orcs2 = g.make("MordorFighterHorde", "Mordor", 500.0f, 620.0f);
		g.run(10);
		SpecialPowerModule *horn = powerOf(*aragorn, "SpecialAbilityAragornElendil");
		REQUIRE(horn);
		while (horn->pauseCount() > 0)
		{
			horn->pauseCountdown(false); // the level-2 unlock (UnpauseSpecialPowerUpgrade)
		}
		horn->setReadyFrame(0);
		const float near1 = meanMemberDistance(g, orcs1, aragorn), near2 = meanMemberDistance(g, orcs2, aragorn);
		horn->doSpecialPower(0);
		AIUpdateInterface *ai1 = orcs1->getAIUpdateInterface(), *ai2 = orcs2->getAIUpdateInterface();
		int frame = 0, firstPanic = -1, lastPanic = -1;
		bool lockedSeen = false, refusedSeen = false;
		float far1 = 0.0f, far2 = 0.0f;
		for (; frame < 120; ++frame)
		{
			g.run(1);
			const bool p1 = ai1->stateMachine().temporaryStateId() == (unsigned)AI_PANIC, p2 = ai2->stateMachine().temporaryStateId() == (unsigned)AI_PANIC;
			if (p1 && p2 && firstPanic < 0)
			{
				firstPanic = frame;
				lockedSeen = ai1->stateMachine().temporaryStateLocked() && ai2->stateMachine().temporaryStateLocked();
				refusedSeen = !ai1->allowedToRespondToCommand(CMD_FROM_PLAYER, 0) && ai1->preventPlayerCommands();
			}
			if (p1 || p2)
			{
				lastPanic = frame;
			}
			far1 = std::max(far1, meanMemberDistance(g, orcs1, aragorn));
			far2 = std::max(far2, meanMemberDistance(g, orcs2, aragorn));
		}
		MESSAGE("horn: panic from frame " << firstPanic << " to " << lastPanic << "; horde 1 " << near1 << " -> " << far1 << ", horde 2 " << near2 << " -> " << far2);
		REQUIRE(firstPanic >= 0);
		CHECK(lockedSeen);
		CHECK(refusedSeen);
		CHECK(far1 > near1 + 60.0f);
		CHECK(far2 > near2 + 60.0f);
		CHECK(lastPanic < 119); // recovered
		CHECK(lastPanic - firstPanic <= 51); // Terror_Base's Duration (10 s) ends the state at the latest
		// recovered: no temporary state, the player's orders are taken again, PANICKING gone
		const int panicking = AIUpdateInterface::modelConditionBit("PANICKING");
		for (Object *h : { orcs1, orcs2 })
		{
			AIUpdateInterface *ai = h->getAIUpdateInterface();
			CHECK(ai->stateMachine().temporaryStateId() == (unsigned)AI_NO_STATE);
			CHECK_FALSE(ai->preventPlayerCommands());
			CHECK(ai->allowedToRespondToCommand(CMD_FROM_PLAYER, 0));
			CHECK_FALSE(h->testModelCondition(panicking));
		}
		hashes[run] = g.logic.computeStateHash();
	}
	CHECK(hashes[0] == hashes[1]);
}

TEST_CASE("emotion AI retail: the MordorAttackTroll's fear aura makes an idle Gondor horde back away (FearIdle_Base: BACK_AWAY) facing the troll; it holds until the horde moves")
{
	if (!hudtest::haveWorld("emotion AI retail"))
	{
		return;
	}
	hudtest::SharedWorld &sh = hudtest::shared();
	auto scope = sh.world->enterContext();
	std::uint32_t hashes[2] = {};
	for (int run = 0; run < 2; ++run)
	{
		mod2test::RetailGame g(sh);
		Object *horde = g.make("GondorFighterHorde", "Men", 300.0f, 300.0f);
		g.run(20);
		Object *troll = g.make("MordorAttackTroll", "Mordor", 450.0f, 300.0f);
		const float d0 = meanMemberDistance(g, horde, troll);
		AIUpdateInterface *ai = horde->getAIUpdateInterface();
		HordeContain *hc = dynamic_cast<HordeContain *>(horde->findModule("HordeContain"));
		REQUIRE(hc);
		bool backedAway = false, cowered = false;
		size_t records = 0;
		for (int i = 0; i < 40; ++i)
		{
			g.run(1);
			cowered = cowered || (ai->stateMachine().temporaryStateId() == (unsigned)AI_BACK_AWAY && hc->isCowering());
			records = std::max(records, hc->backUpRecords().size());
		}
		const float d1 = meanMemberDistance(g, horde, troll);
		backedAway = d1 > d0 + 5.0f;
		MESSAGE("fear aura: back-up records " << records << ", mean member distance " << d0 << " -> " << d1);
		CHECK(cowered);
		CHECK(records > 0);
		CHECK(backedAway);
		// the order to move ends it: the records go, the members re-form
		ai->aiMoveToPosition(Coord3D{ 200.0f, 300.0f, 0.0f }, CMD_FROM_PLAYER);
		g.run(3);
		CHECK(hc->backUpRecords().empty());
		hashes[run] = g.logic.computeStateHash();
	}
	CHECK(hashes[0] == hashes[1]);
}

TEST_CASE("emotion AI retail: a terrified horde ignores the player's move order (PreventPlayerCommands and the locked state), the AI's own orders too; after the terror it obeys")
{
	if (!hudtest::haveWorld("emotion AI retail"))
	{
		return;
	}
	hudtest::SharedWorld &sh = hudtest::shared();
	auto scope = sh.world->enterContext();
	mod2test::RetailGame g(sh);
	Object *orcs = g.make("MordorFighterHorde", "Mordor", 500.0f, 500.0f);
	Object *aragorn = g.make("GondorAragorn", "Men", 560.0f, 500.0f);
	g.run(10);
	EmotionTrackerUpdate::requestEmotion(*orcs, EMOTION_TERROR, aragorn, 1);
	g.run(2);
	AIUpdateInterface *ai = orcs->getAIUpdateInterface();
	REQUIRE(ai->stateMachine().temporaryStateId() == (unsigned)AI_PANIC);
	const Coord3D order{ 300.0f, 900.0f, 0.0f };
	ai->aiMoveToPosition(order, CMD_FROM_PLAYER);
	CHECK(ai->stateMachine().temporaryStateId() == (unsigned)AI_PANIC);
	CHECK(ai->currentStateId() != (unsigned)AI_MOVE_TO);
	ai->aiMoveToPosition(order, CMD_FROM_AI); // the lock refuses the AI too
	CHECK(ai->currentStateId() != (unsigned)AI_MOVE_TO);
	g.run(60);
	CHECK(ai->stateMachine().temporaryStateId() == (unsigned)AI_NO_STATE);
	ai->aiMoveToPosition(order, CMD_FROM_PLAYER);
	CHECK(ai->currentStateId() == (unsigned)AI_MOVE_TO);
}

TEST_CASE("emotion AI retail: the attack troll walking at a Gondor horde warns it (NotifyTargetsOfImminentProbableCrushingUpdate: BRACE_FOR_BEING_CRUSHED) and cheers (RW 0x8D2B21)")
{
	if (!hudtest::haveWorld("emotion AI retail"))
	{
		return;
	}
	hudtest::SharedWorld &sh = hudtest::shared();
	auto scope = sh.world->enterContext();
	std::uint32_t hashes[2] = {};
	for (int run = 0; run < 2; ++run)
	{
		mod2test::RetailGame g(sh);
		Object *horde = g.make("GondorFighterHorde", "Men", 500.0f, 500.0f);
		Object *troll = g.make("MordorAttackTroll", "Mordor", 250.0f, 500.0f);
		g.run(10);
		NotifyCrushUpdate *n = nullptr;
		for (const auto &m : troll->modules())
		{
			n = n ? n : dynamic_cast<NotifyCrushUpdate *>(m.get());
		}
		REQUIRE(n);
		CHECK(n->scans() == 0); // a troll standing still builds no box
		g.logic.random().enableCallLog(true);
		troll->getAIUpdateInterface()->aiMoveToPosition(Coord3D{ 700.0f, 500.0f, 0.0f }, CMD_FROM_PLAYER);
		EmotionTrackerUpdate *ht = EmotionTrackerUpdate::of(*horde);
		REQUIRE(ht);
		bool braced = false;
		int frame = 0;
		for (; frame < 60 && !braced; ++frame)
		{
			g.run(1);
			braced = ht->current() && ht->current()->tmpl().type == EMOTION_BRACE_FOR_BEING_CRUSHED;
		}
		size_t draws = 0;
		for (const auto &c : g.logic.random().callLog())
		{
			draws += c.file == "NotifyTargetsOfImminentProbableCrushingMux.cpp" && c.line == 0x3F ? 1u : 0u;
		}
		MESSAGE("crush warning: scans " << n->scans() << ", warnings " << n->warned() << ", braced at frame " << frame << ", mux draws " << draws);
		CHECK(n->scans() > 0);
		CHECK(n->warned() > 0);
		CHECK(braced);
		CHECK(draws > 0);
		hashes[run] = g.logic.computeStateHash();
	}
	CHECK(hashes[0] == hashes[1]);
}

TEST_CASE("emotion AI retail: the taunt scan finds an enemy within TauntAndPointDistance: TAUNT is requested, Taunt_Base runs FACE_OBJECT and the members turn to it (RW 0x8B5946)")
{
	if (!hudtest::haveWorld("emotion AI retail"))
	{
		return;
	}
	hudtest::SharedWorld &sh = hudtest::shared();
	auto scope = sh.world->enterContext();
	std::uint32_t hashes[2] = {};
	for (int run = 0; run < 2; ++run)
	{
		mod2test::RetailGame g(sh);
		Object *gondor = g.make("GondorFighterHorde", "Men", 400.0f, 400.0f);
		g.run(20);
		Object *orcs = g.make("MordorFighterHorde", "Mordor", 400.0f, 490.0f);
		EmotionTrackerUpdate *t = EmotionTrackerUpdate::of(*gondor);
		REQUIRE(t);
		bool requested = false, taunting = false, faced = false, orderEnded = false;
		std::string ran;
		for (int i = 0; i < 40; ++i)
		{
			g.run(1);
			requested = requested || t->requested(EMOTION_TAUNT) || t->requested(EMOTION_POINT);
			if (t->current() && (t->current()->type() == EMOTION_TAUNT || t->current()->type() == EMOTION_POINT))
			{
				taunting = true;
				ran = t->current()->tmpl().name;
				const bool facingNow = gondor->getAIUpdateInterface()->stateMachine().temporaryStateId() == (unsigned)AI_FACE_OBJECT_IDLE;
				if (facingNow && !faced)
				{
					// an order that replaces the face state (-2) also ends the tracker's nugget (RW 0x751DA9 -> 0x8B4FA1)
					gondor->getAIUpdateInterface()->aiMoveToPosition(Coord3D{ 300.0f, 300.0f, 0.0f }, CMD_FROM_PLAYER);
					CHECK(gondor->getAIUpdateInterface()->stateMachine().temporaryStateId() == (unsigned)AI_NO_STATE);
					CHECK_FALSE(t->current());
					orderEnded = true;
				}
				faced = faced || facingNow;
			}
		}
		MESSAGE("taunt scan: requested " << requested << ", ran '" << ran << "', face state " << faced << ", scans " << g.logic.emotions().tauntScans());
		CHECK(requested);
		CHECK(taunting);
		CHECK(faced);
		(void)orcs;
		CHECK(orderEnded);
		hashes[run] = g.logic.computeStateHash();
	}
	CHECK(hashes[0] == hashes[1]);
}
