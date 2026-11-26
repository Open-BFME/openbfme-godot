// OpenBFME. GPL-3.0.
//
// TheFireLogicSystem implementation: RotWK RW 0xDE46A8 / BFME2 decomp FireLogicSystem.cpp (0x00287FAD).

#include "GameLogic/Map/FireLogicSystem.h"

#include "Common/AsciiString.h"
#include "Common/INI.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameLogic/Damage.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Map/TerrainTrees.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/ObjectGeometry.h"
#include "GameLogic/ObjectTemplateInfo.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

FireLogicSystem *TheFireLogicSystem = nullptr;

namespace
{
inline int floatToLong(float f)
{
	return (int)std::floor(f + 0.5f);
}

// RotWK 10.0 units per cell
inline int worldToCell(float coord)
{
	return (int)std::floor(coord * 0.1f + 0.5f);
}

inline int cellToWorld(int cell)
{
	return cell * 10 + 5;
}
} // namespace

FireLogicSystem::FireLogicSystem(GameLogic &logic)
	: m_logic(logic)
{
	TheFireLogicSystem = this;

	// RotWK / BFME2 defaults: TerrainCellType 0..3 (RW 0xC11750 / FireLogicSystem.ini)
	// Cell type 0: Fire resistant
	m_materials[0].fuel = 0;
	m_materials[0].maxBurnRate = 0;
	m_materials[0].decay = 0;
	m_materials[0].resistance = 0;
	m_materials[0].name = "*Fire resistant";
	m_materials[0].color = 0;

	// Cell type 1: Grass
	m_materials[1].fuel = 100;
	m_materials[1].maxBurnRate = 20;
	m_materials[1].decay = 4;
	m_materials[1].resistance = 10;
	m_materials[1].name = "Grass";
	m_materials[1].color = 0x00FF00;

	// Cell type 2: Highly flammable
	m_materials[2].fuel = 500;
	m_materials[2].maxBurnRate = 100;
	m_materials[2].decay = 1;
	m_materials[2].resistance = 2;
	m_materials[2].name = "Highly flammable";
	m_materials[2].color = 0xFF0000;

	// Cell type 3: Undefined
	m_materials[3].fuel = 500;
	m_materials[3].maxBurnRate = 500;
	m_materials[3].decay = 5;
	m_materials[3].resistance = 0;
	m_materials[3].name = "Undefined";
	m_materials[3].color = 0x0000FF;

	m_maxCellsBurning = 1000; // FireLogicSystem.ini default
	m_fuelRegenRate = 1;
	m_decayModifier = 0;
}

FireLogicSystem::~FireLogicSystem()
{
	clearGrid();
	if (TheFireLogicSystem == this)
	{
		TheFireLogicSystem = nullptr;
	}
}

void FireLogicSystem::clearGrid()
{
	if (m_storage)
	{
		for (int i = 0; i < m_numRows * m_numCols; ++i)
		{
			Cell *cell = &m_storage[i];
			WatcherNode *w = cell->m_watchers;
			while (w)
			{
				WatcherNode *next = w->next;
				Object *obj = m_logic.findObjectByID(w->id);
				if (obj)
				{
					obj->m_fireWatchList = nullptr;
					obj->m_fireWatchCount = 0;
				}
				delete w;
				w = next;
			}
			cell->m_watchers = nullptr;

			ObjectNode *o = cell->m_objects;
			while (o)
			{
				ObjectNode *next = o->next;
				delete o;
				o = next;
			}
			cell->m_objects = nullptr;
		}
		delete[] m_cells;
		delete[] m_storage;
		m_cells = nullptr;
		m_storage = nullptr;
	}
	m_numRows = 0;
	m_numCols = 0;
	m_80 = 0;
	m_cellsOnFire.clear();
}

void FireLogicSystem::reset()
{
	clearGrid();
}

