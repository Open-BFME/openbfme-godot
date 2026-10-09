// OpenBFME. GPL-3.0.
// See Common/PlayerList.h.

#include "Common/PlayerList.h"

#include "Common/StateHash.h"
#include "GameLogic/Map/SidesList.h"

#include <sstream>

PlayerList::PlayerList(NameKeyGenerator &keys, const PlayerTemplateStore &templates, TeamFactory &teams)
	: m_keys(keys)
	, m_templates(templates)
	, m_teams(teams)
{
	init();
}

Player *PlayerList::addPlayer(const std::string &name, const std::string &displayName)
{
	const int index = (int)m_players.size();
	m_players.push_back(std::make_unique<Player>(index));
	Player *p = m_players.back().get();
	p->setEconomyHost(m_economyHost);
	p->friend_setIdentity(name, m_keys.nameToKey(name), displayName);
	m_byName[name] = index;
	return p;
}

void PlayerList::init()
{
	m_players.clear();
	m_byName.clear();
	m_local = nullptr;
	Player *neutral = addPlayer("", "");
	neutral->init(nullptr, 0);
	m_local = neutral; // ZH PlayerList::init: setLocalPlayer(m_players[0])
}

Player *PlayerList::findPlayerWithName(const std::string &name) const
{
	auto it = m_byName.find(name);
	return it == m_byName.end() ? nullptr : m_players[(size_t)it->second].get();
}

Team *PlayerList::findTeamOrPlayerDefaultTeam(const std::string &owner) const
{
	if (Team *t = m_teams.findTeam(owner))
	{
		return t;
	}
	if (Player *p = findPlayerWithName(owner))
	{
		return p->getDefaultTeam();
	}
	return nullptr;
}

namespace
{
std::string utf16ToUtf8(const std::u16string &s)
{
	std::string out;
	for (size_t i = 0; i < s.size(); ++i)
	{
		std::uint32_t cp = s[i];
		if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < s.size())
		{
			cp = 0x10000 + ((cp - 0xD800) << 10) + (s[i + 1] - 0xDC00);
			++i;
		}
		if (cp < 0x80)
		{
			out.push_back((char)cp);
		}
		else if (cp < 0x800)
		{
			out.push_back((char)(0xC0 | (cp >> 6)));
			out.push_back((char)(0x80 | (cp & 0x3F)));
		}
		else if (cp < 0x10000)
		{
			out.push_back((char)(0xE0 | (cp >> 12)));
			out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
			out.push_back((char)(0x80 | (cp & 0x3F)));
		}
		else
		{
			out.push_back((char)(0xF0 | (cp >> 18)));
			out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
			out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
			out.push_back((char)(0x80 | (cp & 0x3F)));
		}
	}
	return out;
}

std::vector<std::string> splitTokens(const std::string &s)
{
	std::vector<std::string> out;
	std::istringstream in(s);
	std::string t;
	while (in >> t)
	{
		out.push_back(t);
	}
	return out;
}
} // namespace

void PlayerList::finishRelationships(Player *p)
{
	// ZH PlayerList::newGame: "finally, make sure self & neutral are correct"
	p->setPlayerRelationship(p, ALLIES);
	if (p != getNeutralPlayer())
	{
		p->setPlayerRelationship(getNeutralPlayer(), NEUTRAL);
	}
}

