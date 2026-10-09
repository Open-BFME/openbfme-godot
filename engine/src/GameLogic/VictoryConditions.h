// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (VictoryConditions.cpp), as RotWK extends it.
//
// VictoryConditions (lane COMBAT-2): the skirmish win / lose rules. A player is eliminated when its base and its builders are gone; the game is won by the last alliance standing.
//
// TARGET FACTS (RotWK game.dat, caveat S-001): the class has the vtable RW 0xC4F108 (constructor RW 0x808D3C, init RW 0x808A43, update RW 0x808F53 = slot 10 (vslot 0x28), hasAchievedVictory RW
//   0x808AA8, hasBeenDefeated RW 0x808B2B, hasSinglePlayerBeenDefeated RW 0x80953C = vslot 0x40, cachePlayerPtrs RW 0x808B6A with the per-player test RW 0x808BD4). Layout: + 0x0C the victory
//   rule (init: 2, or 3 when the game mode is 3), + 0x18 the players (20 pointers), + 0x68 the local slot (-1), + 0x6C the end frame, + 0x70 the defeated flags (20 bytes), + 0x84 the local player
//   is defeated, + 0x85 a single alliance remains, + 0x86 observer, + 0x88 the defeat counter, + 0x8C the number of cached players, + 0x90 / + 0x91 two script flags.
//   * cachePlayerPtrs: every player of the list except the neutral player, a player without a template, the "FactionCivilian" template's player and an observer is cached in list order; the
//     local player's index becomes the local slot; with no local slot the game is observed (+ 0x84 and + 0x86 set).
//   * update (RW 0x808F53, called from the logic frame's phase 1 subsystem row, RW 0x62E6AF ff): after updateEndGame (the end screen, the client's) the delay is ftol(5 * GameData
//     SecondsBeforeBaseCheckActive) frames (default 5.0 seconds: RW 0x6438FD; 25 without GameData); nothing happens when the game is not a multiplayer game (RW 0x77D627), when there is no
//     local slot and no observer, or before that frame. Then (1) while no single alliance remains: if every player that has not been defeated is the ally of the first one (areAllies RW
//     0x8089F2: two different players that both call the other's team ALLIES) the alliance is single: flag + 0x85, end frame = frame, and the game's flag GameLogic + 0x98 clears unless the
//     game info says so (client); (2) for every cached player not yet marked: when hasSinglePlayerBeenDefeated: mark it, the start-of-game goFGC hook (RW 0x625F7C), Player + 0x4CC =
//     frame, for frame > 1 the player's map is revealed for good and the "GUI:PlayerHasBeenDefeated" message goes out (client), the rule for allied computer players of a defeated human
//     (RW 0x809271 .. 0x80936E, see below), then killPlayer (RW 0x6ABE7C).
//   * hasSinglePlayerBeenDefeated (RW 0x80953C): false for no player or an observer; true when the flag + 0x90 is set; else RW 0x809404 decides and a true answer increments the counter (+ 0x88):
//     a player already killed (Player + 0x754) is defeated; the game modes 3 and 9 and a script engine veto (RW 0x441E4A) answer "not defeated"; the rule + 0xC picks the test:
//       2 (the default): not defeated while the player has any object matching GameData VictoryConditionStructureObjectFilter (RW 0x6ABD0B: the player's teams' live, not destroyed, not
//          MINE objects the filter accepts), else defeated unless the player has a live non-structure, non-projectile, non-IGNORE_FOR_VICTORY object of KindOf DOZER (RW 0x6ABD73 /
//          0x7A0830);
//       3: not defeated while a structure matches the structure filter, else only while some object matches GameData VictoryConditionUnitObjectFilter (RW 0x6ABDC2 -> 0x7A1A81 -> 0x7A089B: that scan skips the effectively dead, the destroyed, STRUCTURE, PROJECTILE and IGNORE_FOR_VICTORY objects before the filter applies);
//       0 and 1: other kind-mask / team tests (RW 0x809508, 0x8094F8) that no rule value of a skirmish selects: not ported (stop S-344).
//   * hasAchievedVictory (RW 0x808AA8): false for no player or an observer; true for a player that has not been defeated (or allies of one) when a single alliance remains AND the defeat counter is
//     above 0 (so a game of one player is not won at the start); hasBeenDefeated (RW 0x808B2B): the player is no observer, a single alliance remains, the counter is above 0 and the player has not
//     achieved victory.
//   * killPlayer (RW 0x6ABE7C): the player's live objects are killed (the teams' members: RW 0x7A4AB3 gathers the live objects whose contain interface (object + 0x258, NOT the body at + 0x25C) answers its slot 0x114, then the ordinary objects are killed by RW 0x7A4CAE..0x7A4CB5, which pushes 0 and 8 into Object::kill (RW 0x698EC3: the FIRST argument, 8, is the DAMAGE type UNRESISTABLE, the second, 0, the DEATH type NORMAL: [ebp + 8] goes to DamageInfo + 0x10 and [ebp + 0xC] to + 0x1C); the more elaborate contain / transfer / removal paths of that pass are not ported (stop S-344) and the
//     player is marked dead (Player + 0x754); the score keeper multiplies the player's score by ScoreKeeper_PlayerEliminatedMultiplier; in a campaign mode a player with the flag + 0x5C stays alive.
// DONOR: ZH GameLogic/ScriptEngine/VictoryConditions.cpp (the structure of update / hasAchievedVictory); B1 VictoryConditionsUpdate.cpp (the delay, the BFME additions).
//
// INFERENCE / NOT PORTED (stop S-344): the order the killed objects die in (retail's team lists; here the logic's object list), the rule for allied computer players of a defeated human, the
// script veto (RW 0x441E4A), the rules 0 and 1, the end screens (updateEndGame / showEndGame: the client gets VictoryEvent records), the score screen, map reveal, EVA, chat and the game info /
// AI slot bookkeeping (RW 0x809370 ..), the score keeper multiplier.