// RW 0x6878A0 / BFME2 0x00287015 (FireLogicSystem::NewMap)
void FireLogicSystem::newMap(float, float, float hiX, float hiY)
{
	clearGrid();
	m_numRows = (int)std::floor(hiX * 0.1f) + 1;
	m_numCols = (int)std::floor(hiY * 0.1f) + 1;
	if (m_numRows <= 0) m_numRows = 1;
	if (m_numCols <= 0) m_numCols = 1;

	m_storage = new Cell[m_numRows * m_numCols];
	m_cells = new Cell *[m_numRows];

	for (int x = 0; x < m_numRows; ++x)
	{
		m_cells[x] = &m_storage[x * m_numCols];
		for (int y = 0; y < m_numCols; ++y)
		{
			Cell *cell = &m_cells[x][y];
			cell->m_type = 0;
			cell->m_fuel = 0;
			cell->m_check = 0;
			cell->m_flammability = 0;
			cell->m_resistance = 0;
			cell->m_maxBurnRate = 0;
			cell->m_newlyLit = 0;
			cell->m_burntOut = 0;
			cell->m_watchers = nullptr;
			cell->m_objects = nullptr;

			// Seed from material 1 (Grass) unless underwater
			float wx = (x + 0.5f) * 10.0f;
			float wy = (y + 0.5f) * 10.0f;
			if (!m_logic.terrain() || !m_logic.terrain()->isUnderwater(wx, wy, 0.0f))
			{
				const Material &mat = m_materials[1];
				cell->m_fuel = mat.fuel;
				cell->m_maxBurnRate = mat.maxBurnRate;
				cell->m_flammability = mat.decay;
				cell->m_resistance = mat.resistance;
			}
		}
	}
}

