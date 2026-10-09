// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// PartitionManager (lane MODULES-2): ThePartitionManager of one game, the spatial index every area scan of the logic asks (ZH GameLogic/PartitionManager.h keeps the
// name; RotWK's implementation is NOT ZH's cell grid). Ported from the RotWK binary (caveat S-001); every step names the address it was read at.
//
// TARGET FACTS (RotWK game.dat):
//   * ThePartitionManager (RW global 0xDE4354, made by RW 0x62CE75) forwards to an implementation object at + 0x10 (ctor RW 0xA3BD20). The implementation is a set of
//     21 LOOSE QUADTREES (RW + 0x18: 21 vectors of 12 bytes), one per LAYER: layer -1 (array 0) and the player indices 0 .. 19 (arrays 1 .. 20). The depth is set
//     by RW 0x62CFDE -> 0xA3BBB0(7): 2^7 = 128 cells per side (+ 0x11C), (4^8 - 1) / 3 = 21845 nodes per tree, stored depth first (a node, then its four child
//     subtrees: x low / y low, x high / y low, x low / y high, x high / y high). A node is 8 bytes: + 0 the number of entries in the nodes BELOW it, + 4 the head
//     of its entry list.
//   * The region (RW 0xA3B450, called by the new game RW 0x62FCCD with TheTerrainLogic vslot 0x20's extent, the same call that sizes the shroud): lo / hi corners,
//     scale (+ 0x118) = 1.0f / max(hi.x - lo.x, hi.y - lo.y) under x87 PC24. A cell index (RW 0xA3AD30 x, 0xA3ADA0 y) is fistp(floor((double)(((v - lo) * scale)
//     * cells))) clamped to 0 .. cells - 1 (a negative or invalid value is 0). Setting the region (or the depth) clears every tree and re-inserts every entry in
//     the order of the manager's entry list (RW + 0x114, newest first: RW 0xA394F0 links at the head).
//   * An object enters (RW 0xA3B580, from Object's world entry RW 0x68E31F unless its KindOf has INERT or PROJECTILE) with a 0x30 byte entry (Object + 0x4E8):
//     + 0x10 / + 0x14 its node list links, + 0x18 / + 0x1C the dirty list links, + 0x20 the layer + 1, + 0x24 the size mask, + 0x28 / + 0x2C the cell x / y.
//     Its cells (RW 0xA3AF50): x0 = cellX(pos.x - r), y0 = cellY(pos.y - r), x1 = cellX(pos.x + r), y1 = cellY(pos.y + r) with r the geometry's bounding circle
//     radius (GeometryInfo + 0x10); mask = (x0 ^ x1) | (y0 ^ y1); when mask != 0 its highest bit is cleared from x0 and y0. Insertion (RW 0xA3B220) descends from
//     the root while the child level's bit is clear in the mask (each node passed counts the entry), the child index is (x & bit ? 1 : 0) + (y & bit ? 2 : 0), and
//     the entry is linked at the HEAD of the node's list.
//   * The layer (Object partition interface vslot 0x1C, RW 0x68EE53): the controlling player's index when the object has a controlling player, is not an undetected
//     defector (Object + 0x458 bit 1), its KindOf has no MINE (template + 0x10E bit 7), it has a team and the team has no relationship overrides (team + 0x118 /
//     + 0x11C maps empty, RW 0x7A1C4D / 0x7A207B); else -1. An index outside -1 .. 19 is -1 (RW 0xA3B220).
//   * A moved object is only MARKED (RW 0xA39570: the entry goes to the head of the dirty list, once) by the transform change RW 0x68B244 / 0x68B862 and the team
//     change RW 0x696F0A / 0x69954A. The update (RW 0xA39020 -> 0xA3B4E0, ThePartitionManager vslot 0x28, the partition row RW 0x62E93B before the collision manager)
//     takes the dirty entries from the head: an entry whose layer and cells are unchanged stays where it is, any other is unlinked (RW 0xA3B2E0: the counts of its
//     old path go down) and inserted again (at the head of its new node).
//   * The range query (RW 0xA39340 / 0xA39300 -> 0xA3C4E0): the query's cell rectangle is cellX / cellY of center -+ radius; the layers are walked in array order
//     (layer -1 first, then players 0 .. 19), each kept by the AND of the filters' player masks (filter vslot 2, RW 0xA39490: layer bit k + 1 is player k; layer -1
//     is always walked). Each tree is walked by RW 0xA3A860: the node's own list from the head, then (when entries lie below) its children in the order 0, 1, 2, 3,
//     a child only when it overlaps the rectangle. An entry is kept when its distance (table RW 0xDBDAF8, below) is <= radius^2 (the x87 value; a NaN passes) and
//     every filter allows the object (RW 0xA39450, filter vslot 1, in chain order). The distance stored with the hit is the float (fst dword).
//   * Distance types (RW 0xDBDAF8): 0 RW 0xA3A7A0 dx^2 + dy^2 of the centers; 1 RW 0xA3A7D0 dx^2 + dy^2 + dz^2; 2 RW 0xA3AE50 (sqrt(dx^2 + dy^2) - bounding circle
//     radius), squared and negated when negative; 3 RW 0xA3AEB0 the same in 3D with the bounding sphere radius (GeometryInfo + 0x14) and dz + the geometry's centre
//     height (GeometryInfo + 0x20, RW 0xB4E370); 4 RW 0xA3C7D0 a geometry overlap test (RW 0xAD2CE0 against a sphere of the query radius: 0 or a huge value). A
//     type outside 0 .. 4 is 0.
//   * Sorting (RW 0xA3A750): 1 near to far (RW 0xA3A6B0), 2 far to near (RW 0xA3A700), anything else none. Both are the STLport introsort of (object, distance)
//     pairs on the float distance: median of three, Hoare partition, heap sort past 2 * floor(log2(n)) levels, a final insertion sort of runs of 16. It is not stable:
//     equal distances come out in the order the algorithm leaves them, which this port reproduces step by step.
//   * The closest object (RW 0xA39090 / 0xA39070 -> 0xA3BDB0): a best-first search over the trees with a binary heap of nodes keyed by their squared cell distance;
//     see getClosestObject.
//
// INFERENCE / NOT PORTED (stop S-1020, stopLines()): the geometry is the template's (ObjectGeometry, as CombatQueries; retail keeps it per object and a geometry
// change RW 0x68B2CB marks the entry); the undetected defector bit is never set (no defection is ported: S-141), so it never moves an object to layer -1; distance
// type 4 needs RW 0xAD2CE0 (the shape overlap test, S-952 / S-363): a query of type 4 is refused with the stop; the region of a game without a map is the
// constructor's empty region (RW 0xA3BD20: every object falls into cell 0, 0, as retail's arithmetic gives for a zero extent, and getClosestObject's distance
// clamp 32766 / (cells * scale) makes its range 0: a candidate at distance 0, coincident or overlapping for the edge distance types, can still be returned).

