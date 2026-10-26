// OpenBFME. GPL-3.0.
//
// Lane PHYS-1: the melee approach's destination check (RotWK only, no ZH counterpart): RW 0x6EEBC1, its cell test RW 0x6E9070 and its footprint predicate RW 0x6EBE89.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; review r4 of lane PHYS-1):
//   * RW 0x6EEBC1(unit, locomotor set, dest): the footprint radius / centring of RW 0x6ED071; the cell of `dest` (RW 0x6E8DD5 with that centring), none -> false; the
//     destination layer (RW 0x680A75); the unit's squared 3D distance to `dest` ((dz^2 + dy^2) + dx^2); the unit's clipped cell (RW 0x6EA076), none -> false; the movement
//     of the locomotor set (RW 0x6EA04D) and the effective zone (RW 0x938D89 = getEffectiveZone, terrain variant) of the unit's cell, mapped by RW 0x938939 when that cell is
//     an obstacle; a destination cell in that zone whose footprint passes RW 0x6EBE89 -> true, `dest` unchanged;
//   * else the spiral from the destination cell: +x n times, +y n times, n + 1, -x n times, -y n times, n + 1, ... (n starts at 1), the budget MaxCellsAdjustToMeleeDestination
//     (GameData + 0x1200) decremented per visited cell and checked before each +x leg; each cell passing RW 0x6E9070 writes its coordinate (RW 0x6E8E19) into `dest`, then
//     needs a squared distance to the unit strictly below the original one and RW 0x6EBE89 -> true;
//   * RW 0x6E9070: the cell exists on the layer, is not type 4 (obstacle) or 5, not impassable to players for a human player's unit (movement + 5), and its effective zone
//     (mapped by RW 0x938939 when the unit's cell was an obstacle) is the unit's;
//   * RW 0x6EBE89: every footprint cell (x - r .. x + r + centred) exists, is not type 5 or 4, not impassable to players for a human player's unit (RW 0x68B68A); a unit that is
//     not LARGE_RECTANGLE_PATHFIND (template + 0x122 bit 0x10) needs RW 0x9344DE: no goal reservation of another unit on the cell; a LARGE_RECTANGLE_PATHFIND unit refuses a
//     goal of another LARGE_RECTANGLE_PATHFIND unit; then no ENEMY (RW 0x68D7AB == 0) among the cell's stationary positions.
// INFERENCE (stop S-786): RW 0x938939's obstacle-zone mapping has no counterpart in the zone manager: a unit standing in an obstacle cell uses its cell's zone. Lane
// WALL-2: findMeleeEngagementLocation searches on the destination's layer and retries a WALL_UPGRADE target on the ground with twice the budget (RW 0x6F39AD /
// 0x6F3BBD); the siege-deploy flag of its predicate (RW 0x6F3B4F .. 0x6F3B69, SiegeDeploySpecialPower) is not ported.

#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/SimMath.h"

bool Pathfinder::meleeFootprintFree(const PathfindObject &obj, int x, int y, PathfindLayerEnum layer, int radius, bool center) const
{
	const int above = radius + (center ? 1 : 0);
	const PathfindObjectID self = obj.getID();
	const bool human = !obj.isComputerControlled();
	const bool largeRect = obj.isKindOf(PK_LARGE_RECTANGLE_PATHFIND);
	for (int i = x - radius; i < x + above; ++i)
	{
		for (int j = y - radius; j < y + above; ++j)
		{
			const PathfindCell *c = const_cast<Pathfinder *>(this)->getCell(layer, i, j);
			if (c == nullptr || c->getType() == PathfindCell::CELL_BRIDGE_IMPASSABLE || (c->getImpassableToPlayers() && human) || c->getType() == PathfindCell::CELL_OBSTACLE)
			{
				return false;
			}
			if (!largeRect)
			{
				for (const PathfindOccupant *o = c->occupants(OCC_GROUND_GOAL); o; o = o->next)
				{
					if (o->owner != self)
					{
						return false; // RW 0x9344DE
					}
				}
			}
			else
			{
				for (const PathfindOccupant *o = c->occupants(OCC_GROUND_GOAL); o; o = o->next)
				{
					const PathfindObject *other = m_world ? m_world->findObjectByID(o->owner) : nullptr;
					if (o->owner != self && other && other->isKindOf(PK_LARGE_RECTANGLE_PATHFIND))
					{
						return false;
					}
				}
			}
			for (const PathfindOccupant *o = c->occupants(OCC_POSITION); o; o = o->next)
			{
				const PathfindObject *other = m_world ? m_world->findObjectByID(o->owner) : nullptr;
				if (other && o->owner != self && obj.getRelationship(*other) == PATHFIND_ENEMIES)
				{
					return false;
				}
			}
		}
	}
	return true;
}