// RW 0x6886E0 (FireLogicSystem::update)
void FireLogicSystem::update()
{
	if (!m_storage || m_numRows < 1 || m_numCols < 1)
	{
		return;
	}

	// 1. Process 1/10th of rows this frame (m_80 accumulator)
	int startRow = m_80 * m_numRows / 10;
	int endRow = (m_80 + 1) * m_numRows / 10;

	for (int row = startRow; row < endRow; ++row)
	{
		int worldX = cellToWorld(row);
		Cell *rowCells = m_cells[row];
		for (int col = m_numCols - 1; col >= 0; --col)
		{
			Cell *cell = &rowCells[col];
			if (cell->m_check == 0)
			{
				continue;
			}
			int worldY = cellToWorld(col);
			int decay = (int)cell->m_flammability + m_decayModifier;

			if (cell->m_check <= decay)
			{
				if (!cell->m_newlyLit)
				{
					m_cellsOnFire.erase({ worldX, worldY });
				}
				cell->m_newlyLit = 0;
				cell->m_check = 0;
				if (cell->m_fuel > 0)
				{
					cell->m_type = 2; // burnt out
				}
			}
			else
			{
				if (cell->m_newlyLit)
				{
					cell->m_newlyLit = 0;
					if (m_cellsOnFire.size() >= (size_t)m_maxCellsBurning)
					{
						cell->m_check = 0;
						continue;
					}
					m_cellsOnFire.insert({ worldX, worldY });
				}
				cell->m_check -= (unsigned short)decay;
				int consumed = 0;
				if (cell->m_fuel <= cell->m_check)
				{
					consumed = cell->m_fuel;
					m_cellsOnFire.erase({ worldX, worldY });
					cell->m_check = 0;
					cell->m_burntOut = 1;
					if (cell->m_fuel > 0)
					{
						cell->m_type = 2; // burnt out
					}
				}
				else
				{
					consumed = cell->m_check;
					cell->m_type = 1; // burning
				}
				cell->m_fuel -= (unsigned short)consumed;

				// Tree wood consumption (RW 0x67F4C2 -> 0x67D6E1 harvestTree)
				ObjectNode *onode = cell->m_objects;
				while (onode)
				{
					ObjectNode *next = onode->next;
					m_logic.trees().burnTreeLumber(onode->id, consumed);
					onode = next;
				}
			}
		}
	}

	// 2. Fire damage to watching objects (RW 0x6886E0: damage type 0x17 DAMAGE_FLAME, death type 3 DEATH_BURNED)
	std::map<ObjectID, int> damage;
	for (const auto &p : m_cellsOnFire)
	{
		int rx = p.first / 10;
		int cy = p.second / 10;
		if (rx >= 0 && rx < m_numRows && cy >= 0 && cy < m_numCols)
		{
			Cell *c = &m_cells[rx][cy];
			if (c->m_check > 0)
			{
				for (WatcherNode *w = c->m_watchers; w; w = w->next)
				{
					damage[w->id] += c->m_check;
				}
			}
		}
	}

	for (const auto &pair : damage)
	{
		Object *obj = m_logic.findObjectByID(pair.first);
		if (obj && !obj->isEffectivelyDead())
		{
			DamageInfo info;
			info.m_input.m_damageType = DAMAGE_FLAME;
			info.m_input.m_deathType = DEATH_BURNED;
			info.m_input.m_amount = (float)pair.second;
			obj->attemptDamage(info);
		}
	}

	// 3. Spreading heat to neighbours and regrowing burnt fuel
	int spreadStart = (startRow > 0) ? startRow - 1 : 0;
	int spreadEnd = (endRow + 1 < m_numRows) ? endRow + 1 : m_numRows;

	struct PendingHeat
	{
		Cell *cell;
		unsigned short val;
	};
	std::vector<PendingHeat> pending;

	for (int row = spreadStart; row < spreadEnd; ++row)
	{
		for (int col = 0; col < m_numCols; ++col)
		{
			Cell *cell = &m_cells[row][col];
			if (cell->m_fuel == 0)
			{
				if (cell->m_burntOut)
				{
					const Material &mat = m_materials[1]; // default grass
					if (cell->m_fuel < mat.fuel)
					{
						cell->m_fuel += (unsigned short)m_fuelRegenRate;
						if (cell->m_fuel >= mat.fuel)
						{
							cell->m_fuel = mat.fuel;
							cell->m_burntOut = 0;
						}
					}
					else
					{
						cell->m_burntOut = 0;
					}
				}
			}
			else
			{
				int threshold = cell->m_resistance;
				int heat = 0;
				if (row > 0)
				{
					int n = m_cells[row - 1][col].m_check;
					if (n > threshold) heat += n - threshold;
				}
				if (row + 1 < m_numRows)
				{
					int n = m_cells[row + 1][col].m_check;
					if (n > threshold) heat += n - threshold;
				}
				if (col > 0)
				{
					int n = m_cells[row][col - 1].m_check;
					if (n > threshold) heat += n - threshold;
				}
				if (col + 1 < m_numCols)
				{
					int n = m_cells[row][col + 1].m_check;
					if (n > threshold) heat += n - threshold;
				}

				if (heat > 0)
				{
					if (cell->m_check == 0)
					{
						if (m_cellsOnFire.size() >= (size_t)m_maxCellsBurning)
						{
							continue;
						}
						cell->m_newlyLit = 0;
						m_cellsOnFire.insert({ cellToWorld(row), cellToWorld(col) });
					}
					unsigned int val = cell->m_check + heat;
					if (val > cell->m_maxBurnRate)
					{
						val = cell->m_maxBurnRate;
					}
					pending.push_back({ cell, (unsigned short)val });
				}
			}
		}
	}

	for (const auto &p : pending)
	{
		p.cell->m_check = p.val;
	}

	m_80 = (m_80 + 1) % 10;
}

// RW 0x687053 (FireLogicSystem::ChangeBurnRate)
void FireLogicSystem::changeBurnRate(int x0, int x1, int y, int delta, bool onlyBurning)
{
	if (y < 0 || y >= m_numCols || x0 >= m_numRows || x1 < 0)
	{
		return;
	}
	if (x0 < 0) x0 = 0;
	if (x1 >= m_numRows) x1 = m_numRows - 1;

	while (x0 <= x1)
	{
		Cell *cell = &m_cells[x0][y];
		if (onlyBurning && cell->m_check < 1)
		{
			++x0;
			continue;
		}

		float wx = (x0 + 0.5f) * 10.0f;
		float wy = (y + 0.5f) * 10.0f;
		if (m_logic.terrain() && m_logic.terrain()->isUnderwater(wx, wy, 0.0f))
		{
			cell->m_check = 0;
		}
		else if (delta > 0)
		{
			if (cell->m_check == 0)
			{
				cell->m_newlyLit = 1;
			}
			int burn = (int)cell->m_check + delta;
			if (burn > 0xFFFF)
			{
				burn = 0xFFFF;
			}
			cell->m_check = (unsigned short)burn;
		}
		else if (cell->m_check > 0)
		{
			if (cell->m_check <= -delta)
			{
				if (!cell->m_newlyLit)
				{
					m_cellsOnFire.erase({ cellToWorld(x0), cellToWorld(y) });
				}
				cell->m_newlyLit = 0;
				cell->m_check = 0;
			}
			else
			{
				cell->m_check = (unsigned short)(cell->m_check + delta);
			}
		}
		++x0;
	}
}

