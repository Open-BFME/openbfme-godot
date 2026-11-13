// OpenBFME. GPL-3.0.
// See GameClient/GUI/LoadScreenInfo.h.

#include "GameClient/GUI/LoadScreenInfo.h"
#include "Common/ExtendedPlayerColors.h"

#include "GameClient/GUI/GameTextSource.h"
#include "GameClient/GUI/Skirmish/RealTimeStats.h"
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
	const std::function<bool(const std::string &)> &imageExists, const SkirmishProfileStore *profiles, bool online)
{
	LoadScreenInfo info;
	info.mapStartCount = game.mapStartCount;
	if (game.mapStartCount < LEGACY_MAX_SLOTS || game.mapStartCount > MAX_SLOTS)
	{
		info.error = "loading map capacity must be 8 .. 16";
		return info;
	}
	for (int i = game.mapStartCount; i < MAX_SLOTS; ++i)
	{
		if (game.slots[i].isOccupied())
		{
			info.error = "occupied loading slot outside map capacity: " + std::to_string(i);
			return info;
		}
	}
	info.gameLoadingType = 2;
	info.mapName = game.mapName;
	// lane RANKICON-1 (RW 0x81CF16 .. 0x81CFD4; BFME2 AptLoadScreenInit.cpp): SkirmishPreferences(game mode) -> the current profile (RW 0x8221F5 /
	// 0x821A14), then StrategicStatsPreferences (RW 0x93E6F2) for a War of the Ring battle, else RealTimeStatsPreferences (RW 0x93E72F). Retail builds the
	// object per human card; the file is the same for every card, so it is read once. Retail's preferences object reads a missing file as no keys: 0 points.
	const bool warOfTheRing = game.warOfTheRing();
	const RankPointTable rankTable = loadScreenRankTable(online, warOfTheRing);
	RealTimeStatsPreferences realTimeStats;
	StrategicStatsPreferences strategicStats;
	if (profiles && imageExists && !online)
	{
		info.rankStatsRead = true;
		std::string error;
		const bool ok = warOfTheRing ? strategicStats.load(profiles->folder(), profiles->current(), &error)
									 : realTimeStats.load(profiles->folder(), profiles->current(), &error);
		info.rankStatsPath = warOfTheRing ? strategicStats.path() : realTimeStats.path();
		if (!ok)
		{
			info.rankStatsError = error.empty() ? "the profile stats file could not be read" : error;
		}
	}
	int card = 0;
	bool haveLocal = false;
	for (int i = 0; i < game.mapStartCount; ++i)
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
		else if (const auto *color = ExtendedPlayerColors::find(slot.color, (int)setup.colors().size(), game.mapStartCount))
		{
			c.color = color->rgb;
		}
		// lane MENU-1: the Level column (RW 0x81CB08); lane RANKICON-1: a human's points are its army's "<army>Points" of the profile's stats (RW 0x93C1F9)
		if (imageExists)
		{
			if (slot.isHuman() && info.rankStatsRead && info.rankStatsError.empty())
			{
				const std::string army = rankArmyAscii(c.armyName);
				c.rankPoints = warOfTheRing ? strategicStats.armyPoints(army) : realTimeStats.armyPoints(army);
			}
			c.levelImage = rankIconFor(slot, c.armyName, rankTable, c.rankPoints, imageExists, (int)setup.factions().size());
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
	// lane CHEAT-1: a Cheat Mode game says so before it starts (and the in-game marker keeps saying it)
	if (game.cheatModeOn())
	{
		info.rulesSummary += (info.rulesSummary.empty() ? u"" : u"; ") + CheatModeRule::label() + u": on";
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

RankPointTable loadScreenRankTable(bool online, bool warOfTheRing)
{
	if (online)
	{
		return warOfTheRing ? RankPointTable::OnlineStrategic : RankPointTable::OnlineRealTime;
	}
	return warOfTheRing ? RankPointTable::LocalStrategic : RankPointTable::LocalRealTime;
}

const int *rankPointThresholds(RankPointTable table)
{
	// RW 0x96054C (BFME2 Rva00559A76): RW 0xDEBAA4 / 0xDEBAD0
	static const int kLocal[11] = { -1, 0, 5, 10, 30, 50, 150, 300, 500, 800, 1500 };
	// RW 0x9604E7 (BFME2 Rva00559A11): RW 0xDEBA3C / 0xDEBA70
	static const int kOnline[11] = { -1, 0, 5, 10, 30, 50, 150, 500, 1000, 2000, 5000 };
	switch (table)
	{
		case RankPointTable::LocalStrategic:
		case RankPointTable::LocalRealTime:
			return kLocal;
		case RankPointTable::OnlineStrategic:
		case RankPointTable::OnlineRealTime:
			return kOnline;
	}
	return kLocal; // unreachable: every enumerator is handled above
}

int rankLevelForPoints(RankPointTable table, int points)
{
	// RW 0x960597: the first threshold above the points, less one; 10 past the last
	const int *thresholds = rankPointThresholds(table);
	for (int i = 1; i < 11; ++i)
	{
		if (thresholds[i] > points)
		{
			return i - 1;
		}
	}
	return 10;
}

std::string rankArmyAscii(const std::u16string &armyName)
{
	// AsciiString from the UnicodeString: the low byte of each unit
	std::string army;
	for (char16_t ch : armyName)
	{
		army.push_back((char)(ch & 0xFF));
	}
	return army;
}

std::string rankIconFor(const SkirmishGameSlot &slot, const std::u16string &armyName, RankPointTable table, int humanPoints,
	const std::function<bool(const std::string &)> &imageExists, int templateCount)
{
	const std::string army = rankArmyAscii(armyName);
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
		// RW 0x81CF09 / 0x81CF10: no player template (an observer) -> no icon
		if (templateCount >= 0 && (slot.playerTemplate < 0 || slot.playerTemplate >= templateCount))
		{
			return std::string();
		}
		// RW 0x9606E2 -> RW 0x960597 in the caller's table (lane RANKICON-1)
		level = rankLevelForPoints(table, humanPoints);
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
