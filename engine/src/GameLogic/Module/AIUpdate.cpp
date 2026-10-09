// OpenBFME. GPL-3.0.
// See GameLogic/Module/AIUpdate.h for the sources and what is inference.

#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/PhysicsBehavior.h"
#include "GameLogic/AI/AIGarrisonStates.h"
#include "GameLogic/AI/TurretAI.h"
#include "Common/Thing/RawModuleData.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/ObjectWeapons.h"

#include "Common/GameCommon.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/AI/AIEmotionStates.h"
#include "GameLogic/AI/AIHunt.h"
#include "GameLogic/AI/AIWaypointPath.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/HordeAIUpdate.h"
#include "GameLogic/Module/UnportedModule.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "Common/NumericState.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/UnitSpecificSound.h"
#include "Common/Audio/AudioRequests.h"

#include <cmath>
#include <cstring>
#include <stdexcept>

namespace
{
const char *const kStopCommands =
	"S-222 AIUpdateInterface executes the move / idle / follow-path / busy commands; every other AICommandType (attack, guard, hunt, enter, exit, dock, repair, wander, "
	"face, waypoint paths, ...) is counted by AIUpdateInterface::unportedCommands() and does nothing (the combat and construction milestones port them)";
const char *const kStopUpdate =
	"S-222 AIUpdateInterface::update skips the turrets, the mood / auto-acquire scan, the dead-state machine and the surrender / demoralize timers (RW 0x6695EF blocks "
	"that need weapons, bodies and emotions); the movement-complete refresh of the pathfinder goal is the B1 body (AIUpdate.cpp:2574-2594), RW forces m_doFinalPosition off";
const char *const kStopLocomotor =
	"S-222 chooseGoodLocomotorFromCurrentSet picks the first locomotor of the set whose surfaces fit the cell under the unit and otherwise the previous one or the "
	"ground one (B1 AIUpdate.cpp:2385); the retail Pathfinder::chooseBestLocomotorForPosition was not read. The locomotor 'crusher' flag of the path searches (RW loco+0x15) is "
	"ScalesWalls (inference)";
const char *const kStopStates =
	"S-221 the AI state ids are BFME1's (Idle 0, MoveTo 1, FollowPath 6, FollowExitProductionPath 7, Wait 8, Busy 42; RotWK's own numbering is not read: RW tests 0x2A = Busy and "
	"0x47 = a state RotWK added); AIIdleState::onEnter draws its random sleep offset as ZH does (GetGameLogicRandomValue(0, 10, \"AIStates.cpp\", 0x520)): the RW draw was not read, "
	"so a retail replay's RNG sequence can differ at the first idle of every unit";
const char *const kStopHost =
	"S-222 the movement host reports no river (RiverModifier), no crew power, no speed attribute modifier, no charge order, no physics suppression and no turn limiting "
	"(the modules behind RW 0x68BF11, 0x68C82D, 0x5E3A1B and the terrain river query are other lanes'); the pending position of the movers lives in the AI";

// the registry bits, resolved once by name (a mod's own order cannot break them)
int modelConditionIndex(const char *name)
{
	for (int i = 0; TheModelConditionNames[i]; ++i)
	{
		if (std::strcmp(TheModelConditionNames[i], name) == 0)
		{
			return i;
		}
	}
	throw std::logic_error(std::string("model condition registry has no ") + name);
}

int objectStatusBit(const char *name)
{
	const int bit = ObjectTemplateInfoBuilder::objectStatusIndex(name);
	if (bit < 0)
	{
		throw std::logic_error(std::string("object status registry has no ") + name);
	}
	return bit;
}

struct StatusBits
{
	int hordeMember = objectStatusBit("HORDE_MEMBER");
	int immobile = objectStatusBit("IMMOBILE");
};
const StatusBits &statusBits()
{
	static const StatusBits s;
	return s;
}

// the KindOf bits this file tests, resolved once by name against the binary's table
struct KindBits
{
	int immobile = ObjectTemplateInfoBuilder::kindOfIndex("IMMOBILE");
	int dozer = ObjectTemplateInfoBuilder::kindOfIndex("DOZER");
	int cavalry = ObjectTemplateInfoBuilder::kindOfIndex("CAVALRY");
	int horde = ObjectTemplateInfoBuilder::kindOfIndex("HORDE");
};
const KindBits &kindBits()
{
	static const KindBits k;
	return k;
}
bool hasKind(const Object &o, int bit)
{
	return bit >= 0 && o.isKindOf((unsigned)bit);
}

// RW 0x644FD0 (two SSE loops, the same as the movers use)
float normalizeAngle(float a)
{
	const float pi = 3.14159274f, twoPi = 6.28318548f;
	if (a > pi)
	{
		do
		{
			a = SimMath::subf32(a, twoPi);
		} while (a > pi);
	}
	if (-pi < a)
	{
		return a;
	}
	do
	{
		a = SimMath::addf32(a, twoPi);
	} while (-pi >= a);
	return a;
}
} // namespace

// ---------------------------------------------------------------------------------------------------------------------------------
// ObjectLocomotorHost: every read / write of the object the locomotor movers make (GameLogic/Locomotor.h LocomotorHost)
// ---------------------------------------------------------------------------------------------------------------------------------
class AIUpdateInterface::ObjectLocomotorHost : public LocomotorHost
{
public:
	explicit ObjectLocomotorHost(AIUpdateInterface &ai)
		: m_ai(ai)
	{
	}

	Object &obj() const { return *m_ai.getObject(); }

	Coord3D getPosition() const override { return *obj().getPosition(); }
	float getAngle() const override { return obj().getOrientation(); }
	Coord2D getUnitDirectionVector2D() const override
	{
		const float *b = obj().getBasis(); // the X axis of the transform: (cos, sin) of the angle
		Coord2D d;
		d.x = b[0];
		d.y = b[3];
		return d;
	}
	float getBoundingRadius() const override { return m_ai.adapter().getGeometry().boundingCircleRadius(); }
	LocomotorMatrix getTransform() const override
	{
		const float *b = obj().getBasis();
		const Coord3D &p = *obj().getPosition();
		LocomotorMatrix m;
		for (int r = 0; r < 3; ++r)
		{
			for (int c = 0; c < 3; ++c)
			{
				m.m[r][c] = b[r * 3 + c];
			}
		}
		m.m[0][3] = p.x;
		m.m[1][3] = p.y;
		m.m[2][3] = p.z;
		return m;
	}
	void setTransform(const LocomotorMatrix &m) override
	{
		Coord3D p;
		p.x = m.m[0][3];
		p.y = m.m[1][3];
		p.z = m.m[2][3];
		float basis[9];
		for (int r = 0; r < 3; ++r)
		{
			for (int c = 0; c < 3; ++c)
			{
				basis[r * 3 + c] = m.m[r][c];
			}
		}
		// the horde's own movement does not carry its members with it: they walk to their slots (the member pass), they are not teleported
		HordeContainInterface *hc = hordeInterface();
		if (hc)
		{
			hc->setLocomoting(true);
		}
		obj().setTransform(&p, basis);
		if (hc)
		{
			hc->setLocomoting(false);
		}
	}
	bool hasPendingPosition() const override { return m_ai.m_pendingValid; }
	Coord3D getPendingPosition() const override { return m_ai.m_pendingPosition; }
	void setPendingPosition(const Coord3D &p) override
	{
		m_ai.m_pendingPosition = p;
		m_ai.m_pendingValid = true;
		m_ai.m_pendingFrame = obj().logic().getFrame(); // SMOOTH-1: for the client snapshot only (not hashed, not read by the simulation)
	}

