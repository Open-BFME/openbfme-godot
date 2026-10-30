// OpenBFME. GPL-3.0.
//
// The skirmish AI's base builder and build request queue (lane AI-1, step 2). Spec: workspace/rebuild/specs/skirmish-ai.md section 7.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; static disassembly):
//   * AIPlayer's sub systems are identified by AIPlayer::init RW 0x8F02CC, which disables each from its SkirmishAIData flag: + 0x4 base builder
//     (DisableBaseBuilding, vtable RW 0xC7ADDC), + 0xC0 economy builder (DisableEconomyBuilding, RW 0xC79580), + 0x90 team builder (DisableTeamBuilding,
//     RW 0xC88344), + 0x38 unit builder (DisableUnitBuilding, RW 0xC88288), + 0x12C -> + 0xC science upgrade builder (DisableScienceUpgrading, RW 0xC88208),
//     + 0x108 unit upgrader (DisableUnitUpgrading, RW 0xC882A0), + 0xE4 wall builder (DisableWallBuilding, RW 0xC79558); + 0x140 the dozer manager
//     (ctor RW 0x9A1C45); + 0x130 the pending build requests, + 0x13C the requests in progress.
//   * the game phase (AISkirmishPlayer + 0x16C / + 0x170, RW 0x6C7677): seconds since the AI was made = (frame - creation) as unsigned * 0.2f (x87 at 24 bits);
//     Rush (0) until PhaseDuration_Rush, MidGame (1) until PhaseDuration_Rush + PhaseDuration_MidGame (SSE sum MidGame + Rush), then EndGame (2); + 0x170 is
//     the fraction of the phase (0 at a change, 1.0 in EndGame).
//   * base builder init RW 0x90D6BF: the start position (RW 0x90CE76: the "Player_%d_Start" waypoint of the player's slot start position + 1, z = ground
//     height; (-1, -1, -1) when none), then RW 0x90D3C7: the angle that faces the map centre (the active extent's half size minus the position, normalised
//     through Inv_Sqrt RW 0x441C56, acos of the clamped x, negated for a positive (x * 0 - y), minus PI/2, normalizeAngle RW 0x644FD0) and a new AIBase
//     (0x2C bytes, ctor RW 0x9BC7CF) initialised by RW 0x9BD03A: the side's base list (TheBaseTemplateLibrary RW 0x82F315), the choice RW 0x9BCA8E, the
//     position, the angle when the template AllowsArbirtaryRotation (else 0), the structure slots RW 0x9BCBED.
//   * the choice RW 0x9BCA8E: the current map's cache entry (none: no base); "any" allowed unless the slot's start position + 1 is listed in
//     SkirmishAIData AnyTypeTemplateDisabledSlots (RW 0x9BC734); a template qualifies when GameMapToUseOn equals the map file name (the text after the last
//     '\\' of the map path, exact compare RW 0x4065AA) or, when "any" is allowed, equals "<ANY>" (RW 0xC8A534); one with PlayerPositions must also list the
//     start position + 1; among n > 1 candidates GetGameLogicRandomValue(0, n - 1) picks (RW 0x9BCBAC, AIBase.cpp line 0xD3), n = 1 takes the only one.
//   * TheBaseTemplateLibrary's post-load RW 0x82FA98 reads "Bases\<Map>\<Map>.bse" for every AIBase and parses its CastleTemplates chunks with RW 0x82F787: each
//     entry {name ("" -> "Unnamed"), template, x, y, z, angle, v >= 4: i32 priority (stored as a float, + 4), i32 slot (+ 0x50)} goes to the AIBase whose Map
//     name key equals the chunk's name key (RW 0x82F3D5; another chunk's entries are dropped); a file that does not open throws (RW 0x82FC54).
//   * the slots RW 0x9BCBED: the rotation by the base angle (c = cos, s = sin of the CRT; matrix rows (c, -s), (s, c)) applied to each entry (RW 0xB26200
//     operation order: x' = ((0 * z) + (-s * y)) + x * c, y' = ((0 * z) + (s * x)) + c * y); slots are created up to the entry's slot number (1 based, each a
//     0x18 object RW 0x9EFDC2 with its index) and the entry (a copy, 0x60 bytes, vtable RW 0xC857A8) joins slot[slot - 1] with the base position and angle.
//   * slot update RW 0x9EFCC4 (each frame, per slot): nothing while slot index > phase; the first time, every idle entry is started (RW 0x9EFC29 -> vslot
//     0x18: RW 0x97DE12 counts the start and queues the request, RW 0x9618BA -> 0x8F0B92); once the phase passed the slot, idle entries' priorities become
//     max(effective, base) + difficulty * 75.0f (RW 0x9EFD13, x87); failed entries (state 3) are restarted at their rebuild frame; the slot is done when every
//     entry that counts (+ 0x54) has finished (state 2 or 3).
//   * the request processor (AIPlayer::update RW 0x8F0F7E ..): requests' effective priority = priority + dependents (RW 0x961D28); the queue is reordered
//     (RW 0x8F0A53: negative priorities first in their order, then equal-priority groups in descending order, each group in queue order); then, in order:
//     a request that needs a dozer gets the nearest free dozer (RW 0x9A1D31: squared 2D distance, strictly nearer replaces); the check (RW 0x961975 ->
//     the structure's vslot 0x40 RW 0x97DFCD): more than 2 enemy objects within 300 of the site -> 9; no dozer or a busy one -> 8; BuildAssistant
//     canMakeUnit (RW vtable + 0x64) not OK -> its code; RW 0x97DAFC -> 9; the legality (vtable + 0x44, options 0x85) failing -> 10; else 0. Code 0 executes
//     (the dozer's construct RW 0x97DC1A -> AIUpdate vslot 0x1F8; the request moves to "in progress" (state 1), the dozer leaves the free list RW 0x9A1BC0)
//     and ends the frame's processing; codes 2 and 8 (8 only while requests are in progress) end it without executing; 9 and 10 fail the request (state 3:
//     rebuild frame = frame + 5 * 30, RW 0x97DDC9); other codes go on to the next request.
//   * lane AI-2, the dozer manager (AIPlayer + 0x140) and the AIPlayer object events: see AIDozerManagerState, onObjectEntered (RW 0x8F069A), onObjectLeft
//     (RW 0x8F0C47 with RW 0x8F0BBC and the rebuild RW 0x90D16A), onDozerIdle (RW 0x8F0660), onStructureBuilt (RW 0x8F06FF) and updateDozerManager (RW 0x9A23A4 /
//     0x9A221E / 0x9A19E0) in AIBaseBuilder.cpp; the processor's code 8 waits only while the dozer manager lists a DOZER_FACTORY (RW 0x8F0F8F); stop S-890.
// What is ported: all of the above and (lane BACKLOG6) the fortress rebuild task (AIBase + 0x28, RW 0x9BC882). Not ported (stop S-413 / S-414): the other sub systems, the
// money reserve of a request (+ 0x21), RW 0x97DAFC (the burning terrain cell scan: no terrain fire system, S-191, so it never objects); since lane AI-2 the
// free list and the request completion follow RW's events (inferences: S-890).

