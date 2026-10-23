// OpenBFME. GPL-3.0.
//
// TerrainTrees. See GameLogic/Map/TerrainTrees.h for the target facts. Lane SPELL-3.

#include "GameLogic/Map/TerrainTrees.h"

#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameLogic/Economy.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/Module/NotifyCrushModules.h"
#include "GameLogic/Module/SquishCollide.h"
#include "GameLogic/SimMath.h"

#include <stdexcept>
#include <variant>

namespace
{
const float kBucketScale = 49.9f;    // RW 0xBE4AD0
const float kQueryMargin = 7.0f;     // RW 0xBE4AD4
const float kCritterRadius = 5.0f;   // RW 0xBDAE58
const float kPropClearRadius = 25.0f;        // RW 0xC69574 (lane TREES1)
const float kCrushTreeHeight = 20.0f;        // RW 0xBDBC6C (lane TREES1)
const float kConstructionTreeHeight = 35.0f; // RW 0xBE4CB8 (lane TREES1)
const float kConstructionTreeRadius = 14.0f; // RW 0xBDD368 (lane TREES1)

bool hasTreeDraw(const ThingTemplate &tmpl)
{
	for (const ThingTemplate::Nugget &n : tmpl.drawModules().nuggets())
	{
		if (n.name == "W3DTreeDraw")
		{
			return true;
		}
	}
	return false;
}

bool boolField(const ThingTemplate &tmpl, const char *name)
{
	const FieldValue *v = tmpl.getFinalOverride()->findField(name);
	const bool *b = v ? std::get_if<bool>(v) : nullptr;
	return b && *b;
}

// (clamped - lo) / (hi - lo) * 49.9 on the x87 (PC24), then the CRT floor or ceil of the double and fistp of the stored float (RW 0x67D46D .. 0x67D4BE)
int gridIndex(float p, float lo, float hi, bool up)
{
	const float t = SimMath::pc24Mul(SimMath::pc24Div(SimMath::pc24Sub(p, lo), SimMath::pc24Sub(hi, lo)), kBucketScale);
	const double r = up ? SimMath::ceilD((double)t) : SimMath::floorD((double)t);
	return SimMath::fistp32((double)SimMath::fstpDword(r));
}
} // namespace

TerrainTrees::TerrainTrees(GameLogic &logic) : m_logic(logic)
{
	reset();
}

void TerrainTrees::reset()
{
	m_records.clear();
	m_events.clear();
	m_heads.assign((size_t)BUCKETS * BUCKETS, (std::int16_t)-1);
	m_hasExtent = false;
	m_nextIndex = 0;
	m_toppleSpeed = 0.0f;
	m_changedFrame = 0;
	m_changed = 0;
	m_start.clear();
}

// lane SAVE-4e: the records as the map made them, kept before the first one changes (a load restores them, then applies the snapshot's changed records)
void TerrainTrees::keepStart()
{
	if (m_start.empty() && m_changed == 0)
	{
		m_start = m_records;
	}
}

void TerrainTrees::setExtent(float loX, float loY, float hiX, float hiY)
{
	m_lo[0] = loX;
	m_lo[1] = loY;
	m_hi[0] = hiX;
	m_hi[1] = hiY;
	m_hasExtent = true;
}

// RW 0x67D400
int TerrainTrees::bucketOf(float x, float y) const
{
	// the clamps (SSE comiss): lo first, then hi
	x = m_lo[0] > x ? m_lo[0] : x;
	y = m_lo[1] > y ? m_lo[1] : y;
	x = x > m_hi[0] ? m_hi[0] : x;
	y = y > m_hi[1] ? m_hi[1] : y;
	return gridIndex(y, m_lo[1], m_hi[1], false) * BUCKETS + gridIndex(x, m_lo[0], m_hi[0], false);
}

