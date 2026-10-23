// OpenBFME. GPL-3.0.
// See GameLogic/AI/AIGroup.h.

#include "GameLogic/AI/AIGroup.h"

#include "GameLogic/AI/AIWorld.h"
#include "Common/Thing/KindOfTokens.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/PhysicsBehavior.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/InvisibilityModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/System/InvisibilityManager.h"

#include <algorithm>

namespace
{
const float kCellSize = 10.0f;                       // PATHFIND_CELL_SIZE_F
const float kWaypointClampMargin = 40.0f;             // ZH STD_WAYPOINT_CLAMP_MARGIN = PATHFIND_CELL_SIZE_F * 4 (exactly representable)
const int kLogicFramesPerSecond = 5;                  // RW 0xD9F608
}

AIGroup::AIGroup(GameLogic &logic, const std::vector<ObjectID> &ids)
	: m_logic(logic)
{
	for (ObjectID id : ids)
	{
		Object *o = logic.findObjectByID(id);
		if (o && !o->isDestroyed() && o->getAIUpdateInterface())
		{
			m_members.push_back(o);
		}
	}
}

Coord3D AIGroup::clampWaypointPosition(const Coord3D &pos) const
{
	Coord3D p = pos;
	AIWorld *ai = m_logic.aiWorld();
	if (!ai || !ai->mapReady())
	{
		return p;
	}
	ICoord2D lo, hi;
	ai->pathfinder().getLogicalExtent(lo, hi);
	const float x0 = SimMath::addf32(SimMath::mulf32((float)lo.x, kCellSize), kWaypointClampMargin);
	const float y0 = SimMath::addf32(SimMath::mulf32((float)lo.y, kCellSize), kWaypointClampMargin);
	const float x1 = SimMath::subf32(SimMath::mulf32((float)(hi.x + 1), kCellSize), kWaypointClampMargin);
	const float y1 = SimMath::subf32(SimMath::mulf32((float)(hi.y + 1), kCellSize), kWaypointClampMargin);
	if (!(p.x >= x0 && p.x <= x1 && p.y >= y0 && p.y <= y1))
	{
		if (p.x > x1) p.x = x1;
		else if (p.x < x0) p.x = x0;
		if (p.y > y1) p.y = y1;
		else if (p.y < y0) p.y = y0;
		p.z = m_logic.getGroundHeight(p.x, p.y);
	}
	return p;
}

const char *AIGroup::stopLine()
{
	return "S-223 AIGroup's force moves and waypoints (lane MOVE-3: not MSG_DO_MOVETO, S-1831) move with ZH's individual destinations (offset from the nearest unit clamped to "
		"six bounding radii) unless they march in formation (lane BACKLOG-8: the layout RW 0x94FCBA and the march RW 0x7747DE / 0x77348C, S-4460 / S-4462); "
		"the ZH column and tighten moves and the weapon locks are not ported; a formation move's drag direction becomes the units' final facing (inference)";
}

Coord3D AIGroup::computeIndividualDestination(const Coord3D &groupDest, Object &obj, const Coord3D &center) const
{
	const Coord3D &pos = *obj.getPosition();
	float vx = SimMath::subf32(pos.x, center.x), vy = SimMath::subf32(pos.y, center.y);
	float length = SimMath::length2d(vx, vy);
	AIWorld *world = m_logic.aiWorld();
	const float radius = world ? world->adapterFor(obj).getGeometry().boundingCircleRadius() : 0.0f;
	const float maxLength = SimMath::mulf32(6.0f, radius);
	if (length > maxLength)
	{
		length = maxLength;
	}
	// Coord2D::normalize: x /= len, y /= len when len != 0 (the length is the one before the clamp)
	const float len0 = SimMath::length2d(vx, vy);
	if (len0 != 0.0f)
	{
		vx = SimMath::divf32(vx, len0);
		vy = SimMath::divf32(vy, len0);
	}
	vx = SimMath::mulf32(vx, length);
	vy = SimMath::mulf32(vy, length);
	Coord3D dest;
	dest.x = SimMath::addf32(groupDest.x, vx);
	dest.y = SimMath::addf32(groupDest.y, vy);
	dest.z = m_logic.getGroundHeight(dest.x, dest.y);
	AIUpdateInterface *ai = obj.getAIUpdateInterface();
	if (world && world->mapReady() && ai && ai->isDoingGroundMovement())
	{
		Pathfinder &pf = world->pathfinder();
		pf.adjustDestination(world->adapterFor(obj), ai->locomotorInfo(), &dest, &groupDest);
		pf.updateGoal(world->adapterFor(obj), &dest, LAYER_GROUND);
	}
	return dest;
}