#pragma once

#include "Common/StateHash.h"

#include <string>
#include <vector>

class GameLogic;
class Player;
struct ObjectFilter;

class VictoryConditions
{
public:
	static constexpr int MAX_PLAYER_COUNT = 20; // RW 0x808A55: the loops stop at 0x14

	enum Rule
	{
		RULE_ALL_OBJECTS = 0,   // RW 0x809508
		RULE_TEAM_OBJECTS = 1,  // RW 0x8094F8
		RULE_BASE_OR_BUILDERS = 2, // RW 0x8094A5: the skirmish default
		RULE_BASE_OR_ARMY = 3   // RW 0x809476
	};

	// what the client is told (the retail messages "GUI:PlayerHasBeenDefeated" and the victory / defeat screens)
	struct Event
	{
		enum Kind
		{
			PLAYER_DEFEATED = 0,
			ALLIANCE_VICTORY = 1
		};
		unsigned frame = 0;
		int kind = PLAYER_DEFEATED;
		int playerIndex = -1;
	};

	explicit VictoryConditions(GameLogic &logic);
	// RW 0x808A43 + cachePlayerPtrs RW 0x808B6A: forget everything, cache the players of the list
	void init();
	// the same with exactly these players cached (a scenario or a map test whose list holds placeholder players that are not part of the game): the local slot is the local player's
	void initWithPlayers(const std::vector<Player *> &players);
	void reset();
	// RW 0x808F53
	void update();

	// RW 0x80953C: the simulation's own check (update calls it): a true answer increments the defeat counter (RW 0x809572), which is hashed
	bool hasSinglePlayerBeenDefeated(const Player *player);
	// The client's queries (RW 0x808AA8 / 0x808B2B call hasSinglePlayerBeenDefeated and so bump the counter): CONST here, they evaluate the same rules and change nothing, so polling
	// them at any rate leaves the hash alone (lockstep). The simulation does not call them.
	bool hasAchievedVictory(const Player *player) const;
	bool hasBeenDefeated(const Player *player) const;
	bool wouldBeDefeated(const Player *player) const;