// RW 0x683D89
bool TerrainTrees::addTree(const ThingTemplate &tmpl, const Coord3D &position, std::uint32_t clientId)
{
	m_changedFrame = m_logic.getFrame(); // RW 0x683D9F
	if (!hasTreeDraw(tmpl) || !m_hasExtent)
	{
		return false; // RW 0x683DF6: the debug report only
	}
	const int bucket = bucketOf(position.x, position.y);
	if (bucket < 0 || bucket >= BUCKETS * BUCKETS)
	{
		return false; // the clamps keep the index inside; a degenerate extent (hi == lo) does not
	}
	Record r;
	r.position = position;
	r.clientId = clientId;
	r.index = m_nextIndex++;
	r.tmpl = &tmpl;
	r.next = m_heads[(size_t)bucket]; // RW 0x683E86 .. 0x683E9B: the new record is the bucket's head
	m_heads[(size_t)bucket] = (std::int16_t)m_records.size();
	const long long overrideValue = Economy::templateInt(tmpl, "SupplyOverride"); // ThingTemplate + 0x5C8
	r.value = overrideValue > 0 ? (int)overrideValue : m_logic.economy().settings().supplyBoxesPerTree; // RW 0x683E9E .. 0x683EB6
	r.grabbable = boolField(tmpl, "IsGrabbable");     // + 0x5FB
	r.harvestable = boolField(tmpl, "IsHarvestable"); // + 0x5FC
	m_records.push_back(r);
	return true;
}

void TerrainTrees::bucketRange(const Coord3D &where, float r, int &x0, int &y0, int &x1, int &y1) const
{
	float x = SimMath::subf32(where.x, r), y = SimMath::subf32(where.y, r);
	x = m_lo[0] > x ? m_lo[0] : x;
	y = m_lo[1] > y ? m_lo[1] : y;
	x = x > m_hi[0] ? m_hi[0] : x;
	y = y > m_hi[1] ? m_hi[1] : y;
	x0 = gridIndex(x, m_lo[0], m_hi[0], false);
	y0 = gridIndex(y, m_lo[1], m_hi[1], false);
	x = SimMath::addf32(where.x, r);
	y = SimMath::addf32(where.y, r);
	x = m_lo[0] > x ? m_lo[0] : x;
	y = m_lo[1] > y ? m_lo[1] : y;
	x = x > m_hi[0] ? m_hi[0] : x;
	y = y > m_hi[1] ? m_hi[1] : y;
	x1 = gridIndex(x, m_lo[0], m_hi[0], true);
	y1 = gridIndex(y, m_lo[1], m_hi[1], true);
}

void TerrainTrees::markChanged(Record &tree)
{
	if (!tree.changed)
	{
		tree.changed = true;
		++m_changed;
	}
}

// RW 0x67D3C2: the record is cleared (the bucket link stays)
void TerrainTrees::clearRecord(Record &tree)
{
	tree.position = Coord3D{};
	tree.clientId = 0;
	tree.index = 0;
	tree.tmpl = nullptr;
	tree.toppled = false;
	tree.toppleDirection = Coord3D{};
	tree.value = 1;
	tree.grabbable = true;
	tree.harvestable = true;
}

