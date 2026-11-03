// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// The AI's enter / exit states and ActionManager::canEnterObject. See GameLogic/AI/AIGarrisonStates.h for the target facts. Lane GARRISON-1.

#include "GameLogic/AI/AIGarrisonStates.h"
#include "GameLogic/Object/Contain/TransportContainRuntime.h"

#include "Common/Player.h"
#include "Common/StateHash.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/CollideModule.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/System/ShroudManager.h"
#include "GameLogic/AI/AIXfer.h"

namespace
{
const char *const kStop =
	"[S-1101] the AI's enter / exit (lane GARRISON-1): AIMoveToPositionAndEnterState 56 (RW 0x751A8A / 0x756452), AIEnterState 15 (RW 0x7515E1 / 0x756067 / "
	"0x752B0F), AIHordeEnterState 52 (RW 0x747C37 / 0x7433E3 / 0x747CD1), AIExitState 38 (RW 0x74352D / 0x74356A / 0x752BF4), AIHordeExitState 53 (RW 0x743437 / "
	"0x747D04 / 0x74347D) and ActionManager::canEnterObject (RW 0x82CBD5) are ported; lane BACKLOG-1 (BFME2 decomp): the DOZER + HARVESTER health test (RW "
	"0x82CC09), the collide-module branch (RW 0x82CD2E: every collide class a unit has answers no), AIEnterState's contained-container test (RW 0x756088) and "
	"its attack on an enemy container (RW 0x7560FB, getCanAttackObject RW 0x82CFD4: its object part as ObjectWeapons::canAttackObject, S-325), MSG_ENTER's "
	"weapon lock release (RW 0x76FB64), privateEnter's locomotor point and blocked fields (RW 0x5E39CE, AI + 0x16C / + 0x3B8); not ported: the SHIP branches "
	"(RW 0x82CED4, privateEnter's AI command 0x4F RW 0x66C6E3), behaviour slot 0x48 of the collide branch, getCanAttackObject's STRUCTURE and "
	"SPAWNS_ARE_THE_WEAPONS parts (reported), the container AI's slot 0x1A8 in the exit states (taken as not 2), the members' AI slot 0x1C4 of RW 0x875C93, "
	"the locomotor's allow-invalid-position flag, the group manager of MSG_ENTER (GameData + 0x11CA)";

int statusBit(const char *name)
{
	return CombatNames::status(name);
}

// RW 0x68DAE6: KindOf FS_POWER, FS_FACTORY, FS_BASE_DEFENSE, FS_TECHNOLOGY or FS_CASH_PRODUCER
bool isFactionStructure(const Object &obj)
{
	static const int kinds[5] = { CombatNames::kindOf("FS_POWER"), CombatNames::kindOf("FS_FACTORY"), CombatNames::kindOf("FS_BASE_DEFENSE"),
		CombatNames::kindOf("FS_TECHNOLOGY"), CombatNames::kindOf("FS_CASH_PRODUCER") };
	for (int k : kinds)
	{
		if (obj.isKindOf((unsigned)k))
		{
			return true;
		}
	}
	return false;
}

HordeContainInterface *hordeOf(const Object &obj)
{
	ContainModuleInterface *c = obj.getContain();
	return c ? c->getHordeContainInterface() : nullptr;
}

// RW 0x82C167: the container is SHROUDED (status >= 3, RW 0x68D8F7) for the object's player, unless the order came from a script, the container is one of the
// four reserved ids (100000000 - 4 .. - 1) or the player is an observer (+ 0x5C)
bool shroudedForPlayer(Object &obj, Object &container, CommandSourceType source)
{
	const ObjectID id = container.getID();
	if (id >= 99999996u && id <= 99999999u)
	{
		return false;
	}
	Player *p = obj.getControllingPlayer();
	if (!p || source == CMD_FROM_SCRIPT)
	{
		return false;
	}
	ShroudManager *sm = obj.logic().shroud();
	if (!sm)
	{
		return false; // no shroud in this game: everything is clear
	}
	return (int)sm->getObjectStatus(container, p->getPlayerIndex()) >= 3;
}
} // namespace

const char *GarrisonRules::stopLine()
{
	return kStop;
}

unsigned long long &GarrisonRules::enemyContainerAttacks()
{
	static unsigned long long n = 0;
	return n;
}

