// OpenBFME. GPL-3.0.
#pragma once

#include "Common/BuildVersion.h"

#include <string>

namespace RecordingBuild
{
inline bool validDate(const std::string &date)
{
	if (date.empty())
	{
		return true; // Older headers did not record a build date.
	}
	if (date.size() != 10 || date[4] != '-' || date[7] != '-')
	{
		return false;
	}
	for (size_t i = 0; i < date.size(); ++i)
	{
		if (i != 4 && i != 7 && (date[i] < '0' || date[i] > '9'))
		{
			return false;
		}
	}
	return date.substr(5, 2) >= "01" && date.substr(5, 2) <= "12" && date.substr(8, 2) >= "01" && date.substr(8, 2) <= "31";
}

inline std::string openWith(const std::string &version, const std::string &date)
{
	if (!BuildVersion::isReleaseVersion(version) || !validDate(date))
	{
		return "Made with an unknown build. Open it with the build that made it from the launcher.";
	}
	std::string label = BuildVersion::displayVersion(version);
	const std::string suffix = " (" + (date.empty() ? "build date unavailable" : date) + ")";
	if (label.size() < suffix.size() || label.compare(label.size() - suffix.size(), suffix.size(), suffix) != 0)
	{
		label += suffix;
	}
	return "Made with " + label + ". Open it with that version from the launcher.";
}
} // namespace RecordingBuild
