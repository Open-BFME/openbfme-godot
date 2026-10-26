// OpenBFME. GPL-3.0.
// See Common/BuildVersion.h (lane RELEASE-1); the strings are in the generated BuildVersion.gen.cpp.

#include "Common/BuildVersion.h"

#include <cstddef>
#include <cstdio>
#include <string>

namespace
{
// one release number at s[i..]: "0" or 1-9 digits without a leading zero; advances i, false when there is none
bool number(const std::string &s, size_t &i, std::string &out)
{
	const size_t start = i;
	while (i < s.size() && s[i] >= '0' && s[i] <= '9')
		++i;
	const size_t n = i - start;
	if (n == 0 || n > 9 || (n > 1 && s[start] == '0'))
		return false;
	out = s.substr(start, n);
	return true;
}
} // namespace

std::string BuildVersion::versionLine()
{
	return std::string("OpenBFME ") + kVersion + " (" + kBuildDate + ")";
}

bool BuildVersion::isReleaseVersion(const std::string &version)
{
	std::string part;
	size_t i = 1;
	if (version.size() > 40 || version.empty() || version[0] != 'v' || !number(version, i, part) || i >= version.size() ||
		version[i++] != '.' || !number(version, i, part) || i >= version.size() || version[i++] != '.' || !number(version, i, part))
		return false;
	if (i == version.size())
		return true;
	for (const char *channel : { "-alpha.", "-nightly.", "-preview." })
	{
		const std::string suffix = channel;
		if (version.compare(i, suffix.size(), suffix) == 0)
		{
			i += suffix.size();
			return number(version, i, part) && i == version.size();
		}
	}
	return false;
}

std::string BuildVersion::displayVersion(const std::string &version)
{
	// v<major>.<minor>.<patch>[-alpha.<n>], matched as a whole (the launcher's grammar, launcher/scripts/core/semver.gd)
	std::string major, minor, patch, pre;
	size_t i = 1;
	if (version.size() > 40 || version.empty() || version[0] != 'v' || !number(version, i, major) || i >= version.size() ||
		version[i++] != '.' || !number(version, i, minor) || i >= version.size() || version[i++] != '.' || !number(version, i, patch))
		return version;
	const std::string core = major + "." + minor + "." + patch;
	if (i == version.size())
		return "Alpha " + core;
	const std::string alpha = "-alpha.";
	if (version.compare(i, alpha.size(), alpha) == 0)
	{
		i += alpha.size();
		if (number(version, i, pre) && i == version.size())
			return "v" + core + " Alpha " + pre;
		return version;
	}
	// lane NIGHTLY-1 (owner, 2026-10-21): "v0.4.13-nightly.20261022" is "Nightly 0.4.13 (2026-10-22)" ("Nightly 0.4.13 build <n>" when the
	// number is not a YYYYMMDD date), as Semver.display in launcher/scripts/core/semver.gd
	const std::string nightly = "-nightly.";
	if (version.compare(i, nightly.size(), nightly) == 0)
	{
		i += nightly.size();
		if (number(version, i, pre) && i == version.size())
		{
			const long n = std::stol(pre);
			const long month = (n / 100) % 100, day = n % 100;
			if (n >= 10000000 && month >= 1 && month <= 12 && day >= 1 && day <= 31)
			{
				char date[32];
				std::snprintf(date, sizeof date, "%04ld-%02ld-%02ld", n / 10000, month, day);
				return "Nightly " + core + " (" + date + ")";
			}
			return "Nightly " + core + " build " + pre;
		}
	}
	return version;
}

std::string BuildVersion::displayLine()
{
	return std::string("OpenBFME ") + displayVersion(kVersion) + " (" + kBuildDate + ")";
}
