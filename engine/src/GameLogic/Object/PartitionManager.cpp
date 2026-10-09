// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// PartitionManager (lane MODULES-2): see PartitionManager.h for the target facts and the stop S-1020. Every float operation below is the retail instruction's
// counterpart through SimMath (x87 under PC24: pc24* and the wide carriers; the CRT floor / sqrt), in the retail operand order.

#include "GameLogic/Object/PartitionManager.h"

#include "Common/Player.h"
#include "Common/StateHash.h"
#include "Common/Team.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/ObjectGeometry.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/SimMath.h"

#include <stdexcept>
#include <utility>

// the 0x30 byte entry of RW 0xA394F0 (Object + 0x4E8)
struct PartitionManager::Entry
{
	Object *object = nullptr;
	ObjectID id = INVALID_ID;
	// + 0x08 / + 0x0C: the manager's entry list (newest first)
	Entry **allPrev = nullptr;
	Entry *allNext = nullptr;
	// + 0x10 / + 0x14: the node list (the slot that points at this entry, the next entry)
	Entry **nodePrev = nullptr;
	Entry *nodeNext = nullptr;
	// + 0x18 / + 0x1C: the dirty list
	Entry **dirtyPrev = nullptr;
	Entry *dirtyNext = nullptr;
	int layerPlus1 = 0;     ///< + 0x20
	std::uint32_t mask = 0; ///< + 0x24
	std::uint32_t x = 0;    ///< + 0x28
	std::uint32_t y = 0;    ///< + 0x2C
	// the port's cache of geometryOf(object) (performance): valid while the object's final-override template is geomKey
	mutable const ThingTemplate *geomKey = nullptr;
	mutable const Geometry *geom = nullptr;
};

namespace
{
const char *const kStopPartition =
	"[S-1020] partition: ThePartitionManager is RotWK's 21 loose quadtrees (RW 0xA3BD20 .. 0xA3C7D0, ported: registration, dirty update, range / region / "
	"closest queries, the STLport introsort of the hits); inference: the geometry is the template's (RW keeps it per object and RW 0x68B2CB marks a change), "
	"the undetected defector bit (Object + 0x458 bit 1) is never set (no defection, S-141), distance type 4 needs the shape overlap test RW 0xAD2CE0 (refused), "
	"a game without a map keeps the constructor's empty region (every object in cell 0, 0; getClosestObject's range clamps to 0: only a coincident or overlapping candidate can still be returned)";
} // namespace

PartitionManager::PartitionManager(GameLogic &logic)
	: m_logic(logic)
	, m_trees((size_t)kLayerCount, std::vector<Node>((size_t)kNodes))
{
	m_mineBit = ObjectTemplateInfoBuilder::kindOfIndex("MINE");
	// RW 0xA3BD20: the constructor sets the empty region (scale 1 / 0 under x87: +infinity)
	m_scale = SimMath::pc24DivD(1.0, 0.0);
}

PartitionManager::~PartitionManager() = default;

void PartitionManager::reset()
{
	for (Entry *e = m_entries; e; e = e->allNext)
	{
		e->geomKey = nullptr;
		e->geom = nullptr;
	}
	m_geometry.clear();
}

PartitionHits::~PartitionHits()
{
	if (m_owner)
	{
		m_owner->recycle(std::move(m_hits));
	}
}

std::vector<PartitionHit> PartitionManager::takeBuffer() const
{
	if (m_hitPool.empty())
	{
		return {};
	}
	std::vector<PartitionHit> v = std::move(m_hitPool.back());
	m_hitPool.pop_back();
	return v;
}

void PartitionManager::recycle(std::vector<PartitionHit> &&hits) const
{
	hits.clear();
	if (m_hitPool.size() < 16 && hits.capacity() != 0)
	{
		m_hitPool.push_back(std::move(hits));
	}
}

const PartitionManager::Geometry &PartitionManager::entryGeometry(const Entry &e) const
{
	const ThingTemplate *tt = static_cast<const ThingTemplate *>(e.object->getTemplate());
	const ThingTemplate *key = tt ? tt->getFinalOverride() : nullptr;
	if (!e.geom || e.geomKey != key)
	{
		e.geom = &geometryOf(*e.object);
		e.geomKey = key;
	}
	return *e.geom;
}

std::vector<std::string> PartitionManager::stopLines()
{
	return { kStopPartition };
}

// ---- cells ---------------------------------------------------------------------------------------------------------------------------------------------------

namespace
{
// RW 0xA3AD30 / 0xA3ADA0: fld v; fsub lo; fmul scale; fild cells; fmulp; fstp qword; floor; fstp dword; fistp; clamp 0 .. cells - 1 (unsigned compare)
std::uint32_t cellIndex(float v, float lo, float scale)
{
	double t = SimMath::pc24SubW((double)v, (double)lo);
	t = SimMath::pc24MulW(t, (double)scale);
	t = SimMath::pc24MulW(t, (double)PartitionManager::kCells);
	const std::int32_t c = SimMath::fistp32((double)SimMath::fstpDword(SimMath::floorD(t)));
	if (c < 0)
	{
		return 0;
	}
	if ((std::uint32_t)c >= (std::uint32_t)PartitionManager::kCells)
	{
		return (std::uint32_t)PartitionManager::kCells - 1;
	}
	return (std::uint32_t)c;
}
} // namespace

std::uint32_t PartitionManager::cellX(float x) const
{
	return cellIndex(x, m_loX, m_scale);
}

std::uint32_t PartitionManager::cellY(float y) const
{
	return cellIndex(y, m_loY, m_scale);
}

