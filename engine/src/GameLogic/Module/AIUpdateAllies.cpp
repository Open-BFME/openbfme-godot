// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Lane PHYS-1: allied clearing and the attack path's weapon test (ZH AIUpdate.cpp privateMoveAwayFromUnit / AIPathfind.cpp moveAllies; RotWK bodies below).
//
// TARGET FACTS (RotWK game.dat, caveat S-001; lane PHYS-1 review r1 / r2):
//   * the clearing gate after a new path (computePath RW 0x66670F, computeAttackPath RW 0x666EEA .. 0x666F67, doPathfind RW 0x669459, move-away RW 0x66DDF6): the path
//     must be blocked by an ally (path + 0xD) and the unit not NO_COLLIDE; a HORDE needs AIData HordesWaitForHordes (+0xB9); CrushAllies (template + 0x624) or
//     PATH_THROUGH_INFANTRY forces it; RAMPAGING, FLEE_OFF_MAP or the model condition CHARGING forbid it; then RW 0x6F503B(unit, path, crushableLevel > 3);
//   * moveAllies RW 0x6F503B: a DOZER, a HARVESTER, a PATH_THROUGH_INFANTRY unit or a path blocked by an ally, and not KindOf 187; a recursion guard (pathfinder
//     + 0x1C1BC, below 2); for each path segment (first node to the last, the cells of RW 0x6E8CE6 centred, n = max(|dx|, |dy|) steps of k * d / n) the footprint
//     cells (RW 0x6ED0A0: N / 2 below, N - N / 2 above) and their stationary occupants (cell info + 0x20, the position list): another unit, not the ignored obstacle,
//     not both PATH_THROUGH_EACH_OTHER, not MOVE_FOR_NOONE, ALLIES (RW 0x68D7AB == 2), a HORDE only for a non-HORDE mover; a moving one or one without an AI is
//     skipped; the others are asked to move away (RW 0x66C66E, AI command 0x34 -> RW 0x66DA5F) unless IS_LEAVING_FACTORY, the first of each cell's list only;
//   * the move-away handler RW 0x66DA5F: see aiMoveAwayFromUnit.
// TARGET (review r3): the base AI vtable 0xC10590 slots + 0x1B0 (RW 0x8BD372: true), + 0x1B8 (RW 0x6643FC: isIdle) and + 0x1BC (RW 0x662B3C: the current state's isAttack).
// INFERENCE (stop S-785):
// RW 0x6F503B's model condition 154 test and its HORDE test against Object + 0x78 (taken as the unit's container) are approximations; the contained-horde forwarding
// of RW 0x66DA5F (container vslot 0x1D0) and the collision-ignore timer of a repeated requester (AI + 0x178, flag + 0x3B8) are not ported; the locomotor call
// RW 0x5E39CE and the temporary-state clear RW 0x741675 are not ported; AI command 0x34 is called directly (no command record).

#include "GameLogic/Module/AIUpdate.h"

#include "Common/GameCommon.h"
#include "Common/StateHash.h"
#include "GameLogic/AI/AIStateMachine.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/SquishCollide.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/SimMath.h"

#include <cstdlib>
#include <stdexcept>
#include <string>

