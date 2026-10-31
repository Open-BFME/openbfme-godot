// OpenBFME. GPL-3.0.
//
// TerrainTrees (lane SPELL-3): the logic's list of the map's optimized trees, RotWK's TheTerrainLogic + 0x578 (the records), + 0x588 (50 x 50 bucket heads)
// and the calls on them that the spell book's Devastation makes. The trees themselves are client-only drawables (MAPOBJ_CLIENT_TREE: the tree buffer draws
// them); the logic keeps where they stand, whether they toppled and how much lumber they still hold.
//
// TARGET FACTS (RotWK game.dat, caveat S-001), read from the disassembly:
//   * the record (constructor RW 0x67D359, 0x30 bytes): + 0 the position, + 0xC the client tree id (0: removed), + 0x10 the running index (TerrainLogic + 0x48),
//     + 0x14 the template, + 0x18 toppled, + 0x1C the topple direction, + 0x28 the value (supply boxes), + 0x2C IsGrabbable (ThingTemplate + 0x5FB), + 0x2D
//     IsHarvestable (+ 0x5FC), + 0x2E the next record of the bucket (a short, -1 ends);
//   * add (RW 0x683D89; BFME2 decomp TerrainLogicXfer.cpp TerrainLogic::rva00283642, tier B same-shape): TerrainLogic + 0x1910 = the frame; a template without
//     a W3DTreeDraw module adds nothing; the bucket (RW 0x67D400) of the position becomes the list head: the new record's next is the old head; the value is
//     the template's SupplyOverride (+ 0x5C8) when above 0, else GameData SupplyBoxesPerTree (GlobalData + 0xA60); the record is appended;
//   * the bucket (RW 0x67D400): the extent is TerrainLogic vslot 0x28 (RW 0x4622AC: the map including its border, lo = -border * 10, hi = size * 10 -
//     border * 10); x and y are clamped into it, then floor((p - lo) / (hi - lo) * 49.9) on the x87 (MSVCR71 floor, fistp), bucket = y * 50 + x;
//   * the query (RW 0x67E078, through RW 0x67F52B with the closest collector RW 0x67D4F0 / 0x67D519): radius += 7.0 (RW 0xBE4AD4, SSE); the bucket rectangle
//     from floor((clamp(p - r) - lo) / (hi - lo) * 49.9) to ceil((clamp(p + r) - lo) / (hi - lo) * 49.9), exclusive; buckets walked x outer, y inner, each
//     list from its head; a record counts when its id is not 0, (ignoreToppled: not toppled), (mode 1: IsGrabbable, mode 2: IsHarvestable), and its 3D
//     squared distance (SSE (dz^2 + dy^2) + dx^2) is below r^2; the collector keeps the first record of the smallest 2D squared distance (dy^2 + dx^2,
//     strictly smaller replaces);
//   * topple (RW 0x684BAA -> 0x6849F8): a toppled record does nothing; the direction is the record's position minus the source object's position (or the
//     given location without one), z = 0; the client's topple call (TheGameClient vslot 0x70 with the client id and the direction) must answer true, then
//     toppled = 1 and the direction is kept; ThePartitionManager's objects within 5.0 (RW 0xBDAE58) of KindOf CRITTER_EMITTER (bit 0x91) are destroyed
//     (RW 0x62BBAB) in the iterator's order; a client call with a cylinder GeometryInfo of 25.0 (RW 0xC69574) at the position 5.0 higher follows (vslot 0x4C);
//   * harvest (RW 0x67D6E1): taken = min(asked, value); value -= taken; at or below 0 the client is told (vslot 0x7C with the id), the record is cleared
//     (RW 0x67D3C2: the id 0, so no query finds it again) and TerrainLogic + 0x1910 = the frame.
//   * lane TREES1: the client calls go to TheTerrainVisual (RW 0xDE4AC8, W3DTerrainVisual, vtable RW 0xBDDFC8; GameClient::init RW 0x646771 makes it), not
//     TheGameClient: its slots forward through BaseHeightMap to the tree buffer (0x54 add, 0x58 remove at once, 0x70 topple, 0x7C sink, 0x80 the harvest
//     check), the shrub buffer (0x48, 0x6C) and the prop buffer (0x48, 0x4C);
//   * units crush trees (RW 0x6858BE, from Object's cell change RW 0x69264D for an object that is not IMMOBILE and is INFANTRY, CAVALRY, MONSTER or
//     MACHINE): TheTerrainVisual slot 0x6C (the shrubs' push aside), then an object that is not IMMOBILE, not ARMY_OF_DEAD and has a crusher level (RW
//     0x695070) of 2 or more, or is a MACHINE, topples the trees its geometry touches: RW 0x6854FD(position, angle, geometry, the object, ignoreToppled 1,
//     mode 0) walks the buckets of the geometry's box centre (GeometryInfo + 0x18, RW 0xAD15D0, added to the position, SSE) with the radius GeometryInfo +
//     0x10 (the bounding circle) + 7.0 as the query does; a record passes the query's filters and its 3D squared distance from the centre ((dz^2 + dy^2) +
//     dx^2, SSE) must be below r^2; then a cylinder of height 20.0 (RW 0xBDBC6C) and radius 7.0 (RW 0x450429(1, 0, 20, 7, 7)) at the tree with angle 0 must
//     overlap the object's geometry at its position and angle (RW 0xAD2CE0); the tree is toppled from the object (RW 0x6849F8 with the source);
//   * construction (RW 0x684BCA(position, geometry, angle), removeTreesForConstruction; callers: the dozer's placement RW 0x88D44F for a structure that is
//     neither NO_COLLIDE nor DO_NOT_CLASSIFY, and the ObjectCreationList's ClearRemovables RW 0x5F0EE6): every record of the list in order with an id whose
//     cylinder (height 35.0, RW 0xBE4CB8; radius 14.0, RW 0xBDD368) at angle 0 overlaps the geometry (RW 0xAD2CE0) leaves the fire grid (RW 0x686B2E, when
//     TheFireLogicSystem exists), is cleared (RW 0x67D3C2), goes from the client at once (slot 0x58) and stamps TerrainLogic + 0x1910; then slot 0x48 takes
//     the shrubs (cylinder 35 / 14) and props (cylinder of height 5 * scale, radius 2 * scale, RW 0x4EEB15) the geometry overlaps.
// INFERENCE (stop S-930, reported): the client's topple answer is taken as true for every record whose id is set (the tree buffer's applyTopplingForce,
// RW 0x4E9E4E, refuses a tree that is not upright, which the toppled flag already filters, or one that morphs, which the port's trees never do); the client
// id is the map object's index + 1 (the classified drawable both sides share); the slot 0x4C / 0x48 / 0x58 / 0x7C calls are client events only
// (TerrainTrees::events), not simulation; the CRITTER_EMITTER objects use the partition's centre distance in 2D (FROM_CENTER_2D). Lane TREES1 (stop S-5260):
// slot 0x6C's shrub push aside is not sent (the client's shrubs do not bend), and the fire grid (TheFireLogicSystem: burning cells take a tree's lumber,
// RW 0x6886E0 -> 0x67F4C2) is not ported.