std::vector<std::string> PlayerList::newGame(const SidesList &sides, std::uint32_t defaultStartingCash)
{
	std::vector<std::string> errors;
	m_teams.clear();
	init();
	bool setLocal = false;
	for (const SidesInfo &side : sides.sides)
	{
		const Dict &d = side.dict;
		const std::string pname = d.getAsciiString("playerName");
		if (pname.empty())
		{
			continue; // the neutral side, done by init()
		}
		bool hasDisplay = false;
		const std::u16string display = d.getUnicodeString("playerDisplayName", &hasDisplay);
		Player *p = addPlayer(pname, hasDisplay ? utf16ToUtf8(display) : std::string());
		const std::string faction = d.getAsciiString("playerFaction");
		const PlayerTemplate *pt = m_templates.findPlayerTemplate(faction);
		if (!pt)
		{
			errors.push_back("player '" + pname + "': PlayerTemplate '" + faction + "' not found (ZH: obsolete map)");
		}
		p->init(pt, defaultStartingCash);
		p->initScience(m_spellModeForMaps); // SPELL-1
		bool exists = false;
		const std::int32_t color = d.getInt("playerColor", &exists);
		if (exists)
		{
			p->setPlayerColor(0xFF000000u | ((std::uint32_t)color & 0xFFFFFFu));
		}
		const bool human = d.getBool("playerIsHuman");
		const bool skirmish = d.getBool("playerIsSkirmish", &exists) && exists;
		const std::int32_t night = d.getInt("playerNightColor", &exists);
		if (exists)
		{
			p->setPlayerNightColor(0xFF000000u | ((std::uint32_t)night & 0xFFFFFFu)); // RW 0x6AA55F: or 0xFF000000
		}
		const std::int32_t startMoney = d.getInt("playerStartMoney", &exists);
		if (exists)
		{
			// RW 0x6B10FD (Player::initFromDict): the side's playerStartMoney replaces the amount Player::init made (Money member RW 0x7B18B8)
			*p->getMoney() = Money((std::uint32_t)startMoney);
			p->getMoney()->setPlayerIndex(p->getPlayerIndex());
		}
		const std::int32_t difficulty = d.getInt("skirmishDifficulty", &exists);
		if (exists)
		{
			p->setSkirmishDifficulty(difficulty);
		}
		p->setPlayerType(human ? PLAYER_HUMAN : PLAYER_COMPUTER, !human && skirmish);
		p->setMultiplayerStartIndex(d.getInt("multiplayerStartIndex"));
		if (d.getBool("multiplayerIsLocal", &exists) && exists)
		{
			setLocalPlayer(p);
			setLocal = true;
		}
		if (!setLocal && human)
		{
			setLocalPlayer(p);
			setLocal = true;
		}
	}
	if (!setLocal)
	{
		// ZH: "Map has no human player... picking first nonneutral player for control"
		for (int i = 1; i < getPlayerCount(); ++i)
		{
			getNthPlayer(i)->setPlayerType(PLAYER_HUMAN, false);
			setLocalPlayer(getNthPlayer(i));
			setLocal = true;
			break;
		}
	}
	// ZH TeamFactory::initFromSides
	for (const Dict &t : sides.teams)
	{
		const std::string tname = t.getAsciiString("teamName");
		const std::string oname = t.getAsciiString("teamOwner");
		const bool singleton = t.getBool("teamIsSingleton");
		Player *owner = findPlayerWithName(oname);
		if (!owner)
		{
			errors.push_back("team '" + tname + "': no owner player named '" + oname + "' (ZH: falls back to the neutral player)");
			owner = getNeutralPlayer();
		}
		if (m_teams.findTeamPrototype(owner->getPlayerName(), tname)) // lane SCRIPT-2: a name is unique per owner (RW 0x7A2C47)
		{
			errors.push_back("team '" + tname + "' already exists");
			continue;
		}
		m_teams.initTeam(tname, owner, singleton, &t);
	}
	// relationships and default teams, per side
	for (const SidesInfo &side : sides.sides)
	{
		const Dict &d = side.dict;
		Player *p = findPlayerWithName(d.getAsciiString("playerName"));
		if (!p)
		{
			continue;
		}
		for (const std::string &tok : splitTokens(d.getAsciiString("playerEnemies")))
		{
			if (Player *p2 = findPlayerWithName(tok))
			{
				p->setPlayerRelationship(p2, ENEMIES);
			}
			else
			{
				errors.push_back("player '" + p->getPlayerName() + "': unknown enemy '" + tok + "'");
			}
		}
		for (const std::string &tok : splitTokens(d.getAsciiString("playerAllies")))
		{
			if (Player *p2 = findPlayerWithName(tok))
			{
				p->setPlayerRelationship(p2, ALLIES);
			}
			else
			{
				errors.push_back("player '" + p->getPlayerName() + "': unknown ally '" + tok + "'");
			}
		}
		finishRelationships(p);
		const std::string dt = "team" + p->getPlayerName(); // ZH Player::setDefaultTeam
		Team *team = m_teams.findTeam(dt);
		if (!team)
		{
			errors.push_back("player '" + p->getPlayerName() + "': no default team '" + dt + "'");
		}
		else
		{
			p->setDefaultTeam(team);
		}
	}
	return errors;
}

