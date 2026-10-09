// OpenBFME. GPL-3.0.
// See GameLogic/NewGame/SkirmishSides.h for the sources of every rule.

#include "GameLogic/NewGame/SkirmishSides.h"

#include "Common/PlayerTemplate.h"

namespace
{
bool failWith(std::string *error, const std::string &why)
{
	if (error)
	{
		*error = why;
	}
	return false;
}

void appendName(std::string &list, const std::string &name)
{
	if (!list.empty())
	{
		list += ' ';
	}
	list += name;
}

std::int32_t colorInt(std::uint32_t rgb)
{
	return (std::int32_t)(0xFF000000u | (rgb & 0xFFFFFFu));
}
} // namespace

bool SkirmishSides::build(const SidesList &mapSides, const SkirmishGameInfo &info, int localSlot, const std::vector<GameLogicSettings::MultiplayerColorDef> &colors,
	const PlayerTemplateStore &templates, Built &out, std::string *error)
{
	out = Built();
	SidesList &sides = out.sides;
	sides.sidesVersion = mapSides.sidesVersion;
	sides.hasTeams = true;
	std::vector<std::string> keptOwners;
	bool hasCreeps = false;
	// prepareForMP_or_Skirmish
	for (const SidesInfo &side : mapSides.sides)
	{
		const std::string name = side.dict.getAsciiString("playerName");
		if (name.empty() || name == "PlyrCivilian" || name == "PlyrCreeps")
		{
			SidesInfo kept = side;
			kept.dict.setBool("playerIsHuman", false);
			sides.sides.push_back(kept);
			keptOwners.push_back(name);
			hasCreeps = hasCreeps || name == "PlyrCreeps";
		}
	}
	for (const Dict &team : mapSides.teams)
	{
		const std::string owner = team.getAsciiString("teamOwner");
		if (owner.empty() || owner == "PlyrCivilian" || owner == "PlyrCreeps")
		{
			sides.teams.push_back(team);
		}
	}
	// the side names
	out.slotPlayerNames.assign(MAX_SLOTS, std::string());
	for (int i = 0; i < MAX_SLOTS; ++i)
	{
		const SkirmishGameSlot &slot = info.slots[i];
		if (!slot.isOccupied())
		{
			continue;
		}
		if (slot.playerTemplate >= 0)
		{
			out.slotPlayerNames[(size_t)i] = "Player_" + std::to_string(slot.startPos + 1);
		}
		else
		{
			out.slotPlayerNames[(size_t)i] = "Observer_" + std::to_string(i + 1);
		}
	}
	bool anyAI = false;
	for (const SkirmishGameSlot &slot : info.slots)
	{
		anyAI = anyAI || slot.isAI();
	}
	std::string allNames;
	for (int i = 0; i < MAX_SLOTS; ++i)
	{
		if (info.slots[i].isOccupied())
		{
			appendName(allNames, out.slotPlayerNames[(size_t)i]);
		}
	}
	// two slots must not share a side name (a start position held twice)
	for (int i = 0; i < MAX_SLOTS; ++i)
	{
		for (int j = i + 1; j < MAX_SLOTS; ++j)
		{
			if (info.slots[i].isOccupied() && info.slots[j].isOccupied() && out.slotPlayerNames[(size_t)i] == out.slotPlayerNames[(size_t)j])
			{
				return failWith(error, "slots " + std::to_string(i) + " and " + std::to_string(j) + " both get the side name '" + out.slotPlayerNames[(size_t)i] + "'");
			}
		}
	}
	// addSidesForSlots
	for (int i = 0; i < MAX_SLOTS; ++i)
	{
		const SkirmishGameSlot &slot = info.slots[i];
		if (!slot.isOccupied())
		{
			continue;
		}
		const std::string &name = out.slotPlayerNames[(size_t)i];
		if (slot.color < 0 || slot.color >= (int)colors.size())
		{
			return failWith(error, "slot " + std::to_string(i) + " has the colour " + std::to_string(slot.color) + ": not resolved or outside the " + std::to_string(colors.size()) + " colours");
		}
		const PlayerTemplate *pt = nullptr;
		if (slot.playerTemplate >= 0)
		{
			pt = templates.getNthPlayerTemplate(slot.playerTemplate);
			if (!pt)
			{
				return failWith(error, "slot " + std::to_string(i) + " has the faction " + std::to_string(slot.playerTemplate) + ": not in the store");
			}
		}
		else if (slot.playerTemplate != PLAYERTEMPLATE_OBSERVER)
		{
			return failWith(error, "slot " + std::to_string(i) + " has an unresolved faction");
		}
		else if (!(pt = templates.findPlayerTemplate("FactionObserver")))
		{
			return failWith(error, "the store has no FactionObserver template");
		}
		SidesInfo side;
		Dict &d = side.dict;
		d.setAsciiString("playerName", name);
		d.setBool("playerIsHuman", slot.isHuman());
		d.setUnicodeString("playerDisplayName", slot.name);
		d.setAsciiString("playerFaction", pt->getName());
		std::string allies, enemies;
		for (int j = 0; j < MAX_SLOTS; ++j)
		{
			if (j == i || !info.slots[j].isOccupied())
			{
				continue;
			}
			if (slot.teamNumber == -1 || info.slots[j].teamNumber != slot.teamNumber)
			{
				appendName(enemies, out.slotPlayerNames[(size_t)j]);
			}
			else
			{
				appendName(allies, out.slotPlayerNames[(size_t)j]);
			}
		}
		if (hasCreeps)
		{
			appendName(enemies, "PlyrCreeps");
		}
		d.setAsciiString("playerAllies", allies);
		d.setAsciiString("playerEnemies", enemies);
		const GameLogicSettings::MultiplayerColorDef &col = colors[(size_t)slot.color];
		d.setInt("playerColor", colorInt(col.rgb));
		d.setInt("playerNightColor", colorInt(col.nightRgb));
		d.setInt("multiplayerStartIndex", slot.startPos);
		bool isLocal = false;
		if (slot.isHuman() && localSlot >= 0 && localSlot < MAX_SLOTS)
		{
			isLocal = slot.name == info.slots[localSlot].name;
		}
		d.setBool("multiplayerIsLocal", isLocal);
		if (info.startingCash >= 0)
		{
			d.setInt("playerStartMoney", info.startingCash);
		}
		if (anyAI)
		{
			d.setBool("playerIsSkirmish", true);
			if (slot.isAI())
			{
				d.setInt("skirmishDifficulty", (int)slot.state - (int)SLOT_EASY_AI);
			}
		}
		sides.sides.push_back(side);
		Dict team;
		team.setAsciiString("teamName", "team" + name);
		team.setAsciiString("teamOwner", name);
		team.setBool("teamIsSingleton", true);
		sides.teams.push_back(team);
	}
	// the creeps side: no allies, every player an enemy
	if (hasCreeps)
	{
		for (SidesInfo &side : sides.sides)
		{
			if (side.dict.getAsciiString("playerName") == "PlyrCreeps")
			{
				side.dict.setAsciiString("playerAllies", std::string());
				side.dict.setAsciiString("playerEnemies", allNames);
			}
		}
	}
	// ReplayObserver (RW 0x626E1F)
	if (colors.empty())
	{
		return failWith(error, "the colour list is empty");
	}
	const PlayerTemplate *obs = templates.findPlayerTemplate("FactionObserver");
	SidesInfo replay;
	replay.dict.setAsciiString("playerName", "ReplayObserver");
	replay.dict.setBool("playerIsHuman", true);
	replay.dict.setUnicodeString("playerDisplayName", u"Observer");
	if (obs)
	{
		replay.dict.setAsciiString("playerFaction", obs->getName());
	}
	replay.dict.setAsciiString("playerAllies", std::string());
	replay.dict.setAsciiString("playerEnemies", std::string());
	replay.dict.setInt("playerColor", colorInt(colors[0].rgb));
	replay.dict.setInt("playerNightColor", colorInt(colors[0].nightRgb));
	replay.dict.setInt("multiplayerStartIndex", 0);
	replay.dict.setBool("multiplayerIsLocal", false);
	sides.sides.push_back(replay);
	Dict rteam;
	rteam.setAsciiString("teamName", "teamReplayObserver");
	rteam.setAsciiString("teamOwner", "ReplayObserver");
	rteam.setBool("teamIsSingleton", true);
	sides.teams.push_back(rteam);
	return true;
}

std::vector<std::string> SkirmishSides::stopLines()
{
	return {
		"[S-271] skirmish sides: built from the RotWK 2.01 disassembly (RW 0x73193D, 0x627C1F, 0x626E1F; S-001 caveat); the map's Player_N sides are dropped with their scripts and "
		"build lists (the script lane's), playerAIType / livingWorldPlayerID / the handicap / the preorder flag are not set, a Player's night colour and AI difficulty are stored "
		"and not acted on",
	};
}