const char *AIGroup::formationStopLine()
{
	return "[S-895] AIGroup RW 0x774897 (lane BACKLOG6): a player-source group move of more than one unit without waypoint, target or formation type, with AIData "
		"UseFormations goes to the formation group manager (RW 0x7747DE), which is ported only as far as its refusals: the member filter RW 0x76FFF0 (a held, "
		"AI-less, locomotor-less or IMMOBILE member leaves the group; the last one ends the order), a group without a FormationPriority locomotor and the group "
		"ground path's distance and extent refusals (RW 0x76F115). NOT ported: RW 0x76F426's per-member contain call, the terrain tests (RW 0x680A75, "
		"W3DTerrainLogic slot 0xC8), the nearby enemy test (findClosestEnemy, GroupEnemyScanRange, qualifiers 0xE4), Pathfinder::FindGroundPath (its failure "
		"refuses the group in RW) and the formation itself (createFormation RW 0x94FCBA, moveFormationToPos RW 0x77348C): a group not refused by the ported "
		"tests is taken as accepted, moves with individual destinations and makes no stealth draw (RW's draw, AIGroup.cpp 0x874, belongs to the generic path "
		"after a refusal)";
}

// RW 0x76F115 (BFME2 decomp AI/AIGroupGetCenter.cpp AIGroup::computeGroundPath, byte-matched, tier B same-shape for RW 0x76F115): the checks after the
// terrain tests, in RW's order: the members' centre and extent (RW 0x76D14F: held members and members without an AI do not count), the squared distance of
// the nearest member to the point (starting at (MinDistanceForGroup * 4)^2), the member nearest the centre (none: refused), the extent replacing the
// distance when it is longer than that start, the refusal when the distance is below MinDistanceForGroup^2, and the member whose AI finds the ground path
// (INFANTRY, MONSTER, CAVALRY / MACHINE, never AIRCRAFT; none: refused). NOT ported, so never a refusal here: the terrain tests RW 0x680A75 (the layer; lane
// WALL-2 makes the layers exist, the test was not read) and W3DTerrainLogic slot 0xC8 (RW 0x4627C1: a height map bit plane at the point and at every path node), the nearby enemy test
// (AI::findClosestEnemy within GroupEnemyScanRange with qualifiers 0xE4) and Pathfinder::FindGroundPath itself (a missing path refuses the group in RW)
bool AIGroup::groupGroundPathAccepts(const Coord3D &pos) const
{
	static const int kInfantry = KindOfTokens::indexOf("INFANTRY"), kMonster = KindOfTokens::indexOf("MONSTER"), kCavalry = KindOfTokens::indexOf("CAVALRY"),
		kMachine = KindOfTokens::indexOf("MACHINE"), kAircraft = KindOfTokens::indexOf("AIRCRAFT");
	const float minDistance = m_logic.settings().minDistanceForGroup;
	// RW 0x76D14F: the extent (its centre only feeds the ground path's start)
	float minX = 1e10f, maxX = -1e10f, minY = 1e10f, maxY = -1e10f;
	for (const Object *o : m_members)
	{
		if (o->isDisabledByType(DISABLED_HELD) || !o->getAIUpdateInterface())
		{
			continue;
		}
		const Coord3D &p = *o->getPosition();
		minX = minX > p.x ? p.x : minX;
		maxX = maxX < p.x ? p.x : maxX;
		minY = minY > p.y ? p.y : minY;
		maxY = maxY < p.y ? p.y : maxY;
	}
	const float start = SimMath::mulf32(SimMath::mulf32(minDistance, 4.0f), SimMath::mulf32(minDistance, 4.0f));
	float distSqr = start;
	bool haveCenter = false, haveObj = false;
	for (const Object *o : m_members)
	{
		if (o->isDisabledByType(DISABLED_HELD) || !o->getAIUpdateInterface() || (kAircraft >= 0 && o->isKindOf((unsigned)kAircraft)))
		{
			continue;
		}
		haveObj = haveObj || (kInfantry >= 0 && o->isKindOf((unsigned)kInfantry)) || (kMonster >= 0 && o->isKindOf((unsigned)kMonster)) ||
			(kCavalry >= 0 && o->isKindOf((unsigned)kCavalry)) || (kMachine >= 0 && o->isKindOf((unsigned)kMachine));
		const Coord3D &p = *o->getPosition();
		const float dx = SimMath::subf32(p.x, pos.x), dy = SimMath::subf32(p.y, pos.y);
		const float d = SimMath::addf32(SimMath::mulf32(dx, dx), SimMath::mulf32(dy, dy));
		if (d < distSqr)
		{
			distSqr = d;
		}
		haveCenter = true; // RW's nearest-to-the-centre member only feeds the ground path's start (not ported); one qualifying member is all the refusal needs
	}
	if (!haveCenter)
	{
		return false; // RW: no member to take the ground path from
	}
	const float ddx = SimMath::subf32(maxX, minX), ddy = SimMath::subf32(maxY, minY);
	const float extent = SimMath::addf32(SimMath::mulf32(ddx, ddx), SimMath::mulf32(ddy, ddy));
	if (extent > start)
	{
		distSqr = extent;
	}
	if (distSqr < SimMath::mulf32(minDistance, minDistance))
	{
		return false;
	}
	return haveObj;
}