const PartitionManager::Geometry &PartitionManager::geometryOf(const Object &obj) const
{
	const ThingTemplate *tt = static_cast<const ThingTemplate *>(obj.getTemplate());
	const ThingTemplate *key = tt ? tt->getFinalOverride() : nullptr;
	auto it = m_geometry.find(key);
	if (it != m_geometry.end())
	{
		return it->second;
	}
	Geometry g;
	if (key)
	{
		PathfindGeometry pg;
		ObjectGeometry::fillPathfindGeometry(*key, pg); // throws on a row retail rejects
		g.circle = pg.boundingCircleRadius();          // GeometryInfo + 0x10 (RW 0xAD2860)
		g.sphere = pg.boundingSphereRadius();          // + 0x14
		// RW 0xAD2040 (the z extent of the active shapes, starting at 0 .. 0) and RW 0xAD2860: + 0x20 = (lo.z + hi.z) * 0.5
		float zlo = 0.0f, zhi = 0.0f;
		for (size_t i = 0; i < pg.shapeCount(); ++i)
		{
			const PathfindShape s = pg.shape(i);
			if (!s.active)
			{
				continue;
			}
			float lo, hi;
			if (s.type == PATHFIND_GEOMETRY_SPHERE)
			{
				lo = SimMath::pc24Sub(s.offsetZ, s.majorRadius);
				hi = SimMath::pc24Add(s.offsetZ, s.majorRadius);
			}
			else
			{
				lo = s.offsetZ;
				hi = SimMath::pc24Add(s.height, s.offsetZ);
			}
			if (lo <= zlo)
			{
				zlo = lo;
			}
			if (zhi <= hi)
			{
				zhi = hi;
			}
		}
		g.centerZ = SimMath::pc24Mul(SimMath::pc24Add(zlo, zhi), 0.5f);
	}
	return m_geometry.emplace(key, g).first->second;
}

// RW 0xA3AF50
void PartitionManager::computeCells(const Entry &e, std::uint32_t &x, std::uint32_t &y, std::uint32_t &mask) const
{
	const float r = entryGeometry(e).circle;
	const Coord3D *pos = e.object->getPosition();
	x = cellX(SimMath::pc24Sub(pos->x, r));
	y = cellY(SimMath::pc24Sub(pos->y, r));
	const std::uint32_t x1 = cellX(SimMath::pc24Add(r, pos->x));
	const std::uint32_t y1 = cellY(SimMath::pc24Add(r, pos->y));
	mask = (x1 ^ x) | (y1 ^ y);
	if (mask != 0)
	{
		// the index of the highest set bit (the binary search of RW 0xA3AF7C .. 0xA3AFEA)
		std::uint32_t v = mask;
		unsigned bit = 0;
		if (v & 0xFFFF0000u)
		{
			v >>= 16;
			bit = 16;
		}
		if (v & 0xFF00u)
		{
			v >>= 8;
			bit |= 8;
		}
		if (v & 0xF0u)
		{
			v >>= 4;
			bit |= 4;
		}
		if (v & 0xCu)
		{
			v >>= 2;
			bit |= 2;
		}
		if (v & 2u)
		{
			bit |= 1;
		}
		x &= ~(1u << bit);
		y &= ~(1u << bit);
	}
}

int PartitionManager::layerOf(const Object &obj) const
{
	// RW 0x68EE53 (the undetected defector bit Object + 0x458 bit 1 is never set here: S-1020)
	const Player *player = obj.getControllingPlayer();
	if (!player)
	{
		return -1;
	}
	if (m_mineBit >= 0 && obj.isKindOf((unsigned)m_mineBit))
	{
		return -1;
	}
	const Team *team = obj.getTeam();
	if (!team || team->hasRelationshipOverrides())
	{
		return -1;
	}
	const int index = player->getPlayerIndex();
	// RW 0xA3B220: an index outside -1 .. 19 is -1
	if (index < -1 || index > kLayerCount - 2)
	{
		return -1;
	}
	return index;
}

// ---- links ---------------------------------------------------------------------------------------------------------------------------------------------------

// RW 0xA3B220
void PartitionManager::insert(Entry &e)
{
	computeCells(e, e.x, e.y, e.mask);
	const int layer = layerOf(*e.object);
	std::vector<Node> &tree = m_trees[(size_t)(layer + 1)];
	size_t node = 0;
	std::uint32_t stride = (std::uint32_t)kNodes;
	std::uint32_t bit = (std::uint32_t)kCells;
	for (;;)
	{
		stride >>= 2;
		if (stride == 0)
		{
			break;
		}
		bit >>= 1;
		if ((e.mask & bit) != 0)
		{
			break;
		}
		++tree[node].below;
		const std::uint32_t child = ((e.x & bit) != 0 ? 1u : 0u) + ((e.y & bit) != 0 ? 2u : 0u);
		node += (size_t)child * stride + 1;
	}
	Node &n = tree[node];
	e.nodePrev = &n.head;
	e.nodeNext = n.head;
	if (n.head)
	{
		n.head->nodePrev = &e.nodeNext;
	}
	n.head = &e;
	e.layerPlus1 = layer + 1;
}

// RW 0xA3B2E0: unlink, then the counts of the path the entry's stored cells give
void PartitionManager::remove(Entry &e)
{
	if (e.nodeNext)
	{
		e.nodeNext->nodePrev = e.nodePrev;
	}
	if (e.nodePrev)
	{
		*e.nodePrev = e.nodeNext;
	}
	e.nodePrev = nullptr;
	e.nodeNext = nullptr;
	std::vector<Node> &tree = m_trees[(size_t)e.layerPlus1];
	size_t node = 0;
	std::uint32_t stride = (std::uint32_t)kNodes;
	std::uint32_t bit = (std::uint32_t)kCells;
	for (;;)
	{
		stride >>= 2;
		if (stride == 0)
		{
			break;
		}
		bit >>= 1;
		if ((e.mask & bit) != 0)
		{
			break;
		}
		--tree[node].below;
		const std::uint32_t child = ((e.x & bit) != 0 ? 1u : 0u) + ((e.y & bit) != 0 ? 2u : 0u);
		node += (size_t)child * stride + 1;
	}
}

