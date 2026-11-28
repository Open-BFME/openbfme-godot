// OpenBFME unit tests. GPL-3.0.
// Lane APPROACHGIVEUP-1 (S-7162; RotWK 2.01 and BFME2 1.06: AIAttackApproachTargetState::updateInternal RW 0x749D46 = BFME2 0x748356, BFME2 decomp
// AIAttackApproachTargetStateOnEnter.cpp:1808, tier B same shape): the attack approach gives a victim up
//   (a) RW 0x749FB2 .. 0x74A0DF: a fleeing victim a mood scan chose (AI + 0x3C7, set by the idle scan RW 0x755539, cleared by every command RW 0x66A9EE)
//       farther than StopChaseDistance (module data + 0x20, default 500 RW 0x66E92A) fails the approach; a player's order on the same victim keeps chasing;
//   (b) RW 0x74A11F .. 0x74A1FC: a move that ended out of range with neither side moved more than 10 and no approach to the victim
//       (Pathfinder::canApproachToTarget RW 0x6FB754) waits ten seconds (50 frames), then fails;
//   (c) RW 0x749F65: a StandGround unit on a raised layer without a quick path to its victim waits ten seconds, then fails (onEnter RW 0x74E6CE: two).
// Synthetic data (no retail files).
#include "doctest.h"
#include "CombatTestUtil.h"

#include "GameLogic/AI/AIAttack.h"
#include "GameLogic/AI/AIStateMachine.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/SimMath.h"

#include <cmath>

using namespace combattest;

namespace
{
// a fast unarmed enemy (no CAN_ATTACK: it never fights back) and a StandGround archer
const char kExtra[] =
	"Object Runner\n"
	"  KindOf = INFANTRY SELECTABLE SCORE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = PlainArmor\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100000\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  Behavior = PhysicsBehavior ModuleTag_Physics\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 110\n"
	"  End\n"
	"End\n"
	"Object HoldArcher\n"
	"  KindOf = INFANTRY SELECTABLE CAN_ATTACK SCORE\n"
	"  VisionRange = 250\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = PlainArmor\n"
	"  End\n"
	"  WeaponSet\n"
	"    Conditions = None\n"
	"    Weapon = PRIMARY BowWeapon\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 60\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"    StandGround = Yes\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n";

float distance3(const Object *a, const Object *b)
{
	const Coord3D &p = *a->getPosition(), &q = *b->getPosition();
	return (float)SimMath::length3d(SimMath::subf32(p.x, q.x), SimMath::subf32(p.y, q.y), SimMath::subf32(p.z, q.z));
}

// a 2-cell cliff band across the whole map at x 400 .. 420: everything east of it is unreachable from the west
void cliffBand(CombatWorld &w)
{
	for (int y = 0; y < 100; ++y)
	{
		w.terrain.cliff.insert({ 40, y });
		w.terrain.cliff.insert({ 41, y });
	}
}

bool attacking(AIUpdateInterface *ai) { return ai->currentStateId() == (unsigned)AI_ATTACK_OBJECT; }
} // namespace

TEST_CASE("approachgiveup1: StopChaseDistance defaults to 500 (the module data constructor RW 0x66E92A, RW 0xBDD97C)")
{
	CombatWorld w(kExtra);
	Object *a = w.unit("Archer", 'A', 300, 300);
	CHECK(a->getAIUpdateInterface()->combatSettings().stopChaseDistance == 500.0f);
}

TEST_CASE("approachgiveup1 (a): an idle-acquired victim that flees is given up beyond StopChaseDistance; a player's order keeps chasing it")
{
	for (const bool playerOrder : { false, true })
	{
		CAPTURE(playerOrder);
		CombatWorld w(kExtra);
		w.combat().setAutoAcquireEnabled(!playerOrder);
		Object *archer = w.unit("Archer", 'A', 100, 500);
		Object *runner = w.unit("Runner", 'B', 280, 500); // inside the bow's 200: the idle scan finds it
		w.frames(2);
		AIUpdateInterface *ai = archer->getAIUpdateInterface();
		if (playerOrder)
		{
			ai->aiAttackObject(runner, CMD_FROM_PLAYER);
			CHECK_FALSE(ai->victimAutoAcquired()); // a command clears AI + 0x3C7
		}
		bool acquired = playerOrder;
		bool fleeing = false;
		float lastAttackDistance = 0.0f;
		float farthest = 0.0f;
		bool gaveUp = false;
		for (int f = 0; f < 200 && !gaveUp; ++f)
		{
			w.frames(1);
			if (attacking(ai))
			{
				lastAttackDistance = distance3(archer, runner);
				farthest = lastAttackDistance > farthest ? lastAttackDistance : farthest;
				if (!acquired)
				{
					acquired = true;
					CHECK(ai->victimAutoAcquired()); // RW 0x755539: the idle scan's attack sets it
				}
				if (!fleeing)
				{
					fleeing = true; // the victim runs east, away from the archer, once the archer has it
					runner->getAIUpdateInterface()->aiMoveToPosition(Coord3D{ 990.0f, 500.0f, 0.0f }, CMD_FROM_PLAYER);
				}
			}
			else if (acquired)
			{
				gaveUp = true;
			}
		}
		REQUIRE(acquired);
		if (playerOrder)
		{
			CHECK_FALSE(gaveUp); // no flag: the port's and retail's approach chases on
			CHECK(farthest > 600.0f); // tolerance: the rule is "farther than 500"; the chase passed it by 100 before the runner stopped at the map's edge
		}
		else
		{
			CHECK(gaveUp); // before the lane the archer chased the runner across the map
			// the limit is tested before the update's move: the last frame still attacking ends one frame's gap growth (22 - 11) past 500 at most
			CHECK(lastAttackDistance > 500.0f);  // tolerance: the rule's own threshold (measured 502.667694, the frame before the give-up)
			CHECK(lastAttackDistance < 511.0f);  // tolerance: within one frame's gap growth of the threshold
			CHECK(farthest == lastAttackDistance); // the gap only grew until the give-up
			CHECK(ai->currentStateId() == (unsigned)AI_IDLE);
		}
	}
}

