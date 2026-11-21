// OpenBFME. GPL-3.0.
//
// TheSkirmishAIManager (lane AI-1, step 1): which players get a skirmish AI, how allied computer players share one AI group, and the update cadence.
// Spec: workspace/rebuild/specs/skirmish-ai.md section 4. Source priority: the RotWK binary (Open-BFME-2 has only the AIPlayer / AISkirmishPlayer
// constructors and destructors; ZH's AIPlayer is a different, older design that BFME2 replaced with GameLogic/SkirmishAI/*).
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * newGame RW 0x6AA030 is called from RW 0x6313A6 (load progress 0x61 = 97, after the starting bases RW 0x62B197). It runs only for a skirmish or
//     multiplayer game (TheGameInfo RW 0xDE892C vslots 0x48 / 0x4C) that is not a loaded save (RW 0x5FF924). It clears RW 0xDE9F04 (RW 0x8F11D2),
//     then walks ThePlayerList in index order: a player whose PlayerTemplate has PlayableSide (template + 0x151, RW 0x6AAC66) gets an AI when its
//     type (Player + 0x5C) is COMPUTER (1) or SkirmishAIData MakeAllSkirmishSidesAIControlled is set (RW 0x6AA0AE); every other playable player
//     gets only a player info record (0x44 bytes, ctor RW 0x8E46D9) in the map at manager + 0xA3C keyed by player index (Player + 0x54). Then
//     every group's players are initialised (RW 0x8EDBE1 -> RW 0x6C7581 per AI) and RW 0x6A9E48 runs.
//   * addAIPlayer RW 0x6A9EAD: the first group (vector manager + 0xA48, creation order) that accepts the player (RW 0x8EDBB0: the group has a
//     player and the new player's relationship to the group's first player, by index, is ALLIES (2)) takes it; else a new group (0x28 bytes, ctor
//     RW 0x8EDD10) is created and appended. The group makes the AI (RW 0x8EDF09: new AISkirmishPlayer 0x188 bytes, ctor RW 0x6C7C27 with the
//     player, its side's ArmyDefinition (TheArmyDefinitionManager lookup RW 0x7EE260 of Player + 0x58) and "the group was empty"), keys it by player
//     index, appends the player to the member vector (+ 0xC) and sets the group active (+ 0x24). Back in addAIPlayer: when the side has an army,
//     the dozer of the side (manager + 0xA54) is given to the AI (RW 0x8F04F7) if there is one; when the side has NO army the AI is disabled
//     (AI + 0x168 = 1). Finally the player info record is made, as for a human.
//   * update RW 0x6A96A0 (TheSkirmishAIManager vslot 0x28, called in logic phase 5 at RW 0x62EBEF after the victory conditions, among the
//     subsystems after processDestroyList): nothing until a group exists and the logic frame (TheGameLogic + 0x40) is >= 10; then every player info
//     record in player index order (RW 0x8E3CF3), then every group in creation order (RW 0x8EDDF6).
//   * group update RW 0x8EDDF6 (only while active): the first COMPUTER member (or any member when MakeAll..., RW 0x8EDACE) — none makes the group
//     inactive; its AI's brain's shared part (+ 0x164 -> + 0x14) is updated with the number of live AI members (RW 0x8EDA8E: not defeated
//     (Player + 0x754), and COMPUTER or MakeAll...); the manager's gate list (+ 0xA74) temporarily closes every enemy gate whose OPEN geometry is the one in
//     the pathfinder's map (RW 0x8ED6DC, the poll RW 0x89C12F), every AI of the group, in player index order, gets that shared part and is updated
//     (RW 0x6C7946); the group's tasks (+ 0x18) are updated (RW 0x8EDB2C); the gate list then restores those gates' OPEN geometry (RW 0x8ED726).
//   * AI update RW 0x6C7946: a disabled AI only updates its brain while its group still has more than one live AI member, or exactly one and the AI
//     has an army; an enabled AI whose player is defeated is shut down (RW 0x6C78D9 / 0x6C7928) and disabled; otherwise RW 0x6C75B5 (when
//     + 0x178 != -1), RW 0x6C7677, the AIPlayer part RW 0x8F0EB4 (its sub systems at + 0x4, + 0xE4, + 0x38, + 0x90, + 0xC0, + 0x108, + 0x12C -> + 0xC
//     each through RW 0x90CD00 "update unless disabled"), then the brain (+ 0x164, RW 0x90CD00).
// What is ported here: the creation rules, the grouping, the disabled rule, the cadence and the order of every call above, with the state each
// step owns (hashed), the gate list (+ 0xA74: every GateOpenAndCloseBehavior registers itself, RW 0x89C04B -> 0x8EDA39) and the group's task vector
// (+ 0x18: the money task RW 0x99EEF4 and its cleanup RW 0x8EDA5E). The bodies of the called components (player info, the AIPlayer sub systems, the
// brain, the shutdown) are not ported: each call is counted and
// reported (stop S-412).