bool Pathfinder::meleeReachCells(const PathfindObject &unit, const Coord3D &from, const PathfindObject &target, int rangeCells)
{
	auto box = [&](const PathfindObject &o, const Coord3D &at, ICoord2D &lo) {
		const int size = footprintSize(&o); // RW 0x6EAF79
		ICoord2D cell;
		Coord3D p = at;
		worldToCell(&p, (size & 1) != 0, &cell); // RW 0x6ECFDE: RW 0x6ECFC5 (odd -> centred), RW 0x6E8CE6
		lo.x = cell.x + size / -2;               // idiv by -2 (C truncation)
		lo.y = cell.y + size / -2;
		return size;
	};
	ICoord2D u, t;
	const int sizeU = box(unit, from, u);
	const int sizeT = box(target, target.getPosition(), t);
	const int uLoX = u.x - rangeCells, uLoY = u.y - rangeCells;
	const int uHiX = u.x + sizeU + rangeCells, uHiY = u.y + sizeU + rangeCells;
	const int tHiX = t.x + sizeT, tHiY = t.y + sizeT;
	return uLoX <= tHiX && t.x <= uHiX && uLoY <= tHiY && t.y <= uHiY;
}

bool Pathfinder::adjustToMeleeDestination(const PathfindObject &obj, const PathfindLocomotorInfo &loco, Coord3D *dest)
{
	int radius = 0;
	bool center = false;
	getRadiusAndCenter(&obj, radius, center);
	ICoord2D start;
	{
		// RW 0x6E8DD5 gives x = -1 for a cell off the map and RW 0x6EEBC1 refuses; the port's converter clips and reports the overflow (review r5)
		Coord3D d = *dest;
		if (worldToCell(&d, center, &start))
		{
			return false;
		}
	}
	const PathfindLayerEnum layer = LAYER_GROUND;
	const Coord3D pos = obj.getPosition();
	const float dz0 = SimMath::subf32(pos.z, dest->z), dy0 = SimMath::subf32(pos.y, dest->y), dx0 = SimMath::subf32(pos.x, dest->x);
	const float d0 = SimMath::addf32(SimMath::addf32(SimMath::mulf32(dz0, dz0), SimMath::mulf32(dy0, dy0)), SimMath::mulf32(dx0, dx0));
	Coord3D clipped = pos;
	const PathfindCell *own = getClippedCell(obj.getLayer(), &clipped);
	if (own == nullptr)
	{
		return false;
	}
	const PathfindMovement mv = makeMovement(&obj, loco);
	const zoneStorageType zone = m_zoneManager.getEffectiveZone(mv, true, own->getZone());
	const PathfindCell *destCell = getCell(layer, start.x, start.y);
	if (destCell && m_zoneManager.getEffectiveZone(mv, true, destCell->getZone()) == zone && meleeFootprintFree(obj, start.x, start.y, layer, radius, center))
	{
		return true;
	}
	// RW 0x6E9070 then the closer-only test and RW 0x6EBE89
	auto tryCell = [&](int x, int y) {
		const PathfindCell *c = getCell(layer, x, y);
		if (c == nullptr || c->getLayer() != layer || c->getType() == PathfindCell::CELL_OBSTACLE || c->getType() == PathfindCell::CELL_BRIDGE_IMPASSABLE ||
			(c->getImpassableToPlayers() && mv.b5) || m_zoneManager.getEffectiveZone(mv, true, c->getZone()) != zone)
		{
			return false;
		}
		adjustCoordToCell(x, y, center, *dest, layer);
		const float dz = SimMath::subf32(pos.z, dest->z), dy = SimMath::subf32(pos.y, dest->y), dx = SimMath::subf32(pos.x, dest->x);
		const float d = SimMath::addf32(SimMath::addf32(SimMath::mulf32(dz, dz), SimMath::mulf32(dy, dy)), SimMath::mulf32(dx, dx));
		return d < d0 && meleeFootprintFree(obj, x, y, layer, radius, center);
	};
	int budget = m_config.adjustToMeleeLimit;
	int x = start.x, y = start.y;
	int n = 1;
	while (budget > 0)
	{
		for (int k = 0; k < n; ++k)
		{
			++x;
			--budget;
			if (tryCell(x, y))
			{
				return true;
			}
		}
		for (int k = 0; k < n; ++k)
		{
			++y;
			--budget;
			if (tryCell(x, y))
			{
				return true;
			}
		}
		++n;
		for (int k = 0; k < n; ++k)
		{
			--x;
			--budget;
			if (tryCell(x, y))
			{
				return true;
			}
		}
		for (int k = 0; k < n; ++k)
		{
			--y;
			--budget;
			if (tryCell(x, y))
			{
				return true;
			}
		}
		++n;
	}
	return false;
}