// RW 0x4B12E8 (lane BACKLOG-1): Thing::getHeightAboveTerrain (RW 0x70B8AE, BFME2 decomp Thing.cpp, tier A: the position's z less the terrain height, stored as a
// float) above 0.0 (x87 fcompi)
bool GarrisonRules::isAboveTerrain(const Object &obj)
{
	const Coord3D &p = *obj.getPosition();
	return SimMath::subf32(p.z, obj.logic().getGroundHeight(p.x, p.y)) > 0.0f;
}

// RW 0x82CFD4 ActionManager::getCanAttackObject(obj, victim, source, ATTACK_NEW_TARGET) (lane BACKLOG-1; BFME2 decomp ActionManager.cpp, tier B
// same-shape): no for a dead object or victim (+ 0x458 bit 0), the object itself, an object that is not able to attack (Object::isAbleToAttack RW 0x691269) or
// one with DISABLED bit 4 (+ 0x1C8 & 0x10: ATTACK_NEW_TARGET is refused). A STRUCTURE asks its riders first (RW 0x82C8A7) and a SPAWNS_ARE_THE_WEAPONS object
// its slave closest to the victim (RW 0x68C3A3 slot 8); neither enters a container (not mobile) and both are reported here. Then the object's own
// getAbleToAttackSpecificObject (RW 0x68D6A6): INFERENCE (S-325, as the guard and turret machines): ObjectWeapons::canAttackObject, not forced, stands in for it
bool GarrisonRules::canAttackAsNewTarget(Object &obj, Object &victim, CommandSourceType source)
{
	static const int structure = CombatNames::kindOf("STRUCTURE");
	static const int spawnsAreTheWeapons = CombatNames::kindOf("SPAWNS_ARE_THE_WEAPONS");
	if (obj.isEffectivelyDead() || victim.isEffectivelyDead() || &obj == &victim || !obj.isAbleToAttack())
	{
		return false;
	}
	if ((obj.getDisabledMask() & (1u << 4)) != 0)
	{
		return false; // RW 0x82D021 .. 0x82D034
	}
	if (obj.isKindOf((unsigned)structure) || obj.isKindOf((unsigned)spawnsAreTheWeapons))
	{
		obj.logic().reportError("getCanAttackObject (RW 0x82CFD4): the STRUCTURE / SPAWNS_ARE_THE_WEAPONS branches are not ported (S-1101), " + obj.getTemplate()->getName() +
			" asks for itself");
	}
	ObjectWeapons *w = obj.getWeapons();
	return w && w->canAttackObject(victim, source, false);
}

// RW 0x690E97
bool GarrisonRules::isMobile(const Object &obj)
{
	static const int immobile = CombatNames::kindOf("IMMOBILE");
	static const int stoned = CombatNames::modelCondition("STONED");
	static const int siegeTower = CombatNames::kindOf("SIEGE_TOWER");
	static const int deployed = statusBit("DEPLOYED");
	if (obj.isKindOf((unsigned)immobile))
	{
		return false;
	}
	const DisabledMaskType d = obj.getDisabledMask();
	if (d != 0 && d != (1u << 8))
	{
		return false; // RW 0x690EB4 .. 0x690ED0: only DISABLED type 8, alone, keeps it mobile
	}
	if (obj.testModelCondition(stoned))
	{
		return false;
	}
	return !(obj.isKindOf((unsigned)siegeTower) && obj.testStatus((unsigned)deployed));
}

