// OpenBFME. GPL-3.0.
//
// GiantBirdAIUpdate (lane FLY-1); see GiantBirdAIUpdate.h for the target facts, the donor and the stop.

#include "GameLogic/Module/GiantBirdAIUpdate.h"

#include "Common/GameCommon.h"
#include "Common/INI/HostRealText.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/Module/UnportedModule.h"
#include "Common/NumericState.h"
#include "Common/RandomValue.h"
#include "Common/StateHash.h"
#include "Common/Thing/RawModuleData.h"
#include "GameLogic/AI/AIEmotionStates.h"
#include "GameLogic/AI/AIGarrisonStates.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/FXEvents.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Module/PhysicsBehavior.h"
#include "GameLogic/Module/StancesBehavior.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/ObjectGeometry.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponState.h"

#include <cfloat>
#include <stdexcept>
#include <cstdlib>
#include "GameLogic/AI/AIXfer.h"

#include <algorithm>
#include <iterator>

namespace
{
const char *const kGiantBirdCpp = "GiantBirdAIUpdate.cpp";

std::string trimmed(const std::string &s)
{
	const size_t b = s.find_first_not_of(" \t\r\n");
	const size_t e = s.find_last_not_of(" \t\r\n");
	return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
}

bool sameNoCase(const std::string &a, const char *b)
{
	size_t i = 0;
	for (; i < a.size() && b[i]; ++i)
	{
		if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i]))
		{
			return false;
		}
	}
	return i == a.size() && !b[i];
}

int kindBit(const char *name)
{
	return ObjectTemplateInfoBuilder::kindOfIndex(name);
}

bool isKind(const Object &o, int bit)
{
	return bit >= 0 && o.isKindOf((unsigned)bit);
}

void clearCondition(Object &o, int bit)
{
	if (bit >= 0 && o.testModelCondition(bit))
	{
		o.setModelConditionState(bit, false);
	}
}

void setCondition(Object &o, int bit)
{
	if (bit >= 0 && !o.testModelCondition(bit))
	{
		o.setModelConditionState(bit, true);
	}
}

struct BirdNames
{
	int moving = AIUpdateInterface::modelConditionBit("MOVING");
	int freefall = AIUpdateInterface::modelConditionBit("FREEFALL");
	int climbing = AIUpdateInterface::modelConditionBit("CLIMBING");
	int aboutToHit = AIUpdateInterface::modelConditionBit("ABOUT_TO_HIT");
	int backingUp = AIUpdateInterface::modelConditionBit("BACKING_UP");
	int stunned = AIUpdateInterface::modelConditionBit("STUNNED");
	int hitReaction = AIUpdateInterface::modelConditionBit("HIT_REACTION");
	int specialDamaged = AIUpdateInterface::modelConditionBit("SPECIAL_DAMAGED");
	int airborneTarget = CombatNames::status("AIRBORNE_TARGET");
	int updatingAI = CombatNames::status("UPDATING_AI");
	int movingToDismount = CombatNames::status("MOVING_TO_DISMOUNT");
	int leavingFactory = CombatNames::status("IS_LEAVING_FACTORY");
	int unattackable = CombatNames::status("UNATTACKABLE");
	int grabAndDropStatus = CombatNames::status("GRAB_AND_DROP");
	int immobile = kindBit("IMMOBILE");
	int horde = kindBit("HORDE");
	int machine = kindBit("MACHINE");
	int grabAndDrop = kindBit("GRAB_AND_DROP");
	int producedAtHelipad = kindBit("PRODUCED_AT_HELIPAD"); // RW 0x76DBDB isKindOf(0x48)
	int structure = kindBit("STRUCTURE");
	int monster = kindBit("MONSTER");
};

const BirdNames &names()
{
	static const BirdNames n;
	return n;
}

// RW 0x644FD0
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

// RW 0x5E3BE4: (dx^2 + dy^2) + dz^2 at x87 PC24, kept wide
double distanceSquared3(const Coord3D &a, const Coord3D &b)
{
	const double dx = SimMath::pc24SubW((double)a.x, (double)b.x), dy = SimMath::pc24SubW((double)a.y, (double)b.y), dz = SimMath::pc24SubW((double)a.z, (double)b.z);
	return SimMath::pc24AddW(SimMath::pc24AddW(SimMath::pc24MulW(dx, dx), SimMath::pc24MulW(dy, dy)), SimMath::pc24MulW(dz, dz));
}

// RW 0x70B8AE Object::getHeightAboveTerrain
float heightAboveTerrain(const Object &o)
{
	const Coord3D &p = *o.getPosition();
	return SimMath::pc24Sub(p.z, o.logic().getGroundHeight(p.x, p.y));
}

// the module's stop line, kept alive for AIWorld::noteStop (which keeps the pointer)
const char *birdStop()
{
	static const std::string line = GiantBirdAIUpdate::stopLines()[0];
	return line.c_str();
}

GiantBirdAIUpdate *birdOf(Object *o)
{
	return o ? dynamic_cast<GiantBirdAIUpdate *>(o->getAIUpdateInterface()) : nullptr;
}

// RW 0x690BD2 with no label, mode 1 and a seed (the geometry's random contact point): stop S-362's approximation (the geometry centre), the seed drawn
Coord3D seededContactPoint(Object &victim, int line)
{
	GameLogic &logic = victim.logic();
	(void)logic.random().getValue(0, 12345678, kGiantBirdCpp, line); // GetGameLogicRandomValue(0, 0xBC614E)
	Coord3D aim = *victim.getPosition();
	aim.z = SimMath::addf32(aim.z, SimMath::mulf32(CombatQueries::geometry(victim).height, 0.5f));
	return aim;
}

// the victim's contact point of the swoop / attack states: the AI's labelled point (RW 0x667C76), else the seeded one (RW 0x690BD2 at `line`)
// INFERENCE (S-4040): RW 0x667C76 with `preferred` false runs GeometryInfo::getBestContactPoint's overlap test (RW 0xAD2CE0: the target's shapes against the static
// point geometry RW 0xDF1EA8 at the label's point), which is not ported (S-1281): the port's call refuses it with a logic error. The bird then takes the seeded
// contact point, as RW does for a target without the label, and the world's report carries S-1281
Coord3D contactPointOn(AIUpdateInterface &ai, Object &victim, int line)
{
	Coord3D pos;
	bool found = false;
	try
	{
		found = ai.findNearestLabeledContactPointOnTarget(victim, pos, *ai.getObject()->getPosition(), false);
	}
	catch (const std::logic_error &)
	{
		static const std::string stop = "[S-1281] GiantBirdAIUpdate: the labelled contact point (SpecialContactPoints, RW 0x667C76 without `preferred`) needs the "
		                                "overlap test RW 0xAD2CE0, not ported: the seeded contact point is used";
		ai.world().noteStop(stop.c_str());
		found = false;
	}
	if (!found)
	{
		pos = seededContactPoint(victim, line);
	}
	return pos;
}

// RW 0x68D30E Object::adjustVictim(attacker, 1, 0) on a horde: the member its HordeContainInterface (slot 0x48) picks for the attacker; INFERENCE (S-4040):
// the nearest living member, as AIUpdateInterface's attack command takes it
Object *hordeMemberFor(Object &horde, const Object &attacker)
{
	ContainModuleInterface *c = horde.getContain();
	if (!c || !c->getHordeContainInterface() || !c->getContainedItemsList())
	{
		return nullptr;
	}
	Object *best = nullptr;
	float bestD = 0.0f;
	for (Object *m : *c->getContainedItemsList())
	{
		if (!CombatQueries::isAlive(*m))
		{
			continue;
		}
		const float d = CombatQueries::centerDistanceSquared2D(*m->getPosition(), *attacker.getPosition());
		if (!best || d < bestD || (d == bestD && m->getID() < best->getID()))
		{
			best = m;
			bestD = d;
		}
	}
	return best;
}

// RW 0x6CB81A (the weapon template, the victim, &out): the weapon's PreferredTargetBone on the victim; the bone provider of the logic (as
// ObjectWeaponDelivery::fireFXAimPosition) is not asked here: false (S-4040), so the contact point decides
bool preferredTargetBonePoint(const WeaponTemplate &t, const Object &victim, Coord3D &out)
{
	(void)victim;
	(void)out;
	return t.m_preferredTargetBone.empty() ? false : false;
}
} // namespace

// ---------------------------------------------------------------------------------------------------------------------------------
// BirdHost: the LocomotorHost of the AI plus what the GIANT_BIRD movers read (GiantBirdLocomotorHost)
// ---------------------------------------------------------------------------------------------------------------------------------
class GiantBirdAIUpdate::BirdHost : public GiantBirdLocomotorHost
{
public:
	explicit BirdHost(GiantBirdAIUpdate &ai)
		: m_ai(ai)
	{
	}
	LocomotorHost &base() const { return m_ai.locomotorHost(); }
	Object &obj() const { return *m_ai.getObject(); }

	Coord3D getPosition() const override { return base().getPosition(); }
	float getAngle() const override { return base().getAngle(); }
	Coord2D getUnitDirectionVector2D() const override { return base().getUnitDirectionVector2D(); }
	float getBoundingRadius() const override { return base().getBoundingRadius(); }
	LocomotorMatrix getTransform() const override { return base().getTransform(); }
	void setTransform(const LocomotorMatrix &m) override { base().setTransform(m); }
	bool hasPendingPosition() const override { return base().hasPendingPosition(); }
	Coord3D getPendingPosition() const override { return base().getPendingPosition(); }
	void setPendingPosition(const Coord3D &p) override { base().setPendingPosition(p); }
	float locomotorSetSpeed() const override { return base().locomotorSetSpeed(); }
	int damageState() const override { return base().damageState(); }
	int movementPenaltyDamageState() const override { return base().movementPenaltyDamageState(); }
	float crewPowerMultiplier() const override { return base().crewPowerMultiplier(); }
	bool speedAttributeModifier(float &out) const override { return base().speedAttributeModifier(out); }
	bool isInRiver(float x, float y) const override { return base().isInRiver(x, y); }
	bool isTurnLimited() const override { return base().isTurnLimited(); }
	bool isChargeOrdered() const override { return base().isChargeOrdered(); }
	bool physicsMotionDisabled() const override { return base().physicsMotionDisabled(); }
	bool hasPhysicsModule() const override { return base().hasPhysicsModule(); }
	bool zMotionSuppressed() const override { return base().zMotionSuppressed(); }
	unsigned logicFrame() const override { return base().logicFrame(); }
	bool containerAllowsBackingUp() const override { return base().containerAllowsBackingUp(); }
	float groundHeightAt(float x, float y) const override { return base().groundHeightAt(x, y); }
	LocomotorPath *getPath() override { return base().getPath(); }
	bool testObjectStatus(int bit) const override { return base().testObjectStatus(bit); }
	bool testModelCondition(int bit) const override { return base().testModelCondition(bit); }
	void setModelCondition(int bit, bool value) override { base().setModelCondition(bit, value); }
	void clearAndSetModelConditions(const std::vector<int> &clear, const std::vector<int> &set) override { base().clearAndSetModelConditions(clear, set); }
	void notifyWheelsStopped() override { base().notifyWheelsStopped(); }
	bool hasHordeContain() const override { return base().hasHordeContain(); }
	void hordeBeginReform() override { base().hordeBeginReform(); }
	void hordeEndReform() override { base().hordeEndReform(); }
	bool hordeFormationReady(float angle) const override { return base().hordeFormationReady(angle); }
	bool thingWaitsForFormation() const override { return base().thingWaitsForFormation(); }
	bool isUnderwater(float x, float y, float *waterZ) const override { return base().isUnderwater(x, y, waterZ); }
	bool isDisabledHeld() const override { return base().isDisabledHeld(); }
	bool isKindOfSubmarine() const override { return base().isKindOfSubmarine(); }
	bool hasAI() const override { return base().hasAI(); }
	bool isEffectivelyDead() const override { return base().isEffectivelyDead(); }
	bool hasCurrentWeapon() const override { return base().hasCurrentWeapon(); }
	int currentWeaponStatusWriteBack() override { return base().currentWeaponStatusWriteBack(); }
	bool aiStateActive() const override { return base().aiStateActive(); }
	float gravity() const override { return base().gravity(); }
	bool hasStealthModule() const override { return base().hasStealthModule(); }

	GiantBirdFlight *aerialFlight() override { return &m_ai.m_flight; }
	bool aerialQuery(float distance, Coord3D &out) override { return AerialPathfinder::query(m_ai.m_spline, distance, out); }
	Coord3D aerialPathEnd() const override { return m_ai.m_spline.endPoint(); }
	bool aerialHigh() const override { return m_ai.isWellAbove(); }
	float boundingSphereRadius() const override { return CombatQueries::boundingSphereRadius(obj()); }
	float relativeAngle2D(const Coord3D &p) const override { return ObjectKnockback::relativeAngle2D(obj(), p); }
	void setTransformAtOnce(const LocomotorMatrix &m) override { base().setTransform(m); }
	float heightAboveTerrainOrWater() const override
	{
		// RW 0x70BC8F: over the standing water containing the point when there is one (TheTerrainLogic vslot 0x4C), else over the terrain (RW 0x70B8AE)
		const Object &o = obj();
		const Coord3D &p = *o.getPosition();
		float water = 0.0f;
		const TerrainLogic *terrain = o.logic().terrain();
		const float ground = o.logic().getGroundHeight(p.x, p.y);
		if (terrain && terrain->getStandingWaterHeight(p.x, p.y, water) && water > ground)
		{
			return SimMath::subf32(p.z, water);
		}
		return heightAboveTerrain(o);
	}
	std::uint32_t objectId() const override { return (std::uint32_t)obj().getID(); }
	float logicRandomReal(float lo, float hi, int line) override { return obj().logic().random().getValueReal(lo, hi, "Locomotor.cpp", line); }
	void setOrientation(float angle) override { obj().setOrientation(angle); }
	void setPositionZ(float z) override
	{
		Coord3D p = *obj().getPosition();
		p.z = z;
		obj().setPosition(&p); // RW 0x70C0AD
	}
	bool hasBody() const override { return obj().getBodyModule() != nullptr; }

private:
	GiantBirdAIUpdate &m_ai;
};