#pragma once

#include "Common/NameKeyGenerator.h"
#include "GameLogic/SkirmishAI/AIBaseBuilder.h"
#include "GameLogic/SkirmishAI/AIStatCollector.h"
#include "GameLogic/SkirmishAI/AITacticalAI.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

class GameLogic;
class GateOpenAndCloseBehavior;
class Object;
class Player;
class PlayerList;
class SkirmishAIStore;
class StateHasher;
struct ArmyDefinition;

// AISkirmishPlayer (0x188 bytes, RW ctor 0x6C7C27 over the AIPlayer base RW 0x8F0783)
struct AISkirmishPlayer
{
	int playerIndex = -1;                     ///< + 0x15C the player (by index here)
	const ArmyDefinition *army = nullptr;     ///< + 0x160 the side's ArmyDefinition, null when the side has none
	std::string armySide;                     ///< the side the army was looked up by (for the hash and reports)
	bool firstOfGroup = false;                ///< the brain's ctor flag (RW 0x6C6DB1 arg 2): only the first AI of a group makes the target chooser
	bool disabled = false;                    ///< + 0x168
	float field170 = 0.0f;                    ///< + 0x170 (0.0f at creation; meaning not read)
	std::uint32_t creationFrame = 0;          ///< + 0x174 (TheGameLogic + 0x40 at creation)
	int field178 = -1;                        ///< + 0x178 (-1 at creation; selects RW 0x6C75B5 / the shutdown variant)
	std::string dozerTemplate;                ///< RW 0x8F04F7's argument: the side's AIDozerAssignment unit
	std::uint32_t shutdownFrame = 0;          ///< the frame the AI was shut down for a defeated player (0 = running)
	std::uint32_t retryTimer = 0;             ///< AIPlayer + 0x158 = LOGICFRAMES_PER_SECOND * 30 at creation (RW 0x8F0855)
	int enemyIndex = -1;                      ///< AIPlayer + 0xA0: the cached enemy player (RW 0x8F47EB), -1 when none
	std::uint32_t enemyRefreshFrame = 0;      ///< AIPlayer + 0x9C: the frame the enemy cache refreshes (RW 0x8F47EB: now + 25)
	// lane PORT-39: AIPlayer + 0x46 / + 0x78 / + 0x7C, the resource gatherer bookkeeping of RW 0x8F736E (set at the new game) and RW 0x8F858E (the
	// builder's update: counts the player's supply trucks, makes a new one while there are fewer than + 0x78)
	bool needsResourceGatherers = false;      ///< + 0x46: the AI wants gatherers (set with the counts at RW 0x8F73D2 / 0x8F7429)
	int resourceGatherersWanted = 0;          ///< + 0x78: the SideInfo count of the AI's difficulty + 1 (RW 0x8F7428)
	int resourceGatherersHave = -1;           ///< + 0x7C: the gatherers counted, -1 until the first count (RW 0x8F7424); + 1 means "one is on the way"
	bool hasSideInfo = false;                 ///< the AI's side has an AIData SideInfo entry (not state: data every peer reads alike)
	unsigned long long gathererRequests = 0;  ///< the passes that asked for a gatherer (a diagnostic for S-4194: no object is asked, so RW 0x8F8B69's `inc` has no maker)
	const SkirmishAIStore *store = nullptr;   ///< the data the AI reads (not state)
	AIBuildState build;                       ///< step 2: the base builder, the build requests, the dozer list, the game phase (AIBaseBuilder.h)
	std::vector<std::shared_ptr<AIBuildRequest>> *farmSites = nullptr; ///< the manager's farm site pool (RW: a global vector, RW 0xDE9EEC)
	AISkirmishPlayer *groupFirst = nullptr;   ///< the group's first AI (its brain's current enemy is the group's, RW 0x6C7807)
	AIBrainState brain;                       ///< step 4: the brain (+ 0x164): target chooser, tactics, the team builder's teams (AITacticalAI.h)
};