#pragma once

#include "Common/INIDataTypes.h"
#include "Common/NameKeyGenerator.h"
#include "GameLogic/ObjectTypes.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class GameLogic;
class Object;
class Player;
class StateHasher;
struct AIBaseTemplate;
struct AISkirmishPlayer;

// one structure of an AI base (RW 0xC857A8 over the request base RW 0xC82960)
struct AIBuildRequest
{
	std::string templateName;      ///< + 0x0C
	std::string name;              ///< + 0x2C the entry's first string ("Unnamed" when empty)
	float priority = 0.0f;         ///< + 0x04
	float effectivePriority = 0.0f; ///< + 0x18
	ObjectID producer = INVALID_ID; ///< + 0x08 the dozer chosen for it
	int state = 0;                 ///< + 0x10: 0 waiting, 1 in progress, 2 complete, 3 failed
	ObjectID built = INVALID_ID;   ///< + 0x24 the structure the dozer made
	Coord3D offset;                ///< + 0x30 the rotated entry position (z = ground height at the offset, as RW 0x97D986 stores it)
	float angle = 0.0f;            ///< + 0x3C the entry's own angle
	Coord3D basePosition;          ///< + 0x40
	float baseAngle = 0.0f;        ///< + 0x4C
	int slot = 1;                  ///< + 0x50
	bool counts = true;            ///< + 0x54 (cleared by a failure)
	std::uint32_t rebuildFrame = 0; ///< + 0x58
	int starts = 0;                ///< + 0x5C
	int lastCheck = -1;            ///< the last check code (report only)
	bool unit = false;             ///< a unit request (vtable RW 0xC8EBD0, ctor RW 0x9ED98F) instead of a base structure
	int commandPoints = 0;         ///< unit + 0x38: the template's CommandPoints
	std::uint32_t productionID = 0; ///< the factory queue entry the request made (S-416: how it finishes)
	std::uint32_t serial = 0;      ///< port only: the request's stable identity (creation order per AI), hashed for the queue order
	bool farm = false;             ///< a farm site request (vtable RW 0xC88090, ctor RW 0x99F2DF): vslot 0x14 is RW 0x99F313 (the priority grows)
	bool inUse = false;            ///< farm + 0x64 (RW 0x99F3E5)
	int owner = -1;                ///< farm + 0x68 the AI player (index) using the site
	int failures = 0;              ///< farm + 0x6C (a failed build: the site is skipped once)
	int siteIndex = 0;             ///< farm + 0x70 the pool index
	bool requeueOnProducerDeath = true; ///< + 0x20 (1 from the base ctor RW 0x961BA4): a dozer's death sends the unfinished request back to the queue (RW 0x8F0C03)
	bool reserveMoney = false;     ///< + 0x21 set by the rebuild after a loss (RW 0x90D16A); the money reserve that reads it is not ported (S-413)
	// lane AI5 (AIUpgrade.h): an upgrade request (AIUpgrade, base vtable RW 0xC88128; the unit upgrader's RW 0xC8EC18, ctor RW 0x9EDD1C; the upgrade builder's
	// RW 0xC880E0, ctor RW 0x99F4D6) or an expansion request (AIExpansionScience, RW 0xC8EAF8, ctor RW 0x9ED687). Both keep their object in `producer` (+ 8),
	// the upgrade's or the template's name in `templateName` (+ 0xC); an expansion keeps the plot's position in `offset` (+ 0x30) and angle in `angle` (+ 0x3C)
	bool upgrade = false;          ///< an AIUpgrade
	bool expansion = false;        ///< an AIExpansionScience
	std::string buttonName;        ///< upgrade + 0x30 / expansion + 0x40: the command button (by name: TheCommandStore's final override, as the set holds it)
	int cost = 0;                  ///< upgrade + 0x2C: the upgrade's calcCostToBuild(owner, object) at the registration
	bool upgrading = false;        ///< upgrade + 0x34: handed to the queue by its builder
	bool expansionDone = false;    ///< expansion + 0x2C: started by RW 0x99FAEF (cleared when its structure leaves, RW 0x99FCAE)
	int heuristicType = -1;        ///< upgrade + 0x3C: the upgrade's SkirmishAIHeuristic (RW 0x9EDDAE / 0x99F52F; -1 none)
	int heuristicResult = 0;       ///< upgrade + 0x40: the last evaluation (RW 0x9EDD3D)
	std::uint32_t heuristicNextFrame = 0xFFFFFFFFu; ///< the FORTRESS heuristic's + 8 (RW 0x9ED351: -1)
	int heuristicFortressResult = 3;                ///< the FORTRESS heuristic's + 0xC (RW 0x9ED351: 3)
	std::vector<std::string> heuristicUnlocks;      ///< the FACTORY_UNITUNLOCK heuristic's templates (+ 8, RW 0x9ED4D2), by name