// RW 0xA3B370: every node list emptied (each entry's slot zeroed), every count 0
void PartitionManager::clearTrees()
{
	for (Entry *e = m_entries; e; e = e->allNext)
	{
		if (e->nodePrev)
		{
			*e->nodePrev = nullptr;
		}
		e->nodePrev = nullptr;
		e->nodeNext = nullptr;
	}
	for (std::vector<Node> &tree : m_trees)
	{
		for (Node &n : tree)
		{
			n.below = 0;
			n.head = nullptr;
		}
	}
}

void PartitionManager::setRegion(float loX, float loY, float hiX, float hiY)
{
	// RW 0xA3B450
	clearTrees();
	m_loX = loX;
	m_loY = loY;
	m_hiX = hiX;
	m_hiY = hiY;
	const double w = SimMath::pc24SubW((double)hiX, (double)loX);
	const float h = SimMath::pc24Sub(hiY, loY);
	const double m = (w > (double)h) ? w : (double)h;
	m_scale = SimMath::pc24DivD(1.0, m);
	m_regionSet = true;
	for (Entry *e = m_entries; e; e = e->allNext)
	{
		insert(*e);
	}
}

void PartitionManager::registerObject(Object &obj)
{
	// RW 0xA3B580: an object that already has an entry is left alone
	const ObjectID id = obj.getID();
	if (id >= m_byId.size())
	{
		m_byId.resize((size_t)id + 1);
	}
	if (m_byId[id])
	{
		return;
	}
	try
	{
		(void)geometryOf(obj);
	}
	catch (const std::exception &e)
	{
		// a geometry row that does not parse (retail rejects it at the INI load): the object stays out of the partition and the error is reported, no default cells
		m_logic.reportError(std::string("partition: ") + e.what() + " (the object is not registered)");
		return;
	}
	std::unique_ptr<Entry> owned = std::make_unique<Entry>();
	Entry &e = *owned;
	e.object = &obj;
	e.id = id;
	// RW 0xA394F0: the head of the manager's entry list
	e.allPrev = &m_entries;
	e.allNext = m_entries;
	if (m_entries)
	{
		m_entries->allPrev = &e.allNext;
	}
	m_entries = &e;
	m_byId[id] = std::move(owned);
	++m_entryCount;
	insert(e);
}

void PartitionManager::unRegisterObject(Object &obj)
{
	// RW 0xA3B610
	const ObjectID id = obj.getID();
	if (id >= m_byId.size() || !m_byId[id] || m_byId[id]->object != &obj)
	{
		return;
	}
	Entry &e = *m_byId[id];
	remove(e);
	// RW 0xA39530: off the dirty list, then off the entry list
	if (e.dirtyPrev)
	{
		if (e.dirtyNext)
		{
			e.dirtyNext->dirtyPrev = e.dirtyPrev;
		}
		*e.dirtyPrev = e.dirtyNext;
	}
	if (e.allNext)
	{
		e.allNext->allPrev = e.allPrev;
	}
	*e.allPrev = e.allNext;
	m_byId[id].reset();
	--m_entryCount;
}

bool PartitionManager::isRegistered(const Object &obj) const
{
	const ObjectID id = obj.getID();
	return id < m_byId.size() && m_byId[id] && m_byId[id]->object == &obj;
}

void PartitionManager::markDirty(Object &obj)
{
	// RW 0xA39570
	const ObjectID id = obj.getID();
	if (id >= m_byId.size() || !m_byId[id] || m_byId[id]->object != &obj)
	{
		return;
	}
	Entry &e = *m_byId[id];
	if (e.dirtyPrev)
	{
		return;
	}
	e.dirtyPrev = &m_dirty;
	e.dirtyNext = m_dirty;
	if (m_dirty)
	{
		m_dirty->dirtyPrev = &e.dirtyNext;
	}
	m_dirty = &e;
}

void PartitionManager::update()
{
	// RW 0xA3B4E0: from the head until the list is empty
	while (Entry *e = m_dirty)
	{
		if (e->dirtyNext)
		{
			e->dirtyNext->dirtyPrev = e->dirtyPrev;
		}
		*e->dirtyPrev = e->dirtyNext;
		e->dirtyPrev = nullptr;
		e->dirtyNext = nullptr;
		bool moved = (e->layerPlus1 != layerOf(*e->object) + 1);
		if (!moved)
		{
			std::uint32_t x, y, mask;
			computeCells(*e, x, y, mask);
			moved = (x != e->x || y != e->y || mask != e->mask);
		}
		if (moved)
		{
			remove(*e);
			insert(*e);
		}
	}
}

size_t PartitionManager::dirtyCount() const
{
	size_t n = 0;
	for (const Entry *e = m_dirty; e; e = e->dirtyNext)
	{
		++n;
	}
	return n;
}

bool PartitionManager::entryCells(const Object &obj, int &layer, std::uint32_t &x, std::uint32_t &y, std::uint32_t &mask) const
{
	const ObjectID id = obj.getID();
	if (id >= m_byId.size() || !m_byId[id] || m_byId[id]->object != &obj)
	{
		return false;
	}
	const Entry &e = *m_byId[id];
	layer = e.layerPlus1 - 1;
	x = e.x;
	y = e.y;
	mask = e.mask;
	return true;
}

// ---- queries -------------------------------------------------------------------------------------------------------------------------------------------------

