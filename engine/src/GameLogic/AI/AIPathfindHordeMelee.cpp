// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Lane ATTACKPATH-1: the pathfinder helpers of a horde's melee approach goal (AIAttackMeleeHordeApproachTargetState::computePath RW 0x74A594, stop S-590),
// ported from the RotWK disassembly with the BFME2 decomp's byte-matched sources as names and cross-checks:
//   * RW 0x6F2FFF Pathfinder::IsHordeMeleeLinePassable (tier A, decomp AI/PathfinderCoordLineWalk.cpp; its record RW 0x6EE496 and cell callback RW 0x6EE524,
//     decomp Pathfinder/PathfinderRva002ED15ACellCallback.cpp Rva002ED15AInfo);
//   * RW 0x746783 (tier B same-shape, decomp AI/AIStatesBfmeOnExit.cpp AIAttackMeleeHordeApproachTargetState::rva00344D60): the goal walked back toward the horde;
//   * RW 0x6FB67A Pathfinder::AdjustHordeMeleeDestination (tier A, decomp Pathfinder/pathfinder.cpp) with its ring RW 0x6FA733 (decomp rva002F92BC,
//     "IterateCircular1"), the functor RW 0x6EEAF5 / 0x6F8540 (Pathfinder_IsCheckForMeleeHordeAdjust), its cell test RW 0x6F6C18 and footprint test RW 0x6EB2EF.
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * RW 0x6F2FFF(obj, surfaces, layer, from, to): the record (RW 0x6EE496): the unit's footprint radius and centring (RW 0x6ED071), mode flags 0x0C (terrain
//     failures and layer mismatches counted, units ignored), not transient, the movement (surfaces, not CanPathThroughGates, SlopeLimitIndex - 1, the crusher
//     test RW 0x68B331) twice (one for the check, one for the validity test); both ends to cells (RW 0x6E8CE6, centred), the Bresenham walk RW 0x6F05B1; per
//     cell (RW 0x6EE524): checkForMovement (RW 0x6EB792 at the first cell, RW 0x6EBAA0 with the previous cell after it) refused or a counted terrain failure
//     (+ 0x34) blocks; the record's + 0x30 byte with a second hit blocks too (checkForMovement clears it and never sets it: no effect); then the cell must be
//     a valid movement position (RW 0x6E8200); passable = the walk returned 0;
//   * RW 0x746783(goal, horde, victim): no AI -> false; v = (goal - horde) with z = 0; count = -1 - _ftol(|v| (RW 0x403111, the root in the register) * -0.05f
//     (RW 0xC29938)); v normalised (RW 0x405553) and times 10 (SSE); from the goal, `count` points stepping by -v: the first whose one-point line
//     RW 0x6F2FFF(horde, AI + 0x1DC surfaces, the horde's layer, p, p) passes becomes the goal (true); none -> adjustToPossibleDestination (RW 0x6F3C87), false;
//   * RW 0x6FB67A(obj, locomotors, dest): a PROJECTILE (template + 0x10B bit 1) -> true; centred = the footprint size is odd (RW 0x6ECFC5); the cell of dest
//     (RW 0x6E8CE6 with that centring); the functor (RW 0x6EEAF5): human (not a computer player), the footprint radius / centring (RW 0x6ED071), the layer for
//     dest (RW 0x680A75), the terrain height there (TerrainLogic vslot 0x1C), the goal = dest, no fallback; the ring RW 0x6FA733 over MaxCellsAdjustHordeMelee
//     Destination (GameData + 0x11F4): the cell itself, then rings of growing extent (the budget drops by 4 * extent + 2 each ring; per ring +x extent times,
//     +y extent times, -x extent + 1 times, -y extent + 1 times, the offsets carried from ring to ring; extent += 2), a cell is tested only while none was
//     found in the ring or it is nearer (y*y + x*x, strictly) than the best; a ring with a hit ends the search; found -> that cell; else the functor's lowest-count
//     fallback cell, if any; else false; dest = the cell's coordinate (RW 0x6EA0CB -> 0x6E8E19 with the centring and the layer), true;
//   * the functor test RW 0x6F8540 -> RW 0x6F6C18(x, y): the cell exists, is not a cliff (type 2), lies in the logical extent for a human; the footprint test
//     RW 0x6EB2EF passes with an occupancy count; the cell's coordinate for the unit (RW 0x6ED023); an AIRCRAFT accepts it at once; else with the terrain height
//     <= 0 (RW 0xC1B594) or |coordinate z - height| <= 50 (RW 0xBD88C4): accepted when a quick path (RW 0x6F5BB0) leads from the unit to it, or none leads from
//     the unit to the functor's goal but one leads from that goal to it; an accepted cell becomes the functor's goal; then a count of 0 is a hit, a positive one
//     replaces the fallback when lower (or the first);
//   * RW 0x6EB2EF(obj, x, y, layer, radius, centred, &count): a HORDE gets radius 1, centred, unless it is on the ground layer and TerrainLogic vslot 0xC8 (the passage
//     plane) of the cell's centre is false; a HERO INFANTRY off the ground gets radius 1, not centred; over the footprint cells: none, type 5, a human's bit-18 cell or a
//     cliff -> false; an obstacle cell must be the AI's ignored obstacle (else false; nothing else is checked there); type 6 -> false; the goal list
//     (info + 0x14): every unit that is not the unit, not its ignored obstacle and not contained by it: an ally adds 1, a non-ally standing at its goal
//     (RW 0x68B47F) -> false; the position list (info + 0x20): every unit that is not the unit and not contained by it adds 3 for an ally, else 1.
// INFERENCE / NOT PORTED (stop S-590): the layer for a destination is the ground (S-161); the quick path test RW 0x6F5BB0 is the port's clientSafeQuickDoesPathExist (as in canApproachToTarget).