// ---------------------------------------------------------------------------------------------------------------------------------------
// Lane PHYS-1 round 6: the melee engage state's destination (RW 0x6F37B3) and the contact back-off (RW 0x7463E8).
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * RW 0x6EE6DD(unit): the unit's cell (RW 0x6ECFFE), none -> false; over the footprint extents of RW 0x6ED0A0 around it every cell exists, is not type 5 or 4, not
//     impassable to players for a human player's unit; a cell with goal or position records refuses a unit that is not LARGE_RECTANGLE_PATHFIND when another unit's goal
//     is there (RW 0x93447C), and a LARGE_RECTANGLE_PATHFIND unit when another LARGE_RECTANGLE_PATHFIND unit's goal is there;
//   * RW 0x6F37B3(unit, locomotor set, dest, target): RW 0x6CC07C(unit, its position, target, target position, 0, 1) and RW 0x6EE6DD -> dest = the unit's position, true;
//     else the point 10 ahead of the unit (its facing RW 0x70BA23 * 10, RW 0xBD83D8); v = normalize(ahead - dest) * 10 with dest the target position the caller
//     passed; a STRUCTURE or WALK_ON_TOP_OF_WALL target that is not WALL_UPGRADE runs RW 0x6EA0EC on v; the footprint radius / centring of RW 0x6ED071; at most two
//     rounds: dest = the target's position (RW 0x68C9AC) + v (v = 0 for a WALL_UPGRADE target), + (5, 5) when not centred; its cell (RW 0x6E8DD5, centred), off the
//     map -> false; the clipped cell of `ahead` (RW 0x6EA076), none -> false; when the two cells' effective zones agree, RW 0x6F3082 at the cell and RW 0x6CC07C from the
//     unit's position to dest -> true; else the circular search RW 0x6EFE9F (budget MaxCellsFindMeleeEngagementLocation, patience 1) with the predicate RW 0x6ED798
//     (RW 0x6E9070 on the cell, RW 0x6CC07C from the cell's coordinate to the target, RW 0x6EBE89; the coordinate is written to dest) and the score RW 0x6E8523
//     ((10x - unit.x)^2 + (10y - unit.y)^2 in the x87) -> true; a WALL_UPGRADE target retries on layer 1 with twice the budget; then |v|^2 <= 1 (RW 0xBD1908) ends,
//     else v = 0 for the second round;
//   * RW 0x6EFE9F: the centre cell first (score kept), then rings of legs +x n, +y n, -x n + 1, -y n + 1 (n = 1, 3, 5, ...), the budget less the ring's cell count
//     after each ring; a cell is tried only when nothing was found yet or its score is strictly below the best; the search returns at the start of the ring after
//     the one in which the best was last improved (the patience counter is reset to 1 by every improvement);
//   * RW 0x6EA0EC(unit, target, v) (no AI or RW 0x667C76 false): from the target's position along v the first of 20 steps whose cell is not an obstacle (type 4)
//     scales v by the step count;
//   * RW 0x7463E8(dest, unit, other = 0): the distance d0 from the unit to dest; RW 0x6FB754(unit, dest, weapon, 0) (lines from dest to the 8 points at
//     max(150 (RW 0xC041F8), the weapon's range) around it, RW 0x6FA9EF / 0x6F807D) and RW 0x6F5BB0 (quick path test, unit -> dest) -> true, dest unchanged; else
//     steps = -1 - (int)(d * -0.05) (RW 0xC29938) back from dest by 20 (RW 0xBDBC6C) toward the unit until RW 0x6F5BB0 passes (a step on layer > 1 or RW 0x6EAD32 marks
//     it), none -> false; a marked walk with RW 0x6FB754 false moves the point back by CastleSiegeStandBackDistance (AIData + 0xD4) + the unit's bounding radius;
//     accepted when marked or |dest - point| + 20 <= d0: RW 0x6F3C87 (adjustToPossibleDestination) on the point, dest = point, true; else false.
// INFERENCE (stop S-787): RW 0x68C9AC is the target's position; RW 0x667C76 is false; RW 0x6EA0EC's WALK_ON_TOP_OF_WALL cell test (RW 0x6E82B3) is the obstacle test;
// RW 0x6F3082 is checkDestination with no other unit's goal in it; RW 0x938939's obstacle-zone mapping and the siege-deploy branches (RW 0x863983 / 0x8C9C01) are
// not ported; RW 0x6FB754 is canApproachToTarget (lane PLAY-4, its callback RW 0x6F61BD in AIPathfindSearch.cpp); RW 0x6F5BB0 is clientSafeQuickDoesPathExist. Lane
// WALL-2 ported the mark (RW 0x74661A: the step's layer above the ground, or RW 0x6EAD32: its ground cell an obstacle with bit 17).