AIGroup::FormationVerdict AIGroup::formationManager(const Coord3D &pos, bool addWaypoint, CommandSourceType source, bool haveFormationType)
{
	// RW 0x77494B .. 0x774986 (BFME2 decomp AI/AIGroupRva00372571.cpp, byte-matched, tier B same-shape for RW 0x774897: the differences are Object / AI offsets
	// and the __FILE__ line 0x874): more than one unit, no waypoint, the player's source, no formation type, AIData UseFormations
	if (!(m_members.size() > 1) || addWaypoint || source != CMD_FROM_PLAYER || haveFormationType || !m_logic.settings().useFormations)
	{
		return FORMATION_NOT_OFFERED;
	}
	// RW 0x7747DE (decomp Common/BfmeConv986.cpp privateGatherAndMoveInFormationToPosition, tier A). First RW 0x76FFF0 (decomp Common/Rva0036E346Count.cpp
	// AIGroup::rva0036E357, tier B same-shape): a member that is held (object + 0x1C8 bit 3), has no AI or no current locomotor (AI + 0x1F0) or is IMMOBILE
	// (template + 0x108 bit 2) leaves the group, and the walk starts again; the last member in the group that would leave stops it (the manager then returns 1
	// and nothing moves)
	static const int kImmobile = KindOfTokens::indexOf("IMMOBILE");
	for (size_t i = 0; i < m_members.size();)
	{
		const Object *o = m_members[i];
		const AIUpdateInterface *ai = o->getAIUpdateInterface();
		const bool refuse = o->isDisabledByType(DISABLED_HELD) || !ai || !ai->curLocomotor() || (kImmobile >= 0 && o->isKindOf((unsigned)kImmobile));
		if (!refuse)
		{
			++i;
			continue;
		}
		if (m_members.size() == 1)
		{
			return FORMATION_NOTHING;
		}
		m_members.erase(m_members.begin() + (long)i);
		i = 0;
	}
	// RW 0x7747F2 .. 0x774813: no member whose current locomotor's template has a FormationPriority (+ 0x78): the manager returns 0
	bool priority = false;
	for (const Object *o : m_members)
	{
		const AIUpdateInterface *ai = o->getAIUpdateInterface();
		const Locomotor *loco = ai ? ai->curLocomotor() : nullptr;
		priority = priority || (loco && loco->getTemplate().m_formationPriority != FORMATION_NO_FORMATION);
	}
	if (!priority)
	{
		return FORMATION_REJECTED;
	}
	// RW 0x774822 (RW 0x76F426: every member's contain interface vslot 0xD0 is not ported), then RW 0x76F115
	return groupGroundPathAccepts(pos) ? FORMATION_TAKEN : FORMATION_REJECTED;
}

