// OpenBFME. GPL-3.0.
//
// HordeContain's wall scaling (lane BACKLOG-8): the state a horde keeps while it climbs a wall, the member position of that climb and the move hub's branch
// that puts the member there.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; tier B same-shape with BFME2 1.06: only Object / struct offsets differ, tools/re/pair_diff.py):
//   * contain slot 0x23C RW 0x86BCF9 (a horde with an AI): a wall id sets the horde's SCALING_WALL (model condition 0x1BA, RW 0x694233) unless one is
//     already kept, 0 clears SCALING_WALL and SCALING_WALL_HORDE (0x1BB, RW 0x694183); the id is kept at H + 0x2F0. Slot 0x238 RW 0x86BD54: the horde's
//     current locomotor has ScalesWalls (template + 0x150) and H + 0x2F0 is set.
//   * the collide entry RW 0x86C61E (the + 0x24 interface's slot 0): an object that is KindOf SCALEABLE_WALL (201, template + 0x121 bit 1), not DESTROYED
//     (status bit 0) and not the wall kept, met by a horde whose locomotor has ScalesWalls and whose facing's dot with the wall's is at least 0.5 in
//     absolute value (the CRT fabs of the three-term dot, RW 0xBD869C), becomes the wall (slot 0x23C).
//   * RW 0x86D170 (called at the end of the member pass, RW 0x874681): with a wall kept and no contained member SCALING_WALL_HORDE, CLIMBING (0x67) or
//     RAPPELLING (0x69), a counter (H + 0x2F4) runs; the fifth such pass in a row clears the wall (slot 0x23C with 0) and the counter.
//   * the member order's destination step RW 0x871897: while slot 0x238 answers yes a member that is KindOf CAN_CLIMB_WALLS (184, template + 0x11F bit 0)
//     gets RW 0x86E214 instead of the pathfinder's slot destination RW 0x6F0889.
//   * RW 0x86E214(member, pos, horde): the step toward `pos`: r = 0.7 * min(distance, the horde locomotor's top speed RW 0x6624C9), at most 7; scaled by
//     0.2 + 0.8 * (200 - d) / 200 with d the length estimate (RW 0x4035AC) of member -> horde clamped to 1 .. 200; at most 10; a BANNER (164) takes 0.8 of
//     it; at least 9 when the distance is above twice the horde's bounding circle (Object + 0xB8). The object under the step's end (the pathfinder's cell
//     object RW 0x5E3CC9: an OBSTACLE cell's owner), else, for a member neither CLIMBING nor RAPPELLING, the one under the step's middle (the step is then
//     halved). Under a SCALEABLE_WALL that is not DESTROYED (the wall kept, when it exists, replaces another piece): when the end (or the middle) lies
//     within the wall's major radius along the wall's own x axis and not above its top (RW 0xAD1920) and the member is below the top, the member stays at
//     its place, moved onto the wall's face (its local x clamped to the major radius), and rises: to the wall's base + half its own height at least,
//     else by 0.8 r (far) or 0.6 r, never above the top. Otherwise a member above the ground sinks by the same amounts down to the ground; else under a
//     WALL_UPGRADE / WALL_HUB (150 / 156) that is not the wall kept the step turns to normalize((wall kept - member) + 2 (member - that object)) * r. With
//     neither a rise nor a sink the step's end is the answer. H + 0x2EC keeps the largest distance, at most 90 (RW 0x86BC41).
//   * the hub RW 0x87468B's branch (RW 0x8747AE .. 0x874B1A) for a member whose locomotor's ZAxisBehavior is SCALING_WALLS (template + 0x70 == 9) while
//     slot 0x238 answers yes: with (dx, dy, dz) = goal - position, the member moves when the planar length is above 0.5, rises when it does not move and
//     dz > 0.5, sinks when dz < -0.5. A moving member on the ground (ground + 0.01 >= z) loses SCALING_WALL_HORDE; a rising one gets CLIMBING for
//     RAPPELLING and SCALING_WALL_HORDE; a sinking one RAPPELLING for CLIMBING; any other loses both (RW 0x5E3B79). MOVING follows the move. The member
//     is put on the goal (RW 0x70C201), not below the ground, the pass counts as work, and it faces along the wall under it (the wall's facing, or the
//     opposite one when the horde faces the other way), else turns toward the slot's facing by its locomotor's turn rate (RW 0x5E372D).
// DONOR: BFME2 decomp HordeContainMovementPosition.cpp (HordeContain::rva0046B05B), HordeContainUpdatePosition.cpp (HordeContain::_updatePosition),
// HordeContainIface24Slot0.cpp, HordeContainIface11CSlots.cpp (rva00468BDC, rva00468B24), Common/Rva0046A024.cpp, Common/Rva0046E6EA.cpp: byte-matched.
// INFERENCE / NOT PORTED (stop S-4461): see wallScalingStopLine.