std::uint32_t PartitionManager::layerMask(const PartitionFilterList &filters) const
{
	// RW 0xA3C51C .. 0xA3C537: no filter: every layer (-1); else (AND of the masks) * 2 + 1
	if (filters.empty())
	{
		return 0xFFFFFFFFu;
	}
	std::uint32_t m = 0xFFFFFFFFu; // RW 0xA39490
	for (const PartitionFilter *f : filters)
	{
		m &= f->playerLayerMask();
	}
	return m * 2u + 1u;
}

bool PartitionManager::allowed(Object &obj, const PartitionFilterList &filters) const
{
	for (PartitionFilter *f : filters)
	{
		if (!f->allow(obj))
		{
			return false;
		}
	}
	return true;
}

// the distance functions of RW table 0xDBDAF8: the x87 register value (24-bit significand, the wide exponent of the carrier)
double PartitionManager::distance(DistanceCalculationType type, const Coord3D &c, const Entry &e, float radiusSqr) const
{
	(void)radiusSqr;
	const Coord3D *p = e.object->getPosition();
	switch (type)
	{
		case FROM_CENTER_3D:
		{
			// RW 0xA3A7D0: dz, dy, dx; (dx^2 + dy^2) + dz^2
			const double dz = SimMath::pc24SubW((double)p->z, (double)c.z);
			const double dy = SimMath::pc24SubW((double)p->y, (double)c.y);
			const double dx = SimMath::pc24SubW((double)p->x, (double)c.x);
			const double s = SimMath::pc24AddW(SimMath::pc24MulW(dx, dx), SimMath::pc24MulW(dy, dy));
			return SimMath::pc24AddW(s, SimMath::pc24MulW(dz, dz));
		}
		case FROM_BOUNDINGSPHERE_2D:
		{
			// RW 0xA3AE50: the sum stored as a float, fsqrt, minus the bounding circle radius, squared, negated when negative
			const double dy = SimMath::pc24SubW((double)p->y, (double)c.y);
			const double dx = SimMath::pc24SubW((double)p->x, (double)c.x);
			const float s = SimMath::fstpDword(SimMath::pc24AddW(SimMath::pc24MulW(dx, dx), SimMath::pc24MulW(dy, dy)));
			const double d = SimMath::pc24SubW(SimMath::sqrtPC24((double)s), (double)entryGeometry(e).circle);
			const double sq = SimMath::pc24MulW(d, d);
			return (d < 0.0) ? -sq : sq;
		}
		case FROM_BOUNDINGSPHERE_3D:
		{
			// RW 0xA3AEB0: dx, dy, dz stored as floats, dz += the geometry's centre height (stored), ((dy^2 + dx^2) + dz^2), fsqrt, minus the bounding sphere radius
			const Geometry &g = entryGeometry(e);
			const float dx = SimMath::pc24Sub(p->x, c.x);
			const float dy = SimMath::pc24Sub(p->y, c.y);
			float dz = SimMath::pc24Sub(p->z, c.z);
			dz = SimMath::pc24Add(g.centerZ, dz);
			double s = SimMath::pc24AddW(SimMath::pc24MulW((double)dy, (double)dy), SimMath::pc24MulW((double)dx, (double)dx));
			s = SimMath::pc24AddW(s, SimMath::pc24MulW((double)dz, (double)dz));
			const double d = SimMath::pc24SubW(SimMath::sqrtPC24(s), (double)g.sphere);
			const double sq = SimMath::pc24MulW(d, d);
			return (d < 0.0) ? -sq : sq;
		}
		case FROM_GEOMETRY_OVERLAP:
			throw std::logic_error("partition: distance type 4 (RW 0xA3C7D0) needs the shape overlap test RW 0xAD2CE0, not ported [S-1020]");
		case FROM_CENTER_2D:
		default:
		{
			// RW 0xA3A7A0: dy, dx; dx^2 + dy^2
			const double dy = SimMath::pc24SubW((double)p->y, (double)c.y);
			const double dx = SimMath::pc24SubW((double)p->x, (double)c.x);
			return SimMath::pc24AddW(SimMath::pc24MulW(dx, dx), SimMath::pc24MulW(dy, dy));
		}
	}
}

// Not retail (performance): a bound in binary64 (SSE, deterministic) of the distance() value. The exact value carries at most a few 2^-24 relative roundings
// (the PC24 differences, squares, sums, the float store of type 2, the PC24 root and radius subtraction), so a candidate whose bound exceeds radiusSqr by the
// relative margin 1e-5 (and, for the bounding types, after an absolute margin of 1e-5 of the magnitudes involved) is rejected by the exact test as well. A
// NaN compares false and goes to the exact test; so does every candidate near the boundary.
bool PartitionManager::surelyBeyond(DistanceCalculationType type, const Coord3D &c, const Entry &e, double radiusSqr) const
{
	const Coord3D *p = e.object->getPosition();
	const double limit = SimMath::mulD(radiusSqr, 1.00001);
	const double dx = SimMath::subD((double)p->x, (double)c.x);
	const double dy = SimMath::subD((double)p->y, (double)c.y);
	const double s2 = SimMath::addD(SimMath::mulD(dx, dx), SimMath::mulD(dy, dy));
	auto absD = [](double v) { return v < 0.0 ? SimMath::subD(0.0, v) : v; };
	switch (type)
	{
		case FROM_CENTER_2D:
			return s2 > limit;
		case FROM_CENTER_3D:
		{
			const double dz = SimMath::subD((double)p->z, (double)c.z);
			return SimMath::addD(s2, SimMath::mulD(dz, dz)) > limit;
		}
		case FROM_BOUNDINGSPHERE_2D:
		{
			const double r = (double)entryGeometry(e).circle;
			const double q = SimMath::sqrtd(s2);
			const double err = SimMath::addD(SimMath::mulD(SimMath::addD(q, absD(r)), 1.0e-5), 1.0e-20);
			const double lo = SimMath::subD(SimMath::subD(q, r), err);
			return lo > 0.0 && SimMath::mulD(lo, lo) > limit;
		}
		case FROM_BOUNDINGSPHERE_3D:
		{
			const Geometry &g = entryGeometry(e);
			const double dz0 = SimMath::subD((double)p->z, (double)c.z);
			const double dz = SimMath::addD(dz0, (double)g.centerZ);
			const double q = SimMath::sqrtd(SimMath::addD(s2, SimMath::mulD(dz, dz)));
			const double mag = SimMath::addD(SimMath::addD(q, absD((double)g.sphere)), SimMath::addD(absD((double)g.centerZ), absD(dz0)));
			const double err = SimMath::addD(SimMath::mulD(mag, 1.0e-5), 1.0e-20);
			const double lo = SimMath::subD(SimMath::subD(q, (double)g.sphere), err);
			return lo > 0.0 && SimMath::mulD(lo, lo) > limit;
		}
		default:
			return false;
	}
}