#include "GameLogic/AI/AIPathfindHost.h"

bool Pathfinder::meleeOwnFootprintFree(const PathfindObject &obj)
{
	const ICoord2D here = cellOfPosition(obj, obj.getPosition());
	if (here.x < 0 || here.y < 0)
	{
		return false;
	}
	const int footprint = footprintSize(&obj);
	const int below = footprint / 2;
	const int above = footprint - below;
	const PathfindObjectID self = obj.getID();
	const bool human = !obj.isComputerControlled();
	const bool largeRect = obj.isKindOf(PK_LARGE_RECTANGLE_PATHFIND);
	for (int x = here.x - below; x < here.x + above; ++x)
	{
		for (int y = here.y - below; y < here.y + above; ++y)
		{
			const PathfindCell *c = getCell(LAYER_GROUND, x, y);
			if (c == nullptr || c->getType() == PathfindCell::CELL_BRIDGE_IMPASSABLE || (c->getImpassableToPlayers() && human) || c->getType() == PathfindCell::CELL_OBSTACLE)
			{
				return false;
			}
			for (const PathfindOccupant *o = c->occupants(OCC_GROUND_GOAL); o; o = o->next)
			{
				if (o->owner == self)
				{
					continue;
				}
				if (!largeRect)
				{
					return false; // RW 0x93447C
				}
				const PathfindObject *other = m_world ? m_world->findObjectByID(o->owner) : nullptr;
				if (other && other->isKindOf(PK_LARGE_RECTANGLE_PATHFIND))
				{
					return false;
				}
			}
		}
	}
	return true;
}

