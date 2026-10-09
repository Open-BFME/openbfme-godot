// OpenBFME. GPL-3.0.
// See AIPathfindConfig.h.

#include "GameLogic/AI/AIPathfindConfig.h"

#include "Common/ArchiveFileSystem.h"
#include "Common/AsciiString.h"
#include "Common/INI/HostRealText.h"

#include <cctype>
#include <cstdlib>
#include <map>

namespace
{
std::string trim(const std::string &s)
{
	size_t a = 0, b = s.size();
	while (a < b && std::isspace((unsigned char)s[a])) ++a;
	while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
	return s.substr(a, b - a);
}

struct Values
{
	std::map<std::string, std::string> kv; // lower-cased key
};

void collect(void *user, const std::string &key, const std::string &value)
{
	((Values *)user)->kv[AsciiStringUtil::lowered(key)] = value;
}

bool readText(ArchiveFileSystem &fs, const char *path, std::string &text, std::string *error)
{
	std::vector<std::uint8_t> bytes;
	if (!fs.readFile(path, bytes, error))
	{
		return false;
	}
	text.assign(bytes.begin(), bytes.end());
	return true;
}

bool getInt(const Values &v, const char *key, int &out, const char *file, std::string *error)
{
	auto it = v.kv.find(AsciiStringUtil::lowered(key));
	if (it == v.kv.end())
	{
		if (error) *error = std::string(file) + ": key " + key + " not found";
		return false;
	}
	char *end = nullptr;
	const long n = std::strtol(it->second.c_str(), &end, 10);
	if (end == it->second.c_str())
	{
		if (error) *error = std::string(file) + ": " + key + " is not an integer: '" + it->second + "'";
		return false;
	}
	out = (int)n;
	return true;
}

// lane PHYS-1: INI::parseBool (RW 0x42E558): Yes / No (case-insensitive); anything else is an error
bool getBool(const Values &v, const char *key, bool &out, const char *file, std::string *error)
{
	auto it = v.kv.find(AsciiStringUtil::lowered(key));
	if (it == v.kv.end())
	{
		if (error) *error = std::string(file) + ": key " + key + " not found";
		return false;
	}
	if (AsciiStringUtil::compareNoCase(it->second, "Yes") == 0)
	{
		out = true;
		return true;
	}
	if (AsciiStringUtil::compareNoCase(it->second, "No") == 0)
	{
		out = false;
		return true;
	}
	if (error) *error = std::string(file) + ": " + key + " is not Yes or No: '" + it->second + "'";
	return false;
}

bool getFloat(const Values &v, const char *key, float &out, const char *file, std::string *error)
{
	auto it = v.kv.find(AsciiStringUtil::lowered(key));
	if (it == v.kv.end())
	{
		if (error) *error = std::string(file) + ": key " + key + " not found";
		return false;
	}
	char *end = nullptr;
	const float f = strtofPortable(it->second.c_str(), &end);
	if (end == it->second.c_str())
	{
		if (error) *error = std::string(file) + ": " + key + " is not a number: '" + it->second + "'";
		return false;
	}
	out = f;
	return true;
}
} // namespace

bool PathfindConfigLoader::scanBlock(const std::string &text, const std::string &blockName, std::string *error,
	void (*onPair)(void *user, const std::string &key, const std::string &value), void *user)
{
	bool inBlock = false, found = false;
	size_t pos = 0;
	while (pos <= text.size())
	{
		size_t nl = text.find('\n', pos);
		if (nl == std::string::npos) nl = text.size();
		std::string line = text.substr(pos, nl - pos);
		pos = nl + 1;
		const size_t semi = line.find(';');
		if (semi != std::string::npos) line.erase(semi);
		line = trim(line);
		if (line.empty()) continue;
		if (!inBlock)
		{
			if (AsciiStringUtil::compareNoCase(line, blockName) == 0)
			{
				inBlock = true;
				found = true;
			}
			continue;
		}
		if (AsciiStringUtil::compareNoCase(line, "End") == 0)
		{
			inBlock = false;
			continue;
		}
		const size_t eq = line.find('=');
		if (eq == std::string::npos) continue;
		onPair(user, trim(line.substr(0, eq)), trim(line.substr(eq + 1)));
	}
	if (!found)
	{
		if (error) *error = "block " + blockName + " not found";
		return false;
	}
	return true;
}

