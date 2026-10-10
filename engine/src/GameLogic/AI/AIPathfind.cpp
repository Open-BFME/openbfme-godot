// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Pathfinder: the grid, its classification and the surface table. See AIPathfind.h.
//
// EVIDENCE CONVENTIONS (every port in lane PATH-1): "TARGET" is a fact read from the RotWK game.dat (RW addresses; the
// image is community-modified, so S-001 applies to each); "DONOR" is the Open-BFME-1 matched body or the Zero Hour source;
// "INFERRED" is neither. docs/STOPS.md S-160..S-169 list what is not verified.
//
// CLASSIFICATION (docs/STOPS.md S-160)
//   TARGET RW Pathfinder::classifyMapCell 0x934A85, classifyMap 0x935C57, setType 0x934387, newMap 0x6EA1F2:
//     * the cell is sampled at its four corners TL (x0,y0), (x0,y1), (x1,y1), (x1,y0) with x1 = x0 + 10; the cliff test and the
//       two plane tests use the TL corner only;
//     * isCliffCell (W3DTerrainLogic vtable +0x50) -> CLIFF; the plane tests (+0x58, +0x5C) set cell bits 18 and 21;
//     * a non-cliff cell tests each corner with isUnderwater(x, y, &waterZ, &terrainZ): depth = waterZ - terrainZ;
//       depth > WadeWaterDepth (AIData+0x98) -> WATER, then depth > DeepWaterDepth (AIData+0x9C) -> DEEP_WATER; the LAST
//       corner that passes a test wins (a later shallow corner overwrites an earlier deep one; a corner at or below the wade
//       depth changes nothing);
//     * an existing obstacle type stays OBSTACLE; the slope grade is computed and discarded (setSlope(0));
//     * classifyMap: classify every cell; every CLIFF cell marks its CLEAR 3x3 neighbours pinched; every pinched CLEAR cell
//       becomes CLIFF (one dilation); the layer refresh; then RW-only shore passes (bits 22 / 23, no consumer found: not
//       modelled). The zones are NOT computed here: newMap adds every object's footprint first, then computes them.

#include "GameLogic/AI/AIPathfind.h"
#include "Common/StateHash.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/AI/AIPathfindStops.h"

#include <cmath>

// ---------------------------------------------------------------------------------------------------------
// The shape list of a RotWK GeometryInfo (lane PATH-2, S-341). TARGET FACTS (RW game.dat, S-001 caveat): every routine below is x87 under the game's
// 24-bit precision (setFPMode, PLAN rule 3), so each fmul / fadd / fsub / fsqrt rounds to a 24-bit significand (SimMath::pc24*, sqrtPC24) and every
// fstp dword is exact; fabs is exact.
PathfindShape PathfindGeometry::shape(size_t i) const
{
	if (!shapes.empty())
	{
		return shapes[i];
	}
	PathfindShape s;
	s.type = type;
	s.height = height;
	s.majorRadius = majorRadius;
	s.minorRadius = minorRadius;
	return s;
}