std::vector<std::string> PlayerList::setupSkirmish(const SkirmishSetup &setup)
{
	std::vector<std::string> errors;
	m_teams.clear();
	init();
	Player *neutral = getNeutralPlayer();
	neutral->setDefaultTeam(m_teams.createTeam(m_teams.initTeam("team", neutral, true)));
	bool setLocal = false;
	for (const SkirmishPlayer &sp : setup.players)
	{
		if (findPlayerWithName(sp.name))
		{
			errors.push_back("player '" + sp.name + "' is listed twice");
			continue;
		}
		Player *p = addPlayer(sp.name, sp.name);
		const PlayerTemplate *pt = m_templates.findPlayerTemplate(sp.faction);
		if (!pt)
		{
			errors.push_back("player '" + sp.name + "': PlayerTemplate '" + sp.faction + "' not found");
		}
		p->init(pt, setup.defaultStartingCash);
		p->initScience(SpellGameMode{ true, false }); // SPELL-1: a skirmish (GameLogic + 0x110 == 2)
		if (sp.color)
		{
			p->setPlayerColor(0xFF000000u | (sp.color & 0xFFFFFFu));
		}
		if (setup.startingMoney >= 0)
		{
			*p->getMoney() = Money((std::uint32_t)setup.startingMoney);
			p->getMoney()->setPlayerIndex(p->getPlayerIndex());
		}
		p->setPlayerType(sp.human ? PLAYER_HUMAN : PLAYER_COMPUTER, !sp.human);
		p->setMultiplayerStartIndex(sp.startIndex);
		if (sp.human && !setLocal)
		{
			setLocalPlayer(p);
			setLocal = true;
		}
		p->setDefaultTeam(m_teams.createTeam(m_teams.initTeam("team" + sp.name, p, true)));
	}
	// the slot loop of ZH GameLogic::startNewGame: same team number = allies, else enemies; the neutral player is NEUTRAL to everyone
	for (size_t i = 0; i < setup.players.size(); ++i)
	{
		Player *a = findPlayerWithName(setup.players[i].name);
		if (!a)
		{
			continue;
		}
		for (size_t j = 0; j < setup.players.size(); ++j)
		{
			Player *b = findPlayerWithName(setup.players[j].name);
			if (!b || a == b)
			{
				continue;
			}
			const bool allied = setup.players[i].team >= 0 && setup.players[i].team == setup.players[j].team;
			a->setPlayerRelationship(b, allied ? ALLIES : ENEMIES);
		}
		finishRelationships(a);
		neutral->setPlayerRelationship(a, NEUTRAL);
	}
	finishRelationships(neutral);
	return errors;
}

std::vector<std::string> PlayerList::applySkirmishSlots(const SkirmishSetup &setup)
{
	std::vector<std::string> errors;
	std::vector<Player *> taken(setup.players.size(), nullptr);
	for (size_t i = 0; i < setup.players.size(); ++i)
	{
		const SkirmishPlayer &sp = setup.players[i];
		Player *p = findPlayerWithName(sp.name);
		if (!p || p == getNeutralPlayer())
		{
			errors.push_back("slot '" + sp.name + "' is not a player of the map");
			continue;
		}
		const PlayerTemplate *pt = m_templates.findPlayerTemplate(sp.faction);
		if (!pt)
		{
			errors.push_back("slot '" + sp.name + "': PlayerTemplate '" + sp.faction + "' not found");
		}
		taken[i] = p;
		Team *defaultTeam = p->getDefaultTeam();
		p->init(pt, setup.defaultStartingCash);
		p->initScience(SpellGameMode{ true, false }); // SPELL-1: a skirmish (GameLogic + 0x110 == 2)
		p->setDefaultTeam(defaultTeam); // Player::init clears it; the team belongs to the map's Teams chunk
		if (sp.color)
		{
			p->setPlayerColor(0xFF000000u | (sp.color & 0xFFFFFFu));
		}
		if (setup.startingMoney >= 0)
		{
			*p->getMoney() = Money((std::uint32_t)setup.startingMoney);
			p->getMoney()->setPlayerIndex(p->getPlayerIndex());
		}
		p->setPlayerType(sp.human ? PLAYER_HUMAN : PLAYER_COMPUTER, !sp.human);
		p->setMultiplayerStartIndex(sp.startIndex);
		if (sp.human && m_local == getNeutralPlayer())
		{
			setLocalPlayer(p);
		}
	}
	for (size_t i = 0; i < taken.size(); ++i)
	{
		for (size_t j = 0; j < taken.size(); ++j)
		{
			if (taken[i] && taken[j] && i != j)
			{
				const bool allied = setup.players[i].team >= 0 && setup.players[i].team == setup.players[j].team;
				taken[i]->setPlayerRelationship(taken[j], allied ? ALLIES : ENEMIES);
			}
		}
	}
	return errors;
}

void PlayerList::crc(StateHasher &h) const
{
	h.addU32((std::uint32_t)m_players.size());
	for (const auto &p : m_players)
	{
		p->crc(h);
	}
	// the local player is this peer's UI identity, not simulation state: peers with identical state must hash identically (localCrc is the diagnostic)
	m_teams.crc(h);
}

void PlayerList::localCrc(StateHasher &h) const
{
	h.addI32(m_local ? m_local->getPlayerIndex() : -1);
}
