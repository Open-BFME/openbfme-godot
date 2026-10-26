// OpenBFME. GPL-3.0.
// See RealTimeStats.h (lane OWNERFIX-3).

#include "GameClient/GUI/Skirmish/RealTimeStats.h"
#include "Common/PathIO.h"

#include "GameClient/MapCache.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <system_error>

namespace
{
std::string utf8(const std::u16string &s)
{
	std::string out;
	for (std::size_t i = 0; i < s.size(); ++i)
	{
		std::uint32_t c = s[i];
		if (c >= 0xD800 && c < 0xDC00 && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] < 0xE000)
		{
			c = 0x10000u + ((c - 0xD800u) << 10) + (std::uint32_t)(s[i + 1] - 0xDC00u);
			++i;
		}
		if (c < 0x80)
		{
			out += (char)c;
		}
		else if (c < 0x800)
		{
			out += (char)(0xC0 | (c >> 6));
			out += (char)(0x80 | (c & 0x3F));
		}
		else if (c < 0x10000)
		{
			out += (char)(0xE0 | (c >> 12));
			out += (char)(0x80 | ((c >> 6) & 0x3F));
			out += (char)(0x80 | (c & 0x3F));
		}
		else
		{
			out += (char)(0xF0 | (c >> 18));
			out += (char)(0x80 | ((c >> 12) & 0x3F));
			out += (char)(0x80 | ((c >> 6) & 0x3F));
			out += (char)(0x80 | (c & 0x3F));
		}
	}
	return out;
}
} // namespace

bool RealTimeStatsPreferences::load(const std::string &folder, const std::u16string &profile, std::string *error)
{
	m_file = OptionPreferences();
	m_path.clear();
	if (!profile.empty() && folder.empty())
	{
		m_file.set("UseMapListTooltips", "1");
		if (error)
		{
			*error = "RealTimeStats: the profile store has no user data folder (an in-memory store): no stats file";
		}
		return false;
	}
	if (!profile.empty())
	{
		// loadStatsFile (RW 0x93E669): the profile name and the leaf appended to the user data folder (UserPreferences::load RW 0x7B2B7F)
		m_path = folder + utf8(profile) + kLeaf;
		std::error_code ec;
		const std::filesystem::path path = PathIO::native(m_path);
		if (std::filesystem::exists(path, ec))
		{
			// a folder (or anything not a file) of that name cannot be read: an error, not an empty profile
			const bool file = std::filesystem::is_regular_file(path, ec);
			if (!file || !m_file.load(m_path, error))
			{
				if (!file && error)
				{
					*error = "RealTimeStats: " + m_path + " is not a file";
				}
				m_file = OptionPreferences();
				m_file.set("UseMapListTooltips", "1");
				return false;
			}
		}
	}
	m_file.set("UseMapListTooltips", "1"); // RealTimeStatsPreferences' constructor (RW 0x93E72F): setBool -> boolAsStr "%d"
	return true;
}

int RealTimeStatsPreferences::getInt(const std::string &key, int defaultValue) const
{
	if (!m_file.has(key))
	{
		return defaultValue;
	}
	const std::string v = m_file.get(key);
	return v.empty() ? defaultValue : std::atoi(v.c_str()); // RW 0x7B2620: an empty value is the default, else atoi
}

void RealTimeStatsPreferences::setInt(const std::string &key, int value)
{
	m_file.set(key, std::to_string(value)); // intAsStr "%d"
}

bool RealTimeStatsPreferences::write(std::string *error) const
{
	if (m_path.empty())
	{
		if (error)
		{
			*error = "RealTimeStats: no profile, no file to write (UserPreferences::write RW 0x7B274C refuses an empty file name)";
		}
		return false;
	}
	return m_file.save(m_path, error);
}

std::string RealTimeStatsPreferences::statsMapName(const std::string &cacheKey)
{
	std::string s = cacheKey;
	std::replace(s.begin(), s.end(), '/', '\\');
	return s;
}

int RealTimeStatsPreferences::conquestState(const RealTimeStatsPreferences *stats, const MapCacheEntry &map)
{
	int state = 0;
	if (stats && map.isMultiplayer)
	{
		const std::string name = statsMapName(map.name);
		// RW 0x8460B5: the counters of difficulty 2 .. 6, the highest non-zero one decides (6 first)
		state = 1;
		for (int difficulty = 6; difficulty >= 2; --difficulty)
		{
			if (stats->mapCounter(name, difficulty) != 0)
			{
				state = difficulty;
				break;
			}
		}
	}
	if (!map.isOfficial)
	{
		state |= 0x8000; // + 0xF5 |= 0x80
	}
	return state;
}

