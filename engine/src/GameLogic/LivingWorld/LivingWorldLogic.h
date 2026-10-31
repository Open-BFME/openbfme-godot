// OpenBFME. GPL-3.0.
//
// LivingWorldLogic: the War of the Ring strategic game (RotWK's TheLivingWorldLogic, LivingWorldLogic.cpp).  Lane WOTR-1.
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * TheLivingWorldLogic = [RW 0xDE4950], ctor RW 0x6B9F80 (0x17C bytes), xfer RW 0x6BBD55 (version 7).  The turn phase is +0xF4 (xfer name
//     "LivingWorldTurnPhase", RW 0x6B2EFA), the previous phase +0xF8, the turn number +0xFC.
//   * Seven phases per turn, 0..6.  advancePhase RW 0x6BDF30: when canAdvance (RW 0x6B5F78) and phase < 6, phase+1 and the phase-enter handler
//     RW 0x6BDD98.  nextTurn RW 0x6BDEC7: at phase 6 the per-army turn update (RW 0x6B87E9), turn+1, phase 0, the campaign manager's new turn
//     (RW 0x7B970F) and the phase-enter handler.
//   * canAdvance (RW 0x6B5F78): phase 0 when the list at +0xCC (players still planning) is empty; phases 1 and 5 when no army is moving
//     (RW 0x6B5600 over the players); phase 2 when the battle list (+0xB0 -> +0x14) is empty; phase 3 when the counter +0x100 reached +0x104+1;
//     phase 4 when the list at +0x10C is empty or RW 0x6B3C80 allows; phase 6 always.
//   * Phase-enter (RW 0x6BDD98): leaving 1 runs the per-army arrive updates (RW 0x71B77A / 0x71AF26) and the region manager's RW 0x60E985; entering
//     2 finds the battles (region manager RW 0x60E272 / 0x60E985: flag +0x108 when one involves the local player, +0x109 another kind); entering 4
//     resolves region ownership after the battles (RW 0x6BABA8: a region whose surviving armies belong to one player changes hands, "LW:HeroKilled"
//     notices); entering 1 / 5 starts the army moves (RW 0x6B45C7).
//   * LivingWorldPlayer::addTreasure RW 0x6E1A40: the treasury is the template's ScenarioStartResources plus an accumulated amount (+0x318), and the
//     total never exceeds ScenarioMaxResources (the add is clipped).  The IncreaseTreasury nugget (vtable RW 0xC8C7DC, effect RW 0x9D3F70) adds its
//     TreasureAmount to the building's owner.
// INFERENCE (stops S-1324..S-1331, see docs/STOPS.md): which phase a given rule runs in beyond the above, the movement rule (one connected region per
// turn; a hero army two across one friendly region, the retail new-features page's description), the income timing (end of turn), the conquest rule for buildings, the command point formula, the recruit cost (the BuildCost of the army's
// entries), the victory check. WOTR2 milestone 4: the AI is retail's LivingWorldAI (LivingWorldAI.h).
// WOTR-2: the autoresolve is retail's battle (LivingWorldAutoResolveBattle.h: SimMath floats, the logic generator m_random).  Every decision walks vectors
// in index order (no unordered containers): the state hash covers all of it, the generator's seed array included.

#pragma once

#include "Common/RandomValue.h"
#include "GameLogic/LivingWorld/LivingWorldData.h"
#include "GameLogic/LivingWorld/SciencePurchasePointTracker.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

class StateHasher;
class Xfer;

class GameMessage; // WOTR2 milestone 8

namespace LivingWorld
{

struct AutoResolvePlayerSpec;
class AutoResolveBattle;
class LivingWorldAI;

enum TurnPhase
{
	PHASE_ORDERS = 0,     // players plan (moves, construction, recruitment); advances when every human ended the turn
	PHASE_MOVEMENT = 1,   // armies move along their connection
	PHASE_BATTLES = 2,    // contested regions are fought (RTS or autoresolve)
	PHASE_SETTLE = 3,     // the retail counter phase (+0x100 / +0x104)
	PHASE_OWNERSHIP = 4,  // region ownership after the battles
	PHASE_RETREAT = 5,    // retreat moves
	PHASE_END_TURN = 6,   // the end of the turn: construction, recruitment, income, victory
	PHASE_COUNT = 7
};
const char *turnPhaseName(int phase);

// The War of the Ring acceptance stops (docs/STOPS.md S-1320..S-1335): one line per stop, "[S-13xx] what is not ported / inferred"
const std::vector<std::string> &stopLines();

// Unit facts the strategic game needs from the object templates (provided by the host from TheThingFactory; tests may stub it)
struct UnitInfo
{
	bool known = false;
	int buildCost = 0;     // ThingTemplate BuildCost
	int commandPoints = 0; // ThingTemplate CommandPoints
	bool hero = false;     // KindOf HERO
	bool createAHero = false; // lane WOTR-2: KindOf CREATE_A_HERO (0xBE: canBuildUnit WB 0xD98FE0 hides it without a Create-a-Hero profile)
	int strength = 0;      // the HUD's army strength estimate (presentation: the BuildCost, a hero counts double; the AI does not read it)

	// ---- autoresolve (lane WOTR-2; the ThingTemplate rows of the field table RW 0xDA3DF8) ----
	int autoResolveUnitType = 9; // AutoResolveUnitType +0x5D4, default AutoResolveUnit_INVALID (RW 0x73FF94)
	std::string autoResolveBody, autoResolveCombatChain, autoResolveLeadership; // +0x4A0 / +0x4A8 / +0x4A4 names ("" = none: the default template)
	// AutoResolveArmor / AutoResolveWeapon sub-blocks (+0x3EC / +0x3F8, 0x124-byte entries; table RW 0xC80638): the first entry whose RequiredUpgrades
	// the unit has all of and whose ExcludedUpgrades it has none of (RW 0x73BE09) is the current one (RW 0x73C1E5 / 0x73C222), none: the default
	struct AutoResolveSet
	{
		std::string name;
		std::vector<std::string> requiredUpgrades, excludedUpgrades;
	};
	std::vector<AutoResolveSet> autoResolveArmor, autoResolveWeapon;
	// the ExperienceLevel chain of the template (ExperienceLevelSystem::nextLevel from no level, single player): rank, RequiredExperience, ExperienceAward
	struct Level
	{
		int rank = 0;
		int requiredExperience = 0;
		int experienceAward = 0;
	};
	std::vector<Level> levels;
	// a horde's HordeContain InitialPayload {member template, count} (its experience value is its members')
	bool horde = false;
	std::vector<std::pair<std::string, int>> hordePayload;
	// WOTR2 milestone 5: what the UpgradeTroops nugget reads (spec K): WorldMapArmoryUpgradesAllowed (+0x404, the armory set) and the LevelUpUpgrade
	// behaviour modules in module order (TriggeredBy +0x08, ConflictsWith +0x98, LevelsToGain +0x138, LevelCap +0x13C)
	std::vector<std::string> armoryUpgrades;
	struct LevelUp
	{
		std::vector<std::string> triggeredBy, conflictsWith;
		int levelsToGain = 0, levelCap = 0;
	};
	std::vector<LevelUp> levelUps;
	// the rank reached with `experience` points (0 without a level chain)
	int levelForExperience(float experience) const;
	// the ExperienceAward of the level reached with `experience` (0 without one)
	int awardForExperience(float experience) const;
};
using UnitInfoSource = std::function<UnitInfo(const std::string &thingTemplate)>;

struct SetupSlot
{
	std::string name;           // the player's display name (UTF-8)
	std::string faction;        // the PlayerTemplate name (FactionMen, ...): its LivingWorldPlayerTemplate is the one with Faction = this
	bool human = false;
	int aiDifficulty = 5;       // the AI level: the lobby slot's state (Easy 2, Medium 3, Hard 4, Brutal 5; RW 0x6E189C); 5 for a player the lobby did
	                            // not set (the scenario branch of RW 0x932EB8 keeps the attach's 5)
	int team = -1;              // the lobby team (-1 none)
	int color = -1;             // the lobby colour index
	int handicap = 0;           // the displayed handicap level (LivingWorldPlayer +0x38: the AutoResolveHandicapLevel lookup)
	std::string startRegion;    // empty: the scenario's DefaultStartSpots[slot order]
};

struct GameSetup
{
	std::string campaign; // the LivingWorldCampaign with the Scenario block
	std::vector<SetupSlot> slots;
	std::uint32_t seed = 0;
	int victoryType = 0; // the Rules tab's victory type: an index into the campaign's LivingWorldVictoryType blocks (GetVictoryCondition)
	// lane WOTR-2: TheRankInfoStore's ranks 1 .. n (RW 0x6BB459: AddPlayer gives each player rank 1's SciencePurchasePointsGranted as power points,
	// nothing without rank 1; the autoresolve's SciencePurchasePointTracker reads them all, each with its campaign count)
	std::vector<SciencePurchasePointTracker::RankRow> ranks;
	// the RTS PlayerTemplates by name (PlayerTemplate + 0x154 / + 0x158: the MaxLevelSP and IntrinsicSciencePurchasePoints the tracker reads): the
	// autoresolve's tracker takes the one its Living World player template's Faction names (RW 0x6BD755 .. 0x6BD784), none when no template has
	// that name
	struct RankTemplate
	{
		int maxLevelSP = 0;
		int intrinsicSciencePurchasePoints = 0;
	};
	std::map<std::string, RankTemplate> rankTemplates;
	float resourceMultiplierLimit = 0.0f; // GameData ResourceMultiplierLimit (+ 0xE98): the HUD's resource multiplier clamp (RW 0x6E1F2F)
};

struct ArmyMember
{
	std::string thingTemplate;
	std::string spawnId;     // lane WOTR-2: a hero's summary entry + 0xD0, the ArmyToSpawn it came from (its revival entry's ID; "" none)
	int quantity = 0;        // hordes / units of this entry (each one is an army summary entry in retail)
	float experience = 0.0f; // the experience of each unit of the entry (the summary entry's tracker value; autoresolve adds to it)
	// WOTR2 milestone 4: the ArmySummary entry's key (the summary's + 0x40 pairs {key, entry}: ArmySummary::AddEntry RW 0x811951 hands out the
	// summary counter + 0x3C, 1 after the constructor RW 0x810FBE, so an army's keys rise in the order its entries were added). The AI reads the
	// keys, not the counts, in its unit tally (RW 0x9A6C32 via the world information RW 0x9095A7)
	int key = 0;
	int homeArmy = 0; // the entry's + 0xC8: the army it was lent from by a member move (MoveArmyMember RW 0x6B47DC), 0 none
	// WOTR2 milestone 5 (UpgradeTroops, spec K): the entry's applied upgrade set (+0x10, sorted names), its rank (+0x0C: written by
	// ApplyWorldMapUpgrades RW 0x80F0D8 only) and its upgrade mark (+0xCC: the marking building's id, 0 none)
	std::vector<std::string> upgrades;
	int rank = 0;
	int upgradeBuilding = 0;
};

struct Army
{
	int id = 0;
	int owner = -1;       // player index
	int region = -1;      // region index
	int destination = -1; // ordered move target (region index), -1 none
	int movedFrom = -1;   // the region left this turn (a retreat goes back there)
	std::string scriptingName, playerArmy, heroTemplate, icon, banner;
	std::vector<ArmyMember> members;
	bool garrison = false; // no hero: a garrison army
	bool retreating = false; // lost a battle: `destination` is its retreat (moved in phase 5; RW LivingWorldLogic::PrepareRetreats)
	int nextKey = 1;         // WOTR2 milestone 4: the ArmySummary's entry key counter (+ 0x3C)
};

struct Building
{
	std::string templateName;
	int turnsLeft = 0; // 0: complete
	int slot = 0;      // the region's BuildingSpot index
	int turnsSpent = 0; // lane WOTR-2: its SpawnArmy nugget's + 0x2C, the queue head's turns of production (WB 0x1335A10 / RW 0x8FF849)
	int id = 0;         // WOTR2 milestone 4: the building's id (+ 0x18): ++TheLivingWorldLogic + 0xA0 when its plot constructs it (RW 0x902B96 / 0x902C67)
	bool cpApplied = false; // its IncreaseCommandPoints nugget instance's + 0xC: applied on completion (RW 0x9D3829), taken back by its destructor (RW 0x9D3881)
	int upgradesUsed = 0;   // WOTR2 milestone 5: its UpgradeTroops nugget instance's + 0x20 (the marks this orders phase; RW 0x9D3C16 / 0x9D3C2E / 0x9D3DD4)
};

// one entry of a building's SpawnArmy queue (the nugget's ID vector + 0x20; the region's list keeps each building's entries in queue order)
struct ProductionOrder
{
	int buildingSlot = 0;   // the producing building's slot
	int armyIndex = 0;      // index into the building's SpawnArmy nugget armiesToSpawn
};

struct RegionState
{
	int owner = -1;
	int ownerSinceTurn = 0; // the turn the owner took it (RW region +0x154, ControlledRegionsHeldForTurns)
	bool enabled = true;
	std::vector<Building> buildings;
	std::vector<ProductionOrder> production;
};

// lane WOTR-2: one entry of a player's buildable-hero (revival) list, LivingWorldPlayer + 0x1D4 (0xE8-byte elements built by WorldBuilder 0xDEA810)
struct RevivalUnit
{
	std::string spawnId;     // + 0xBC: the ArmyToSpawn the hero is built from (BuildingTemplate name "#" its index in the SpawnArmy nugget)
	std::string thing;       // + 0xE4: the hero's thing template (the lookup by thing, WB 0xDEA310)
	float experience = 0.0f; // + 0x08: the hero's experience (a dead hero's is kept for its return)
	bool dead = false;       // added by a death (AddRevivalUnit WB 0xDEAFB0), not by InitBuildableHeroes
};

// WOTR2 milestone 5: LivingWorldScoreKeeper (LivingWorldPlayer + 0x348; ctor RW 0x8F352E, xfer RW 0x8F3A89; spec P): what the War of the Ring score
// screen shows (TimeLine.apt type 6). The turns it records are retail's (its turn counter starts at 0: here m_turn - 1, INFERENCE [S-1331]). The wall
// clock (+0x6C / + 0x70) and the real-time battles' tactical statistics (+0x74, + 0xB4 / + 0xC0 / + 0xCC) are the host's (not this logic's state)
struct ScoreKeeper
{
	struct PerTurn // "LivingWorldScoreKeeper::PerTurnStats" (three shorts, RW 0x8F2B36)
	{
		std::int16_t territories = 0, units = 0, structures = 0;
	};
	std::vector<PerTurn> perTurn;        // + 0x28: one per collect (RW 0x8F3888)
	std::vector<int> regionUnifiedTurns; // + 0x34: a ConcurrentRegionBonus became held
	std::vector<int> fortressTurns;      // + 0x4C: a FORTRESS building completed
	std::vector<bool> rulesHeld;         // + 0x58: per ConcurrentRegionBonus at the last collect (RW 0x60EE5E's rule)
	int defeatTurn = -1;                 // + 0x78
	int lastTurn = -1;                   // + 0x7C
	int buildingsBuilt[5] = { 0, 0, 0, 0, 0 }; // + 0x80: completed buildings per Type
	int structuresLost = 0, structuresDestroyed = 0; // + 0x94 / + 0x98
	std::map<std::string, int> recruited;            // + 0x9C: units recruited on the map (entry counts)
	int unitsLostAutoResolve = 0, heroesLostAutoResolve = 0, unitsKilledAutoResolve = 0; // + 0xA8 / + 0xAC / + 0xB0
	int rtsBattlesWon = 0, rtsBattlesLost = 0;   // + 0xD8 / + 0xDC
	int autoResolveWon = 0, autoResolveLost = 0; // + 0xE0 / + 0xE4
	int territoriesConquered = 0, territoriesLost = 0; // + 0xE8 / + 0xEC
	int regionsLost = 0;                 // + 0xF0: ConcurrentRegionBonuses no longer held
	bool detached = false;               // + 0xF8: defeated, records nothing more
};

struct PlayerState
{
	std::string name;
	std::string faction, lwTemplate;
	bool human = false;
	int aiDifficulty = 0;
	int team = -1;          // the effective team (a player without a lobby team is its own team: 100 + index)
	int color = -1;
	int handicap = 0;       // +0x38
	int capital = -1;       // the start region
	int treasuryAccum = 0;  // RW +0x318
	bool defeated = false;
	bool endedTurn = false;
	// WOTR2 milestone 8 (step 3): the network war's battle vote (LivingWorldPlayer + 0x334: 0 none, 1 auto-resolve, 2 retreat, 3 real time) and the
	// battle it is for (the port's battle index; retail resets + 0x334 after each decision)
	int battleVote = 0;
	int battleVoteFor = -1;
	int lastIncome = 0;
	int powerPoints = 0;    // lane WOTR-2: LivingWorldPlayer + 0x1F0 (RW 0x619071 sets it); the HUD shows it plus + 0x2A0 (RW 0x6E185A)
	float experience = 0.0f; // lane WOTR-2: + 0x1F4, the rank skill points the autoresolve's tracker starts from (0.0: constructor RW 0x61FCA4)
	// WOTR2 milestone 7: what a fought battle's player starts with and gives back (RW 0x8125FC / ArmySummarySystem::Save RW 0x811E1F tail): its sciences
	// (+ 0x1F8, the battle player's list, RW 0x6E38F0) and its lasting PLAYER upgrades (+ 0x204, PersistsInCampaign ones, RW 0x6E281B), by name
	std::vector<std::string> sciences;
	std::vector<std::string> upgrades;
	std::vector<RevivalUnit> buildableHeroes; // lane WOTR-2: + 0x1D4, the heroes its buildings may build (InitBuildableHeroes WB 0xDEA3B0)
	// WOTR2 milestone 4: the IncreaseCommandPoints state, counters as retail keeps them (no capture or defeat moves them: the old owner keeps a
	// captured farm's bonus and a pending queue's points; a later destroy / dequeue debits the region's owner then)
	int tacticalCPFromBuildings = 0; // + 0x30C: TACTICAL IncreaseCommandPoints (AddTacticalCP RW 0x6E19FE; saved, read by nothing)
	int worldCPFromBuildings = 0;    // + 0x310: WORLD IncreaseCommandPoints (AddWorldCP RW 0x6E1A0B; signed, unclamped)
	int queuedCommandPoints = 0;     // + 0x314: OnUnitQueued RW 0x6E1970 / OnUnitDequeued RW 0x6E1986 (clamped at 0)
	ScoreKeeper score;               // WOTR2 milestone 5: + 0x348
};

// A battle found when phase 2 starts: every army in a region where two hostile players meet, or an army in a region a hostile player owns
// with a building that CanDefendTerritory
struct Battle
{
	int region = -1;
	int attacker = -1;              // the player whose army entered last [inference]
	int defender = -1;              // the region's owner, or the other army's owner
	std::vector<int> armies;        // army ids, index order
	enum Resolution
	{
		PENDING = 0,
		RTS_REQUESTED = 1,  // a human chose to fight it: the host runs the real-time battle and reports applyBattleResult
		RESOLVED = 2
	};
	int resolution = PENDING;
	int winner = -1;
	int autoResolveRounds = 0; // the rounds the autoresolve took (0: not autoresolved)
	// WOTR2 milestone 5: the battle's sides for StrategicConflictResults (LivingWorldBattle + 0x18, zeroed when phase 2 builds it): one per team in the
	// order its first player comes (the armies' owners in army order, then the defending region owner); per player the counters
	// ComputeBattleResultsForPlayersAfterAutoBattle RW 0x7F6443 writes after an autoresolve (only then: a fought or retreated battle keeps zeros)
	struct SidePlayer
	{
		int player = -1;
		int veterancy = 0, unitsLost = 0, enemyDestroyed = 0, heroesLost = 0;
	};
	struct Side
	{
		int team = -1;
		std::vector<SidePlayer> players;
	};
	std::vector<Side> sides;
	int winningSide = -1;       // + 0x38: the last side holding a player of the autoresolve's winning side
	bool resultsCounted = false; // RW 0x7F6443 ran
};

// What the real-time battle needs (RotWK's battle hand-off: the region's map, the players and the armies each brings)
struct BattleSpawn
{
	int player = -1;
	int armyId = 0;
	int memberIndex = 0;
	std::string thingTemplate;
	int quantity = 0;
	// WOTR2 milestone 6: CarryoverUnit RW 0x780172 gives every object of the entry its experience (when above the new object's) and its upgrade set
	float experience = 0.0f;
	std::vector<std::string> upgrades;
	std::string spawnId; // the entry's + 0xD0, the object's + 0x480 (RW 0x80EDF1): the survivors' entries keep it
};
struct BattleSetup
{
	int battleIndex = -1;
	int region = -1;
	std::string mapName;
	std::vector<int> players;        // participating player indices (the attacker first)
	std::vector<BattleSpawn> spawns; // every army member to create
	std::vector<std::string> defenderBuildings; // completed buildings of the region owner (their BattleThingTemplate)
	std::vector<int> defenderBuildingIds;       // WOTR2 milestone 7: their building ids (+ 0x18; the base placer tags the objects, Object + 0x484)
	unsigned startingCash = 0, startingCashWithFort = 0; // the campaign's StartingCashRTS / StartingCashRTSWithFort
	// WOTR2 milestone 6 (spec R 3, 2.4): per battle player (battle side order, then player order) what its tactical player starts with
	struct PlayerStart
	{
		int player = -1;
		int garrisonArmyId = 0;   // its garrison army in the region (RW 0x6B851C pass 1 creates an empty one): the starting units' army
		unsigned cash = 0;        // StartingCashRTSWithFort or StartingCashRTS (RW 0x6B1213), plus its ExtraStartResources (+0x2B4)
		std::vector<std::string> bonusUnits; // FreeBuilder: FactionDozerTemplateName, FreeInnUnits: FactionInnUnitTemplateName, one object each
		std::vector<RevivalUnit> heroes;     // WOTR2 milestone 7: its hero list entries no queue holds, moved to the battle (RW 0x6E42DA)
		// WOTR2 milestone 7 (RW 0x8125FC): its skill points (+ 0x1F4), the bonus purchase points (+ 0x2A0: the LegendaryBonus total), sciences, upgrades
		float skillPoints = 0.0f;
		int bonusPoints = 0;
		std::vector<std::string> sciences;
		std::vector<std::string> upgrades;
	};
	std::vector<PlayerStart> playerStarts;
	// WOTR2 milestone 7 (step 5): PrepareBattleForLoading RW 0x6B851C pass 2, per battle army: on the field at the load (state 4) or a reinforcement counting
	// down from frame 0 (state 2, arrival frame), and for a reinforcement where it walks on (getSpawnPositions RW 0x80F754's choices, in order: spawn and
	// gather waypoint names; the battle takes the first pair its map has)
	struct ArmyStart
	{
		int armyId = 0;
		int player = -1;
		int state = 4;
		int arrivalFrame = 0;
		std::vector<std::pair<std::string, std::string>> spawnPoints;
	};
	std::vector<ArmyStart> armyStarts;
};
// WOTR2 milestone 6 (spec R 5.1): ArmySummarySystem::Save RW 0x811E1F: one entry (count 1) per live KindOf ARMY_SUMMARY object of a battle player, under the
// army id the object carries (a spawned member's army; an object made in the battle carries its maker's, which the port takes to be the player's
// garrison [S-1329 inference]), with the object's experience, level and upgrades
struct BattleSurvivor
{
	int armyId = 0;
	std::string thingTemplate;
	float experience = 0.0f;
	int level = 0;
	std::vector<std::string> upgrades; // sorted
	std::string spawnId;
};
// WOTR2 milestone 7: a HERO + ARMY_SUMMARY record left in a battle player's hero list at the end (one it never bought, or a hero that died in the battle):
// UnitRevivalTracker::retrieveHeroesForArmySummary RW 0x78100E returns it to the war
struct BattleHeroRecord
{
	int player = -1;
	std::string thingTemplate;
	std::string spawnId; // + 0xBC
	int armyId = 0;      // + 0xB8
	float experience = 0.0f;
	int rank = 0;
	bool dead = false;
};
struct BattleResult
{
	int winner = -1; // GetLivingWorldTacticalVictor RW 0x6265D2 (a player index); -1 none: only the rosters change
	bool collected = false; // the survivors were read from the battle (a result never collected is refused)
	std::vector<BattleSurvivor> survivors;
	std::vector<BattleHeroRecord> heroes;   // WOTR2 milestone 7
	std::vector<int> destroyedBuildings;    // WOTR2 milestone 7: the region buildings that died in the battle (their ids)
	std::vector<int> undeployedArmies;      // WOTR2 milestone 7 (step 5): reinforcements never called in (state 2 / 3): their rosters stay (RW 0x810C95)
	// WOTR2 milestone 7: ArmySummarySystem::Save's tail RW 0x811E1F per battle player: + 0x1F0 = earned spell points (RW 0x80F49C -> RW 0x619071), + 0x1F8 =
	// its sciences (RW 0x6E38F0), + 0x204 = its PersistsInCampaign PLAYER upgrades (RW 0x6E281B), + 0x1F4 = its skill points (Player + 0x14)
	struct PlayerBack
	{
		int player = -1;
		int earnedSpellPoints = 0;
		float skillPoints = 0.0f;
		std::vector<std::string> sciences;
		std::vector<std::string> upgrades;
	};
	std::vector<PlayerBack> players;
};

// WOTR2 milestone 5: a strategic notice (the StrategicMessageBox queue LWL + 0x154, spec M). Retail pushes a notice only when its player is the local
// one (RW 0x6E18A6); this shared logic records it for its player and each client queues the local player's (the queue is not saved: RW 0x6BBD55
// does not xfer it). The texts are the client's (formatted from these names with the game text)
struct StrategicNotice
{
	enum Kind
	{
		HERO_KILLED = 0,     // PrepareRetreats RW 0x6BABA8: a lost army with nowhere to retreat (LW:HeroKilledTitle / Text with its hero's name)
		PLAYER_DEFEATED = 1, // RemovePlayer's observers RW 0x6BAEB6: LW:AllyDefeated* / LW:EnemyDefeated* for every other player (the client tests)
		ATTACK_REPELLED = 2, // CheckHeadToHeadBattles RW 0x6BD97E: one box, an item per army whose move was cancelled
		RTS_DEMOTED = 3,     // WOTR2 milestone 8 [S-1342, OpenBFME]: a network battle voted real time has more sides than its map's start positions:
		                     // auto-resolved instead (`region`: the battle's region; every player sees it)
		RTS_MAP_MISSING = 4, // WOTR2 milestone 8 [OpenBFME, an error]: a network battle voted real time whose map this install does not have: auto-resolved
		                     // on every peer (the same data everywhere: the profile identity), reported by name
		SAVE_REQUESTED = 5,  // WOTR2 milestone 8 (step 4): ProcessSaveGameCommand RW 0x629750 kind 1: `defeated` (the asker) asks to save the LAN war
		                     // (`player`: each other live human, who answers Yes / No)
		SAVE_REFUSED = 6,    // kind 2: `defeated` (the refuser) said No (`player`: the asker)
		LEAVE_VOTE = 7       // after the save (MSG 0x6B9, MSG_LW_REQUEST_ENDGAME): leave the war for now? (`player`: each live human)
	};
	int kind = HERO_KILLED;
	int player = -1;          // who sees it (PLAYER_DEFEATED: everyone but the defeated, -1)
	std::string hero;         // HERO_KILLED: the army's hero template ("" for a garrison: retail shows an empty name)
	int defeated = -1;        // PLAYER_DEFEATED: the player removed
	int region = -1;          // RTS_DEMOTED: the battle's region
	std::vector<std::pair<std::string, int>> repelled; // ATTACK_REPELLED: {hero template, the region the army stays in}
};

// WOTR2 milestone 8 (step 4): a LAN war's save, ordered on every peer at one frame (ProcessSaveGameCommand RW 0x629750 kind 5 -> RW 0x6DEF4E): each peer
// writes its own copy, under the asker's name, of the state at the end of that frame
struct NetSaveOrder
{
	std::u16string name;
	int asker = -1;
	std::uint32_t frame = 0;
};

struct StrategicEvent
{
	int turn = 0;
	int phase = 0;
	std::string text; // a report line (client: the HUD's notices; tests: the flow)
};

class LivingWorldLogic
{
public:
	LivingWorldLogic(const LivingWorldStores &stores, UnitInfoSource units);
	~LivingWorldLogic();
	LivingWorldLogic(const LivingWorldLogic &) = delete;
	LivingWorldLogic &operator=(const LivingWorldLogic &) = delete;

	// Creates the campaign: players from the slots, start regions, OwnershipSets, Act One's armies, starting buildings.  False with *error when the
	// setup does not fit the scenario (rule 10).
	bool start(const GameSetup &setup, std::string *error);

	// ---- orders (phase 0) ----
	bool orderMove(int player, int armyId, int region, std::string *error);
	// WOTR2 milestone 6: the move order's checks without the order (the HUD's cursor, RW 0x9E1500)
	bool canOrderMove(int player, int armyId, int region) const;
	bool cancelMove(int player, int armyId, std::string *error);
	// MSG_LW_BUILD_BUILDING 0x6AA (BuildBuilding RW 0x7F32A8): in plot `slot` (-1: the first free plot, as the AI's SubmitOrders RW 0x7F1779 picks)
	bool orderBuild(int player, int region, const std::string &building, std::string *error, int slot = -1);
	// MSG_LW_DISBAND_ARMY_MEMBER 0x6B1 (RW 0x6B4933 then RW 0x71AAEC): a member that is not KindOf HERO leaves the army at once, its owner paid the
	// unit's cost now (RW 0x80F018, capped by ScenarioMaxResources); the army stays, even empty
	bool disbandArmyMember(int player, int armyId, int memberKey, std::string *error);
	bool canDisbandArmyMember(int armyId, int memberKey) const; // CanDisband RW 0x6B48CE
	bool orderRecruit(int player, int region, int buildingSlot, int armyIndex, std::string *error);
	bool canRecruit(int player, int region, int buildingSlot, int armyIndex, std::string *error) const; // orderRecruit's checks, nothing changed
	// lane WOTR-2: MSG_LW_DEQUEUE_UNIT_AT_INDEX (RW 0x8FF6F8: a full refund at the current cost, index 0 resets the head's turns), MSG_LW_DEQUEUE_UNIT
	// (RW 0x8FF364: the LAST queued entry of that army) and MSG_LW_DESTROY_BUILDING (RW 0x7F32F4: a building under construction refunds its cost for
	// the player, a finished one nothing; its queue goes without a refund)
	bool orderDequeueAt(int player, int region, int buildingSlot, int queueIndex, std::string *error);
	bool orderDequeue(int player, int region, int buildingSlot, int armyIndex, std::string *error);
	bool orderDestroyBuilding(int player, int region, int buildingSlot, std::string *error);
	// RW 0x80F018: an army's cost for the player (the first entry's BuildCost, the siege / hero halving and the barracks discount)
	int recruitCost(int player, const PlayerArmy &army) const;
	int unitCost(int player, const std::string &thingTemplate) const; // RW 0x80F018's rule for one unit
	int recruitCommandPoints(const PlayerArmy &army) const; // RW 0x8E5C5C: every entry's CommandPoints x count, heroes included
	// RW 0x6E0B4A: a building's cost for the player (StrategicResourceCost less BuildingDiscount, at least 0)
	int buildingCost(int player, const BuildingTemplate &b) const;
	// RW 0x7F19D6's construction lock (region + 0x1C3): a building of the region is under construction, so none can be ordered
	bool constructionUnderway(int region) const;
	// the queued entries of the building in `buildingSlot` of `region`, in queue order (their indices into the region's production list)
	std::vector<size_t> queueOf(int region, int buildingSlot) const;
	bool endTurn(int player, std::string *error);
	// WOTR2 milestone 6: MSG_LW_DONE_PHASE 0x6A6 in phase 4 (the retreat phase): refused while one of the player's armies in the retreat list has no
	// destination (RW 0x6B6ACC -> RW 0x6B3CC6); phase 0's END PHASE is endTurn
	bool endPhase(int player, std::string *error);
	bool canEndPhase(int player) const;

	// ---- battles (phase 2) ----
	// The next battle a human must decide (index into battles()), -1 none.
	int pendingHumanBattle() const;
	bool chooseAutoResolve(int battleIndex, std::string *error);
	// WOTR2 milestone 8: the strategic command path (LivingWorldLogicMessages.cpp; RW 0x6BE6A6): an MSG_LW_* message from `sender` (a player index) runs
	// the order it names; false + error when it is malformed or the order is refused. A real-time battle vote leaves its setup for takeBattleSetup
	// Port decision (stricter than retail, coordinator 10-30): the owner and phase checks refuse an order the same way on every peer inside the frame,
	// and every refusal is counted (refusedOrders, in a network war's state hash): a desync from a check that differs between peers shows at once
	bool dispatch(const ::GameMessage &message, int sender, std::string *error);
	std::uint32_t refusedOrders() const { return m_refusedOrders; } ///< the orders dispatch refused so far (hashed in a network war, saved)
	bool takeBattleSetup(BattleSetup &out);
	int buildingIdAt(int region, int slot) const; // the building in that plot's id (+ 0x18), 0 none
	// RW LivingWorldBattle::GetResolutionOptions bits for `player`
	enum ResolutionOption
	{
		RESOLVE_AUTO = 2,
		RESOLVE_RETREAT = 4,
		RESOLVE_REAL_TIME = 8
	};
	int resolutionOptions(int battleIndex, int player) const;
	bool chooseRetreat(int battleIndex, int player, std::string *error);
	// a human's real-time choice; when an AI player of the battle retreats first and ends it (LivingWorldAI::GetBattleChoice), true with
	// setup->battleIndex -1: nothing to fight
	bool chooseRts(int battleIndex, BattleSetup *setup, std::string *error, bool votesTallied = false);
	bool applyBattleResult(int battleIndex, const BattleResult &result, std::string *error);

	// ---- WOTR2 milestone 8 (step 2): the network game (GameNetwork/StrategicSession.h) ----
	// A network war: the strategic frame clock (L + 0x100, counted once per lockstep frame: advanceFrame) and the phase timer (TheGameInfo + 0x78 seconds,
	// 0 none) are part of the game; they enter the state hash from here on (a single player war never calls it: its hash is unchanged)
	void setNetworked(int phaseTimerSeconds, int battleChoice = 0);
	// WOTR2 milestone 8 [S-1342]: the start positions of a region's battle map (its MapName), -1 unknown (no check), -2 the map is not in this install:
	// the client's map cache, the same on every peer (the profile identity covers the data). A network battle voted real time with more sides than that,
	// or on a missing map (an error, reported by name), is auto-resolved instead
	void setMapStartCount(std::function<int(const std::string &mapName)> f) { m_mapStartCount = std::move(f); }
	bool networked() const { return m_networked; }
	void advanceFrame() { ++m_frame; } // LivingWorldLogic::update RW 0x6BE50E's last step (every update)
	std::uint32_t frame() const { return m_frame; }
	bool phaseTimerEnabled() const;      // RW 0x6B359B: a network war, phase 0 and a non-zero rule
	int phaseFramesRemaining() const;    // RW 0x6B47AA (0x7FFFFFFF without a timer: RW 0x6B4779)
	int phaseSecondsRemaining() const;   // RW 0x6B47C8: (frames + 4) / 5, the HUD's seconds
	bool phaseTimerExpired() const;      // RW 0x6B5113 (UpdateTurnPhase RW 0x6BE283 then sends the local player's DONE_PHASE)
	// IsLocalPlayerAllowedToEndPhase RW 0x6B6ACC for `player`: phase 0 or 4, not defeated, not yet done (and phase 4's retreat paths, canEndPhase)
	bool mayEndPhase(int player) const;
	// MSG_LW_PLAYER_SURRENDERED 0x6B8 (RW 0x6BECB5): `player` and every player of its team leave the war (RemovePlayer RW 0x6B89D4), then the victory check
	// RW 0x6BA994; a player leaving while a real-time battle runs ends the war for everyone (QuitWar RW 0x6B94C6)
	void surrender(int player);
	// WOTR2 milestone 8 (step 4): the LAN war's save request (MSG_LW_OPENBFME_SAVE: kind 1 ask with the name, 2 refuse, 5 save; RW 0x629750) and the
	// leave-for-now vote after a save (MSG_LW_REQUEST_ENDGAME 0x6B9, unanimous). While a request is pending the phase timer does not end the phase
	// (GameLogic + 0x11D). A player who leaves clears what it held: the asker's request, a request no other human can answer, its vote
	enum SaveKind
	{
		SAVE_ASK = 1,
		SAVE_REFUSE = 2,
		SAVE_YES = 5
	};
	static constexpr size_t kSaveNameMax = 64;
	// WOTR2 milestone 8 (step 4): every peer writes its copy under the request's name, so a name is a plain file name: 1 .. 64 units, no
	// control character, none of / \ : * ? " < > | % (Godot's is_valid_filename), not "." or "..", no leading space nor trailing space or dot (OpenBFME: the name reaches no path
	// outside the Save folder on any PC). false with the reason
	static bool saveNameValid(const std::u16string &name, std::string *why);
	int saveAsker() const { return m_saveAsker; } ///< -1: no request pending
	// (Sol wotr2m8s4 follow-up 1) the serial of the last save request (its answers and the leave vote after it name it; a late one names an older serial)
	std::uint32_t saveRequestSerial() const { return m_saveRequestSerial; }
	const std::u16string &saveName() const { return m_saveName; }
	const std::vector<NetSaveOrder> &saveOrders() const { return m_saveOrders; } ///< the saves ordered so far (client output: not saved, not hashed)
	void clearSaveOrders() { m_saveOrders.clear(); }
	bool leaveVoteOpen() const { return m_leaveVoteOpen; }
	int leaveVote(int player) const { return player >= 0 && player < (int)m_leaveVotes.size() ? m_leaveVotes[(size_t)player] : -1; } ///< -1 none, 0 no, 1 yes
	bool warEndAgreed() const { return m_warEndAgreed; } ///< every live human voted to leave for now: the war ends on every peer

	// Runs the phase machine as far as it can go without input (AI orders, movement, autoresolve, ownership, end of turn), at most to the start of the
	// next turn's orders phase.  Call after every order (and once per turn when no human plays).
	void update();

	// ---- state ----
	int turn() const { return m_turn; }
	int phase() const { return m_phase; }
	bool finished() const { return m_finished; }
	int winningTeam() const { return m_winningTeam; }
	const std::vector<PlayerState> &players() const { return m_players; }
	const std::vector<RegionState> &regions() const { return m_regions; }
	const std::vector<Army> &armies() const { return m_armies; }
	const std::vector<Battle> &battles() const { return m_battles; }
	// WOTR2 milestone 5: the notices recorded so far (not saved, not in the state hash; the host takes them after each update)
	const std::vector<StrategicNotice> &notices() const { return m_notices; }
	void clearNotices() { m_notices.clear(); }
	const std::vector<StrategicEvent> &events() const { return m_events; }
	const RegionCampaign &regionCampaign() const { return *m_regionCampaign; }
	const Campaign &campaign() const { return *m_campaign; }
	int victoryType() const { return m_victoryType; } // the Rules tab's index (GetVictoryCondition)
	const LivingWorldStores &stores() const { return m_stores; }
	UnitInfo unitInfo(const std::string &thingTemplate) const; // the host's unit facts (none: an unknown unit, AutoResolveUnitType INVALID)
	const LivingWorldAI *ai() const { return m_ai.get(); }       // WOTR2 milestone 4: the strategic AI (not saved: retail re-attaches it)
	// tests: the logic generator's call log (every draw's range, file and line, in order: GameLogicRandom::enableCallLog)
	void enableRandomCallLog(bool on) { m_random.enableCallLog(on); }
	const std::vector<GameLogicRandom::Call> &randomCalls() const { return m_random.callLog(); }
	void clearRandomCalls() { m_random.clearCallLog(); }
	// the armies waiting for a retreat destination (TheLivingWorldLogic + 0x10C, army ids in order): filled by the battles, emptied in phase 4
	const std::vector<int> &retreatList() const { return m_retreatList; }

	// WOTR2 milestone 4: CanMoveArmyMember RW 0x6B9B65 / MoveArmyMember RW 0x6B4A6E: the entry with `memberKey` of army `armyId` joins `targetArmyId`
	// (the same owner, another army, the orders phase; not the army's leader; the entry's home army or, lent, the path from it within 1 (garrison)
	// or 2 (hero army) by ValidateArmyRegionEntry's cost; the target's command points take it: RW 0x71AD29)
	bool canMoveArmyMember(int armyId, int memberKey, int targetArmyId) const;
	static std::string spawnIdOf(const BuildingTemplate &b, int armyIndex); // a hero's ArmyToSpawn id: the building template name "#" the index
	bool moveArmyMember(int armyId, int memberKey, int targetArmyId);
	// MSG_LW_SWAP_ARMY_MEMBERS 0x6AF (HandleSwapArmyMembersMessage RW 0x6BB70D): the entries `keysA` of army `a` go to `b` and `keysB` of `b` to `a`
	// (a plain move: keysB empty). Refused whole when an army or a key is unknown (RW 0x6BB66E), by CanSwapArmyMembers (RW 0x6B796B: the gate, every
	// entry's own test without its room test, then the room of the army that gains: the summed command points' difference); then the entries move
	// in order (SwapArmyMembers RW 0x6B79D8)
	// roomTest false: CanMoveArmyMembersNoRoom RW 0x6B7938 (the gate and each entry's path, not the command points: the unit swapper opens on it)
	bool canSwapArmyMembers(int a, const std::vector<int> &keysA, int b, const std::vector<int> &keysB, bool roomTest = true) const;
	bool swapArmyMembers(int a, const std::vector<int> &keysA, int b, const std::vector<int> &keysB);
	// MSG_LW_CANCEL_ARMY_MEMBER_MOVE 0x6B0 (CancelArmyMemberMove RW 0x6B8C33): a lent entry goes back to its home army when CanMoveArmyMember allows
	bool cancelArmyMemberMove(int armyId, int memberKey);
	// WOTR2 milestone 5: the UpgradeTroops nugget (LivingWorldLogicUpgrades.cpp, spec K). MSG_LW_MARK_UNIT_FOR_UPGRADE 0x6BF (RW 0x6B7BAA) marks an
	// entry for the region's first armory that can take it; 0x6C0 (RW 0x6B605F) cancels the mark. Retail tests neither the phase nor the sender:
	// OpenBFME refuses a foreign or late order
	bool canUpgradeArmyMember(int armyId, int memberKey) const;
	bool orderUpgradeArmyMember(int player, int armyId, int memberKey, std::string *error = nullptr);
	bool cancelUpgradeArmyMember(int player, int armyId, int memberKey, std::string *error = nullptr);
	// GetNumUpgradeableTroopsInRegionForPlayer RW 0x6B7ADD: the region's armories' marks left / per turn when `player` owns it
	void upgradeCapacity(int player, int region, int *left, int *perTurn) const;
	bool fullyUpgraded(const ArmyMember &e) const; // RW 0x80F237
	// WOTR2 milestone 5: the palantir's region stats tray for `player` (RegionDisplay RW 0x9DAD60): [0] the plots built (under construction too) /
	// the plots, [1] the armories' marks used / per turn (GetNumUpgradeableTroopsInRegionForPlayer RW 0x6B7ADD), [2] the player's garrison's points
	// there (RW 0x7F1029) / the region's CPLimit, AllyCPLimit when an ally holds it (RW 0x7F0FE5)
	void regionStats(int player, int region, int out[3][2]) const;

	int treasury(int player) const;
	int incomePerTurn(int player) const;
	int commandPointLimit(int player) const;       // RW 0x6E1FD8
	int commandPointsUsed(int player) const;       // RW 0x6E22B3: the armies' plus the queued points (the AI's cpUsed)
	int armiesCommandPoints(int player) const;     // RW 0x6E1F6C: every army's entries, heroes included (the Palantir's "used")
	int armyCommandPoints(const Army &army) const;
	int armyMaxCommandPoints(const Army &army) const { return maxCommandPoints(army); } // RW 0x71ABFC (the unit swapper's CP line)
	int armyStrength(const Army &army) const;
	const Army *findArmy(int id) const;
	bool hostile(int a, int b) const;
	int regionCount(int player) const;
	// what a player may build in a region (building template names), in store order
	std::vector<std::string> buildableIn(int player, int region) const;
	int freeBuildingSlots(int region) const;
	// the least cost of a move from `from` to `to` for `player`'s army (retail's ValidateArmyRegionEntry path cost), -1 unreachable
	int moveCost(int player, int from, int to) const;

	std::uint32_t stateHash() const;
	void hashInto(StateHasher &h) const;

	// ---- saving (lane WOTR2 milestone 3, LivingWorldLogicXfer.cpp) ----
	// The strategic game's state as the snapshot block "obfme:LivingWorld" (GameState's stream: its header, the token, the block, the end token).
	// Retail's CHUNK_LivingWorldLogic (RW 0x6BC653 -> the shared state RW 0x6BBD55, version 7) xfers its own object graph (LivingWorldPlayer,
	// ArmySummary, LivingWorldRegion, the beacons); this model keeps the same state in other shapes, so the block is OpenBFME's ("obfme:", the
	// native dialect of docs/SAVES.md) and follows retail's order where the model has the field. Reading a retail save is SAVE-7's [S-1332].
	static constexpr const char *kSnapshotToken = "obfme:LivingWorld";
	// false + *error when the game was never started (nothing to save)
	bool saveState(std::vector<std::uint8_t> &out, std::string *error) const;
	// Restores a saved game into this logic, which must be new (never started). `data` gives what the save does not hold because the world's data
	// rebuilds it (the ranks, the RTS player templates' rank fields, ResourceMultiplierLimit: GameSetup's data parts, as `start` takes them); its
	// campaign, seed, slots and victory type are not read (the save holds them). Every name and index is checked against the stores: an unknown
	// campaign, template or building, a region count that is not the region campaign's, or an index out of range is a named error (rule 10).
	// WOTR2 milestone 6 (spec Q 1.4, 3): `battleSave` (a save inside a fought battle, retail's mode 3): the state must be in the battles phase with exactly
	// one battle fought in real time (RTS_REQUESTED: retail's L + 0xB8 / + 0xB5 == 0); a map save holds none
	bool loadState(const GameSetup &data, const std::vector<std::uint8_t> &bytes, std::string *error, bool battleSave = false);
	void xfer(Xfer &x); // the block's contents (save and load; loadState validates after it)
	// What retail does once a War of the Ring map save has loaded (GameState::doLoadGame, BFME2 0x6DD91A byte-matched; RotWK RW 0x6DEA25 the same shape):
	// a save of type 4 or 6 (the map, single player / multiplayer, RW 0x6DCFDF) is followed by InitRandom(0), so the logic generator the
	// autoresolve draws from starts again from seed 0. The host calls it after loadState and the state hash check (the hash of the saved state)
	void reseedAfterLoad();
	// WOTR2 milestone 8 (step 4, Sol wotr2m8s4 #2): a resumed LAN war (every peer, after its copy passed the state hash check): reseeded as any mode-6
	// load, and the saved session's leave-for-now vote is over (the save is written on the Yes's frame, which opens the vote; its notices are not saved)
	void resumeAfterLanLoad();
	// What a load does to the AI (the players' loadPostProcess RW 0x6E1A1E -> attach RW 0x90002D; the AI object is not saved): a new AI, every level 5,
	// no pending order, the region distances built again. loadState calls it; a never-saved game calling it at the same moment plays on as the loaded one
	bool reattachAi(std::string *error);

	// lane WOTR-2: what the strategic HUD's stats show for a player (RW 0x97E280 reads the LivingWorldPlayer): the bonus lines (+ 0x2A4 / + 0x2A8 /
	// + 0x2AC: the player bonus totals' attack, defense, experience), the resource multiplier in percent (RW 0x6E1F2F: min(the resource bonus total
	// + 0x298, -100 - (int)(ResourceMultiplierLimit * -100)); the line shows it * 0.01 + 1), the power points (RW 0x6E185A: + 0x1F0 plus the
	// legendary (power point) bonus total + 0x2A0)
	struct HudStats
	{
		int attack = 0, defense = 0, experience = 0;
		int resourceMultiplierPercent = 0;
		int powerPoints = 0;
	};
	HudStats hudStats(int player) const;

private:
	friend struct LivingWorldLogicTestAccess; // tests: a state no order can make (a damaged or hostile save's)
	friend class LivingWorldAI;               // the strategic AI submits its orders through retail's functions (LivingWorldAI.h)
	friend struct LivingWorldAITestAccess;    // tests of the AI (test_wotr4.cpp)
	void log(const std::string &text);
	bool canAdvance() const;
	bool checkMove(int player, int armyId, int region, std::string *error) const; // CanArmyMoveTo RW 0x6B57E7 and the phase gates
	void enterPhase(int previous);
	void advance();
	void doMovement();
	void findBattles();
	void autoResolve(Battle &battle);
	std::vector<AutoResolvePlayerSpec> autoResolveSpecs(const Battle &battle) const;
	std::vector<AutoResolvePlayerSpec> autoResolveSpecsFor(int region, const std::vector<int> &players, int ref, const std::vector<int> &armies,
		bool trackers) const; // SetUpAutoResolveBattle RW 0x6BD3F3
	void prepareRetreats();       // PrepareRetreats RW 0x6BABA8 (phase-enter 4)
	bool aiBattleVotes(int battleIndex, int humanRetreat);
	void retreatFrom(int battleIndex, int player); // RetreatArmiesForPlayer RW 0x7F7E9D (WorldBuilder 0x105AAC0)
	bool regionDefended(int region) const;         // the owner has a finished CanDefendTerritory building there
	void moveArmyToRegion(Army &army, int region); // MoveArmyToRegion RW 0x6B31D8: no check; the army's region itself: nothing
	int maxCommandPoints(const Army &army) const;  // RW 0x71ABFC
	void addMember(Army &army, ArmyMember member); // ArmySummary::AddEntry RW 0x811951: the next key
	void consolidateGarrisons();                   // RW 0x6B546B
	// WOTR2 milestone 5 (UpgradeTroops, LivingWorldLogicUpgrades.cpp)
	const BuildingNugget *armoryOf(const Building &b) const;      // UpgradeIface RW 0x8E4A47: a complete building's (last) UpgradeTroops nugget
	std::vector<Building *> upgradeBuildings(int region);         // RW 0x7F3F58, plot order
	bool canUpgradeWith(const Building &b, const ArmyMember &e) const; // vslot 1 RW 0x9D3E5B
	Building *findBuildingById(int id);
	void markUpgrade(ArmyMember &e, Building &b);                 // RW 0x80ED8C
	void cancelUpgrade(ArmyMember &e);                            // RW 0x80EDB8
	void applyUpgrade(ArmyMember &e) const;                       // RW 0x80F0D8
	void autoMarkUnitsForUpgrades();                              // RW 0x6B5F1C -> 0x6E4123
	void endOfOrdersUpgrades();                                   // the listeners on leaving phase 0: RW 0x71AB1D (apply) and RW 0x8E4ACD (reset)
	void onBuildingCompleted(int region, Building &b); // RW 0x8E50C8 -> the IncreaseCommandPoints nugget's vslot 6 RW 0x9D3829
	void onBuildingDestroyed(int region, Building &b); // the building's destructor: the nugget RW 0x9D3881, its SpawnArmy queue RW 0x8FFF41
	// WOTR2 milestone 7 (step 6): ConsumeGarrisonUnitsFromRTS RW 0x6BC7F0
	void consumeGarrisonUnits(int garrisonId, const Battle &b, const std::map<int, std::vector<ArmyMember>> &preBattle, const std::vector<int> &undeployed);
	void onUnitQueued(int region, const PlayerArmy *pa);   // RW 0x8FFCFF -> RW 0x6E1970 (the region's owner)
	void onUnitDequeued(int region, const PlayerArmy *pa); // RW 0x6E1986 (the region's owner; clamped at 0)
	void attachAi();
	void applyAutoResolve(const AutoResolveBattle &fight, const std::vector<AutoResolvePlayerSpec> &specs, Battle &battle, int retreatingPlayer);
	// WOTR2 milestone 5: ComputeBattleResultsForPlayersAfterAutoBattle RW 0x7F6443 (Battle::sides' counters, the winning side)
	void checkHeadToHeadBattles(); // RW 0x6BD97E (WOTR2 milestone 5)
	// WOTR2 milestone 5: the score keepers' hooks (LivingWorldLogicScore.cpp, spec P 2.2)
	int retailTurn() const { return m_turn - 1; }
	void scoreCollect(int player, int turn);          // RW 0x8F3888
	void scoreCollectAll(int turn);                   // every attached keeper (the turn listeners RW 0x8F3D4A, EndWar's RW 0x8F3D59)
	void scoreDefeated(int player);                   // RW 0x8F3CFB
	void scoreBuildingCompleted(int region, const Building &b); // RW 0x8F31EE
	void scoreBuildingDestroyed(int region, const Building &b); // RW 0x8F2A4D (the destroyer not known here: only the loss)
	void scoreAutoResolve(const AutoResolveBattle &fight, const std::vector<AutoResolvePlayerSpec> &specs, const Battle &battle); // RW 0x8F2C55
	void scoreRtsBattle(const Battle &battle);        // RW 0x8F2AA1
	void scoreRecruited(int player, const PlayerArmy &pa); // RW 0x8F323D
	void setRegionOwner(int region, int owner);       // Region SetOwner (RW 0x7F40E6) with the owner-change observers (RW 0x8F2AEE)
	void countBattleResults(const AutoResolveBattle &fight, const std::vector<AutoResolvePlayerSpec> &specs, Battle &battle);
	void prepareRetreat(int armyId);
	void doRetreats();
	struct PlayerBonuses
	{
		int resource = 0, army = 0, legendary = 0, attack = 0, defense = 0, experience = 0, fertile = 0; // LivingWorldPlayer +0x298.. (RW +0x294 struct)
		int extraStartResources = 0, buildingDiscount = 0, freeInnUnits = 0, discountedSiegeUnits = 0, discountedHeroUnits = 0, freeBuilder = 0,
		    discountedBarracksUnits = 0;
	};
	PlayerBonuses playerBonuses(int player) const;
	void resolveOwnership();
	void endOfTurn();
	void checkVictory();
	void spawnArmy(int player, const SpawnArmy &spawn, int region, const std::string &spawnId = std::string());
	void addTreasure(int player, int amount);
	bool spend(int player, int amount);
	int membersCommandPoints(const Army &army) const;       // ArmySummary::GetArmyCommandPointSize (WB 0x109D2D0): every member's CommandPoints x count
	// LivingWorldBuildingNuggetSpawnArmy::runProductionPhase (WB 0x1335A10 / RW 0x8FF849) for the building in `slot` of region `region`
	void runProduction(int region, Building &building, const BuildingNugget &nug);
	// LivingWorldLogic::spawnArmy for a produced army (WB 0xD9AA50 / RW 0x6B7229): a garrison army merges into the player's garrison in the region
	void spawnProducedArmy(int player, const SpawnArmy &spawn, int region, const std::string &spawnId);
	// lane WOTR-2: the buildable-hero list (WorldBuilder 0xDEA3B0 / 0xDEA180 / 0xDE9E20 / 0xDEA200 / 0xDEA310 / 0xDEAFB0)
	void initBuildableHeroes(int player);
	bool heroQueuedAnywhere(int player, const std::string &spawnId) const;
	void takeRevivalData(int player, const std::string &spawnId, ArmyMember &member);
	void addRevivalUnit(int player, const ArmyMember &member, float experience);
	const PlayerTemplate &playerTemplate(int player) const;
	const BuildingTemplate *buildingTemplate(const Building &b) const;
	int countBuildings(int region, int type) const;
	void removeArmy(int id);
	void removePlayer(int p); // RemovePlayer RW 0x6B89D4
	const std::vector<Condition> &victoryConditions(int kind) const; // 0 player defeat, 1 team defeat, 2 team victory

	const LivingWorldStores &m_stores;
	UnitInfoSource m_units;
	const Campaign *m_campaign = nullptr;
	const RegionCampaign *m_regionCampaign = nullptr;
	std::vector<PlayerState> m_players;
	std::vector<RegionState> m_regions;
	std::vector<Army> m_armies;
	std::vector<Battle> m_battles;
	std::vector<BattleSetup> m_pendingBattleSetups; // WOTR2 milestone 8: real-time votes the host has not taken yet (not saved: taken at once)
	std::vector<StrategicEvent> m_events;
	int m_turn = 0;
	int m_phase = PHASE_ORDERS;
	int m_nextArmyId = 1;
	int m_nextBuildingId = 1;          // WOTR2 milestone 4: ++TheLivingWorldLogic + 0xA0 (the building ids)
	std::vector<int> m_retreatList;    // + 0x10C
	std::unique_ptr<LivingWorldAI> m_ai;
	int m_settleCounter = 0;
	bool m_started = false;
	bool m_finished = false;
	std::vector<StrategicNotice> m_notices; // WOTR2 milestone 5 (not saved, not hashed)
	int m_winningTeam = -1;
	std::uint32_t m_seed = 0;
	std::vector<SciencePurchasePointTracker::RankRow> m_ranks;                 // lane WOTR-2: the setup's ranks (data: not hashed)
	std::map<std::string, GameSetup::RankTemplate> m_rankTemplates;            // the setup's RTS PlayerTemplates (data: not hashed)
	float m_resourceMultiplierLimit = 0.0f; // lane WOTR-2: GameData's ResourceMultiplierLimit from the setup (data, read only by hudStats: not hashed)
	int m_victoryType = 0;
	// WOTR2 milestone 8 (step 2): the network game's clock (hashed only when m_networked; not saved yet: the multiplayer save is step 4 [S-1336])
	bool m_networked = false;
	std::uint32_t m_frame = 0;      // L + 0x100
	std::uint32_t m_phaseStart = 0; // L + 0x104: the frame the current phase began (AdvanceTurnPhase RW 0x6BDF54, EndTurn RW 0x6BDF04)
	int m_phaseTimerSeconds = 0;    // TheGameInfo + 0x78 (StrategicPhaseTimer)
	int m_battleChoice = 0;         // TheGameInfo + 0x80 (BattleChoice: 0 auto-resolve first, 1 real time first)
	std::function<int(const std::string &)> m_mapStartCount; // [S-1342] data (not hashed)
	// WOTR2 milestone 8 (step 4): the save request (GameLogic + 0x11D) and the leave vote (hashed and saved when m_networked)
	int m_saveAsker = -1;
	std::u16string m_saveName;
	std::vector<NetSaveOrder> m_saveOrders;
	bool m_leaveVoteOpen = false;
	std::vector<int> m_leaveVotes;
	bool m_warEndAgreed = false;
	std::uint32_t m_refusedOrders = 0; // the refusals of dispatch (see dispatch)
	std::uint32_t m_saveRequestSerial = 0; // see saveRequestSerial
	bool dispatchOrder(const ::GameMessage &message, int sender, std::string *error);
	bool saveMessage(const ::GameMessage &m, int sender, std::string *error);
	bool leaveVoteMessage(const ::GameMessage &m, int sender, std::string *error);
	bool liveHuman(int player) const;
	void settleLeaveVote();
	// WOTR2 milestone 8 (step 3): 0x6A7 in a network war (RW 0x6BE9A5 .. 0x6BEC30): the vote, the tally, the retreats, the decision
	bool networkVote(int battleIndex, int player, int method, std::string *error);
	GameLogicRandom m_random{ RandomAlgorithm::ZH_CarryChain }; // the logic generator the autoresolve draws from (S-080: the algorithm the rebuild uses)
};

} // namespace LivingWorld