#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIPathfindLine.h"
#include "GameLogic/SimMath.h"

#include <functional>

namespace
{
// RW 0x6E8CE6 (no clamp): centred floor(v * 0.1), else floor(v * 0.1 + 0.5)
inline int cellIndex(float v, bool centred)
{
	return centred ? SimMath::floorToInt(SimMath::mulf32(v, 0.1f)) : SimMath::floorToInt(SimMath::addf32(SimMath::mulf32(v, 0.1f), 0.5f));
}
} // namespace

bool Pathfinder::isHordeMeleeLinePassable(const PathfindObject *obj, unsigned surfaces, PathfindLayerEnum layer, const Coord3D &from, const Coord3D &to)
{
	// TARGET RW 0x6F2FFF (see the file comment)
	PathfindCheckMovement info;
	getRadiusAndCenter(obj, info.radius, info.center);
	info.flags = 0x0C;
	info.transient = false;
	info.ignoreId = PATHFIND_INVALID_ID;
	info.mv = makeMovement(obj, PathfindLocomotorInfo{ surfaces, false, false });
	info.mv.bC = obj ? obj->getCrusherLevel() > 0 : false;
	const PathfindMovement valid = info.mv;
	const ICoord2D a{ cellIndex(from.x, true), cellIndex(from.y, true) };
	const ICoord2D b{ cellIndex(to.x, true), cellIndex(to.y, true) };
	ICoord2D prev{ 0, 0 };
	const bool blocked = walkLine(*this, layer, a, b, [&](PathfindCell *fromCell, PathfindCell *toCell, int x, int y) {
		info.cell.x = x;
		info.cell.y = y;
		info.layer = toCell->getLayer();
		if (!checkForMovement(obj, info, fromCell ? &prev : nullptr) || info.terrainPenalty != 0)
		{
			return true;
		}
		prev.x = x;
		prev.y = y;
		return !validMovementPosition(valid, toCell);
	});
	return !blocked;
}