// the 0x28-byte group of allied AI players (ctor RW 0x8EDD10)
struct AIGroupTask
{
	// AITask (base ctor RW 0x961B48, 0x3C bytes): + 0x10 the state (RW 0x8EDA5E: 2 and 3 are finished, 1 runs); the group's vector (+ 0x18) holds these.
	// The only task the group's add (RW 0x8EDEAC) makes is the money transfer (ctor RW 0x99EFD7, tick RW 0x99EEF4): + 0x2C / + 0x30 the player indices,
	// + 0x34 the amount left, + 0x38 the most one tick pays.
	enum State
	{
		TASK_ACTIVE = 1,
		TASK_DONE = 2,
		TASK_FAILED = 3
	};
	int state = TASK_ACTIVE;      ///< + 0x10
	int fromPlayer = -1;          ///< + 0x2C
	int toPlayer = -1;            ///< + 0x30
	std::uint32_t remaining = 0;  ///< + 0x34
	std::uint32_t perUpdate = 0;  ///< + 0x38
};

struct AISkirmishGroup
{
	std::map<int, std::unique_ptr<AISkirmishPlayer>> players; ///< + 0x00 by player index
	std::vector<int> members;                                  ///< + 0x0C players in the order they joined
	std::vector<AIGroupTask> tasks;                            ///< + 0x18 (RW 0x8EDB2C / 0x8EDA5E)
	bool active = true;                                        ///< + 0x24
	int lastLiveCount = 0;                                     ///< the count RW 0x8EDA8E gave the last update (fed to the brains)
	// RW 0x8EDEAC: the money task (from, to, amount, the most one update pays)
	void addTask(int from, int to, std::uint32_t amount, std::uint32_t perUpdate)
	{
		AIGroupTask task;
		task.fromPlayer = from;
		task.toPlayer = to;
		task.remaining = amount;
		task.perUpdate = perUpdate;
		tasks.push_back(task);
	}
};

class SkirmishAIManager
{
public:
	explicit SkirmishAIManager(GameLogic &logic) : m_logic(logic) {}

	// the data of this game's world (Default\SkirmishAIData.ini): set before newGame; null = the AI cannot start (reported, never a default)
	void setStore(const SkirmishAIStore *store) { m_store = store; }
	const SkirmishAIStore *store() const { return m_store; }

	// RW 0x6AA030. `skirmishOrMultiplayer`: TheGameInfo says so (a game started from a skirmish setup); `loadingSave`: RW 0x5FF924. Returns the
	// number of AI players made.
	// `mapFileName`: the current map's file name as the map cache holds it ("map mp evendim.map"), compared with AIBase GameMapToUseOn
	int newGame(bool skirmishOrMultiplayer, bool loadingSave = false, const std::string &mapFileName = std::string());
	// RW 0x6A96A0, once per logic frame in phase 5 (GameLogic::update)
	void update();
	void reset();
	~SkirmishAIManager();