namespace
{
float absF(float v)
{
	return (float)SimMath::absD((double)v);
}

float sqrtF(float v)
{
	return (float)SimMath::sqrtPC24((double)v);
}

// RW 0xAD2700: the bounding circle of one shape
float shapeCircle(const PathfindShape &s)
{
	switch (s.type)
	{
	case PATHFIND_GEOMETRY_SPHERE:
	case PATHFIND_GEOMETRY_CYLINDER:
		// sqrt(offY^2 + offX^2) + major
		return SimMath::pc24Add(sqrtF(SimMath::pc24Add(SimMath::pc24Mul(s.offsetY, s.offsetY), SimMath::pc24Mul(s.offsetX, s.offsetX))), s.majorRadius);
	case PATHFIND_GEOMETRY_BOX:
	{
		const float a = SimMath::pc24Add(absF(s.offsetX), s.majorRadius);
		const float b = SimMath::pc24Add(absF(s.offsetY), s.minorRadius);
		return sqrtF(SimMath::pc24Add(SimMath::pc24Mul(b, b), SimMath::pc24Mul(a, a)));
	}
	}
	return 0.0f;
}

// RW 0xAD2770: the bounding sphere of one shape (the half height is a multiply by the double constant 0.5 at RW 0xBD86A0: exact)
float shapeSphere(const PathfindShape &s)
{
	const float x = s.offsetX, y = s.offsetY, z = s.offsetZ;
	switch (s.type)
	{
	case PATHFIND_GEOMETRY_SPHERE:
		// sqrt(z^2 + y^2 + x^2) + major
		return SimMath::pc24Add(sqrtF(SimMath::pc24Add(SimMath::pc24Add(SimMath::pc24Mul(z, z), SimMath::pc24Mul(y, y)), SimMath::pc24Mul(x, x))), s.majorRadius);
	case PATHFIND_GEOMETRY_CYLINDER:
	{
		// r = sqrt(y^2 + x^2) + major, h = |z| + height * 0.5, d = sqrt(z^2 + y^2 + x^2); the result is d + sqrt(r^2 + h^2)
		const float r = SimMath::pc24Add(sqrtF(SimMath::pc24Add(SimMath::pc24Mul(y, y), SimMath::pc24Mul(x, x))), s.majorRadius);
		const float h = SimMath::pc24Add(absF(z), SimMath::pc24Mul(s.height, 0.5f));
		const float d = sqrtF(SimMath::pc24Add(SimMath::pc24Add(SimMath::pc24Mul(z, z), SimMath::pc24Mul(y, y)), SimMath::pc24Mul(x, x)));
		return SimMath::pc24Add(d, sqrtF(SimMath::pc24Add(SimMath::pc24Mul(r, r), SimMath::pc24Mul(h, h))));
	}
	case PATHFIND_GEOMETRY_BOX:
	{
		// a = |x| + major, b = |y| + minor, c = |z| + height * 0.5: sqrt((b^2 + a^2) + c^2)
		const float a = SimMath::pc24Add(absF(x), s.majorRadius);
		const float b = SimMath::pc24Add(absF(y), s.minorRadius);
		const float c = SimMath::pc24Add(absF(z), SimMath::pc24Mul(s.height, 0.5f));
		return sqrtF(SimMath::pc24Add(SimMath::pc24Add(SimMath::pc24Mul(b, b), SimMath::pc24Mul(a, a)), SimMath::pc24Mul(c, c)));
	}
	}
	return 0.0f;
}
} // namespace

float PathfindGeometry::boundingCircleRadius() const
{
	// RW 0xAD2860: start at 0.01f (0x3C23D70A), keep the larger (fcomp: an equal value is taken from the shape, the same value)
	float best = 0.01f;
	for (size_t i = 0; i < shapeCount(); ++i)
	{
		const PathfindShape s = shape(i);
		if (!s.active)
		{
			continue;
		}
		const float c = shapeCircle(s);
		if (!(best > c))
		{
			best = c;
		}
	}
	return best;
}

float PathfindGeometry::boundingSphereRadius() const
{
	float best = 0.0f; // RW 0xAD287A
	for (size_t i = 0; i < shapeCount(); ++i)
	{
		const PathfindShape s = shape(i);
		if (!s.active)
		{
			continue;
		}
		const float c = shapeSphere(s);
		if (!(best > c))
		{
			best = c;
		}
	}
	return best;
}

void PathfindGeometry::applyShapeOffset(const PathfindShape &s, float angle, Coord3D &pos)
{
	// RW 0xAD14F0: no turn when both planar offsets are 0 (fucompp: -0.0 counts as 0)
	if (!(s.offsetX == 0.0f && s.offsetY == 0.0f))
	{
		const float c = SimMath::cosDet(angle); // RW 0x42F4E0 fcos (stop S-167, as every pathfinder cos / sin)
		const float sn = SimMath::sinDet(angle); // RW 0x42F4D0 fsin
		pos.x = SimMath::pc24Add(SimMath::pc24Sub(SimMath::pc24Mul(c, s.offsetX), SimMath::pc24Mul(sn, s.offsetY)), pos.x);
		pos.y = SimMath::pc24Add(SimMath::pc24Add(SimMath::pc24Mul(sn, s.offsetX), SimMath::pc24Mul(c, s.offsetY)), pos.y);
	}
	pos.z = SimMath::pc24Add(s.offsetZ, pos.z);
}