namespace
{
// RW 0x6E8523 (x87 under PC24, the integers loaded exactly): (10x - unit.x)^2 + (10y - unit.y)^2, compared as float
float engagementScore(int x, int y, const Coord3D &unit)
{
	const double ax = SimMath::pc24SubW((double)(x * 10), (double)unit.x);
	const double ay = SimMath::pc24SubW((double)(y * 10), (double)unit.y);
	return SimMath::fstpDword(SimMath::pc24AddW(SimMath::pc24MulW(ax, ax), SimMath::pc24MulW(ay, ay)));
}

float lengthOf(float x, float y, float z)
{
	return SimMath::fstpDword(SimMath::sqrtPC24((double)SimMath::sumSquares3(x, y, z)));
}
} // namespace

bool Pathfinder::findMeleeEngagementLocation(const PathfindObject &obj, const PathfindLocomotorInfo &loco, const PathfindObject &target, const PathfindAttackRange &range,
	Coord3D *dest)
{
	const Coord3D self = obj.getPosition();
	if (range.inRangeFrom(self, 0.0f) && meleeOwnFootprintFree(obj))
	{
		*dest = self;
		return true;
	}
	// the point 10 ahead of the unit and the step v from the caller's point toward it
	const Coord3D ahead{ SimMath::addf32(self.x, SimMath::mulf32(SimMath::cosDet(obj.getOrientation()), 10.0f)),
		SimMath::addf32(self.y, SimMath::mulf32(SimMath::sinDet(obj.getOrientation()), 10.0f)), self.z };
	float vx = SimMath::subf32(ahead.x, dest->x), vy = SimMath::subf32(ahead.y, dest->y), vz = 0.0f;
	{
		const float len = lengthOf(vx, vy, vz);
		if (len != 0.0f)
		{
			const float inv = SimMath::divf32(1.0f, len);
			vx = SimMath::mulf32(vx, inv);
			vy = SimMath::mulf32(vy, inv);
		}
		vx = SimMath::mulf32(vx, 10.0f);
		vy = SimMath::mulf32(vy, 10.0f);
	}
	const bool wallUpgrade = target.isKindOf(PK_WALL_UPGRADE);
	if ((target.isKindOf(PK_STRUCTURE) || target.isKindOf(PK_WALK_ON_TOP_OF_WALL)) && !wallUpgrade)
	{
		// RW 0x6EA0EC: the first of 20 steps from the target along v that leaves the obstacle
		const Coord3D t = target.getPosition();
		for (int i = 0; i < 20; ++i)
		{
			const float fi = SimMath::sseFromInt32(i);
			const Coord3D p{ SimMath::addf32(t.x, SimMath::mulf32(vx, fi)), SimMath::addf32(t.y, SimMath::mulf32(vy, fi)), t.z };
			const PathfindCell *c = getCell(LAYER_GROUND, &p);
			if (c == nullptr)
			{
				break;
			}
			if (c->getType() != PathfindCell::CELL_OBSTACLE)
			{
				vx = SimMath::mulf32(vx, fi);
				vy = SimMath::mulf32(vy, fi);
				break;
			}
		}
	}
	int radius = 0;
	bool center = false;
	getRadiusAndCenter(&obj, radius, center);
	const PathfindMovement mv = makeMovement(&obj, loco);
	const bool human = !obj.isComputerControlled();
	for (int round = 0;; ++round)
	{
		*dest = target.getPosition();
		if (wallUpgrade)
		{
			vx = 0.0f;
			vy = 0.0f;
		}
		dest->x = SimMath::addf32(dest->x, vx);
		dest->y = SimMath::addf32(dest->y, vy);
		if (!center)
		{
			dest->x = SimMath::addf32(dest->x, 5.0f);
			dest->y = SimMath::addf32(dest->y, 5.0f);
		}
		ICoord2D cell;
		{
			Coord3D d = *dest;
			if (worldToCell(&d, true, &cell))
			{
				return false; // RW 0x6E8DD5 gives x < 0 off the map
			}
		}
		Coord3D aheadClip = ahead;
		const PathfindCell *own = getClippedCell(obj.getLayer(), &aheadClip);
		if (own == nullptr)
		{
			return false;
		}
		const zoneStorageType zone = m_zoneManager.getEffectiveZone(mv, true, own->getZone());
		// lane WALL-2, RW 0x6F39AD .. 0x6F3A05: the search's layer is the destination's (RW 0x680A75), the ground for a WALK_ON_TOP_OF_WALL target that is not
		// WALL_UPGRADE
		PathfindLayerEnum layer = getLayerAt(dest->x, dest->y, dest->z);
		if (target.isKindOf(PK_WALK_ON_TOP_OF_WALL) && !wallUpgrade)
		{
			layer = LAYER_GROUND;
		}
		const PathfindCell *destCell = getCell(layer, cell.x, cell.y);
		if (destCell && m_zoneManager.getEffectiveZone(mv, true, destCell->getZone()) == zone)
		{
			int crowd = 0;
			if (checkDestination(&obj, cell.x, cell.y, layer, radius, center, &crowd, false) && crowd == 0 && range.inRangeFromTo(self, *dest, 0.0f))
			{
				return true;
			}
		}
		// RW 0x6EFE9F with RW 0x6ED798 / 0x6E8523
		auto predicate = [&](int x, int y, Coord3D &out) {
			const PathfindCell *c = getCell(layer, x, y);
			if (c == nullptr || c->getType() == PathfindCell::CELL_OBSTACLE || c->getType() == PathfindCell::CELL_BRIDGE_IMPASSABLE ||
				(c->getImpassableToPlayers() && human) || m_zoneManager.getEffectiveZone(mv, true, c->getZone()) != zone)
			{
				return false; // RW 0x6E9070
			}
			adjustCoordToCell(x, y, center, out, layer);
			return range.inRangeFrom(out, 0.0f) && meleeFootprintFree(obj, x, y, layer, radius, center);
		};
		bool found = false;
		float best = 0.0f;
		Coord3D bestPos = *dest;
		const Coord3D destStart = *dest;
		for (int attempt = 0; attempt < 2 && !found; ++attempt)
		{
		if (attempt == 1)
		{
			// RW 0x6F3BBD .. 0x6F3C14: a WALL_UPGRADE target searched off the ground is searched again on the ground with twice the budget
			if (!wallUpgrade || layer == LAYER_GROUND)
			{
				break;
			}
			layer = LAYER_GROUND;
			bestPos = destStart;
		}
		auto visit = [&](int x, int y, int &patience) {
			const float score = engagementScore(x, y, self);
			Coord3D p;
			if ((!found || score < best) && predicate(x, y, p))
			{
				found = true;
				best = score;
				bestPos = p;
				patience = 1;
			}
		};
		int patience = 10000;
		{
			Coord3D p;
			if (predicate(cell.x, cell.y, p))
			{
				found = true;
				best = engagementScore(cell.x, cell.y, self);
				bestPos = p;
				patience = 1;
			}
		}
		int budget = attempt == 0 ? config().findMeleeEngagementLimit : 2 * config().findMeleeEngagementLimit;
		int ox = 0, oy = 0, n = 1, ringCost = 4;
		while (budget > 0)
		{
			if (--patience < 1)
			{
				break;
			}
			budget -= 2 + ringCost;
			for (int k = 0; k < n; ++k)
			{
				++ox;
				visit(cell.x + ox, cell.y + oy, patience);
			}
			for (int k = 0; k < n; ++k)
			{
				++oy;
				visit(cell.x + ox, cell.y + oy, patience);
			}
			for (int k = 0; k < n + 1; ++k)
			{
				--ox;
				visit(cell.x + ox, cell.y + oy, patience);
			}
			for (int k = 0; k < n + 1; ++k)
			{
				--oy;
				visit(cell.x + ox, cell.y + oy, patience);
			}
			n += 2;
			ringCost += 8;
		}
		}
		if (found)
		{
			*dest = bestPos;
			return true;
		}
		if (!(SimMath::addf32(SimMath::mulf32(vy, vy), SimMath::mulf32(vx, vx)) > 1.0f) || round >= 1)
		{
			return false;
		}
		vx = 0.0f;
		vy = 0.0f;
	}
}

