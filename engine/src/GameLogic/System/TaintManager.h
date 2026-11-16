// OpenBFME. GPL-3.0.
//
// TaintManager (lane TAINTAREA-1): TheTaintManager, the grid of ground cells the Taint (Mordor / Isengard / Wild) and Elven Wood spells paint and the
// TAINT / ELVEN_WOOD condition of AttributeModifierAuraUpdate reads. Ported from RotWK; the BFME2 decomp is the donor (Code/GameEngine/Source/Common/
// TaintManagerImpl.cpp and Bfme5SeventySix.cpp: TaintManagerImpl / BfmeTaintManager, byte-matched, BFME1 donor). Every grid function below is tier A
// (identical bytes) in RotWK and BFME2, so the grid is the same in both editions; the readers differ (HeroModules.cpp, AttributeModifierAuraUpdate).
// ZH has no taint manager.
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * TheTaintManager is the global RW 0xDE435C (BfmeTaintManager: SubsystemInterface + Snapshot, ctor RW 0xAD4A50, vtables RW 0xD006B4 / 0xD006A4); its
//     grid (TaintManagerImpl, 0x30 bytes, ctor = BFME2 0x6C1490) is at + 0x10: region (Region3D, + 0x00), cell size (+ 0x18), its inverse (+ 0x1C),
//     width (+ 0x20), height (+ 0x24), the cells (+ 0x28, 12 bytes each), the client's cell visitor (+ 0x2C).
//   * a cell (ctor = BFME2 0x6C0BA0): kind byte + 0x00 = 0x80 (neutral), value + 0x04 = 0, owner + 0x08 = -1. A kind above 0x80 is elven wood (value 1),
//     below it taint (value 2).
//   * the constructor: region zero, cell size 1.0, then _ReallocCells(region, 1.0); GameLogic::init (RW 0x62CFEF, BFME2 GameLogicInit.cpp:981) calls
//     SetExtents(zero region, GameData PartitionCellSize = GlobalData + 0xD4) right after the shroud's; the new map (RW 0x62FCE2 .. 0x62FCFB, BFME2
//     GameLogicInit.cpp:3308) calls SetExtents(TheTerrainLogic::getExtent, 0.0) then the visitor refresh RW 0xAD4F00, after ThePartitionManager's region.
//   * SetExtents RW 0xAD5660: a cell size <= 0 keeps the current one; a negative width or height is ignored; else _ReallocCells.
//   * _ReallocCells RW 0xAD5340 (x87, PC24): a region narrower than 1.0 gets hi = lo + 1.0; inv = 1 / size; width / height = max(1, ceil((hi - lo) *
//     inv)); every new cell (x, y) takes the KIND of the old cell at floor(((float)i * size + lo - oldLo) * oldInv) when that is inside the old grid
//     (only the kind: the value stays 0 and the owner -1); then region, inv, width, height and size are stored.
//   * reset RW 0xAD4BB0 (vslot 1; GameLogic::reset RW 0x62D2FF): SetExtents(zero region, 0.0).
//   * the paint RW 0xAD4C10 (bfmeApplyCircleWorld(point, radius, amount, absolute, mode), x87 PC24): cell radius = ceil(radius * inv), cell = floor((p - lo)
//     * inv) for y then x; ChangeTaintLevel RW 0xAD56E0: radius < 0, or amount 0 without absolute, does nothing; else the circle raster RW 0xAD52B0 calls the
//     range updater RW 0xAD5040 per row span; it takes the row's cells from bfmeGetCellRange RW 0xAD4E00 (a row outside the grid or a span past its
//     sides: none; else first = row + max(x1, 0), last = row + min(x2 + 1, width)), updates each (RW 0xAD5740) and calls the visitor (x, y, kind).
//   * the cell update RW 0xAD5740 (amount, absolute, mode): mode != -1, amount 0x80 and mode != the cell's owner: nothing (a clear only clears the cells
//     its owner painted); otherwise the kind becomes clamp(absolute ? amount : kind + amount, 0, 255); kind 0x80: value 0 and owner -1; else value 1 (above
//     0x80) or 2 (below) and owner = mode.
//   * the query RW 0xAD49B0 -> RW 0xAD4FA0 -> RW 0xAD4D30: the cell at floor((x - lo.x) * inv), floor((y - lo.y) * inv) (none outside the grid): returns
//     its value and writes its owner; 0 outside the grid (the owner untouched).
//   * the grid is painted only through RW 0xAD4C10, whose callers are the Taint power (RW 0x8C8F47 / 0x8C8F65), the Elven Wood power (RW 0x8C7EAC /
//     0x8C7ECD) and the terrain's radius clear (RW 0x6833EA, GameLogic::clearTaintArea).
//   * the save: CHUNK_TaintManager (retail's list 0 only, not the deep logic CRC list 1): the manager's xfer RW 0xAD4B80 is version (1, 1) then the grid's
//     RW 0xAD50B0: version (1, 1), the region (Region3D), size, inverse (reals), width, height (ints), a checksum word (always 0 when saving, unread when
//     loading), then every cell row by row (RW 0xAD57E0: version (1, 2), the kind byte, the value as 4 raw bytes, version >= 2 the owner int); a load
//     allocates width * height new cells first and refreshes the visitor after. GameLogic::getCRC (RW 0x625886) transfers the grid too (its light CRC:
//     ComputeCRC of every kind byte and value word).
//   * the visitor is the client's (PlayerList::setLocalPlayer RW 0x6A8559 registers the local player's taint callback RW 0x6D8B6E -> TheDisplay and
//     TheRadar); it reads the grid, never writes it. Here it is an optional hook (lane TAINTAREA-1 step 5); a logic without a client has none.

