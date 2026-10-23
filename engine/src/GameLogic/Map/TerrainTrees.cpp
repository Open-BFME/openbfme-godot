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
#include "GameLogic/SimMath.h"

#include <variant>

namespace
{
const float kBucketScale = 49.9f;    // RW 0xBE4AD0
const float kQueryMargin = 7.0f;     // RW 0xBE4AD4
const float kCritterRadius = 5.0f;   // RW 0xBDAE58

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

// RW 0x67F52B -> 0x67E078 with the collector RW 0x67D519
TerrainTrees::Record *TerrainTrees::findClosestTree(const Coord3D &where, float radius, bool ignoreToppled, int mode, const std::function<bool(const Record &)> &accept)
{
	if (m_records.empty() || !m_hasExtent)
	{
		return nullptr;
	}
	const float r = SimMath::addf32(radius, kQueryMargin);
	float x = SimMath::subf32(where.x, r), y = SimMath::subf32(where.y, r);
	x = m_lo[0] > x ? m_lo[0] : x;
	y = m_lo[1] > y ? m_lo[1] : y;
	x = x > m_hi[0] ? m_hi[0] : x;
	y = y > m_hi[1] ? m_hi[1] : y;
	const int x0 = gridIndex(x, m_lo[0], m_hi[0], false);
	const int y0 = gridIndex(y, m_lo[1], m_hi[1], false);
	x = SimMath::addf32(where.x, r);
	y = SimMath::addf32(where.y, r);
	x = m_lo[0] > x ? m_lo[0] : x;
	y = m_lo[1] > y ? m_lo[1] : y;
	x = x > m_hi[0] ? m_hi[0] : x;
	y = y > m_hi[1] ? m_hi[1] : y;
	const int x1 = gridIndex(x, m_lo[0], m_hi[0], true);
	const int y1 = gridIndex(y, m_lo[1], m_hi[1], true);
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
	// TheGameClient vslot 0x70 (the tree buffer's topple): INFERENCE (S-930) true for a record with an id
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
	if (!tree.changed)
	{
		tree.changed = true;
		++m_changed;
	}
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
}

// RW 0x67D6E1
int TerrainTrees::harvestTree(Record &tree, int amount)
{
	const int taken = amount < tree.value ? amount : tree.value; // RW 0x67D6EC .. 0x67D6FD: `cmp amount, value; jl`
	keepStart(); // lane SAVE-4e review r2: the start is kept before the first change touches a record
	tree.value -= taken;
	if (!tree.changed)
	{
		tree.changed = true;
		++m_changed;
	}
	if (tree.value <= 0)
	{
		Event e;
		e.kind = Event::REMOVE; // TheGameClient vslot 0x7C (RW 0x67D713)
		e.clientId = tree.clientId;
		e.frame = m_logic.getFrame();
		m_events.push_back(e);
		// RW 0x67D3C2: the record is cleared (the bucket link stays)
		tree.position = Coord3D{};
		tree.clientId = 0;
		tree.index = 0;
		tree.tmpl = nullptr;
		tree.toppled = false;
		tree.toppleDirection = Coord3D{};
		tree.value = 1;
		tree.grabbable = true;
		tree.harvestable = true;
		m_changedFrame = m_logic.getFrame(); // RW 0x67D71D
	}
	return taken;
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
		   "(TheGameClient vslot 0x70) is taken as true, the client id is the map object index + 1, the client effect at the tree (vslot 0x4C, a 25.0 "
		   "cylinder) and the removal (vslot 0x7C) are client events, the CRITTER_EMITTER query uses the partition's 3D centre distance; the stealth forest "
		   "check reads these records";
}
