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
// INFERENCE (stop S-930, reported): the client's topple answer is taken as true for every record whose id is set (the tree buffer's applyTopplingForce,
// ZH W3DTreeBuffer.cpp, refuses only a tree that is not upright, which the toppled flag already filters); the client id is the map object's index + 1
// (the classified drawable both sides share); the vslot 0x4C call (a client effect at the tree) and the vslot 0x7C call are client events only
// (TerrainTrees::events), not simulation; the CRITTER_EMITTER objects use the partition's centre distance in 2D (FROM_CENTER_2D).

#pragma once

#include "Common/INIDataTypes.h"
#include "Common/StateHash.h"

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
			TOPPLE,
			REMOVE
		};
		Kind kind = TOPPLE;
		std::uint32_t clientId = 0;
		Coord3D direction{};
		float speed = 0.0f; ///< TerrainLogic + 0x1918 when the tree was toppled (Devastation sets 0.1 for its loop, RW 0x8CC75C)
		std::uint32_t frame = 0;
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

private:
	int bucketOf(float x, float y) const;            ///< RW 0x67D400
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
