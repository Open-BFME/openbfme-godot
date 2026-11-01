// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Lane ATTACKPATH-1: Pathfinder::isAttackViewBlockedByObstacle, the attack view test of the attack path (ZH AIPathfind.cpp isAttackViewBlockedByObstacle; RotWK
// body RW 0x6F4557, tier A to the BFME2 decomp's Pathfinder/PathfinderAttackView.cpp, byte-matched; its cell callback RW 0x6F2D31, tier B to the decomp's
// AI/cellCallbackRva002F18D4Farm.cpp Rva002F18D4Info::cellCallback, read here from the RotWK disassembly where the two differ: the container field is
// Object + 0x27C, the slaver lookup RW 0x6E82C8 walks the behaviour list at + 0x24C).
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * RW 0x6F4557(attacker, attackerPos, victim, victimPos): AIData + 0x67 (AttackUsesLineOfSight, field row RW 0xC1CCA0, parseBool) clear -> false; no victim
//     -> false; the attacker neither KindOf ATTACK_NEEDS_LINE_OF_SIGHT (template + 0x10F bit 3 = 59) nor CAN_SHOOT_OVER_WALLS (+ 0x122 bit 6 = 214) -> false;
//   * the current weapon (RW 0x68B58C(0); none for an IMMOBILE attacker, + 0x108 bit 2): RW 0x6CC6DD(attacker, attackerPos, victim) = the weapon's terrain line
//     (the launch point RW 0x6CA33E, the victim's aim point RW 0x6CC37B, a WALK_ON_TOP_OF_WALL victim's RW 0x6EFD35, TerrainLogic vslot 0x3C) false -> true;
//   * the layers (RW 0x68BBE0) of attacker and victim differ and either is a raised layer (2 .. 15): both points at the higher z mapped to a layer
//     (RW 0x680A75); the same layer -> true;
//   * the record (RW 0x6E8845): attacker, victim, the victim's cell (RW 0x5E2EF2 on the victim's layer: RW 0x6E8DD5, centred, -1 off the extent), skip 0;
//     an attacker whose layer is neither the ground nor a raised layer (RW 0x76550F) skips 3 cells, and a ground victim's walk then takes the attacker's layer;
//   * the end point: victimPos + Normalize2D(attackerPos - victimPos) * 5 (RW 0x405595: x87 y*y + x*x, the CRT root RW 0xA3CF96 under PC24, FST float,
//     1 / len (SSE), x and y scaled; z kept; then every component * 5, SSE); RW 0x6F44BD: both ends to cells (RW 0x6E8CE6, centred, no clamp), the
//     Bresenham walk RW 0x6F334C from the attacker's cell; a cell off the grid ends the walk (not blocked); a nonzero callback result is the answer;
//   * the callback RW 0x6F2D31 per cell (the layer counters + 0x14 / + 0x18 / + 0x10 are kept but read by nobody here): skip > 0 -> skip - 1, go on; not an
//     obstacle cell (type 4) -> go on; the obstacle is (RW 0x6E80B5: an obstacle cell whose info + 0x28 is the id) the attacker, the victim, the victim's
//     slaver (RW 0x6E82C8), the attacker's container (+ 0x27C) or that one's container, the attacker's slaver -> go on; a transparent obstacle (RW 0x6E80DF:
//     info + 0x2C bit 2) -> go on; the victim cell's obstacle id -> go on; the obstacle's object (RW 0x449681) gone -> blocked;
//     a wall obstacle (KindOf WALK_ON_TOP_OF_WALL 60, WALL_SEGMENT 189, WALL_GATE 203 or WALL_UPGRADE 150): blocked when the attacker is on the ground layer
//     and there is no victim, or the victim is on the ground layer too and (the attacker is not CAN_SHOOT_OVER_WALLS, or the cell corner (x * 10, y * 10) is
//     not farther from the victim than from the attacker: (vy^2 + vx^2) <= (ay^2 + ay^2), SSE); else go on;
//     any other obstacle: blocked unless the attacker is CAN_SHOOT_OVER_WALLS and not ATTACK_NEEDS_LINE_OF_SIGHT.
// Lane LOS-1: the weapon's terrain line RW 0x6CC6DD is ported (GameLogic/Combat/CombatLineOfSight.cpp, reached through PathfindObject::
// isClearGoalFiringLineOfSightTerrain), and so is RW 0x6EFD35 (adjustToWallTopPoint below), the line-of-sight point of a WALK_ON_TOP_OF_WALL victim.
// Only the ground layer exists (S-161): the raised-layer branch and the skip of 3 cannot fire, and every ground cell's layer field is the ground, so
// adjustToWallTopPoint stops at the victim's own cell (retail walks on to the wall's edge).