// RW 0x774C6D .. 0x774D04 (ZH AIGroup::groupMoveToPosition's "delay mood checks so the unit can restealth"): a player-source order to a unit with the
// CAN_STEALTH status (0x12) whose AI auto-acquires (module data + 0x1C != 0) but not while stealthed (bit 1), that is not invisible (RW 0x68FC2F), STEALTHED
// (0xF) or DETECTED (0x11) and has a StealthUpdate (RW 0x68FBD3) draws GetGameLogicRandomValue(0, LOGICFRAMES_PER_SECOND) at AIGroup.cpp 0x874 and sets the
// AI's next mood check (RW 0x662E01) to frame + draw + StealthDelay (module data + 8)
void AIGroup::delayMoodCheckForStealth(Object &obj, AIUpdateInterface &ai)
{
	static const int kCanStealth = ObjectTemplateInfoBuilder::objectStatusIndex("CAN_STEALTH");
	static const int kStealthed = ObjectTemplateInfoBuilder::objectStatusIndex("STEALTHED");
	static const int kDetected = ObjectTemplateInfoBuilder::objectStatusIndex("DETECTED");
	if (kCanStealth < 0 || !obj.testStatus((unsigned)kCanStealth) || ai.autoAcquireFlags() == 0)
	{
		return;
	}
	if (InvisibilityManager::isInvisible(obj) || (kStealthed >= 0 && obj.testStatus((unsigned)kStealthed)) || (kDetected >= 0 && obj.testStatus((unsigned)kDetected)))
	{
		return;
	}
	if ((ai.autoAcquireFlags() >> 1) & 1u)
	{
		return;
	}
	const StealthUpdate *stealth = StealthUpdate::of(obj);
	if (!stealth)
	{
		return;
	}
	const unsigned delay = stealth->data()->m_stealthDelay;
	const unsigned draw = (unsigned)m_logic.random().getValue(0, kLogicFramesPerSecond, "AIGroup.cpp", 0x874);
	ai.setNextMoodCheckTime(m_logic.getFrame() + draw + delay); // RW 0x774CF0 .. 0x774CFB: 32-bit adds
}