// ---------------------------------------------------------------------------------------------------------------------------------
// the states (RW 0x7695C2)
// ---------------------------------------------------------------------------------------------------------------------------------
namespace
{
GiantBirdAIUpdate &bird(AIState &s)
{
	return static_cast<GiantBirdAIUpdate &>(s.ai());
}

// the idle state of the swoop-then-idle machine (RW 0x741812(machine, 1), vtable of AIIdleState): it does not look for targets; its onEnter (RW 0x748A4F) draws the
// initial sleep offset from the logic generator (AIStates.cpp line 0x94A), as the hunt machine's idle state does
class SwoopIdleState : public AIState
{
public:
	explicit SwoopIdleState(AIStateMachine &m)
		: AIState(m, "AIIdleState")
	{
	}
	StateReturnType onEnter() override
	{
		owner().logic().random().getValue(0, 2 * (int)LOGICFRAMES_PER_SECOND, "AIStates.cpp", 0x94A);
		return STATE_CONTINUE;
	}
	StateReturnType update() override { return STATE_CONTINUE; }
	bool isIdle() const override { return true; }
	void xfer(Xfer &x) override { xferNoFields(x); } // lane SAVE-4c: nothing kept (the draw is made at every entry)
};

// GiantBirdNormalFlightState (ctor RW 0x7691CC (machine, + 0x20, + 0x21), vtable RW 0xC2D988; enter RW 0x76AD1D, update RW 0x76AE06, exit RW 0x76AED0)
class GiantBirdNormalFlightState : public AIState
{
public:
	GiantBirdNormalFlightState(AIStateMachine &m, bool flag20, bool aboveGround, const char *name = "GiantBirdNormalFlightState")
		: AIState(m, name)
		, m_flag20(flag20)
		, m_aboveGround(aboveGround)
	{
	}
	StateReturnType onEnter() override
	{
		Object &o = owner();
		clearCondition(o, names().aboutToHit);
		GiantBirdAIUpdate &ai = bird(*this);
		if (o.isEffectivelyDead())
		{
			return STATE_FAILURE;
		}
		ai.chooseLocomotorSet(LOCOMOTORSET_NORMAL); // vslot 0x238
		Locomotor *loco = ai.curLocomotor();
		if (!loco)
		{
			return STATE_FAILURE;
		}
		ai.setPathHeight(loco->preferredHeight());
		Coord3D goal = machine().goalPosition();
		if (m_aboveGround && !o.testStatus((unsigned)names().movingToDismount))
		{
			const float h = ai.pathHeight();
			goal.z = SimMath::fstpDword(SimMath::pc24AddW((double)AerialPathfinder::flightGroundHeight(o.logic(), goal.x, goal.y), (double)h)); // RW 0x777ED7
		}
		ai.buildPath(goal, AerialPathfinder::MASK_A, nullptr, true);
		return ai.spline().valid() ? STATE_CONTINUE : STATE_FAILURE;
	}
	StateReturnType update() override
	{
		Object &o = owner();
		if (o.isEffectivelyDead())
		{
			return STATE_FAILURE;
		}
		GiantBirdAIUpdate &ai = bird(*this);
		ai.steer(ai.pathHeight(), nullptr, true);
		if (!ai.spline().valid())
		{
			return STATE_FAILURE;
		}
		return arrived(ai) ? STATE_SUCCESS : STATE_CONTINUE;
	}
	void onExit(StateExitType status) override
	{
		(void)status;
		if (ai().isDestroying())
		{
			return;
		}
		Object &o = owner();
		clearCondition(o, names().climbing);
		clearCondition(o, names().freefall);
		reserveGoal(o);
		o.setStatus((unsigned)names().leavingFactory, false);
		o.setStatus((unsigned)names().movingToDismount, false);
		if (Locomotor *loco = ai().curLocomotor())
		{
			loco->setTemporarySpeedCap(-1.0f); // RW 0x76AF45: locomotor + 0x2C = -1 (RW 0xBD19DC)
		}
	}
	void crc(StateHasher &h) const override
	{
		h.addBool(m_flag20);
		h.addBool(m_aboveGround);
	}

	// the end test of RW 0x76AE06 (also the follow-through's): reached, or within one speed of the anchor (x87 squared distance under speed^2): the object
	// is put on the anchor (RW 0x70C201)
	static bool arrived(GiantBirdAIUpdate &ai)
	{
		Object &o = *ai.getObject();
		const float speed = ai.flight().speed;
		const Coord3D anchor = ai.flight().anchor;
		const double d2 = distanceSquared3(*o.getPosition(), anchor);
		if (!ai.reached() && !(d2 < SimMath::pc24MulW((double)speed, (double)speed)))
		{
			return false;
		}
		o.setPosition(&anchor);
		return true;
	}
	// RW 0x68B3BD(position, 1): the pathfinder's goal reservation at the object's position
	static void reserveGoal(Object &o)
	{
		AIWorld *world = o.logic().aiWorld();
		if (world && world->mapReady())
		{
			Coord3D p = *o.getPosition();
			world->pathfinder().updateGoal(world->adapterFor(o), &p, LAYER_GROUND);
		}
	}

private:
	bool m_flag20;      // + 0x20 (read by nothing ported)
	bool m_aboveGround; // + 0x21: the goal is raised to the flight ground height + the path height
public:
	// lane SAVE-4c (obfme:AIWorld, OpenBFME layout: S-4220): the constructor's two flags, saved and checked (a machine of the same class has them)
	void xfer(Xfer &x) override
	{
		XferVersion v{ 1, 1 };
		x.xferVersion(v);
		xferFlags(x);
	}

protected:
	void xferFlags(Xfer &x)
	{
		bool flag20 = x.isLoading() ? false : m_flag20, above = x.isLoading() ? false : m_aboveGround;
		x.xferBool(&flag20);
		x.xferBool(&above);
		if (x.isLoading() && (flag20 != m_flag20 || above != m_aboveGround))
		{
			throw XferException("giant bird: a flight state saved with other constructor flags");
		}
	}
};

// AIGiantBirdSwoopState (ctor RW 0x7691FD (machine, + 0x20 target mode, + 0x30 level), vtable RW 0xC2D9E8; enter RW 0x76DB0E, update RW 0x76DF1D, the target
// refresh RW 0x76E154, exit RW 0x76AFA6). BFME2 decomp AIGiantBirdSwoopStateOnEnter.cpp (tier B same-shape for all three bodies)
class AIGiantBirdSwoopState : public AIState
{
public:
	AIGiantBirdSwoopState(AIStateMachine &m, bool targetObject, bool level)
		: AIState(m, "AIGiantBirdSwoopState")
		, m_targetObject(targetObject)
		, m_level(level)
	{
	}
	StateReturnType onEnter() override
	{
		Object &o = owner();
		GiantBirdAIUpdate &ai = bird(*this);
		const BirdNames &n = names();
		if (o.isEffectivelyDead())
		{
			return STATE_FAILURE;
		}
		ai.noteSwoop();
		ai.flight().flags &= ~0x38u;
		ai.setVictimId(INVALID_ID);
		m_scale = 0.4f; // RW 0xBE0DE4
		ObjectWeapons *w = o.getWeapons();
		if (!m_targetObject)
		{
			if (w)
			{
				w->chooseBestWeaponForTarget(nullptr, PREFER_MOST_DAMAGE, CMD_FROM_AI);
			}
			m_pos = machine().goalPosition();
			ai.setVictimId(INVALID_ID);
			m_pos.z = SimMath::addf32(m_pos.z, o.logic().partition().geometryOf(o).centerZ); // RW 0xB4E370 on a copy of the geometry
		}
		else
		{
			Object *victim = machine().goalObject();
			if (victim && isKind(*victim, n.producedAtHelipad))
			{
				victim = nullptr;
				machine().setGoalObject(INVALID_ID);
			}
			if (!victim)
			{
				if (ai.swoopHordeId() != INVALID_ID)
				{
					victim = o.logic().findObjectByID(ai.swoopHordeId());
					m_scale = 1.0f;
				}
				if (!victim)
				{
					return STATE_FAILURE;
				}
			}
			if (victim->isEffectivelyDead())
			{
				return STATE_FAILURE;
			}
			if (isKind(*victim, n.horde))
			{
				ai.setSwoopHordeId(victim->getID());
				victim = hordeMemberFor(*victim, o);
			}
			else if (Object *container = victim->getContainedBy())
			{
				ContainModuleInterface *c = container->getContain();
				if (c && c->getHordeContainInterface())
				{
					ai.setSwoopHordeId(container->getID());
				}
			}
			else
			{
				ai.setSwoopHordeId(INVALID_ID);
			}
			if (!victim || victim->isEffectivelyDead())
			{
				return STATE_FAILURE;
			}
			ai.setVictimId(victim->getID());
			if (!w)
			{
				return STATE_FAILURE;
			}
			w->chooseBestWeaponForTarget(victim, PREFER_GRAB_OVER_DAMAGE, CMD_FROM_AI); // RW 0x76DCB5 (victim, 2, 2)
			Weapon *weapon = w->currentWeapon();
			if (!weapon)
			{
				return STATE_FAILURE;
			}
			const WeaponTemplate &wt = *weapon->getTemplate();
			unsigned &flags = ai.flight().flags;
			flags &= ~0x10u;
			const bool grabs = wt.m_hasGrabNugget && o.getContain() && o.getContain()->isValidContainerFor(*victim, true, false); // RW 0x6CA3DA
			if (grabs && (isKind(*victim, n.grabAndDrop) || victim->testStatus((unsigned)n.grabAndDropStatus)))
			{
				flags |= 0x110u;
			}
			else if (isKind(*victim, n.machine))
			{
				flags = (flags & ~0x100u) | 0x20u;
			}
			else if (wt.m_clipSize > 1 && !victim->testStatus((unsigned)n.airborneTarget))
			{
				flags |= 8u;
			}
			ai.setCurrentVictim(victim);
			ai.setDuelMode(2);
			if (GiantBirdAIUpdate *other = birdOf(victim)) // the victim's AI vslot 0x188: a GiantBirdAIUpdate only
			{
				if (other->currentVictim() == &o)
				{
					if (o.logic().random().getValue(11, 23, kGiantBirdCpp, 0x23F) <= 17)
					{
						ai.setDuelMode(0);
						other->setDuelMode(1);
					}
					else
					{
						ai.setDuelMode(1);
						other->setDuelMode(0);
					}
				}
			}
			if (!preferredTargetBonePoint(wt, *victim, m_pos) || !(m_pos.z > o.logic().getGroundHeight(m_pos.x, m_pos.y)))
			{
				m_pos = contactPointOn(ai, *victim, 0x254);
			}
			if (ai.duelMode() != 2)
			{
				if (Locomotor *loco = ai.curLocomotor())
				{
					m_pos.z = SimMath::addf32(o.logic().getGroundHeight(m_pos.x, m_pos.y), loco->preferredHeight());
					machine().setGoalPosition(m_pos);
				}
			}
		}
		if (m_level)
		{
			ai.chooseLocomotorSet(LOCOMOTORSET_PANIC);
		}
		else if (ai.duelMode() == 0)
		{
			ai.chooseLocomotorSet(LOCOMOTORSET_WANDER);
		}
		else
		{
			ai.chooseLocomotorSet(LOCOMOTORSET_SUPERSONIC);
		}
		const unsigned flags = ai.flight().flags;
		const unsigned mask = ((flags >> 2) & 1u) || ((flags >> 3) & 1u) ? AerialPathfinder::MASK_A_LOWER_END : (m_level ? AerialPathfinder::MASK_DIRECT : AerialPathfinder::MASK_A);
		ai.buildPath(m_pos, mask, nullptr, false);
		return ai.spline().valid() ? STATE_CONTINUE : STATE_FAILURE;
	}
	StateReturnType update() override
	{
		Object &o = owner();
		if (o.isEffectivelyDead())
		{
			return STATE_FAILURE;
		}
		GiantBirdAIUpdate &ai = bird(*this);
		Object *victim = o.logic().findObjectByID(ai.victimId());
		if (!victim || victim->testStatus((unsigned)names().unattackable) || victim->isEffectivelyDead())
		{
			return STATE_SUCCESS;
		}
		// the speed shrinks by the swoop's scale, which decays (x 0.8 a frame)
		const float speed = SimMath::mulf32(SimMath::subf32(1.0f, m_scale), ai.flight().speed);
		m_scale = SimMath::mulf32(m_scale, 0.8f);
		ai.flight().speed = speed;
		refreshTarget(false);
		if (m_level)
		{
			if (ai.checkAhead(-5.0f, true) == 1) // RW 0xC2E430
			{
				return STATE_SUCCESS;
			}
		}
		else
		{
			// RW 0x76DFE9 .. 0x76E03B: the victim is still when it is not mobile (RW 0x690E97) or its AI's locomotor has no speed for its condition (RW 0x6624C9:
			// RW 0x5E3F49, 0 without a locomotor; x87 0 >= it); a WEBBED (KindOf 0x9B) flyer does not steer at a still victim
			bool still = true;
			if (GarrisonRulesIsMobile(*victim))
			{
				AIUpdateInterface *vai = victim->getAIUpdateInterface();
				still = vai != nullptr && !(vai->curLocomotorSpeed() > 0.0f);
			}
			static const int kWebbed = kindBit("WEBBED");
			if (!isKind(o, kWebbed) || !still)
			{
				ai.steer(5.0f, &m_pos, false);
			}
		}
		if (!ai.spline().valid())
		{
			return STATE_FAILURE;
		}
		const float circle = CombatQueries::boundingCircleRadius(o);
		const Coord3D &p = *o.getPosition();
		const float dx = SimMath::subf32(m_pos.x, p.x), dy = SimMath::subf32(m_pos.y, p.y), dz = SimMath::subf32(m_pos.z, p.z);
		const float d2 = SimMath::addf32(SimMath::addf32(SimMath::mulf32(dx, dx), SimMath::mulf32(dy, dy)), SimMath::mulf32(dz, dz));
		if (d2 < SimMath::mulf32(SimMath::mulf32(circle, circle), 0.5f))
		{
			refreshTarget(true);
		}
		return GiantBirdNormalFlightState::arrived(ai) ? STATE_SUCCESS : STATE_CONTINUE;
	}
	void onExit(StateExitType status) override
	{
		(void)status;
		if (ai().isDestroying())
		{
			return;
		}
		Object &o = owner();
		clearCondition(o, names().moving);
		clearCondition(o, names().climbing);
		clearCondition(o, names().freefall);
		ai().chooseLocomotorSet(LOCOMOTORSET_NORMAL);
		ai().setCurrentVictim(nullptr); // RW 0x6682B1(0)
		GiantBirdNormalFlightState::reserveGoal(o);
	}
	void crc(StateHasher &h) const override
	{
		h.addFloat(m_pos.x);
		h.addFloat(m_pos.y);
		h.addFloat(m_pos.z);
		h.addFloat(m_scale);
	}

private:
	static bool GarrisonRulesIsMobile(const Object &o)
	{
		static const int kImmobile = names().immobile;
		return !isKind(o, kImmobile); // RW 0x690E97 (its IMMOBILE test: stop S-1920)
	}
	// RW 0x76E154 (BFME2 rva0036C438): ABOUT_TO_HIT goes; the victim's aim point (bone, else the contact point at line 0x294); within 4 speeds of the
	// victim ABOUT_TO_HIT is set; when the aim point moved over 10 (or forced) and the victim is within 8 speeds or moved more than that, the swoop's point
	// follows it (at the preferred height over the ground in duel mode 0) and the path is rebuilt
	void refreshTarget(bool force)
	{
		Object &o = owner();
		GiantBirdAIUpdate &ai = bird(*this);
		clearCondition(o, names().aboutToHit);
		Object *victim = o.logic().findObjectByID(ai.victimId());
		if (!victim)
		{
			return;
		}
		Coord3D pos;
		ObjectWeapons *w = o.getWeapons();
		const Weapon *weapon = w ? w->currentWeapon() : nullptr;
		if (!weapon || !preferredTargetBonePoint(*weapon->getTemplate(), *victim, pos) || !(pos.z > o.logic().getGroundHeight(pos.x, pos.y)))
		{
			pos = contactPointOn(ai, *victim, 0x294);
		}
		const float moved = (float)SimMath::length3d(SimMath::subf32(pos.x, m_pos.x), SimMath::subf32(pos.y, m_pos.y), SimMath::subf32(pos.z, m_pos.z));
		const Coord3D &op = *o.getPosition();
		const float range = (float)SimMath::length3d(SimMath::subf32(pos.x, op.x), SimMath::subf32(pos.y, op.y), SimMath::subf32(pos.z, op.z));
		const float speed = ai.flight().speed;
		if (SimMath::mulf32(speed, 4.0f) > range)
		{
			setCondition(o, names().aboutToHit);
		}
		if (moved > 10.0f || force)
		{
			const float near8 = SimMath::mulf32(speed, 8.0f);
			if (near8 > range || moved > near8 || force)
			{
				m_pos = pos;
				if (ai.duelMode() == 0)
				{
					if (Locomotor *loco = ai.curLocomotor())
					{
						m_pos.z = SimMath::addf32(o.logic().getGroundHeight(m_pos.x, m_pos.y), loco->preferredHeight());
						machine().setGoalPosition(m_pos);
					}
				}
				const unsigned flags = ai.flight().flags;
				ai.buildPath(m_pos, ((flags >> 2) & 1u) || ((flags >> 3) & 1u) ? AerialPathfinder::MASK_A_LOWER_END : AerialPathfinder::MASK_A, nullptr, false);
			}
		}
	}

