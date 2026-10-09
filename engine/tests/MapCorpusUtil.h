// OpenBFME unit tests. GPL-3.0.
// Small helpers shared by the retail map corpus tests.

#pragma once

#include "doctest.h"

#include "Common/MiniJson.h"

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <string>

namespace mapcorpus
{

inline std::string lowerStr(std::string s)
{
	for (char &c : s)
	{
		c = (char)std::tolower((unsigned char)c);
	}
	return s;
}

inline std::string slashes(std::string s, char to)
{
	for (char &c : s)
	{
		if (c == '/' || c == '\\')
		{
			c = to;
		}
	}
	return s;
}

inline const JsonValue &J(const JsonValue &v, const char *key)
{
	const JsonValue *p = v.get(key);
	REQUIRE_MESSAGE(p != nullptr, "survey key missing: " << key);
	return *p;
}

inline std::int64_t N(const JsonValue &v)
{
	REQUIRE(v.isNumber());
	return (std::int64_t)v.number;
}

inline std::string installRoot(const char *env)
{
	const char *v = std::getenv(env);
	std::string s = v ? slashes(lowerStr(v), '\\') : std::string();
	while (!s.empty() && s.back() == '\\')
	{
		s.pop_back();
	}
	return s;
}

// "rotwk:maps.big" for the archive that owns a path, from the archive's disk path.
inline std::string archiveLabel(const std::string &diskPath)
{
	std::string p = slashes(lowerStr(diskPath), '\\');
	std::string base = p.substr(p.find_last_of('\\') + 1);
	std::string rotwk = installRoot("ROTWK_INSTALL"), bfme2 = installRoot("BFME2_INSTALL");
	if (p.rfind(rotwk + "\\", 0) == 0) return "rotwk:" + base;
	if (p.rfind(bfme2 + "\\", 0) == 0) return "bfme2:" + base;
	return "?:" + base;
}

} // namespace mapcorpus