	// lane END-1: the local player's answers the human player's script library asks (Multiplayer_Human: MULTIPLAYER_ALLIED_VICTORY / _DEFEAT / PLAYER_DEFEAT):
	// RW vslots 0x48 / 0x4C / 0x50 and RW 0x7E526C; peer-local and const like the queries above
	bool localAlliedVictory() const;
	bool localAlliedDefeat() const;
	bool localDefeatedAnswer() const;
	bool localPlayerOnlyDefeat() const;

	unsigned endFrame() const { return m_endFrame; }
	bool singleAllianceRemaining() const { return m_singleAllianceRemaining; }
	bool isObserver() const { return m_isObserver; }
	int localSlot() const { return m_localSlot; }
	bool localPlayerDefeated() const { return m_localPlayerDefeated; }
	int defeatCounter() const { return m_defeatCount; }
	int cachedPlayers() const { return m_cached; }
	// the real Player::getPlayerIndex() of the cached player at this position (-1 outside)
	int cachedPlayerIndex(int cacheIndex) const;
	bool isDefeated(int cacheIndex) const { return cacheIndex >= 0 && cacheIndex < MAX_PLAYER_COUNT && m_isDefeated[cacheIndex]; }
	int rule() const { return m_rule; }
	void setRule(int rule) { m_rule = rule; }
	void setScriptFlag90(bool v) { m_flag90 = v; }
	void setScriptFlag91(bool v) { m_flag91 = v; }
	const std::vector<Event> &events() const { return m_events; }

	// RW 0x6ABE7C Player::killPlayer (see above); public for MSG_SELF_DESTRUCT (lane MP-2, GameLogic/SelfDestruct.h)
	void killPlayer(Player *p);
	// lane END-2: MSG_SELF_DESTRUCT (the dispatcher's case RW 0x77CA3D, the quit menu's surrender / exit): see VictoryConditions.cpp
	void selfDestruct(Player *player, bool transferToAlly);
	static void registerHandlers(class GameLogicDispatch &dispatcher);
	unsigned long long selfDestructs() const { return m_selfDestructs; }
	unsigned long long assetTransfersUnported() const { return m_transfersUnported; } // lane MP-2: stays 0 (RW 0x6AF598 is ported, GameLogic/SelfDestruct.h)

	void crc(StateHasher &hasher) const;
	static std::vector<std::string> stops();
	std::vector<std::string> report() const;

private:
	bool cachePlayer(Player *p);
	bool defeatedByRule(const Player *p, bool *ruleUnported) const;
	bool anyObjectOfKind(const Player &p, const char *kindName) const;
	bool anyUnitMatching(const Player &p, const ObjectFilter &filter) const;
	static bool areAllies(const Player *a, const Player *b);

	GameLogic &m_logic;
	int m_rule = RULE_BASE_OR_BUILDERS;
	Player *m_players[MAX_PLAYER_COUNT] = {};
	// PEER-LOCAL (a diagnostic of this client, NOT hashed: two peers of one game differ in them): the local player's cache position and the observer flags
	int m_localSlot = -1;
	unsigned m_endFrame = 0;
	bool m_isDefeated[MAX_PLAYER_COUNT] = {};
	bool m_localPlayerDefeated = false;
	bool m_singleAllianceRemaining = false;
	bool m_isObserver = false;
	int m_defeatCount = 0;
	int m_cached = 0;
	bool m_flag90 = false, m_flag91 = false;
	std::vector<Event> m_events;
	unsigned long long m_objectsKilled = 0;
	unsigned long long m_ruleUnported = 0;
	unsigned long long m_eliminationBonuses = 0; // lane END-1: killPlayer's bonus for the last attacker (RW 0x6ABFEE)
	unsigned long long m_selfDestructs = 0;      // lane END-2
	unsigned long long m_transfersUnported = 0;  // lane END-2: an ally that would have received the assets (RW 0x6AF598, S-1064)
};
