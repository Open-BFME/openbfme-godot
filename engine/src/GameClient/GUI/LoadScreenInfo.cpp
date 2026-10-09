// OpenBFME. GPL-3.0.
// See GameClient/GUI/LoadScreenInfo.h.

#include "GameClient/GUI/LoadScreenInfo.h"

#include "GameClient/GUI/GameTextSource.h"
#include "GameClient/GUI/Skirmish/SkirmishSetup.h"

std::string loadScreenU16ToUtf8(const std::u16string &s)
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

LoadScreenInfo makeLoadScreenInfo(const SkirmishGameInfo &game, const SkirmishSetupSource &setup, const GameTextSource *text)
{
	LoadScreenInfo info;
	info.gameLoadingType = 2;
	info.mapName = game.mapName;
	int card = 0;
	bool haveLocal = false;
	for (int i = 0; i < MAX_SLOTS && card < MAX_LOAD_SLOTS; ++i)
	{
		const SkirmishGameSlot &slot = game.slots[i];
		if (!slot.isOccupied())
		{
			continue;
		}
		LoadScreenSlotInfo &c = info.cards[card];
		c.occupied = true;
		c.playerName = slot.name;
		c.teamNumber = slot.teamNumber;
		if (slot.playerTemplate >= 0 && slot.playerTemplate < (int)setup.factions().size())
		{
			c.armyName = fetchOrMissing(text, setup.factions()[(size_t)slot.playerTemplate].displayLabel);
		}
		else
		{
			c.armyName = fetchOrMissing(text, "GUI:Observer");
		}
		if (slot.color >= 0 && slot.color < (int)setup.colors().size())
		{
			c.color = setup.colors()[(size_t)slot.color].rgb;
		}
		if (slot.isHuman() && !haveLocal)
		{
			info.localCard = card;
			haveLocal = true;
		}
		++card;
	}
	return info;
}
