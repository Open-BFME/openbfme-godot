// OpenBFME. GPL-3.0.
// See Common/BuildVersion.h (lane RELEASE-1); the strings are in the generated BuildVersion.gen.cpp.

#include "Common/BuildVersion.h"

std::string BuildVersion::versionLine()
{
	return std::string("OpenBFME ") + kVersion + " (" + kBuildDate + ")";
}