namespace
{
const char *const kStopAllies =
	"[S-785] allied clearing: moveAllies (RW 0x6F503B), the move-away handler (RW 0x66DA5F), getMoveAwayFromPath (RW 0x6FB231) and AI_MOVE_OUT_OF_THE_WAY (26) are ported; "
	"the AI vslots 0x1B0 / 0x1B8 / 0x1BC are true / isIdle / isAttack (subclass overrides not ported); INFERENCE: RW 0x67C81E is ZH's LineInRegion, the contained-horde "
	"forwarding (container vslot 0x1D0), the repeated-requester collision timer, RW 0x5E39CE and RW 0x741675 are not ported, command 0x34 is a direct call";

int kindIndex(const char *name)
{
	const int b = ObjectTemplateInfoBuilder::kindOfIndex(name);
	if (b < 0)
	{
		throw std::logic_error(std::string("KindOf registry has no ") + name);
	}
	return b;
}

int statusIndexOf(const char *name)
{
	const int b = ObjectTemplateInfoBuilder::objectStatusIndex(name);
	if (b < 0)
	{
		throw std::logic_error(std::string("object status registry has no ") + name);
	}
	return b;
}

struct AllyBits
{
	int dozer = kindIndex("DOZER"), harvester = kindIndex("HARVESTER"), noCollide = kindIndex("NO_COLLIDE"), horde = kindIndex("HORDE");
	int pathThroughInfantry = kindIndex("PATH_THROUGH_INFANTRY"), pathThroughEachOther = kindIndex("PATH_THROUGH_EACH_OTHER"), moveForNoone = kindIndex("MOVE_FOR_NOONE");
	int kind187 = 187; // template + 0x11F bit 3 (RW 0x6F503B)
	int rampaging = statusIndexOf("RAMPAGING"), fleeOffMap = statusIndexOf("FLEE_OFF_MAP"), leavingFactory = statusIndexOf("IS_LEAVING_FACTORY");
	int inFormationTemplate = statusIndexOf("IN_FORMATION_TEMPLATE");
	int charging = CombatNames::modelCondition("CHARGING");
};

const AllyBits &bits()
{
	static const AllyBits b;
	return b;
}

bool kind(const Object &o, int bit)
{
	return o.isKindOf((unsigned)bit);
}

// RW vslot 0x1B8 (review r3: base AI vtable 0xC10590 slot + 0x1B8 is RW 0x6643FC, isIdle, with its horde-member / container rule)
bool canBeCleared(const AIUpdateInterface &ai)
{
	return ai.isIdle();
}

// AI_MOVE_OUT_OF_THE_WAY (26): the installed move-away path, followed to its end
class AIMoveOutOfTheWayState : public AIState
{
public:
	explicit AIMoveOutOfTheWayState(AIStateMachine &m)
		: AIState(m, "AIMoveOutOfTheWayState")
	{
	}
	StateReturnType onEnter() override
	{
		AIMover &mv = ai().mover();
		if (mv.path() == nullptr)
		{
			return STATE_FAILURE;
		}
		mv.setGoalOnPath();
		mv.setDesiredSpeed(AI_FAST_SPEED);
		mv.startingMove();
		return STATE_CONTINUE;
	}
	StateReturnType update() override
	{
		AIMover &mv = ai().mover();
		if (mv.path() == nullptr)
		{
			return STATE_SUCCESS;
		}
		if (mv.locomotorDistanceToGoal() < ai().closeEnoughDist())
		{
			mv.setGoalNone();
			return STATE_SUCCESS;
		}
		return STATE_CONTINUE;
	}
	void onExit(StateExitType) override
	{
		if (!ai().isDestroying())
		{
			ai().mover().endingMove();
		}
	}
};
} // namespace

std::unique_ptr<AIState> makeMoveOutOfTheWayState(AIStateMachine &machine)
{
	return std::make_unique<AIMoveOutOfTheWayState>(machine);
}

// ---- the attack path's hooks ----------------------------------------------------------------------------------------------------
bool AIUpdateInterface::attackRangeFrom(const Coord3D &from, PathfindObjectID victim, const Coord3D &victimPos, float extra)
{
	const ObjectWeapons *w = getObject()->getWeapons();
	if (!w)
	{
		return false;
	}
	const Object *v = victim != PATHFIND_INVALID_ID ? getObject()->logic().findObjectByID((ObjectID)victim) : nullptr;
	return w->isWithinAttackRangeFrom(from, v, v ? *v->getPosition() : victimPos, extra, true);
}

bool AIUpdateInterface::attackRangeFromTo(const Coord3D &from, PathfindObjectID victim, const Coord3D &victimPos, float extra)
{
	const ObjectWeapons *w = getObject()->getWeapons();
	if (!w)
	{
		return false;
	}
	const Object *v = victim != PATHFIND_INVALID_ID ? getObject()->logic().findObjectByID((ObjectID)victim) : nullptr;
	return v ? w->isWithinAttackRangeFromTo(from, *v, victimPos, extra, true) : w->isWithinAttackRangeFrom(from, nullptr, victimPos, extra, true);
}

const PathfindObject *AIUpdateInterface::findPathfindObject(PathfindObjectID id)
{
	return m_world ? m_world->findObjectByID(id) : nullptr;
}

void AIUpdateInterface::noteLogicStop(const std::string &line)
{
	getObject()->logic().noteStop(line);
}