void AIGroup::groupMoveToPosition(const Coord3D &posIn, bool addWaypoint, CommandSourceType source, bool haveFinalAngle, float finalAngle, bool attackMove)
{
	AIWorld *world = m_logic.aiWorld();
	if (!world || m_members.empty())
	{
		return;
	}
	world->noteStop(stopLine());
	// lane BACKLOG6: RW 0x774897's formation group manager: a group it takes (lane BACKLOG-8 r2: and that is not an attack-move and is not a click inside its own
	// gather rectangle) marches in formation; otherwise it keeps the port's stand-in move and makes no stealth draw; one it refuses runs the generic path below
	// (its draw) on the members the manager's filter left; a group whose last member the filter refuses does nothing
	bool managed = false;
	bool marchGroup = !attackMove && source == CMD_FROM_PLAYER;
	if (marchGroup)
	{
		// lane BACKLOG-8 r2, RW 0x7748C1 .. 0x77498D: the tighten test (a click inside the group's rectangle scaled by GameData GroupMoveClickToGatherAreaFactor
		// about its middle, RW 0x65F4DC / 0x65F4A2) sends the group past the manager to the generic path
		const GameLogicSettings &gs = m_logic.settings();
		if (gs.groupMoveClickToGatherAreaFactor > 0.0f && m_members.size() > 1)
		{
			Coord2D lo, hi;
			Coord3D c;
			getMinMaxAndCenter(lo, hi, c);
			const float f = SimMath::mulf32(SimMath::subf32(gs.groupMoveClickToGatherAreaFactor, 1.0f), 0.5f);
			const float dx = SimMath::mulf32(SimMath::subf32(hi.x, lo.x), f), dy = SimMath::mulf32(SimMath::subf32(hi.y, lo.y), f);
			lo.x = SimMath::subf32(lo.x, dx);
			lo.y = SimMath::subf32(lo.y, dy);
			hi.x = SimMath::addf32(hi.x, dx);
			hi.y = SimMath::addf32(hi.y, dy);
			marchGroup = !(posIn.x >= lo.x && hi.x >= posIn.x && posIn.y >= lo.y && hi.y >= posIn.y);
		}
	}
	const bool tightened = !attackMove && source == CMD_FROM_PLAYER && !marchGroup;
	switch (tightened ? FORMATION_NOT_OFFERED : formationManager(posIn, addWaypoint, source, haveFinalAngle))
	{
	case FORMATION_NOTHING:
		world->noteStop(formationStopLine());
		return;
	case FORMATION_TAKEN:
		world->noteStop(formationStopLine());
		// the march (RW 0x7747DE's ground path, createFormation and moveFormationToPos, S-4462): a refusal by its ground path (the terrain, the nearby enemy,
		// no path) is the manager's refusal: the generic path below
		if (marchGroup && gatherAndMoveInFormation(posIn, source, 0, true))
		{
			return;
		}
		managed = !marchGroup;
		break;
	default:
		break;
	}
	const Coord3D pos = clampWaypointPosition(posIn);
	// the units to move, nearest to the goal first (ZH sorts them near to far so the near units get the first paths)
	struct Entry
	{
		Object *obj;
		float distSq;
		size_t order;
	};
	std::vector<Entry> units;
	for (size_t i = 0; i < m_members.size(); ++i)
	{
		Object *o = m_members[i];
		if (o->isDestroyed())
		{
			continue;
		}
		AIUpdateInterface *ai = o->getAIUpdateInterface();
		if (!ai || ai->isImmobile() || o->isDisabledByType(DISABLED_HELD)) // RW 0x774B2B .. 0x774B4B: a held, immobile or AI-less member takes no part
		{
			continue;
		}
		// lane MODULES-3 r2: each unit's order passes aiDoCommand's gate (RW 0x667174) before anything of it changes: a refused unit keeps its reservation and
		// takes no part in the group's layout
		if (!ai->acceptCommand(source, addWaypoint || haveFinalAngle ? AIUpdateInterface::kCommandUnidentifiedMove : 0))
		{
			continue;
		}
		if (world->mapReady())
		{
			world->pathfinder().removeGoal(world->adapterFor(*o)); // ZH: every unit gives up its goal reservation first
		}
		const Coord3D &p = *o->getPosition();
		const float dx = SimMath::subf32(p.x, pos.x), dy = SimMath::subf32(p.y, pos.y);
		units.push_back(Entry{ o, SimMath::addf32(SimMath::mulf32(dx, dx), SimMath::mulf32(dy, dy)), i });
	}
	std::stable_sort(units.begin(), units.end(), [](const Entry &a, const Entry &b) {
		if (a.distSq != b.distSq)
		{
			return a.distSq < b.distSq;
		}
		return a.obj->getID() < b.obj->getID();
	});
	Coord3D center = pos;
	bool first = true;
	for (const Entry &e : units)
	{
		Object *o = e.obj;
		if (first)
		{
			center = *o->getPosition(); // ZH: the group's reference point is the nearest unit
			first = false;
		}
		const Coord3D dest = computeIndividualDestination(pos, *o, center);
		AIUpdateInterface *ai = o->getAIUpdateInterface();
		if (source == CMD_FROM_PLAYER && !managed)
		{
			delayMoodCheckForStealth(*o, *ai);
		}
		if (!addWaypoint)
		{
			if (haveFinalAngle)
			{
				ai->aiMoveToPositionAndOrientate(dest, finalAngle, source);
			}
			else
			{
				ai->aiMoveToPosition(dest, source);
			}
		}
		else
		{
			ai->aiFollowPathAppend(dest, source);
		}
	}
}