#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/SimMath.h"

#include <cstdint>

namespace
{
// RW 0x6E8CE6 with the centring flag 1: floor(v * 0.1) as an int, no clamp; NaN (a zero-length Normalize2D) gives the integer indefinite like the retail FISTP
int cellIndexCentred(float v)
{
	return SimMath::floorToInt(SimMath::mulf32(v, 0.1f));
}

// RW 0x6F2D31's record (RW 0x6E8845: + 0 attacker, + 4 victim, + 8 the victim's cell, + 0xC skip; the layer counters are not kept)
struct AttackViewWalk
{
	const PathfindObject *attacker = nullptr;
	const PathfindObject *victim = nullptr;
	const PathfindCell *victimCell = nullptr;
	int skip = 0;
};

int attackViewCellBlocks(const PathfindWorld *world, const AttackViewWalk &w, int &skip, const PathfindCell &cell, int x, int y)
{
	// TARGET RW 0x6F2D31 (see the file comment)
	if (skip > 0)
	{
		--skip;
		return 0;
	}
	if (cell.getType() != PathfindCell::CELL_OBSTACLE)
	{
		return 0;
	}
	const PathfindObject &obj = *w.attacker;
	if (cell.isObstaclePresent(obj.getID()))
	{
		return 0;
	}
	if (w.victim)
	{
		if (cell.isObstaclePresent(w.victim->getID()) || cell.isObstaclePresent(w.victim->getSlaverID()))
		{
			return 0;
		}
	}
	if (const PathfindObject *held = obj.getContainer())
	{
		if (cell.isObstaclePresent(held->getID()))
		{
			return 0;
		}
		const PathfindObject *held2 = held->getContainer();
		if (held2 && cell.isObstaclePresent(held2->getID()))
		{
			return 0;
		}
	}
	if (cell.isObstaclePresent(obj.getSlaverID()) || cell.isObstacleTransparent())
	{
		return 0;
	}
	if (w.victimCell && cell.isObstaclePresent(w.victimCell->getObstacleID()))
	{
		return 0;
	}
	const PathfindObject *unit = world ? world->findObjectByID(cell.getObstacleID()) : nullptr;
	if (unit == nullptr)
	{
		return 1;
	}
	const bool overWalls = obj.isKindOf(PK_CAN_SHOOT_OVER_WALLS);
	if (unit->isKindOf(PK_WALK_ON_TOP_OF_WALL) || unit->isKindOf(PK_WALL_SEGMENT) || unit->isKindOf(PK_WALL_GATE) || unit->isKindOf(PK_WALL_UPGRADE))
	{
		if (obj.getLayer() != LAYER_GROUND)
		{
			return 0;
		}
		if (w.victim == nullptr)
		{
			return 1;
		}
		if (w.victim->getLayer() != LAYER_GROUND)
		{
			return 0;
		}
		if (!overWalls)
		{
			return 1;
		}
		// RW 0x6F2E6B .. 0x6F2ED4 (SSE): the cell corner's squared distance to the victim against the attacker's, y^2 + x^2
		const float cx = SimMath::sseFromInt32(x * 10), cy = SimMath::sseFromInt32(y * 10);
		const Coord3D &a = obj.getPosition();
		const Coord3D &v = w.victim->getPosition();
		const float ax = SimMath::subf32(a.x, cx), ay = SimMath::subf32(a.y, cy);
		const float vx = SimMath::subf32(v.x, cx), vy = SimMath::subf32(v.y, cy);
		const float vd = SimMath::addf32(SimMath::mulf32(vy, vy), SimMath::mulf32(vx, vx));
		const float ad = SimMath::addf32(SimMath::mulf32(ay, ay), SimMath::mulf32(ax, ax));
		return vd <= ad ? 1 : 0;
	}
	if (!overWalls || obj.isKindOf(PK_ATTACK_NEEDS_LINE_OF_SIGHT))
	{
		return 1;
	}
	return 0;
}
} // namespace