void PathfindGeometry::boundingBox2D(const Coord3D &pos, float angle, float out[4]) const
{
	// RW 0xAD1D60: lo = hi = pos, then every ACTIVE shape at its offset position; a BOX adds its four corners, a SPHERE / CYLINDER centre +- major
	out[0] = pos.x;
	out[1] = pos.y;
	out[2] = pos.x;
	out[3] = pos.y;
	auto add = [&](float x, float y) {
		if (x < out[0]) out[0] = x;
		if (out[1] > y) out[1] = y;
		if (x > out[2]) out[2] = x;
		if (out[3] < y) out[3] = y;
	};
	for (size_t i = 0; i < shapeCount(); ++i)
	{
		const PathfindShape s = shape(i);
		if (!s.active)
		{
			continue;
		}
		Coord3D p = pos;
		applyShapeOffset(s, angle, p);
		if (s.type == PATHFIND_GEOMETRY_BOX)
		{
			const float c = SimMath::cosDet(angle), sn = SimMath::sinDet(angle);
			const float cM = SimMath::pc24Mul(c, s.majorRadius), cm = SimMath::pc24Mul(c, s.minorRadius);
			const float sM = SimMath::pc24Mul(sn, s.majorRadius), sm = SimMath::pc24Mul(sn, s.minorRadius);
			const float a = SimMath::pc24Sub(p.x, cM);
			add(SimMath::pc24Sub(a, sm), SimMath::pc24Sub(SimMath::pc24Add(cm, p.y), sM));
			add(SimMath::pc24Sub(SimMath::pc24Add(cM, p.x), sm), SimMath::pc24Add(SimMath::pc24Add(sM, cm), p.y));
			const float yMinus = SimMath::pc24Sub(p.y, cm);
			add(SimMath::pc24Add(SimMath::pc24Add(sm, cM), p.x), SimMath::pc24Add(yMinus, sM));
			add(SimMath::pc24Add(a, sm), SimMath::pc24Sub(yMinus, sM));
		}
		else
		{
			// RW 0xAD1F91: lo takes centre - major, hi takes centre + major (each compared with its own bound only)
			const float lx = SimMath::pc24Sub(p.x, s.majorRadius), ly = SimMath::pc24Sub(p.y, s.majorRadius);
			const float hx = SimMath::pc24Add(p.x, s.majorRadius), hy = SimMath::pc24Add(p.y, s.majorRadius);
			if (!(out[0] < lx)) out[0] = lx;
			if (!(out[1] < ly)) out[1] = ly;
			if (!(out[2] > hx)) out[2] = hx;
			if (!(out[3] > hy)) out[3] = hy;
		}
	}
}

namespace
{
inline int realToIntFloor(float f)
{
	return SimMath::floorToInt(f);
}
} // namespace

Pathfinder::Pathfinder(const PathfindConfig &config, const PathfindWorld *world)
	: m_config(config), m_world(world)
{
	m_extent.lo = ICoord2D();
	m_extent.hi = ICoord2D();
	m_logicalExtent = m_extent;
	m_pool.allocate(config.cellInfoPoolSize);
	m_zoneManager.setBlockSize(config.zoneBlockSize);
	m_queue1.reset();
	m_queue2.reset();
}

Pathfinder::~Pathfinder() {}

void Pathfinder::freeGrid()
{
	m_blockOfMapCells.clear();
	m_isMapReady = false;
}

void Pathfinder::reset()
{
	freeGrid();
	m_extent.lo = ICoord2D();
	m_extent.hi = ICoord2D();
	m_logicalExtent = m_extent;
	m_stride = 0;
	m_open.clear();
	m_closedList = nullptr;
	m_ignoreObstacleID = PATHFIND_INVALID_ID;
	m_isTunneling = false;
	m_cumulativeCellsAllocated = 0;
	m_pool.allocate(m_config.cellInfoPoolSize);
	m_zoneManager.reset();
	m_queue1.reset();
	m_queue2.reset();
	m_units.clear();
}

void Pathfinder::note(const char *stop)
{
	for (const std::string &s : m_stops)
	{
		if (s == stop) return;
	}
	m_stops.push_back(stop);
}

PathfindCell *Pathfinder::getCell(PathfindLayerEnum layer, int x, int y)
{
	(void)layer; // bridge / wall layers are not ported (S-161): every query reads the ground layer
	if (x >= m_extent.lo.x && x <= m_extent.hi.x && y >= m_extent.lo.y && y <= m_extent.hi.y)
	{
		return &m_blockOfMapCells[(size_t)x * (size_t)m_stride + (size_t)y];
	}
	return nullptr;
}

const PathfindCell *Pathfinder::cellAt(int x, int y) const
{
	if (x >= m_extent.lo.x && x <= m_extent.hi.x && y >= m_extent.lo.y && y <= m_extent.hi.y)
	{
		return &m_blockOfMapCells[(size_t)x * (size_t)m_stride + (size_t)y];
	}
	return nullptr;
}