const char *RealTimeStatsPreferences::conquestImage(int state)
{
	switch (state)
	{
		case 0:
		case 1:
			return "AptDifficultyNotConquered";
		case 2:
			return "AptDifficultyEasyConquered";
		case 3:
			return "AptDifficultyMedConquered";
		case 4:
			return "AptDifficultyHardConquered";
		case 5:
			return "AptDifficultyBrutalConquered";
		case 6:
			return "AptDifficultyMaxConquered";
		case 0x8000:
		case 0x8001:
			return "AptUserMapNotConquered";
		case 0x8002:
			return "AptUserMapEasyConquered";
		case 0x8003:
			return "AptDifficultyMedConquered"; // RW 0x846278: the official medium image (retail's table)
		case 0x8004:
			return "AptUserMapHardConquered";
		case 0x8005:
			return "AptUserMapBrutalConquered";
		case 0x8006:
			return "AptUserMapMaxConquered";
		default:
			return "AptDifficultyNotConquered"; // RW 0x846674: the badge starts as the not-conquered image
	}
}

bool SkirmishHonorsGame::isSandbox() const
{
	int localTeam = -1;
	if (localSlot < 0)
	{
		for (int i = 0; i < (int)slots.size(); ++i)
		{
			if (occupied(i))
			{
				localTeam = slots[(size_t)i].team;
				break;
			}
		}
	}
	else
	{
		localTeam = slots[(size_t)localSlot].team;
	}
	for (int i = 0; i < (int)slots.size(); ++i)
	{
		if (i == localSlot)
		{
			continue;
		}
		if (occupied(i) && (slots[(size_t)i].team < 0 || slots[(size_t)i].team != localTeam))
		{
			return false;
		}
	}
	return true;
}

bool SkirmishHonorsGame::isAlly(int i) const
{
	if (localSlot < 0 || localSlot >= (int)slots.size())
	{
		return true; // RW 0x9CE6C5: no local slot
	}
	if (i == localSlot)
	{
		return true;
	}
	const int team = slots[(size_t)i].team;
	return team >= 0 && team == slots[(size_t)localSlot].team;
}

bool recordSkirmishHonors(RealTimeStatsPreferences &stats, const SkirmishHonorsGame &game, bool localAlliedVictory, std::string *what)
{
	auto say = [&](const std::string &s) {
		if (what)
		{
			*what = s;
		}
		return false;
	};
	if (!game.valid)
	{
		return say("no skirmish game");
	}
	if (game.isSandbox())
	{
		return say("a sandbox game (RW 0x9CE8F3 returns first)");
	}
	if (!localAlliedVictory)
	{
		return say("not the local side's victory");
	}
	// Rva005BF2C7Update (RW 0x9CE6FE)
	int easy = 0, normal = 0, hard = 0, brutal = 0;
	bool allied = false;
	for (int i = 0; i < (int)game.slots.size(); ++i)
	{
		if (game.isAI(i) && !game.isAlly(i))
		{
			const int state = game.slots[(size_t)i].state;
			easy += state == 2 ? 1 : 0;
			normal += state == 3 ? 1 : 0;
			hard += state == 4 ? 1 : 0;
			brutal += state == 5 ? 1 : 0;
		}
		else if (game.isAI(i))
		{
			allied = true;
		}
	}
	if (allied)
	{
		return say("an AI ally");
	}
	if (!easy && !normal && !hard && !brutal)
	{
		return say("no AI opponent");
	}
	const std::string map = RealTimeStatsPreferences::statsMapName(game.mapKey);
	const int oldEasy = stats.mapCounter(map, 2), oldNormal = stats.mapCounter(map, 3), oldHard = stats.mapCounter(map, 4), oldBrutal = stats.mapCounter(map, 5);
	int difficulty = 0, value = 0;
	if (game.mapPlayers - 1 == brutal)
	{
		difficulty = 6;
		value = std::max(oldBrutal, brutal); // retail compares with the Brutal counter here
	}
	else if (brutal)
	{
		difficulty = 5;
		value = std::max(oldBrutal, brutal);
	}
	else if (hard)
	{
		difficulty = 4;
		value = std::max(oldHard, hard);
	}
	else if (normal)
	{
		difficulty = 3;
		value = std::max(oldNormal, normal);
	}
	else
	{
		difficulty = 2;
		value = std::max(oldEasy, easy);
	}
	stats.setMapCounter(map, difficulty, value);
	if (what)
	{
		*what = map + "_" + std::to_string(difficulty) + " = " + std::to_string(value);
	}
	return true;
}