TEST_CASE("approachgiveup1 (b): an attacker on an island that cannot get nearer to its victim gives it up ten seconds after its last move ends")
{
	CombatWorld w(kExtra);
	w.combat().setAutoAcquireEnabled(false);
	// a cliff ring around the 40 x 40 island x 180 .. 220, y 480 .. 520: the archer walks to its east shore, then its next move ends where it began
	// (neither side moved more than 10) and no cell near the victim can be reached (RW 0x6FB754 false)
	for (int x = 17; x <= 22; ++x)
	{
		for (int y = 47; y <= 52; ++y)
		{
			if (x == 17 || x == 22 || y == 47 || y == 52)
			{
				w.terrain.cliff.insert({ x, y });
			}
		}
	}
	w.buildMap();
	Object *archer = w.unit("Archer", 'A', 200, 500);
	Object *victim = w.unit("Runner", 'B', 700, 500);
	w.frames(2);
	AIUpdateInterface *ai = archer->getAIUpdateInterface();
	ai->aiAttackObject(victim, CMD_FROM_PLAYER);
	Coord3D last = *archer->getPosition();
	int lastMoveFrame = 0;
	int idleFrame = -1;
	for (int f = 1; f <= 300 && idleFrame < 0; ++f)
	{
		w.frames(1);
		const Coord3D &p = *archer->getPosition();
		if (p.x != last.x || p.y != last.y)
		{
			last = p;
			lastMoveFrame = f;
		}
		if (!attacking(ai))
		{
			idleFrame = f;
		}
	}
	// before the lane the archer stood in its attack for good. Its only move is the step to its cell's centre (frame 2), the wait is 10 *
	// LOGICFRAMES_PER_SECOND = 50 frames from the update that saw the move end, the failure comes on the update after it
	CHECK(idleFrame == 53);
	CHECK(lastMoveFrame == 2);
	CHECK(archer->getPosition()->x == 200.5f); // still on the island
	CHECK(archer->getPosition()->y == 500.5f);
}

TEST_CASE("approachgiveup1 (c): a StandGround unit on a raised layer with no quick path waits, then gives up; on the ground it walks")
{
	for (const bool raised : { true, false })
	{
		CAPTURE(raised);
		CombatWorld w(kExtra);
		w.combat().setAutoAcquireEnabled(false);
		cliffBand(w); // no quick path from the archer to the victim
		w.buildMap();
		Object *archer = w.unit("HoldArcher", 'A', 200, 500);
		Object *victim = w.unit("Runner", 'B', 600, 500);
		w.frames(2);
		AIUpdateInterface *ai = archer->getAIUpdateInterface();
		if (raised)
		{
			w.ai->adapterFor(*archer).setLayer(LAYER_WALL_FIRST);
			REQUIRE(AIWorld::objectLayer(*archer) == LAYER_WALL_FIRST);
		}
		const Coord3D start = *archer->getPosition();
		ai->aiAttackObject(victim, CMD_FROM_PLAYER);
		int idleFrame = -1;
		for (int f = 1; f <= 120 && idleFrame < 0; ++f)
		{
			w.frames(1);
			if (!attacking(ai))
			{
				idleFrame = f;
			}
		}
		if (raised)
		{
			// onEnter's two seconds (RW 0x74E6CE), the update's wait-over failure: no path asked, no step taken
			CHECK(idleFrame == 12);
			CHECK(archer->getPosition()->x == start.x);
			CHECK(archer->getPosition()->y == start.y);
		}
		else
		{
			CHECK(idleFrame == -1); // still attacking: it walked to the cliff's edge, from where its bow reaches the victim
			CHECK(archer->getPosition()->x > 380.0f); // tolerance: the cliff starts at x 400 (measured stop 389.07)
		}
	}
}