// RW 0x67F52B -> 0x67E078 with the collector RW 0x67D519
TerrainTrees::Record *TerrainTrees::findClosestTree(const Coord3D &where, float radius, bool ignoreToppled, int mode, const std::function<bool(const Record &)> &accept)
{
	if (m_records.empty() || !m_hasExtent)
	{
		return nullptr;
	}
	const float r = SimMath::addf32(radius, kQueryMargin);
	int x0, y0, x1, y1;
	bucketRange(where, r, x0, y0, x1, y1);
	const float r2 = SimMath::mulf32(r, r);
	Record *best = nullptr;
	float bestD = 0.0f;
	const int n = (int)m_records.size();
	for (int bx = x0; bx < x1; ++bx)
	{
		for (int by = y0; by < y1; ++by)
		{
			if (bx < 0 || by < 0 || bx >= BUCKETS || by >= BUCKETS)
			{
				continue; // the clamps keep the corners in [0, 50]: row 50 / column 50 would read the next row in retail; an index past the grid ends nothing here
			}
			for (int i = m_heads[(size_t)(by * BUCKETS + bx)]; i != -1;)
			{
				if (i < 0 || i >= n)
				{
					break; // RW 0x67E259 .. 0x67E263
				}
				Record &t = m_records[(size_t)i];
				const bool pass = t.clientId != 0 && !(ignoreToppled && t.toppled) && (mode != 1 || t.grabbable) && (mode != 2 || t.harvestable);
				if (pass)
				{
					const float dx = SimMath::subf32(t.position.x, where.x);
					const float dy = SimMath::subf32(t.position.y, where.y);
					const float dz = SimMath::subf32(t.position.z, where.z);
					const float d3 = SimMath::addf32(SimMath::addf32(SimMath::mulf32(dz, dz), SimMath::mulf32(dy, dy)), SimMath::mulf32(dx, dx));
					if (r2 > d3)
					{
						// RW 0x67D519: the 2D squared distance from the query point, dy^2 + dx^2
						const float cx = SimMath::subf32(where.x, t.position.x);
						const float cy = SimMath::subf32(where.y, t.position.y);
						const float d2 = SimMath::addf32(SimMath::mulf32(cy, cy), SimMath::mulf32(cx, cx));
						if ((!best || bestD > d2) && (!accept || accept(t)))
						{
							best = &t;
							bestD = d2;
						}
					}
				}
				i = t.next;
			}
		}
	}
	return best;
}

const TerrainTrees::Record *TerrainTrees::findClosestTree(const Coord3D &where, float radius, bool ignoreToppled, int mode) const
{
	return const_cast<TerrainTrees *>(this)->findClosestTree(where, radius, ignoreToppled, mode); // the query changes nothing
}

// RW 0x6849F8
void TerrainTrees::toppleTree(Record &tree, const Coord3D &from, const Object *source)
{
	if (tree.toppled)
	{
		return;
	}
	const Coord3D &o = source ? *source->getPosition() : from;
	Coord3D dir{ SimMath::subf32(tree.position.x, o.x), SimMath::subf32(tree.position.y, o.y), 0.0f };
	// TheTerrainVisual slot 0x70 (the tree buffer's topple, RW 0x4E9E4E): INFERENCE (S-930) true for a record with an id
	if (tree.clientId == 0)
	{
		return;
	}
	Event e;
	e.kind = Event::TOPPLE;
	e.clientId = tree.clientId;
	e.direction = dir;
	e.speed = m_toppleSpeed;
	e.frame = m_logic.getFrame();
	m_events.push_back(e);
	keepStart(); // lane SAVE-4e review r2: the start is kept before the first change touches a record
	tree.toppled = true;
	tree.toppleDirection = dir;
	markChanged(tree);
	// RW 0x684AB6 .. 0x684B14: the CRITTER_EMITTER objects within 5.0 are destroyed (the birds of the tree)
	static const int critter = ObjectTemplateInfoBuilder::kindOfIndex("CRITTER_EMITTER");
	if (critter >= 0)
	{
		PartitionFilterFn kind([](Object &obj) { return obj.isKindOf((unsigned)critter); });
		const PartitionHits hits = m_logic.partition().iterateObjectsInRange(tree.position, kCritterRadius, FROM_CENTER_3D, { &kind }, ITER_FASTEST);
		for (const PartitionHit &h : hits)
		{
			m_logic.destroyObject(h.object);
		}
	}
	// RW 0x684B14 .. 0x684B3F (lane TREES1): slot 0x4C, the props a cylinder of 25.0 (RW 0xC69574: height and both radii) at the tree, 5.0 (RW 0xBDAE58) higher,
	// overlaps (the prop buffer's RW 0x4EEB15); a client event
	m_events.back().position = Coord3D{ tree.position.x, tree.position.y, SimMath::addf32(tree.position.z, kCritterRadius) };
	ObjectGeometry::Shape c;
	c.type = ObjectGeometry::SHAPE_CYLINDER;
	c.height = kPropClearRadius;
	c.majorRadius = kPropClearRadius;
	c.minorRadius = kPropClearRadius;
	m_events.back().shapes.push_back(c);
}