#pragma once

#include "Common/INIDataTypes.h"
#include "GameLogic/ObjectTypes.h"

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class GameLogic;
class Object;
class StateHasher;
class ThingTemplate;

// RW table 0xDBDAF8 (ZH DistanceCalculationType plus RotWK's geometry overlap)
enum DistanceCalculationType
{
	FROM_CENTER_2D = 0,
	FROM_CENTER_3D = 1,
	FROM_BOUNDINGSPHERE_2D = 2,
	FROM_BOUNDINGSPHERE_3D = 3,
	FROM_GEOMETRY_OVERLAP = 4
};

// RW 0xA3A750 (ZH IterOrderType)
enum IterOrderType
{
	ITER_FASTEST = 0,
	ITER_SORTED_NEAR_TO_FAR = 1,
	ITER_SORTED_FAR_TO_NEAR = 2
};

// A partition filter (RW vtable: slot 1 allow(object), slot 2 the mask of the player layers it can accept). Filters are asked in chain order; the first that
// refuses ends the test (RW 0xA39450).
class PartitionFilter
{
public:
	virtual ~PartitionFilter() = default;
	virtual bool allow(Object &obj) = 0;
	// bit k: player k's layer may hold an allowed object (RW 0xA39490 ANDs them); layer -1 is always walked
	virtual std::uint32_t playerLayerMask() const { return 0xFFFFFFFFu; }
};

// a filter made of a predicate (the port's filters that have no own class); the callable is held by value (no std::function allocation per scan):
// PartitionFilterFn f([&](Object &o) { ... });
template <class Fn>
class PartitionFilterFn : public PartitionFilter
{
public:
	explicit PartitionFilterFn(Fn fn)
		: m_fn(std::move(fn))
	{
	}
	bool allow(Object &obj) override { return m_fn(obj); }

private:
	Fn m_fn;
};

