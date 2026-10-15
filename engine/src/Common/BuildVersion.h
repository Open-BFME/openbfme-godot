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

// "OpenBFME <version> (<build date>)": the one line every log starts with (exact: tools and bug reports read the tag).
std::string versionLine();

// Lane VERSION-1 (owner, 2026-10-14): a version as players read it. A plain release tag "v0.4.5" is "Alpha 0.4.5", an alpha build
// "v0.4.0-alpha.3" is "v0.4.0 Alpha 3" (its release title); anything else (a preview, a development build "v0.4.5-3-g0123456789",
// "0af8b6b84c-dirty") is returned as it is. Same rules as launcher/scripts/core/semver.gd display() and tools/release/autorelease.py
// display_name(): numbers are digits without leading zeros, at most 9 of them.
std::string displayVersion(const std::string &version);

// "OpenBFME Alpha 0.4.5 (<build date>)": the line the main menu's watermark and the first-run screen show.
std::string displayLine();
} // namespace BuildVersion
