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
//     (Player + 0x754), and COMPUTER or MakeAll...); the manager's object at + 0xA74 runs before and after (RW 0x8ED6DC / 0x8ED726); every AI of the
//     group, in player index order, gets that shared part and is updated (RW 0x6C7946); the group's tasks (+ 0x18) are updated (RW 0x8EDB2C).
//   * AI update RW 0x6C7946: a disabled AI only updates its brain while its group still has more than one live AI member, or exactly one and the AI
//     has an army; an enabled AI whose player is defeated is shut down (RW 0x6C78D9 / 0x6C7928) and disabled; otherwise RW 0x6C75B5 (when
//     + 0x178 != -1), RW 0x6C7677, the AIPlayer part RW 0x8F0EB4 (its sub systems at + 0x4, + 0xE4, + 0x38, + 0x90, + 0xC0, + 0x108, + 0x12C -> + 0xC
//     each through RW 0x90CD00 "update unless disabled"), then the brain (+ 0x164, RW 0x90CD00).
// What is ported here: the creation rules, the grouping, the disabled rule, the cadence and the order of every call above, with the state each
// step owns (hashed). The bodies of the called components (player info, the + 0xA74 object, the group tasks, the AIPlayer sub systems, the brain,
// the shutdown) are not ported: each call is counted and reported (stop S-412).

#pragma once

#include "Common/NameKeyGenerator.h"
#include "GameLogic/SkirmishAI/AIBaseBuilder.h"
#include "GameLogic/SkirmishAI/AITacticalAI.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

class GameLogic;
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
	const SkirmishAIStore *store = nullptr;   ///< the data the AI reads (not state)
	AIBuildState build;                       ///< step 2: the base builder, the build requests, the dozer list, the game phase (AIBaseBuilder.h)
	std::vector<std::shared_ptr<AIBuildRequest>> *farmSites = nullptr; ///< the manager's farm site pool (RW: a global vector, RW 0xDE9EEC)
	AISkirmishPlayer *groupFirst = nullptr;   ///< the group's first AI (its brain's current enemy is the group's, RW 0x6C7807)
	AIBrainState brain;                       ///< step 4: the brain (+ 0x164): target chooser, tactics, the team builder's teams (AITacticalAI.h)
};

// the 0x28-byte group of allied AI players (ctor RW 0x8EDD10)
struct AISkirmishGroup
{
	std::map<int, std::unique_ptr<AISkirmishPlayer>> players; ///< + 0x00 by player index
	std::vector<int> members;                                  ///< + 0x0C players in the order they joined
	bool active = true;                                        ///< + 0x24
	int lastLiveCount = 0;                                     ///< the count RW 0x8EDA8E gave the last update (fed to the brains)
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
	// list + 0xA68 and the player info record add RW 0x8E4591 are not ported) and RW 0x6A99CB (RW 0x6C778A -> RW 0x8F0C47; the record's remove RW 0x8E480C)
	void objectEnteredWorld(Object &obj);
	void objectLeftWorld(Object &obj);
	// the dozer notifications: DozerAIInterface vslot 0x7C (RW 0x88BF4E -> AIPlayer RW 0x8F0660) and the construction-complete call RW 0x86C1ED -> RW 0x6C77E4 ->
	// RW 0x8F06FF(builder, structure, 1)
	void dozerIdle(Object &dozer);
	void structureBuilt(Object &builder, Object &structure);

	const std::vector<std::unique_ptr<AISkirmishGroup>> &groups() const { return m_groups; }
	const AISkirmishPlayer *findAI(int playerIndex) const;
	const AISkirmishGroup *findGroup(int playerIndex) const;
	// manager + 0xA3C: the players with a player info record (every playable player of a started game), in index order
	const std::vector<int> &infoPlayers() const { return m_infoPlayers; }
	// RW 0xDE9EEC: the farm site pool (made at newGame)
	const std::vector<std::shared_ptr<AIBuildRequest>> &farmSites() const { return m_farmSites; }
	bool started() const { return m_started; }

	struct Counters
	{
		unsigned long long managerUpdates = 0;     ///< update() calls that passed the gate
		unsigned long long gatedUpdates = 0;       ///< update() calls before frame 10 or without a group
		unsigned long long playerInfoUpdates = 0;  ///< RW 0x8E3CF3 calls (not ported)
		unsigned long long groupUpdates = 0;       ///< RW 0x8EDDF6 calls on an active group
		unsigned long long sharedBrainUpdates = 0; ///< RW 0x6C6397 calls (not ported)
		unsigned long long aiUpdates = 0;          ///< RW 0x6C7946 calls
		unsigned long long aiPlayerUpdates = 0;    ///< RW 0x8F0EB4 calls (the AIPlayer sub systems; not ported)
		unsigned long long brainUpdates = 0;       ///< the + 0x164 brain updates (not ported)
		unsigned long long disabledSkips = 0;      ///< a disabled AI whose group has no reason to update its brain
		unsigned long long shutdowns = 0;          ///< RW 0x6C78D9 / 0x6C7928 (not ported)
	};
	const Counters &counters() const { return m_counters; }

	void crc(StateHasher &h) const;
	std::vector<std::string> report() const;
	static std::vector<std::string> stopLines();
	// lane AI-3: the attacked-at records' running id (RW 0xDE4A08: cleared by RW 0x6A922D, incremented by RW 0x6C6F01)
	std::uint32_t &attackedAtIds() { return m_attackedAtIds; }

private:
	std::uint32_t m_attackedAtIds = 0;
	AISkirmishPlayer *addAIPlayer(Player &player);
	bool groupAccepts(const AISkirmishGroup &g, const Player &player) const;
	int liveAICount(const AISkirmishGroup &g) const;
	Player *firstAIMember(const AISkirmishGroup &g) const;
	AISkirmishPlayer *groupFirstAI(AISkirmishGroup &g) const; ///< the group's first AI (firstOfGroup: the target chooser's owner)
	bool countsAsAI(const Player &p) const;
	void updateGroup(AISkirmishGroup &g);
	void updateAI(AISkirmishPlayer &ai, AISkirmishGroup &g);
	AISkirmishPlayer *eventAI(const Object &obj);

	GameLogic &m_logic;
	const SkirmishAIStore *m_store = nullptr;
	std::vector<std::unique_ptr<AISkirmishGroup>> m_groups;
	std::vector<int> m_infoPlayers;
	std::vector<std::shared_ptr<AIBuildRequest>> m_farmSites; ///< RW 0xDE9EEC (made by the first economy builder, RW 0x8EEBAC)
	bool m_started = false;
	std::vector<std::string> m_errors;
	Counters m_counters;
	int m_worldHooks = 0; ///< the GameLogic world hook token while a game runs (0: none)
};