PathfindCell *Pathfinder::getCell(PathfindLayerEnum layer, const Coord3D *pos)
{
	ICoord2D cell;
	bool overflow = worldToCell(pos, &cell);
	if (overflow)
	{
		return nullptr;
	}
	return getCell(layer, cell.x, cell.y);
}

PathfindCell *Pathfinder::getClippedCell(PathfindLayerEnum layer, const Coord3D *pos)
{
	ICoord2D cell;
	worldToCell(pos, &cell);
	return getCell(layer, cell.x, cell.y);
}

bool Pathfinder::worldToCell(const Coord3D *pos, ICoord2D *cell)
{
	cell->x = realToIntFloor(SimMath::divf32(pos->x, 10.0f));
	cell->y = realToIntFloor(SimMath::divf32(pos->y, 10.0f));
	bool overflow = false;
	if (cell->x < m_extent.lo.x) { overflow = true; cell->x = m_extent.lo.x; }
	if (cell->y < m_extent.lo.y) { overflow = true; cell->y = m_extent.lo.y; }
	if (cell->x > m_extent.hi.x) { overflow = true; cell->x = m_extent.hi.x; }
	if (cell->y > m_extent.hi.y) { overflow = true; cell->y = m_extent.hi.y; }
	return overflow;
}

// ---------------------------------------------------------------------------------------------------------
// the grid
// ---------------------------------------------------------------------------------------------------------
void Pathfinder::newMap(const PathfindTerrain &terrain, const std::vector<PathfindObject *> *objects)
{
	m_terrain = &terrain;
	float lx, ly, hx, hy;
	terrain.getMaximumPathfindExtent(lx, ly, hx, hy);
	PathfindRegion bounds;
	bounds.lo.x = realToIntFloor(SimMath::divf32(lx, 10.0f));
	bounds.hi.x = realToIntFloor(SimMath::divf32(hx, 10.0f));
	bounds.lo.y = realToIntFloor(SimMath::divf32(ly, 10.0f));
	bounds.hi.y = realToIntFloor(SimMath::divf32(hy, 10.0f));
	bounds.hi.x--;
	bounds.hi.y--;
	m_pool.allocate(m_config.cellInfoPoolSize);
	m_extent.lo = bounds.lo;
	m_extent.hi = bounds.hi;
	m_stride = bounds.hi.y + 1;
	m_zoneManager.setBlockSize(m_config.zoneBlockSize);
	m_zoneManager.allocateBlocks(bounds);
	m_blockOfMapCells.assign((size_t)(bounds.hi.x + 1) * (size_t)(bounds.hi.y + 1), PathfindCell());
	m_queue1.reset();
	m_queue2.reset();
	m_units.clear();
	m_stops.clear();
	note(pathstops::kSlopeAndConfig);
	note(pathstops::kLayers);
	note(pathstops::kZoneProfiles);
	note(pathstops::kNumerics);
	setLogicalExtentFromTerrain();
	classifyMap();
	m_buildingMap = true;
	if (objects)
	{
		for (PathfindObject *o : *objects)
		{
			classifyObjectFootprint(*o, true);
		}
	}
	m_buildingMap = false;
	m_zoneManager.calculateZones(gridView(), PathfindRegion{ m_extent.lo, m_extent.hi });
	m_isMapReady = true;
}

void Pathfinder::classifyMap()
{
	note(pathstops::kShore);
	for (int j = m_extent.lo.y; j <= m_extent.hi.y; j++)
	{
		for (int i = m_extent.lo.x; i <= m_extent.hi.x; i++)
		{
			classifyMapCell(i, j, getCell(LAYER_GROUND, i, j));
		}
	}
	// every cliff cell marks the CLEAR cells of its 3x3 block pinched
	for (int j = m_extent.lo.y; j <= m_extent.hi.y; j++)
	{
		for (int i = m_extent.lo.x; i <= m_extent.hi.x; i++)
		{
			if (getCell(LAYER_GROUND, i, j)->getType() == PathfindCell::CELL_CLIFF)
			{
				for (int k = i - 1; k < i + 2; k++)
				{
					if (k < m_extent.lo.x || k > m_extent.hi.x) continue;
					for (int l = j - 1; l < j + 2; l++)
					{
						if (l < m_extent.lo.y || l > m_extent.hi.y) continue;
						if (getCell(LAYER_GROUND, k, l)->getType() == PathfindCell::CELL_CLEAR)
						{
							getCell(LAYER_GROUND, k, l)->setPinched(true);
						}
					}
				}
			}
		}
	}
	// every pinched CLEAR cell becomes CLIFF
	for (int j = m_extent.lo.y; j <= m_extent.hi.y; j++)
	{
		for (int i = m_extent.lo.x; i <= m_extent.hi.x; i++)
		{
			PathfindCell *c = getCell(LAYER_GROUND, i, j);
			if (c->getPinched())
			{
				if (c->getType() == PathfindCell::CELL_CLEAR)
				{
					c->setType(PathfindCell::CELL_CLIFF);
				}
			}
		}
	}
}

