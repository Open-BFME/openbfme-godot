// OpenBFME. GPL-3.0.
//
// LivingWorldAI: the War of the Ring strategic AI of RotWK 2.01 (WOTR2 milestone 4, S-1330), ported from the RotWK game executable (caveat S-001:
// no function of the AI's call tree, 1074 functions from the dispatcher RW 0x900AA7 twelve calls deep, calls or jumps into the image's added
// sections; indirect calls were not followed). The BFME2 1.06 decomp (byte-matched, open-bfme-2) names the parts; RotWK changed most of them and
// is followed where they differ. Retail's classes and their files (the assert / random-draw file strings):
//   LivingWorldAI                 LivingWorldAISupport\LivingWorldAI.cpp: the phase dispatcher RW 0x900AA7, SubmitOrders RW 0x9008A6, the
//                                 end-of-phase garrison executor RW 0x9006E5, ProcessRetreats RW 0x900533, GetBattleChoice RW 0x9001FB
//   LWAIWorldInformation          LivingWorldAISupport\LivingWorldAIInformation.cpp: GatherWorldInformation RW 0x9095A7 (the global RW 0xDE9F60), the
//                                 region distances RW 0x908010 (LivingWorldAIRegionDistances.cpp, the global RW 0xDE9F9C) and the queries
//   LivingWorldAIBuildingTypes    RW 0x908F18 (+ 0x1C of the AI)
//   LivingWorldAITroopProducer    LivingWorldAITroopProducer.cpp: RW 0x9A747A (+ 0x94)
//   LivingWorldAIBuilder          LivingWorldAIBuilder.cpp: FillBuildingOrders RW 0x9A63BD (+ 0x110)
//   LivingWorldAIArmyMover        LivingWorldAIArmyMover.cpp: RW 0x9A5807 (+ 0x124)
//   LivingWorldAIGarrisonMover    LivingWorldAIGarrisonMover.cpp: RW 0x9A4205 (+ 0x14C)
//   LivingWorldPathFinder         FindShortestPath RW 0x7F0C1A (the region campaign's + 0x4C)
//
// The AI object is not saved (the player's xfer RW 0x6E445E keeps its controller type and AI template name only): a load re-attaches it
// (RW 0x6E1A1E -> attach RW 0x90002D), so after a load every AI plays at level 5 and its pending orders are gone, as in retail. OpenBFME keeps
// the whole AI in this one object, owned by LivingWorldLogic and rebuilt at start and after a load. Every decision walks vectors in retail's
// orders: the multimaps' hinted inserts (STLport insert(begin(), v)) are emulated by hintedInsertPos; every random draw is the logic generator
// with retail's range, file and line.

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace LivingWorld
{

class LivingWorldLogic;
struct Army;
struct Battle;
struct SpawnArmy;
struct BuildingTemplate;
struct AITemplate;

// STLport 4.6 multimap insert(begin(), v) (RW 0x909490 / 0x90724A / 0x904E17): the index a new element with key k takes in `keys` (the multimap's
// keys in order). Empty: 0; k <= first key: 0 (before every equal key); one element, or the second key >= k: 1; else upper_bound.
size_t hintedInsertPos(const std::vector<int> &keys, int k);

// ---- LWAIWorldInformation (RW 0xDE9F60) ----
struct AIArmyInfo
{
	int armyId = 0;
	std::vector<std::pair<int, int>> units; // {AutoResolveUnitType, ArmySummary entry KEY}: insert_equal (RW 0x6B7DDE), stable by type
	int region = -1;                        // the army's region record (region index)
};
struct AIRegionInfo
{
	int regionId = -1, ownerId = -1, strategicValue = 0, freePlots = 0;
	std::vector<int> strengthen, upgrade, worldCP, spawnOnly, spawnPlus; // building ids, plot order (finished buildings only)
	std::vector<std::pair<int, AIArmyInfo>> armies;                     // {owner player, army}: hinted-insert order
};
struct AIPlayerInfo
{
	int treasury = 0;       // + 0x0C: ScenarioStartResources + the accumulated treasury (+ 0x318)
	int regionCount = 0;    // + 0x14
	std::vector<AIArmyInfo> named, unnamed; // + 0x1C hero armies / + 0x28 garrisons, the player's army order
};

class LWAIWorldInformation
{
public:
	void gather(const LivingWorldLogic &logic);           // GatherWorldInformation RW 0x9095A7
	void buildDistances(const LivingWorldLogic &logic);   // RW 0x908010: once per game / load
	bool gathered = false;                                // byte + 0x00

	std::vector<AIPlayerInfo> players;                    // + 0x04, by player id (= index)
	std::vector<std::pair<int, int>> regionsByOwner;      // + 0x10: {owner, region id}, hinted-insert order
	std::vector<AIRegionInfo> regions;                    // the records, by region id (+ 0x1C's targets)
	struct DistEntry
	{
		bool present = false;                             // only enabled regions are sources
		std::vector<std::pair<int, int>> byDistance;      // {distance, region}: hinted-insert order
		std::vector<int> byRegion;                        // distance by region index, -1 absent (unreachable)
	};
	std::vector<DistEntry> distances;                     // RW 0xDE9F9C, by source region

	// the owner multimap's equal_range(owner) as region ids, in order
	std::vector<int> regionsOf(int owner) const;
	bool hasRow(int region) const { return region >= 0 && region < (int)distances.size() && distances[(size_t)region].present; }
	// RW 0x90646E RegionDistance: 9999 when either region is no source row; a pair the row lacks (unreachable) is a lookup miss
	int regionDistance(int from, int to) const;
	// the unchecked `row.byRegion.find(b)->second` reads of the helpers: a missing row or pair is a lookup miss
	int dist(int from, int to) const;
	mutable int lookupMisses = 0; // retail reads the tree's header node there (13.1 of the spec): counted, reported as S-1330, the result 9999
};

// ---- LivingWorldAIBuildingTypes (RW 0x908F18) ----
struct AISpawnRef
{
	std::string building;   // the building template
	int armyIndex = 0;      // its SpawnArmy nugget's ArmyToSpawn index (the SpawnArmy + 0x4C key's identity)
};
struct AIBuildingTypes
{
	std::string farmKey, barracksKey, castleKey, armoryKey; // + 0x04 / 0x08 / 0x0C / 0x10
	std::vector<std::string> spawnKeys;                     // + 0x14
	int farmCP = 0;                                         // + 0x20
	float castleArmor = 0.0f;                               // + 0x34
	struct Spawner
	{
		std::string building;
		std::array<std::vector<AISpawnRef>, 10> byCategory;    // + 0x3C: by AutoResolveUnitType, one copy per PlayerArmy entry
		std::vector<std::pair<int, AISpawnRef>> byChain;       // + 0x48: {combat chain definition index, spawn}, sorted by chain, stable
	};
	std::vector<Spawner> spawners;
	const Spawner *spawner(const std::string &building) const;
};

// ---- the order records (LivingWorldAI + 0x70 / 0x7C / 0x88) ----
struct AISpawnOrder
{
	int buildingId = 0;
	AISpawnRef unit;
};
struct AIBuildOrder
{
	bool destroy = false;
	int region = -1;
	std::string buildingTemplate;
	int buildingId = 0;
};
struct AIArmyOrder
{
	int armyId = 0;
	bool flag = false;
	int targetArmyId = 0; // 0: a region move
	int memberKey = 0;
	int region = -1;
};

class LivingWorldAI
{
public:
	explicit LivingWorldAI(LivingWorldLogic &logic);

	// attach RW 0x90002D for every player (the template is "DefaultAITemplate", RW 0xC7F748); builds the region distances when empty. A new game
	// then sets each level to the lobby slot's state (RW 0x6E189C: Easy 2, Medium 3, Hard 4, Brutal 5; a human 6); after a load it stays 5 (the
	// load's re-attach RW 0x6E1A1E). False + *error when the template is missing (retail does not add the player then: rule 10, a named error)
	bool attachAll(bool afterLoad, std::string *error);
	int level(int player) const { return player >= 0 && player < (int)m_ai.size() ? m_ai[(size_t)player].level : 0; }

	// phase 0, the AI players' turns in player order (RW 0x6BE20A: one step per logic update, the first AI not done; each AI's steps 0..4 and
	// SubmitOrders run back to back before the next AI's [INFERENCE: no human order lands within those 5 x AIs logic updates])
	void runOrdersPhase();
	// the end of a phase's barrier (RW 0x6BE20A at RW 0x6BE42C): every non-defeated AI's garrison executor RW 0x9006E5, player order
	void endOfPhase();
	// phase 4 (after PrepareRetreats): every non-defeated AI's dispatch with phase 4: ProcessRetreats RW 0x900533, then the gathered flag clears
	void runRetreatPhase();
	// LivingWorldAI::GetBattleChoice RW 0x9001FB for `player` in `battle`: 2 retreat, 4 none
	int battleChoice(int player, const Battle &battle);

	int lookupMisses() const { return m_info.lookupMisses; }
	const LWAIWorldInformation &worldInformation() const { return m_info; }

private:
	friend struct LivingWorldAITestAccess; // tests: the orders and the world information of one step
	struct PlayerAI
	{
		bool attached = false;
		int level = 5;                   // + 0x08
		AIBuildingTypes bt;
		float weight[10] = { 0 };        // troop producer + 0x00 (weight[9] never written: 0)
		int want[10] = { 0 };            // + 0x28
		int foreignTally[10] = { 0 };    // + 0x50
		int heroesThisTurn = 0;          // + 0x78
		int wFarm = 0, wBarracks = 0, wCastle = 0, wArmory = 0; // builder weights
		int builderBudget = 0;           // builder + 0x10
		int prefLegendary = 0, prefAttack = 0, prefDefense = 0, prefExperience = 0, prefTreasury = 0; // mover + 0x14..0x24
		int budget = 0;                  // + 0x18 (this[6]): the treasury at step 0 less the recruits
		std::vector<AISpawnOrder> spawnOrders;
		std::vector<AIBuildOrder> buildOrders;
		std::vector<AIArmyOrder> armyOrders;
	};

	bool idleGuard(int player) const;                              // RW 0x90018B
	void initBuildingTypes(int player, PlayerAI &ai);               // RW 0x908F18
	void step0(int player, PlayerAI &ai);                           // RW 0x9004A6
	void step1(int player, PlayerAI &ai);                           // RW 0x900089 -> TroopProducer RW 0x9A747A
	void step2(int player, PlayerAI &ai);                           // RW 0x9000D4 -> FillBuildingOrders RW 0x9A63BD
	void step3(int player, PlayerAI &ai);                           // RW 0x90010C -> GarrisonMover RW 0x9A4205
	void step4(int player, PlayerAI &ai);                           // RW 0x900131 -> ArmyMover RW 0x9A5807
	void submitOrders(int player, PlayerAI &ai);                    // RW 0x9008A6
	void executeGarrisonOrders(int player, PlayerAI &ai);           // RW 0x9006E5
	void processRetreats(int player);                               // RW 0x900533

	// world information helpers (RW 0x904F7A ..)
	bool allied(int player, int other) const;                       // RW 0x6E1EC4 -> 0x6E1940
	int closestEnemyHeroDistance(int player, int region) const;     // RW 0x90652D
	const AIArmyInfo *closestAlliedHero(int player, int region, int *dist) const; // RW 0x9050E3
	const AIArmyInfo *closestOwnHero(int player, int region, int *dist) const;    // RW 0x9051BC
	const AIArmyInfo *closestEnemyHero(int player, int region, int *dist) const;  // RW 0x90526A
	bool enemyHeroWithin(int player, int limit, int region) const;  // RW 0x904FBF
	int closestEnemyRegion(int player, int region, int *dist) const; // RW 0x905DA7
	int closestAlliedRegionIf(int player, int region, int *dist, const std::function<bool(int)> &accept) const; // RW 0x905EBE / 0x905FF9 / 0x906134
	int closestOwnUpgradeRegion(int player, int region, int *dist) const;          // RW 0x906263
	std::vector<int> ownHeroesWithin(int player, int limit, int region) const;     // RW 0x9082C5

	// building / spawn lookups
	const BuildingTemplate *buildingById(int id, int *region = nullptr, int *slot = nullptr) const; // RW 0x60EC2A
	const SpawnArmy *spawnOf(const AISpawnRef &ref) const;

	// troop producer (LivingWorldAITroopProducer.cpp)
	void tallyPlayerUnits(int player, int out[10]) const;           // RW 0x9A6C32
	int regionScore(int player, int region) const;                  // RW 0x9A6D13
	int tallyAround(int region, int player, bool matchOwner, int maxHops, int out[10]) const; // RW 0x9A70E3
	bool heroRecruit(PlayerAI &ai, int player, const AIRegionInfo &ri, int budget, int *spent, int level);   // RW 0x9A6F78
	bool counterRecruit(PlayerAI &ai, int player, const AIRegionInfo &ri, int budget, int *spent);           // RW 0x9A71F7
	bool picker(PlayerAI &ai, int player, const AIRegionInfo &ri, int budget, int *spent);                   // RW 0x9A6DE0
	bool hasUnitOfType(const PlayerAI &ai, int type, const std::string &building) const;                     // RW 0x904F3A
	AISpawnRef getUnitOfType(const PlayerAI &ai, int player, int type, int buildingId);                      // RW 0x9059E0
	bool hasUnitToFightType(const PlayerAI &ai, int type, const std::string &building);                      // RW 0x9058A1
	AISpawnRef getUnitToFightType(const PlayerAI &ai, int type, const std::string &building);                // RW 0x905C14
	int costOf(const AISpawnRef &unit, int player) const;
	bool cpOk(int region, const AISpawnRef &unit) const;            // RW 0x7F1354
	int unitKind(const AISpawnRef &unit) const;                     // the PlayerArmy's first entry's AutoResolveUnitType

	// builder (LivingWorldAIBuilder.cpp)
	bool canBuild(int region, const std::string &building, int player) const; // RW 0x7F31CD
	float scoreFarm(PlayerAI &ai, int player, int n, const AIRegionInfo &ri);
	float scoreBarracks(PlayerAI &ai, int player, int n, const AIRegionInfo &ri);
	float scoreArmory(PlayerAI &ai, int player, int n, const AIRegionInfo &ri);
	float scoreCastle(PlayerAI &ai, int player, int nFarm, int nCastle, const AIRegionInfo &ri);
	float randReal(float lo, float hi, int line);                   // GetGameLogicRandomValueReal RW 0x6D332C

	// army mover (LivingWorldAIArmyMover.cpp)
	void defendHomeTerritory(PlayerAI &ai, int player);             // RW 0x9A4C70
	void strategyPass(PlayerAI &ai, int player, int difficulty);    // RW 0x9A53E6
	int winTestBattle(int region, int maxDist, int player, int armyId, int difficulty); // RW 0x9A4EBE
	int pathStep(int player, int from, int to) const;               // inline in RW 0x9A53E6 / 0x9A4C70: -1 none
	std::vector<int> victoryRegions(int team) const;                // RW 0x90220B
	void defeatCapitals(int playerId, std::vector<int> &out) const; // RW 0x90216E -> 0x901AA3

	// garrison mover (LivingWorldAIGarrisonMover.cpp)
	void distributeMembers(PlayerAI &ai, int player, const AIArmyInfo &g, const std::vector<int> &cands); // RW 0x9A3FB2

	int random(int lo, int hi, const char *file, int line);

	LivingWorldLogic &m_logic;
	LWAIWorldInformation m_info;
	std::vector<PlayerAI> m_ai;
};

// LivingWorldPathFinder::FindShortestPath (RW 0x7F0C1A): A* over the region connections; `aiCost` selects the AI movement callback (vtable
// RW 0xBFA4F0: a step between regions of the same / allied owners 1.0, else 2.0, passability by the player) instead of the distance callback
// (RW 0xBFA4E0: every step 1.0, player -1). Returns the cost (-1 unreachable) and fills `path` with [from .. to].
float findShortestPath(const LivingWorldLogic &logic, bool aiCost, int player, int from, int to, std::vector<int> *path);

} // namespace LivingWorld