// RW 0x67D6E1
int TerrainTrees::harvestTree(Record &tree, int amount)
{
	const int taken = amount < tree.value ? amount : tree.value; // RW 0x67D6EC .. 0x67D6FD: `cmp amount, value; jl`
	keepStart(); // lane SAVE-4e review r2: the start is kept before the first change touches a record
	tree.value -= taken;
	markChanged(tree);
	if (tree.value <= 0)
	{
		Event e;
		e.kind = Event::REMOVE; // TheTerrainVisual slot 0x7C (RW 0x67D713 -> RW 0x4EA068: the tree sinks)
		e.clientId = tree.clientId;
		e.frame = m_logic.getFrame();
		m_events.push_back(e);
		clearRecord(tree);
		m_changedFrame = m_logic.getFrame(); // RW 0x67D71D
	}
	return taken;
}

// RW 0x6858BE (lane TREES1)
int TerrainTrees::unitMoved(const Object &obj)
{
	// TheTerrainVisual slot 0x6C (RW 0x4E70F5: the shrubs' push aside) is the client's and not sent (S-5260)
	static const int immobile = ObjectTemplateInfoBuilder::kindOfIndex("IMMOBILE");      // template + 0x108 bit 2
	static const int machine = ObjectTemplateInfoBuilder::kindOfIndex("MACHINE");        // + 0x109 bit 3
	static const int armyOfDead = ObjectTemplateInfoBuilder::kindOfIndex("ARMY_OF_DEAD"); // + 0x11A bit 7
	if (immobile < 0 || machine < 0 || armyOfDead < 0)
	{
		throw std::logic_error("terrain trees: the KindOf names IMMOBILE / MACHINE / ARMY_OF_DEAD are not known");
	}
	if (obj.isKindOf((unsigned)immobile))
	{
		return 0;
	}
	// RW 0x6858E9 .. 0x685917: crusher level (RW 0x695070, a signed byte) above 1, or MACHINE; and not ARMY_OF_DEAD
	if (!(ObjectCrush::crusherLevel(obj) > 1 || obj.isKindOf((unsigned)machine)) || obj.isKindOf((unsigned)armyOfDead))
	{
		return 0;
	}
	const ThingTemplate &tt = *static_cast<const ThingTemplate *>(obj.getTemplate())->getFinalOverride();
	// (obj + 0x38, + 0x44, + 0xA8 with its bounding circle + 0xB8, 1, 0); the circle (RW 0xAD2860) from the template's shapes, as the pathfinder's geometry
	// computes it (a game without a pathfinder has crushers too)
	PathfindGeometry pg;
	ObjectGeometry::fillPathfindGeometry(tt, pg);
	return toppleTreesInGeometry(*obj.getPosition(), obj.getOrientation(), ObjectGeometry::shapesOf(tt), pg.boundingCircleRadius(), obj, true, 0);
}