	float locomotorSetSpeed() const override { return m_ai.m_locomotorSetSpeed; }
	int damageState() const override
	{
		const BodyModuleInterface *body = obj().getBodyModule();
		return body ? (int)body->getDamageState() : (int)BODY_PRISTINE;
	}
	int movementPenaltyDamageState() const override { return m_ai.world().config().movementPenaltyDamageState; }
	float crewPowerMultiplier() const override
	{
		// RW 0x68BF11: the object's contain's slot 0xD4 (lane GARRISON-2: SiegeEngineContain's crew), 1.0 without a contain
		const ContainModuleInterface *c = m_ai.getObject()->getContain();
		return c ? c->getCrewPowerMultiplier() : 1.0f;
	}
	bool speedAttributeModifier(float &) const override { return false; }
	bool isInRiver(float, float) const override { return false; }
	bool isTurnLimited() const override { return !stationary(); }
	bool isChargeOrdered() const override { return false; }
	// RW 0x5E3A1B: Object + 0x264 (the PhysicsBehavior) byte + 0x5C, the shock stun (lane COMBAT-3)
	bool physicsMotionDisabled() const override
	{
		if (!m_physicsLooked)
		{
			m_physics = PhysicsBehavior::find(obj()); // the modules never change after the object is made
			m_physicsLooked = true;
		}
		return m_physics != nullptr && m_physics->isStunned();
	}
	bool zMotionSuppressed() const override { return false; }
	unsigned logicFrame() const override { return obj().logic().getFrame(); }
	bool containerAllowsBackingUp() const override { return false; }
	float groundHeightAt(float x, float y) const override { return obj().logic().getGroundHeight(x, y); }
	LocomotorPath *getPath() override { return m_ai.m_mover->path(); }
	bool testObjectStatus(int bit) const override { return bit >= 0 && obj().testStatus((unsigned)bit); }
	bool testModelCondition(int bit) const override { return obj().testModelCondition(bit); }
	void setModelCondition(int bit, bool value) override { obj().setModelConditionState(bit, value); }
	void clearAndSetModelConditions(const std::vector<int> &clear, const std::vector<int> &set) override
	{
		Object::ModelConditionBits c{}, s{};
		for (int b : clear)
		{
			if (b >= 0 && b < 19 * 32)
			{
				c[(size_t)b >> 5] |= 1u << (b & 31);
			}
		}
		for (int b : set)
		{
			if (b >= 0 && b < 19 * 32)
			{
				s[(size_t)b >> 5] |= 1u << (b & 31);
			}
		}
		obj().clearAndSetModelConditionFlags(c, s);
	}
	void notifyWheelsStopped() override { m_ai.setLocomotorGoalNone(); }

	bool hasHordeContain() const override { return hordeInterface() != nullptr; }
	void hordeBeginReform() override
	{
		if (HordeContainInterface *hc = hordeInterface())
		{
			hc->beginReform();
		}
	}
	void hordeEndReform() override
	{
		if (HordeContainInterface *hc = hordeInterface())
		{
			hc->endReform();
		}
	}
	bool hordeFormationReady(float angle) const override
	{
		HordeContainInterface *hc = hordeInterface();
		return hc ? hc->isFormationReady(angle) : true;
	}
	bool thingWaitsForFormation() const override
	{
		// ThingTemplate+0x109 bit 2 (RW 0x5E73AA) is the locomotor's WaitForFormation flag of the template in use
		return m_ai.m_curLocomotor ? m_ai.m_curLocomotor->getTemplate().m_waitForFormation : false;
	}

private:
	HordeContainInterface *hordeInterface() const
	{
		ContainModuleInterface *c = obj().getContain();
		return c ? c->getHordeContainInterface() : nullptr;
	}
	// RW 0x66288A (AI vtable +0x228): true when the unit follows a path with a special node; PATH-1 names the same predicate isStationary
	bool stationary() const { return m_ai.adapter().isStationary(); }

	AIUpdateInterface &m_ai;
	mutable PhysicsBehavior *m_physics = nullptr;
	mutable bool m_physicsLooked = false;
};

// ---------------------------------------------------------------------------------------------------------------------------------
// construction
// ---------------------------------------------------------------------------------------------------------------------------------
AIUpdateInterface::AIUpdateInterface(Thing *thing, const ModuleData *data)
	: UpdateModule(thing, data)
{
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	m_world = logic.aiWorld();
	if (!m_world)
	{
		throw std::logic_error("AIUpdateInterface of " + obj->getTemplate()->getName() + ": the game has no AIWorld (GameLogic::setAIWorld)");
	}
	m_id = (PathfindObjectID)obj->getID();
	m_info = &m_world->movementInfo(*obj->getTemplate());
	m_adapter = &m_world->adapterFor(*obj);
	m_mover = std::make_unique<AIMover>(*this, m_world->pathfinder());
	m_locoHost = std::make_unique<ObjectLocomotorHost>(*this);
	m_world->moveWorld().add(*m_mover);
	m_world->registerAI(*this);
	chooseLocomotorSet(LOCOMOTORSET_NORMAL); // B1 ctor
	readCombatSettings(data);                 // COMBAT-1: the mood fields of the raw module data
	setWakeFrame(obj, UPDATE_SLEEP_NONE);     // B1 ctor (SLEEPY_AI)
	// lane GARRISON-2: RW 0x66ECFE makes the turret from the AI data's turret (+ 0x14); its machine enters IDLE at once (the idle scan's draw, TurretAI.cpp line 0x524)
	if (const RawModuleData *raw = dynamic_cast<const RawModuleData *>(data))
	{
		std::vector<std::string> lines;
		for (const RawModuleData::Line &l : raw->lines())
		{
			lines.push_back(l.text);
		}
		auto td = std::make_unique<TurretAIData>();
		std::string error;
		const bool hasTurret = TurretAIData::parseFromModuleLines(lines, *td, error);
		if (!error.empty())
		{
			throw std::runtime_error("AIUpdateInterface of " + obj->getTemplate()->getName() + ": " + error);
		}
		if (hasTurret)
		{
			m_turretData = std::move(td);
			m_turret = std::make_unique<TurretAI>(*this, *m_turretData, 0);
			logic.noteStop(TurretAI::stopLine()); // the turret's stop reaches GameLogic::report().stops in every game that has one
		}
	}
}

