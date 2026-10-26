// OpenBFME. GPL-3.0.
//
// Lane JOINVER-1 (the owner, 2026-10-23: "it says the build is different and gives me a hash but not a useful answer like which client version
// they have, is it official, etc what operating system"): an online player's build as players read it, and the join refusal of a game whose
// build differs (the lobby's 409 "profile", lobby.mjs join), said in plain words: each side's version, whether it is an official release, its
// system, and what to do. The profile ids (ProfileIdentity.h) stay in the details text only.
#pragma once

#include <string>

namespace JoinBuild
{
// what a client tells about its build (lobby.mjs `build`: create / join bodies, the game list's host build, the 409's host_build / your_build)
struct Build
{
	std::string version;  ///< the release tag (BuildVersion::kVersion: "v0.4.12-nightly.20261022", "v0.4.11", "v0.4.5-3-g0123456789-dirty")
	std::string date;     ///< "YYYY-MM-DD" (BuildVersion::kBuildDate), "" when not told
	std::string platform; ///< "windows", "linux", "macos", "" when not told
	int official = -1;    ///< 1 an official release (a packaged build with the release key and a release tag), 0 not, -1 not told
	bool known() const { return !version.empty(); }
};

// a release tag's shape: "v<x>.<y>.<z>" or "v<x>.<y>.<z>-<alpha|beta|preview|nightly>.<n>" (a development build's describe output is not one)
bool releaseTag(const std::string &version);

// "Nightly 0.4.12 (2026-10-22), official release, Windows"; "a development build (v0.4.5-3-g0123456789, built 2026-10-22), unofficial,
// Linux"; "an unknown version" when nothing was told
std::string describe(const Build &b);

// -1 a is older, 0 the same release, 1 a is newer; `comparable` false when either is not a release tag (then 0)
int compare(const std::string &a, const std::string &b, bool *comparable);

// the box's text (lines separated by '\n'): the refusal, the host's build, the player's, what to do. `host` may be unknown (an older lobby that
// sends no build): the player's own build and the general rule are said
std::string refusalText(const Build &mine, const Build &host);

// the DETAILS text (copied to the clipboard): both raw tags, build days, systems and profile ids, for a bug report
std::string detailsText(const Build &mine, const std::string &myProfile, const Build &host, const std::string &hostProfile);
} // namespace JoinBuild