	bool m_targetObject;                   // + 0x20
	Coord3D m_pos{ 0.0f, 0.0f, 0.0f };     // + 0x24
	bool m_level;                          // + 0x30
	float m_scale = 1.0f;                  // + 0x38
public:
	void xfer(Xfer &x) override // lane SAVE-4c: the constructor's flags checked, the aim point and the scale
	{
		XferVersion v{ 1, 1 };
		x.xferVersion(v);
		bool target = x.isLoading() ? false : m_targetObject, level = x.isLoading() ? false : m_level;
		x.xferBool(&target);
		x.xferBool(&level);
		if (x.isLoading() && (target != m_targetObject || level != m_level))
		{
			throw XferException("giant bird: a swoop state saved with other constructor flags");
		}
		AIXfer::coord(x, m_pos);
		x.xferReal(&m_scale);
	}
};

// AIGiantBirdAttack (ctor RW 0x76923F (machine, + 0x20 target mode), vtable RW 0xC2DA48; enter RW 0x76B668, update RW 0x76BA8B, exit RW 0x76B033). BFME2 decomp
// AIGiantBirdAttackStateOnEnter.cpp (tier B same-shape)
class AIGiantBirdAttackState : public AIState
{
public:
	AIGiantBirdAttackState(AIStateMachine &m, bool targetObject)
		: AIState(m, "AIGiantBirdAttack")
		, m_targetObject(targetObject)
	{
	}
	StateReturnType onEnter() override
	{
		Object &o = owner();
		GiantBirdAIUpdate &ai = bird(*this);
		const BirdNames &n = names();
		ai.setAttacking(true);
		if (o.isEffectivelyDead())
		{
			return STATE_FAILURE;
		}
		if ((ai.flight().flags >> 3) & 1u)
		{
			return STATE_CONTINUE; // the clip attack: the update fires
		}
		ObjectWeapons *w = o.getWeapons();
		if (!m_targetObject)
		{
			if (w && w->currentWeapon())
			{
				w->setFiringConditionForCurrentWeapon(); // RW 0x69036C
				Coord3D at = *o.getPosition();
				w->fireCurrentWeapon(nullptr, &at); // RW 0x6920AD
				ai.noteStrike();
			}
			return STATE_SUCCESS;
		}
		Object *victim = o.logic().findObjectByID(ai.victimId());
		if (!victim || victim->isEffectivelyDead())
		{
			return STATE_SUCCESS;
		}
		const Coord3D pos = contactPointOn(ai, *victim, 0x37B);
		float range = SimMath::mulf32(ai.flight().speed, 3.0f);
		if (range < 8.5f) // RW 0xC2E190
		{
			range = 8.5f;
		}
		if (distanceSquared3(*o.getPosition(), pos) > (double)SimMath::mulf32(range, range))
		{
			return STATE_FAILURE; // the swoop missed: the follow-through fails and the swoop starts again
		}
		if (!w || !w->currentWeapon())
		{
			return STATE_FAILURE;
		}
		if ((ai.flight().flags >> 4) & 1u)
		{
			// the grab (RW 0x76B79E .. 0x76B8B3): a victim held by a horde leaves it (its contain's HordeContainInterface, contain slot 0x7C, slot 0x38: RW 0x87393E
			// removeMemberFromHorde, here the horde contain's removal, which frees the slot and ends an empty horde); the owner's contain takes it unless it holds
			// anything that is not RIDER_IS_PILOT (status 0x28) (contain slot 0x9C), the rider (+ 0x588) is its id. Held: the grab weapon's FireFX at the victim
			// (a client doFXPos, RW 0x5E21D8, when the template has a GrabNugget), flag 6 (+ 0x57C |= 0x40), success. Not held: the weapon fired at its position,
			// failure
			if (Object *container = victim->getContainedBy())
			{
				ContainModuleInterface *vc = container->getContain();
				if (vc && vc->getHordeContainInterface())
				{
					vc->removeFromContain(victim);
				}
			}
			ContainModuleInterface *contain = o.getContain();
			if (contain)
			{
				static const int pilot = CombatNames::status("RIDER_IS_PILOT");
				bool other = false;
				if (const ContainModuleInterface::ContainedItemsList *items = contain->getContainedItemsList())
				{
					for (const Object *item : *items)
					{
						if (!item->testStatus((unsigned)pilot))
						{
							other = true;
							break;
						}
					}
				}
				if (!other && contain->addToContain(victim))
				{
					ai.setRiderId(victim->getID());
				}
			}
			if (victim->getContainedBy() == &o)
			{
				const WeaponTemplate &wt = *w->currentWeapon()->getTemplate();
				if (wt.m_hasGrabNugget && !wt.m_fireFXs[0].empty())
				{
					FXEvent e = FXEventLog::objectEvent(FXEvent::POSITION_FX, "GiantBirdGrabFX", o.logic().getFrame(), wt.m_fireFXs[0], o);
					e.position = *victim->getPosition();
					o.logic().fxEvents().emit(e);
				}
				ai.flight().flags |= 0x40u;
				ai.noteStrike();
				return STATE_SUCCESS;
			}
			w->setFiringConditionForCurrentWeapon();
			Coord3D at = *victim->getPosition();
			w->fireCurrentWeapon(nullptr, &at); // RW 0x6920AD
			ai.noteStrike();
			return STATE_FAILURE;
		}
		w->setFiringConditionForCurrentWeapon();
		w->fireCurrentWeapon(victim, nullptr); // RW 0x69200A(victim, its id)
		ai.noteStrike();
		if (victim->isEffectivelyDead() && ((ai.flight().flags >> 5) & 1u))
		{
			// the MACHINE victim's CreateObjectDie object made and carried (RW 0x76B8E9 ..) is not ported (S-4040)
			ai.flight().flags &= ~0x20u;
			o.logic().aiWorld()->noteStop(birdStop());
		}
		else if (GiantBirdAIUpdate *other = birdOf(victim))
		{
			victim->setSpecialModelConditionState(n.hitReaction, 5);
			victim->setSpecialModelConditionState(n.stunned, 5);
			if (other->currentVictim() == &o && ai.duelMode() == 1 && other->duelMode() == 0)
			{
				if (ObjectWeapons *vw = victim->getWeapons())
				{
					if (vw->currentWeapon())
					{
						vw->fireCurrentWeapon(&o, nullptr); // RW 0x69200A
					}
				}
				victim->setSpecialModelConditionState(n.specialDamaged, 5);
			}
		}
		return STATE_SUCCESS;
	}
	StateReturnType update() override
	{
		Object &o = owner();
		if (o.isEffectivelyDead())
		{
			return STATE_FAILURE;
		}
		GiantBirdAIUpdate &ai = bird(*this);
		if (!((ai.flight().flags >> 3) & 1u))
		{
			return STATE_FAILURE;
		}
		Object *victim = o.logic().findObjectByID(ai.victimId());
		if (!victim || victim->isEffectivelyDead())
		{
			return STATE_SUCCESS;
		}
		ObjectWeapons *w = o.getWeapons();
		if (!w || !w->currentWeapon())
		{
			return STATE_FAILURE;
		}
		const int status = w->currentStatus(); // RW 0x6CDCE7
		if (status == WEAPON_READY_TO_FIRE)
		{
			w->setFiringConditionForCurrentWeapon();
			w->fireCurrentWeapon(victim, nullptr);
			ai.noteStrike();
			setCondition(o, names().aboutToHit);
			return STATE_CONTINUE;
		}
		return status == WEAPON_RELOADING_CLIP ? STATE_SUCCESS : STATE_CONTINUE;
	}
	void onExit(StateExitType status) override
	{
		(void)status;
		if (ai().isDestroying())
		{
			return;
		}
		Object &o = owner();
		clearCondition(o, names().aboutToHit);
		GiantBirdAIUpdate &ai = bird(*this);
		ai.flight().flags &= ~0x20u; // RW 0x76B0A2(0): flag 5
		if (ObjectWeapons *w = o.getWeapons())
		{
			w->reloadCurrentWeaponAmmo(); // RW 0x6CEE4C
		}
		GiantBirdNormalFlightState::reserveGoal(o);
		ai.setAttacking(false);
	}
	void crc(StateHasher &h) const override { h.addBool(m_targetObject); }

private:
	bool m_targetObject; // + 0x20
public:
	void xfer(Xfer &x) override // lane SAVE-4c: the constructor's flag, checked
	{
		XferVersion v{ 1, 1 };
		x.xferVersion(v);
		bool target = x.isLoading() ? false : m_targetObject;
		x.xferBool(&target);
		if (x.isLoading() && target != m_targetObject)
		{
			throw XferException("giant bird: an attack state saved with another constructor flag");
		}
	}
};

// AIGiantBirdFollowThruState (ctor RW 0x769269 (machine, + 0x20 the long follow-through), vtable RW 0xC2DAA0; enter RW 0x76E3CB, the point RW 0x76E5C1, its
// test RW 0x76E757, update RW 0x76BB41, exit RW 0x76B0B0)
class AIGiantBirdFollowThruState : public AIState
{
public:
	AIGiantBirdFollowThruState(AIStateMachine &m, bool longer)
		: AIState(m, "AIGiantBirdFollowThruState")
		, m_longer(longer)
	{
	}
	StateReturnType onEnter() override
	{
		m_frames = 0;
		Object &o = owner();
		clearCondition(o, names().aboutToHit);
		GiantBirdAIUpdate &ai = bird(*this);
		ai.setAttacking(true);
		if (o.isEffectivelyDead())
		{
			return STATE_FAILURE;
		}
		ai.chooseLocomotorSet(LOCOMOTORSET_NORMAL);
		Locomotor *loco = ai.curLocomotor();
		if (!loco)
		{
			return STATE_FAILURE;
		}
		bool shortened = false;
		Coord3D goal = throughPoint(shortened);
		const float ph = loco->preferredHeight();
		float desired = SimMath::addf32(goal.z, ph);
		const float ground = o.logic().getGroundHeight(goal.x, goal.y);
		const float minimum = SimMath::addf32(SimMath::mulf32(ph, 1.25f), ground); // RW 0xC2E434
		if (minimum < desired)
		{
			desired = SimMath::addf32(SimMath::mulf32(ph, 0.25f), goal.z); // RW 0xBD1904
			if (desired <= minimum)
			{
				desired = minimum;
			}
		}
		goal.z = desired;
		if (ai.duelMode() != 2)
		{
			const Coord3D &p = *o.getPosition();
			goal.z = SimMath::addf32(ground, ph);
			goal.x = SimMath::mulf32(SimMath::addf32(p.x, goal.x), 0.5f);
			goal.y = SimMath::mulf32(SimMath::addf32(p.y, goal.y), 0.5f);
		}
		machine().setGoalPosition(goal); // RW 0x8DB810(goal, FLT_MAX)
		const unsigned mask = (!shortened && !((ai.flight().flags >> 3) & 1u)) ? AerialPathfinder::MASK_A : AerialPathfinder::MASK_A_LOWER_START;
		ai.buildPath(goal, mask, nullptr, true);
		ai.flight().flags &= ~0x08u; // RW 0x76E5B3(0): flag 3
		if (!ai.spline().valid())
		{
			return STATE_FAILURE;
		}
		// RW 0x76E536 .. 0x76E567: holding a victim (flag 6) takes its FREEFALL condition
		if (((ai.flight().flags >> 6) & 1u) && ai.riderId() != INVALID_ID)
		{
			if (Object *rider = o.logic().findObjectByID(ai.riderId()))
			{
				setCondition(*rider, names().freefall);
			}
		}
		return STATE_CONTINUE;
	}
	StateReturnType update() override
	{
		Object &o = owner();
		if (o.isEffectivelyDead())
		{
			return STATE_FAILURE;
		}
		clearCondition(o, names().aboutToHit);
		if (++m_frames >= 0x29)
		{
			return STATE_FAILURE; // RW 0x76BB6A: 41 frames at most
		}
		GiantBirdAIUpdate &ai = bird(*this);
		ai.steer(5.0f, nullptr, true); // RW 0xBDAE58
		if (!ai.spline().valid())
		{
			return STATE_FAILURE;
		}
		if (!GiantBirdNormalFlightState::arrived(ai))
		{
			return STATE_CONTINUE;
		}
		Object *victim = o.logic().findObjectByID(ai.victimId());
		if (!victim || victim->isEffectivelyDead())
		{
			Object *horde = o.logic().findObjectByID(ai.swoopHordeId());
			machine().setGoalObject(INVALID_ID); // machine vslot 0x38(0)
			if (!horde || horde->isEffectivelyDead())
			{
				return STATE_SUCCESS;
			}
		}
		return STATE_FAILURE; // the victim (or its horde) lives: the failure id swoops again
	}
	void onExit(StateExitType status) override
	{
		(void)status;
		if (ai().isDestroying())
		{
			return;
		}
		Object &o = owner();
		clearCondition(o, names().moving);
		clearCondition(o, names().climbing);
		clearCondition(o, names().freefall);
		clearCondition(o, names().aboutToHit);
		GiantBirdNormalFlightState::reserveGoal(o);
		GiantBirdAIUpdate &ai = bird(*this);
		ai.setAttacking(false);
		ai.setDuelMode(2);
	}
	void crc(StateHasher &h) const override
	{
		h.addBool(m_longer);
		h.addI32(m_frames);
	}

private:
	// RW 0x76E5C1 (&out, &shortened, longer): along the transform's x axis from one check step up to FollowThroughDistance (twice that when `longer`) in
	// steps of FollowThroughCheckStep, each point at z + step * FollowThroughGradient while it passes RW 0x76E757; the last passing point (the object's
	// position when none); shortened when the distance reached is under half of the full one
	Coord3D throughPoint(bool &shortened)
	{
		Object &o = owner();
		GiantBirdAIUpdate &ai = bird(*this);
		const Coord3D p = *o.getPosition();
		Coord3D best = p;
		float reached = 0.0f;
		const GiantBirdAIUpdateData &d = ai.data();
		const float *b = o.getBasis();
		const float fx = b[0], fy = b[3], fz = b[6];
		float full = d.followThroughDistance;
		if (m_longer)
		{
			full = SimMath::mulf32(full, 2.0f); // RW 0xBD889C
		}
		for (float s = d.followThroughCheckStep; s <= full; s = SimMath::addf32(s, d.followThroughCheckStep))
		{
			Coord3D q{ SimMath::addf32(p.x, SimMath::mulf32(s, fx)), SimMath::addf32(p.y, SimMath::mulf32(s, fy)), SimMath::addf32(p.z, SimMath::mulf32(fz, s)) };
			if (!pointClear(q, SimMath::addf32(SimMath::mulf32(s, d.followThroughGradient), p.z)))
			{
				break;
			}
			reached = s;
			best = q;
		}
		shortened = !(SimMath::mulf32(full, 0.5f) <= reached);
		return best;
	}
	// RW 0x76E757 (point, height): the terrain is not above `height`, and the closest object within the flyer's bounding sphere radius that is STRUCTURE and
	// MONSTER and MACHINE at once (the KindOf filter RW 0x445139 with the mask RW 0x66158D(0, 7, 10, 11): isKindOfMulti RW 0x70B8C7 wants every bit) stays
	// below it (its geometry's maximum height above its position, RW 0xAD1920, plus the terrain)
	bool pointClear(const Coord3D &q, float height)
	{
		Object &o = owner();
		const float ground = o.logic().getGroundHeight(q.x, q.y);
		if (height < ground)
		{
			return false;
		}
		const BirdNames &n = names();
		auto all = [&n](Object &x) { return isKind(x, n.structure) && isKind(x, n.monster) && isKind(x, n.machine); };
		PartitionFilterFn<decltype(all)> filter(all);
		Object *near = o.logic().partition().getClosestObject(q, CombatQueries::boundingSphereRadius(o), FROM_CENTER_2D, { &filter });
		if (!near)
		{
			return true;
		}
		const float top = ObjectGeometry::maxHeightAbovePosition(near->geometryShapes());
		return SimMath::addf32(top, ground) <= height;
	}

