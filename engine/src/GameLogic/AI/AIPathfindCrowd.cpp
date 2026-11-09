// OpenBFME. GPL-3.0.
//
// Lane PHYS-1: the crowd test of the Amoeba's one-cell steps (RotWK, no ZH counterpart).
//
// TARGET FACTS (RotWK game.dat, caveat S-001): the Amoeba's candidate loop (RW 0x9905EB .. 0x9905F8) calls RW 0x6F1C90(unit, candidate) for each of the eight cells
// before the history test RW 0x98F371 and the scoring:
//   1. RW 0x6F1B3E(unit, position, candidate): a step that ends in the unit's own cell (RW 0x6EF865) passes; else the candidate's cell must be a valid movement cell for
//      the unit's locomotor set (RW 0x6EA04D / 0x6E8200) on the same layer;
//   2. the footprint extents of RW 0x6ED0A0 (the cells x - below .. x + above - 1): the number of goal reservations of OTHER units (cell info + 0x14, the ground goal list)
//      over the footprint around the unit's current cell (RW 0x6ECFDE);
//   3. over the footprint around the candidate's cell: a cell with info must be a valid movement cell, each goal reservation of another unit counts, and the step is
//      refused as soon as that count reaches the current one, or when the reserving unit (or its container, RW 0x693A1A(1)) has an AI whose + 0x16C is above 0.
// TARGET (review r2): RW 0x6ED0A0 calls RW 0x6EAF79 (the footprint size N) and returns below = N / 2, above = N - N / 2; RW 0x666F74 zeroes AI + 0x16C after a new
// path, as computePath zeroes the blocked-frame counter (PathfindObject::aiBlockedFrames). INFERENCE (stop S-783): the second layer of RW 0x76550F / terrain vslot 0xB0 (a raised walkway, lane WALL-2) and the
// neighbour-layer test of a cell with info byte + 0xE bit 0 (RW 0xDA2420 / 0xDA2430) are not applied: the footprint is read on the ground grid.

#include "GameLogic/AI/AIPathfind.h"

bool Pathfinder::crowdingAllowsStep(const PathfindObject &obj, const PathfindLocomotorInfo &loco, const Coord3D &dest)
{
	return crowdingAllowsStep(crowdStepContext(obj, loco), obj, dest);
}

Pathfinder::CrowdStepContext Pathfinder::crowdStepContext(const PathfindObject &obj, const PathfindLocomotorInfo &loco)
{
	CrowdStepContext ctx;
	ctx.mv = makeMovement(&obj, loco);
	ctx.here = cellOfPosition(obj, obj.getPosition());
	// RW 0x6ED0A0: the footprint size N of RW 0x6EAF79, below = N / 2, above = N - N / 2 (review r2: no HORDE / SHIP forced radius here)
	const int footprint = footprintSize(&obj);
	ctx.below = footprint / 2;
	ctx.above = footprint - ctx.below;
	ctx.self = obj.getID();
	// 2. the other units' goal reservations around the unit
	for (int x = ctx.here.x - ctx.below; x < ctx.here.x + ctx.above; ++x)
	{
		for (int y = ctx.here.y - ctx.below; y < ctx.here.y + ctx.above; ++y)
		{
			const PathfindCell *c = getCell(LAYER_GROUND, x, y);
			if (c == nullptr)
			{
				continue;
			}
			for (const PathfindOccupant *o = c->occupants(OCC_GROUND_GOAL); o; o = o->next)
			{
				ctx.current += o->owner != ctx.self ? 1 : 0;
			}
		}
	}
	return ctx;
}

bool Pathfinder::crowdingAllowsStep(const CrowdStepContext &ctx, const PathfindObject &obj, const Coord3D &dest)
{
	const PathfindMovement &mv = ctx.mv;
	const ICoord2D here = ctx.here;
	const ICoord2D there = cellOfPosition(obj, dest);
	// 1. RW 0x6F1B3E (ground layer)
	if (here.x != there.x || here.y != there.y)
	{
		const PathfindCell *c = getCell(LAYER_GROUND, there.x, there.y);
		if (c == nullptr || !validMovementPosition(mv, c))
		{
			return false;
		}
	}
	const int below = ctx.below, above = ctx.above, current = ctx.current;
	const PathfindObjectID self = ctx.self;
	// 3. around the candidate
	int dest_count = 0;
	for (int x = there.x - below; x < there.x + above; ++x)
	{
		for (int y = there.y - below; y < there.y + above; ++y)
		{
			const PathfindCell *c = getCell(LAYER_GROUND, x, y);
			if (c == nullptr || !c->hasInfo())
			{
				continue;
			}
			if (!validMovementPosition(mv, c))
			{
				return false;
			}
			for (const PathfindOccupant *o = c->occupants(OCC_GROUND_GOAL); o; o = o->next)
			{
				if (o->owner == self)
				{
					continue;
				}
				++dest_count;
				if (current <= dest_count)
				{
					return false;
				}
				const PathfindObject *other = m_world ? m_world->findObjectByID(o->owner) : nullptr;
				if (other)
				{
					const PathfindObject *top = other->getTopContainer();
					if ((top ? top : other)->aiBlockedFrames() > 0)
					{
						return false;
					}
				}
			}
		}
	}
	return true;
}