#pragma once

#include "Common/INIDataTypes.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class StateHasher;
class Xfer;

class TaintManager
{
public:
	static constexpr std::uint8_t kNeutral = 0x80;
	enum Value
	{
		VALUE_NONE = 0,       ///< a neutral cell, or outside the grid
		VALUE_ELVEN_WOOD = 1, ///< kind above 0x80
		VALUE_TAINT = 2       ///< kind below 0x80
	};
	struct Cell
	{
		std::uint8_t kind = kNeutral; ///< + 0x00
		std::int32_t value = 0;       ///< + 0x04
		std::int32_t owner = -1;      ///< + 0x08: the painting mode (the painter's object id), -1 none
		bool isDefault() const { return kind == kNeutral && value == 0 && owner == -1; }
	};
	struct Region
	{
		float loX = 0.0f, loY = 0.0f, loZ = 0.0f, hiX = 0.0f, hiY = 0.0f, hiZ = 0.0f;
	};
	using Visitor = std::function<void(int x, int y, std::uint8_t kind)>;

	TaintManager(); ///< BFME2 0x6C1490 (TaintManagerImpl::TaintManagerImpl)

	void init(float cellSize);                              ///< RW 0x62CFEF: SetExtents(zero region, GameData PartitionCellSize)
	void setExtents(const Region &region, float cellSize);   ///< RW 0xAD5660
	void reset();                                           ///< RW 0xAD4BB0
	void applyCircleWorld(const Coord3D &point, float radius, int amount, bool absolute, int mode); ///< RW 0xAD4C10
	void changeTaintLevel(int x, int y, int radius, int amount, bool absolute, int mode);         ///< RW 0xAD56E0
	int getTaintAt(float worldX, float worldY, int *owner) const; ///< RW 0xAD49B0: 0 none, 1 elven wood, 2 taint
	const Cell *cellAt(int x, int y) const;                       ///< RW 0xAD4E70's bounds (BFME2 0x6C0C60)

	// the client's cell callback (RW + 0x2C); refreshVisitor is RW 0xAD4F00 (every cell, row by row)
	void setVisitor(Visitor v) { m_visitor = std::move(v); }
	void refreshVisitor() const;

	int width() const { return m_width; }
	int height() const { return m_height; }
	float cellSize() const { return m_cellSize; }
	const Region &region() const { return m_region; }
	bool anyNonDefault() const { return m_nonDefault > 0; } ///< the state hash's section exists only then (a game without a cast keeps its hash)

	void crc(StateHasher &h) const;
	// RW 0xAD4B80 -> 0xAD50B0 -> 0xAD57E0 in retail's layout. Loading reads into `staged` (validated, nothing of this manager touched); commit() takes it.
	// A load needs this manager's grid (the same map and GameData: extent, cell size, inverse and dimensions), as the game it loads into has
	struct Staged
	{
		Region region;
		float cellSize = 1.0f, cellSizeInv = 1.0f;
		int width = 0, height = 0;
		std::vector<Cell> cells;
	};
	void xfer(Xfer &x, Staged *staged);
	void commit(Staged &&staged);
	// OpenBFME: every cell back to the constructor's state, the extent kept (a snapshot from before the grid was saved: TAINTAREA-1)
	void neutralize();


private:
	void reallocCells(Region region, float cellSize); ///< RW 0xAD5340
	void rasterCircle(int centerX, int centerY, int radius, int amount, bool absolute, int mode); ///< RW 0xAD52B0
	void updateRange(int firstX, int lastX, int y, int amount, bool absolute, int mode);        ///< RW 0xAD5040 with RW 0xAD4E00
	void updateCell(Cell &c, int amount, bool absolute, int mode);                              ///< RW 0xAD5740
	void recount();

	Region m_region;                ///< + 0x00
	float m_cellSize = 1.0f;        ///< + 0x18
	float m_cellSizeInv = 1.0f;     ///< + 0x1C
	int m_width = 0;                ///< + 0x20
	int m_height = 0;               ///< + 0x24
	std::vector<Cell> m_cells;      ///< + 0x28
	Visitor m_visitor;              ///< + 0x2C (client)
	int m_nonDefault = 0;           ///< OpenBFME: cells not in the constructor's state
};
