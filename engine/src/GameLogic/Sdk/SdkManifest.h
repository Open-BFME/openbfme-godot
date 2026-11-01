// OpenBFME. GPL-3.0.
//
// The manifest of a mod's Luau scripts (lane SDK-2, GameLogic/Sdk/LuauScriptState.h) and the fingerprint of its sim scripts.
//
// A mod's scripts are loose files of the mod folder, never inside its archives (an archive is fingerprinted as a whole, so a presentation
// script inside one could not be left out of the fingerprint):
//
//   <mod>/openbfme/sdk/mod.json                   {"sdk": 1, "name": "<shown name>", "sim": ["income"], "presentation": ["banner"]}
//   <mod>/openbfme/sdk/sim/<name>.luau            sim role scripts and the modules they require
//   <mod>/openbfme/sdk/presentation/<name>.luau   presentation role scripts and the modules they require
//
// "sim" / "presentation" list the entry scripts of each role, loaded in that order; either may be absent or empty. A script's require
// only reaches its own role's folder. The manifest is strict: an unknown key, a version other than 1, a name that is not a plain script
// name or listed twice, or a listed script that is missing is an error naming the problem; nothing is guessed.
//
// The sim fingerprint is what peers, replays and saves must share (presentation scripts are not part of it): "" when the mod has no sim
// folder and no sim entry, else the lower-case hex SHA-256 of (agreed with lane PROFILE-2 / Anti-cheat)
//   "<path> <hex sha256 of its bytes>\n" for every file below openbfme/sdk/sim/ (the mount's keys: lower case, '\\' separators), sorted
//   + "sim <name>\n" for each sim entry in the manifest's order + "sdk <version>\n".

#pragma once

#include "Common/ArchiveFileSystem.h"
#include "GameLogic/Sdk/LuauScriptState.h"

#include <map>

#include <functional>
#include <string>
#include <vector>

struct SdkManifest
{
	static const char *path() { return "openbfme/sdk/mod.json"; }
	static std::string scriptPath(SdkScriptRole role, const std::string &name); // openbfme/sdk/<role>/<name>.luau
	static const char *roleFolder(SdkScriptRole role);                           // openbfme/sdk/sim/ or openbfme/sdk/presentation/

	int sdkVersion = 0;
	std::string name;
	std::vector<std::string> sim;
	std::vector<std::string> presentation;

	// false + error naming the problem
	static bool parse(const std::string &json, SdkManifest &out, std::string &error);
};

// every file below a folder of the mod ("openbfme/sdk/sim/"): paths relative to the mod root, '/'-separated
using SdkLister = std::function<bool(const std::string &folder, std::vector<std::string> &paths, std::string &error)>;

// the sim fingerprint (see above); also checks that every listed entry (sim and presentation) exists. false + error on a missing file or a read error.
bool SdkSimFingerprint(const SdkManifest &manifest, const LuauScriptState::Reader &reader, const SdkLister &lister, std::string &out,
	std::string &error);

// The SDK's view of a mod folder: its frozen loose files (ModLooseIndex, taken and hashed at mount), by the mount's key of a path (lower
// case, '\\' separators: a mod made on Windows names its files in any case). Reading a file checks that it is still the one hashed at mount; a
// changed file is a read error, as for the rest of the mod.
class SdkModFiles
{
public:
	explicit SdkModFiles(const ModLooseIndex &index);
	static std::string key(const std::string &path); // ModLooseIndex::key: lower case, '\\' separators
	bool has(const std::string &path) const { return m_files.count(key(path)) != 0; }
	bool read(const std::string &path, std::string &out, std::string &error) const;
	LuauScriptState::Reader reader() const;
	SdkLister lister() const;
	// is `path` (any case / separators) one of the presentation scripts' files (openbfme/sdk/presentation/...)
	static bool isPresentationFile(const std::string &path);

private:
	std::map<std::string, ModLooseFile> m_files;
};