bool Pathfinder::hordeMeleeStepBack(PathfindObject &obj, const PathfindLocomotorInfo &loco, Coord3D *goal)
{
	// TARGET RW 0x746783 (see the file comment)
	if (!obj.hasAI())
	{
		return false;
	}
	const Coord3D &src = obj.getPosition();
	Coord3D cand = *goal;
	float vx = SimMath::subf32(cand.x, src.x), vy = SimMath::subf32(cand.y, src.y);
	const double len = SimMath::sqrtPC24((double)SimMath::sumSquares3(vx, vy, 0.0f));
	const int count = -1 - (int)SimMath::ftol2Low32(SimMath::pc24MulW(len, (double)-0.05f));
	{
		const float l = SimMath::fstpDword(len);
		const float inv = SimMath::divf32(1.0f, l);
		vx = SimMath::mulf32(SimMath::mulf32(vx, inv), 10.0f);
		vy = SimMath::mulf32(SimMath::mulf32(inv, vy), 10.0f);
	}
	for (int i = 0; i < count; ++i)
	{
		if (isHordeMeleeLinePassable(&obj, loco.validSurfaces, obj.getLayer(), cand, cand))
		{
			*goal = cand;
			return true;
		}
		cand.x = SimMath::subf32(cand.x, vx);
		cand.y = SimMath::subf32(cand.y, vy);
	}
	adjustToPossibleDestination(obj, loco, goal);
	return false;
}

bool Pathfinder::hordeMeleeFootprint(const PathfindObject &obj, int x, int y, PathfindLayerEnum layer, int radius, bool centred, int *count)
{
	// TARGET RW 0x6EB2EF (see the file comment)
	int r = radius;
	bool c = centred;
	if (obj.isKindOf(PK_HORDE))
	{
		// RW 0x6EB30B .. 0x6EB33B: on the ground the horde keeps its footprint unless TerrainLogic vslot 0xC8 (the passage plane, RW 0x4627C1) holds at the
		// cell's centre (RW 0x6E8E19, centred); off the ground it always narrows
		bool narrow = layer != LAYER_GROUND;
		if (!narrow && m_terrain)
		{
			Coord3D centre;
			adjustCoordToCell(x, y, true, centre, LAYER_GROUND);
			narrow = m_terrain->passagePlane(centre);
		}
		if (narrow)
		{
			r = 1;
			c = true;
		}
	}
	if (obj.isKindOf(PK_HERO) && obj.isKindOf(PK_INFANTRY) && layer != LAYER_GROUND)
	{
		c = false;
		r = 1;
	}
	const int n = r + (c ? 1 : 0);
	*count = 0;
	const PathfindObjectID ignoreId = obj.hasAI() ? obj.getIgnoredObstacleID() : PATHFIND_INVALID_ID;
	const PathfindObjectID selfId = obj.hasAI() ? obj.getID() : PATHFIND_INVALID_ID;
	const bool isHuman = !obj.isComputerControlled();
	for (int i = x - r; i < x + n; ++i)
	{
		for (int j = y - r; j < y + n; ++j)
		{
			const PathfindCell *cell = getCell(layer, i, j);
			if (cell == nullptr || cell->getType() == PathfindCell::CELL_BRIDGE_IMPASSABLE || (cell->getImpassableToPlayers() && isHuman))
			{
				return false;
			}
			const PathfindCell::CellType t = cell->getType();
			if (t == PathfindCell::CELL_CLIFF)
			{
				return false;
			}
			if (t == PathfindCell::CELL_OBSTACLE)
			{
				if (!cell->isObstaclePresent(ignoreId))
				{
					return false;
				}
				continue;
			}
			if (t == PathfindCell::CELL_IMPASSABLE)
			{
				return false;
			}
			for (const PathfindOccupant *node = cell->occupants(OCC_GROUND_GOAL); node; node = node->next)
			{
				const PathfindObject *o = m_world ? m_world->findObjectByID(node->owner) : nullptr;
				if (o == nullptr || o->getID() == selfId || o->getID() == ignoreId || o->getContainer() == &obj)
				{
					continue;
				}
				if (obj.getRelationship(*o) == PATHFIND_ALLIES)
				{
					*count += 1;
				}
				else if (o->isParked())
				{
					return false;
				}
			}
			for (const PathfindOccupant *node = cell->occupants(OCC_POSITION); node; node = node->next)
			{
				const PathfindObject *o = m_world ? m_world->findObjectByID(node->owner) : nullptr;
				if (o == nullptr || o->getID() == selfId || o->getContainer() == &obj)
				{
					continue;
				}
				*count += obj.getRelationship(*o) == PATHFIND_ALLIES ? 3 : 1;
			}
		}
	}
	return true;
}

