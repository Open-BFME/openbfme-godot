// OpenBFME. GPL-3.0.
//
// TheTaintManager (lane TAINTAREA-1): see TaintManager.h for the target facts. The x87 sequences are PC24 (the game's control word, RW setFPMode
// 0x440809): every fsub / fadd / fmul / fdiv rounds to binary32 in the register, the CRT floor / ceil take the stored double, FISTP stores the integral
// value. The same sequences as ShroudManager::setExtent (RW 0xB51D90), which is the same donor code.

#include "GameLogic/System/TaintManager.h"

#include "Common/StateHash.h"
#include "Common/Xfer.h"
#include "GameLogic/SimMath.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

TaintManager::TaintManager()
{
	// BFME2 0x6C1490: region zero, width / height 0, no cells, no visitor, cell size 1.0, then _ReallocCells(region, 1.0) (the old grid is empty, so its
	// inverse, never initialised there, is never read)
	reallocCells(m_region, 1.0f);
}

void TaintManager::init(float cellSize)
{
	setExtents(Region{}, cellSize); // RW 0x62CFEF: zero region, GameData PartitionCellSize (GlobalData + 0xD4)
}

// RW 0xAD5660 (BFME2 Bfme5SeventySix.cpp TaintManagerImpl::SetExtents)
void TaintManager::setExtents(const Region &region, float cellSize)
{
	if (cellSize <= 0.0f)
	{
		cellSize = m_cellSize;
	}
	if (!(SimMath::pc24Sub(region.hiX, region.loX) < 0.0f) && !(SimMath::pc24Sub(region.hiY, region.loY) < 0.0f))
	{
		reallocCells(region, cellSize);
	}
}

// RW 0xAD4BB0 (BFME2 BfmeTaintManager::bfmeResetGrid): the grid's empty reset (the ICF fold RW 0xAD4950 -> BFME2 0x69E440), then a zero region
void TaintManager::reset()
{
	setExtents(Region{}, 0.0f);
}

// RW 0xAD5340 (BFME2 TaintManagerImpl.cpp TaintManagerImpl::_ReallocCells)
void TaintManager::reallocCells(Region region, float cellSize)
{
	if (SimMath::pc24Sub(region.hiX, region.loX) < 1.0f)
	{
		region.hiX = SimMath::pc24Add(region.loX, 1.0f);
	}
	if (SimMath::pc24Sub(region.hiY, region.loY) < 1.0f)
	{
		region.hiY = SimMath::pc24Add(region.loY, 1.0f);
	}
	const float inv = SimMath::pc24Div(1.0f, cellSize);
	const int width = std::max(SimMath::ceilToInt(SimMath::pc24Mul(SimMath::pc24Sub(region.hiX, region.loX), inv)), 1);
	const int height = std::max(SimMath::ceilToInt(SimMath::pc24Mul(SimMath::pc24Sub(region.hiY, region.loY), inv)), 1);
	std::vector<Cell> cells((size_t)width * (size_t)height); // RW 0xAD545A: the cell constructor (kind 0x80, value 0, owner -1)
	for (int y = 0; y < height; ++y)
	{
		// RW 0xAD5496 .. 0xAD54C5: fild y; fmul size; fadd lo.y; fsub old lo.y; fmul old inverse; floor; fistp
		const int oldY = SimMath::floorToInt(SimMath::pc24Mul(SimMath::pc24Sub(SimMath::pc24Add(SimMath::pc24MulD((double)y, cellSize), region.loY), m_region.loY), m_cellSizeInv));
		if (oldY < 0 || oldY >= m_height)
		{
			continue;
		}
		for (int x = 0; x < width; ++x)
		{
			const int oldX = SimMath::floorToInt(SimMath::pc24Mul(SimMath::pc24Sub(SimMath::pc24Add(SimMath::pc24MulD((double)x, cellSize), region.loX), m_region.loX), m_cellSizeInv));
			if (oldX >= 0 && oldX < m_width)
			{
				cells[(size_t)y * (size_t)width + (size_t)x].kind = m_cells[(size_t)oldY * (size_t)m_width + (size_t)oldX].kind; // RW 0xAD5534: the kind only
			}
		}
	}
	m_cells.swap(cells);
	m_region = region;
	m_cellSizeInv = inv;
	m_width = width;
	m_height = height;
	m_cellSize = cellSize;
	recount();
}