	bool m_longer; // + 0x20
	int m_frames = 0; // + 0x24
public:
	// lane SAVE-4c: AIGiantBirdFollowThruState::xfer RW 0x769297 is (1, 2): a bool and an int (these two; the bool is the constructor's, checked)
	void xfer(Xfer &x) override
	{
		XferVersion v{ 1, 1 };
		x.xferVersion(v);
		bool longer = x.isLoading() ? false : m_longer;
		x.xferBool(&longer);
		if (x.isLoading() && longer != m_longer)
		{
			throw XferException("giant bird: a follow-through state saved with another constructor flag");
		}
		x.xferInt(&m_frames);
		if (x.isLoading())
		{
			AIXfer::inRange(m_frames, 0, 0x7FFFFFFF, "giant bird: the follow-through frames");
		}
	}
};

// GiantBirdAttackMoveToState (ctor RW 0x769451, vtable RW 0xC2DBD0; enter RW 0x76B2A6 -> 0x76AD1D, exit RW 0x76B2E5, update RW 0x76B301): a NormalFlight(0, 1)
// with an AIAttackSwoopThenIdleStateMachine (RW 0x769335: 10 Swoop(1, 0) -> 0x3F4 Attack(1) -> 0x3F5 FollowThru(0) -> 0 the idle state that does not scan)
class GiantBirdAttackMoveToState : public GiantBirdNormalFlightState
{
public:
	explicit GiantBirdAttackMoveToState(AIStateMachine &m)
		: GiantBirdNormalFlightState(m, false, true, "GiantBirdAttackMoveToState")
	{
	}
	StateReturnType onEnter() override
	{
		AIUpdateInterface &a = ai();
		ensureSubMachine();
		m_sub->resetToDefaultState();
		m_sub->setState(AI_IDLE);
		m_source = a.lastCommandSource();
		m_retries = 5;
		m_nextMoodCheck = 0;
		return GiantBirdNormalFlightState::onEnter();
	}
	StateReturnType update() override
	{
		Object &o = owner();
		GiantBirdAIUpdate &ai = bird(*this);
		ensureSubMachine();
		bool force = false, repath = false;
		if (!subIdle())
		{
			ai.setLocomotorGoalNone(); // vslot 0x220
			clearCondition(o, names().moving);
			clearCondition(o, names().backingUp);
			m_sub->updateStateMachine();
			if (!subIdle())
			{
				return STATE_CONTINUE;
			}
			force = true;
			repath = true;
			ai.setCommandSource(m_source); // AI + 0x48
		}
		if (subIdle())
		{
			// no crates in RotWK (RW 0x66810B answers none: S-4040); the mood scan RW 0x66844A(!force, 0): its own cadence (AI + 0x21C) when called by the AI
			// (lane TARGET-1; m_nextMoodCheck, the earlier stand-in clock, is kept in the snapshot but no longer read)
			Object *target = ai.getNextMoodTarget(!force, false);
			if (target)
			{
				ai.mover().endingMove(); // RW 0x6627CB
				m_sub->setGoalObject(target->getID());
				m_sub->setState(10);
				ai.setCommandSource(CMD_FROM_AI);
				return STATE_CONTINUE;
			}
		}
		if (repath)
		{
			GiantBirdNormalFlightState::onEnter();
		}
		return GiantBirdNormalFlightState::update();
	}
	void onExit(StateExitType status) override
	{
		if (m_sub && !ai().isDestroying())
		{
			m_sub->setState(AI_IDLE);
		}
		GiantBirdNormalFlightState::onExit(status);
	}
	void crc(StateHasher &h) const override
	{
		GiantBirdNormalFlightState::crc(h);
		h.addI32((int)m_source);
		h.addI32(m_retries);
		h.addU32(m_nextMoodCheck);
		h.addBool(m_sub != nullptr);
		if (m_sub)
		{
			m_sub->crc(h);
		}
	}

private:
	bool subIdle() const
	{
		const AIState *s = m_sub ? m_sub->currentState() : nullptr;
		return !s || s->isIdle();
	}
	void ensureSubMachine()
	{
		if (!m_sub)
		{
			m_sub = bird(*this).makeAttackThenIdleMachine(10); // RW 0x769451 -> RW 0x769335
		}
	}