const char *AIGroup::planningStopLine()
{
	return "S-1831 the group manager's move order (RW 0x75748C / 0x94E232) is executed at once and alone: the order queue's modes (player + 0x770, RW 0x7572FA), the "
		"chained orders (RW 0x7571A7 / 0x756F76), the order's completion test (slot 0x14 RW 0x94DC37), its save data, the SHIP / AMPHIBIOUS transport branch "
		"(RW 0x94DA57 -> 0x68F3F2 / 0x770D27) and the object + 0x27C branch (RW 0x754B18) are not ported";
}

void AIGroup::planningMoveToPosition(const Coord3D &pos, bool attackMove, CommandSourceType source)
{
	AIWorld *world = m_logic.aiWorld();
	if (!world || m_members.empty())
	{
		return;
	}
	world->noteStop(planningStopLine());
	for (Object *o : m_members)
	{
		if (o->isDestroyed())
		{
			continue;
		}
		AIUpdateInterface *ai = o->getAIUpdateInterface();
		if (!ai || ai->isContained()) // RW 0x94E184: RW 0x6939DF (a horde member) or no AI (+ 0x260): nothing
		{
			continue;
		}
		// lane MODULES-3 r2: the order passes aiDoCommand's gate (RW 0x667174) before anything of the unit changes
		if (!ai->acceptCommand(source, 0))
		{
			continue;
		}
		// RW 0x94DF97: the destination of a unit without an AI doing ground movement is the point itself
		Coord3D dest = pos;
		if (world->mapReady() && ai->isDoingGroundMovement())
		{
			Pathfinder &pf = world->pathfinder();
			PathfindObject &adapter = world->adapterFor(*o);
			// RW TerrainLogic vslot 0x1C at the point's layer (RW 0x680A75; lane WALL-2: a wall top's, a ramp's or a walkway's height, the terrain elsewhere)
			Coord3D p{ pos.x, pos.y, pf.getLayerHeight(pos.x, pos.y, pf.getLayerAt(pos.x, pos.y, pos.z)) };
			if (!pf.adjustDestination(adapter, ai->locomotorInfo(), &p, &pos))
			{
				p = pos;
			}
			if (adapter.isKindOf(PK_LARGE_RECTANGLE_PATHFIND))
			{
				// RW 0x94E04E .. 0x94E083: Object + 0x44 plus RW 0x4B3D8D to the destination (x87 fadd, stored as a float), RW 0x68B3CF
				const float heading = SimMath::pc24Add(ObjectKnockback::relativeAngle2D(*o, p), o->getOrientation());
				pf.updateGoalAngle(adapter, &p, heading, LAYER_GROUND);
			}
			else
			{
				pf.updateGoal(adapter, &p, LAYER_GROUND); // RW 0x68B3BD
			}
			dest = p;
		}
		// RW 0x94DF1B: the order's attack flag (+ 0x24) picks the attack-move (RW 0x696266(dest, 0x7FFFFFFF, 0)), else aiMoveToPosition(dest, 0) (RW 0x66C4CA)
		ai->aiMoveToPosition(dest, source);
		if (attackMove)
		{
			ai->armAttackMove(ai->stateMachine().goalPosition()); // the port's attack-move (lane COMBAT-1, S-328)
		}
	}
}

void AIGroup::groupIdle(CommandSourceType source)
{
	for (Object *o : m_members)
	{
		if (o->isDestroyed())
		{
			continue;
		}
		if (AIUpdateInterface *ai = o->getAIUpdateInterface())
		{
			ai->aiIdle(source);
		}
	}
}