struct PartitionHit
{
	Object *object = nullptr;
	float distSqr = 0.0f; ///< the distance the query computed (squared, signed for the bounding types)
};

// the filters of one query, not owned (performance: no vector per call). Built from a braced list ({ &a, &b }: the list lives until the end of the call's
// full expression) or from a vector the caller keeps alive for the call
class PartitionFilterList
{
public:
	PartitionFilterList() = default;
	PartitionFilterList(std::initializer_list<PartitionFilter *> list)
		: m_data(list.begin())
		, m_size(list.size())
	{
	}
	PartitionFilterList(const std::vector<PartitionFilter *> &list)
		: m_data(list.data())
		, m_size(list.size())
	{
	}
	PartitionFilter *const *begin() const { return m_data; }
	PartitionFilter *const *end() const { return m_data + m_size; }
	bool empty() const { return m_size == 0; }
	size_t size() const { return m_size; }

private:
	PartitionFilter *const *m_data = nullptr;
	size_t m_size = 0;
};

class PartitionManager;

// the hits of one range / region query (performance: the buffer comes from the manager's pool and goes back to it when the result dies, so a scan in steady state
// allocates nothing). Iterate it, index it, or read it as a vector; it must not outlive the manager.
class PartitionHits
{
public:
	PartitionHits(const PartitionManager &owner, std::vector<PartitionHit> &&hits)
		: m_owner(&owner)
		, m_hits(std::move(hits))
	{
	}
	PartitionHits(PartitionHits &&other) noexcept
		: m_owner(other.m_owner)
		, m_hits(std::move(other.m_hits))
	{
		other.m_owner = nullptr;
	}
	PartitionHits(const PartitionHits &) = delete;
	PartitionHits &operator=(const PartitionHits &) = delete;
	PartitionHits &operator=(PartitionHits &&) = delete;
	~PartitionHits();
	std::vector<PartitionHit>::const_iterator begin() const { return m_hits.begin(); }
	std::vector<PartitionHit>::const_iterator end() const { return m_hits.end(); }
	size_t size() const { return m_hits.size(); }
	bool empty() const { return m_hits.empty(); }
	const PartitionHit &operator[](size_t i) const { return m_hits[i]; }
	const std::vector<PartitionHit> &vec() const { return m_hits; }
	operator const std::vector<PartitionHit> &() const { return m_hits; }

private:
	const PartitionManager *m_owner;
	std::vector<PartitionHit> m_hits;
};

// a 2D rectangle query (RW 0xA3C4E0 with no center: the entry's position must lie in [lo, hi]; the distance is from the rectangle's centre)
struct PartitionRegion
{
	float loX = 0.0f, loY = 0.0f, hiX = 0.0f, hiY = 0.0f;
};

class PartitionManager
{
public:
	static constexpr int kLayerCount = 21;     // RW 0xA3BD20: 21 trees
	static constexpr int kDepth = 7;           // RW 0x62CFDE
	static constexpr int kCells = 1 << kDepth; // RW + 0x11C
	static constexpr int kNodes = 21845;       // (4^(kDepth + 1) - 1) / 3

	explicit PartitionManager(GameLogic &logic);
	~PartitionManager();
	PartitionManager(const PartitionManager &) = delete;
	PartitionManager &operator=(const PartitionManager &) = delete;

	// a new game (GameLogic::reset, after every object left): the template geometry cache goes (the templates may be a map's overrides); the region stays until
	// the next map sets it
	void reset();
	// RW 0xA3B450: the region (TheTerrainLogic's extent), every entry re-inserted
	void setRegion(float loX, float loY, float hiX, float hiY);
	bool hasRegion() const { return m_regionSet; }

	// RW 0xA3B580 / 0xA3B610 (the object's world entry / exit, RW 0x68E31F / 0x68C18F)
	void registerObject(Object &obj);
	void unRegisterObject(Object &obj);
	bool isRegistered(const Object &obj) const;
	// RW 0xA39570: the entry goes to the head of the dirty list (once until the next update)
	void markDirty(Object &obj);
	// RW 0xA3B4E0: re-link the dirty entries whose layer or cells changed
	void update();