	// lane AIEXPAND-1: a plain structure request (vtable RW 0xC85750, ctor RW 0x97DA21 over the request base RW 0x961B48), the one SimpleExpansion makes
	// (RW 0x9B51E9): its site is `offset` as RW 0x97D986 stored it (vslot 0x34 = RW 0x97DA4E) at its own `angle` (vslot 0x44 = RW 0x48DA54); its check
	// RW 0x97DC99 has no enemy count; its start is RW 0x9618BA (no start count); its vslot 0x1C is RW 0x993D2E (the state only)
	bool plain = false;

	bool finished() const { return state == 2 || state == 3; } // RW 0x8EDA5E
	float bestPriority() const;                                 // RW 0x961BB9: max(effective, base)
	Coord3D sitePosition(const GameLogic &logic) const;         // RW 0x97DED8
	float siteAngle() const;                                    // RW 0x97DDB8
};

// RW 0x9EFDC2 (0x18 bytes)
struct AIBaseSlot
{
	bool done = false;          ///< + 0x00
	bool started = false;       ///< + 0x01
	bool boosted = false;       ///< + 0x02
	std::vector<std::shared_ptr<AIBuildRequest>> entries; ///< + 0x04
	int index = 0;              ///< + 0x10
};

// RW 0x9BC7CF (0x2C bytes)
struct AIBaseInstance
{
	std::vector<AIBaseSlot> slots; ///< + 0x00
	int index = 0;                 ///< + 0x0C
	const AIBaseTemplate *tmpl = nullptr; ///< + 0x10
	std::string templateMap;       ///< the chosen template's Map (hash / report)
	Coord3D position;              ///< + 0x18
	float angle = 0.0f;            ///< + 0x24
	std::shared_ptr<AIBuildRequest> fortress; ///< + 0x28 the fortress rebuild task (RW 0x9BC882; lane BACKLOG6)
};