// RW 0x6854FD (lane TREES1)
int TerrainTrees::toppleTreesInGeometry(const Coord3D &position, float angle, const std::vector<ObjectGeometry::Shape> &shapes, float circle, const Object &source,
                                        bool ignoreToppled, int mode)
{
	// the geometry's bounding circle (`circle`: GeometryInfo + 0x10, RW 0xAD2860) and its box centre (+ 0x18, RW 0xAD15D0) added to the position (SSE)
	const Coord3D offset = ObjectGeometry::boxCentre(shapes);
	const Coord3D centre{ SimMath::addf32(position.x, offset.x), SimMath::addf32(position.y, offset.y), SimMath::addf32(position.z, offset.z) };
	if (m_records.empty() || !m_hasExtent)
	{
		return 0; // RW 0x685568: no record, nothing (the extent is the map's, always set while records exist)
	}
	const float r = SimMath::addf32(circle, kQueryMargin);
	int x0, y0, x1, y1;
	bucketRange(centre, r, x0, y0, x1, y1);
	const float r2 = SimMath::mulf32(r, r);
	// RW 0x685783 .. 0x6857E3: the tree's GeometryInfo (RW 0x450429(CYLINDER, not small, 20.0, 7.0, 7.0))
	std::vector<ObjectGeometry::Shape> tree(1);
	tree[0].type = ObjectGeometry::SHAPE_CYLINDER;
	tree[0].height = kCrushTreeHeight;
	tree[0].majorRadius = kQueryMargin;
	tree[0].minorRadius = kQueryMargin;
	int toppled = 0;
	const int n = (int)m_records.size();
	for (int bx = x0; bx < x1; ++bx)
	{
		for (int by = y0; by < y1; ++by)
		{
			if (bx < 0 || by < 0 || bx >= BUCKETS || by >= BUCKETS)
			{
				continue; // as in findClosestTree
			}
			for (int i = m_heads[(size_t)(by * BUCKETS + bx)]; i != -1;)
			{
				if (i < 0 || i >= n)
				{
					break; // RW 0x685733 .. 0x68573D
				}
				Record &t = m_records[(size_t)i];
				const bool pass = t.clientId != 0 && !(ignoreToppled && t.toppled) && (mode != 1 || t.grabbable) && (mode != 2 || t.harvestable);
				if (pass)
				{
					const float dx = SimMath::subf32(t.position.x, centre.x);
					const float dy = SimMath::subf32(t.position.y, centre.y);
					const float dz = SimMath::subf32(t.position.z, centre.z);
					const float d3 = SimMath::addf32(SimMath::addf32(SimMath::mulf32(dz, dz), SimMath::mulf32(dy, dy)), SimMath::mulf32(dx, dx));
					// RW 0x685840: the geometry (ECX) at the object's position and angle against the tree's at its position, angle 0
					if (r2 > d3 && NotifyCrushModules::geometriesOverlap(shapes, position, angle, tree, t.position, 0.0f))
					{
						const bool was = t.toppled;
						toppleTree(t, centre, &source); // RW 0x684851 with the source object: the direction is from the object's position
						toppled += (!was && t.toppled) ? 1 : 0;
					}
				}
				i = t.next;
			}
		}
	}
	return toppled;
}

// RW 0x684BCA (lane TREES1)
int TerrainTrees::removeTreesForConstruction(const Coord3D &position, const std::vector<ObjectGeometry::Shape> &shapes, float angle)
{
	// RW 0x684BD7 .. 0x684C02: the tree's GeometryInfo (RW 0x450429(CYLINDER, not small, 35.0, 14.0, 14.0))
	std::vector<ObjectGeometry::Shape> tree(1);
	tree[0].type = ObjectGeometry::SHAPE_CYLINDER;
	tree[0].height = kConstructionTreeHeight;
	tree[0].majorRadius = kConstructionTreeRadius;
	tree[0].minorRadius = kConstructionTreeRadius;
	int removed = 0;
	for (Record &t : m_records) // RW 0x684C1A: the record list in order (no buckets)
	{
		if (t.clientId == 0 || !NotifyCrushModules::geometriesOverlap(shapes, position, angle, tree, t.position, 0.0f))
		{
			continue;
		}
		// RW 0x684C43 .. 0x684C54: the fire grid (TheFireLogicSystem) is not ported (S-5260)
		keepStart();
		Event e;
		e.kind = Event::REMOVE_NOW; // slot 0x58 (RW 0x684C69 -> RW 0x4E9C81)
		e.clientId = t.clientId;
		e.frame = m_logic.getFrame();
		m_events.push_back(e);
		clearRecord(t);
		markChanged(t);
		m_changedFrame = m_logic.getFrame(); // RW 0x684C74
		++removed;
	}
	// RW 0x684C86 .. 0x684C9B: slot 0x48, the shrubs and props the geometry overlaps (a client event)
	Event e;
	e.kind = Event::CLEAR_FOOTPRINT;
	e.frame = m_logic.getFrame();
	e.position = position;
	e.angle = angle;
	e.shapes = shapes;
	m_events.push_back(e);
	return removed;
}