// RW 0xAD4C10 (BFME2 Bfme5SeventySix.cpp BfmeTaintManager::bfmeApplyCircleWorld)
void TaintManager::applyCircleWorld(const Coord3D &point, float radius, int amount, bool absolute, int mode)
{
	const int cellRadius = SimMath::ceilToInt(SimMath::pc24Mul(radius, m_cellSizeInv));                         // RW 0xAD4C13 .. 0xAD4C38
	const int y = SimMath::floorToInt(SimMath::pc24Mul(SimMath::pc24Sub(point.y, m_region.loY), m_cellSizeInv)); // RW 0xAD4C43 .. 0xAD4C63
	const int x = SimMath::floorToInt(SimMath::pc24Mul(SimMath::pc24Sub(point.x, m_region.loX), m_cellSizeInv)); // RW 0xAD4C67 .. 0xAD4C88
	changeTaintLevel(x, y, cellRadius, amount, absolute, mode);
}

// RW 0xAD56E0 (BFME2 TaintManagerImpl::ChangeTaintLevel)
void TaintManager::changeTaintLevel(int x, int y, int radius, int amount, bool absolute, int mode)
{
	if (radius < 0)
	{
		return;
	}
	if (amount == 0 && !absolute)
	{
		return;
	}
	rasterCircle(x, y, radius, amount, absolute, mode);
}

// RW 0xAD52B0 (BFME2 TaintManagerImpl.cpp bfmeRasterCircleFC): the midpoint circle, one span per row
void TaintManager::rasterCircle(int centerX, int centerY, int radius, int amount, bool absolute, int mode)
{
	int x = 0;
	int y = radius;
	int d = (1 - radius) * 2;
	int firstX = centerX;
	int lastX = centerX;
	for (;;)
	{
		if (d + y > 0)
		{
			if (y == 0 && radius == 1)
			{
				++x;
				++lastX;
				--firstX;
			}
			updateRange(firstX, lastX, centerY + y, amount, absolute, mode);
			if (y == 0)
			{
				return;
			}
			updateRange(firstX, lastX, centerY - y, amount, absolute, mode);
			--y;
			d -= (y * 2 - 1);
		}
		if (x > d)
		{
			++x;
			++lastX;
			--firstX;
			d += (x * 2 + 1);
		}
	}
}

// RW 0xAD5040 (the range updater) with RW 0xAD4E00 (bfmeGetCellRange)
void TaintManager::updateRange(int firstX, int lastX, int y, int amount, bool absolute, int mode)
{
	if (lastX < 0 || firstX >= m_width || y < 0 || y >= m_height)
	{
		return;
	}
	const size_t row = (size_t)y * (size_t)m_width;
	const size_t first = row + (size_t)std::max(firstX, 0); // RW 0xAD4E00: x1 > 0 moves the first cell
	const size_t last = row + (size_t)(lastX < m_width ? lastX + 1 : m_width);
	int cellX = firstX; // RW 0xAD5040: the visitor's x starts at the span's first x (not the clamped cell's: a retail quirk, client only)
	for (size_t i = first; i < last; ++i)
	{
		updateCell(m_cells[i], amount, absolute, mode);
		if (m_visitor)
		{
			m_visitor(cellX, y, m_cells[i].kind);
		}
		++cellX;
	}
}

// RW 0xAD5740 (BFME2 TaintManagerImpl.cpp BfmeCellFC::bfmeUpdate)
void TaintManager::updateCell(Cell &c, int amount, bool absolute, int mode)
{
	if (mode != -1 && amount == kNeutral && mode != c.owner)
	{
		return; // a clear by a painter only clears its own cells
	}
	const bool was = c.isDefault();
	if (!absolute)
	{
		amount += c.kind;
	}
	amount = std::clamp(amount, 0, 255);
	c.kind = (std::uint8_t)amount;
	if (c.kind == kNeutral)
	{
		mode = -1;
		c.value = VALUE_NONE;
	}
	else
	{
		c.value = c.kind > kNeutral ? VALUE_ELVEN_WOOD : VALUE_TAINT;
	}
	c.owner = mode;
	m_nonDefault += (was ? 1 : 0) - (c.isDefault() ? 1 : 0);
}