bool Pathfinder::meleeBackOff(PathfindObject &obj, const PathfindLocomotorInfo &loco, float weaponRange, Coord3D *dest, bool meleeWeapon)
{
	const Coord3D pos = obj.getPosition();
	const float d0 = lengthOf(SimMath::subf32(dest->x, pos.x), SimMath::subf32(dest->y, pos.y), 0.0f);
	// RW 0x6FB754 (lane PLAY-4: CanApproachToTarget ported; PHYS-1 had taken isLinePassable for it, whose goal reservations of other units closed every
	// line out of a horde, so a lone melee unit never got near a horde member)
	const bool open = canApproachToTarget(obj, loco, *dest, weaponRange, meleeWeapon);
	if (open && clientSafeQuickDoesPathExist(loco, &pos, dest, &obj)) // RW 0x6F5BB0 with the unit (lane WALL-2: its portals)
	{
		return true;
	}
	float dx = SimMath::subf32(dest->x, pos.x), dy = SimMath::subf32(dest->y, pos.y);
	// RW 0x746563 .. 0x746579: the root left in the register, fmul -0.05 (RW 0xC29938), _ftol2; steps = -1 - that
	const double lenW = SimMath::sqrtPC24((double)SimMath::sumSquares3(dx, dy, 0.0f));
	const float len = SimMath::fstpDword(lenW);
	const int steps = -1 - (int)SimMath::ftol2Low32(SimMath::pc24MulW(lenW, (double)-0.05f));
	if (len != 0.0f)
	{
		const float inv = SimMath::divf32(1.0f, len);
		dx = SimMath::mulf32(dx, inv);
		dy = SimMath::mulf32(dy, inv);
	}
	dx = SimMath::mulf32(dx, 20.0f);
	dy = SimMath::mulf32(dy, 20.0f);
	Coord3D p = *dest;
	bool reached = false;
	bool marked = false; // RW EBP - 0xD
	for (int i = 0; i < steps; ++i)
	{
		p.x = SimMath::subf32(p.x, dx);
		p.y = SimMath::subf32(p.y, dy);
		if (clientSafeQuickDoesPathExist(loco, &pos, &p, &obj))
		{
			reached = true;
			break;
		}
		// lane WALL-2, RW 0x74661A .. 0x746648: a step the quick test refuses marks the walk when its layer (RW 0x680A75) is above the ground (a wall top, a ramp,
		// a walkway) or its ground cell is an obstacle with bit 17 (RW 0x6EAD32(pos, 1): a wall or gate footprint)
		if ((int)getLayerAt(p.x, p.y, p.z) > (int)LAYER_GROUND)
		{
			marked = true;
		}
		else if (const PathfindCell *c = cellAt(SimMath::floorToInt(SimMath::mulf32(p.x, 0.1f)), SimMath::floorToInt(SimMath::mulf32(p.y, 0.1f))))
		{
			marked = marked || (c->getType() == PathfindCell::CELL_OBSTACLE && c->getBit17());
		}
	}
	if (!reached)
	{
		return false;
	}
	bool backed = false;
	if (marked && !open)
	{
		const float m = SimMath::addf32(m_config.castleSiegeStandBackDistance, obj.getGeometry().boundingCircleRadius());
		const float l = lengthOf(dx, dy, 0.0f);
		const float inv = SimMath::divf32(1.0f, l);
		p.x = SimMath::subf32(p.x, SimMath::mulf32(SimMath::mulf32(dx, inv), m));
		p.y = SimMath::subf32(p.y, SimMath::mulf32(SimMath::mulf32(dy, inv), m));
		backed = true;
	}
	const float d = lengthOf(SimMath::subf32(dest->x, p.x), SimMath::subf32(dest->y, p.y), 0.0f);
	if (!backed && SimMath::addf32(d, 20.0f) > d0)
	{
		return false;
	}
	adjustToPossibleDestination(obj, loco, &p);
	*dest = p;
	return true;
}