// RW 0xA3A860 (the recursion kept as written: the node's list, then the children 0, 1 (y low), 2, 3 (y high) that overlap the cell rectangle)
void PartitionManager::walk(const Node *node, std::uint32_t childNodes, int x0, int y0, int x1, int y1, int nodeX, int nodeY, int size, const Coord3D *center,
                            const PartitionRegion *region, double radiusSqr, DistanceCalculationType type, const PartitionFilterList &filters,
                            std::vector<PartitionHit> &out) const
{
	for (;;)
	{
		for (const Entry *e = node->head; e; e = e->nodeNext)
		{
			float stored;
			if (!center)
			{
				// RW 0xA3A8C3: the position inside [lo, hi] (a NaN passes), the distance from the rectangle's centre
				const Coord3D *p = e->object->getPosition();
				if (p->x < region->loX || p->x > region->hiX || p->y < region->loY || p->y > region->hiY)
				{
					continue;
				}
				const double cx = SimMath::pc24MulW(SimMath::pc24AddW((double)region->loX, (double)region->hiX), 0.5);
				const double cy = SimMath::pc24MulW(SimMath::pc24AddW((double)region->loY, (double)region->hiY), 0.5);
				const double dy = SimMath::pc24SubW((double)p->y, cy);
				const double dx = SimMath::pc24SubW((double)p->x, cx);
				stored = SimMath::fstpDword(SimMath::pc24AddW(SimMath::pc24MulW(dx, dx), SimMath::pc24MulW(dy, dy)));
			}
			else
			{
				if (m_walkPreReject && surelyBeyond(type, *center, *e, radiusSqr))
				{
					continue; // the exact test below would reject it (performance)
				}
				const double d = distance(type, *center, *e, (float)radiusSqr);
				stored = SimMath::fstpDword(d); // RW 0xA3A8AB fst dword
				if (d > radiusSqr)              // fcomp; test ah, 0x41; je: only "greater" rejects (a NaN passes)
				{
					continue;
				}
			}
			Object *obj = e->object; // RW proxy slot 3 (never null for a live entry)
			if (!filters.empty() && !allowed(*obj, filters))
			{
				continue;
			}
			out.push_back(PartitionHit{ obj, stored });
		}
		if (node->below == 0)
		{
			return;
		}
		size = size / 2;
		const int midY = nodeY + size;
		const int midX = nodeX + size;
		const Node *first = node + 1;
		if (y0 < midY)
		{
			if (x0 < midX)
			{
				walk(first, childNodes >> 2, x0, y0, x1, y1, nodeX, nodeY, size, center, region, radiusSqr, type, filters, out);
			}
			if (midX <= x1)
			{
				walk(first + childNodes, childNodes >> 2, x0, y0, x1, y1, midX, nodeY, size, center, region, radiusSqr, type, filters, out);
			}
		}
		if (y1 < midY)
		{
			return;
		}
		if (x0 < midX)
		{
			walk(first + 2 * (size_t)childNodes, childNodes >> 2, x0, y0, x1, y1, nodeX, midY, size, center, region, radiusSqr, type, filters, out);
		}
		if (x1 < midX)
		{
			return;
		}
		node = first + 3 * (size_t)childNodes;
		childNodes >>= 2;
		nodeX = midX;
		nodeY = midY;
	}
}

PartitionHits PartitionManager::iterateObjectsInRange(const Coord3D &center, float radius, DistanceCalculationType type, PartitionFilterList filters,
                                                      IterOrderType order) const
{
	// RW 0xA3C4E0
	if ((int)type < 0 || (int)type > 4)
	{
		type = FROM_CENTER_2D;
	}
	if (type == FROM_GEOMETRY_OVERLAP)
	{
		throw std::logic_error("partition: distance type 4 (RW 0xA3C7D0) needs the shape overlap test RW 0xAD2CE0, not ported [S-1020]");
	}
	std::uint32_t layers = layerMask(filters);
	const int x0 = (int)cellX(SimMath::pc24Sub(center.x, radius));
	const int x1 = (int)cellX(SimMath::pc24Add(radius, center.x));
	const int y0 = (int)cellY(SimMath::pc24Sub(center.y, radius));
	const int y1 = (int)cellY(SimMath::pc24Add(radius, center.y));
	const float radiusSqr = SimMath::pc24Mul(radius, radius);
	m_walkPreReject = m_preReject && !(x0 == 0 && y0 == 0 && x1 == kCells - 1 && y1 == kCells - 1);
	std::vector<PartitionHit> out = takeBuffer();
	for (int i = 0; i < kLayerCount; ++i)
	{
		if ((layers & 1u) != 0)
		{
			walk(m_trees[(size_t)i].data(), (std::uint32_t)kNodes >> 2, x0, y0, x1, y1, 0, 0, kCells, &center, nullptr, (double)radiusSqr, type, filters, out);
		}
		layers >>= 1;
	}
	sortHits(out, order);
	return PartitionHits(*this, std::move(out));
}