	// RW 0xA39340 -> 0xA3C4E0: the objects within `radius` of `center`
	PartitionHits iterateObjectsInRange(const Coord3D &center, float radius, DistanceCalculationType type, PartitionFilterList filters, IterOrderType order) const;
	// RW 0xA3C4E0 with a rectangle and no center
	PartitionHits iterateObjectsInRegion(const PartitionRegion &region, PartitionFilterList filters, IterOrderType order) const;
	// RW 0xA39090 -> 0xA3BDB0: the closest object within maxDist accepted by the filters (null: none); `within` (optional) restricts the positions to a rectangle.
	// distSqrOut receives the winner's distance
	Object *getClosestObject(const Coord3D &center, float maxDist, DistanceCalculationType type, PartitionFilterList filters,
	                         const PartitionRegion *within = nullptr, float *distSqrOut = nullptr) const;

	// the layer an object belongs to now (RW 0x68EE53), -1 .. 19
	int layerOf(const Object &obj) const;
	// tests: the layer, cell and mask an entry is linked with (false: not registered)
	bool entryCells(const Object &obj, int &layer, std::uint32_t &x, std::uint32_t &y, std::uint32_t &mask) const;
	size_t dirtyCount() const;
	size_t entryCount() const { return m_entryCount; }

	void crc(StateHasher &h) const;
	static std::vector<std::string> stopLines();
	// tests: the range walk's double-precision pre-reject (surelyBeyond) on or off; the results are the same either way (not state)
	void setPreReject(bool on) { m_preReject = on; }

	// RW 0xA3A6B0 / 0xA3A700: the STLport introsort of the hits on distSqr (exposed for the tests)
	static void sortHits(std::vector<PartitionHit> &hits, IterOrderType order);

	struct Geometry
	{
		float circle = 0.01f; ///< GeometryInfo + 0x10
		float sphere = 0.0f;  ///< + 0x14
		float centerZ = 0.0f; ///< + 0x20
	};
	const Geometry &geometryOf(const Object &obj) const;

private:
	struct Entry;
	struct Node
	{
		std::int32_t below = 0; ///< + 0: entries in the nodes below
		Entry *head = nullptr;  ///< + 4
	};

	std::uint32_t cellX(float x) const;
	std::uint32_t cellY(float y) const;
	void computeCells(const Entry &e, std::uint32_t &x, std::uint32_t &y, std::uint32_t &mask) const;
	void insert(Entry &e);
	void remove(Entry &e);
	void clearTrees();
	void walk(const Node *node, std::uint32_t childNodes, int x0, int y0, int x1, int y1, int nodeX, int nodeY, int size, const Coord3D *center,
	          const PartitionRegion *region, double radiusSqr, DistanceCalculationType type, const PartitionFilterList &filters,
	          std::vector<PartitionHit> &out) const;
	double distance(DistanceCalculationType type, const Coord3D &center, const Entry &e, float radiusSqr) const;
	std::uint32_t layerMask(const PartitionFilterList &filters) const;
	bool allowed(Object &obj, const PartitionFilterList &filters) const;
	// true when the exact distance of the entry is certainly above radiusSqr (a double-precision bound with a margin far above the PC24 rounding): the walk
	// skips the exact x87 arithmetic of a candidate it would reject anyway; never true for a NaN or a candidate near the boundary
	bool surelyBeyond(DistanceCalculationType type, const Coord3D &center, const Entry &e, double radiusSqr) const;
	const Geometry &entryGeometry(const Entry &e) const;
	std::vector<PartitionHit> takeBuffer() const;
	friend class PartitionHits;
	void recycle(std::vector<PartitionHit> &&hits) const;

	GameLogic &m_logic;
	float m_loX = 0.0f, m_loY = 0.0f, m_hiX = 0.0f, m_hiY = 0.0f;
	float m_scale = 0.0f; ///< + 0x118
	bool m_regionSet = false;
	std::vector<std::vector<Node>> m_trees; ///< 21 x kNodes
	Entry *m_entries = nullptr;             ///< + 0x114, newest first
	Entry *m_dirty = nullptr;               ///< + 0x120
	size_t m_entryCount = 0;
	std::vector<std::unique_ptr<Entry>> m_byId; ///< Object + 0x4E8, indexed by object id
	mutable std::unordered_map<const ThingTemplate *, Geometry> m_geometry; ///< looked up only (never iterated); element references stay valid
	mutable std::vector<std::vector<PartitionHit>> m_hitPool;              ///< the buffers of finished queries (PartitionHits)
	bool m_preReject = true;
	mutable bool m_walkPreReject = true; ///< this walk's: off when the query rectangle is the whole grid (every candidate is near: the bound would only cost)
	int m_mineBit = -1;
};