bool Pathfinder::isAttackViewBlockedByObstacle(const PathfindObject &attacker, const Coord3D &attackerPos, const PathfindObject *victim, const Coord3D &victimPos)
{
	// TARGET RW 0x6F4557 (see the file comment)
	if (!m_config.attackUsesLineOfSight || victim == nullptr)
	{
		return false;
	}
	if (!attacker.isKindOf(PK_ATTACK_NEEDS_LINE_OF_SIGHT) && !attacker.isKindOf(PK_CAN_SHOOT_OVER_WALLS))
	{
		return false;
	}
	// RW 0x6F4596 .. 0x6F45D3: the current weapon (RW 0x68B58C(0); an IMMOBILE attacker has none) and its terrain line RW 0x6CC6DD from attackerPos: not clear -> blocked
	if (!attacker.isKindOf(PK_IMMOBILE) && attacker.hasCurrentWeapon() && !attacker.isClearGoalFiringLineOfSightTerrain(attackerPos, *victim))
	{
		return true;
	}
	// the raised-layer test (RW 0x6F45F5 .. 0x6F4683) and the skip of RW 0x6F46B1 need a layer other than the ground (S-161): none exists
	AttackViewWalk w;
	w.attacker = &attacker;
	w.victim = victim;
	{
		// RW 0x5E2EF2 -> 0x6E8DD5: the victim's cell, centred, none outside the extent
		const int vx = cellIndexCentred(victimPos.x), vy = cellIndexCentred(victimPos.y);
		if (vx >= m_extent.lo.x && vy >= m_extent.lo.y && vx <= m_extent.hi.x && vy <= m_extent.hi.y)
		{
			w.victimCell = getCell(LAYER_GROUND, vx, vy);
		}
	}
	// RW 0x6F46D2 .. 0x6F475D: Normalize2D(attackerPos - victimPos) (RW 0x405595), times 5, plus victimPos
	float dx = SimMath::subf32(attackerPos.x, victimPos.x), dy = SimMath::subf32(attackerPos.y, victimPos.y);
	{
		const float len = SimMath::fstpDword(SimMath::sqrtPC24((double)SimMath::sumSquares2(dy, dx)));
		const float inv = SimMath::divf32(1.0f, len);
		dx = SimMath::mulf32(dx, inv);
		dy = SimMath::mulf32(inv, dy);
	}
	const float endX = SimMath::addf32(victimPos.x, SimMath::mulf32(dx, 5.0f));
	const float endY = SimMath::addf32(victimPos.y, SimMath::mulf32(dy, 5.0f));
	// RW 0x6F44BD -> RW 0x6F334C: the Bresenham walk from the attacker's cell to the end's (no clamp), 32-bit wrapping like the binary
	const std::int32_t x0 = cellIndexCentred(attackerPos.x), y0 = cellIndexCentred(attackerPos.y);
	const std::int32_t x1 = cellIndexCentred(endX), y1 = cellIndexCentred(endY);
	auto wrapSub = [](std::int32_t a, std::int32_t b) { return (std::int32_t)((std::uint32_t)a - (std::uint32_t)b); };
	auto wrapAbs = [](std::int32_t a) { return a < 0 ? (std::int32_t)(0u - (std::uint32_t)a) : a; };
	auto wrapAdd = [](std::int32_t a, std::int32_t b) { return (std::int32_t)((std::uint32_t)a + (std::uint32_t)b); };
	std::int32_t ax = wrapAbs(wrapSub(x1, x0));
	std::int32_t ay = wrapAbs(wrapSub(y1, y0));
	const bool yMajor = ax < ay;
	std::int32_t d, incNE, count, incE;
	if (yMajor)
	{
		d = wrapSub(wrapAdd(ax, ax), ay);
		incNE = wrapSub(ax, ay);
		count = ay;
		incE = ax;
	}
	else
	{
		incNE = wrapSub(ay, ax);
		d = wrapSub(wrapAdd(ay, ay), ax);
		count = ax;
		incE = ay;
	}
	std::int32_t sxMinor = yMajor ? 0 : 1, syMinor = yMajor ? 1 : 0; // the step when d < 0: the major axis only
	std::int32_t sx = 1, sy = 1;
	if (x1 < x0)
	{
		sxMinor = -sxMinor;
		sx = -1;
	}
	if (y1 < y0)
	{
		syMinor = -syMinor;
		sy = -1;
	}
	std::int32_t x = x0, y = y0;
	int skip = w.skip;
	for (std::int32_t k = 0; k < wrapAdd(count, 1); ++k)
	{
		const PathfindCell *cell = getCell(LAYER_GROUND, x, y);
		if (cell == nullptr)
		{
			return false;
		}
		if (attackViewCellBlocks(m_world, w, skip, *cell, x, y) != 0)
		{
			return true;
		}
		std::int32_t stepX = sx, stepY = sy, inc = incNE;
		if (d < 0)
		{
			stepX = sxMinor;
			stepY = syMinor;
			inc = incE;
		}
		d = wrapAdd(d, wrapAdd(inc, inc));
		x = wrapAdd(x, stepX);
		y = wrapAdd(y, stepY);
	}
	return false;
}