// RW 0x6871EB (FireLogicSystem::ChangeBurnRate directional)
void FireLogicSystem::changeBurnRate(const Coord3D &origin, const Coord3D &dir, float threshold, int x0, int x1, int y, int delta, bool onlyBurning)
{
	if (y < 0 || y >= m_numCols || x0 >= m_numRows || x1 < 0)
	{
		return;
	}
	if (x0 < 0) x0 = 0;
	if (x1 >= m_numRows) x1 = m_numRows - 1;

	float offX = (x0 + 0.5f) * 10.0f - origin.x;
	float offY = (y + 0.5f) * 10.0f - origin.y;

	for (; x0 <= x1; ++x0, offX += 10.0f)
	{
		Cell *cell = &m_cells[x0][y];
		if (onlyBurning && cell->m_check < 1)
		{
			continue;
		}
		float dot = offX * dir.x + offY * dir.y;
		if (dot < threshold)
		{
			continue;
		}

		float wx = (x0 + 0.5f) * 10.0f;
		float wy = (y + 0.5f) * 10.0f;
		if (m_logic.terrain() && m_logic.terrain()->isUnderwater(wx, wy, 0.0f))
		{
			cell->m_check = 0;
		}
		else if (delta > 0)
		{
			if (cell->m_check == 0)
			{
				cell->m_newlyLit = 1;
			}
			int burn = (int)cell->m_check + delta;
			if (burn > 0xFFFF)
			{
				burn = 0xFFFF;
			}
			cell->m_check = (unsigned short)burn;
		}
		else if (cell->m_check > 0)
		{
			if (cell->m_check <= -delta)
			{
				if (!cell->m_newlyLit)
				{
					m_cellsOnFire.erase({ cellToWorld(x0), cellToWorld(y) });
				}
				cell->m_newlyLit = 0;
				cell->m_check = 0;
			}
			else
			{
				cell->m_check = (unsigned short)(cell->m_check + delta);
			}
		}
	}
}

// RW 0x6878D1 (FireLogicSystem::ChangeBurnRateInArea disc)
void FireLogicSystem::changeBurnRateInArea(const Coord3D &pos, float radius, int delta, bool onlyBurning)
{
	if (radius <= 0.0f || delta == 0)
	{
		return;
	}
	int cx = worldToCell(pos.x);
	int cy = worldToCell(pos.y);
	int rad = (int)std::ceil(radius * 0.1f);
	int y = rad;
	int distance = 0;
	int err = 2 - 2 * rad;
	int bot = cx;
	int top = cx;

	while (true)
	{
		if (err + y > 0)
		{
			if (y == 0 && rad == 1)
			{
				distance++;
				bot++;
				top--;
			}
			changeBurnRate(top, bot, cy + y, delta, onlyBurning);
			if (y == 0)
			{
				break;
			}
			changeBurnRate(top, bot, cy - y, delta, onlyBurning);
			y--;
			err += 1 - 2 * y;
		}
		if (distance <= err)
		{
			continue;
		}
		distance++;
		bot++;
		top--;
		err += 2 * distance + 1;
	}
}