PartitionHits PartitionManager::iterateObjectsInRegion(const PartitionRegion &region, PartitionFilterList filters, IterOrderType order) const
{
	// RW 0xA3C4E0 with no center: the cell rectangle of the region's corners
	std::uint32_t layers = layerMask(filters);
	const int x0 = (int)cellX(region.loX);
	const int x1 = (int)cellX(region.hiX);
	const int y0 = (int)cellY(region.loY);
	const int y1 = (int)cellY(region.hiY);
	std::vector<PartitionHit> out = takeBuffer();
	for (int i = 0; i < kLayerCount; ++i)
	{
		if ((layers & 1u) != 0)
		{
			walk(m_trees[(size_t)i].data(), (std::uint32_t)kNodes >> 2, x0, y0, x1, y1, 0, 0, kCells, nullptr, &region, 0.0, FROM_CENTER_2D, filters, out);
		}
		layers >>= 1;
	}
	sortHits(out, order);
	return PartitionHits(*this, std::move(out));
}

// ---- RW 0xA3A6B0 / 0xA3A700: STLport sort of (object, distance) pairs ------------------------------------------------------------------------------------------

namespace
{
template <class Less>
struct HitSort
{
	Less less;
	typedef PartitionHit T;

	// RW 0xA397B0 __push_heap
	void pushHeap(T *first, int hole, int top, T value)
	{
		int parent = (hole - 1) / 2;
		while (hole > top && less(first[parent], value))
		{
			first[hole] = first[parent];
			hole = parent;
			parent = (hole - 1) / 2;
		}
		first[hole] = value;
	}
	// RW 0xA399E0 __adjust_heap
	void adjustHeap(T *first, int hole, int len, T value)
	{
		const int top = hole;
		int second = 2 * hole + 2;
		while (second < len)
		{
			if (less(first[second], first[second - 1]))
			{
				--second;
			}
			first[hole] = first[second];
			hole = second;
			second = 2 * (second + 1);
		}
		if (second == len)
		{
			first[hole] = first[second - 1];
			hole = second - 1;
		}
		pushHeap(first, hole, top, value);
	}
	// RW 0xA39C60 make_heap, RW 0xA3A2A0 partial_sort (middle == last), RW 0xA3A0D0 sort_heap
	void partialSort(T *first, T *middle, T *last)
	{
		const int len = (int)(middle - first);
		if (len >= 2)
		{
			int parent = (len - 2) / 2;
			for (;;)
			{
				adjustHeap(first, parent, len, first[parent]);
				if (parent == 0)
				{
					break;
				}
				--parent;
			}
		}
		for (T *i = middle; i < last; ++i)
		{
			if (less(*i, *first))
			{
				T v = *i;
				*i = *first;
				adjustHeap(first, 0, len, v);
			}
		}
		while (middle - first > 1)
		{
			--middle;
			T v = *middle;
			*middle = *first;
			adjustHeap(first, 0, (int)(middle - first), v);
		}
	}
	// RW 0xA3A580 __median
	const T &median(const T &a, const T &b, const T &c)
	{
		if (less(a, b))
		{
			if (less(b, c))
			{
				return b;
			}
			if (less(a, c))
			{
				return c;
			}
			return a;
		}
		if (less(a, c))
		{
			return a;
		}
		if (less(b, c))
		{
			return c;
		}
		return b;
	}
	// RW 0xA39AE0 __unguarded_partition
	T *partition(T *first, T *last, T pivot)
	{
		for (;;)
		{
			while (less(*first, pivot))
			{
				++first;
			}
			--last;
			while (less(pivot, *last))
			{
				--last;
			}
			if (!(first < last))
			{
				return first;
			}
			std::swap(*first, *last);
			++first;
		}
	}
	// RW 0xA3A4F0 __introsort_loop (threshold 16 elements)
	void introsortLoop(T *first, T *last, int depth)
	{
		while (last - first > 16)
		{
			if (depth == 0)
			{
				partialSort(first, last, last);
				return;
			}
			--depth;
			const T pivot = median(*first, *(first + (last - first) / 2), *(last - 1));
			T *cut = partition(first, last, pivot);
			introsortLoop(cut, last, depth);
			last = cut;
		}
	}
	// RW 0xA39730 __unguarded_linear_insert
	void unguardedLinearInsert(T *last, T value)
	{
		T *next = last - 1;
		while (less(value, *next))
		{
			*last = *next;
			last = next;
			--next;
		}
		*last = value;
	}
	// RW 0xA39FD0 __linear_insert, RW 0xA3A220 __insertion_sort
	void insertionSort(T *first, T *last)
	{
		if (first == last)
		{
			return;
		}
		for (T *i = first + 1; i != last; ++i)
		{
			T value = *i;
			if (less(value, *first))
			{
				for (T *p = i; p != first; --p)
				{
					*p = *(p - 1);
				}
				*first = value;
			}
			else
			{
				unguardedLinearInsert(i, value);
			}
		}
	}
	// RW 0xA3A410 __final_insertion_sort, RW 0xA39920 __unguarded_insertion_sort
	void finalInsertionSort(T *first, T *last)
	{
		if (last - first > 16)
		{
			insertionSort(first, first + 16);
			for (T *i = first + 16; i != last; ++i)
			{
				unguardedLinearInsert(i, *i);
			}
		}
		else
		{
			insertionSort(first, last);
		}
	}
	void sort(T *first, T *last)
	{
		if (first == last)
		{
			return;
		}
		int lg = 0; // RW 0xA3A6B0: __lg(n) * 2
		for (std::ptrdiff_t n = last - first; n != 1; n >>= 1)
		{
			++lg;
		}
		introsortLoop(first, last, lg * 2);
		finalInsertionSort(first, last);
	}
};

struct NearToFar
{
	bool operator()(const PartitionHit &a, const PartitionHit &b) const { return a.distSqr < b.distSqr; }
};
struct FarToNear
{
	bool operator()(const PartitionHit &a, const PartitionHit &b) const { return b.distSqr < a.distSqr; }
};
} // namespace