#include "Common/StateHash.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/ObjectGeometry.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/SimMath.h"

namespace
{
const float kPi = 3.14159274f;
const float kTwoPi = 6.28318548f;

struct WallBits
{
	int canClimbWalls = ObjectTemplateInfoBuilder::kindOfIndex("CAN_CLIMB_WALLS"); // 184
	int scaleableWall = ObjectTemplateInfoBuilder::kindOfIndex("SCALEABLE_WALL");  // 201
	int wallUpgrade = ObjectTemplateInfoBuilder::kindOfIndex("WALL_UPGRADE");      // 150
	int wallHub = ObjectTemplateInfoBuilder::kindOfIndex("WALL_HUB");              // 156
	int banner = ObjectTemplateInfoBuilder::kindOfIndex("BANNER");                 // 164
	int moving = AIUpdateInterface::modelConditionBit("MOVING");
	int climbing = AIUpdateInterface::modelConditionBit("CLIMBING");
	int rappelling = AIUpdateInterface::modelConditionBit("RAPPELLING");
	int scalingWall = AIUpdateInterface::modelConditionBit("SCALING_WALL");
	int scalingWallHorde = AIUpdateInterface::modelConditionBit("SCALING_WALL_HORDE");
};
const WallBits &wallBits()
{
	static const WallBits b;
	return b;
}

float normalizeAngle(float a)
{
	while (a > kPi)
	{
		a = SimMath::subf32(a, kTwoPi);
	}
	while (a <= -kPi)
	{
		a = SimMath::addf32(a, kTwoPi);
	}
	return a;
}

float length3(float x, float y, float z)
{
	return SimMath::fstpDword(SimMath::sqrtPC24((double)SimMath::sumSquares3(x, y, z))); // Coord3D::length RW 0x403111
}

// RW 0x4035AC
float lengthEstimate(float x, float y)
{
	const float ax = x < 0.0f ? SimMath::subf32(0.0f, x) : x, ay = y < 0.0f ? SimMath::subf32(0.0f, y) : y;
	return ax > ay ? SimMath::addf32(ax, SimMath::mulf32(ay, 0.25f)) : SimMath::addf32(ay, SimMath::mulf32(ax, 0.25f));
}

// a point in the frame of an upright object (the binary multiplies by the object's matrix and its orthogonal inverse)
struct Frame
{
	Coord3D origin;
	float c, s;
	explicit Frame(const Object &o) : origin(*o.getPosition()), c(SimMath::cosf32(o.getOrientation())), s(SimMath::sinf32(o.getOrientation())) {}
	Coord3D toLocal(const Coord3D &p) const
	{
		const float dx = SimMath::subf32(p.x, origin.x), dy = SimMath::subf32(p.y, origin.y);
		return Coord3D{ SimMath::addf32(SimMath::mulf32(c, dx), SimMath::mulf32(s, dy)), SimMath::subf32(SimMath::mulf32(c, dy), SimMath::mulf32(s, dx)),
						SimMath::subf32(p.z, origin.z) };
	}
	Coord3D toWorld(const Coord3D &p) const
	{
		return Coord3D{ SimMath::addf32(SimMath::subf32(SimMath::mulf32(c, p.x), SimMath::mulf32(s, p.y)), origin.x),
						SimMath::addf32(SimMath::addf32(SimMath::mulf32(s, p.x), SimMath::mulf32(c, p.y)), origin.y), SimMath::addf32(p.z, origin.z) };
	}
};

float maxHeightOf(const Object &o)
{
	return ObjectGeometry::maxHeightAbovePosition(o.geometryShapes());
}
} // namespace