// RW 0x687A7D (FireLogicSystem::ChangeBurnRateInArea directional)
void FireLogicSystem::changeBurnRateInArea(const Coord3D &pos, float radius, const Coord3D &dir, float cosArc, int delta, bool onlyBurning)
{
	if (radius <= 0.0f || delta == 0)
	{
		return;
	}
	int cx = worldToCell(pos.x);
	int cy = worldToCell(pos.y);
	int rad = (int)std::ceil(radius * 0.1f);
	int y = rad;
	int distance = 0;
	int err = 2 - 2 * rad;
	int bot = cx;
	int top = cx;

	while (true)
	{
		if (err + y > 0)
		{
			if (y == 0 && rad == 1)
			{
				distance++;
				bot++;
				top--;
			}
			changeBurnRate(pos, dir, cosArc, top, bot, cy + y, delta, onlyBurning);
			if (y == 0)
			{
				break;
			}
			changeBurnRate(pos, dir, cosArc, top, bot, cy - y, delta, onlyBurning);
			y--;
			err += 1 - 2 * y;
		}
		if (distance <= err)
		{
			continue;
		}
		distance++;
		bot++;
		top--;
		err += 2 * distance + 1;
	}
}

// RW 0x687C4C (FireLogicSystem::ChangeBurnRateAll)
void FireLogicSystem::changeBurnRateAll(int delta)
{
	for (int row = 0; row < m_numRows; ++row)
	{
		for (int col = 0; col < m_numCols; ++col)
		{
			changeBurnRate(row, row, col, delta, false);
		}
	}
}

// RW 0x686C13 (FireLogicSystem::ChangeFuelInArea)
void FireLogicSystem::changeFuelInArea(const Coord3D &pos, float radius, int amount, int maxBurnRate, int decay, int resistance, bool onlyBurning)
{
	if (radius <= 0.0f || amount == 0)
	{
		return;
	}
	int cx = worldToCell(pos.x);
	int cy = worldToCell(pos.y);
	int rad = (int)std::ceil(radius * 0.1f);

	for (int y = cy - rad; y <= cy + rad; ++y)
	{
		if (y < 0 || y >= m_numCols) continue;
		int dy = y - cy;
		int maxDx = (int)std::floor(std::sqrt((float)(rad * rad - dy * dy)));
		int x0 = std::max(0, cx - maxDx);
		int x1 = std::min(m_numRows - 1, cx + maxDx);

		for (int x = x0; x <= x1; ++x)
		{
			Cell *cell = &m_cells[x][y];
			if (onlyBurning && cell->m_check < 1)
			{
				continue;
			}
			int fuel = (int)cell->m_fuel + amount;
			if (fuel < 0) fuel = 0;
			if (fuel > 0xFFFF) fuel = 0xFFFF;
			cell->m_fuel = (unsigned short)fuel;

			if (maxBurnRate > 0 && (unsigned)maxBurnRate > cell->m_maxBurnRate)
			{
				cell->m_maxBurnRate = maxBurnRate;
			}
			if (decay > 0 && (unsigned)decay < cell->m_flammability)
			{
				cell->m_flammability = decay;
			}
			if (resistance > 0 && (unsigned)resistance < cell->m_resistance)
			{
				cell->m_resistance = resistance;
			}
		}
	}
}

// RW 0x686A82 (FireLogicSystem::addObject / registerTree)
void FireLogicSystem::registerTree(std::uint32_t treeId, const Coord3D &pos, const ThingTemplate *tmpl)
{
	int x = worldToCell(pos.x);
	int y = worldToCell(pos.y);
	if (x >= 0 && x < m_numRows && y >= 0 && y < m_numCols)
	{
		ObjectNode *node = new ObjectNode;
		node->id = treeId;
		node->next = m_cells[x][y].m_objects;
		m_cells[x][y].m_objects = node;
		if (tmpl)
		{
			changeCellToObjectFlammability(x, y, tmpl);
		}
	}
}

// RW 0x686B2E (FireLogicSystem::removeObject / unregisterTree)
void FireLogicSystem::unregisterTree(std::uint32_t treeId, const Coord3D &pos)
{
	int x = worldToCell(pos.x);
	int y = worldToCell(pos.y);
	if (x >= 0 && x < m_numRows && y >= 0 && y < m_numCols)
	{
		Cell *cell = &m_cells[x][y];
		ObjectNode **cur = &cell->m_objects;
		while (*cur)
		{
			if ((*cur)->id == treeId)
			{
				ObjectNode *next = (*cur)->next;
				delete *cur;
				*cur = next;
				if (!cell->m_watchers && !cell->m_objects)
				{
					resetCellToOriginalFlammability(x, y);
				}
				break;
			}
			cur = &(*cur)->next;
		}
	}
}