void PartitionManager::sortHits(std::vector<PartitionHit> &hits, IterOrderType order)
{
	if (hits.empty())
	{
		return;
	}
	if (order == ITER_SORTED_NEAR_TO_FAR)
	{
		HitSort<NearToFar>{}.sort(hits.data(), hits.data() + hits.size());
	}
	else if (order == ITER_SORTED_FAR_TO_NEAR)
	{
		HitSort<FarToNear>{}.sort(hits.data(), hits.data() + hits.size());
	}
}

// ---- RW 0xA3BDB0: the closest object -------------------------------------------------------------------------------------------------------------------------

namespace
{
// a node of the search (24 bytes in RW: key, node, child node count, x, y, size)
struct SearchNode
{
	std::int32_t key = 0;
	const void *node = nullptr;
	std::uint32_t childNodes = 0;
	std::int32_t x = 0, y = 0, size = 0;
};

// RW 0xA3B0A0 __push_heap (a max-heap on the key: `parent.key < value.key` moves the parent down)
void searchPushHeap(SearchNode *first, int hole, int top, SearchNode value)
{
	while (top < hole)
	{
		const int parent = (hole - 1) / 2;
		if (value.key <= first[parent].key)
		{
			break;
		}
		first[hole] = first[parent];
		hole = parent;
	}
	first[hole] = value;
}

// RW 0xA3B140 __adjust_heap
void searchAdjustHeap(SearchNode *first, int hole, int len, SearchNode value)
{
	const int top = hole;
	int second = 2 * hole + 2;
	while (second < len)
	{
		if (first[second].key < first[second - 1].key)
		{
			--second;
		}
		first[hole] = first[second];
		hole = second;
		second = 2 * second + 2;
	}
	if (second == len)
	{
		first[hole] = first[second - 1];
		hole = second - 1;
	}
	searchPushHeap(first, hole, top, value);
}

// RW 0xA3C1A0 .. 0xA3C24A: ((p -+ r) - lo) * cells * scale, floored and clamped (the order differs from RW 0xA3AD30)
std::uint32_t boundCell(double edge, float lo, float scale)
{
	double t = SimMath::pc24SubW(edge, (double)lo);
	t = SimMath::pc24MulW(t, (double)PartitionManager::kCells);
	t = SimMath::pc24MulW(t, (double)scale);
	const std::int32_t c = SimMath::fistp32((double)SimMath::fstpDword(SimMath::floorD(t)));
	if (c < 0)
	{
		return 0;
	}
	if ((std::uint32_t)c >= (std::uint32_t)PartitionManager::kCells)
	{
		return (std::uint32_t)PartitionManager::kCells - 1;
	}
	return (std::uint32_t)c;
}
} // namespace