	// lane AI-2: the manager's object events, installed as GameLogic world hooks while a game runs (RW 0x68E42D in the enter-world function RW 0x68E31F, RW 0x68C205
	// in the leave-world function RW 0x68C18F): RW 0x6A9D9C (the object's controlling player's AI, unless disabled: RW 0x6C7779 -> RW 0x8F069A; the capture flag
	// list + 0xA68 is not ported; lane DECOMP-3: the player info record add RW 0x8E4591 is) and RW 0x6A99CB (RW 0x6C778A -> RW 0x8F0C47; the record's remove RW 0x8E480C)
	// lane DECOMP-3: the world hooks are containment listeners (RW 0x68E31F / 0x68C18F also run when a contain takes an object in or out); an INERT template's
	// world entry skips the event (RW 0x68E32A -> 0x68E439), the exit never does
	void objectEnteredWorld(Object &obj);
	void objectLeftWorld(Object &obj);
	// RW 0x6A9D9C / RW 0x6A99CB themselves, also called by Object's team change (RW 0x697C09: the controlling player changes), the capture (RW 0x854712 /
	// 0x85474D), CommandSetUpgrade (RW 0x8B7C8E / 0x8B7C9A) and the AIPlayer's Upgrade_RingHero check (RW 0x8F0E34 / 0x8F0E41). The object's + 0x4D0 byte they
	// test first is DozerAIUpdate's phantom structure flag (makePhantomStructureInert RW 0x88D0D8 / NotInert RW 0x88D123), which the port never sets
	void objectAdded(Object &obj);
	void objectRemoved(Object &obj);
	// lane DECOMP-3: the player info record of a player (manager + 0xA3C, RW 0x6A9999); null without one
	AIStatCollector *record(int playerIndex);
	const AIStatCollector *record(int playerIndex) const;
	// the dozer notifications: DozerAIInterface vslot 0x7C (RW 0x88BF4E -> AIPlayer RW 0x8F0660) and the construction-complete call RW 0x86C1ED -> RW 0x6C77E4 ->
	// RW 0x8F06FF(builder, structure, 1)
	void dozerIdle(Object &dozer);
	void structureBuilt(Object &builder, Object &structure);

	const std::vector<std::unique_ptr<AISkirmishGroup>> &groups() const { return m_groups; }
	const AISkirmishPlayer *findAI(int playerIndex) const;
	// lane RAI-POW-2: the same AI for a caller that updates its state (the enemy cache, RW 0x8F457D)
	AISkirmishPlayer *findAI(int playerIndex) { return const_cast<AISkirmishPlayer *>(static_cast<const SkirmishAIManager *>(this)->findAI(playerIndex)); }
	// RW 0x89C04B -> 0x8EDA39 / 0x8ED772: the manager's gate list (+ 0xA74), the gates the AI opens and closes for itself (RW 0x8ED6DC / 0x8ED726)
	void registerGate(GateOpenAndCloseBehavior &gate);
	void unregisterGate(GateOpenAndCloseBehavior &gate);
	const std::vector<GateOpenAndCloseBehavior *> &gates() const { return m_gates; }
	// RW 0x6A9D9C: historical registration order; dead and captured flags stay listed.
	const std::vector<ObjectID> &captureFlags() const { return m_captureFlags; }
	const AISkirmishGroup *findGroup(int playerIndex) const;
	// lane PORT-39: RW 0x8F736E, the gatherer counts of an AI from its side's AIData SideInfo entry and its difficulty (retail runs it from the AIPlayer
	// update RW 0x8F7D71, not ported: the port runs it at the AI's init in newGame, S-4194) and RW 0x8F858E, the count of the player's live supply trucks
	// and the order for one more (AIPlayer + 0x34; updateAI runs it every frame). The tests drive them directly: `updateResourceGatherers` returns whether
	// the AI was short (RW 0x8F868C) and leaves the count and the order tally in the AI
	void initResourceGatherers(AISkirmishPlayer &ai);
	bool updateResourceGatherers(AISkirmishPlayer &ai);
	// manager + 0xA3C: the players with a player info record (every playable player of a started game), in index order
	const std::vector<int> &infoPlayers() const { return m_infoPlayers; }
	// RW 0xDE9EEC: the farm site pool (made at newGame)
	const std::vector<std::shared_ptr<AIBuildRequest>> &farmSites() const { return m_farmSites; }
	bool started() const { return m_started; }