bool Pathfinder::adjustToWallTopPoint(Coord3D &point, const Coord3D &toward)
{
	// TARGET RW 0x6EFD35 -> RW 0x6ECAC7(point, toward, layer 1, &out): both ends to cells (RW 0x6E8CE6, centred), then RW 0x6E9E66's walk from point's cell
	const std::int32_t x0 = cellIndexCentred(point.x), y0 = cellIndexCentred(point.y);
	const std::int32_t x1 = cellIndexCentred(toward.x), y1 = cellIndexCentred(toward.y);
	auto wrapSub = [](std::int32_t a, std::int32_t b) { return (std::int32_t)((std::uint32_t)a - (std::uint32_t)b); };
	auto wrapAbs = [](std::int32_t a) { return a < 0 ? (std::int32_t)(0u - (std::uint32_t)a) : a; };
	auto wrapAdd = [](std::int32_t a, std::int32_t b) { return (std::int32_t)((std::uint32_t)a + (std::uint32_t)b); };
	const std::int32_t ax = wrapAbs(wrapSub(x1, x0));
	const std::int32_t ay = wrapAbs(wrapSub(y1, y0));
	const bool yMajor = ax < ay;
	std::int32_t d, incNE, count, incE;
	if (yMajor)
	{
		d = wrapSub(wrapAdd(ax, ax), ay);
		incNE = wrapSub(ax, ay);
		count = ay;
		incE = ax;
	}
	else
	{
		incNE = wrapSub(ay, ax);
		d = wrapSub(wrapAdd(ay, ay), ax);
		count = ax;
		incE = ay;
	}
	std::int32_t sxMinor = yMajor ? 0 : 1, syMinor = yMajor ? 1 : 0;
	std::int32_t sx = 1, sy = 1;
	if (x0 > x1)
	{
		sxMinor = -sxMinor;
		sx = -1;
	}
	if (y0 > y1)
	{
		syMinor = -syMinor;
		sy = -1;
	}
	std::int32_t x = x0, y = y0;
	for (std::int32_t k = 0; k < wrapAdd(count, 1); ++k)
	{
		const PathfindCell *cell = getCell(LAYER_GROUND, x, y); // RW 0x5E2E9C(1, x, y)
		if (cell == nullptr)
		{
			return false;
		}
		// RW 0x6E9356: the cell's layer field not a wall layer (RW 0x6E82B3: 0x10 < layer < 0x41) -> its centre (RW 0x6E8E19 centred: (i + 0.5) * 10)
		const int layer = (int)cell->getLayer();
		if (!(layer > 0x10 && layer < 0x41))
		{
			point.x = SimMath::mulf32(SimMath::addf32(SimMath::sseFromInt32(x), 0.5f), 10.0f);
			point.y = SimMath::mulf32(SimMath::addf32(SimMath::sseFromInt32(y), 0.5f), 10.0f);
			return true;
		}
		std::int32_t stepX = sx, stepY = sy, inc = incNE;
		if (d < 0)
		{
			stepX = sxMinor;
			stepY = syMinor;
			inc = incE;
		}
		d = wrapAdd(d, wrapAdd(inc, inc));
		x = wrapAdd(x, stepX);
		y = wrapAdd(y, stepY);
	}
	return false;
}