// AIUnitBuilder (AIPlayer + 0x38, vtable RW 0xC88288, ctor RW 0x9A12A1)
struct AIUnitBuilderState
{
	bool disabled = false;                               ///< + 0x4 (DisableUnitBuilding)
	std::multimap<NameKeyType, ObjectID> factories;      ///< + 0xC: unit name key -> factory (RW 0x9A0838 inserts at the object event, RW 0x9A051D erases)
	std::map<NameKeyType, float> composition;            ///< + 0x18: unit name key -> desired percent of the army (RW 0x9A0E2B)
	std::vector<std::shared_ptr<AIBuildRequest>> requests; ///< + 0x14: the unit requests made (RW 0x9A1051 appends)
	bool recompute = true;                               ///< + 0x2C
	unsigned long long made = 0, queued = 0, executed = 0; ///< requests made, units out of the queue, requests queued at a factory
	// lane DECOMP-2, the hero path (RW 0x9A1051 -> 0x9A03A1 / 0x9A0993 / 0x9A05DB)
	std::vector<std::string> heroNames;                  ///< + 0x4C: the army's HeroBuildOrder (RW 0x9A10D8)
	int heroIndex = -1;                                  ///< + 0x38: the hero chosen to build next (-1: none)
	std::vector<int> removedHeroes;                      ///< + 0x3C: the indices of the heroes built (a lost one is built again)
	int heroCost = -1;                                   ///< + 0x48: the chosen hero's cost (calcCostToBuild)
	bool heroInProgress = false;                         ///< + 0x34: a hero request is out (cleared when a HERO request finishes, RW 0x9A0D9C)
	unsigned long long heroesMade = 0;                   ///< hero requests made (report)
};

// the economy builder (AIPlayer + 0xB4, its sub system at + 0xC0, vtable RW 0xC79574, ctor RW 0x8EF287, update RW 0x8EEF8F)
struct AIEconomyState
{
	bool disabled = false;               ///< + 0xC4 (DisableEconomyBuilding)
	int built = 0;                       ///< + 0xCC farms finished
	int inFlight = 0;                    ///< + 0xD0 farm requests out
	int lastFrame = -1;                  ///< + 0xD4 the frame of the last farm request (-1 after a failure)
	std::vector<std::shared_ptr<AIBuildRequest>> requests; ///< + 0xD8
	unsigned long long made = 0, refusedByDifficulty = 0, noSite = 0;
};

// the dozer manager (AIPlayer + 0x140, 0x14 bytes, ctor RW 0x9A1C45, update RW 0x9A23A4): + 0x0 the free dozers (AIBuildState::freeDozers), + 0x4 the
// DOZER_FACTORY structures, + 0x8 the dozer template (AISkirmishPlayer::dozerTemplate, set by RW 0x8F04F7 at AIPlayer + 0x148), + 0xC the player, + 0x10 "a dozer
// of the template was lost"
struct AIDozerManagerState
{
	std::vector<ObjectID> factories;   ///< + 0x4 (RW 0x8F06A2 adds, RW 0x9A19B0 removes)
	bool lostDozer = false;            ///< + 0x10 (RW 0x9A1C0B)
	unsigned long long queued = 0, refused = 0; ///< dozers queued at a factory by RW 0x9A19E0; queue calls the factory refused (report)
};