const char *HordeContain::wallScalingStopLine()
{
	return "[S-4461] horde wall scaling (lane BACKLOG-8): the wall kept by the horde (contain slots 0x238 / 0x23C, RW 0x86BD54 / 0x86BCF9), its collide entry "
		   "(RW 0x86C61E), its end (RW 0x86D170), the climbing member's position (RW 0x86E214 through RW 0x871897) and the hub's branch (RW 0x87468B: "
		   "RW 0x8747AE ..) are ported; NOT connected: nothing calls the collide entry in a game (the horde object itself cannot cross a wall: the locomotor's "
		   "ScalesWalls look-ahead RW 0x5E8956 is S-084, the wall layer S-161), so a horde never starts to scale; INFERENCE: the wall's frame is its position "
		   "and facing (RW: the object matrix), its major radius the template's first shape's, the destination layer the ground; not ported: the climber's stealth reveal (RW 0x7767A9), the wall owner's "
		   "EVA event 15, the drawable's switch (RW 0x672A02) and the member call RW 0x68B531";
}

// RW 0x694233 / 0x694183 / 0x6943ED: a model condition of the horde object and of every member it contains (the contain's iteration, slot 0x108)
void HordeContain::setModelConditionForHorde(int bit, bool on)
{
	for (Object *m : m_members)
	{
		if (m)
		{
			m->setModelConditionState(bit, on);
		}
	}
	getObject()->setModelConditionState(bit, on);
}

void HordeContain::setWallToScale(ObjectID wall)
{
	Object *horde = getObject();
	if (!horde || !horde->getAIUpdateInterface())
	{
		return;
	}
	horde->logic().noteStop(wallScalingStopLine());
	if (wall != 0)
	{
		if (m_wallToScale == 0)
		{
			setModelConditionForHorde(wallBits().scalingWall, true);
		}
	}
	else
	{
		setModelConditionForHorde(wallBits().scalingWall, false);
		setModelConditionForHorde(wallBits().scalingWallHorde, false);
	}
	m_wallToScale = wall;
}

bool HordeContain::isScalingWall() const
{
	const AIUpdateInterface *ai = getObject()->getAIUpdateInterface();
	const Locomotor *loco = ai ? ai->curLocomotor() : nullptr;
	return loco && loco->getTemplate().m_scalesWalls && m_wallToScale != 0;
}

void HordeContain::collideWithWall(Object *other)
{
	Object *horde = getObject();
	if (!other || !other->isKindOf((unsigned)wallBits().scaleableWall) || other->isDestroyed() || other->getID() == m_wallToScale)
	{
		return;
	}
	const AIUpdateInterface *ai = horde->getAIUpdateInterface();
	if (!ai || !ai->curLocomotor() || !ai->curLocomotor()->getTemplate().m_scalesWalls)
	{
		return;
	}
	const float hx = SimMath::cosf32(horde->getOrientation()), hy = SimMath::sinf32(horde->getOrientation());
	const float wx = SimMath::cosf32(other->getOrientation()), wy = SimMath::sinf32(other->getOrientation());
	const float dot = SimMath::addf32(SimMath::mulf32(wx, hx), SimMath::mulf32(wy, hy));
	if (SimMath::absD((double)dot) < 0.5)
	{
		return;
	}
	setWallToScale(other->getID());
}