	struct Counters
	{
		unsigned long long managerUpdates = 0;     ///< update() calls that passed the gate
		unsigned long long gatedUpdates = 0;       ///< update() calls before frame 10 or without a group
		unsigned long long playerInfoUpdates = 0;  ///< RW 0x8E3CF3 calls (lane DECOMP-3: ported but for the position sampler)
		unsigned long long groupUpdates = 0;       ///< RW 0x8EDDF6 calls on an active group
		unsigned long long sharedBrainUpdates = 0; ///< RW 0x6C6397 calls (not ported)
		unsigned long long aiUpdates = 0;          ///< RW 0x6C7946 calls
		unsigned long long aiPlayerUpdates = 0;    ///< RW 0x8F0EB4 calls (the AIPlayer sub systems; not ported)
		unsigned long long brainUpdates = 0;       ///< the + 0x164 brain updates (not ported)
		unsigned long long disabledSkips = 0;      ///< a disabled AI whose group has no reason to update its brain
		unsigned long long shutdowns = 0;          ///< RW 0x6C78D9 / 0x6C7928 (not ported)
		unsigned long long objectAdds = 0;         ///< lane DECOMP-3: RW 0x6A9D9C calls while a game runs
		unsigned long long objectRemoves = 0;      ///< lane DECOMP-3: RW 0x6A99CB calls while a game runs
	};
	const Counters &counters() const { return m_counters; }
	// lane DECOMP-3: a diagnostic sink for the object events (not game state, not hashed): while set, each RW 0x6A9D9C (true) / 0x6A99CB (false) call while a
	// game runs appends (add, object id)
	struct ObjectEvent
	{
		bool add;
		ObjectID id;
		std::string templateName; ///< the object's template at the event
		int playerIndex;          ///< its controlling player at the event (-1: none)
	};
	void setEventTrace(std::vector<ObjectEvent> *trace) { m_eventTrace = trace; }
	// diagnostics only (not game state, not saved or hashed): a sink for the gate walk's results, set by tests. After each walk (RW 0x8ED6DC before the
	// group's AIs, RW 0x8ED726 after them) one sample per listed gate: the walk, the gate and its geometry state at that moment.
	struct GateWalkSample
	{
		bool before;
		const GateOpenAndCloseBehavior *gate;
		int geometryState; ///< GateOpenAndCloseBehavior::GeometryState
	};
	void setGateWalkTrace(std::vector<GateWalkSample> *trace) { m_gateWalkTrace = trace; }