// lane AI5: AIUnitUpgrader (the listener at AIPlayer + 0xFC with its sub system at + 0x108, ctor RW 0x9A13DD; AIUpgrade.h)
struct AIUnitUpgraderState
{
	bool listening = false;  ///< added to the player info record's listeners by AIPlayer::init (RW 0x9A1340)
	bool disabled = false;   ///< + 0x10C (DisableUnitUpgrading)
	std::vector<std::shared_ptr<AIBuildRequest>> waiting;   ///< sub system + 0x8 (listener + 0x14)
	std::vector<std::shared_ptr<AIBuildRequest>> upgrading; ///< sub system + 0x14 (listener + 0x20)
	unsigned long long registered = 0, submitted = 0, declined = 0; ///< records made, handed to the queue, refused by ChanceForUnitsToUpgrade (report)
};

// lane AI5: AIUpgradeScienceBuilder (AIPlayer + 0x12C, ctor RW 0x99FB84: the listener with the sub system at + 0xC; AIUpgrade.h)
struct AIUpgradeScienceBuilderState
{
	bool listening = false;  ///< added to the player info record's listeners by AIPlayer::init (RW 0x8EE74A)
	bool disabled = false;   ///< + 0x10 (DisableScienceUpgrading)
	std::vector<ObjectID> pendingStructures;                ///< + 0x18: structures under construction at their registration
	std::vector<std::shared_ptr<AIBuildRequest>> upgrades;  ///< + 0x24
	std::vector<std::shared_ptr<AIBuildRequest>> expansions; ///< + 0x30
	std::uint32_t lastExpansionFrame = 0;                   ///< + 0x3C (0 from the ctor)
	unsigned long long registered = 0, upgradesSubmitted = 0, expansionsSubmitted = 0; ///< report
};

// the base builder (+ 0x4), the requests (+ 0x130 / + 0x13C) and the dozer manager's free list (+ 0x140) of one AI
struct AIBuildState
{
	bool baseBuilderDisabled = false;   ///< base builder + 4 (DisableBaseBuilding)
	bool baseBuilderInitialised = false; ///< + 0x24
	Coord3D startPosition;              ///< + 0x28
	std::vector<AIBaseInstance> bases;  ///< + 0x0C
	std::vector<std::shared_ptr<AIBuildRequest>> pending;    ///< AIPlayer + 0x130
	std::vector<std::shared_ptr<AIBuildRequest>> inProgress; ///< AIPlayer + 0x13C
	std::vector<ObjectID> freeDozers;   ///< dozer manager + 0x00 (std::list order: events append, RW 0x60011F)
	AIDozerManagerState dozerManager;   ///< lane AI-2: the rest of the dozer manager (+ 0x140)
	AIUnitBuilderState units;           ///< step 3: the unit builder (+ 0x38)
	AIEconomyState economy;             ///< step 5: the economy builder (+ 0xC0)
	AIUnitUpgraderState unitUpgrader;   ///< lane AI5: + 0xFC / + 0x108
	AIUpgradeScienceBuilderState scienceBuilder; ///< lane AI5: + 0x12C
	int phase = 0;                      ///< AISkirmishPlayer + 0x16C
	float phaseFraction = 0.0f;         ///< + 0x170
	bool ringHero = false;              ///< AIPlayer + 0x154: the player had Upgrade_RingHero at the last check (0 from the ctor, RW 0x8F084E; lane DECOMP-3)
	unsigned long long executed = 0, failed = 0, waitedForMoney = 0, waitedForDozer = 0; ///< structure executions; failures and waits of every request
	std::uint32_t nextSerial = 1;       ///< port only: the next AIBuildRequest::serial
	unsigned long long rebuilds = 0, requeuedForDozerDeath = 0; ///< lane AI-2: RW 0x90D16A restarts; RW 0x8F0C03 requests sent back after their dozer died
	unsigned long long fortressRebuilds = 0; ///< lane BACKLOG6: fortress rebuild tasks made by RW 0x9BC882 (report)
};