void TerrainTrees::crc(StateHasher &h) const
{
	h.addU32((std::uint32_t)m_changed);
	for (size_t i = 0; i < m_records.size(); ++i)
	{
		const Record &r = m_records[i];
		if (!r.changed)
		{
			continue;
		}
		h.addU32((std::uint32_t)i);
		h.addU32(r.clientId);
		h.addBool(r.toppled);
		h.addFloat(r.toppleDirection.x);
		h.addFloat(r.toppleDirection.y);
		h.addI32(r.value);
	}
}

// lane SAVE-4d. Retail saves the tree records inside TerrainLogic::xfer (CHUNK_TerrainLogic, RW 0x683F8C, window (1, 2), tier A: the record count, every
// record, the 2,500 bucket heads, + 0x1910, + 0x1914 and, from version 2, + 0x48). The port's trees are a part of TerrainLogic that the map builds at the
// START, so the block is OpenBFME's own (S-4252): the scalars, then only the records that changed since the map start (the ones the state hash covers),
// each by its list index and in retail's record layout (RW 0x680753, tier A: the decomp's TerrainLogicXfer.cpp Rva0027C230::rva0028002F; window
// (1, 2)): the position, the client id (XferDrawableID, 4 bytes), the template's name, toppled, the topple direction, the value, the bucket link (a short),
// IsGrabbable, IsHarvestable and, from version 2, the running index. The buckets never change after the START (a cleared record keeps its link). The client
// events (topples and removals the tree buffer shows) are not saved: a loaded game's client is not told which trees lie (S-4252).
// The load (SAVE-4a review r1 rules, SAVES.md section 8) reads every record into a temporary and checks it (an index of the map's list given once, a
// template name the data has, the bucket link the START gave that record) before the game's records change. A version 1 record has no running index
// (it keeps the map's).
void TerrainTrees::xfer(Xfer &x)
{
	const bool loading = x.isLoading();
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	std::uint32_t records = loading ? 0 : (std::uint32_t)m_records.size();
	x.xferUnsignedInt(&records);
	std::int32_t nextIndex = loading ? 0 : m_nextIndex;
	x.xferInt(&nextIndex);
	if (loading && (records != m_records.size() || nextIndex != m_nextIndex))
	{
		throw XferException("terrain trees: the snapshot's map has " + std::to_string(records) + " trees (next index " + std::to_string(nextIndex) +
			"), this game's " + std::to_string(m_records.size()) + " (next index " + std::to_string(m_nextIndex) + ")");
	}
	std::uint32_t changedFrame = loading ? 0 : m_changedFrame;
	float toppleSpeed = loading ? 0.0f : m_toppleSpeed;
	x.xferUnsignedInt(&changedFrame);
	x.xferReal(&toppleSpeed);
	std::uint32_t changed = loading ? 0 : (std::uint32_t)m_changed;
	// one changed record at its shortest (version 1, which has no running index): its index (4), version (1), position (12), client id (4), an empty
	// name (1), toppled (1), direction (12), value (4), link (2), two flags (2)
	x.xferCount(&changed, 43, sizeof(std::uint32_t) + sizeof(Record));
	std::vector<std::pair<std::uint32_t, Record>> loaded;
	if (loading)
	{
		loaded.reserve(changed);
	}
	size_t next = 0;
	for (std::uint32_t n = 0; n < changed; ++n)
	{
		std::uint32_t index = 0;
		Record r;
		if (!loading)
		{
			while (!m_records[next].changed)
			{
				++next;
			}
			index = (std::uint32_t)next++;
			r = m_records[index];
		}
		x.xferUnsignedInt(&index);
		XferVersion rv{ 1, 2 };
		x.xferVersion(rv);
		x.xferReal(&r.position.x);
		x.xferReal(&r.position.y);
		x.xferReal(&r.position.z);
		x.xferUnsignedInt(&r.clientId);
		std::string name = r.tmpl ? r.tmpl->getName() : std::string();
		x.xferAsciiString(&name);
		x.xferBool(&r.toppled);
		x.xferReal(&r.toppleDirection.x);
		x.xferReal(&r.toppleDirection.y);
		x.xferReal(&r.toppleDirection.z);
		x.xferInt(&r.value);
		x.xferShort(&r.next);
		x.xferBool(&r.grabbable);
		x.xferBool(&r.harvestable);
		if (rv.current >= 2)
		{
			x.xferInt(&r.index);
		}
		if (!loading)
		{
			continue;
		}
		if (rv.current < 2 && index < m_records.size())
		{
			r.index = (m_start.empty() ? m_records[index] : m_start[index]).index; // RW 0x680753: version 1 leaves + 0x10 as the record was made
		}
		const std::string tree = "terrain trees: tree " + std::to_string(index);
		if (index >= m_records.size())
		{
			throw XferException(tree + " of " + std::to_string(m_records.size()));
		}
		for (const auto &e : loaded)
		{
			if (e.first == index)
			{
				throw XferException(tree + " twice");
			}
		}
		r.tmpl = name.empty() ? nullptr : m_logic.things().findTemplate(name);
		if (!name.empty() && !r.tmpl)
		{
			throw XferException(tree + " names the template '" + name + "', which this game lacks");
		}
		const Record &startRecord = m_start.empty() ? m_records[index] : m_start[index];
		if (r.next != startRecord.next)
		{
			// the buckets are the START's and never change (a cleared record keeps its link, RW 0x67D3C2): any other link could loop a query
			throw XferException(tree + ": a bucket link to " + std::to_string(r.next) + " (the map's is " + std::to_string(startRecord.next) + ")");
		}
		r.changed = true;
		loaded.emplace_back(index, r);
	}
	if (!loading)
	{
		return;
	}
	// lane SAVE-4e: the game's records go back to the map's (kept before the first change), then take the snapshot's changes: a game whose trees changed
	// since its START loads too (a refused load is put back by loading the game's own snapshot, LiveGame::loadSnapshot)
	keepStart();
	m_records = m_start;
	for (const auto &e : loaded)
	{
		m_records[e.first] = e.second;
	}
	m_changed = (int)changed;
	m_changedFrame = changedFrame;
	m_toppleSpeed = toppleSpeed;
}

