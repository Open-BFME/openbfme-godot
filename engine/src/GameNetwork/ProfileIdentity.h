// OpenBFME. GPL-3.0.
//
// ProfileIdentity (lane MP-1; PLAN "Two profiles": "A profile has an immutable internal identifier. It covers engine features, the numeric mode and the
// effective mounted data: archive hashes, the contents of every directory mod, and the final mount order and precedence." Enhanced profile: "our own
// formats carry the identifier directly, and peers with different identifiers refuse to play.")
//
// The identity is a canonical text, one fact per line, and its MD5:
//   openbfme-profile 1
//   profile enhanced                        (lane UBER-1: "profile uber" for the Uber performance mode, Common/SimulationProfile.h: an Uber peer never
//                                            matches a Faithful one, so the lobby refuses the mix and a replay of the other profile does not play)
//   engine <the engine compatibility id: SHA-256 of the canonical manifest of the simulation sources, data and configuration (cmake/EngineId.cmake), or the
//           release pipeline's -DOPENBFME_ENGINE_ID; the Git commit is provenance only (ProfileIdentity::gitProvenance), never part of the identity>
//   features <the owned format and rule versions: net wire, GameMessage encoding, replay, lobby, state hash; plus any extra feature names, sorted>
//   numeric <the numeric mode: IEEE binary32 / binary64 through the facade, canonical MXCSR 0x1F80, no FMA contraction (cmake/SimFp.cmake)>
//   rng <the logic RNG algorithm>
//   mods none                               (no mod mounted; lane MODS-1: with `-mod`, "mods <n>" and the mod's own lines, Common/ModMount.h:
//                                            "mod folder <name>" / "mod archive <name>", "modcontent <n> archives <m> files <md5>": the MD5 of the
//                                            mod's manifest, every archive in mount order and every loose file, ModMountReport::manifest; bounded)
//   archive <n> <install>:<path> <md5> <size>   (every mounted archive, in mount order = precedence: the first one holding a file wins)
// Peers compare it in the lobby (JOIN and START, GameNetwork/LANLobby.h) before anything is loaded, and a replay carries it (Common/Recorder.h); a
// different identity is refused, naming the first differing lines.
//
// Not simulation code: it is compared, never part of the logic state.

#pragma once

#include "Common/RandomValue.h"
#include "Common/RetailArchivePolicy.h"
#include "Common/SimulationProfile.h"

#include <string>
#include <vector>

struct ProfileIdentity
{
	std::string text;
	std::string digest; ///< MD5 of text, lower-case hex

	// `extraFeatures`: names of enabled engine features beyond the built-in list (a test uses one to make two builds differ)
	// `profile`: the simulation profile the peer runs (lane UBER-1; Faithful writes exactly the identity text of the builds before UBER-1)
	// `modLines`: ModMountReport::identity of the mounted mod (lane MODS-1; empty = "mods none", the text of every build before it)
	static ProfileIdentity compute(const std::vector<MountedArchive> &archives, RandomAlgorithm rng, const std::vector<std::string> &extraFeatures = {},
		SimulationProfile profile = SimulationProfile::Faithful, const std::vector<std::string> &modLines = {});
	// lane UBER-1: the simulation profile the text names ("profile uber" -> Uber; "profile enhanced" -> Faithful)
	SimulationProfile simulationProfile() const;
	static ProfileIdentity fromText(const std::string &text);
	bool operator==(const ProfileIdentity &o) const { return digest == o.digest && text == o.text; }
	bool operator!=(const ProfileIdentity &o) const { return !(*this == o); }
	// "" when equal, else the first differing lines of the two texts ("ours: ... / theirs: ...")
	static std::string difference(const ProfileIdentity &ours, const ProfileIdentity &theirs);
	// lane NETLAG-1: REPLAY playback only (a live peer, a lobby JOIN / START and a save still compare the whole identity): true when `theirs` is `ours` in
	// every line but its `engine` line and that engine id is one a recording was verified to play back under (verifiedReplayEngines): older builds' recordings
	// keep playing after the engine id moved with the sources; the features line may also differ in its `net-wire` entry alone (the live datagram format,
	// which a recording never reads: BATCH-P4). `engine` receives theirs' id
	static bool replayCompatible(const ProfileIdentity &ours, const ProfileIdentity &theirs, std::string *engine = nullptr);
	// the engine ids of older builds whose recordings are known to play back on this one, each with the evidence (the fixture test that plays one)
	struct VerifiedEngine
	{
		const char *id, *note;
	};
	static const std::vector<VerifiedEngine> &verifiedReplayEngines();
	// the build's engine compatibility id (OPENBFME_ENGINE_ID)
	static const char *buildId();
	// the commit the build was configured from ("none" without Git): provenance for reports, not identity
	static const char *gitProvenance();
};