AIUpdateInterface::~AIUpdateInterface()
{
	m_destroying = true; // the object left the world already (its drawable is gone): the states' exits must not touch its model condition
	if (m_machine)
	{
		m_machine->halt(); // B1 ~AIUpdateInterface: the machine halts before the path goes
		m_machine.reset();
	}
	m_turret.reset(); // after the machine: the attack state's exit clears the turret's target
	if (m_world)
	{
		m_world->moveWorld().remove(m_id);
		m_world->unregisterAI(*this);
	}
}

std::unique_ptr<AIStateMachine> AIUpdateInterface::makeStateMachine()
{
	std::unique_ptr<AIStateMachine> m = std::make_unique<AIStateMachine>(*this, machineName());
	// B1 AIStateMachineConstructor.cpp (ids), ZH AIStates.cpp (states and their success / failure transitions: the move states fall back to idle)
	m->defineState(AI_IDLE, std::make_unique<AIIdleState>(*m), AI_IDLE, AI_IDLE);
	m->defineState(AI_MOVE_TO, std::make_unique<AIMoveToMachineState>(*m), AI_IDLE, AI_IDLE);
	m->defineState(AI_FOLLOW_PATH, std::make_unique<AIFollowPathState>(*m, false), AI_IDLE, AI_IDLE);
	m->defineState(AI_FOLLOW_EXITPRODUCTION_PATH, std::make_unique<AIFollowPathState>(*m, true), AI_IDLE, AI_IDLE);
	m->defineState(AI_WAIT, std::make_unique<AIWaitState>(*m), AI_IDLE, AI_IDLE);
	m->defineState(AI_BUSY, std::make_unique<AIBusyState>(*m), AI_IDLE, AI_IDLE);
	m->defineState(AI_MOVE_OUT_OF_THE_WAY, makeMoveOutOfTheWayState(*m), AI_IDLE, AI_IDLE); // lane PHYS-1: state 26 (AIUpdateAllies.cpp)
	// COMBAT-1 (B1 AIStateMachineConstructor.cpp:563-567, 579): attack object 50, force attack object 51, dead 13; an attack ends in idle, which looks for the next target
	m->defineState(AI_ATTACK_OBJECT, std::make_unique<AIAttackState>(*m, false, true, false), AI_IDLE, AI_IDLE);
	m->defineState(AI_FORCE_ATTACK_OBJECT, std::make_unique<AIAttackState>(*m, false, true, true), AI_IDLE, AI_IDLE);
	m->defineState(AI_DEAD, std::make_unique<AIDeadState>(*m), AI_DEAD, AI_DEAD);
	// lane GARRISON-1: RotWK's ids (RW 0x753755): enter 15, exit 38, horde enter 52, horde exit 53, move to position and enter 56; each ends in idle
	m->defineState(AI_ENTER, makeEnterState(*m), AI_IDLE, AI_IDLE);
	m->defineState(AI_EXIT, makeExitState(*m), AI_IDLE, AI_IDLE);
	m->defineState(AI_HORDE_ENTER, makeHordeEnterState(*m), AI_IDLE, AI_IDLE);
	m->defineState(AI_HORDE_EXIT, makeHordeExitState(*m), AI_IDLE, AI_IDLE);
	m->defineState(AI_MOVE_TO_POSITION_AND_ENTER, makeMoveToPositionAndEnterState(*m), AI_IDLE, AI_IDLE);
	// lane MODULES-3 (RW 0x753755): the emotion states (AIEmotionStates.cpp); the panic state's success goes on to the cower state (as a temporary state it just ends)
	m->defineState(AI_PANIC, std::make_unique<AIPanicState>(*m, false), AI_PANIC_COWER, AI_IDLE);
	m->defineState(AI_PANIC_COWER, std::make_unique<AIPanicState>(*m, true), AI_IDLE, AI_IDLE);
	m->defineState(AI_BACK_AWAY, std::make_unique<AIBackAwayState>(*m), AI_IDLE, AI_IDLE);
	m->defineState(AI_QUARREL, std::make_unique<AIQuarrelState>(*m), AI_IDLE, AI_IDLE);
	m->defineState(AI_FACE_OBJECT_IDLE, std::make_unique<AIFaceObjectIdleState>(*m), AI_IDLE, AI_IDLE);
	m->defineState(AI_HUNT, std::make_unique<AIHuntState>(*m), AI_IDLE, AI_IDLE); // lane SCRIPT-3: RW 0x741133 (AIHunt.cpp)
	// lane SCRIPT-3: the waypoint path states (RW 0x753755: 2 as a team, 3 as individuals, 4 / 5 the exact forms; AIWaypointPath.cpp)
	m->defineState(AI_FOLLOW_WAYPOINT_PATH_AS_TEAM, std::make_unique<AIFollowWaypointPathState>(*m, true, false), AI_IDLE, AI_IDLE);
	m->defineState(AI_FOLLOW_WAYPOINT_PATH_AS_INDIVIDUALS, std::make_unique<AIFollowWaypointPathState>(*m, false, false), AI_IDLE, AI_IDLE);
	m->defineState(AI_FOLLOW_WAYPOINT_PATH_EXACT_AS_TEAM, std::make_unique<AIFollowWaypointPathState>(*m, true, true), AI_IDLE, AI_IDLE);
	m->defineState(AI_FOLLOW_WAYPOINT_PATH_EXACT_AS_INDIVIDUALS, std::make_unique<AIFollowWaypointPathState>(*m, false, true), AI_IDLE, AI_IDLE);
	return m;
}

// B1 AIUpdate.cpp:2006
void AIUpdateInterface::onObjectCreated()
{
	if (!m_machine)
	{
		m_machine = makeStateMachine();
		m_machine->initDefaultState();
		m_stateMachineMade = true;
	}
}

void AIUpdateInterface::onDelete()
{
	// the object leaves the pathfinder before its modules are deleted (AIWorld::objectLeftWorld also runs from ~Object)
	if (m_machine)
	{
		m_machine->halt();
	}
	m_mover->destroyPath();
}