void HordeContain::checkWallScalingDone()
{
	if (m_wallToScale == 0)
	{
		return;
	}
	const WallBits &b = wallBits();
	for (const Object *m : m_members)
	{
		if (m && (m->testModelCondition(b.scalingWallHorde) || m->testModelCondition(b.climbing) || m->testModelCondition(b.rappelling)))
		{
			return;
		}
	}
	if (m_wallIdlePasses++ < 4)
	{
		return;
	}
	setWallToScale(0);
	m_wallIdlePasses = 0;
}

static Object *cellObjectAt(Object &asker, const Coord3D &pos)
{
	// RW 0x5E3CC9 with the layer of TheTerrainLogic's getLayerForDestination (the ground: S-161): the owner of the OBSTACLE cell under the point
	AIUpdateInterface *ai = asker.getAIUpdateInterface();
	if (!ai || !ai->world().mapReady())
	{
		return nullptr;
	}
	Pathfinder &pf = ai->world().pathfinder();
	const PathfindCell *cell = pf.getCell(LAYER_GROUND, &pos);
	if (cell && cell->getType() == PathfindCell::CELL_CLEAR)
	{
		// lane WALL-1: a wall top replaced the wall's own obstacle cells (Pathfinder::classifyWall); the wall piece holding the cell stands for it
		const PathfindObjectID wall = pf.wallPieceAt(pos);
		return wall == PATHFIND_INVALID_ID ? nullptr : asker.logic().findObjectByID((ObjectID)wall);
	}
	if (!cell || cell->getType() != PathfindCell::CELL_OBSTACLE || cell->getObstacleID() == PATHFIND_INVALID_ID)
	{
		return nullptr;
	}
	return asker.logic().findObjectByID((ObjectID)cell->getObstacleID());
}

