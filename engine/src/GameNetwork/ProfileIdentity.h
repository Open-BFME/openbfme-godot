// OpenBFME. GPL-3.0.
//
// ProfileIdentity (lane MP-1; PLAN "Two profiles": "A profile has an immutable internal identifier. It covers engine features, the numeric mode and the
// effective mounted data: archive hashes, the contents of every directory mod, and the final mount order and precedence." Enhanced profile: "our own
// formats carry the identifier directly, and peers with different identifiers refuse to play.")
//
// The identity is a canonical text, one fact per line, and its MD5:
//   openbfme-profile 1
//   profile enhanced
//   engine <the engine compatibility id: SHA-256 of the canonical manifest of the simulation sources, data and configuration (cmake/EngineId.cmake), or the
//           release pipeline's -DOPENBFME_ENGINE_ID; the Git commit is provenance only (ProfileIdentity::gitProvenance), never part of the identity>
//   features <the owned format and rule versions: net wire, GameMessage encoding, replay, lobby, state hash; plus any extra feature names, sorted>
//   numeric <the numeric mode: IEEE binary32 / binary64 through the facade, canonical MXCSR 0x1F80, no FMA contraction (cmake/SimFp.cmake)>
//   rng <the logic RNG algorithm>
//   mods none                               (this build mounts no directory mod: a mod lane adds one line per mod with its content hash)
//   archive <n> <install>:<path> <md5> <size>   (every mounted archive, in mount order = precedence: the first one holding a file wins)
// Peers compare it in the lobby (JOIN and START, GameNetwork/LANLobby.h) before anything is loaded, and a replay carries it (Common/Recorder.h); a
// different identity is refused, naming the first differing lines.
//
// Not simulation code: it is compared, never part of the logic state.

#pragma once

#include "Common/RandomValue.h"
#include "Common/RetailArchivePolicy.h"

#include <string>
#include <vector>

struct ProfileIdentity
{
	std::string text;
	std::string digest; ///< MD5 of text, lower-case hex

	// `extraFeatures`: names of enabled engine features beyond the built-in list (a test uses one to make two builds differ)
	static ProfileIdentity compute(const std::vector<MountedArchive> &archives, RandomAlgorithm rng, const std::vector<std::string> &extraFeatures = {});
	static ProfileIdentity fromText(const std::string &text);
	bool operator==(const ProfileIdentity &o) const { return digest == o.digest && text == o.text; }
	bool operator!=(const ProfileIdentity &o) const { return !(*this == o); }
	// "" when equal, else the first differing lines of the two texts ("ours: ... / theirs: ...")
	static std::string difference(const ProfileIdentity &ours, const ProfileIdentity &theirs);
	// the build's engine compatibility id (OPENBFME_ENGINE_ID)
	static const char *buildId();
	// the commit the build was configured from ("none" without Git): provenance for reports, not identity
	static const char *gitProvenance();
};
