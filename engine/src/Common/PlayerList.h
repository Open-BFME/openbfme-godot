// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// PlayerList (ZH Include/Common/PlayerList.h, Source/Common/RTS/PlayerList.cpp), lane LOGIC-1: the players of a game in index order,
// the neutral player first. Two ways to fill it:
//   * newGame: from a map's SidesList (ZH PlayerList::newGame, PlayerList.cpp:129-260): the neutral player is index 0, every side with
//     a playerName gets the next index, a side's playerFaction names its PlayerTemplate, playerEnemies / playerAllies (space separated
//     player names) set the relationships, then each player is related to itself (ALLIES) and to the neutral player (NEUTRAL), then the
//     TeamFactory is filled from the Teams chunk and every player's default team is "team" + its name;
//   * setupSkirmish: the skirmish-style API (players, factions, teams, colours, starting money) the shell will drive, which builds
//     the same structures without a map: players on one team number are allies, players on different team numbers enemies (ZH
//     GameLogic::startNewGame's slot loop), the neutral player is NEUTRAL to everybody.
//
// Determinism: players live in a vector in index order; lookups by name go through an ordered map.

#pragma once

#include "Common/CreateAHeroRecord.h"

#include "Common/Player.h"
#include "Common/PlayerTemplate.h"
#include "Common/Team.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

struct SidesList;
class PlayerEconomyHost;

struct SkirmishPlayer
{
	std::string name;          ///< the player's name (the neutral player's is "")
	std::string faction;       ///< a PlayerTemplate name (the block name, e.g. a faction template)
	bool human = false;
	int team = -1;             ///< skirmish team number: players with the same number >= 0 are allies; -1 = alone
	std::uint32_t color = 0;   ///< 0xRRGGBB; 0 = the template's PreferredColor
	int startIndex = 0;        ///< multiplayerStartIndex
	// lane HERO-2: the player's Create-a-Hero from the setup (the game slot's record, GameInfo.h SkirmishGameSlot::createAHero), installed at the game start
	bool hasCreateAHero = false;
	CreateAHeroHero createAHero;
};

struct SkirmishSetup
{
	std::vector<SkirmishPlayer> players;
	// -1 = the template's StartMoney (and the game's default when that is 0); otherwise every player starts with this
	long long startingMoney = -1;
	std::uint32_t defaultStartingCash = 0; ///< ZH GameInfo::getStartingCash when a template has no money
};

class PlayerList
{
public:
	PlayerList(NameKeyGenerator &keys, const PlayerTemplateStore &templates, TeamFactory &teams);
	PlayerList(const PlayerList &) = delete;
	PlayerList &operator=(const PlayerList &) = delete;

	// the neutral player only (ZH PlayerList::init)
	void init();
	// the errors are conditions retail reports with DEBUG_ASSERTCRASH (a missing PlayerTemplate, an unknown ally name ...): returned,
	// never dropped (PLAN rule 10)
	// lane SPELL-1: the game mode newGame's players read the spell book rules with (MP costs, IntrinsicSciencesMP, MaxLevelMP in a skirmish or
	// multiplayer game); the game start sets it before newGame. setupSkirmish / applySkirmishSlots always use the skirmish mode.
	void setSpellGameMode(const SpellGameMode &mode) { m_spellModeForMaps = mode; }
	const SpellGameMode &spellGameMode() const { return m_spellModeForMaps; }
	std::vector<std::string> newGame(const SidesList &sides, std::uint32_t defaultStartingCash);
	std::vector<std::string> setupSkirmish(const SkirmishSetup &setup);
	// A map game with lobby slots (ZH GameLogic::startNewGame): the players of `newGame` stay, and each entry of `setup.players` takes over the
	// existing player of the same NAME (a multiplayer map's player slot): its faction template, colour, human / computer type, start index and
	// money are re-initialised (ZH Player::init) and its relationships to the other taken-over players follow the team numbers (same
	// number = allies, else enemies). A name that is not a player of the map is an error. Money: `setup.startingMoney` when it is >= 0, else
	// the faction's StartMoney, else `defaultStartingCash`.
	std::vector<std::string> applySkirmishSlots(const SkirmishSetup &setup);

	int getPlayerCount() const { return (int)m_players.size(); }
	Player *getNthPlayer(int i) const { return i >= 0 && i < (int)m_players.size() ? m_players[(size_t)i].get() : nullptr; }
	Player *getNeutralPlayer() const { return m_players.empty() ? nullptr : m_players[0].get(); }
	Player *findPlayerWithName(const std::string &name) const;
	Player *getLocalPlayer() const { return m_local; }
	void setLocalPlayer(Player *p) { m_local = p ? p : getNeutralPlayer(); }
	// ZH PlayerList::validateTeam: an owner may name a team or a player ("team name" first, then a player's default team); null when neither
	Team *findTeamOrPlayerDefaultTeam(const std::string &owner) const;

	// lane ECON-1: the host every Player (existing and future) answers world questions through (Common/PlayerEconomyHost.h)
	void setEconomyHost(PlayerEconomyHost *host)
	{
		m_economyHost = host;
		for (std::unique_ptr<Player> &p : m_players)
		{
			p->setEconomyHost(host);
		}
	}
	PlayerEconomyHost *economyHost() const { return m_economyHost; }

	TeamFactory &teams() { return m_teams; }
	const PlayerTemplateStore &templates() const { return m_templates; }
	void crc(StateHasher &hasher) const;
	// the local player index: a per-peer diagnostic, NOT part of crc()
	void localCrc(StateHasher &hasher) const;

private:
	SpellGameMode m_spellModeForMaps; // SPELL-1
	Player *addPlayer(const std::string &name, const std::string &displayName);
	void finishRelationships(Player *p);

	NameKeyGenerator &m_keys;
	const PlayerTemplateStore &m_templates;
	TeamFactory &m_teams;
	std::vector<std::unique_ptr<Player>> m_players;
	std::map<std::string, int> m_byName;
	Player *m_local = nullptr;
	PlayerEconomyHost *m_economyHost = nullptr; ///< not state
};