// RW 0xAD49B0 -> 0xAD4FA0 -> 0xAD4D30 (BFME2 rva006C0850 -> rva006C0E40 -> bfmeCellAtWorld)
int TaintManager::getTaintAt(float worldX, float worldY, int *owner) const
{
	const int x = SimMath::floorToInt(SimMath::pc24Mul(SimMath::pc24Sub(worldX, m_region.loX), m_cellSizeInv));
	if (x < 0 || x >= m_width)
	{
		return VALUE_NONE;
	}
	const int y = SimMath::floorToInt(SimMath::pc24Mul(SimMath::pc24Sub(worldY, m_region.loY), m_cellSizeInv));
	if (y < 0 || y >= m_height)
	{
		return VALUE_NONE;
	}
	const Cell &c = m_cells[(size_t)y * (size_t)m_width + (size_t)x];
	if (owner)
	{
		*owner = c.owner;
	}
	return c.value;
}

// BFME2 0x6C0C60 (TaintManagerImpl::rva006C0C60): the cell at cell coordinates, none outside the grid
const TaintManager::Cell *TaintManager::cellAt(int x, int y) const
{
	if (x < 0 || x >= m_width || y < 0 || y >= m_height)
	{
		return nullptr;
	}
	return &m_cells[(size_t)y * (size_t)m_width + (size_t)x];
}

// RW 0xAD4F00 (BFME2 TaintManagerImpl::rva006C0DA0)
void TaintManager::refreshVisitor() const
{
	if (!m_visitor)
	{
		return;
	}
	int x = 0, y = 0;
	for (const Cell &c : m_cells)
	{
		m_visitor(x, y, c.kind);
		if (++x == m_width)
		{
			x = 0;
			++y;
		}
	}
}

void TaintManager::recount()
{
	m_nonDefault = (int)std::count_if(m_cells.begin(), m_cells.end(), [](const Cell &c) { return !c.isDefault(); });
}

void TaintManager::crc(StateHasher &h) const
{
	// OpenBFME's state hash (retail's light CRC, RW 0xAD50B0, sums ComputeCRC of every kind and value): the extent, then every cell not in the
	// constructor's state by index (the section exists only while one does, GameLogic::computeStateHash)
	h.addFloat(m_region.loX);
	h.addFloat(m_region.loY);
	h.addFloat(m_region.hiX);
	h.addFloat(m_region.hiY);
	h.addFloat(m_cellSize);
	h.addI32(m_width);
	h.addI32(m_height);
	h.addI32(m_nonDefault);
	for (size_t i = 0; i < m_cells.size(); ++i)
	{
		const Cell &c = m_cells[i];
		if (!c.isDefault())
		{
			h.addU32((std::uint32_t)i);
			h.addU32(c.kind);
			h.addI32(c.value);
			h.addI32(c.owner);
		}
	}
}