// ---- lane PLAY-2 -------------------------------------------------------------------------------------------------------------------------

const char *AIGroup::forceAttackStopLine()
{
	return "[S-2480] force attack (lane PLAY-2): MSG_DO_FORCE_ATTACK_GROUND (RW 0x77AF93) and MSG_DO_FORCE_ATTACK_OBJECT (RW 0x77AED3) run with the group's "
		"weapon locks, groupAttackPosition and aiAttackPosition (state 9); not ported: the passengers of a container that lets them fire and the slaves of a "
		"spawner in groupAttackPosition (RW 0x77229A: the container's allowed-to-fire test, its passenger list and SpawnBehaviorInterface slot 4 have no port), the contain module's "
		"weapon lock pass-through (Object::setWeaponLock / releaseWeaponLock, contain slots 0x168 / 0x16C) and the AI hook of the lock, the object form's "
		"reserved-id branch (victim id 99999999 with the GameData flag + 0xBC: TerrainLogic RW 0x68449B makes a real object of the terrain prop at the click "
		"and attacks it), the outside-the-playable-area refusal (Object + 0x458 bit 3 is never set); INFERENCE: PartitionFilterPossibleToAttack of the "
		"ContinueAttackRange search is the non-forced canAttackObject";
}

bool AIGroup::isIdle() const
{
	for (Object *o : m_members)
	{
		const AIUpdateInterface *ai = o->getAIUpdateInterface();
		if (ai && !ai->isIdle() && !o->isEffectivelyDead())
		{
			return false;
		}
	}
	return true;
}

bool AIGroup::setWeaponLockForGroup(int slot, WeaponLockType type)
{
	static const int kSwitchedWeapons = ObjectTemplateInfoBuilder::objectStatusIndex("SWITCHED_WEAPONS"); // RW status 0x51
	bool any = false;
	for (Object *o : m_members)
	{
		ObjectWeapons *w = o->getWeapons();
		Weapon *before = w ? w->currentWeapon() : nullptr;
		// RW 0x69121A: the object status SWITCHED_WEAPONS = (permanent and slot != PRIMARY), then the weapon set's lock (RW 0x6C97F9); no weapon set: false
		if (kSwitchedWeapons >= 0)
		{
			o->setStatus((unsigned)kSwitchedWeapons, type == LOCKED_PERMANENTLY && slot != 0);
		}
		if (w && w->setWeaponLock(slot, type))
		{
			any = true;
		}
		if (w && before && before->getTemplate() && before->getTemplate()->m_shareTimers && w->weaponStatus(*before) != WEAPON_READY_TO_FIRE)
		{
			if (Weapon *now = w->currentWeapon())
			{
				now->setWhenWeCanFireAgain(before->whenWeCanFireAgain());
				now->setStatus(WEAPON_OUT_OF_AMMO);
			}
		}
	}
	return any;
}

void AIGroup::releaseWeaponLockForGroup(WeaponLockType type)
{
	for (Object *o : m_members)
	{
		if (ObjectWeapons *w = o->getWeapons())
		{
			w->releaseWeaponLock(type); // RW 0x68DF11 -> RW 0x6C98E6 (a destroyed object, status + 0x94 bit 0, is not a member)
		}
	}
}

void AIGroup::groupAttackPosition(const Coord3D &pos, int maxShotsToFire, CommandSourceType source)
{
	m_logic.noteStop(forceAttackStopLine());
	for (Object *o : m_members)
	{
		if (AIUpdateInterface *ai = o->getAIUpdateInterface())
		{
			ai->aiAttackPosition(pos, maxShotsToFire, source);
		}
	}
}

void AIGroup::clearTemporarySpeedCaps()
{
	for (Object *o : m_members)
	{
		AIUpdateInterface *ai = o->getAIUpdateInterface();
		if (Locomotor *loco = ai ? ai->curLocomotor() : nullptr)
		{
			loco->setTemporarySpeedCap(-1.0f);
		}
	}
}