void FireLogicSystem::changeCellToObjectFlammability(int x, int y, const ThingTemplate *tmpl)
{
	if (!tmpl || x < 0 || x >= m_numRows || y < 0 || y >= m_numCols)
	{
		return;
	}
	Cell *cell = &m_cells[x][y];
	const auto &flam = tmpl->flammability();
	if (flam.fuel > 0)
	{
		cell->m_fuel = flam.fuel;
		cell->m_maxBurnRate = flam.maxBurnRate;
		cell->m_flammability = flam.decay;
		cell->m_resistance = flam.resistance;
	}
}

void FireLogicSystem::resetCellToOriginalFlammability(int x, int y)
{
	if (x < 0 || x >= m_numRows || y < 0 || y >= m_numCols)
	{
		return;
	}
	Cell *cell = &m_cells[x][y];
	const Material &mat = m_materials[1]; // default grass
	if (mat.fuel < cell->m_fuel) cell->m_fuel = mat.fuel;
	if (mat.maxBurnRate < cell->m_maxBurnRate) cell->m_maxBurnRate = mat.maxBurnRate;
	if (mat.decay > cell->m_flammability) cell->m_flammability = mat.decay;
	if (mat.resistance > cell->m_resistance) cell->m_resistance = mat.resistance;
	cell->m_burntOut = 0;
}

void *FireLogicSystem::addWatcher(std::uint32_t id, int x, int y, bool add)
{
	if (!m_cells || !m_storage || x < 0 || y < 0 || x >= m_numRows || y >= m_numCols)
	{
		return nullptr;
	}
	Cell *cell = &m_cells[x][y];
	if (add)
	{
		for (WatcherNode *w = cell->m_watchers; w; w = w->next)
		{
			if (w->id == id) return nullptr;
		}
		WatcherNode *node = new WatcherNode;
		node->next = cell->m_watchers;
		node->objectNext = nullptr;
		node->x = (unsigned short)x;
		node->y = (unsigned short)y;
		node->id = id;
		cell->m_watchers = node;
		return node;
	}
	for (WatcherNode **cur = &cell->m_watchers; *cur; cur = &(*cur)->next)
	{
		if ((*cur)->id == id)
		{
			WatcherNode *next = (*cur)->next;
			delete *cur;
			*cur = next;
			if (!cell->m_watchers && !cell->m_objects)
			{
				resetCellToOriginalFlammability(x, y);
			}
			break;
		}
	}
	return nullptr;
}