// ---- the clearing gate (RW 0x666EEA .. 0x666F67) ---------------------------------------------------------------------------------
void AIUpdateInterface::clearAlliesFromPath(Path &path)
{
	if (!m_world)
	{
		return;
	}
	Object &obj = *getObject();
	const AllyBits &b = bits();
	bool clear = path.getBlockedByAlly() && !kind(obj, b.noCollide);
	if (kind(obj, b.horde) && !m_world->config().pathfind.hordesWaitForHordes)
	{
		clear = false;
	}
	if (CrushTemplateInfo::of(*obj.getTemplate()).crushAllies || kind(obj, b.pathThroughInfantry))
	{
		clear = true;
	}
	if (obj.testStatus((unsigned)b.rampaging) || obj.testStatus((unsigned)b.fleeOffMap) || obj.testModelCondition(b.charging))
	{
		clear = false;
	}
	if (clear)
	{
		m_world->moveAllies(*this, path, ObjectCrush::crushableLevel(obj) > 3);
	}
}

// ---- moveAllies (RW 0x6F503B) ----------------------------------------------------------------------------------------------------
bool AIWorld::moveAllies(AIUpdateInterface &mover, Path &path, bool force)
{
	Object &obj = *mover.getObject();
	const AllyBits &b = bits();
	const bool pathThroughInf = kind(obj, b.pathThroughInfantry);
	if ((!kind(obj, b.dozer) && !kind(obj, b.harvester) && !pathThroughInf && !path.getBlockedByAlly()) || kind(obj, b.kind187))
	{
		return false;
	}
	if (m_moveAlliesDepth + 1 >= 2)
	{
		return false;
	}
	++m_moveAlliesDepth;
	m_logic.noteStop(kStopAllies);
	Pathfinder &pf = *m_pathfinder;
	ObjectPathfindAdapter &self = mover.adapter();
	const int footprint = pf.footprintSize(&self);
	const int below = footprint / 2, above = footprint - below;
	const PathfindObjectID ignoreId = mover.mover().ignoredObstacleID();
	const Coord3D requesterPos = *path.getFirstNode()->getPosition();
	const Object *myContainer = obj.getContainedBy();
	for (PathNode *node = path.getFirstNode(); node && node != path.getLastNode() && node->getNext(); node = node->getNext())
	{
		ICoord2D c0, c1;
		Coord3D p0 = *node->getPosition(), p1 = *node->getNext()->getPosition();
		pf.worldToCell(&p0, true, &c0);
		pf.worldToCell(&p1, true, &c1);
		const int dx = c1.x - c0.x, dy = c1.y - c0.y;
		const int n = std::abs(dx) > std::abs(dy) ? std::abs(dx) : std::abs(dy);
		for (int k = 0; k < n; ++k)
		{
			const int x = (k * dx) / n + c0.x, y = (k * dy) / n + c0.y;
			for (int i = x - below; i < x + above; ++i)
			{
				for (int j = y - below; j < y + above; ++j)
				{
					const PathfindCell *cell = pf.getCell(node->getLayer(), i, j);
					if (cell == nullptr || !cell->hasInfo())
					{
						continue;
					}
					for (const PathfindOccupant *o = cell->occupants(OCC_POSITION); o; o = o->next)
					{
						Object *other = m_logic.findObjectByID((ObjectID)o->owner);
						if (other == nullptr || other == &obj || other->getID() == (ObjectID)ignoreId)
						{
							continue;
						}
						if (kind(obj, b.pathThroughEachOther) && kind(*other, b.pathThroughEachOther))
						{
							continue;
						}
						if (kind(*other, b.moveForNoone))
						{
							continue;
						}
						if (kind(*other, b.horde) && myContainer && other == myContainer)
						{
							continue;
						}
						if (self.getRelationship(adapterFor(*other)) != PATHFIND_ALLIES)
						{
							continue;
						}
						if (kind(obj, b.horde) && (kind(*other, b.horde) || other->getContainedBy() != nullptr))
						{
							continue;
						}
						AIUpdateInterface *oa = other->getAIUpdateInterface();
						bool ask;
						if (!pathThroughInf || kind(*other, b.pathThroughInfantry))
						{
							if (oa == nullptr || oa->isMoving())
							{
								continue;
							}
							ask = canBeCleared(*oa) || force;
						}
						else
						{
							ask = !(oa && (oa->isAttacking() || oa->isMoving())) || force;
							if (oa == nullptr)
							{
								ask = false;
							}
						}
						if (!ask)
						{
							continue;
						}
						if (!other->testStatus((unsigned)b.leavingFactory))
						{
							oa->aiMoveAwayFromUnit(&obj, requesterPos, CMD_FROM_AI);
							break;
						}
					}
				}
			}
		}
	}
	--m_moveAlliesDepth;
	return true;
}

