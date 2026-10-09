// OpenBFME. GPL-3.0.
// See GameLogic/ProductionSettings.h.

#include "GameLogic/ProductionSettings.h"

#include "Common/ArchiveFileSystem.h"
#include "Common/AsciiString.h"
#include "Common/INI/HostRealText.h"

#include <cctype>
#include <cstdlib>
#include <sstream>

namespace
{
std::string trim(const std::string &s)
{
	size_t a = 0, b = s.size();
	while (a < b && std::isspace((unsigned char)s[a]))
	{
		++a;
	}
	while (b > a && std::isspace((unsigned char)s[b - 1]))
	{
		--b;
	}
	return s.substr(a, b - a);
}

bool parseFloat(const std::string &v, float &out)
{
	std::string s = v;
	if (!s.empty() && (s.back() == 'f' || s.back() == 'F'))
	{
		s.pop_back();
	}
	char *end = nullptr;
	const float d = strtofPortable(s.c_str(), &end); // retail reads the field with sscanf "%f" (lane WIN-1)
	if (end == s.c_str() || *end != 0)
	{
		return false;
	}
	out = d;
	return true;
}

// `MP1:1.0 MP2:2.0 ...` (tabs or spaces); MPn fills slot n - 1
bool parseMultiPlay(const std::string &value, float (&out)[8], int &count)
{
	std::istringstream in(value);
	std::string token;
	count = 0;
	while (in >> token)
	{
		const size_t colon = token.find(':');
		if (colon == std::string::npos || token.size() < 3 || token[0] != 'M' || token[1] != 'P')
		{
			return false;
		}
		const int n = std::atoi(token.substr(2, colon - 2).c_str());
		float v = 0.0f;
		if (n < 1 || n > 8 || !parseFloat(token.substr(colon + 1), v))
		{
			return false;
		}
		out[n - 1] = v;
		++count;
	}
	return count > 0;
}
} // namespace

bool ProductionSettings::scan(const std::string &text, ProductionSettings &out, std::string *error)
{
	bool inBlock = false, haveMin = false, haveMax = false, havePenalty = false, haveMultiple = false, haveUnit = false, haveBuilding = false;
	size_t pos = 0;
	while (pos <= text.size())
	{
		size_t nl = text.find('\n', pos);
		if (nl == std::string::npos)
		{
			nl = text.size();
		}
		std::string line = text.substr(pos, nl - pos);
		pos = nl + 1;
		const size_t semi = line.find(';');
		if (semi != std::string::npos)
		{
			line.erase(semi);
		}
		line = trim(line);
		if (line.empty())
		{
			continue;
		}
		if (!inBlock)
		{
			inBlock = AsciiStringUtil::compareNoCase(line, "GameData") == 0;
			continue;
		}
		if (AsciiStringUtil::compareNoCase(line, "End") == 0)
		{
			break;
		}
		const size_t eq = line.find('=');
		if (eq == std::string::npos)
		{
			continue;
		}
		const std::string key = trim(line.substr(0, eq)), value = trim(line.substr(eq + 1));
		auto real = [&](const char *name, float &dst, bool &flag) {
			if (AsciiStringUtil::compareNoCase(key, name) == 0)
			{
				if (!parseFloat(value, dst))
				{
					if (error)
					{
						*error = std::string("GameData ") + name + ": '" + value + "' is not a number";
					}
					return false;
				}
				flag = true;
			}
			return true;
		};
		if (!real("MinLowEnergyProductionSpeed", out.minLowEnergyProductionSpeed, haveMin) || !real("MaxLowEnergyProductionSpeed", out.maxLowEnergyProductionSpeed, haveMax) ||
			!real("LowEnergyPenaltyModifier", out.lowEnergyPenaltyModifier, havePenalty) || !real("MultipleFactory", out.multipleFactory, haveMultiple))
		{
			return false;
		}
		int n = 0;
		if (AsciiStringUtil::compareNoCase(key, "MultiPlayUnitSpeedMult") == 0)
		{
			if (!parseMultiPlay(value, out.multiPlayUnitSpeedMult, n))
			{
				if (error)
				{
					*error = "GameData MultiPlayUnitSpeedMult: cannot read '" + value + "'";
				}
				return false;
			}
			out.multiPlayEntries = n;
			haveUnit = true;
		}
		else if (AsciiStringUtil::compareNoCase(key, "MultiPlayBuildingSpeedMult") == 0)
		{
			if (!parseMultiPlay(value, out.multiPlayBuildingSpeedMult, n))
			{
				if (error)
				{
					*error = "GameData MultiPlayBuildingSpeedMult: cannot read '" + value + "'";
				}
				return false;
			}
			haveBuilding = true;
		}
	}
	if (!(haveMin && haveMax && havePenalty && haveMultiple && haveUnit && haveBuilding))
	{
		if (error)
		{
			*error = "GameData lacks a build time key (MinLowEnergyProductionSpeed, MaxLowEnergyProductionSpeed, LowEnergyPenaltyModifier, MultipleFactory, MultiPlayUnitSpeedMult, MultiPlayBuildingSpeedMult)";
		}
		return false;
	}
	out.loaded = true;
	return true;
}

bool ProductionSettings::load(ArchiveFileSystem &fs, ProductionSettings &out, std::string *error)
{
	std::vector<std::uint8_t> bytes;
	if (!fs.readFile("data\\ini\\gamedata.ini", bytes, error))
	{
		return false;
	}
	return scan(std::string(bytes.begin(), bytes.end()), out, error);
}