// RW 0x687C85 (rva00287552)
void FireLogicSystem::updateWatchers(Object *obj, bool add)
{
	if (!obj) return;

	if (!add)
	{
		if (!m_cells || !m_storage)
		{
			obj->m_fireWatchList = nullptr;
			obj->m_fireWatchCount = -1;
			return;
		}
		WatcherNode *w = (WatcherNode *)obj->m_fireWatchList;
		while (w)
		{
			WatcherNode *next = w->objectNext;
			addWatcher(obj->getID(), w->x, w->y, false);
			w = next;
		}
		obj->m_fireWatchList = nullptr;
		obj->m_fireWatchCount = -1;
		return;
	}

	if (!m_cells || !m_storage)
	{
		return;
	}

	const ThingTemplate *tmpl = obj->getTemplate();
	if (!tmpl) return;

	static const int kStructure = ObjectTemplateInfoBuilder::kindOfIndex("STRUCTURE");

	const Coord3D &pos = *obj->getPosition();
	if (pos.x == 0.0f && pos.y == 0.0f)
	{
		obj->m_fireWatchCount = 0;
		return;
	}

	bool isStructure = kStructure >= 0 && obj->isKindOf((unsigned)kStructure);
	const std::vector<ObjectGeometry::Shape> &shapes = obj->geometryShapes();

	bool single = false;
	if (!isStructure)
	{
		single = true;
	}

	if (single || shapes.empty())
	{
		int x = worldToCell(pos.x);
		int y = worldToCell(pos.y);
		void *node = addWatcher(obj->getID(), x, y, true);
		obj->m_fireWatchList = node;
		obj->m_fireWatchCount = node ? 1 : 0;
		if (isStructure && node)
		{
			changeCellToObjectFlammability(x, y, tmpl);
		}
		return;
	}

	int count = 0;
	WatcherNode *list = nullptr;
	float angle = obj->getOrientation();
	float cosA = std::cos(angle);
	float sinA = std::sin(angle);

	for (const auto &shape : shapes)
	{
		if (!shape.active || shape.offsetZ > 10.0f)
		{
			continue;
		}
		Coord3D p = pos;
		p.x += shape.offsetX * cosA - shape.offsetY * sinA;
		p.y += shape.offsetX * sinA + shape.offsetY * cosA;

		if (shape.type == ObjectGeometry::SHAPE_BOX)
		{
			float major = shape.majorRadius;
			float minor = shape.minorRadius;
			float step = 5.0f;
			int cols = (int)std::ceil(2.0f * major / step);
			int rows = (int)std::ceil(2.0f * minor / step);
			float rowStepX = sinA * step;
			float rowStepY = -cosA * step;
			float colStepX = cosA * step;
			float colStepY = sinA * step;
			float startX = p.x - major * cosA - minor * sinA;
			float startY = minor * cosA + p.y - major * sinA;

			for (int r = 0; r < rows; ++r)
			{
				float sx = startX + r * rowStepX;
				float sy = startY + r * rowStepY;
				for (int c = 0; c < cols; ++c, sx += colStepX, sy += colStepY)
				{
					int x = worldToCell(sx);
					int y = worldToCell(sy);
					WatcherNode *node = (WatcherNode *)addWatcher(obj->getID(), x, y, true);
					if (node)
					{
						count++;
						node->objectNext = list;
						list = node;
						if (isStructure)
						{
							changeCellToObjectFlammability(x, y, tmpl);
						}
					}
				}
			}
		}
		else
		{
			// Sphere / Cylinder
			float radius = shape.majorRadius;
			int minX = worldToCell(p.x - radius);
			int maxX = worldToCell(p.x + radius);
			int minY = worldToCell(p.y - radius);
			int maxY = worldToCell(p.y + radius);
			float r2 = (radius + 4.0f) * (radius + 4.0f);

			for (int y = minY; y <= maxY; ++y)
			{
				for (int x = minX; x <= maxX; ++x)
				{
					float dx = cellToWorld(x) - p.x;
					float dy = cellToWorld(y) - p.y;
					if (dx * dx + dy * dy <= r2)
					{
						WatcherNode *node = (WatcherNode *)addWatcher(obj->getID(), x, y, true);
						if (node)
						{
							count++;
							node->objectNext = list;
							list = node;
							if (isStructure)
							{
								changeCellToObjectFlammability(x, y, tmpl);
							}
						}
					}
				}
			}
		}
	}
	obj->m_fireWatchList = list;
	obj->m_fireWatchCount = count;
}

// RW 0x688354 (FireLogicSystem::RegisterObject)
void FireLogicSystem::registerObject(Object *obj)
{
	if (obj && obj->m_fireWatchCount < 0)
	{
		updateWatchers(obj, true);
	}
}

// RW 0x68836C (FireLogicSystem::UnregisterObject)
void FireLogicSystem::unregisterObject(Object *obj)
{
	if (obj && obj->m_fireWatchCount >= 0)
	{
		updateWatchers(obj, false);
	}
}

bool FireLogicSystem::isCellBurning(int x, int y) const
{
	if (x >= 0 && x < m_numRows && y >= 0 && y < m_numCols)
	{
		return m_cells[x][y].m_check > 0;
	}
	return false;
}

int FireLogicSystem::getCellBurnRate(int x, int y) const
{
	if (x >= 0 && x < m_numRows && y >= 0 && y < m_numCols)
	{
		return m_cells[x][y].m_check;
	}
	return 0;
}

int FireLogicSystem::getCellFuel(int x, int y) const
{
	if (x >= 0 && x < m_numRows && y >= 0 && y < m_numCols)
	{
		return m_cells[x][y].m_fuel;
	}
	return 0;
}