// ---- the move-away handler (RW 0x66C66E -> 0x66DA5F) ----------------------------------------------------------------------------
void AIUpdateInterface::aiMoveAwayFromUnit(Object *unit, const Coord3D &requesterPos, CommandSourceType source)
{
	if (!acceptCommand(source, 0x34))
	{
		return; // lane MODULES-3: RW 0x667174
	}
	Object &obj = *getObject();
	// RW 0x66DA5F: the unit, AI + 0x3BD, mobility (RW 0x690E97), then vslot 0x1B0 (review r3: the base slot RW 0x8BD372 returns true; no subclass override is ported)
	if (unit == nullptr || isAiInDeadState() || isImmobile() || !m_world || !m_machine)
	{
		return;
	}
	const AllyBits &b = bits();
	if (obj.testStatus((unsigned)b.inFormationTemplate) && currentStateId() == AI_IDLE)
	{
		if (AIUpdateInterface *ua = unit->getAIUpdateInterface())
		{
			ua->setCanPathThroughUnits(true); // RW: the requester's + 0x3BA
		}
		return;
	}
	const ObjectID unitId = unit->getID();
	if (m_machine->temporaryStateId() == AI_MOVE_OUT_OF_THE_WAY || m_machine->currentStateId() == AI_MOVE_OUT_OF_THE_WAY)
	{
		if (m_moveAwayRequesters[0] == unitId || m_moveAwayRequesters[1] == unitId)
		{
			return;
		}
	}
	m_moveAwayRequesters[1] = m_moveAwayRequesters[0];
	m_moveAwayRequesters[0] = unitId;
	Object *prev = m_moveAwayRequesters[1] != INVALID_ID ? obj.logic().findObjectByID(m_moveAwayRequesters[1]) : nullptr;
	const Path *prevPath = prev && prev->getAIUpdateInterface() ? prev->getAIUpdateInterface()->mover().path() : nullptr;
	const Path *unitPath = unit->getAIUpdateInterface() ? unit->getAIUpdateInterface()->mover().path() : nullptr;
	Pathfinder &pf = m_world->pathfinder();
	const PathfindObject *unitPf = &m_world->adapterFor(*unit);
	const PathfindObject *prevPf = prev ? &m_world->adapterFor(*prev) : nullptr;
	Path *path = pf.getMoveAwayFromPath(&adapter(), locomotorInfo(), unitPf, unitPath, prevPf, prevPath);
	if (path == nullptr && !canPathThroughUnits())
	{
		setCanPathThroughUnits(true);
		path = pf.getMoveAwayFromPath(&adapter(), locomotorInfo(), unitPf, unitPath, prevPf, prevPath);
	}
	if (path == nullptr)
	{
		return;
	}
	// the unit's own goal: far from the requester's start -> a temporary state that returns to the order, else a new state
	Coord3D dest = m_machine->goalPosition();
	if (!m_machine->goalPath().empty())
	{
		dest = m_machine->goalPath().back();
	}
	const float r = CombatQueries::boundingCircleRadius(*unit);
	const float ex = SimMath::subf32(dest.x, requesterPos.x), ey = SimMath::subf32(dest.y, requesterPos.y), ez = SimMath::subf32(dest.z, requesterPos.z);
	const bool far = SimMath::mulf32(r, r) <= SimMath::addf32(SimMath::addf32(SimMath::mulf32(ez, ez), SimMath::mulf32(ey, ey)), SimMath::mulf32(ex, ex));
	m_mover->setPath(path);
	m_mover->setGoalOnPath();
	if (far)
	{
		m_machine->setTemporaryState(AI_MOVE_OUT_OF_THE_WAY, 10 * LOGICFRAMES_PER_SECOND);
	}
	else
	{
		m_machine->setState(AI_MOVE_OUT_OF_THE_WAY);
	}
	wakeUpNow();
	if (m_mover->path())
	{
		clearAlliesFromPath(*m_mover->path()); // RW 0x66DDF6: the recursive clearing
	}
}