	std::unique_ptr<AIStateMachine> m_sub; // + 0x28
	CommandSourceType m_source = CMD_FROM_AI; // + 0x24
	int m_retries = 5;                        // + 0x2C
	unsigned m_nextMoodCheck = 0;
public:
	// lane SAVE-4c: GiantBirdAttackMoveToState::xfer RW 0x769521 is (1, 1): an int, the sub machine, the command source; OpenBFME layout (S-4220)
	void xfer(Xfer &x) override
	{
		XferVersion v{ 1, 1 };
		x.xferVersion(v);
		xferFlags(x);
		AIXfer::enumIn(x, m_source, CMD_FROM_PLAYER, CMD_FROM_AI, "giant bird: the attack move's command source");
		x.xferInt(&m_retries);
		AIXfer::uns(x, m_nextMoodCheck);
		bool has = m_sub != nullptr;
		x.xferBool(&has);
		if (!x.isLoading())
		{
			if (m_sub)
			{
				m_sub->xfer(x);
			}
			return;
		}
		AIXfer::inRange(m_source, CMD_FROM_PLAYER, CMD_FROM_AI, "giant bird: the attack move's command source");
		AIXfer::inRange(m_retries, 0, 5, "giant bird: the attack move's retries");
		std::unique_ptr<AIStateMachine> loaded; // made (RW 0x769451 -> RW 0x769335) and loaded complete, then attached
		if (has)
		{
			loaded = bird(*this).makeAttackThenIdleMachine(10);
			if (!loaded)
			{
				throw XferException("giant bird: the attack move's swoop machine cannot be made");
			}
			loaded->xfer(x);
		}
		m_sub = std::move(loaded);
	}
};

// ---- lane STEALTHFLY1: the follow states ---------------------------------------------------------------------------------------------------------------
// The next point of a follow state (RW 0x76A320 GiantBirdFollowPathState / RW 0x76A496 GiantBirdFollowWaypointPathState, the same body with the index at
// + 0x58 / + 0x20; BFME2 decomp GiantBirdFollowPathStates.cpp followToNextPointNow): the machine's goal path point `index` (RW 0x74898B), the index advanced;
// past the end the path restarts at its first point when the bird loops (+ 0x614), else false. The point after it is the look-ahead (AI flags bit 7 set, else
// cleared); both are put at the terrain height (TheTerrainLogic vslot 0x18, not the flight ground height) + the path height (+ 0x604), the x87 sum stored as
// a float, and the path is built to the point (RW 0x769EA5 with mask RW 0xDE7908 = MASK_A, the look-ahead, "no look-ahead")
bool followToNextPoint(AIState &s, int &index)
{
	Object &o = s.owner();
	GiantBirdAIUpdate &ai = bird(s);
	if (!ai.curLocomotor())
	{
		return false;
	}
	const Coord3D *point = s.machine().goalPathPosition(index++);
	if (!point)
	{
		if (!ai.loopPath())
		{
			return false;
		}
		index = 0;
		point = s.machine().goalPathPosition(index++);
		if (!point)
		{
			return false;
		}
	}
	const float h = ai.pathHeight();
	const Coord3D *nextPoint = s.machine().goalPathPosition(index);
	Coord3D lookAhead{ 0.0f, 0.0f, 0.0f };
	if (nextPoint)
	{
		ai.flight().flags |= 0x80u;
		lookAhead = *nextPoint;
		lookAhead.z = SimMath::fstpDword(SimMath::pc24AddW((double)o.logic().getGroundHeight(lookAhead.x, lookAhead.y), (double)h));
	}
	else
	{
		ai.flight().flags &= ~0x80u;
	}
	Coord3D pos = *point;
	pos.z = SimMath::fstpDword(SimMath::pc24AddW((double)o.logic().getGroundHeight(pos.x, pos.y), (double)h));
	ai.buildPath(pos, AerialPathfinder::MASK_A, nextPoint ? &lookAhead : nullptr, nextPoint == nullptr);
	return true;
}

// the update of both follow states (RW 0x76ABC1 / RW 0x76B14A): dead, failure; the path steered (RW 0x76A933 with the path height, the anchor, no snap);
// reached, or within one speed of the anchor (the object put on it, RW 0x70C201): the next point, success when none, failure when its path is not valid.
// With a look-ahead the anchor is the look-ahead point (RW 0x778413 ends the spline there), so a steering rebuild flies straight at it: a leg's own point
// can be cut short (a climbing take-off does). That is RotWK's rule (RW 0x76ABC1 passes AI + 0x608, the anchor), not a port gap
StateReturnType followUpdate(AIState &s, int &index)
{
	Object &o = s.owner();
	if (o.isEffectivelyDead())
	{
		return STATE_FAILURE;
	}
	GiantBirdAIUpdate &ai = bird(s);
	const Coord3D anchor = ai.flight().anchor;
	ai.steer(ai.pathHeight(), &anchor, false);
	if (GiantBirdNormalFlightState::arrived(ai))
	{
		if (!followToNextPoint(s, index))
		{
			return STATE_SUCCESS;
		}
		if (!ai.spline().valid())
		{
			return STATE_FAILURE;
		}
	}
	return STATE_CONTINUE;
}

// the common tail of both exits (RW 0x76AC9F / RW 0x76B228 after their base exit): the look-ahead bit cleared, MOVING, CLIMBING and FREEFALL (conditions 61,
// 103, 72) cleared, the goal reservation at the object's position (RW 0x68B3BD(position, 1))
void followExitTail(AIState &s)
{
	Object &o = s.owner();
	bird(s).flight().flags &= ~0x80u;
	clearCondition(o, names().moving);
	clearCondition(o, names().climbing);
	clearCondition(o, names().freefall);
	GiantBirdNormalFlightState::reserveGoal(o);
}

// GiantBirdFollowPathState (state 0x3F6; ctor RW 0x7691AB, an AIFollowPathState: vtable RW 0xC2D940)
class GiantBirdFollowPathState : public AIState
{
public:
	explicit GiantBirdFollowPathState(AIStateMachine &m)
		: AIState(m, "GiantBirdFollowPathState")
	{
	}
	StateReturnType onEnter() override
	{
		// RW 0x76A2B8: the index 0; no AI, dead or without a locomotor: failure; the path height is the locomotor's PreferredHeight (+ 0x48); no first point:
		// success; the path not valid: failure; else the look-ahead bit set (RW 0x76A312 -> RW 0x76913B(7, 1))
		m_index = 0;
		Object &o = owner();
		GiantBirdAIUpdate &ai = bird(*this);
		Locomotor *loco = ai.curLocomotor();
		if (o.isEffectivelyDead() || !loco)
		{
			return STATE_FAILURE;
		}
		ai.setPathHeight(loco->preferredHeight());
		if (!followToNextPoint(*this, m_index))
		{
			return STATE_SUCCESS;
		}
		if (!ai.spline().valid())
		{
			return STATE_FAILURE;
		}
		ai.flight().flags |= 0x80u;
		return STATE_CONTINUE;
	}
	StateReturnType update() override { return followUpdate(*this, m_index); }
	void onExit(StateExitType status) override
	{
		(void)status;
		if (ai().isDestroying())
		{
			return;
		}
		// RW 0x74B77D (AIFollowPathState::onExit) first: its internal move's exit RW 0x748D8A (MOVING, BACKING_UP, CLIMBING, RAPPELLING cleared, the move
		// loop stopped, RW 0x6627CB), then the AI's path-through-units flag (+ 0x3BA), its ignored obstacle (+ 0x164, RW 0x662D98(0)), its goal path index
		// (+ 0x194 = -1), IS_LEAVING_FACTORY (status 0x5A) unless the object is a HORDE, the locomotor's speed cap (+ 0x2C = -1). Not ported: the
		// locomotor flag + 0x44 bit 3 (its reader is not identified; the bird's locomotor is the GIANT_BIRD mover)
		Object &o = owner();
		GiantBirdAIUpdate &ai = bird(*this);
		AIMover &m = ai.mover();
		m.setMoveCondition("MOVING", false);
		m.setMoveCondition("BACKING_UP", false);
		m.setMoveCondition("CLIMBING", false);
		m.setMoveCondition("RAPPELLING", false);
		m.host().stopMoveSound();
		m.endingMove();
		ai.setCanPathThroughUnits(false);
		m.ignoreObstacle(PATHFIND_INVALID_ID);
		ai.setCurrentGoalPathIndex(-1);
		if (!isKind(o, names().horde))
		{
			o.setStatus((unsigned)names().leavingFactory, false);
		}
		if (Locomotor *loco = ai.curLocomotor())
		{
			loco->setTemporarySpeedCap(-1.0f);
		}
		followExitTail(*this); // RW 0x76ACB1 ..
	}
	void crc(StateHasher &h) const override { h.addI32(m_index); }
	void xfer(Xfer &x) override // OpenBFME layout (S-4220): the point index
	{
		XferVersion v{ 1, 1 };
		x.xferVersion(v);
		x.xferInt(&m_index);
		if (x.isLoading() && m_index < 0)
		{
			throw XferException("giant bird: a follow path state's point index " + std::to_string(m_index));
		}
	}

private:
	int m_index = 0; // + 0x58
};

// GiantBirdFollowWaypointPathState (states 0x3F7 / 0x3F8; ctor RW 0x7692DA (machine, + 0x24), vtable RW 0xC2DB00; retail xfer RW 0x769308: the index and
// the flag)
class GiantBirdFollowWaypointPathState : public AIState
{
public:
	GiantBirdFollowWaypointPathState(AIStateMachine &m, bool asTeam)
		: AIState(m, "GiantBirdFollowWaypointPathState")
		, m_asTeam(asTeam)
	{
	}
	StateReturnType onEnter() override
	{
		// RW 0x76A451: the index 0; no AI or dead: failure; no first point: success; the path not valid: failure; else the look-ahead bit set (no locomotor
		// test, no path height here: the command set it)
		m_index = 0;
		if (owner().isEffectivelyDead())
		{
			return STATE_FAILURE;
		}
		if (!followToNextPoint(*this, m_index))
		{
			return STATE_SUCCESS;
		}
		GiantBirdAIUpdate &ai = bird(*this);
		if (!ai.spline().valid())
		{
			return STATE_FAILURE;
		}
		ai.flight().flags |= 0x80u;
		return STATE_CONTINUE;
	}
	StateReturnType update() override { return followUpdate(*this, m_index); }
	void onExit(StateExitType status) override
	{
		(void)status; // RW 0x76B228: State::onExit (RW 0x9F3A3C) first
		if (ai().isDestroying())
		{
			return;
		}
		followExitTail(*this);
	}
	void crc(StateHasher &h) const override
	{
		h.addI32(m_index);
		h.addBool(m_asTeam);
	}
	void xfer(Xfer &x) override // OpenBFME layout (S-4220): the point index, the constructor's flag checked
	{
		XferVersion v{ 1, 1 };
		x.xferVersion(v);
		x.xferInt(&m_index);
		bool asTeam = m_asTeam;
		x.xferBool(&asTeam);
		if (x.isLoading() && (m_index < 0 || asTeam != m_asTeam))
		{
			throw XferException("giant bird: a waypoint path state saved with another index or flag");
		}
	}

private:
	int m_index = 0; // + 0x20
	bool m_asTeam;   // + 0x24 (read by nothing ported)
};
} // namespace

// ---------------------------------------------------------------------------------------------------------------------------------
// the module
// ---------------------------------------------------------------------------------------------------------------------------------
GiantBirdAIUpdateData GiantBirdAIUpdate::parseData(const ModuleData *data)
{
	GiantBirdAIUpdateData d;
	const RawModuleData *raw = dynamic_cast<const RawModuleData *>(data);
	if (!raw)
	{
		return d;
	}
	for (const RawModuleData::Line &l : raw->lines())
	{
		std::string text = l.text;
		const size_t semi = text.find(';');
		if (semi != std::string::npos)
		{
			text.erase(semi);
		}
		const size_t eq = text.find('=');
		if (eq == std::string::npos)
		{
			continue;
		}
		const std::string name = trimmed(text.substr(0, eq));
		const std::string value = trimmed(text.substr(eq + 1));
		auto real = [&](float &out) {
			char *end = nullptr;
			out = strtofPortable(value.c_str(), &end); // INI::parseReal (sscanf %f)
		};
		auto locomotorSet = [&](int &out) {
			for (int i = 0; TheLocomotorSetNames[i]; ++i)
			{
				if (sameNoCase(value, TheLocomotorSetNames[i]))
				{
					out = i;
					return;
				}
			}
			throw std::logic_error("GiantBirdAIUpdate: " + name + " = " + value + " is not a locomotor set name"); // INI::parseIndexList
		};
		if (sameNoCase(name, "AttackLocomotorType"))
		{
			locomotorSet(d.attackLocomotorType);
		}
		else if (sameNoCase(name, "ReturnForAmmoLocomotorType"))
		{
			locomotorSet(d.returnForAmmoLocomotorType);
		}
		else if (sameNoCase(name, "GrabTossTimeTrigger"))
		{
			real(d.grabTossTimeTrigger);
		}
		else if (sameNoCase(name, "GrabTossHeightTrigger"))
		{
			real(d.grabTossHeightTrigger);
		}
		else if (sameNoCase(name, "FollowThroughDistance"))
		{
			real(d.followThroughDistance);
		}
		else if (sameNoCase(name, "FollowThroughCheckStep"))
		{
			real(d.followThroughCheckStep);
		}
		else if (sameNoCase(name, "FollowThroughGradient"))
		{
			real(d.followThroughGradient);
		}
		else if (sameNoCase(name, "TossFX"))
		{
			d.tossFX = value;
		}
	}
	return d;
}

GiantBirdAIUpdate::GiantBirdAIUpdate(Thing *thing, const ModuleData *data)
	: AIUpdateInterface(thing, data)
	, m_data(parseData(data))
	, m_host(std::make_unique<BirdHost>(*this))
{
}

GiantBirdAIUpdate::~GiantBirdAIUpdate() = default;

void GiantBirdAIUpdate::registerClass(ModuleFactory &modules)
{
	modules.bindModuleProc("GiantBirdAIUpdate", MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &info) -> std::unique_ptr<Module> {
		// a logic without an AIWorld has nothing to fly on: the explicit unported module, as AIUpdateInterface::registerClasses does
		if (!thing->asObject() || !thing->asObject()->logic().aiWorld())
		{
			return makeUnportedModule(thing, data, info.name, info.type, info.interfaceMask);
		}
		return std::make_unique<GiantBirdAIUpdate>(thing, data);
	});
}