namespace AIBaseBuilder
{
// RW 0x8EEBAC: the farm site pool (TheGameLogic's FarmTemplate objects in object list order; RW keeps it in a global vector shared by every AI)
std::vector<std::shared_ptr<AIBuildRequest>> makeFarmSites(GameLogic &logic);
// RW 0x6C7677
void updatePhase(GameLogic &logic, AISkirmishPlayer &ai);
// RW 0x90D6BF; `mapFileName` = the current map's file name ("map mp evendim.map"); appends problems to `errors`
void init(GameLogic &logic, AISkirmishPlayer &ai, const std::string &mapFileName, const std::vector<int> &anyTypeDisabledSlots, std::vector<std::string> &errors);
// RW 0x90D3C7 (lane AIEXPAND-1, factored out of init): a new AIBase at `pos` facing the map centre (RW 0x9BC7CF, index = the number of bases), its layout
// chosen as RW 0x9BD03A -> 0x9BCA8E does (the "is an expansion" argument is unused there) and its slots made (RW 0x9BCBED); appends problems to `errors`
void addBase(GameLogic &logic, AISkirmishPlayer &ai, const Coord3D &pos, const std::string &mapFileName, const std::vector<int> &anyTypeDisabledSlots,
	std::vector<std::string> &errors);
// lane AIEXPAND-1: RW 0x9618BA(player, 0) on a plain request: state 0, then RW 0x8F0B92 queues it
void startPlainRequest(AIBuildState &s, const std::shared_ptr<AIBuildRequest> &req);
// RW 0x9618E3 -> RW 0x8F0602: the request leaves the pending and the in-progress queues
void leaveQueues(AIBuildState &s, const AIBuildRequest *req);
// RW 0x90D3C7's angle (exposed for tests)
float baseAngle(const Coord3D &pos, float extentX, float extentY);
// RW 0x9BCA8E's filter (the random pick is done by init); exposed for tests
std::vector<const AIBaseTemplate *> candidates(const std::vector<const AIBaseTemplate *> &sideBases, const std::string &mapFileName, int startPos, bool anyAllowed);
// one AIPlayer frame of the base builder and the request processor (RW 0x8F0EB4 parts)
void update(GameLogic &logic, AISkirmishPlayer &ai);
void crc(const AIBuildState &s, StateHasher &h);
// lane AI-2: the AIPlayer's object events (TheSkirmishAIManager's RW 0x6A9D9C / 0x6A99CB -> AISkirmishPlayer RW 0x6C7779 / 0x6C778A, skipped for a
// disabled AI) and the dozer / structure notifications
void onObjectEntered(GameLogic &logic, AISkirmishPlayer &ai, Object &obj);   // RW 0x8F069A
void onObjectLeft(GameLogic &logic, AISkirmishPlayer &ai, Object &obj);      // RW 0x8F0C47
void onDozerIdle(GameLogic &logic, AISkirmishPlayer &ai, Object &dozer);     // RW 0x8F0660 (DozerAIInterface vslot 0x7C, RW 0x88BF4E)
void onStructureBuilt(GameLogic &logic, AISkirmishPlayer &ai, const Object &builder, const Object &structure, bool built); // RW 0x8F06FF
// RW 0x9A21DB / 0x9A2129 / 0x9A20C4: retail's descending sort of the dozer factories (16-entry cutoff, equal keys exchanged); exposed for tests
void retailSortDozerFactories(std::vector<std::pair<float, Object *>> &entries);
// the complete state of one request (the farm site pool is hashed by the manager)
void crcRequest(const AIBuildRequest &r, StateHasher &h);
// lane AI5 (S-413): the money reserve of the player info record (record + 0xC -> + 0x14): RW 0x99E766 (money + reserve) and RW 0x99E79A (an amount back to the money)
int moneyPlusReserve(GameLogic &logic, const Player &player);
// RW 0x8F0502: the money saver (the AIPlayer update's last call; exposed for tests)
void saveMoney(GameLogic &logic, AISkirmishPlayer &ai, Player &player);
void withdrawFromReserve(GameLogic &logic, Player &player, std::uint32_t amount);
} // namespace AIBaseBuilder