	void crc(StateHasher &h) const;
	// lane SAVE-4c: the obfme:SkirmishAI block (SkirmishAISnapshot.cpp): the groups, every AI's builders and brain, the player info records, the farm site pool
	// and the counters; every build request once, in a table the lists refer to by index (they share requests)
	void xfer(class Xfer &x);
	std::vector<std::string> report() const;
	// lane SAVE-4c: the transferred state (a load fills a fresh manager, which checkLoaded validates before this one takes it)
	void xferState(class Xfer &x, bool hooksInstalled);
	void checkLoaded() const;
	static std::vector<std::string> stopLines();
	// lane AI-3: the attacked-at records' running id (RW 0xDE4A08: cleared by RW 0x6A922D, incremented by RW 0x6C6F01)
	std::uint32_t &attackedAtIds() { return m_attackedAtIds; }
	// lane AIPOW-1: the spell book ASSIST_BATTLE decisions' per-player timer (RW 0xDEC154, a std::vector of {player index, next frame} in the order the
	// players first asked, RW 0x9E880B). Retail's is a static the process keeps across games; here it belongs to the game (cleared by reset), so the first
	// game of a process is retail's and later ones start clean (INFERENCE, S-1421)
	std::vector<std::pair<int, unsigned>> &spellBookAssistTimers() { return m_assistTimers; }
	// lane AIEXPAND-1: the EXPANSION heuristic's frame counter (+ 8 of the one AITargetHeuristicExpansionBase in TheAITargetHeuristicLibrary, ctor RW 0x97D00D,
	// read by RW 0x97D035), shared by every AI. Retail's library is made once (RW 0x82EF7B) and its reset (RW 0x63F3BF) does nothing, so the count runs on
	// across the games of a process; here it belongs to the game (cleared by reset), so the first game of a process is retail's (INFERENCE, S-417)
	std::uint32_t &expansionHeuristicFrames() { return m_expansionHeuristicFrames; }
	// lane AIEXPAND-1: the map file name newGame was given (RW reads TheGlobalData's map name, RW 0xDE4364 + 0xC, when a base is made: RW 0x9BCA8E) and the
	// AnyTypeTemplateDisabledSlots list (manager + 0x970), for the bases a SimpleExpansion adds (RW 0x9B5020 -> RW 0x90D3C7)
	const std::string &mapFileName() const { return m_mapFileName; }
	std::vector<std::string> &errors() { return m_errors; }

private:
	std::uint32_t m_attackedAtIds = 0;
	std::uint32_t m_expansionHeuristicFrames = 0;
	std::string m_mapFileName;
	std::vector<std::pair<int, unsigned>> m_assistTimers;
	AISkirmishPlayer *addAIPlayer(Player &player);
	bool groupAccepts(const AISkirmishGroup &g, const Player &player) const;
	int liveAICount(const AISkirmishGroup &g) const;
	Player *firstAIMember(const AISkirmishGroup &g) const;
	AISkirmishPlayer *groupFirstAI(AISkirmishGroup &g) const; ///< the group's first AI (firstOfGroup: the target chooser's owner)
	bool countsAsAI(const Player &p) const;
	void updateGroup(AISkirmishGroup &g);
	// RW 0x8ED6DC (before the group's AIs: the enemy gates' CLOSED geometry) / RW 0x8ED726 (after them: OPEN, forced)
	void updateGates(bool before, Player &player);
	void traceGateWalk(bool before); // diagnostics only (setGateWalkTrace)
	// RW 0x8EDB2C: the group's tasks (+ 0x18): finished entries are deleted (RW 0x8EDA5E), active ones tick (RW 0x99EEF4)
	void updateTasks(AISkirmishGroup &g);
	void updateAI(AISkirmishPlayer &ai, AISkirmishGroup &g);
	AISkirmishPlayer *eventAI(const Object &obj);
	// lane AI5: the AI of a player whatever its state (the record's listeners keep listening after a shutdown); null without one
	AISkirmishPlayer *listenerAI(int playerIndex);

	GameLogic &m_logic;
	const SkirmishAIStore *m_store = nullptr;
	std::vector<std::unique_ptr<AISkirmishGroup>> m_groups;
	std::vector<int> m_infoPlayers;
	std::vector<ObjectID> m_captureFlags;
	std::vector<GateOpenAndCloseBehavior *> m_gates; ///< manager + 0xA74 (a 12-byte vector in retail, made by the manager's ctor)
	std::map<int, std::unique_ptr<AIStatCollector>> m_records; ///< manager + 0xA3C: the player info records by player index (lane DECOMP-3)
	void makeRecord(const Player &player);
	std::vector<std::shared_ptr<AIBuildRequest>> m_farmSites; ///< RW 0xDE9EEC (made by the first economy builder, RW 0x8EEBAC)
	bool m_started = false;
	std::vector<std::string> m_errors;
	Counters m_counters;
	std::vector<ObjectEvent> *m_eventTrace = nullptr; ///< diagnostics only (setEventTrace)
	std::vector<GateWalkSample> *m_gateWalkTrace = nullptr; ///< diagnostics only (setGateWalkTrace)
	int m_worldHooks = 0; ///< the GameLogic world hook token while a game runs (0: none)
};