void Pathfinder::classifyMapCell(int i, int j, PathfindCell *cell)
{
	// TARGET RW 0x934A85 (see the header of this file)
	const bool hasObstacle = (cell->getType() == PathfindCell::CELL_OBSTACLE);
	const float y0 = SimMath::mulf32((float)j, 10.0f);
	const float y1 = SimMath::addf32(y0, 10.0f);
	const float x0 = SimMath::mulf32((float)i, 10.0f);
	const float x1 = SimMath::addf32(x0, 10.0f);
	cell->setPinched(false);
	PathfindCell::CellType type = PathfindCell::CELL_CLEAR;
	if (m_terrain->isCliffCell(x0, y0))
	{
		type = PathfindCell::CELL_CLIFF;
	}
	cell->setImpassableToPlayers(m_terrain->impassableToPlayers(x0, y0));
	cell->setExtraPass(m_terrain->extraPass(x0, y0));
	if (type != PathfindCell::CELL_CLIFF)
	{
		const float wade = m_config.wadeWaterDepth;
		const float deep = m_config.deepWaterDepth;
		const float cornersX[4] = { x0, x0, x1, x1 };
		const float cornersY[4] = { y0, y1, y1, y0 };
		for (int k = 0; k < 4; ++k)
		{
			float waterZ, terrainZ;
			if (m_terrain->isUnderwater(cornersX[k], cornersY[k], &waterZ, &terrainZ))
			{
				const float depth = SimMath::subf32(waterZ, terrainZ);
				if (depth > wade) type = PathfindCell::CELL_WATER;
				if (depth > deep) type = PathfindCell::CELL_DEEP_WATER;
			}
		}
	}
	if (hasObstacle)
	{
		type = PathfindCell::CELL_OBSTACLE;
	}
	cell->setType(type);
}

PathfindGridStats Pathfinder::gridStats() const
{
	PathfindGridStats s;
	s.width = m_extent.hi.x + 1;
	s.height = m_extent.hi.y + 1;
	for (const PathfindCell &c : m_blockOfMapCells)
	{
		s.types[(int)c.getType() & 7]++;
		if (c.getPinched()) s.pinched++;
		if (c.getImpassableToPlayers()) s.impassableToPlayers++;
		if (c.getExtraPass()) s.extraPass++;
	}
	return s;
}

std::vector<std::string> Pathfinder::allStops()
{
	return { pathstops::kShore, pathstops::kSlopeAndConfig, pathstops::kRivers, pathstops::kLayers, pathstops::kZoneProfiles, pathstops::kHierarchical,
		pathstops::kLargeRectGoal, pathstops::kPartialRebuild, pathstops::kGroupDestination, pathstops::kWallLayer, pathstops::kLargeRectFootprint,
		pathstops::kLargeRectReservation, pathstops::kQueueFull, pathstops::kQueueTime, pathstops::kPath, pathstops::kNumerics };
}

// ---- the state hash (MOVE-1) ----
void PathfindZoneManager::crc(StateHasher &h) const
{
	h.addI32(m_blockSize);
	h.addI32(m_zoneBlockExtent.x);
	h.addI32(m_zoneBlockExtent.y);
	h.addBool(m_dirty);
	h.addU32((std::uint32_t)m_blocks.size());
	for (const Block &b : m_blocks)
	{
		h.addBool(b.passable);
		h.addBool(b.dirty);
	}
	h.addU32((std::uint32_t)m_zones.size());
	for (const ZoneInfo &z : m_zones)
	{
		h.addU32(z.type);
		h.addU32(z.layer);
		h.addU32(z.conn);
		h.addBool(z.bit17);
		h.addBool(z.bit18);
		h.addBool(z.bit22);
	}
	h.addU32((std::uint32_t)m_adjacent.size());
	for (const auto &a : m_adjacent)
	{
		h.addU32(a.first);
		h.addU32(a.second);
	}
}