std::unique_ptr<AIStateMachine> GiantBirdAIUpdate::makeStateMachine()
{
	std::unique_ptr<AIStateMachine> m = AIUpdateInterface::makeStateMachine(); // RW 0x7695C2 -> the base states (RW 0x753755)
	// AIGoingIdleState 0x4E (RW 0x7537E0 / 0x7541EE) is a base state of RW 0x753755: AIUpdateInterface::makeStateMachine defines it (lane PLAY-4, AIGuard.cpp)
	m->defineState(STATE_FLY_TO, std::make_unique<GiantBirdNormalFlightState>(*m, false, true), STATE_GOING_IDLE, STATE_GOING_IDLE);
	m->defineState(STATE_FLY_TO_ABOVE, std::make_unique<GiantBirdNormalFlightState>(*m, true, true), STATE_GOING_IDLE, STATE_GOING_IDLE);
	m->defineState(STATE_FLY_400, std::make_unique<GiantBirdNormalFlightState>(*m, false, true), AI_IDLE, AI_IDLE);
	m->defineState(STATE_SWOOP, std::make_unique<AIGiantBirdSwoopState>(*m, true, false), STATE_ATTACK, AI_IDLE);
	m->defineState(STATE_SWOOP_LEVEL, std::make_unique<AIGiantBirdSwoopState>(*m, true, true), STATE_ATTACK, AI_IDLE);
	m->defineState(STATE_ATTACK, std::make_unique<AIGiantBirdAttackState>(*m, true), STATE_FOLLOW_THRU, STATE_FOLLOW_THRU);
	m->defineState(STATE_FOLLOW_THRU, std::make_unique<AIGiantBirdFollowThruState>(*m, false), AI_IDLE, STATE_SWOOP);
	m->defineState(STATE_SWOOP_AGAIN, std::make_unique<AIGiantBirdSwoopState>(*m, true, false), STATE_ATTACK_AGAIN, AI_IDLE);
	m->defineState(STATE_ATTACK_AGAIN, std::make_unique<AIGiantBirdAttackState>(*m, true), STATE_FOLLOW_THRU_AGAIN, STATE_FOLLOW_THRU_AGAIN);
	m->defineState(STATE_FOLLOW_THRU_AGAIN, std::make_unique<AIGiantBirdFollowThruState>(*m, true), STATE_SWOOP_AGAIN, STATE_SWOOP_AGAIN);
	m->defineState(STATE_SWOOP_POSITION, std::make_unique<AIGiantBirdSwoopState>(*m, false, false), STATE_ATTACK_POSITION, AI_IDLE);
	m->defineState(STATE_SWOOP_POSITION_LEVEL, std::make_unique<AIGiantBirdSwoopState>(*m, false, true), STATE_ATTACK, AI_IDLE);
	m->defineState(STATE_ATTACK_POSITION, std::make_unique<AIGiantBirdAttackState>(*m, false), STATE_FOLLOW_THRU_POSITION, STATE_FOLLOW_THRU_POSITION);
	m->defineState(STATE_FOLLOW_THRU_POSITION, std::make_unique<AIGiantBirdFollowThruState>(*m, false), STATE_GOING_IDLE, STATE_GOING_IDLE);
	m->defineState(STATE_FOLLOW_PATH, std::make_unique<GiantBirdFollowPathState>(*m), STATE_GOING_IDLE, STATE_GOING_IDLE); // lane STEALTHFLY1
	m->defineState(STATE_FOLLOW_WAYPOINTS, std::make_unique<GiantBirdFollowWaypointPathState>(*m, false), STATE_GOING_IDLE, STATE_GOING_IDLE);
	m->defineState(STATE_FOLLOW_WAYPOINTS_TEAM, std::make_unique<GiantBirdFollowWaypointPathState>(*m, true), STATE_GOING_IDLE, STATE_GOING_IDLE);
	m->defineState(STATE_ATTACK_MOVE, std::make_unique<GiantBirdAttackMoveToState>(*m), STATE_GOING_IDLE, STATE_GOING_IDLE);
	m->defineState(STATE_FACE_POSITION, std::make_unique<GiantBirdNormalFlightState>(*m, false, true), AI_IDLE, AI_IDLE); // RW 0x7695C2: 0x3FC
	m->defineState(STATE_FACE_OBJECT, std::make_unique<GiantBirdNormalFlightState>(*m, false, true), AI_IDLE, AI_IDLE);   // RW 0x7695C2: 0x3FF
	return m;
}

std::unique_ptr<AIStateMachine> GiantBirdAIUpdate::makeAttackThenIdleMachine(unsigned attackStateId)
{
	std::unique_ptr<AIStateMachine> m = std::make_unique<AIStateMachine>(*this, "AIAttackSwoopThenIdleStateMachine");
	m->defineState(attackStateId, std::make_unique<AIGiantBirdSwoopState>(*m, true, false), STATE_ATTACK, AI_IDLE);
	m->defineState(STATE_ATTACK, std::make_unique<AIGiantBirdAttackState>(*m, true), STATE_FOLLOW_THRU, STATE_FOLLOW_THRU);
	m->defineState(STATE_FOLLOW_THRU, std::make_unique<AIGiantBirdFollowThruState>(*m, false), AI_IDLE, AI_IDLE);
	m->defineState(AI_IDLE, std::make_unique<SwoopIdleState>(*m), AI_IDLE, AI_IDLE);
	return m;
}

void GiantBirdAIUpdate::startCommand(unsigned state, CommandSourceType source)
{
	setLastCommandSource(source); // RW 0x6E8498
	stateMachine().setState(state);
	m_goalType = 1; // + 0x5EC
	wakeUpNow();
}

// RW 0x76D995 (aiDoCommand) for the commands the base routes here
bool GiantBirdAIUpdate::routeCommand(const RoutedCommand &c)
{
	Object &o = *getObject();
	const bool routed = c.type == 0 || c.type == kCommandUnidentifiedMove || c.type == 0xB || c.type == 0xC || c.type == 0x39 || c.type == 0xE || c.type == 0xF ||
						c.type == 6 || c.type == 7 || c.type == 0x32 || c.type == 0x33 || c.type == 0x47;
	if (!routed)
	{
		// RW 0x76DACD: the bird's flag 0 cleared, then the base aiDoCommand (RW 0x66A7EC). (The deferral of such a command while attacking is ported for the
		// follow paths only, deferWhileAttacking)
		if (!m_attacking)
		{
			m_flight.flags &= ~1u;
		}
		return false;
	}
	if (m_attacking)
	{
		// RW 0x76D9B6 .. 0x76D9C7: a command during the attack waits (RW 0x75307D); the update re-issues it
		m_deferred = c;
		m_deferredTarget = c.target ? c.target->getID() : INVALID_ID;
		m_deferredValid = true;
		return true;
	}
	m_deferredValid = false; // RW 0x76D9CF: + 0x494 (AI + 0x4B4) = -1
	const bool mobile = !isKind(o, names().immobile); // RW 0x690E97 (its IMMOBILE test, S-1920)
	switch (c.type)
	{
	case 0:
	case kCommandUnidentifiedMove: // INFERENCE: the move with a final angle is AI command 0x4E, which RW 0x76DADE routes as a move (the angle is not kept)
		// RW 0x769FF7: the machine cleared (vslot 0x14), the goal position (RW 0x6654AD), the source, state 0x3E9, goal type 1
		if (mobile)
		{
			stateMachine().clear();
			stateMachine().setGoalPosition(c.position);
			stateMachine().setGoalObject(INVALID_ID);
			startCommand(STATE_FLY_TO, c.source);
		}
		return true;
	case 0xB:
	case 0xC:
	case 0x39: // lane STEALTHFLY1: RW 0x76DA1A, as 0xB
		// RW 0x76A139 (victim, source, 0): the source, the machine cleared, the goal object (vslot 0x38), state 0x3F2, goal type 1
		if (c.target && mobile)
		{
			setLastCommandSource(c.source);
			stateMachine().clear();
			stateMachine().setGoalObject(c.target->getID());
			stateMachine().setGoalPosition(*c.target->getPosition());
			startCommand(STATE_SWOOP, c.source);
		}
		return true;
	case 0x47:
		// RW 0x76A04A (a deferred ability approach re-issued by the update)
		if (mobile)
		{
			stateMachine().clear();
			stateMachine().setGoalPosition(c.position);
			stateMachine().setGoalObject(INVALID_ID);
			startCommand(STATE_FLY_400, c.source);
		}
		return true;
	case 0xE:
		return true; // RW 0x76DAC3: attack position: nothing
	case 0xF:
		// RW 0x76A09D (attack move): the goal position and state 0x3FB (the same shape as the move, RW 0x769FF7)
		if (mobile)
		{
			stateMachine().clear();
			stateMachine().setGoalPosition(c.position);
			stateMachine().setGoalObject(INVALID_ID);
			startCommand(STATE_ATTACK_MOVE, c.source);
		}
		return true;
	default:
		// 6 / 0x32 -> RW 0x76D84B(waypoint, 0, source); 7 / 0x33 -> (waypoint, 1, source) (lane STEALTHFLY1)
		followWaypointPath(c.waypointId, c.type == 7 || c.type == 0x33, c.source);
		return true;
	}
}

// RW 0x76D84B (BFME2 decomp Rva00368344Rva0036BB2F.cpp, tier B same-shape): not dead and mobile; the path height is the locomotor's PreferredHeight; the loop
// flag cleared; the chain from the waypoint: each location appended (RW 0x66E1D3), stopping at a waypoint without links (+ 0x4C), else following its first
// link (+ 0x20) until it comes back to the first waypoint (the loop flag set). With a waypoint: the machine cleared, the goal position the last point (RW
// 0x8DB810: unless the machine is locked; its + 0x30 = FLT_MAX), the goal path, the source (RW 0x6E8498), state 0x3F7 + asTeam. (No goal type here.)
void GiantBirdAIUpdate::followWaypointPath(int first, bool asTeam, CommandSourceType source)
{
	Object &o = *getObject();
	if (o.isEffectivelyDead() || isKind(o, names().immobile)) // RW 0x76D86A (+ 0x458 bit 0), RW 0x690E97 (its IMMOBILE test, S-1920)
	{
		return;
	}
	if (Locomotor *loco = curLocomotor())
	{
		m_pathHeight = loco->preferredHeight();
	}
	m_loopPath = false;
	const TerrainLogic *terrain = o.logic().terrain();
	const Waypoint *start = terrain ? terrain->findWaypointById(first) : nullptr;
	if (!start)
	{
		return;
	}
	std::vector<Coord3D> path;
	const Waypoint *w = start;
	// a chain that runs into a cycle that does not pass the first waypoint never ends in RotWK (the walk only stops at a waypoint without links or at the
	// first one): it is cut after as many steps as the map has waypoints and reported
	const size_t limit = terrain->waypoints().size() + 1;
	for (;;)
	{
		path.push_back(w->location);
		if (w->linksTo.empty())
		{
			break;
		}
		w = terrain->findWaypointById(w->linksTo.front());
		if (!w)
		{
			break;
		}
		if (w == start)
		{
			m_loopPath = true;
			break;
		}
		if (path.size() > limit)
		{
			world().noteStop("[S-4040] GiantBirdAIUpdate: a waypoint chain that cycles without returning to its first waypoint (RotWK RW 0x76D84B walks it "
							 "forever): cut after every waypoint of the map");
			break;
		}
	}
	stateMachine().clear();
	if (!stateMachine().isLocked())
	{
		stateMachine().setGoalPosition(path.back());
		stateMachine().clearGoalAngle(); // INFERENCE: RW 0x8DB778's + 0x30 = FLT_MAX is the machine's goal angle "none"
	}
	stateMachine().setGoalPath(path);
	setLastCommandSource(source);
	stateMachine().setState(asTeam ? STATE_FOLLOW_WAYPOINTS_TEAM : STATE_FOLLOW_WAYPOINTS);
	wakeUpNow();
}

bool GiantBirdAIUpdate::deferrable(int type)
{
	static const int kRouted[] = { 0, kCommandUnidentifiedMove, 0xB, 0xC, 0x39, 0xE, 0xF, 6, 7, 0x32, 0x33, 0x47, kDeferredFollowPath, kDeferredFollowPathAppend };
	return std::find(std::begin(kRouted), std::end(kRouted), type) != std::end(kRouted);
}

bool GiantBirdAIUpdate::deferWhileAttacking(const RoutedCommand &c)
{
	if (!m_attacking)
	{
		m_deferredValid = false;  // RW 0x76D9CF
		m_flight.flags &= ~1u;    // RW 0x76DACD (the base aiDoCommand's commands)
		return false;
	}
	m_deferred = c;
	m_deferredTarget = c.target ? c.target->getID() : INVALID_ID;
	m_deferredValid = true;
	return true;
}

// AI vslot 0x78 (RW 0x76B491; BFME2 decomp GiantBirdAIUpdate_privateFollowPath.cpp, tier B edited): mobile, else nothing; the machine cleared; the goal position the path's last point
// (RW 0x8DB810 as above); the goal path; the source; the ignored obstacle (RW 0x66831A); state 0x3F6; goal type 1. The production exit flag is not read
void GiantBirdAIUpdate::privateFollowPath(const std::vector<Coord3D> &path, Object *ignoreObject, CommandSourceType source, bool exitProduction)
{
	(void)exitProduction;
	RoutedCommand c;
	c.type = kDeferredFollowPath;
	c.path = path;
	c.target = ignoreObject;
	c.source = source;
	if (deferWhileAttacking(c))
	{
		return;
	}
	if (isKind(*getObject(), names().immobile)) // RW 0x690E97 (S-1920)
	{
		return;
	}
	stateMachine().clear();
	if (!path.empty() && !stateMachine().isLocked())
	{
		stateMachine().setGoalPosition(path.back());
		stateMachine().clearGoalAngle();
	}
	stateMachine().setGoalPath(path);
	mover().ignoreObstacle(ignoreObject ? ignoreObject->getID() : PATHFIND_INVALID_ID);
	startCommand(STATE_FOLLOW_PATH, source);
}

// AI vslots 0x114 / 0x118: RW 0x76B543 passes the target's position (Object + 0x38) and 1, RW 0x76B559 the position and 0. The base aiDoCommand does not route
// the face commands (0x26 / 0x27), so RW 0x76DACD clears flag 0 first when not attacking (as routeCommand's unrouted path)
void GiantBirdAIUpdate::privateFaceObject(Object *target, CommandSourceType source)
{
	if (!target)
	{
		return; // INFERENCE: RW 0x76B543 reads the target's position without a test (every ported caller passes one)
	}
	faceTowards(*target->getPosition(), source, true);
}

void GiantBirdAIUpdate::privateFacePosition(const Coord3D &pos, CommandSourceType source)
{
	faceTowards(pos, source, false);
}