std::string TerrainTrees::stopLine()
{
	return "[S-930] terrain trees (lane SPELL-3): the logic's tree records (RW 0x683D89), their 50 x 50 buckets (RW 0x67D400), the closest-tree query (RW 0x67E078 / "
		   "0x67D519), topple (RW 0x6849F8, with its CRITTER_EMITTER destruction) and harvest (RW 0x67D6E1) run; INFERENCE: the client's topple answer "
		   "(TheTerrainVisual slot 0x70) is taken as true, the client id is the map object index + 1, the props' removal at the tree (slot 0x4C, a 25.0 "
		   "cylinder) and the sink (slot 0x7C) are client events, the CRITTER_EMITTER query uses the partition's 3D centre distance; the stealth forest "
		   "check reads these records";
}

std::string TerrainTrees::trees1StopLine()
{
	return "[S-5260] terrain trees (lane TREES1): units crush trees (RW 0x6858BE / 0x6854FD: crusher level 2 or more, or MACHINE) and a dozer's structure "
		   "removes the trees, shrubs and props under it (RW 0x684BCA). Not ported: "
		   "the shrubs' push aside (slot 0x6C), the fire grid (TheFireLogicSystem: burning cells take a tree's lumber, RW 0x6886E0), a tree made an object "
		   "(RW 0x68449B: attacked, grabbed, set alight by FireSpreadUpdate), the grab powers, Taint / ElvenWood's tree morph (RW 0x67F6F0 -> 0x4EC40E), "
		   "ClearRemovables (RW 0x5F0EE6), the radius clear (RW 0x683393) and trees an ObjectCreationList adds; INFERENCE: TheTerrainVisual's "
		   "harvest check (slot 0x80, RW 0x4EA005: the tree toppled, not sinking, not morphing) is answered by the logic";
}