void Pathfinder::crc(StateHasher &h) const
{
	h.addBool(m_isMapReady);
	h.addFloat(m_config.wadeWaterDepth);
	h.addFloat(m_config.deepWaterDepth);
	h.addFloat(m_config.slopeLimits[0]);
	h.addFloat(m_config.slopeLimits[1]);
	for (int v : { m_config.cellsPerFrame, m_config.adjustDestinationLimit, m_config.adjustHordeMeleeLimit, m_config.patchPathLimit, m_config.findPathLimit, m_config.adjustToPossibleLimit,
			 m_config.examineTowardsGoalLimit, m_config.cellInfoPoolSize, m_config.zoneBlockSize, m_config.findAttackPathLimit, m_config.adjustToMeleeLimit,
			 m_config.findMeleeEngagementLimit })
	{
		h.addI32(v);
	}
	h.addBool(m_config.hordesWaitForHordes); // lane PHYS-1 (review r3)
	h.addBool(m_config.planningModeEnabled); // lane MOVE-3 (review r1): GameData + 0x11CA routes move orders through the group manager
	h.addFloat(m_config.meleeApproachDist);
	h.addFloat(m_config.meleeApproachTolerance);
	h.addFloat(m_config.castleSiegeStandBackDistance);
	for (int v : { m_extent.lo.x, m_extent.lo.y, m_extent.hi.x, m_extent.hi.y, m_logicalExtent.lo.x, m_logicalExtent.lo.y, m_logicalExtent.hi.x, m_logicalExtent.hi.y, m_stride })
	{
		h.addI32(v);
	}
	h.addBool(m_isTunneling);
	h.addU32((std::uint32_t)m_ignoreObstacleID);
	h.addI32(m_cumulativeCellsAllocated);
	h.addU32((std::uint32_t)m_blockOfMapCells.size());
	for (const PathfindCell &c : m_blockOfMapCells)
	{
		h.addU32(c.getType());
		h.addU32(c.getLayer());
		h.addU32(c.getConnectLayer());
		h.addU32((c.getBit17() ? 1u : 0u) | (c.getImpassableToPlayers() ? 2u : 0u) | (c.getExtraPass() ? 4u : 0u) | (c.getBit22() ? 8u : 0u) | (c.getBit23() ? 16u : 0u) | (c.getPinched() ? 32u : 0u));
		h.addU32(c.getZone());
		const bool info = c.hasInfo();
		h.addBool(info);
		if (!info)
		{
			continue;
		}
		h.addU32((std::uint32_t)c.getObstacleID());
		h.addU32((c.isObstacleFence() ? 1u : 0u) | (c.isObstacleTransparent() ? 2u : 0u) | (c.isBlockedByAlly() ? 4u : 0u));
		for (int k = 0; k < OCC_KIND_COUNT; ++k)
		{
			std::uint32_t n = 0;
			for (const PathfindOccupant *o = c.occupants((PathfindOccupantKind)k); o; o = o->next)
			{
				h.addU32((std::uint32_t)o->owner); // the chain in list order: the order is history
				++n;
			}
			h.addU32(n);
		}
	}
	m_zoneManager.crc(h);
	h.addU32((std::uint32_t)m_queue1.size());
	for (size_t i = 0; i < m_queue1.size(); ++i)
	{
		h.addU32((std::uint32_t)m_queue1.at(i));
	}
	h.addU32((std::uint32_t)m_queue2.size());
	for (size_t i = 0; i < m_queue2.size(); ++i)
	{
		h.addU32((std::uint32_t)m_queue2.at(i));
	}
	h.addU32((std::uint32_t)m_units.size());
	for (const auto &kv : m_units) // ordered by id
	{
		h.addU32((std::uint32_t)kv.first);
		for (const Slot *s : { &kv.second.goal, &kv.second.pos })
		{
			h.addBool(s->occupied);
			h.addI32(s->cell.x);
			h.addI32(s->cell.y);
			h.addI32(s->angleCode);
			h.addU32((std::uint32_t)s->layer);
			h.addU32((std::uint32_t)s->kind);
			h.addU32((std::uint32_t)s->cells.size());
			for (const ICoord2D &c : s->cells)
			{
				h.addI32(c.x);
				h.addI32(c.y);
			}
		}
	}
}