bool PathfindConfigLoader::load(ArchiveFileSystem &fs, PathfindConfig &out, std::string *error)
{
	std::string text, err;
	Values ai;
	// default\aidata.ini then aidata.ini (the second one overrides key by key; its AIData block may be empty)
	if (!readText(fs, "data\\ini\\default\\aidata.ini", text, error) || !scanBlock(text, "AIData", &err, collect, &ai))
	{
		if (error && error->empty()) *error = "data\\ini\\default\\aidata.ini: " + err;
		return false;
	}
	if (readText(fs, "data\\ini\\aidata.ini", text, nullptr))
	{
		std::string e2;
		scanBlock(text, "AIData", &e2, collect, &ai); // a missing block here is not an error: the file only overrides
	}
	Values gd;
	if (!readText(fs, "data\\ini\\gamedata.ini", text, error) || !scanBlock(text, "GameData", &err, collect, &gd))
	{
		if (error && error->empty()) *error = "data\\ini\\gamedata.ini: " + err;
		return false;
	}
	PathfindConfig c;
	if (!getFloat(ai, "WadeWaterDepth", c.wadeWaterDepth, "aidata.ini", error)) return false;
	if (!getFloat(ai, "DeepWaterDepth", c.deepWaterDepth, "aidata.ini", error)) return false;
	if (!getBool(ai, "HordesWaitForHordes", c.hordesWaitForHordes, "aidata.ini", error)) return false;
	if (!getFloat(ai, "MeleeApproachTolerance", c.meleeApproachTolerance, "aidata.ini", error)) return false; // lane PHYS-1: AIData +0x90 (row RW 0xC1CDB0)
	if (!getFloat(ai, "MeleeApproachDist", c.meleeApproachDist, "aidata.ini", error)) return false; // lane PHYS-1: AIData +0x94 (row RW 0xC1CDC0) // lane PHYS-1: AIData +0xB9 (row RW 0xC1CE40)
	if (!getFloat(ai, "CastleSiegeStandBackDistance", c.castleSiegeStandBackDistance, "aidata.ini", error)) return false; // lane PHYS-1: AIData +0xD4 (row RW 0xC1CF00)
	if (!getInt(gd, "MaxPathfindCellsPerFrame", c.cellsPerFrame, "gamedata.ini", error)) return false;
	if (!getInt(gd, "MaxCellsAdjustDestination", c.adjustDestinationLimit, "gamedata.ini", error)) return false;
	if (!getInt(gd, "MaxCellsAdjustHordeMeleeDestination", c.adjustHordeMeleeLimit, "gamedata.ini", error)) return false;
	if (!getInt(gd, "MaxCellsPatchPath", c.patchPathLimit, "gamedata.ini", error)) return false;
	if (!getInt(gd, "MaxCellsFindPathLimit", c.findPathLimit, "gamedata.ini", error)) return false;
	if (!getInt(gd, "MaxCellsFindAttackPath", c.findAttackPathLimit, "gamedata.ini", error)) return false;
	if (!getInt(gd, "MaxCellsAdjustToMeleeDestination", c.adjustToMeleeLimit, "gamedata.ini", error)) return false;
	if (!getInt(gd, "MaxCellsFindMeleeEngagementLocation", c.findMeleeEngagementLimit, "gamedata.ini", error)) return false;
	if (!getInt(gd, "MaxCellsAdjustToPossibleDestination", c.adjustToPossibleLimit, "gamedata.ini", error)) return false;
	if (!getInt(gd, "MaxCellsToExamineTowardsGoal", c.examineTowardsGoalLimit, "gamedata.ini", error)) return false;
	// Pathfinder.ini SlopeLimits: the RW field row's parse proc is a lone `ret` (RW 0x63F3BF), so retail never loads
	// them and the slope grade stays off (S-160)
	c.slopeLimits[0] = c.slopeLimits[1] = 0.0f;
	// the retail cell-info pool is not the ZH array (BFME keeps 0x200 fixed slots plus an overflow list, Open-BFME-1
	// PathfindCellInfoPoolReset.cpp); the pool here is sized to cover the largest search (FindPathLimit cells plus the
	// reservations of a full map) and exhaustion is reported (S-164)
	c.cellInfoPoolSize = 60000;
	c.zoneBlockSize = 16;
	out = c;
	return true;
}