void GiantBirdAIUpdate::faceTowards(const Coord3D &pos, CommandSourceType source, bool objectTarget)
{
	if (!m_attacking)
	{
		m_flight.flags &= ~1u; // RW 0x76DACD
	}
	Object &o = *getObject();
	if (!GarrisonRules::isMobile(o)) // RW 0x690E97 (IMMOBILE, disabled, stoned), as the base face commands
	{
		return;
	}
	m_goalAngle = SimMath::addf32(emotionRelAngle(o, pos), o.getOrientation()); // + 0x5F0: RW 0x4B3D8D + Object + 0x44
	stateMachine().clear();                                                    // machine vslot 0x14
	if (!stateMachine().isLocked())
	{
		stateMachine().setGoalPosition(*o.getPosition()); // RW 0x661EFE -> RW 0x8DB810: the bird's own position, unless the machine is locked
	}
	setLastCommandSource(source); // RW 0x6E8498
	stateMachine().setState(objectTarget ? STATE_FACE_OBJECT : STATE_FACE_POSITION); // machine vslot 0x20
	m_goalType = 2;                                                                   // + 0x5EC
	wakeUpNow(); // INFERENCE: RW 0x76AB4E does not wake the AI; the port's commands do (startCommand)
}

// AI vslot 0x84 (RW 0x76D659; BFME2 decomp GiantBirdAIUpdatePrivateFollowPathAppend.cpp, tier B edited): the loop flag cleared; following a path (0x3F6) with
// points left: the point is appended (RW 0x75536B); else a new path: in a flight state (0x3E9 .. 0x400 but the attack states 0x3ED / 0x3F4) from the machine's
// goal position through the point, otherwise the point alone, through vslot 0x78 with no ignored obstacle
void GiantBirdAIUpdate::privateFollowPathAppend(const Coord3D &pos, CommandSourceType source)
{
	RoutedCommand c;
	c.type = kDeferredFollowPathAppend;
	c.position = pos;
	c.source = source;
	if (deferWhileAttacking(c))
	{
		return;
	}
	m_loopPath = false;
	const unsigned st = currentStateId();
	if (st == STATE_FOLLOW_PATH && !stateMachine().goalPath().empty())
	{
		stateMachine().addToGoalPath(pos);
		return;
	}
	std::vector<Coord3D> path;
	if (!(st < 0x3E9 || st > 0x400 || st == STATE_ATTACK_POSITION || st == STATE_ATTACK))
	{
		path.push_back(stateMachine().goalPosition());
	}
	path.push_back(pos);
	privateFollowPath(path, nullptr, source, false);
}

bool GiantBirdAIUpdate::aiSwoopAttackObject(Object *victim, CommandSourceType source)
{
	// RW 0x77131B: AICommandParms(0x39, source) with the victim -> aiDoCommand (the gate RW 0x667174, then RW 0x76D995)
	if (!acceptCommand(source, 0x39, victim))
	{
		return false;
	}
	RoutedCommand c;
	c.type = 0x39;
	c.target = victim;
	c.source = source;
	return routeCommand(c);
}

bool GiantBirdAIUpdate::aiAbilityMoveTo(const Coord3D &pos, CommandSourceType source)
{
	// RW 0x76D995 for AI command 0x47 (0x48 passes the object's position + 0x38): the gate, the deferral while attacking, then RW 0x76A04A: a mobile flyer's
	// machine cleared, the goal position (RW 0x6654AD), the source, state 0x400, goal type 1
	const int command = 0x47;
	if (!acceptCommand(source, command))
	{
		return false;
	}
	if (m_attacking)
	{
		RoutedCommand c;
		c.type = command;
		c.position = pos;
		c.source = source;
		m_deferred = c;
		m_deferredTarget = INVALID_ID;
		m_deferredValid = true;
		return true;
	}
	m_deferredValid = false;
	if (isKind(*getObject(), names().immobile))
	{
		return false;
	}
	stateMachine().clear();
	stateMachine().setGoalPosition(pos);
	stateMachine().setGoalObject(INVALID_ID);
	startCommand(STATE_FLY_400, source);
	return true;
}

bool GiantBirdAIUpdate::aiLevelAttackObject(Object *victim, CommandSourceType source)
{
	// AI command 0x3F -> RW 0x76A139 (victim, source, 1): the level swoop 0x3F3
	if (!acceptCommand(source, 0x3F, victim))
	{
		return false;
	}
	if (!victim || isKind(*getObject(), names().immobile))
	{
		return false;
	}
	if (m_attacking)
	{
		return false; // RW 0x75307D would keep it for later; the ability's command is not deferred here (S-4040)
	}
	setLastCommandSource(source);
	stateMachine().clear();
	stateMachine().setGoalObject(victim->getID());
	stateMachine().setGoalPosition(*victim->getPosition());
	startCommand(STATE_SWOOP_LEVEL, source);
	return true;
}

bool GiantBirdAIUpdate::isIdle() const
{
	if (m_flight.flags & 1u)
	{
		return false; // RW 0x769E8F
	}
	return AIUpdateInterface::isIdle();
}

float GiantBirdAIUpdate::locomotorPreferredHeight() const
{
	const Locomotor *loco = curLocomotor();
	return loco ? loco->preferredHeight() : 0.0f;
}

AerialPathfinder::Flyer GiantBirdAIUpdate::flyer() const
{
	AerialPathfinder::Flyer f;
	f.object = const_cast<GiantBirdAIUpdate *>(this)->getObject();
	f.hasAI = true;
	f.preferredHeight = locomotorPreferredHeight();
	f.partner = m_victim; // AI vslot 0x184 (RW 0x8CEF91: the AI itself) -> + 0x584
	f.surfaces = locomotorInfo().validSurfaces;
	return f;
}

void GiantBirdAIUpdate::buildPath(const Coord3D &goal, unsigned mask, const Coord3D *lookAhead, bool snap)
{
	static const std::string aerialStop = AerialPathfinder::stopLines()[0];
	world().noteStop(aerialStop.c_str()); // S-4041, once per game (AIWorld keeps each line once)
	world().noteStop(birdStop());          // S-4040
	AerialPathfinder::computeAerialPath(flyer(), goal, m_spline, mask, lookAhead, snap);
	m_spline.prepare();
	m_spline.build();
	m_flight.anchor = m_spline.endPoint();
	m_pathDistance = 0.0f;
	m_reached = false;
}

int GiantBirdAIUpdate::checkAhead(float clearance, bool forward)
{
	Object &o = *getObject();
	if (!curLocomotor())
	{
		return 0;
	}
	if (!m_spline.valid())
	{
		return 2;
	}
	const AerialPathfinder::Flyer f = flyer();
	const float sphere = CombatQueries::boundingSphereRadius(o);
	if (!forward)
	{
		const float base = SimMath::mulf32(m_pathDistance, 2.0f); // RW 0xBD889C
		if (!AerialPathfinder::pathClear(f, m_spline, SimMath::subf32(base, SimMath::mulf32(sphere, 0.5f)), clearance) ||
			!AerialPathfinder::pathClear(f, m_spline, base, clearance))
		{
			return 1;
		}
	}
	else if (!AerialPathfinder::pathClear(f, m_spline, SimMath::addf32(SimMath::addf32(sphere, sphere), m_pathDistance), clearance))
	{
		return 1;
	}
	if (!isWellAbove())
	{
		return 0;
	}
	Coord3D next;
	if (!AerialPathfinder::query(m_spline, SimMath::addf32(m_pathDistance, 10.0f), next)) // RW 0xBD83D8
	{
		return 0;
	}
	const float z = o.getPosition()->z;
	if (SimMath::subf32(next.z, 2.0f) <= z || z <= m_pathHeight)
	{
		return 0;
	}
	return 2;
}

void GiantBirdAIUpdate::steer(float clearance, const Coord3D *goal, bool snap)
{
	const int r = checkAhead(clearance, false);
	if (r == 0)
	{
		return;
	}
	const Coord3D target = goal ? *goal : m_flight.anchor;
	Object &o = *getObject();
	Coord3D p = *o.getPosition();
	unsigned mask;
	if (r == 1)
	{
		p.z = SimMath::addf32(p.z, 5.0f); // RW 0xBDAE58
		mask = AerialPathfinder::MASK_A_LOWER_START;
	}
	else
	{
		p.z = SimMath::subf32(p.z, 1.0f); // RW 0xBD1908
		mask = AerialPathfinder::MASK_A;
	}
	o.setPosition(&p); // RW 0x70C0AD
	buildPath(target, mask, nullptr, snap);
}

bool GiantBirdAIUpdate::isWellAbove() const
{
	const Object &o = *const_cast<GiantBirdAIUpdate *>(this)->getObject();
	const Locomotor *loco = curLocomotor();
	if (!loco)
	{
		return false;
	}
	const float ph = loco->preferredHeight();
	const float threshold = SimMath::mulf32(ph, 2.0f);
	const float h = heightAboveTerrain(o);
	const bool above = h > threshold;
	const bool also = o.getPosition()->z > threshold && h > SimMath::mulf32(ph, 0.3f);
	return above || also;
}

// RW 0x76A9CB (BFME2 decomp GiantBirdDoLocomotor.cpp rva00368D12): the closest object within twice the bounding circle radius that is STRUCTURE and MONSTER
// and MACHINE at once (isKindOfMulti, see the follow-through's test) pushes the flyer 2 away from it (plus a logic random +-0.1 in x and y)
void GiantBirdAIUpdate::separationNudge()
{
	Object &o = *getObject();
	const float radius = CombatQueries::boundingCircleRadius(o);
	Coord3D p = *o.getPosition();
	const BirdNames &n = names();
	auto all = [&n](Object &x) { return isKind(x, n.structure) && isKind(x, n.monster) && isKind(x, n.machine); };
	PartitionFilterFn<decltype(all)> filter(all);
	Object *near = o.logic().partition().getClosestObject(p, SimMath::mulf32(radius, 2.0f), FROM_CENTER_2D, { &filter });
	if (!near)
	{
		return;
	}
	Coord3D away{ SimMath::subf32(p.x, near->getPosition()->x), SimMath::subf32(p.y, near->getPosition()->y), 0.0f };
	const float len = (float)SimMath::length3d(away.x, away.y, 0.0f);
	if (len != 0.0f)
	{
		away.x = SimMath::divf32(away.x, len);
		away.y = SimMath::divf32(away.y, len);
	}
	away.x = SimMath::mulf32(away.x, 2.0f);
	away.y = SimMath::mulf32(away.y, 2.0f);
	away.z = SimMath::mulf32(away.z, 2.0f);
	away.x = SimMath::addf32(away.x, o.logic().random().getValueReal(-0.1f, 0.1f, kGiantBirdCpp, 0xEB4));
	const float dy = SimMath::addf32(o.logic().random().getValueReal(-0.1f, 0.1f, kGiantBirdCpp, 0xEB5), away.y);
	p.z = SimMath::addf32(p.z, away.z);
	p.x = SimMath::addf32(p.x, away.x);
	p.y = SimMath::addf32(p.y, dy);
	o.setPosition(&p);
}

unsigned GiantBirdAIUpdate::doLocomotor()
{
	Object &o = *getObject();
	if (isKind(o, names().immobile))
	{
		return 0x3FFFFFFFu;
	}
	chooseGoodLocomotorFromCurrentSet(); // RW 0x663D70
	Locomotor *loco = curLocomotor();
	if (!loco || (mover().isAiDead() && !loco->getTemplate().m_locomotorWorksWhenDead))
	{
		return 1;
	}
	switch (m_goalType)
	{
	case 1:
	{
		const float speed = loco->getMaxSpeedForCondition(*m_host); // RW 0x5E3F49
		m_reached = loco->giantBirdFollowPath(*m_host, speed, m_pathDistance);
		break;
	}
	case 2:
	{
		// RW 0x76D2E2 .. 0x76D34A: the face states 0x3FC / 0x3FF turn toward + 0x5F0 (Locomotor RW 0x5E98D6); within 0.1 (RW 0xBD83D4, both angles through
		// RW 0x644FD0) the AI idles with its last command's source (AI vslot 0x23C, RW 0x5E821A) and the AIRBORNE_TARGET update is skipped
		loco->locomotorMoveTowardsAngle(*m_host, m_goalAngle);
		const float facing = normalizeAngle(o.getOrientation());
		const float goal = normalizeAngle(m_goalAngle);
		if (SimMath::absD((double)SimMath::subf32(facing, goal)) < (double)0.1f)
		{
			aiIdle(lastCommandSource());
			return 1;
		}
		break;
	}
	case 3:
		separationNudge();
		loco->giantBirdHover(*m_host);
		m_reached = true;
		break;
	default:
		break;
	}
	o.setStatus((unsigned)names().airborneTarget, (float)loco->getTemplate().m_airborneTargetingHeight < heightAboveTerrain(o));
	return 1;
}

