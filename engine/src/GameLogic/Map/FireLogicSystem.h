// OpenBFME. GPL-3.0.
//
// TheFireLogicSystem: logical fire grid, fuel consumption, spreading, decay, tree burning and extinguishing.
// TARGET FACTS:
//   * Global instance: RotWK RW 0xDE46A8 (BFME2 decomp TheTriggerManager / 0xDFEC68).
//   * Grid: 10 world units per cell (RW 0x6878A0 / 0x687015), cell coords floor(pos * 0.1f + 0.5f), centre (cx * 10 + 5, cy * 10 + 5).
//   * update: RW 0x6886E0 (BFME2 decomp FireLogicSystem.cpp 0x00287FAD). Burns 1/10th rows per frame (m_80 accumulator);
//     decay / fuel consumption; tree harvesting via TerrainTrees::burnTreeLumber (RW 0x67F4C2 -> 0x67D6E1);
//     damage to objects watching burning cells (DamageType 0x17 DAMAGE_FLAME, DeathType 3 DEATH_BURNED, amount = cell->check);
//     spreading to 4 orthogonal neighbours if neighbour burn > cell resistance;
//     fuel regrowth from terrain material on burnt cells (RW 0x6886E0).
//   * Area operations: ChangeBurnRateInArea RW 0x6878D1 / 0x687A7D, ChangeFuelInArea RW 0x686C13, ChangeBurnRateAll RW 0x687C4C.
//   * Watcher / object registration: RegisterObject RW 0x688354, UnregisterObject RW 0x68836C,
//     tree registration RW 0x686A82, unregistration RW 0x686B2E.
//   * Save/Load: xfer RW 0x688384, version (1, 2).

#pragma once

#include "Common/INIDataTypes.h"
#include "Common/Snapshot.h"
#include "Common/StateHash.h"

#include <cstdint>
#include <set>
#include <string>
#include <utility>
#include <vector>

class GameLogic;
class Object;
class ThingTemplate;
class Xfer;
class INI;

class FireLogicSystem : public Snapshot
{
public:
	struct Material
	{
		unsigned short fuel = 0;
		unsigned short maxBurnRate = 0;
		unsigned short decay = 0;
		unsigned short resistance = 0;
		std::string name;
		unsigned int color = 0;
	};

	struct WatcherNode
	{
		WatcherNode *next = nullptr;
		WatcherNode *objectNext = nullptr;
		std::uint32_t id = 0;
		unsigned short x = 0;
		unsigned short y = 0;
	};

	struct ObjectNode
	{
		ObjectNode *next = nullptr;
		std::uint32_t id = 0;
	};

	struct Cell
	{
		int m_type = 0;                 // 0 = neutral, 1 = burning, 2 = burnt out
		unsigned short m_fuel = 0;      // remaining fuel
		unsigned short m_check = 0;     // current burn rate / heat
		unsigned int m_flammability : 10; // decay per slice
		unsigned int m_resistance : 8;    // spread resistance
		unsigned int m_maxBurnRate : 12;  // max burn rate
		unsigned int m_newlyLit : 1;      // ignited this frame
		unsigned int m_burntOut : 1;      // burnt out (eligible for fuel regrowth)
		WatcherNode *m_watchers = nullptr;
		ObjectNode *m_objects = nullptr;

		Cell()
			: m_type(0), m_fuel(0), m_check(0)
			, m_flammability(0), m_resistance(0), m_maxBurnRate(0)
			, m_newlyLit(0), m_burntOut(0)
			, m_watchers(nullptr), m_objects(nullptr)
		{}
	};

	explicit FireLogicSystem(GameLogic &logic);
	~FireLogicSystem() override;

	void newMap(float loX, float loY, float hiX, float hiY);
	void reset();

	// RW 0x6886E0: 1/10th rows slice, decay, spreading, tree harvest, damage, fuel regrowth
	void update();

	// RW 0x687053: span change
	void changeBurnRate(int x0, int x1, int y, int delta, bool onlyBurning);
	// RW 0x6871EB: directional span change
	void changeBurnRate(const Coord3D &origin, const Coord3D &dir, float threshold, int x0, int x1, int y, int delta, bool onlyBurning);
	// RW 0x6878D1: disc midpoint circle
	void changeBurnRateInArea(const Coord3D &pos, float radius, int delta, bool onlyBurning);
	// RW 0x687A7D: directional disc
	void changeBurnRateInArea(const Coord3D &pos, float radius, const Coord3D &dir, float cosArc, int delta, bool onlyBurning);
	// RW 0x687C4C: whole map (e.g. Freezing Rain)
	void changeBurnRateAll(int delta);
	// RW 0x686C13: fuel disc
	void changeFuelInArea(const Coord3D &pos, float radius, int amount, int maxBurnRate, int decay, int resistance, bool onlyBurning);

	// RW 0x688354 / 0x68836C
	void registerObject(Object *obj);
	void unregisterObject(Object *obj);

	// RW 0x686A82 / 0x686B2E
	void registerTree(std::uint32_t treeId, const Coord3D &pos, const ThingTemplate *tmpl);
	void unregisterTree(std::uint32_t treeId, const Coord3D &pos);

	void changeCellToObjectFlammability(int x, int y, const ThingTemplate *tmpl);
	void resetCellToOriginalFlammability(int x, int y);

	// Queries
	bool isCellBurning(int x, int y) const;
	int getCellBurnRate(int x, int y) const;
	int getCellFuel(int x, int y) const;
	int numRows() const { return m_numRows; }
	int numCols() const { return m_numCols; }
	int cellsBurningCount() const { return (int)m_cellsOnFire.size(); }
	bool anyBurning() const { return !m_cellsOnFire.empty(); }
	const std::set<std::pair<int, int>> &cellsOnFire() const { return m_cellsOnFire; }

	// Snapshot interface
	void crc(Xfer *xfer) override;
	void xfer(Xfer *xfer) override;
	void loadPostProcess() override {}

	// Persistence & CRC
	void xfer(Xfer &x);
	void crc(StateHasher &h) const;

	// Configuration / INI
	void parseINI(INI *ini);
	void setDecayModifier(int mod) { m_decayModifier = mod; }
	int decayModifier() const { return m_decayModifier; }
	void setMaxCellsBurning(int m) { m_maxCellsBurning = m; }
	void setFuelRegenRate(int r) { m_fuelRegenRate = r; }

private:
	void clearGrid();
	void *addWatcher(std::uint32_t id, int x, int y, bool add);
	void updateWatchers(Object *obj, bool add);

	GameLogic &m_logic;
	Material m_materials[4];
	Cell *m_storage = nullptr;
	Cell **m_cells = nullptr;
	int m_numRows = 0;
	int m_numCols = 0;
	int m_80 = 0; // slice accumulator 0..9
	std::set<std::pair<int, int>> m_cellsOnFire; // keyed by (worldX, worldY)
	int m_maxCellsBurning = 0x7FFFFFFF;
	int m_fuelRegenRate = 1;
	int m_decayModifier = 0;
};

extern FireLogicSystem *TheFireLogicSystem;

