// OpenBFME. GPL-3.0.
// See GameClient/GUI/LoadScreenInfo.h.

#include "GameClient/GUI/LoadScreenInfo.h"

#include "GameClient/GUI/GameTextSource.h"
#include "GameClient/GUI/Skirmish/SkirmishSetup.h"

#include <cctype>

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

LoadScreenInfo makeLoadScreenInfo(const SkirmishGameInfo &game, const SkirmishSetupSource &setup, const GameTextSource *text,
	const std::function<bool(const std::string &)> &imageExists)
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
		// lane MENU-1: the Level column (RW 0x81CB08); the profile's points are 0: the stats files are not ported [S-3506]
		if (imageExists)
		{
			c.levelImage = rankIconFor(slot, c.armyName, 0, imageExists);
		}
		c.human = slot.isHuman();
		if (slot.isHuman() && !haveLocal)
		{
			info.localCard = card;
			haveLocal = true;
		}
		++card;
	}
	// lane RULES-1, OpenBFME: the rules a player must know about that retail's load screen has no field for (drawn by the device over the movie)
	if (game.legendaryHeroes != LegendaryHeroes::kOff)
	{
		info.rulesSummary = LegendaryHeroes::label() + u": " + LegendaryHeroes::choiceText(game.legendaryHeroes);
	}
	// lane UBER-1: the Uber performance mode is on the load screen too, so nobody is surprised the game is not the original's simulation
	if (game.simulationProfile == SimulationProfile::Uber)
	{
		info.rulesSummary += (info.rulesSummary.empty() ? u"" : u"; ") + std::u16string(u"Uber performance mode");
	}
	return info;
}

namespace
{
// RW 0xDA3AC0
const char *const kRankSides[9] = { "Men", "Elves", "Dwarves", "Isengard", "Mordor", "Wild", "Angmar", "Arnor", "Neutral" };

bool equalNoCaseAscii(const std::string &a, const char *b)
{
	std::size_t i = 0;
	for (; i < a.size() && b[i]; ++i)
	{
		const char x = (char)std::tolower((unsigned char)a[i]), y = (char)std::tolower((unsigned char)b[i]);
		if (x != y)
		{
			return false;
		}
	}
	return i == a.size() && b[i] == 0;
}
} // namespace

std::string rankIconFor(const SkirmishGameSlot &slot, const std::u16string &armyName, int humanPoints, const std::function<bool(const std::string &)> &imageExists)
{
	// the army name as ASCII (AsciiString from the UnicodeString: the low byte of each unit)
	std::string army;
	for (char16_t ch : armyName)
	{
		army.push_back((char)(ch & 0xFF));
	}
	int side = 9; // RW 0x73BDA3
	if (army == "Goblins")
	{
		side = 5;
	}
	else
	{
		for (int i = 0; i < 9; ++i)
		{
			if (equalNoCaseAscii(army, kRankSides[i]))
			{
				side = i;
				break;
			}
		}
	}
	int level = -1;
	if (slot.isAI())
	{
		// RW 0x81CFAA: easy 1, medium 4, hard 6, brutal 9
		static const int kAILevel[4] = { 1, 4, 6, 9 };
		level = kAILevel[slot.state - SLOT_EASY_AI];
	}
	else if (slot.isHuman())
	{
		// RW 0x9604E7 thresholds, RW 0x960597
		static const int kThresholds[11] = { -1, 0, 5, 10, 30, 50, 150, 500, 1000, 2000, 5000 };
		level = 10;
		for (int i = 1; i < 11; ++i)
		{
			if (humanPoints < kThresholds[i])
			{
				level = i - 1;
				break;
			}
		}
	}
	if (level < 0)
	{
		return std::string();
	}
	// RW 0x960621: "AptRankIcon%s%d", else "AptRankIcon%d" (side 9 prints "(null)", which no image has)
	const std::string sideName = side < 9 ? kRankSides[side] : "(null)";
	const std::string withSide = "AptRankIcon" + sideName + std::to_string(level);
	if (imageExists && imageExists(withSide))
	{
		return withSide;
	}
	const std::string plain = "AptRankIcon" + std::to_string(level);
	return imageExists && imageExists(plain) ? plain : std::string();
}