#pragma once

#include "Common/INIDataTypes.h"
#include "Common/StateHash.h"
#include "GameLogic/Object/ObjectGeometry.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class GameLogic;
class Object;
class ThingTemplate;

class TerrainTrees
{
public:
	struct Record
	{
		Coord3D position{};                ///< + 0
		std::uint32_t clientId = 0;        ///< + 0xC (0: removed)
		int index = 0;                     ///< + 0x10
		const ThingTemplate *tmpl = nullptr; ///< + 0x14
		bool toppled = false;              ///< + 0x18
		Coord3D toppleDirection{};         ///< + 0x1C
		int value = 0;                     ///< + 0x28 (supply boxes)
		bool grabbable = false;            ///< + 0x2C
		bool harvestable = false;          ///< + 0x2D
		std::int16_t next = -1;            ///< + 0x2E
		bool changed = false;              ///< toppled or harvested since the map start (the hash covers only these)
	};
	// what the client shows: a tree starts to fall (TheGameClient vslot 0x70) or its lumber is gone (vslot 0x7C)
	struct Event
	{
		enum Kind
		{
			TOPPLE, ///< slot 0x70, then slot 0x4C: the props within a cylinder of 25.0 at the tree, 5.0 higher (`position`, `shapes`)
			REMOVE, ///< slot 0x7C: the lumber is gone, the tree sinks
			REMOVE_NOW, ///< lane TREES1: slot 0x58 (construction): the tree goes at once
			CLEAR_FOOTPRINT ///< lane TREES1: slot 0x48 (construction): the shrubs and props the geometry (`position`, `angle`, `shapes`) overlaps go
		};
		Kind kind = TOPPLE;
		std::uint32_t clientId = 0;
		Coord3D direction{};
		float speed = 0.0f; ///< TerrainLogic + 0x1918 when the tree was toppled (Devastation sets 0.1 for its loop, RW 0x8CC75C)
		std::uint32_t frame = 0;
		Coord3D position{};
		float angle = 0.0f;
		std::vector<ObjectGeometry::Shape> shapes;
	};
	enum
	{
		BUCKETS = 50 ///< RW 0x67D4C4: `imul eax, eax, 0x32`
	};