// RW 0x82CBD5
bool GarrisonRules::canEnterObject(Object &obj, Object *container, CommandSourceType source, int mode, bool checkPath, bool *occupied)
{
	bool localOccupied = false;
	bool &out = occupied ? *occupied : localOccupied;
	out = false;
	static const int dozer = CombatNames::kindOf("DOZER");
	static const int harvester = CombatNames::kindOf("HARVESTER");
	static const int underConstruction = statusBit("UNDER_CONSTRUCTION");
	static const int deployed = statusBit("DEPLOYED");
	static const int sold = statusBit("SOLD");
	static const int mobNexus = CombatNames::kindOf("MOB_NEXUS");
	static const int ignoredInGui = CombatNames::kindOf("IGNORED_IN_GUI");
	static const int immobile = CombatNames::kindOf("IMMOBILE");
	static const int structure = CombatNames::kindOf("STRUCTURE");
	static const int noFreewillEnter = CombatNames::kindOf("NO_FREEWILL_ENTER");
	static const int infantry = CombatNames::kindOf("INFANTRY");
	if (obj.isKindOf((unsigned)dozer) && obj.isKindOf((unsigned)harvester) && container && container->getBodyModule())
	{
		// RW 0x82CC09 .. 0x82CC22 (lane BACKLOG-1; BFME2 decomp BFMEActionManager::canEnterObject, tier B same-shape for RW 0x82CBD5): a DOZER + HARVESTER object
		// is refused when the container body's vt[0x14] is below 0.99 (RW 0xBE53BC, x87 fcompi). vt[0x14] is the health fraction: ActiveBody RW 0x8C1D75
		// (max health > 0.0 (comiss): fld health, fdiv max health, x87 PC24; else 0.0), InactiveBody RW 0x70D41A (0.0)
		const BodyModuleInterface &body = *container->getBodyModule();
		const double fraction = body.getMaxHealth() > 0.0f ? SimMath::pc24DivW((double)body.getHealth(), (double)body.getMaxHealth()) : 0.0;
		if ((double)0.99f > fraction)
		{
			return false;
		}
	}
	if (!container || container == &obj || !container->getContain() || container->isEffectivelyDead())
	{
		return false;
	}
	ContainModuleInterface *contain = container->getContain();
	AIUpdateInterface *ai = obj.getAIUpdateInterface();
	const bool alreadyEntering = ai && ai->currentStateId() == (unsigned)AI_MOVE_TO_POSITION_AND_ENTER && ai->stateMachine().goalObject() == container;
	if (!alreadyEntering && shroudedForPlayer(obj, *container, source))
	{
		return false;
	}
	if (obj.testStatus((unsigned)underConstruction) || container->testStatus((unsigned)underConstruction) || container->testStatus((unsigned)deployed) ||
		container->testStatus((unsigned)sold))
	{
		return false;
	}
	if (obj.isKindOf((unsigned)mobNexus) || obj.isKindOf((unsigned)ignoredInGui) || obj.isKindOf((unsigned)immobile) || obj.isKindOf((unsigned)structure))
	{
		return false;
	}
	if (container->isKindOf((unsigned)ignoredInGui) || container->isKindOf((unsigned)noFreewillEnter))
	{
		return false;
	}
	if (obj.isKindOf((unsigned)infantry) && (container->getDisabledMask() & (1u << 5)) != 0)
	{
		return true; // RW 0x82CD1C .. 0x82CD28
	}
	// RW 0x82CD2E .. 0x82CD67 (lane BACKLOG-1): every collide module of the object (behaviour slot 1) whose behaviour slot 0x48 answers null and that would like to
	// collide with the container (collide slot 1) lets it in. Every collide class RotWK gives a unit answers no (CollideModuleInterface::wouldLikeToCollideWith);
	// behaviour slot 0x48 (RW 0x8A18E0 family: the module's + 0x20 sub-interface) is not identified (S-1101): a module that would like to collide is reported
	for (const std::unique_ptr<BehaviorModule> &m : obj.modules())
	{
		const CollideModuleInterface *collide = m->getCollide();
		if (collide && collide->wouldLikeToCollideWith(*container))
		{
			obj.logic().reportError("canEnterObject: a collide module of " + obj.getTemplate()->getName() + " would like to collide with " +
				container->getTemplate()->getName() + "; behaviour slot 0x48 of RW 0x82CD45 is not identified (S-1101): it enters");
			return true;
		}
	}
	// RW 0x82CD6D: the heal contain's full-health test (slot 0x18: false for the contains of this lane)
	if (mode == 2)
	{
		return !isFactionStructure(*container); // RW 0x82CDA8
	}
	bool checkCapacity = mode == 0;
	bool allowed = false; // contain slot 0x20 (RW 0x9188EB: false for every contain class here)
	if (obj.getControllingPlayer() != container->getControllingPlayer())
	{
		// RW 0x82CE3D .. 0x82CE8C: the count (slot 0x114) less the stealth units (slot 0x124); an occupied container, or an enemy faction structure, refuses unless
		// AllowAlliesInside (slot 0x38) or the contain's slot 0x20 (false here) allows it
		const int count = (int)contain->getContainCount();
		const int stealth = (int)contain->getStealthUnitsContained();
		if (!contain->allowAlliesInside() && (count - stealth > 0 || isFactionStructure(*container)))
		{
			if (!allowed)
			{
				return false;
			}
			out = true;
		}
		if (stealth > 0 && count == stealth)
		{
			checkCapacity = false;
		}
	}
	if (checkCapacity && TransportContain::transportSlotCount(obj) == 0)
	{
		return false; // RW 0x82CE99: RW 0x69029B, the object's TransportSlotCount (lane GARRISON-2: GARRISON-1 read it as the object's AI)
	}
	if (out)
	{
		checkCapacity = false;
	}
	return contain->isValidContainerFor(obj, checkCapacity, checkPath);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// 56 AIMoveToPositionAndEnterState
// ---------------------------------------------------------------------------------------------------------------------------------
namespace
{
class AIMoveToPositionAndEnterState : public AIState
{
public:
	explicit AIMoveToPositionAndEnterState(AIStateMachine &m) : AIState(m, "AIMoveToPositionAndEnterState") {}

	// RW 0x751A8A
	StateReturnType onEnter() override
	{
		Object *container = machine().goalObject();
		if (!container || !container->getContain())
		{
			return STATE_FAILURE;
		}
		container->getContain()->getEntryOffset(m_goal); // slot 0x15C
		static const int kShip = CombatNames::kindOf("SHIP");
		if (container->isKindOf((unsigned)kShip))
		{
			ai().world().noteStop(OpenContain::shipStop()); // lane SHIP-1: the boarding legs of RW 0x751A8A / 0x756452 are not ported (S-4160)
		}
		m_move = std::make_unique<AIMoveToState>(ai().mover(), m_goal, true);
		return m_move->onEnter();
	}

	// RW 0x756452
	StateReturnType update() override
	{
		Object &obj = owner();
		Object *container = machine().goalObject();
		if (!GarrisonRules::canEnterObject(obj, container, ai().lastCommandSource(), 0, true))
		{
			return STATE_FAILURE;
		}
		ContainModuleInterface *contain = container->getContain();
		const int count = (int)contain->getContainCount();
		const bool otherPlayer = obj.getControllingPlayer() != container->getControllingPlayer();
		static const int canEnterAnything = statusBit("CAN_ENTER_ANYTHING");
		if (otherPlayer && obj.testStatus((unsigned)canEnterAnything) && count > 0)
		{
			const Coord3D &a = *container->getPosition();
			const Coord3D &b = *obj.getPosition();
			const double d = SimMath::length3d(SimMath::subf32(a.x, b.x), SimMath::subf32(a.y, b.y), SimMath::subf32(a.z, b.z));
			if (d < 200.0) // RW 0x756518: [0xDA4B60] = 200.0f
			{
				contain->orderAllPassengersToExit((int)ai().lastCommandSource());
			}
		}
		if (!m_move)
		{
			return STATE_FAILURE;
		}
		const StateReturnType r = m_move->update();
		if (otherPlayer && count > 0)
		{
			return STATE_CONTINUE;
		}
		if (r == STATE_SUCCESS)
		{
			ai().aiEnterObject(container, ai().lastCommandSource()); // RW 0x66C5A4: AI command 0x17
			return STATE_SUCCESS;
		}
		return r;
	}

	void onExit(StateExitType) override
	{
		if (m_move)
		{
			m_move->onExit();
			m_move.reset();
		}
	}

	void crc(StateHasher &h) const override
	{
		h.addFloat(m_goal.x);
		h.addFloat(m_goal.y);
		h.addFloat(m_goal.z);
		h.addBool(m_move != nullptr);
		if (m_move)
		{
			m_move->crc(h);
		}
	}

	// lane SAVE-4c (obfme:AIWorld, OpenBFME layout: stop S-4220): the goal and the move
	void xfer(Xfer &x) override
	{
		XferVersion v{ 1, 1 };
		x.xferVersion(v);
		AIXfer::coord(x, m_goal);
		xferMove(x, ai().mover(), m_move);
	}

private:
	Coord3D m_goal{ 0.0f, 0.0f, 0.0f };
	std::unique_ptr<AIMoveToState> m_move;
};

// ---------------------------------------------------------------------------------------------------------------------------------
// 15 AIEnterState
// ---------------------------------------------------------------------------------------------------------------------------------
class AIEnterState : public AIState
{
public:
	explicit AIEnterState(AIStateMachine &m) : AIState(m, "AIEnterState") {}

	// RW 0x7515E1
	StateReturnType onEnter() override
	{
		Object &obj = owner();
		m_container = INVALID_ID;
		m_timeout = obj.logic().getFrame() + obj.logic().settings().waitToForceMemberToEnterDelay; // RW 0x7515EE .. 0x751605: GameData + 0x1234
		Object *container = machine().goalObject();
		if (!container || !GarrisonRules::canEnterObject(obj, container, ai().lastCommandSource(), 0, false))
		{
			return STATE_FAILURE;
		}
		if (ContainModuleInterface *contain = container->getContain())
		{
			contain->getEntryPosition(m_goal);                 // slot 0x158
			contain->onObjectWantsToEnterOrExit(&obj, 0);     // slot 0x44(obj, WANTS_TO_ENTER)
			m_container = container->getID();
		}
		else
		{
			m_goal = *container->getPosition();
		}
		// RW 0x7516CD .. 0x7516D7: the locomotor flag + 0x44 bit 1 (ZH setAllowInvalidPosition); the port's locomotor has no reader of it (S-1101)
		m_move = std::make_unique<AIMoveToState>(ai().mover(), m_goal, true);
		return m_move->onEnter();
	}

	// RW 0x756067
	StateReturnType update() override
	{
		Object &obj = owner();
		Object *container = machine().goalObject();
		if (!container)
		{
			return STATE_FAILURE;
		}
		// RW 0x756088 .. 0x7560A5 (lane BACKLOG-1; BFME2 decomp AIEnterState::update, tier B same-shape): a container that is itself contained (+ 0x27C) and above
		// the terrain (RW 0x4B12E8) cannot be entered from the ground
		if (container->getContainedBy() && GarrisonRules::isAboveTerrain(*container) && !GarrisonRules::isAboveTerrain(obj))
		{
			return STATE_FAILURE;
		}
		ContainModuleInterface *contain = container->getContain();
		if (contain)
		{
			contain->getEntryPosition(m_goal);
		}
		else
		{
			m_goal = *container->getPosition();
		}
		machine().setGoalObject(container->getID()); // RW 0x6627F0
		if (!GarrisonRules::canEnterObject(obj, container, ai().lastCommandSource(), 0, false))
		{
			// RW 0x7560FB .. 0x75615F (lane BACKLOG-1): an enemy container (RW 0x68D7AB == ENEMIES) the object can attack as a new target (RW 0x82CFD4: POSSIBLE or
			// POSSIBLE_AFTER_MOVING) is attacked instead: AI command 0xB (RW 0x66C536: the container, 0x7FFFFFFF shots, the last command source), CONTINUE
			if (obj.getRelationship(*container) == ENEMIES && GarrisonRules::canAttackAsNewTarget(obj, *container, ai().lastCommandSource()))
			{
				ai().aiAttackObject(container, ai().lastCommandSource(), 0x7FFFFFFF);
				++GarrisonRules::enemyContainerAttacks();
				return STATE_CONTINUE;
			}
			return STATE_FAILURE;
		}
		if ((obj.getDisabledMask() & (1u << 3)) != 0)
		{
			return STATE_SUCCESS; // RW 0x75616E: + 0x1C8 bit 3 (DISABLED_HELD)
		}
		if (!m_move)
		{
			return STATE_FAILURE;
		}
		m_move->setGoal(m_goal);
		const StateReturnType r = m_move->update();
		if (r == STATE_CONTINUE)
		{
			return STATE_CONTINUE;
		}
		if (!contain)
		{
			return r;
		}
		// RW 0x7561A0 .. 0x7561E8: SSE, (dy * dy) + (dx * dx) against the container's bounding radius squared, or the timeout
		Coord3D entry;
		contain->getEntryPosition(entry);
		const float dy = SimMath::subf32(obj.getPosition()->y, entry.y);
		const float dx = SimMath::subf32(obj.getPosition()->x, entry.x);
		const float dist = SimMath::addf32(SimMath::mulf32(dy, dy), SimMath::mulf32(dx, dx));
		const float r0 = CombatQueries::boundingCircleRadius(*container);
		const float radiusSq = SimMath::mulf32(r0, r0);
		if (!(radiusSq > dist) && obj.logic().getFrame() <= m_timeout)
		{
			return r;
		}
		contain->addToContain(&obj); // slot 0x9C
		return STATE_SUCCESS;
	}

	// RW 0x752B0F
	void onExit(StateExitType) override
	{
		if (m_move)
		{
			m_move->onExit();
			m_move.reset();
		}
		if (m_container != INVALID_ID)
		{
			if (Object *c = owner().logic().findObjectByID(m_container))
			{
				if (ContainModuleInterface *contain = c->getContain())
				{
					contain->onObjectWantsToEnterOrExit(&owner(), 2);
				}
			}
		}
	}

	void crc(StateHasher &h) const override
	{
		h.addU32(m_container);
		h.addU32(m_timeout);
		h.addFloat(m_goal.x);
		h.addFloat(m_goal.y);
		h.addFloat(m_goal.z);
		h.addBool(m_move != nullptr);
		if (m_move)
		{
			m_move->crc(h);
		}
	}

	// lane SAVE-4c (obfme:AIWorld, OpenBFME layout: stop S-4220; retail's AIEnterState has no xfer of its own in docs/SAVES-RETAIL.md's table)
	void xfer(Xfer &x) override
	{
		XferVersion v{ 1, 1 };
		x.xferVersion(v);
		AIXfer::objectId(x, m_container, "ai garrison: the object id container");
		x.xferUnsignedInt(&m_timeout);
		AIXfer::coord(x, m_goal);
		xferMove(x, ai().mover(), m_move);
	}

private:
	ObjectID m_container = INVALID_ID; // state + 0x4C
	unsigned m_timeout = 0;            // state + 0x50
	Coord3D m_goal{ 0.0f, 0.0f, 0.0f };
	std::unique_ptr<AIMoveToState> m_move;
};

// ---------------------------------------------------------------------------------------------------------------------------------
// 52 AIHordeEnterState
// ---------------------------------------------------------------------------------------------------------------------------------
class AIHordeEnterState : public AIState
{
public:
	explicit AIHordeEnterState(AIStateMachine &m) : AIState(m, "AIHordeEnterState") {}

	// RW 0x747C37
	StateReturnType onEnter() override
	{
		Object &horde = owner();
		Object *container = machine().goalObject();
		if (!container || container->isEffectivelyDead())
		{
			return STATE_FAILURE;
		}
		HordeContainInterface *h = hordeOf(horde);
		ContainModuleInterface *contain = container->getContain();
		if (!h || !contain || contain->getHordeContainInterface() || !contain->isValidContainerFor(horde, true, false))
		{
			return STATE_FAILURE;
		}
		h->releaseMembersToward(container, (int)ai().lastCommandSource()); // horde slot 0x7C (RW 0x875C93)
		static const int immobile = CombatNames::kindOf("IMMOBILE");
		m_heldHorde = !container->isKindOf((unsigned)immobile);
		if (m_heldHorde)
		{
			horde.setDisabled(8, (UnsignedInt)UPDATE_SLEEP_FOREVER); // RW 0x747CB5: RW 0x6936C7(8), the counted DISABLED type 8
		}
		contain->addToContain(&horde); // slot 0x9C
		return STATE_CONTINUE;
	}

	// RW 0x7433E3
	StateReturnType update() override
	{
		Object *container = machine().goalObject();
		HordeContainInterface *h = hordeOf(owner());
		if (!container || !h)
		{
			return STATE_FAILURE;
		}
		if (h->allMembersEntered())
		{
			return STATE_SUCCESS;
		}
		h->pushMembersIntoContainer(container); // slot 0xFC
		return STATE_CONTINUE;
	}

	// RW 0x747CD1
	void onExit(StateExitType) override
	{
		if (m_heldHorde)
		{
			owner().clearDisabled(8); // RW 0x6936E4(8)
			m_heldHorde = false;
		}
	}

	void crc(StateHasher &h) const override { h.addBool(m_heldHorde); }
	void xfer(Xfer &x) override // lane SAVE-4c (obfme:AIWorld, OpenBFME layout: stop S-4220)
	{
		XferVersion v{ 1, 1 };
		x.xferVersion(v);
		x.xferBool(&m_heldHorde);
	}

private:
	bool m_heldHorde = false;
};

// ---------------------------------------------------------------------------------------------------------------------------------
// 38 AIExitState / 53 AIHordeExitState
// ---------------------------------------------------------------------------------------------------------------------------------
class AIExitState : public AIState
{
public:
	AIExitState(AIStateMachine &m, bool horde) : AIState(m, horde ? "AIHordeExitState" : "AIExitState"), m_horde(horde) {}

	// RW 0x74352D / 0x743437
	StateReturnType onEnter() override
	{
		m_container = INVALID_ID;
		Object *container = machine().goalObject();
		if (!container)
		{
			return STATE_FAILURE;
		}
		if (m_horde && !hordeOf(owner()))
		{
			return STATE_FAILURE;
		}
		if (ContainModuleInterface *contain = container->getContain())
		{
			contain->onObjectWantsToEnterOrExit(&owner(), 1);
			m_container = container->getID();
		}
		return STATE_CONTINUE;
	}

	// RW 0x74356A / 0x747D04
	StateReturnType update() override
	{
		Object &obj = owner();
		Object *container = machine().goalObject();
		if (!container || !container->getContain())
		{
			return STATE_FAILURE;
		}
		ContainModuleInterface *exit = container->getContain();
		if (m_horde && !hordeOf(obj))
		{
			return STATE_FAILURE;
		}
		if (exit->isExitBusy())
		{
			return STATE_CONTINUE;
		}
		const int door = exit->reserveDoorForExit(obj);
		if (door == -1)
		{
			return STATE_FAILURE;
		}
		const unsigned myId = id();
		exit->exitObjectViaDoor(&obj, door);
		if (!m_horde)
		{
			return machine().currentStateId() != myId ? STATE_CONTINUE : STATE_SUCCESS; // RW 0x7435C2
		}
		HordeContainInterface *h = hordeOf(obj);
		const ContainModuleInterface *own = obj.getContain();
		const bool empty = !own || own->getContainCount() == 0; // horde slot 0xF8 (RW 0x86DC6B)
		if (h && empty)
		{
			if (obj.getContainedBy())
			{
				exit->exitObjectViaDoor(&obj, door);
			}
			return STATE_SUCCESS;
		}
		return obj.getContainedBy() ? STATE_CONTINUE : STATE_SUCCESS;
	}

	// RW 0x752BF4 / 0x74347D
	void onExit(StateExitType) override
	{
		if (m_container != INVALID_ID)
		{
			if (Object *c = owner().logic().findObjectByID(m_container))
			{
				if (ContainModuleInterface *contain = c->getContain())
				{
					contain->onObjectWantsToEnterOrExit(&owner(), 2);
				}
			}
		}
		machine().setGoalObject(INVALID_ID);
	}

	void crc(StateHasher &h) const override { h.addU32(m_container); }
	// lane SAVE-4c (obfme:AIWorld): retail's AIExitState::xfer RW 0x7434E9 / AIHordeExitState::xfer RW 0x7434B1 are (1, 2); OpenBFME layout (S-4220)
	void xfer(Xfer &x) override
	{
		XferVersion v{ 1, 1 };
		x.xferVersion(v);
		AIXfer::objectId(x, m_container, "ai garrison: the object id container");
	}

private:
	bool m_horde;
	ObjectID m_container = INVALID_ID;
};
} // namespace

std::unique_ptr<AIState> makeMoveToPositionAndEnterState(AIStateMachine &machine)
{
	return std::make_unique<AIMoveToPositionAndEnterState>(machine);
}
std::unique_ptr<AIState> makeEnterState(AIStateMachine &machine)
{
	return std::make_unique<AIEnterState>(machine);
}
std::unique_ptr<AIState> makeHordeEnterState(AIStateMachine &machine)
{
	return std::make_unique<AIHordeEnterState>(machine);
}
std::unique_ptr<AIState> makeExitState(AIStateMachine &machine)
{
	return std::make_unique<AIExitState>(machine, false);
}
std::unique_ptr<AIState> makeHordeExitState(AIStateMachine &machine)
{
	return std::make_unique<AIExitState>(machine, true);
}
