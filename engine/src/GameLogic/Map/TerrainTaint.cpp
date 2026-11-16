// OpenBFME. GPL-3.0.
//
// TheTerrainLogic's taint clears (lane TAINTAREA-1), on GameLogic beside the terrain's trees (GameLogic/Map/TerrainTrees.h). Both are tier A in RotWK
// and BFME2 (both editions):
//   * RW 0x683393 (BFME2 TerrainLogic.cpp TerrainLogic::rva00282CFB): with a position, TheTaintManager, ThePartitionManager and TheGameLogic: the paint
//     RW 0xAD4C10(position, radius, 0x80, absolute, mode) (a neutral paint: only `mode`'s cells, every painter's for -1); every object within radius
//     (ThePartitionManager RW 0xA39340, FROM_CENTER_2D, near to far) with KindOf TAINTEFFECT (175) is destroyed (RW 0x62BBAB); the trees within radius
//     are retired (RW 0x67F63F); TheTerrainVisual slot 0x5C (position, radius: the client's ground decal) is not sent.
//     Called by AttributeModifierAuraUpdate's destructor (RW 0x89EE67) for a TAINT object (its position, its Range, its id) and by RW 0x683955.
//   * RW 0x683955 (BFME2 TerrainLogic::rva00283262): with a position and the same three subsystems: every object within radius (FROM_CENTER_2D, near
//     to far) with KindOf TAINT (152): the radius clear RW 0x683393(its position, radius, -1), then it is destroyed; finally RW 0x683393(position,
//     radius, -1). Called by the castle unpack (RW 0x79BF36: the castle's position and MaxCastleRadius, when TheTerrainLogic exists).

#include "GameLogic/GameLogic.h"

#include "GameLogic/Map/TerrainTrees.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/System/TaintManager.h"

#include <stdexcept>

namespace
{
int kindOrThrow(const char *name)
{
	const int k = ObjectTemplateInfoBuilder::kindOfIndex(name);
	if (k < 0)
	{
		throw std::logic_error(std::string("taint clear: the KindOf name ") + name + " is not known");
	}
	return k;
}
} // namespace

void GameLogic::clearTaintArea(const Coord3D &pos, float radius, int mode)
{
	static const int taintEffect = kindOrThrow("TAINTEFFECT"); // RW 0x6833F4: 0xAF
	m_taint->applyCircleWorld(pos, radius, TaintManager::kNeutral, true, mode); // RW 0x6833EA
	PartitionFilterFn kind([](Object &o) { return o.isKindOf((unsigned)taintEffect); });
	const PartitionHits hits = m_partition->iterateObjectsInRange(pos, radius, FROM_CENTER_2D, { &kind }, ITER_SORTED_NEAR_TO_FAR); // RW 0x683424
	for (const PartitionHit &h : hits)
	{
		destroyObject(h.object); // RW 0x68343D
	}
	m_trees->retireTreesInRadius(pos, radius); // RW 0x683458
	// RW 0x683465 .. 0x683498: TheTerrainVisual slot 0x5C (position, radius) is the client's ground decal: not sent
}

void GameLogic::clearTaintObjects(const Coord3D &pos, float radius)
{
	static const int taint = kindOrThrow("TAINT"); // RW 0x68399B: 0x98
	PartitionFilterFn kind([](Object &o) { return o.isKindOf((unsigned)taint); });
	const PartitionHits hits = m_partition->iterateObjectsInRange(pos, radius, FROM_CENTER_2D, { &kind }, ITER_SORTED_NEAR_TO_FAR); // RW 0x6839CD
	for (const PartitionHit &h : hits)
	{
		const Coord3D at = *h.object->getPosition();
		clearTaintArea(at, radius, -1); // RW 0x6839E3
		destroyObject(h.object);        // RW 0x6839EF
	}
	clearTaintArea(pos, radius, -1); // RW 0x683A10
}