	explicit TerrainTrees(GameLogic &logic);

	void reset();
	// the bucket grid's extent (RW 0x4622AC); MapObjectLoop sets it from the map before the trees are added
	void setExtent(float loX, float loY, float hiX, float hiY);
	bool hasExtent() const { return m_hasExtent; }
	// RW 0x683D89; false (nothing added) for a template without a W3DTreeDraw module or before the extent is set
	bool addTree(const ThingTemplate &tmpl, const Coord3D &position, std::uint32_t clientId);
	// RW 0x67F52B: the closest record within `radius` (see the header), null when none. Lane LUMBER1: `accept` (the harvest finder's collector RW 0x67D5A0:
	// the same 2D distance and order, then the worker's quick path and ground line tests) is asked only for a record that would become the closest; a
	// refused one is passed over
	Record *findClosestTree(const Coord3D &where, float radius, bool ignoreToppled, int mode, const std::function<bool(const Record &)> &accept = nullptr);
	const Record *findClosestTree(const Coord3D &where, float radius, bool ignoreToppled, int mode) const;
	// RW 0x6849F8 with the source object (or none: `from` is the location)
	void toppleTree(Record &tree, const Coord3D &from, const Object *source);
	// RW 0x67D6E1: the supply boxes taken
	int harvestTree(Record &tree, int amount);
	// lane TREES1: RW 0x6858BE, an object entered a new cell (Object's RW 0x69264D gate): a crusher topples the trees under it; the number toppled
	int unitMoved(const Object &obj);
	// lane TREES1: RW 0x6854FD: the trees whose cylinder (20 high, radius 7) the geometry (its bounding circle `circle`) at `position` / `angle` touches are
	// toppled from `source`; the number toppled
	int toppleTreesInGeometry(const Coord3D &position, float angle, const std::vector<ObjectGeometry::Shape> &shapes, float circle, const Object &source,
	                          bool ignoreToppled, int mode);
	// lane TREES1: RW 0x684BCA, removeTreesForConstruction: the trees the geometry overlaps go (no topple, no lumber); the number removed
	int removeTreesForConstruction(const Coord3D &position, const std::vector<ObjectGeometry::Shape> &shapes, float angle);

	// TerrainLogic + 0x1918, the topple speed the client is given (Devastation sets it around its loop)
	void setToppleSpeed(float s) { m_toppleSpeed = s; }

	const std::vector<Record> &records() const { return m_records; }
	// the client events in order; the client reads from its last index (the list only grows during a game)
	const std::vector<Event> &events() const { return m_events; }
	bool anyChanged() const { return m_changed > 0; }
	// the changed records (toppled or harvested), in list order: a game where no tree changed keeps its hash
	void crc(StateHasher &h) const;
	// lane SAVE-4d: the block obfme:TerrainTrees (see TerrainTrees.cpp, S-4252): the changed records in retail's record layout; a load goes into a game of the
	// same START (lane SAVE-4e: its records are put back to the map's first, so the game's own changes since need not be undone), and throws an XferException
	// naming a mismatch
	void xfer(class Xfer &x);
	static std::string stopLine(); ///< [S-930]
	static std::string trees1StopLine(); ///< [S-5260] (lane TREES1)

private:
	int bucketOf(float x, float y) const;            ///< RW 0x67D400
	// the query's bucket rectangle around `where` for the radius r (already + 7.0): [x0, x1) x [y0, y1) (RW 0x67E0A3 .. 0x67E1F5, the same in RW 0x6854FD)
	void bucketRange(const Coord3D &where, float r, int &x0, int &y0, int &x1, int &y1) const;
	void clearRecord(Record &tree);                  ///< RW 0x67D3C2
	void markChanged(Record &tree);
	void keepStart();                                ///< lane SAVE-4e: m_start before the first change
	std::vector<Record> m_start;                     ///< lane SAVE-4e: the records as the map made them (empty until the first change)
	GameLogic &m_logic;
	std::vector<Record> m_records;   ///< + 0x578
	std::vector<std::int16_t> m_heads; ///< + 0x588 (50 * 50)
	std::vector<Event> m_events;
	float m_lo[2] = { 0.0f, 0.0f }, m_hi[2] = { 0.0f, 0.0f };
	bool m_hasExtent = false;
	int m_nextIndex = 0;             ///< + 0x48
	float m_toppleSpeed = 0.0f;      ///< + 0x1918
	std::uint32_t m_changedFrame = 0; ///< + 0x1910
	int m_changed = 0;
};