// RW 0xAD4B80 (the manager: version (1, 1)) -> RW 0xAD50B0 (the grid) -> RW 0xAD57E0 (a cell)
void TaintManager::xfer(Xfer &x, Staged *staged)
{
	const bool loading = x.isLoading();
	if (loading && !staged)
	{
		throw XferException("taint manager: a load needs a staging grid");
	}
	XferVersion manager{ 1, 1 };
	x.xferVersion(manager);
	XferVersion grid{ 1, 1 };
	x.xferVersion(grid);
	Staged s;
	if (!loading)
	{
		s.region = m_region;
		s.cellSize = m_cellSize;
		s.cellSizeInv = m_cellSizeInv;
		s.width = m_width;
		s.height = m_height;
	}
	x.xferReal(&s.region.loX); // Region3D: lo (x, y, z), hi (x, y, z)
	x.xferReal(&s.region.loY);
	x.xferReal(&s.region.loZ);
	x.xferReal(&s.region.hiX);
	x.xferReal(&s.region.hiY);
	x.xferReal(&s.region.hiZ);
	x.xferReal(&s.cellSize);
	x.xferReal(&s.cellSizeInv);
	x.xferInt(&s.width);
	x.xferInt(&s.height);
	std::uint32_t checksum = 0; // RW 0xAD50B0: written as 0, not checked by a load
	x.xferUnsignedInt(&checksum);
	if (loading)
	{
		if (s.width < 1 || s.height < 1)
		{
			throw XferException("taint manager: a grid of " + std::to_string(s.width) + " x " + std::to_string(s.height) + " cells");
		}
		// OpenBFME (review r1): the grid is the map's and GameData's (RW 0x62CFEF / 0x62FCE2 size it before any object exists), so a snapshot of this game
		// has this grid's extent, cell size, inverse and dimensions; anything else is refused before a cell is read (a size or inverse that does not
		// fit its grid would make a later paint's cell radius overflow the raster's arithmetic)
		const auto same = [](float a, float b) { return std::memcmp(&a, &b, sizeof(float)) == 0; };
		if (!same(s.cellSize, m_cellSize) || !same(s.cellSizeInv, m_cellSizeInv) || !same(s.region.loX, m_region.loX) || !same(s.region.loY, m_region.loY) ||
		    !same(s.region.loZ, m_region.loZ) || !same(s.region.hiX, m_region.hiX) || !same(s.region.hiY, m_region.hiY) || !same(s.region.hiZ, m_region.hiZ) ||
		    s.width != m_width || s.height != m_height)
		{
			throw XferException("taint manager: the snapshot's grid (" + std::to_string(s.width) + " x " + std::to_string(s.height) + " cells of " +
			                    std::to_string(s.cellSize) + " over " + std::to_string(s.region.hiX) + " x " + std::to_string(s.region.hiY) + ") is not this game's (" +
			                    std::to_string(m_width) + " x " + std::to_string(m_height) + " cells of " + std::to_string(m_cellSize) + " over " +
			                    std::to_string(m_region.hiX) + " x " + std::to_string(m_region.hiY) + ")");
		}
		const std::uint64_t n = (std::uint64_t)s.width * (std::uint64_t)s.height;
		x.checkLoadedCount(n, 6, sizeof(Cell)); // a cell: its version byte, the kind byte, the 4-byte value (version 1)
		s.cells.resize((size_t)n);
	}
	const std::vector<Cell> &from = loading ? s.cells : m_cells;
	for (size_t i = 0; i < from.size(); ++i)
	{
		Cell c = loading ? Cell{} : m_cells[i];
		XferVersion cv{ 1, 2 };
		x.xferVersion(cv);
		x.xferUnsignedByte(&c.kind);
		x.xferInt(&c.value); // retail: 4 raw bytes (the same little-endian word)
		if (cv.current >= 2)
		{
			x.xferInt(&c.owner);
		}
		if (loading)
		{
			// the states the paint and the realloc leave: neutral (0x80, 0, -1); painted (kind's value, any owner); a kind the realloc copied (0, -1)
			const std::int32_t derived = c.kind == kNeutral ? VALUE_NONE : (c.kind > kNeutral ? VALUE_ELVEN_WOOD : VALUE_TAINT);
			const bool ok = c.value == derived ? (c.kind != kNeutral || c.owner == -1) : (c.value == VALUE_NONE && c.owner == -1);
			if (!ok)
			{
				throw XferException("taint manager: cell " + std::to_string(i) + " has kind " + std::to_string(c.kind) + ", value " + std::to_string(c.value) + " and owner " +
				                    std::to_string(c.owner));
			}
			s.cells[i] = c;
		}
	}
	if (loading)
	{
		*staged = std::move(s);
	}
}

void TaintManager::commit(Staged &&staged)
{
	m_region = staged.region;
	m_cellSize = staged.cellSize;
	m_cellSizeInv = staged.cellSizeInv;
	m_width = staged.width;
	m_height = staged.height;
	m_cells = std::move(staged.cells);
	recount();
	refreshVisitor(); // RW 0xAD5292: a load refreshes the visitor
}

void TaintManager::neutralize()
{
	for (Cell &c : m_cells)
	{
		c = Cell{};
	}
	m_nonDefault = 0;
	refreshVisitor();
}