// RW 0x76CDFB
// RW 0x76CB71: while a victim is held (flag 6): the clock (+ 0x580) runs in seconds (1 / LOGICFRAMES_PER_SECOND a frame, SSE) while the contain holds anything,
// else it is 0. Once it passes GrabTossTimeTrigger (when over 0) or the height above the terrain (when the flyer is above it, RW 0x4B12E8) passes
// GrabTossHeightTrigger (when over 0), flag 6 goes and the held rider (+ 0x588) is let go: out of the contain (slot 0xA4), turned like the flyer (Matrix3D::
// Get_Z_Rotation RW 0xB261F0), the machine's goal object cleared (vslot 0x38(0)); with a PhysicsBehavior and a locomotor it is flung along the flyer's facing at
// half the aerial speed and -6 down (RW 0xC2E194; PhysicsBehavior RW 0x792DBD (v, 0, 0), stunned RW 0x792A69(1)), TossFX on it (RW 0x4B1B5A), and with flag 8
// the flyer scores the kill (RW 0x6955BC(rider, 1)) and the flight ends in its death (physics + 0x5F); FREEFALL, UNATTACKABLE cleared, a non-HERO's body made
// destructible again (body vslot 0x88(0)); the victim (+ 0x584) and the clock are 0
void GiantBirdAIUpdate::toss()
{
	if (!((m_flight.flags >> 6) & 1u))
	{
		return;
	}
	Object &o = *getObject();
	float height = 0.0f;
	const float above = heightAboveTerrain(o);
	if (above > 0.0f)
	{
		height = above;
	}
	ContainModuleInterface *contain = o.getContain();
	if (!contain || contain->getContainCount() == 0)
	{
		m_grabClock = 0.0f;
		return;
	}
	m_grabClock = SimMath::addf32(SimMath::divf32(1.0f, (float)LOGICFRAMES_PER_SECOND), m_grabClock);
	const float timeTrigger = m_data.grabTossTimeTrigger, heightTrigger = m_data.grabTossHeightTrigger;
	if (!((0.0f < timeTrigger && timeTrigger < m_grabClock) || (0.0f < heightTrigger && heightTrigger < height)))
	{
		return;
	}
	m_flight.flags &= ~0x40u;
	const ContainModuleInterface::ContainedItemsList *items = contain->getContainedItemsList();
	if (!items)
	{
		return;
	}
	const std::vector<Object *> held(items->begin(), items->end()); // RW 0x76CB49: the walk runs on a copy
	for (Object *item : held)
	{
		if (item->getID() != m_rider)
		{
			continue;
		}
		m_rider = INVALID_ID;
		contain->removeFromContain(item);
		item->setOrientation(o.getOrientation());
		stateMachine().setGoalObject(INVALID_ID);
		PhysicsBehavior *physics = PhysicsBehavior::find(*item);
		if (physics && curLocomotor())
		{
			const float *b = o.getBasis();
			const float half = SimMath::mulf32(m_flight.speed, 0.5f); // RW 0xBD869C
			const Coord3D v{ SimMath::mulf32(half, b[0]), SimMath::mulf32(half, b[3]), -6.0f };
			physics->fling(v, 0, 0);
			physics->setStunned(true);
			if (!m_data.tossFX.empty())
			{
				FXEvent e = FXEventLog::objectEvent(FXEvent::OBJECT_FX, "GiantBirdTossFX", o.logic().getFrame(), m_data.tossFX, *item);
				e.secondary = o.getID();
				o.logic().fxEvents().emit(e);
			}
			if ((m_flight.flags >> 8) & 1u)
			{
				o.scoreTheKill(*item, 1);
				physics->setKillOnRest(true);
			}
		}
		setCondition(*item, names().freefall);
		item->setStatus((unsigned)names().unattackable, false);
		static const int kHero = kindBit("HERO");
		if (!isKind(*item, kHero))
		{
			if (BodyModuleInterface *body = item->getBodyModule())
			{
				body->setIndestructible(false);
			}
		}
		m_victim = INVALID_ID;
		m_grabClock = 0.0f;
		break;
	}
}

UpdateSleepTime GiantBirdAIUpdate::update()
{
	Object &o = *getObject();
	const BirdNames &n = names();
	o.setStatus((unsigned)n.updatingAI, true);
	if (!m_attacking && m_deferredValid)
	{
		// RW 0x76CE3A .. 0x76CE7F: the deferred command goes through aiDoCommand again
		RoutedCommand c = m_deferred;
		c.target = m_deferredTarget != INVALID_ID ? o.logic().findObjectByID(m_deferredTarget) : nullptr;
		m_deferredValid = false;
		if (c.type == kDeferredFollowPath)
		{
			privateFollowPath(c.path, c.target, c.source, false); // lane STEALTHFLY1: the follow paths go to their vslot again
		}
		else if (c.type == kDeferredFollowPathAppend)
		{
			privateFollowPathAppend(c.position, c.source);
		}
		else if ((c.type != 0xB && c.type != 0xC && c.type != 0x39) || c.target)
		{
			routeCommand(c);
		}
	}
	toss(); // RW 0x76CB71
	m_flight.turnRate = SimMath::mulf32(m_flight.turnRate, 0.8f); // RW 0xBDE8D8
	m_flight.speed = SimMath::mulf32(m_flight.speed, 0.9f);       // RW 0xBDE198
	bool airborne = false;
	if (const Locomotor *loco = curLocomotor())
	{
		airborne = !(heightAboveTerrain(o) <= (float)loco->getTemplate().m_airborneTargetingHeight);
	}
	o.setStatus((unsigned)n.airborneTarget, airborne);
	const StateReturnType st = stateMachine().updateStateMachine();
	unsigned sleep = 2;
	{
		const int r = IS_STATE_SLEEP(st) ? (int)GET_STATE_SLEEP_FRAMES(st) : 0;
		if (r < 1)
		{
			sleep = 1;
		}
		else if (r < 2)
		{
			sleep = (unsigned)r;
		}
	}
	if (o.isEffectivelyDead())
	{
		o.setStatus((unsigned)n.updatingAI, false);
		return (UpdateSleepTime)1;
	}
	// the collision push (+ 0x630, written by onCollide) is never set here (S-4040); the pushed flag rebuilds the path once
	if (m_collisionPushed)
	{
		m_collisionPushed = false;
		buildPath(m_flight.anchor, AerialPathfinder::MASK_A_LOWER_START, nullptr, false);
	}
	m_lastPosition = *o.getPosition();
	// RW 0x76D1D9 .. 0x76D23F, by the current state: idle hovers (goal 3); 0x10, 0x11, 0x17, 0x1F and 0x3F7 .. 0x3FA fly a path (goal 1); 0x3FC / 0x3FF turn
	// (goal 2); 0x3E9 .. 0x3EC, 0x3EE, 0x3EF, 0x3F1 .. 0x3F3, 0x3F5, 0x3F6, 0x3FB, 0x3FD, 0x400 keep their goal; doLocomotor runs for all of these. The attack
	// states 0x3ED / 0x3F4 sleep 1 without it; anything else (0x3F0, 0x3FE, the other base states) neither
	const unsigned id = currentStateId();
	bool locomote = false;
	switch (id)
	{
	case AI_IDLE:
		m_goalType = 3;
		locomote = true;
		break;
	case 0x10:
	case 0x11:
	case 0x17:
	case 0x1F:
	case 0x3F7:
	case 0x3F8:
	case 0x3F9:
	case 0x3FA:
		m_goalType = 1;
		locomote = true;
		break;
	case 0x3FC:
	case 0x3FF:
		m_goalType = 2;
		locomote = true;
		break;
	case STATE_ATTACK_POSITION:
	case STATE_ATTACK:
		sleep = 1;
		break;
	case STATE_FLY_TO:
	case STATE_FLY_TO_ABOVE:
	case STATE_SWOOP_POSITION:
	case STATE_SWOOP_POSITION_LEVEL:
	case STATE_FOLLOW_THRU_POSITION:
	case STATE_SWOOP_AGAIN:
	case STATE_FOLLOW_THRU_AGAIN:
	case STATE_SWOOP:
	case STATE_SWOOP_LEVEL:
	case STATE_FOLLOW_THRU:
	case 0x3F6:
	case STATE_ATTACK_MOVE:
	case 0x3FD:
	case STATE_FLY_400:
		locomote = true;
		break;
	default:
		break;
	}
	if (locomote)
	{
		const unsigned r = doLocomotor();
		if (r < sleep)
		{
			sleep = r;
		}
	}
	o.setStatus((unsigned)n.updatingAI, false);
	return (UpdateSleepTime)sleep;
}

// lane SAVE-4c (obfme:AIWorld): retail GiantBirdAIUpdate::xfer RW 0x76C96B is (1, 2) (AIUpdateInterface's, then its own fields, docs/SAVES-RETAIL.md);
// OpenBFME layout (S-4220): the base's, then crc's fields with the deferred command (its target as the id it is looked up by when re-issued), the
// collision push and the diagnostics' counters. A load fills a fresh spline first (staged) and checks the closed sets
void GiantBirdAIUpdate::xferAIStateBody(Xfer &x)
{
	AIUpdateInterface::xferAIStateBody(x);
	XferVersion v{ 1, 2 }; // 2 (lane STEALTHFLY1): the loop flag and a deferred follow path's points
	x.xferVersion(v);
	const bool loading = x.isLoading();
	AerialSpline spline;
	AerialSpline &sp = loading ? spline : m_spline;
	sp.xfer(x);
	x.xferInt(&m_goalType);
	x.xferReal(&m_goalAngle);
	x.xferReal(&m_pathDistance);
	x.xferBool(&m_reached);
	x.xferReal(&m_flight.speed);
	x.xferReal(&m_flight.turnRate);
	x.xferUnsignedInt(&m_flight.flags);
	AIXfer::coord(x, m_flight.anchor);
	x.xferReal(&m_pathHeight);
	AIXfer::objectId(x, m_victim, "giant bird: the object id victim");
	AIXfer::objectId(x, m_rider, "giant bird: the object id rider");
	x.xferReal(&m_grabClock);
	AIXfer::objectId(x, m_swoopHorde, "giant bird: the object id swoopHorde");
	x.xferBool(&m_attacking);
	x.xferInt(&m_duelMode);
	AIXfer::coord(x, m_lastPosition);
	AIXfer::coord(x, m_collisionPush);
	x.xferBool(&m_collisionPushed);
	x.xferBool(&m_deferredValid);
	x.xferInt(&m_deferred.type);
	AIXfer::coord(x, m_deferred.position);
	AIXfer::enumIn(x, m_deferred.source, CMD_FROM_PLAYER, CMD_FROM_AI, "giant bird: the deferred command's source");
	x.xferInt(&m_deferred.waypointId);
	AIXfer::objectId(x, m_deferredTarget, "giant bird: the object id deferredTarget");
	if (v.current >= 2)
	{
		x.xferBool(&m_loopPath);
		unsigned count = (unsigned)m_deferred.path.size();
		x.xferUnsignedInt(&count);
		if (loading)
		{
			AIXfer::inRange((long long)count, 0, 100000, "giant bird: the deferred path's points");
			m_deferred.path.assign(count, Coord3D{ 0.0f, 0.0f, 0.0f });
		}
		for (Coord3D &p : m_deferred.path)
		{
			AIXfer::coord(x, p);
		}
	}
	else if (loading)
	{
		m_loopPath = false;
		m_deferred.path.clear();
	}
	AIXfer::u64(x, m_swoopsStarted);
	AIXfer::u64(x, m_strikes);
	if (!loading)
	{
		return;
	}
	AIXfer::inRange(m_goalType, 0, 3, "giant bird: the goal type");
	AIXfer::inRange(m_duelMode, 0, 2, "giant bird: the duel mode");
	AIXfer::inRange(m_deferred.source, CMD_FROM_PLAYER, CMD_FROM_AI, "giant bird: the deferred command's source");
	if (m_deferredValid && !deferrable(m_deferred.type))
	{
		throw XferException("giant bird: the deferred command " + std::to_string(m_deferred.type) + " is not one the bird routes");
	}
	m_deferred.target = nullptr; // looked up by m_deferredTarget when re-issued (RW 0x76CE3A)
	m_spline = std::move(spline);
}

void GiantBirdAIUpdate::crc(StateHasher &h) const
{
	AIUpdateInterface::crc(h);
	m_spline.crc(h);
	h.addI32(m_goalType);
	h.addFloat(m_goalAngle);
	h.addFloat(m_pathDistance);
	h.addBool(m_reached);
	h.addFloat(m_flight.speed);
	h.addFloat(m_flight.turnRate);
	h.addU32(m_flight.flags);
	h.addFloat(m_flight.anchor.x);
	h.addFloat(m_flight.anchor.y);
	h.addFloat(m_flight.anchor.z);
	h.addFloat(m_pathHeight);
	h.addU32(m_victim);
	h.addU32(m_rider);
	h.addFloat(m_grabClock);
	h.addU32(m_swoopHorde);
	h.addBool(m_attacking);
	h.addI32(m_duelMode);
	h.addFloat(m_lastPosition.x);
	h.addFloat(m_lastPosition.y);
	h.addFloat(m_lastPosition.z);
	h.addBool(m_collisionPushed);
	if (m_loopPath)
	{
		h.addBool(m_loopPath); // lane STEALTHFLY1: only a looping waypoint path (the hash of every other bird is unchanged)
	}
	h.addBool(m_deferredValid);
	if (m_deferredValid)
	{
		h.addI32(m_deferred.type);
		h.addU32(m_deferredTarget);
		h.addFloat(m_deferred.position.x);
		h.addFloat(m_deferred.position.y);
		h.addFloat(m_deferred.position.z);
		h.addI32((int)m_deferred.source);
		for (const Coord3D &p : m_deferred.path)
		{
			h.addFloat(p.x);
			h.addFloat(p.y);
			h.addFloat(p.z);
		}
	}
}

std::vector<std::string> GiantBirdAIUpdate::stopLines()
{
	return { "S-4040: GiantBirdAIUpdate (lanes FLY-1, STEALTHFLY1) is ported without the states 0x3FD / 0x3FE and their commands 0x23 / 0x41 (no ported caller), "
		     "vtable slot 0x68 (RW 0x76D778), the guard machine and command, the MACHINE victim's CreateObjectDie carry, the collision push of onCollide, a "
		     "deferred command other than the routed ones and the follow paths, the weapon's PreferredTargetBone aim (the "
		     "contact point is used) and Object::adjustVictim's member choice (the nearest living member, INFERENCE); the attack-move's mood re-check clock (RW 0x662E01) "
		     "is the MoodAttackCheckRate interval (INFERENCE)" };
}

// lane MODCOMPAT-MOD1: AI vslot 0x164 (RW 0x76D3B1), run when a temporary state machine ends (RW 0x66311F): not ported (S-4040)
void GiantBirdAIUpdate::onTemporaryStateMachineEnded()
{
	world().noteStop("[S-4040] GiantBirdAIUpdate: vslot 0x164 (RW 0x76D3B1), run when the AI's temporary state machine ends (RW 0x6630D7), is not ported");
}