Object *PartitionManager::getClosestObject(const Coord3D &center, float maxDist, DistanceCalculationType type, PartitionFilterList filters,
                                           const PartitionRegion *within, float *distSqrOut) const
{
	// RW 0xA3BDC9: a type outside 0 .. 3 is 0
	if ((int)type < 0 || (int)type > 3)
	{
		type = FROM_CENTER_2D;
	}
	std::uint32_t layers = layerMask(filters);
	std::vector<SearchNode> heap;
	// RW 0xA3BE70: one root per walked layer, appended (all keys 0)
	for (int i = 0; i < kLayerCount; ++i)
	{
		if ((layers & 1u) != 0)
		{
			SearchNode n;
			n.node = m_trees[(size_t)i].data();
			n.childNodes = (std::uint32_t)kNodes >> 2;
			n.size = kCells;
			heap.push_back(n);
		}
		layers >>= 1;
	}
	// RW 0xA3BF21: a distance below 1e-5 finds nothing (a NaN goes on)
	if (maxDist < 9.99999975e-06f)
	{
		return nullptr;
	}
	// RW 0xA3BF3C: the distance in cells is kept below 32766
	const double perCell = SimMath::pc24MulW((double)kCells, (double)m_scale);
	if (SimMath::pc24MulW((double)maxDist, perCell) > 32766.0)
	{
		maxDist = SimMath::pc24DivD(32766.0, perCell);
	}
	float best = SimMath::pc24Mul(maxDist, maxDist);
	const std::int32_t cellsRadius = SimMath::fistp32((double)SimMath::fstpDword(SimMath::ceilD(SimMath::pc24MulW(perCell, (double)maxDist))));
	std::int32_t bound = (std::int32_t)((std::uint32_t)(cellsRadius + 1) * (std::uint32_t)(cellsRadius + 1));
	const std::int32_t qx = (std::int32_t)cellX(center.x);
	const std::int32_t qy = (std::int32_t)cellY(center.y);
	std::int32_t rx0 = 0, rx1 = 0, ry0 = 0, ry1 = 0;
	if (within)
	{
		rx0 = (std::int32_t)cellX(within->loX);
		rx1 = (std::int32_t)cellX(within->hiX);
		ry0 = (std::int32_t)cellY(within->loY);
		ry1 = (std::int32_t)cellY(within->hiY);
	}
	Object *winner = nullptr;
	float winnerDist = best;
	while (!heap.empty())
	{
		if (heap.front().key > bound)
		{
			break;
		}
		// RW 0xA3B780 pop_heap: the last element is re-seated from the root
		const SearchNode value = heap.back();
		heap.back() = heap.front();
		searchAdjustHeap(heap.data(), 0, (int)heap.size() - 1, value);
		const SearchNode cur = heap.back();
		heap.pop_back();
		const Node *node = static_cast<const Node *>(cur.node);
		for (const Entry *e = node->head; e; e = e->nodeNext)
		{
			const double d = distance(type, center, *e, 0.0f);
			const float cand = SimMath::fstpDword(d);
			if (d > (double)best)
			{
				continue;
			}
			const Coord3D *p = e->object->getPosition();
			if (within && (p->x < within->loX || p->y < within->loY || p->x > within->hiX || p->y > within->hiY))
			{
				continue;
			}
			Object *obj = e->object;
			if (!filters.empty() && !allowed(*obj, filters))
			{
				continue;
			}
			best = cand;
			winner = obj;
			winnerDist = cand;
			// RW 0xA3C175 .. 0xA3C2AA: the new bound from the winner's cell extent (unsigned differences to the query cell, the larger as unsigned)
			const float r = entryGeometry(*e).circle;
			const std::uint32_t ex0 = boundCell(SimMath::pc24SubW((double)p->x, (double)r), m_loX, m_scale);
			const std::uint32_t ex1 = boundCell(SimMath::pc24AddW((double)r, (double)p->x), m_loX, m_scale);
			std::uint32_t dx = ex0 - (std::uint32_t)qx;
			const std::uint32_t dx1 = ex1 - (std::uint32_t)qx;
			const std::uint32_t ey0 = cellY(SimMath::pc24Sub(p->y, r));
			const std::uint32_t ey1 = cellY(SimMath::pc24Add(r, p->y));
			std::uint32_t dy = ey0 - (std::uint32_t)qy;
			const std::uint32_t dy1 = ey1 - (std::uint32_t)qy;
			if (dx1 > dx)
			{
				dx = dx1;
			}
			if (dy1 > dy)
			{
				dy = dy1;
			}
			bound = (std::int32_t)(dy * dy + dx * dx);
		}
		if (node->below == 0)
		{
			continue;
		}
		// RW 0xA3C2CB: the four children (0, 1, 2, 3), each pushed when it holds entries, lies within the bound and (with a rectangle) overlaps it
		const std::int32_t half = cur.size / 2;
		const Node *child = node + 1;
		for (std::uint32_t c = 0; c < 4; ++c, child += cur.childNodes)
		{
			if (!child->head && child->below == 0)
			{
				continue;
			}
			const std::int32_t cx = ((c & 1u) ? half : 0) + cur.x;
			const std::int32_t cy = ((c & 2u) ? half : 0) + cur.y;
			std::int32_t kx, ky;
			if (qx < cx)
			{
				kx = (cx - qx - 1) * (cx - qx - 1);
			}
			else if (qx < cx + half)
			{
				kx = 0;
			}
			else
			{
				kx = (qx - cx - half) * (qx - cx - half);
			}
			if (qy < cy)
			{
				ky = (cy - qy - 1) * (cy - qy - 1);
			}
			else if (qy < cy + half)
			{
				ky = 0;
			}
			else
			{
				ky = (qy - cy - half) * (qy - cy - half);
			}
			const std::int32_t key = ky + kx;
			if (key > bound)
			{
				continue;
			}
			if (within && (cx > rx1 || cy > ry1 || cx + half < rx0 || cy + half < ry0))
			{
				continue;
			}
			SearchNode n;
			n.key = key;
			n.node = child;
			n.childNodes = cur.childNodes >> 2;
			n.x = cx;
			n.y = cy;
			n.size = half;
			heap.push_back(n);
			searchPushHeap(heap.data(), (int)heap.size() - 1, 0, heap.back());
		}
	}
	if (distSqrOut)
	{
		*distSqrOut = winnerDist;
	}
	return winner;
}

// ---- state hash ----------------------------------------------------------------------------------------------------------------------------------------------

void PartitionManager::crc(StateHasher &h) const
{
	h.addU32((std::uint32_t)m_entryCount);
	h.addFloat(m_loX);
	h.addFloat(m_loY);
	h.addFloat(m_scale);
	// the entry list (newest first) with each entry's links and cells
	for (const Entry *e = m_entries; e; e = e->allNext)
	{
		h.addU32(e->id);
		h.addI32(e->layerPlus1);
		h.addU32(e->x);
		h.addU32(e->y);
		h.addU32(e->mask);
	}
	for (const Entry *e = m_dirty; e; e = e->dirtyNext)
	{
		h.addU32(e->id);
	}
	// every node that holds or leads to entries, in tree order (pre-order, children 0 .. 3), with its list in order
	struct Walk
	{
		static void node(StateHasher &h, const Node *n, std::uint32_t index, std::uint32_t childNodes)
		{
			if (n->below == 0 && !n->head)
			{
				return;
			}
			h.addU32(index);
			h.addI32(n->below);
			for (const Entry *e = n->head; e; e = e->nodeNext)
			{
				h.addU32(e->id);
			}
			if (n->below == 0 || childNodes == 0)
			{
				return;
			}
			for (std::uint32_t c = 0; c < 4; ++c)
			{
				const std::uint32_t offset = 1 + c * childNodes;
				node(h, n + offset, index + offset, childNodes >> 2);
			}
		}
	};
	for (size_t t = 0; t < m_trees.size(); ++t)
	{
		Walk::node(h, m_trees[t].data(), (std::uint32_t)(t * (size_t)kNodes), (std::uint32_t)kNodes >> 2);
	}
}