void HordeContain::wallScalingMemberPosition(Object &member, Coord3D &pos, Object &other)
{
	const WallBits &b = wallBits();
	Object *horde = getObject();
	GameLogic &logic = horde->logic();
	const Coord3D mp = *member.getPosition();
	float dx = SimMath::subf32(pos.x, mp.x), dy = SimMath::subf32(pos.y, mp.y);
	const float dist = length3(dx, dy, 0.0f);
	if (dist != 0.0f)
	{
		const float inv = SimMath::divf32(1.0f, dist);
		dx = SimMath::mulf32(dx, inv);
		dy = SimMath::mulf32(dy, inv);
	}
	const bool far = dist > SimMath::mulf32(2.0f, CombatQueries::boundingCircleRadius(*horde));
	AIUpdateInterface *oai = other.getAIUpdateInterface();
	const float top = (oai && oai->curLocomotor()) ? oai->curLocomotor()->getMaxSpeedForCondition(oai->locomotorHost()) : 0.0f; // RW 0x6624C9
	float r = SimMath::mulf32(dist < top ? dist : top, 0.7f);
	r = r < 7.0f ? r : 7.0f;
	float d = lengthEstimate(SimMath::subf32(horde->getPosition()->x, mp.x), SimMath::subf32(horde->getPosition()->y, mp.y));
	if (200.0f < d)
	{
		d = 200.0f;
	}
	else if (d <= 1.0f)
	{
		d = 1.0f;
	}
	const float t = SimMath::divf32(SimMath::subf32(200.0f, d), 200.0f);
	r = SimMath::mulf32(r, SimMath::addf32(SimMath::mulf32(t, 0.8f), 0.2f));
	r = r > 10.0f ? 10.0f : r;
	if (member.isKindOf((unsigned)b.banner))
	{
		r = SimMath::mulf32(r, 0.8f);
	}
	if (far)
	{
		r = r > 9.0f ? r : 9.0f;
	}
	Coord3D start = mp;
	Coord3D target{ SimMath::addf32(mp.x, SimMath::mulf32(dx, r)), SimMath::addf32(mp.y, SimMath::mulf32(dy, r)), mp.z };
	// RW 0x70C55D (isSignificantlyAboveTerrain) is false for every KindOf CAN_CLIMB_WALLS object (RW 0x70C50F), the only members that come here
	const bool onFoot = !member.testModelCondition(b.climbing) && !member.testModelCondition(b.rappelling);
	auto middle = [&]() { return Coord3D{ SimMath::mulf32(SimMath::addf32(target.x, start.x), 0.5f), SimMath::mulf32(SimMath::addf32(target.y, start.y), 0.5f), target.z }; };
	Object *o = cellObjectAt(member, target);
	if (!o && onFoot)
	{
		const Coord3D mid = middle();
		o = cellObjectAt(member, mid);
		if (o)
		{
			target = mid;
		}
	}
	bool raised = false, lowered = false;
	if (o && o->isKindOf((unsigned)b.scaleableWall) && !o->isDestroyed())
	{
		if (o->getID() != m_wallToScale)
		{
			if (Object *kept = logic.findObjectByID(m_wallToScale))
			{
				o = kept;
			}
		}
		const Frame frame(*o);
		const float major = horde->getAIUpdateInterface()->world().movementInfo(*o->getTemplate()).geometry.majorRadius;
		const float height = maxHeightOf(*o);
		auto within = [&](const Coord3D &p) {
			const Coord3D l = frame.toLocal(p);
			return SimMath::absD((double)l.x) <= (double)major && height >= l.z;
		};
		bool inside = within(target);
		if (!inside && onFoot)
		{
			inside = within(middle());
		}
		if (inside)
		{
			const float baseZ = o->getPosition()->z;
			const float topZ = SimMath::addf32(baseZ, height);
			if (start.z < topZ)
			{
				Coord3D l = frame.toLocal(start);
				if (l.x > major)
				{
					l.x = major;
				}
				else if (SimMath::subf32(0.0f, major) > l.x)
				{
					l.x = SimMath::subf32(0.0f, major);
				}
				start = frame.toWorld(l);
				const float half = SimMath::mulf32(maxHeightOf(member), 0.5f);
				if (SimMath::addf32(half, baseZ) > start.z)
				{
					start.z = SimMath::addf32(baseZ, half);
				}
				else
				{
					start.z = SimMath::addf32(start.z, SimMath::mulf32(r, far ? 0.8f : 0.6f));
				}
				start.z = start.z > topZ ? topZ : start.z;
				raised = true;
			}
		}
	}
	else
	{
		const float ground = logic.getGroundHeight(start.x, start.y);
		if (start.z > ground)
		{
			start.z = SimMath::subf32(start.z, SimMath::mulf32(r, far ? 0.8f : 0.6f));
			start.z = start.z < ground ? ground : start.z;
			lowered = true;
		}
		else if (o && (o->isKindOf((unsigned)b.wallUpgrade) || o->isKindOf((unsigned)b.wallHub)) && o->getID() != m_wallToScale)
		{
			if (const Object *kept = logic.findObjectByID(m_wallToScale))
			{
				const Coord3D kp = *kept->getPosition(), op = *o->getPosition();
				float ax = SimMath::addf32(SimMath::subf32(kp.x, mp.x), SimMath::mulf32(SimMath::subf32(mp.x, op.x), 2.0f));
				float ay = SimMath::addf32(SimMath::subf32(kp.y, mp.y), SimMath::mulf32(SimMath::subf32(mp.y, op.y), 2.0f));
				float az = SimMath::addf32(SimMath::subf32(kp.z, mp.z), SimMath::mulf32(SimMath::subf32(mp.z, op.z), 2.0f));
				const float len = length3(ax, ay, az);
				if (len != 0.0f)
				{
					const float inv = SimMath::divf32(1.0f, len);
					ax = SimMath::mulf32(ax, inv);
					ay = SimMath::mulf32(ay, inv);
					az = SimMath::mulf32(az, inv);
				}
				target = Coord3D{ SimMath::addf32(mp.x, SimMath::mulf32(ax, r)), SimMath::addf32(mp.y, SimMath::mulf32(ay, r)), SimMath::addf32(mp.z, SimMath::mulf32(az, r)) };
			}
		}
	}
	if (!lowered && !raised)
	{
		start = target;
	}
	pos = start;
	// RW 0x86BC41
	m_scalingReach = dist > m_scalingReach ? dist : m_scalingReach;
	m_scalingReach = m_scalingReach > 90.0f ? 90.0f : m_scalingReach;
}

