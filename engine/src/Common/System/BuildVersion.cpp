// OpenBFME. GPL-3.0.
// See Common/BuildVersion.h (lane RELEASE-1); the strings are in the generated BuildVersion.gen.cpp.

#include "Common/BuildVersion.h"

#include <cstddef>

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
	}
	return version;
}

std::string BuildVersion::displayLine()
{
	return std::string("OpenBFME ") + displayVersion(kVersion) + " (" + kBuildDate + ")";
}