int AIUpdateInterface::modelConditionBit(const char *name)
{
	return modelConditionIndex(name);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// update (RW 0x6695EF, B1 AIUpdate.cpp:2547)
// ---------------------------------------------------------------------------------------------------------------------------------
UpdateSleepTime AIUpdateInterface::update()
{
	m_isInUpdate = true;
	getObject()->setStatus((unsigned)CombatNames::statuses().updatingAI, true); // RW: damage dealt to this object during its own update waits a frame
	getObject()->logic().scriptModelConditionEvents(*getObject(), m_luaConditionSnapshot); // lane HERO-2: RW 0x66964D -> 0x663E32
	if (ObjectWeapons *w = getObject()->getWeapons())
	{
		// the weapon model conditions (RW 0x68E197) are the WeaponStatusHelper's (lane PROJ-2: a PHASE_FINAL module, after this update; RW 0x66DA50 is
		// a call inside RW 0x66D8F5, not one at the start of every AI update)
		w->updateFiringTracker(); // RW 0x8E30DD
	}
	updateBeforeBase();
	if (m_attackMoveActive && !m_mover->isAiDead() && frame() >= m_nextMoodCheck)
	{
		// ZH AIAttackMoveToState::update: look around while marching; a target interrupts the march (the idle state resumes it)
		m_nextMoodCheck = frame() + 3u;
		const unsigned id = currentStateId();
		if (id == (unsigned)AI_MOVE_TO || id == (unsigned)AI_FOLLOW_PATH)
		{
			if (Object *t = attackMoveTarget())
			{
				aiAttackObject(t, CMD_FROM_AI);
			}
		}
	}
	unsigned sleep = (unsigned)UPDATE_SLEEP_FOREVER;
	const StateReturnType st = m_machine->updateStateMachine();
	if (IS_STATE_SLEEP(st))
	{
		const unsigned frames = GET_STATE_SLEEP_FRAMES(st);
		if (frames < sleep)
		{
			sleep = frames;
		}
	}
	else
	{
		sleep = (unsigned)UPDATE_SLEEP_NONE; // CONTINUE / SUCCESS / FAILURE: the next frame
	}
	if (m_mover->movementComplete())
	{
		doMovementCompleteBookkeeping();
	}
	// lane GARRISON-2: RW 0x6658D3 after the machine (and RW's path timer, which the port runs inside AIMover::update below): the turret runs unless the object is
	// dead or disabled by type 2 or 4 (mask 0x14); its sleep joins the AI's. The AI slot 0x1C4 test before it is taken as false (S-1106)
	if (m_turret && !getObject()->isEffectivelyDead() && (getObject()->getDisabledMask() & 0x14u) == 0)
	{
		const unsigned ts = m_turret->updateTurretAI();
		if (ts < sleep)
		{
			sleep = ts;
		}
	}
	// the movement-complete block, the due path-request timer and doLocomotor (AIMover::update)
	unsigned moverSleep = 0;
	try
	{
		moverSleep = m_mover->update(sleep);
	}
	catch (const std::logic_error &e)
	{
		// a locomotor case the port reports as unported (S-081 / S-084): the unit stops, the failure is kept in the world's report
		m_world->reportMovementFailure(*getObject(), e.what());
		m_mover->destroyPath();
		m_mover->endingMove();
		m_machine->clear();
		m_machine->setState(AI_IDLE);
		moverSleep = (unsigned)UPDATE_SLEEP_FOREVER;
	}
	m_isInUpdate = false;
	getObject()->setStatus((unsigned)CombatNames::statuses().updatingAI, false);
	m_world->noteStop(kStopUpdate);
	return (UpdateSleepTime)(moverSleep < 1u ? 1u : moverSleep);
}

// B1 AIUpdate.cpp:2574-2594: the model condition goes, and the pathfinder's goal for the unit is refreshed (a goal further than a cell from the unit is
// replaced by the cell under it)
void AIUpdateInterface::doMovementCompleteBookkeeping()
{
	Object *obj = getObject();
	obj->setModelConditionState(modelConditionIndex("MOVING"), false);
	Coord3D goalPos;
	Pathfinder &pf = m_world->pathfinder();
	if (pf.goalPosition(*m_adapter, &goalPos))
	{
		const Coord3D &p = *obj->getPosition();
		const float dx = SimMath::subf32(goalPos.x, p.x), dy = SimMath::subf32(goalPos.y, p.y);
		if (SimMath::addf32(SimMath::mulf32(dx, dx), SimMath::mulf32(dy, dy)) >= 100.0f)
		{
			goalPos = p;
			pf.snapPosition(*m_adapter, &goalPos);
		}
		pf.updateGoal(*m_adapter, &goalPos, m_adapter->getLayer());
	}
	pf.updatePos(*m_adapter);
}

void AIUpdateInterface::reactToTransformChange()
{
	if (m_world->mapReady())
	{
		m_world->pathfinder().updatePos(*m_adapter);
	}
}

void AIUpdateInterface::wakeUpNow()
{
	if (getWakeFrame() > (UnsignedInt)UPDATE_SLEEP_NONE && !m_isInUpdate)
	{
		setWakeFrame(getObject(), UPDATE_SLEEP_NONE);
	}
}

void AIUpdateInterface::setQueueForPathTime(unsigned frames)
{
	if (frames >= (unsigned)UPDATE_SLEEP_NONE && getWakeFrame() > frames && !m_isInUpdate)
	{
		setWakeFrame(getObject(), UPDATE_SLEEP(frames));
	}
	m_mover->setQueueForPathTime(frames);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// commands (B1 AIUpdate.cpp:4511-5075)
// ---------------------------------------------------------------------------------------------------------------------------------
void AIUpdateInterface::goalPositionClipped(const Coord3D &pos, CommandSourceType source, Coord3D &out) const
{
	out = pos;
	if (source == CMD_FROM_PLAYER)
	{
		// B1 setGoalPositionClipped (0x273DE0): a player's goal is clipped to the map extent minus half a partition cell (GameData
		// PartitionCellSize, 40 in the retail data); the extent is the pathfinder's logical extent
		ICoord2D lo, hi;
		m_world->pathfinder().getLogicalExtent(lo, hi);
		const float fudge = 20.0f;
		const float x0 = SimMath::addf32(SimMath::mulf32((float)lo.x, 10.0f), fudge), y0 = SimMath::addf32(SimMath::mulf32((float)lo.y, 10.0f), fudge);
		const float x1 = SimMath::subf32(SimMath::mulf32((float)(hi.x + 1), 10.0f), fudge), y1 = SimMath::subf32(SimMath::mulf32((float)(hi.y + 1), 10.0f), fudge);
		if (out.x < x0) out.x = x0;
		if (out.x > x1) out.x = x1;
		if (out.y < y0) out.y = y0;
		if (out.y > y1) out.y = y1;
	}
}

void AIUpdateInterface::aiMoveToPosition(const Coord3D &pos, CommandSourceType source)
{
	if (!acceptCommand(source, 0))
	{
		return; // lane MODULES-3: RW 0x667174
	}
	m_world->noteStop(kStopCommands);
	if (source != CMD_FROM_AI)
	{
		m_attackMoveActive = false; // a move order ends an attack-move (COMBAT-1)
	}
	privateMoveToPosition(pos, source);
	wakeUpNow();
}

bool AIUpdateInterface::aiMoveToPositionFromAttackMove(const Coord3D &goal)
{
	if (!acceptCommand(CMD_FROM_PLAYER, 0))
	{
		return false; // lane MODULES-3 r2: RW 0x667174 before any change
	}
	privateMoveToPosition(goal, CMD_FROM_PLAYER);
	wakeUpNow();
	return true;
}

void AIUpdateInterface::privateMoveToPosition(const Coord3D &pos, CommandSourceType source)
{
	Object *obj = getObject();
	if (hasKind(*obj, kindBits().immobile))
	{
		return; // B1 privateMoveToPosition: getObject()->isMobile() == FALSE
	}
	Coord3D goal;
	goalPositionClipped(pos, source, goal);
	if (!isIdle() && source == CMD_FROM_AI)
	{
		// an internally generated move of a busy unit: a temporary state for 20 seconds (B1 AIUpdate.cpp:4531)
		m_machine->setGoalPosition(goal);
		m_machine->setGoalObject(INVALID_ID);
		m_machine->setTemporaryState(AI_MOVE_TO, (unsigned)LOGICFRAMES_PER_SECOND * 20u);
	}
	else
	{
		m_machine->clear();
		m_machine->setGoalPosition(goal);
		m_machine->setGoalObject(INVALID_ID);
		setLastCommandSource(source);
		m_machine->setState(AI_MOVE_TO);
	}
}

void AIUpdateInterface::aiMoveToPositionAndOrientate(const Coord3D &pos, float angle, CommandSourceType source)
{
	if (!acceptCommand(source, -1))
	{
		return; // lane MODULES-3: RW 0x667174
	}
	m_world->noteStop(kStopCommands);
	Object *obj = getObject();
	if (hasKind(*obj, kindBits().immobile))
	{
		return;
	}
	Coord3D goal;
	goalPositionClipped(pos, source, goal);
	m_machine->clear();
	m_machine->setGoalPosition(goal);
	m_machine->setGoalObject(INVALID_ID);
	m_machine->setGoalAngle(angle);
	setLastCommandSource(source);
	m_machine->setState(AI_MOVE_TO);
	wakeUpNow();
}

void AIUpdateInterface::aiMoveToObject(Object *target, CommandSourceType source)
{
	if (!acceptCommand(source, 1))
	{
		return; // lane MODULES-3: RW 0x667174
	}
	m_world->noteStop(kStopCommands);
	if (!target || hasKind(*getObject(), kindBits().immobile) || m_mover->isAiDead())
	{
		return;
	}
	m_machine->clear();
	m_machine->setGoalObject(target->getID());
	m_machine->setGoalPosition(*target->getPosition());
	setLastCommandSource(source);
	m_machine->setState(AI_MOVE_TO);
	wakeUpNow();
}

// B1 AIUpdate.cpp:4722
void AIUpdateInterface::aiIdle(CommandSourceType source)
{
	if (!acceptCommand(source, -1))
	{
		return; // lane MODULES-3: RW 0x667174
	}
	m_world->noteStop(kStopCommands);
	if (source != CMD_FROM_AI)
	{
		m_attackMoveActive = false;
	}
	setLastCommandSource(source);
	m_machine->clear();
	m_machine->setGoalObject(INVALID_ID);
	m_machine->setState(AI_IDLE);
	wakeUpNow();
}

void AIUpdateInterface::aiBusy(CommandSourceType source)
{
	if (!acceptCommand(source, -1))
	{
		return; // lane MODULES-3: RW 0x667174
	}
	setLastCommandSource(source);
	m_machine->clear();
	m_machine->setState(AI_BUSY);
	wakeUpNow();
}

// B1 AIUpdate.cpp:5039
void AIUpdateInterface::aiFollowPath(const std::vector<Coord3D> &path, Object *ignoreObject, CommandSourceType source, bool exitProduction)
{
	if (!acceptCommand(source, -1))
	{
		return; // lane MODULES-3: RW 0x667174
	}
	m_world->noteStop(kStopCommands);
	if (path.empty())
	{
		return;
	}
	if (hasKind(*getObject(), kindBits().immobile))
	{
		return;
	}
	m_machine->clear();
	m_machine->setGoalPath(path);
	m_mover->ignoreObstacle(ignoreObject ? ignoreObject->getID() : PATHFIND_INVALID_ID);
	setLastCommandSource(source);
	m_machine->setState(exitProduction ? AI_FOLLOW_EXITPRODUCTION_PATH : AI_FOLLOW_PATH);
	wakeUpNow();
}

// B1 AIUpdate.cpp:5005: add a point to the path being followed (or start one)
void AIUpdateInterface::aiFollowPathAppend(const Coord3D &pos, CommandSourceType source)
{
	if (!acceptCommand(source, -1))
	{
		return; // lane MODULES-3: RW 0x667174
	}
	m_world->noteStop(kStopCommands);
	const unsigned st = m_machine->currentStateId();
	if (st != (unsigned)AI_FOLLOW_PATH)
	{
		m_machine->clear();
		m_machine->setGoalPath(std::vector<Coord3D>());
	}
	m_machine->addToGoalPath(pos);
	setLastCommandSource(source);
	if (st != (unsigned)AI_FOLLOW_PATH)
	{
		m_machine->setState(AI_FOLLOW_PATH);
	}
	wakeUpNow();
}

// ---- lane GARRISON-1 ----
// RW 0x66D85F privateEnter (AI command 0x42)
bool AIUpdateInterface::aiEnter(Object *container, CommandSourceType source)
{
	if (!acceptCommand(source, 0x42))
	{
		return false; // RW 0x667174: aiDoCommand's gate (lane MODULES-3) before the command
	}
	if (!container || !GarrisonRules::isMobile(*getObject()) || m_mover->isAiDead())
	{
		return false;
	}
	// RW 0x66D873: the locomotor's RW 0x5E39CE(obj) and the AI fields + 0x16C / + 0x3B8 are not ported (S-1101)
	m_machine->clear();
	m_machine->setGoalObject(container->getID());
	setLastCommandSource(source);
	m_machine->setState(AI_MOVE_TO_POSITION_AND_ENTER);
	wakeUpNow();
	return true;
}

// RW 0x664579 privateEnterObject (AI command 0x17)
void AIUpdateInterface::aiEnterObject(Object *container, CommandSourceType source)
{
	if (!acceptCommand(source, 0x17))
	{
		return; // RW 0x667174: aiDoCommand's gate (lane MODULES-3) before the command
	}
	Object *obj = getObject();
	if (ContainModuleInterface *c = obj->getContain())
	{
		if (HordeContainInterface *h = c->getHordeContainInterface())
		{
			h->enterContainer(container, (int)source); // horde slot 0x80
			return;
		}
	}
	if (!GarrisonRules::isMobile(*obj) || !GarrisonRules::canEnterObject(*obj, container, source, 1, false))
	{
		return;
	}
	m_machine->clear();
	m_machine->setGoalObject(container->getID());
	setLastCommandSource(source);
	m_machine->setState(AI_ENTER);
	wakeUpNow();
}

// RW 0x6645F3 (AI command 0x3D)
void AIUpdateInterface::aiHordeEnter(Object *container, CommandSourceType source)
{
	if (!acceptCommand(source, 0x3D))
	{
		return; // RW 0x667174: aiDoCommand's gate (lane MODULES-3) before the command
	}
	m_machine->clear();
	m_machine->setGoalObject(container ? container->getID() : INVALID_ID);
	setLastCommandSource(source);
	m_machine->setState(AI_HORDE_ENTER);
	wakeUpNow();
}

// RW 0x66468F privateExit (AI command 0x1A)
void AIUpdateInterface::aiExit(Object *container, CommandSourceType source)
{
	if (!acceptCommand(source, 0x1A))
	{
		return; // RW 0x667174: aiDoCommand's gate (lane MODULES-3) before the command
	}
	Object *obj = getObject();
	if (!container)
	{
		container = obj->getContainedBy();
		if (!container)
		{
			return;
		}
	}
	if (ContainModuleInterface *c = obj->getContain())
	{
		if (HordeContainInterface *h = c->getHordeContainInterface())
		{
			h->exitContainer(container, (int)source); // horde slot 0x84
			return;
		}
	}
	m_machine->clear();
	m_machine->setGoalObject(container->getID());
	setLastCommandSource(source);
	m_machine->setState(AI_EXIT);
	wakeUpNow();
}

// RW 0x664767 (AI command 0x3E)
void AIUpdateInterface::aiHordeExit(Object *container, CommandSourceType source)
{
	if (!acceptCommand(source, 0x3E))
	{
		return; // RW 0x667174: aiDoCommand's gate (lane MODULES-3) before the command
	}
	Object *obj = getObject();
	if (!container)
	{
		container = obj->getContainedBy();
		if (!container)
		{
			return;
		}
	}
	m_machine->clear();
	m_machine->setGoalObject(container->getID());
	setLastCommandSource(source);
	m_machine->setState(AI_HORDE_EXIT);
	wakeUpNow();
}

void AIUpdateInterface::aiUnported(const char *command, CommandSourceType)
{
	++m_unportedCommands[command];
	m_world->noteStop(kStopCommands);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// queries
// ---------------------------------------------------------------------------------------------------------------------------------
// RW 0x6643FC: the object's flag (+0x458 bit 0) first, then a horde member is idle when its horde's AI is, then the machine's current state
bool AIUpdateInterface::isIdle() const
{
	Object *obj = getObject();
	if (obj->testStatus((unsigned)statusBits().hordeMember))
	{
		Object *horde = obj->getContainedBy();
		if (horde)
		{
			if (AIUpdateInterface *hai = horde->getAIUpdateInterface())
			{
				if (hai->isIdle())
				{
					return true;
				}
			}
		}
	}
	if (!m_machine || !m_machine->currentState())
	{
		return true;
	}
	return m_machine->currentState()->isIdle();
}

bool AIUpdateInterface::isMoving() const
{
	return m_mover->isMoving();
}

// B1 AIUpdate.cpp:3978; RW 0x667144 (its test of the AI's + 0x1DC == 8 is not traced: B1 has no such branch)
bool AIUpdateInterface::isDoingGroundMovement() const
{
	if (!m_curLocomotor)
	{
		return false;
	}
	const unsigned surfaces = m_curLocomotor->getTemplate().m_surfaces;
	if (surfaces == (unsigned)LOCOMOTORSURFACE_AIR || (surfaces & (unsigned)LOCOMOTORSURFACE_AIR))
	{
		return false;
	}
	return (getObject()->getDisabledMask() & (1u << 3)) == 0; // RW 0x667163: object + 0x1C8 bit 3, DISABLED_HELD: a held object (a garrison's rider) is not doing ground movement
}

// B1 AIUpdate.cpp:4010: hover and wings adjust
bool AIUpdateInterface::isAircraftThatAdjustsDestination() const
{
	if (!m_curLocomotor)
	{
		return false;
	}
	const int a = m_curLocomotor->getTemplate().m_appearance;
	return a == LOCO_HOVER || a == LOCO_WINGS;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// locomotors (B1 AIUpdate.cpp:2326-2430)
// ---------------------------------------------------------------------------------------------------------------------------------
void AIUpdateInterface::setLocomotorUpgrade(bool on)
{
	m_upgradedLocomotors = on;
	if (m_curLocomotorSet == LOCOMOTORSET_NORMAL || m_curLocomotorSet == LOCOMOTORSET_NORMAL_UPGRADED)
	{
		m_curLocomotorSet = -1;
		chooseLocomotorSet(LOCOMOTORSET_NORMAL);
	}
}

bool AIUpdateInterface::chooseLocomotorSet(int set)
{
	if (set == LOCOMOTORSET_NORMAL && m_upgradedLocomotors)
	{
		set = LOCOMOTORSET_NORMAL_UPGRADED;
	}
	if (set == m_curLocomotorSet)
	{
		return true;
	}
	const LocomotorSetTemplate::Slot *slot = m_info->locomotorSets.find(set);
	if (!slot || slot->locomotors.empty())
	{
		return false;
	}
	// chooseLocomotorSetExplicit: the locomotor instances of the set, in set order (a template that did not resolve stays null in retail and is skipped)
	m_locomotors.clear();
	m_curLocomotor = nullptr;
	for (const LocomotorTemplate *lt : slot->locomotors)
	{
		if (lt)
		{
			m_locomotors.push_back(std::make_unique<Locomotor>(lt));
		}
	}
	m_curLocomotorSet = set;
	m_locomotorSetSpeed = slot->speed;
	chooseGoodLocomotorFromCurrentSet();
	return true;
}

void AIUpdateInterface::chooseGoodLocomotorFromCurrentSet()
{
	Locomotor *prev = m_curLocomotor;
	Locomotor *chosen = nullptr;
	if (m_world->mapReady())
	{
		const Coord3D pos = *getObject()->getPosition();
		PathfindCell *cell = m_world->pathfinder().getCell(m_adapter->getLayer(), &pos);
		if (cell)
		{
			const unsigned fit = Pathfinder::validLocomotorSurfacesForCellType(cell->getType());
			for (const std::unique_ptr<Locomotor> &l : m_locomotors)
			{
				if (l->getTemplate().m_surfaces & fit)
				{
					chosen = l.get();
					break;
				}
			}
		}
	}
	if (!chosen)
	{
		chosen = prev;
		if (!chosen)
		{
			for (const std::unique_ptr<Locomotor> &l : m_locomotors)
			{
				if (l->getTemplate().m_surfaces & (unsigned)LOCOMOTORSURFACE_GROUND)
				{
					chosen = l.get();
					break;
				}
			}
			if (!chosen && !m_locomotors.empty())
			{
				chosen = m_locomotors.front().get();
			}
		}
	}
	m_curLocomotor = chosen;
	m_world->noteStop(kStopLocomotor);
}

float AIUpdateInterface::curLocomotorSpeed() const
{
	return m_curLocomotor ? m_curLocomotor->getMaxSpeedForCondition(*m_locoHost) : 0.0f;
}

// B1 AIUpdate.cpp:3925-3975
void AIUpdateInterface::setLocomotorGoalNone()
{
	m_mover->setGoalNone();
}

void AIUpdateInterface::setLocomotorGoalOrientation(float angle)
{
	m_mover->setGoalAngle(angle);
}

void AIUpdateInterface::setLocomotorGoalPositionExplicit(const Coord3D &pos)
{
	m_mover->setGoalExplicit(pos);
}

// RW 0x662934 (AI vtable +0x214): goal type 4 (explicit with path): the old path goes unless the goal already was type 4, the goal position is stored
void AIUpdateInterface::hordeMemberMoveTo(const Coord3D &dest)
{
	m_mover->setGoalExplicitWithPath(dest);
	wakeUpNow();
}

// RW 0x662916 (AI vtable +0x210): goal type 2 (explicit): the goal position is stored
void AIUpdateInterface::hordeMemberMoveExplicit(const Coord3D &dest)
{
	m_mover->setGoalExplicit(dest);
	wakeUpNow();
}

// ---------------------------------------------------------------------------------------------------------------------------------
// AIMoveHost
// ---------------------------------------------------------------------------------------------------------------------------------
PathfindObject &AIUpdateInterface::pathfindObject()
{
	return *m_adapter;
}

LocomotorHost &AIUpdateInterface::locomotorHost()
{
	return *m_locoHost;
}

PathfindLocomotorInfo AIUpdateInterface::locomotorInfo() const
{
	PathfindLocomotorInfo info;
	if (m_curLocomotor)
	{
		const LocomotorTemplate &t = m_curLocomotor->getTemplate();
		// the surfaces of the whole current locomotor set (RW ai + 0x1CC), not of the one locomotor chosen now: the path may cross terrain a later choice walks
		info.validSurfaces = 0;
		for (const std::unique_ptr<Locomotor> &l : m_locomotors)
		{
			info.validSurfaces |= l->getTemplate().m_surfaces;
		}
		info.downhillOnly = t.m_downhillOnly;
		info.crusher = t.m_scalesWalls; // S-222 inference (RW loco+0x15)
	}
	return info;
}

unsigned AIUpdateInterface::frame() const
{
	return getObject()->logic().getFrame();
}

void AIUpdateInterface::setPosition(const Coord3D &p)
{
	getObject()->setPosition(&p);
}

float AIUpdateInterface::orientation() const
{
	return getObject()->getOrientation();
}

void AIUpdateInterface::setOrientation(float angle)
{
	getObject()->setOrientation(angle);
}

float AIUpdateInterface::groundHeightAt(float x, float y) const
{
	return getObject()->logic().getGroundHeight(x, y);
}

void AIUpdateInterface::setLayer(PathfindLayerEnum layer)
{
	m_adapter->setLayer(layer);
}

bool AIUpdateInterface::isImmobile() const
{
	return hasKind(*getObject(), kindBits().immobile);
}

bool AIUpdateInterface::isStatusImmobile() const
{
	return getObject()->testStatus((unsigned)statusBits().immobile);
}

// RW 0x6939DF
bool AIUpdateInterface::isContained() const
{
	Object *obj = getObject();
	Object *container = obj->getContainedBy();
	if (obj->testStatus((unsigned)statusBits().hordeMember))
	{
		return container != nullptr;
	}
	return container != nullptr && hasKind(*container, kindBits().horde);
}

AIMover *AIUpdateInterface::moverOf(PathfindObjectID id)
{
	return m_world->moveWorld().find(id);
}

AIMover *AIUpdateInterface::containerMover()
{
	Object *container = getObject()->getContainedBy();
	if (!container)
	{
		return nullptr;
	}
	AIUpdateInterface *ai = container->getAIUpdateInterface();
	return ai ? &ai->mover() : nullptr;
}

// RW 0x68B34C: the locomotor's current speed (RW 0x5E36F7) when it is above zero, else 0
float AIUpdateInterface::speedOf(const PathfindObject &o) const
{
	if (const ObjectPathfindAdapter *a = dynamic_cast<const ObjectPathfindAdapter *>(&o))
	{
		if (AIUpdateInterface *ai = a->ai())
		{
			if (ai->curLocomotor())
			{
				const float s = ai->curLocomotor()->speed();
				return s > 0.0f ? s : 0.0f;
			}
		}
	}
	return 0.0f;
}

Coord2D AIUpdateInterface::unitDirectionOf(const PathfindObject &o) const
{
	Coord2D d;
	d.x = 1.0f;
	d.y = 0.0f;
	if (const ObjectPathfindAdapter *a = dynamic_cast<const ObjectPathfindAdapter *>(&o))
	{
		const float *b = a->object().getBasis();
		d.x = b[0];
		d.y = b[3];
	}
	return d;
}

// RW 0x663F48: (DOZER ? 100 : 0) + 10 * veterancy level (0 until the experience tracker, S-142) + (CAVALRY ? 5 : 0)
int AIUpdateInterface::pathPriority() const
{
	int p = 0;
	if (hasKind(*getObject(), kindBits().dozer))
	{
		p += 100;
	}
	if (hasKind(*getObject(), kindBits().cavalry))
	{
		p += 5;
	}
	return p;
}

PathfindObjectID AIUpdateInterface::attackTargetOf(const PathfindObject &) const
{
	return PATHFIND_INVALID_ID; // the attack machine is the combat milestone's
}

float AIUpdateInterface::wanderFactor() const
{
	return m_curLocomotor ? m_curLocomotor->getTemplate().m_wanderWidthFactor : 0.0f;
}

float AIUpdateInterface::closeEnoughDist() const
{
	return m_curLocomotor ? m_curLocomotor->getTemplate().m_closeEnoughDist : 0.0f;
}

float AIUpdateInterface::locomotorSpeed() const
{
	return m_curLocomotor ? m_curLocomotor->speed() : 0.0f;
}

bool AIUpdateInterface::movingBackwards() const
{
	return m_curLocomotor && (m_curLocomotor->flags() & LOCOMOTOR_FLAG_BACKING_UP) != 0;
}

namespace
{
// RW 0x748B14 (lane AUDIO-3): the one-shot move start of `obj`: SoundMoveStartDamaged (row 0x22, RW 0x74868F) when the body is worse than DAMAGED (vslot 0x24
// > 1) and it has one, else SoundMoveStart (row 0x21, RW 0x748676), added for the object (no handle kept)
void playMoveStart(const Object &obj)
{
	const ThingTemplate *tt = obj.getTemplate();
	if (!tt)
	{
		return;
	}
	const BodyModuleInterface *body = obj.getBodyModule();
	std::string sound;
	if (body && body->getDamageState() > BODY_DAMAGED)
	{
		sound = UnitSpecificSound::templateSound(*tt, "SoundMoveStartDamaged");
	}
	if (sound.empty())
	{
		sound = UnitSpecificSound::templateSound(*tt, "SoundMoveStart");
	}
	if (!sound.empty())
	{
		AudioApi::playSoundForObject(sound, obj.getID());
	}
}
} // namespace

void AIUpdateInterface::startMoveSound()
{
	if (m_destroying)
	{
		return;
	}
	// RW 0x748C0B: the object's one-shot start (RW 0x748B14), then its loop: SoundMoveLoopDamaged (row 0x24) when worse than DAMAGED and set, else SoundMoveLoop
	// (row 0x23); a loop that exists replaces the kept one (RW 0x748CC3) and its handle is kept (state + 0x40); then the one-shot start of every object the
	// object contains (RW 0x748D32: its contain's list, vslot 0x108)
	const Object *obj = getObject();
	playMoveStart(*obj);
	const BodyModuleInterface *body = obj->getBodyModule();
	std::string loop;
	if (body && body->getDamageState() > BODY_DAMAGED)
	{
		loop = UnitSpecificSound::templateSound(*obj->getTemplate(), "SoundMoveLoopDamaged");
	}
	if (loop.empty())
	{
		loop = UnitSpecificSound::templateSound(*obj->getTemplate(), "SoundMoveLoop");
	}
	if (!loop.empty())
	{
		AudioApi::postHeldSound(AudioApi::HELD_MOVE_LOOP, loop, obj->getID(), obj->getID());
		m_moveLoopPosted = true;
	}
	if (const ContainModuleInterface *contain = obj->getContain())
	{
		if (const ContainModuleInterface::ContainedItemsList *items = contain->getContainedItemsList())
		{
			for (const Object *member : *items)
			{
				if (member)
				{
					playMoveStart(*member);
				}
			}
		}
	}
}

void AIUpdateInterface::stopMoveSound()
{
	if (m_moveLoopPosted)
	{
		m_moveLoopPosted = false;
		AudioApi::stopHeldSound(AudioApi::HELD_MOVE_LOOP, getObject()->getID());
	}
}

bool AIUpdateInterface::setModelCondition(const char *name, bool on)
{
	if (m_destroying)
	{
		return true;
	}
	getObject()->setModelConditionState(modelConditionIndex(name), on);
	return true;
}

// RW relAngle2D(obj, p): the angle to the point relative to the object's facing, in [-pi, pi]
float AIUpdateInterface::relativeAngleTo(const Coord3D &p) const
{
	const Coord3D &pos = *getObject()->getPosition();
	const float dx = SimMath::subf32(p.x, pos.x), dy = SimMath::subf32(p.y, pos.y);
	const double a = SimMath::atan2d(dy, dx);
	return normalizeAngle(NumericState::pc24SubD(a, (double)getObject()->getOrientation())); // fld double; fsub dword; fstp dword (PC24, one store)
}

bool AIUpdateInterface::isCellTypeTwo(const Coord3D &p, PathfindLayerEnum layer) const
{
	if (!m_world->mapReady())
	{
		return false;
	}
	const PathfindCell *cell = m_world->pathfinder().getCell(layer, &p);
	return cell && cell->getType() == PathfindCell::CELL_CLIFF; // RW 0x6EAD0E: the cell type is 2
}

std::vector<std::string> AIUpdateInterface::allStops()
{
	return { kStopCommands, kStopUpdate, kStopLocomotor, kStopHost, kStopStates };
}

std::vector<std::string> AIUpdateInterface::stopsRaised() const
{
	std::vector<std::string> out = m_mover->stops();
	if (m_curLocomotor)
	{
		for (const std::string &s : m_curLocomotor->unverified())
		{
			out.push_back(s);
		}
	}
	out.push_back(kStopHost);
	out.push_back(kStopStates);
	return out;
}

TurretAI *AIUpdateInterface::turretForCurWeapon() const
{
	return m_turret && m_turret->isOwnersCurWeaponOnTurret() ? m_turret.get() : nullptr;
}

float AIUpdateInterface::turretTurnRate() const
{
	return m_turret ? m_turret->data().m_turnRate : 0.0f;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// the OpenBFME state hash
// ---------------------------------------------------------------------------------------------------------------------------------
void AIUpdateInterface::crc(StateHasher &h) const
{
	UpdateModule::crc(h);
	for (std::uint32_t w : m_luaConditionSnapshot) // lane HERO-2
	{
		h.addU32(w);
	}
	h.addU32(currentStateId());
	h.addI32(m_curLocomotorSet);
	h.addU32((std::uint32_t)m_lastCommandSource);
	h.addU32(m_currentVictim);
	h.addU32(m_targetHorde);
	h.addU32(m_moveAwayRequesters[0]); // lane PHYS-1
	h.addU32(m_moveAwayRequesters[1]);
	h.addBool(m_canPathThroughUnits);
	h.addBool(m_attackMoveActive);
	if (m_attitude != 0) // lane SCRIPT-3: hashed once a script changed it
	{
		h.addI32(0x218 + m_attitude);
	}
	h.addBool(m_preventPlayerCommands); // lane MODULES-3
	h.addBool(m_fastCower);
	h.addU32(m_minCowerTime);
	h.addU32(m_maxCowerTime);
	h.addFloat(m_attackMoveGoal.x);
	h.addFloat(m_attackMoveGoal.y);
	h.addFloat(m_attackMoveGoal.z);
	h.addU32(m_nextMoodCheck);
	h.addBool(m_isInUpdate);
	h.addBool(m_pendingValid);
	h.addFloat(m_pendingPosition.x);
	h.addFloat(m_pendingPosition.y);
	h.addFloat(m_pendingPosition.z);
	h.addBool(m_turret != nullptr); // lane GARRISON-2
	if (m_turret)
	{
		m_turret->crc(h);
	}
	m_mover->crc(h);
	{
		// the unit's reservations in the pathfinder (goal cell and position cell: -1 when none)
		const ICoord2D g = m_world->pathfinder().pathfindGoalCell(m_id), c = m_world->pathfinder().curPathfindCell(m_id);
		h.addI32(g.x);
		h.addI32(g.y);
		h.addI32(c.x);
		h.addI32(c.y);
		h.addI32((int)m_adapter->getLayer());
	}
	h.addBool(m_curLocomotor != nullptr);
	for (const std::unique_ptr<Locomotor> &l : m_locomotors)
	{
		h.addBool(l.get() == m_curLocomotor);
		l->crc(h);
	}
	h.addFloat(m_locomotorSetSpeed);
	h.addBool(m_upgradedLocomotors);
	h.addBool(m_canPathThroughUnits);
	h.addBool(m_machine != nullptr);
	if (m_machine)
	{
		m_machine->crc(h);
	}
}

// ---------------------------------------------------------------------------------------------------------------------------------
// the module classes
// ---------------------------------------------------------------------------------------------------------------------------------
void AIUpdateInterface::registerClasses(ModuleFactory &modules)
{
	// every AI module class of the registry (RW module-registry golden: isAiModuleData true). The classes with behaviour of their own that is not ported
	// run as the base AI and report it (S-222); HordeAIUpdate is ported here
	for (const std::string &name : modules.unportedModuleClassNames())
	{
		const ModuleFactory::ModuleTemplate *info = modules.findModuleTemplate(name, MODULETYPE_BEHAVIOR);
		if (!info || !info->isAiModuleData)
		{
			continue;
		}
		const bool horde = name == "HordeAIUpdate";
		modules.bindModuleProc(name, MODULETYPE_BEHAVIOR, [horde](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &info) -> std::unique_ptr<Module> {
			// a logic without an AIWorld (the LOGIC-1 fixtures) has no pathfinder to move on: the class is then the explicit unported module of S-140, as before
			if (!thing->asObject() || !thing->asObject()->logic().aiWorld())
			{
				return makeUnportedModule(thing, data, info.name, info.type, info.interfaceMask);
			}
			if (horde)
			{
				return std::make_unique<HordeAIUpdate>(thing, data);
			}
			return std::make_unique<AIUpdateInterface>(thing, data);
		});
	}
}