bool Pathfinder::adjustHordeMeleeDestination(PathfindObject &obj, const PathfindLocomotorInfo &loco, Coord3D *dest)
{
	// TARGET RW 0x6FB67A with RW 0x6FA733 / 0x6EEAF5 / 0x6F8540 / 0x6F6C18 (see the file comment)
	if (obj.isKindOf(PK_PROJECTILE))
	{
		return true;
	}
	int radius = 0;
	bool centred = false;
	getRadiusAndCenter(&obj, radius, centred);
	const ICoord2D centre{ cellIndex(dest->x, centred), cellIndex(dest->y, centred) };
	const PathfindLayerEnum layer = LAYER_GROUND; // RW 0x680A75: the layer for the destination (only the ground, S-161)
	const float height = m_terrain ? m_terrain->getGroundHeight(dest->x, dest->y) : 0.0f;
	const bool isHuman = !obj.isComputerControlled();
	const Coord3D unitPos = obj.getPosition();
	Coord3D goal = *dest; // the functor's goal (+ 0x28), replaced by every accepted cell
	int fallbackCount = 0;
	ICoord2D fallback{ 0, 0 };
	// RW 0x6F8540 -> RW 0x6F6C18
	auto test = [&](int x, int y) -> bool {
		const PathfindCell *cell = getCell(layer, x, y);
		if (cell == nullptr || cell->getType() == PathfindCell::CELL_CLIFF)
		{
			return false;
		}
		if (isHuman && !(m_logicalExtent.lo.x <= x && m_logicalExtent.lo.y <= y && x <= m_logicalExtent.hi.x && y <= m_logicalExtent.hi.y))
		{
			return false;
		}
		int count = 0;
		if (!hordeMeleeFootprint(obj, x, y, layer, radius, centred, &count))
		{
			return false;
		}
		Coord3D at;
		adjustCoordToCell(x, y, centred, at, cell->getLayer()); // RW 0x6ED023
		bool accepted = false;
		if (obj.isKindOf(PK_AIRCRAFT))
		{
			accepted = true;
		}
		else if (height <= 0.0f || SimMath::absD((double)SimMath::subf32(at.z, height)) <= 50.0)
		{
			bool ok = clientSafeQuickDoesPathExist(loco, &unitPos, &at);
			if (!clientSafeQuickDoesPathExist(loco, &unitPos, &goal) && clientSafeQuickDoesPathExist(loco, &goal, &at))
			{
				ok = true;
			}
			accepted = ok;
		}
		if (!accepted)
		{
			return false;
		}
		goal = at;
		if (count == 0)
		{
			return true;
		}
		if (count < fallbackCount || fallbackCount == 0)
		{
			fallbackCount = count;
			fallback = ICoord2D{ x, y };
		}
		return false;
	};
	// RW 0x6FA733 (IterateCircular1)
	ICoord2D found{ 0, 0 };
	bool hit = false;
	if (test(centre.x, centre.y))
	{
		found = centre;
		hit = true;
	}
	else
	{
		int budget = m_config.adjustHordeMeleeLimit;
		int best = 0, extent = 1, ox = 0, oy = 0;
		auto visit = [&]() {
			if ((best == 0 || oy * oy + ox * ox < best) && test(centre.x + ox, centre.y + oy))
			{
				best = oy * oy + ox * ox;
				found = ICoord2D{ centre.x + ox, centre.y + oy };
			}
		};
		while (budget > 0)
		{
			budget -= 2 + 4 * extent;
			for (int k = 0; k < extent; ++k)
			{
				++ox;
				visit();
			}
			for (int k = 0; k < extent; ++k)
			{
				++oy;
				visit();
			}
			for (int k = 0; k <= extent; ++k)
			{
				--ox;
				visit();
			}
			for (int k = 0; k <= extent; ++k)
			{
				--oy;
				visit();
			}
			if (best != 0)
			{
				hit = true;
				break;
			}
			extent += 2;
		}
	}
	if (!hit)
	{
		if (fallbackCount == 0)
		{
			return false;
		}
		found = fallback;
	}
	adjustCoordToCell(found.x, found.y, centred, *dest, layer); // RW 0x6EA0CB -> 0x6E8E19
	return true;
}