// RW 0x688384 (FireLogicSystem::xfer)
void FireLogicSystem::crc(Xfer *xfer)
{
	if (xfer)
	{
		this->xfer(*xfer);
	}
}

void FireLogicSystem::xfer(Xfer *xfer)
{
	if (xfer)
	{
		this->xfer(*xfer);
	}
}

void FireLogicSystem::xfer(Xfer &x)
{
	XferVersion v{ 1, 2 };
	x.xferVersion(v);
	const std::uint8_t ver = v.current;

	std::int32_t rows = m_numRows;
	std::int32_t cols = m_numCols;
	x.xferInt(&rows);
	x.xferInt(&cols);
	if (rows != m_numRows || cols != m_numCols)
	{
		return;
	}

	if (ver < 2 && x.isLoading())
	{
		m_cellsOnFire.clear();
	}

	std::int32_t acc80 = m_80;
	x.xferInt(&acc80);
	if (x.isLoading())
	{
		m_80 = acc80;
	}

	for (int r = 0; r < m_numRows; ++r)
	{
		for (int c = 0; c < m_numCols; ++c)
		{
			Cell *cell = &m_cells[r][c];
			std::int32_t type = cell->m_type;
			x.xferInt(&type);
			if (x.isLoading())
			{
				cell->m_type = type;
			}
			x.xferUnsignedShort(&cell->m_fuel);
			x.xferUnsignedShort(&cell->m_check);

			std::uint16_t flam = (std::uint16_t)cell->m_flammability;
			std::uint16_t res = (std::uint16_t)cell->m_resistance;
			std::uint16_t mbr = (std::uint16_t)cell->m_maxBurnRate;
			std::uint8_t newly = (std::uint8_t)cell->m_newlyLit;
			std::uint8_t burnt = (std::uint8_t)cell->m_burntOut;

			x.xferUnsignedShort(&flam);
			x.xferUnsignedShort(&res);
			x.xferUnsignedShort(&mbr);
			x.xferUnsignedByte(&newly);
			x.xferUnsignedByte(&burnt);

			if (x.isLoading())
			{
				cell->m_flammability = flam;
				cell->m_resistance = res;
				cell->m_maxBurnRate = mbr;
				cell->m_newlyLit = newly;
				cell->m_burntOut = burnt;
				if (ver < 2 && cell->m_check > 0)
				{
					m_cellsOnFire.insert({ cellToWorld(r), cellToWorld(c) });
				}
			}
		}
	}

	if (ver >= 2)
	{
		if (x.isLoading())
		{
			m_cellsOnFire.clear();
			std::int32_t count = 0;
			x.xferInt(&count);
			for (int i = 0; i < count; ++i)
			{
				std::int32_t wx = 0, wy = 0;
				x.xferInt(&wx);
				x.xferInt(&wy);
				m_cellsOnFire.insert({ (int)wx, (int)wy });
			}
		}
		else
		{
			std::int32_t count = (std::int32_t)m_cellsOnFire.size();
			x.xferInt(&count);
			for (const auto &p : m_cellsOnFire)
			{
				std::int32_t wx = (std::int32_t)p.first;
				std::int32_t wy = (std::int32_t)p.second;
				x.xferInt(&wx);
				x.xferInt(&wy);
			}
		}
	}
}

void FireLogicSystem::crc(StateHasher &h) const
{
	h.addU32((std::uint32_t)m_80);
	h.addU32((std::uint32_t)m_cellsOnFire.size());
	for (const auto &p : m_cellsOnFire)
	{
		h.addU32((std::uint32_t)p.first);
		h.addU32((std::uint32_t)p.second);
		int rx = p.first / 10;
		int cy = p.second / 10;
		if (rx >= 0 && rx < m_numRows && cy >= 0 && cy < m_numCols)
		{
			const Cell *c = &m_cells[rx][cy];
			h.addU32((std::uint32_t)c->m_check);
			h.addU32((std::uint32_t)c->m_fuel);
		}
	}
}

void FireLogicSystem::parseINI(INI *ini)
{
	if (!ini) return;
	// Optional INI parser for FireLogicSystem.ini overrides
}

