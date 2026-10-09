// OpenBFME. GPL-3.0.
//
// Lane RELEASE-1: what build this is (shown on the main menu, written at the top of every log, in the package names).
// The strings are generated at build time by engine/cmake/BuildVersion.cmake (git describe + the UTC build day).

#pragma once

#include <string>

namespace BuildVersion
{
extern const char *const kVersion;   // e.g. "v0.3.0-preview.1-4-g0123456789" or "0af8b6b84c-dirty"; "untracked" without Git
extern const char *const kCommit;    // the full commit hash, "none" without Git
extern const char *const kBuildDate; // "YYYY-MM-DD" (UTC)
// the provenance line tools/release/package.sh checks (engine/cmake/BuildVersion.cmake): version, commit, dirty, engine id
extern const char *const kBuildRecord;

// "OpenBFME <version> (<build date>)": the one line every log starts with and the main menu shows.
std::string versionLine();
} // namespace BuildVersion
