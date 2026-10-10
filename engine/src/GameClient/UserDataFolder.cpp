// OpenBFME. GPL-3.0.
// See GameClient/UserDataFolder.h (lane CAH-2).

#include "GameClient/UserDataFolder.h"

#include <cctype>
#include <cstring>

namespace
{
bool sameNoCase(const std::string &a, const char *b)
{
	const std::size_t n = std::strlen(b);
	if (a.size() != n)
	{
		return false;
	}
	for (std::size_t i = 0; i < n; ++i)
	{
		if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i]))
		{
			return false;
		}
	}
	return true;
}

// RW 0xAAA5A0's keys
const char *const kKeys[] = { "SkuName", "GameName", "GameRegPath", "InstallerRegPath", "OnlineServer", "UserDataLeafName", "G1", "G2", "G3", "G4" };
} // namespace

namespace UserDataFolder
{

bool parseGameInfo(const std::vector<std::uint8_t> &bytes, std::map<std::string, std::string> &values, std::string *error)
{
	auto fail = [&](const std::string &why) {
		if (error)
		{
			*error = "gi.dat: " + why;
		}
		return false;
	};
	values.clear();
	if (bytes.size() < 8)
	{
		return fail("shorter than its 8-byte header");
	}
	auto u32 = [&](std::size_t at) {
		return (std::uint32_t)bytes[at] | ((std::uint32_t)bytes[at + 1] << 8) | ((std::uint32_t)bytes[at + 2] << 16) | ((std::uint32_t)bytes[at + 3] << 24);
	};
	if (u32(0) != 0x20204947u) // RW 0xAAA75A: "GI  "
	{
		return fail("no \"GI  \" magic");
	}
	const std::int32_t count = (std::int32_t)u32(4);
	std::size_t at = 8;
	auto readString = [&](std::string &out) {
		const std::size_t start = at;
		while (at < bytes.size() && bytes[at] != 0)
		{
			++at;
		}
		if (at >= bytes.size())
		{
			return false; // retail reads past its buffer here; the port stops
		}
		out.assign((const char *)bytes.data() + start, at - start);
		++at;
		return true;
	};
	for (std::int32_t i = 0; i < count; ++i)
	{
		std::string key, value;
		if (!readString(key) || !readString(value))
		{
			return fail("entry " + std::to_string(i) + " of " + std::to_string(count) + " runs past the end of the file");
		}
		for (const char *k : kKeys)
		{
			if (sameNoCase(key, k))
			{
				values[k] = value;
			}
		}
	}
	return true;
}

std::string leafName(const std::vector<std::uint8_t> &bytes, std::string *error)
{
	std::map<std::string, std::string> values;
	if (!parseGameInfo(bytes, values, error))
	{
		return std::string();
	}
	auto it = values.find("UserDataLeafName");
	if (it == values.end() || it->second.empty())
	{
		if (error)
		{
			*error = "gi.dat: no UserDataLeafName";
		}
		return std::string();
	}
	return it->second;
}

std::string userDataFolder(const std::string &appData, const std::string &leaf, char separator)
{
	std::string out = appData;
	if (out.empty() || (out.back() != '\\' && out.back() != '/'))
	{
		out.push_back(separator); // RW 0x6442E5: '\' unless the folder ends with one
	}
	out += leaf;
	out.push_back(separator);
	return out;
}

std::string heroSaveFolder(const std::string &userData, char separator)
{
	return userData + "Save" + separator;
}

} // namespace UserDataFolder