bool HordeContain::wallScalingHub(Object &member, const Coord3D &goal, float orientation)
{
	AIUpdateInterface *a = member.getAIUpdateInterface();
	Locomotor *l = a ? a->curLocomotor() : nullptr;
	if (!l || l->getTemplate().m_zAxisBehavior != Z_SCALING_WALLS || !isScalingWall())
	{
		return false;
	}
	const WallBits &b = wallBits();
	Object *horde = getObject();
	GameLogic &logic = horde->logic();
	const Coord3D mp = *member.getPosition();
	const float dx = SimMath::subf32(goal.x, mp.x), dy = SimMath::subf32(goal.y, mp.y), dz = SimMath::subf32(goal.z, mp.z);
	const bool move = length3(dx, dy, 0.0f) > 0.5f;
	const bool rising = !move && dz > 0.5f;
	const bool falling = !move && dz < -0.5f;
	if (SimMath::addf32(logic.getGroundHeight(mp.x, mp.y), 0.01f) >= mp.z && move && member.testModelCondition(b.scalingWallHorde))
	{
		member.setModelConditionState(b.scalingWallHorde, false);
	}
	if (rising)
	{
		member.setModelConditionState(b.rappelling, false);
		member.setModelConditionState(b.climbing, true);
		member.setModelConditionState(b.scalingWallHorde, true);
	}
	else if (falling)
	{
		member.setModelConditionState(b.climbing, false);
		member.setModelConditionState(b.rappelling, true);
	}
	else
	{
		member.setModelConditionState(b.climbing, false);
		member.setModelConditionState(b.rappelling, false);
	}
	member.setModelConditionState(b.moving, move);
	Coord3D placed = goal;
	const float ground = logic.getGroundHeight(placed.x, placed.y);
	placed.z = ground > placed.z ? ground : placed.z;
	member.setPosition(&placed);
	m_workDone = true;
	if (Object *wall = cellObjectAt(member, *member.getPosition()))
	{
		const float dot = SimMath::addf32(SimMath::mulf32(SimMath::cosf32(horde->getOrientation()), SimMath::cosf32(wall->getOrientation())),
										  SimMath::mulf32(SimMath::sinf32(horde->getOrientation()), SimMath::sinf32(wall->getOrientation())));
		member.setOrientation(dot >= 0.0f ? wall->getOrientation() : normalizeAngle(SimMath::addf32(wall->getOrientation(), kPi)));
		return true;
	}
	float rel = normalizeAngle(SimMath::subf32(orientation, member.getOrientation()));
	const float turn = l->getMaxTurnRate(a->locomotorHost());
	rel = rel > turn ? turn : (rel < SimMath::subf32(0.0f, turn) ? SimMath::subf32(0.0f, turn) : rel);
	member.setOrientation(normalizeAngle(SimMath::addf32(member.getOrientation(), rel)));
	return true;
}

void HordeContain::crcWallScaling(StateHasher &h) const
{
	if (m_wallToScale != 0 || m_wallIdlePasses != 0 || m_scalingReach != 0.0f)
	{
		h.addU32(m_wallToScale);
		h.addI32(m_wallIdlePasses);
		h.addFloat(m_scalingReach);
	}
}
