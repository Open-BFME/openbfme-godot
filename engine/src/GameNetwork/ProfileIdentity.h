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
//   simdata <md5>                           (lane PROFILE-2, version 2 and 3: SimulationData::digest, the game data the simulation reads)
//   pack <name> <files> <sha256>            (lane PROFILE-3, version 3 only: each asset pack the game uses, in mount order, AssetPacks::identityLines)
//   archive <n> <install>:<path> <md5> <size>   (every mounted archive, in mount order = precedence: the first one holding a file wins)
// Version 2 (lane PROFILE-2, owner decision 10-26 "B"): the digest covers every line but the archive lines, so two installs whose archives differ only
// in what the simulation never reads (textures, audio, models, the archive layout: a disc install patched with EA's patch next to an All In One one,
// S-4700) match. The archive lines stay in the text: the LAN START and a replay carry them, so a checker can name the archives that differ (owner
// decision 10-26 "A"). Retail's own check is narrower still: ZH GameEngine.cpp:338-555 (BFME1 open-bfme-1 GameEngine.cpp:367-583) CRCs the INIs loaded
// at engine init (m_iniCRC), GlobalData.cpp:992-1034 the exe and Data\Scripts\SkirmishScripts.scb / MultiplayerScripts.scb (m_exeCRC), and
// GameInfo::setMapCRC the game's map; nothing else. Version 1 (no simdata line) is the text of every build before PROFILE-2: its digest is the MD5 of
// the whole text, as it was.
// Version 3 (lane PROFILE-3, owner: permissive; agreed with MODS / SDK-1c): the digest covers only what decides whether two players can play together:
// the header, profile, engine, features (with a mod's engine features), numeric, rng, a mod's "sdk sim" line and simdata. The archive lines, a mod's
// names and content hashes ("mods <n>", "mod folder / archive / install", "modcontent") and the pack lines stay in the text for the START, a replay and
// a checker (presentationDifference), but never refuse a game: two mods, or a mod and no mod, play together when the files the simulation reads and the
// sim scripts are the same. A version 3 text without its pack lines, header 2, is its exact version 2 text (inFormatOf): an older build's replay or
// save is compared in its own format.
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
	std::string digest; ///< MD5 of identityText(), lower-case hex (version 1: of the whole text)

	// `extraFeatures`: names of enabled engine features beyond the built-in list (a test uses one to make two builds differ)
	// `profile`: the simulation profile the peer runs (lane UBER-1; Faithful writes exactly the identity text of the builds before UBER-1)
	// `modLines`: ModMountReport::identity of the mounted mod (lane MODS-1; empty = "mods none", the text of every build before it)
	// `simData` (lane PROFILE-2): SimulationData::digest of the mounted file system; a product caller always passes it (version 3). Empty writes the
	// version 1 text of the builds before PROFILE-2 (the unit tests that only exercise the other lines)
	// `packLines` (lane PROFILE-3): AssetPacks::identityLines of the packs the game uses (written with simData only; never counted)
	static ProfileIdentity compute(const std::vector<MountedArchive> &archives, RandomAlgorithm rng, const std::vector<std::string> &extraFeatures = {},
		SimulationProfile profile = SimulationProfile::Faithful, const std::vector<std::string> &modLines = {}, const std::string &simData = {},
		const std::vector<std::string> &packLines = {});
	// lane PROFILE-2 / PROFILE-3: 2 or 3 when the text has a simdata line under "openbfme-profile 2" / "openbfme-profile 3", else 1
	int version() const;
	// lane PROFILE-2 / PROFILE-3: the lines the digest covers (version 3: all but the presentation lines; version 2: all but the archive lines;
	// version 1: the whole text)
	std::string identityText() const;
	// lane PROFILE-2 / PROFILE-3: `ours` in the format of `theirs` when theirs is older (an older build's replay or save): a version 3 identity as the
	// exact version 2 text (header 2, without the pack lines), a version 2 or 3 one as the exact version 1 text (header 1, without the pack and simdata
	// lines); else `ours` unchanged
	static ProfileIdentity inFormatOf(const ProfileIdentity &ours, const ProfileIdentity &theirs);
	// lane PROFILE-2: the archive lines without their index ("<install>:<path> <md5> <size>", mount order): what a client tells the lobby
	// (OnlineLobbyClient::Options::profileArchives) and a checker compares
	std::vector<std::string> archiveLines() const;
	// lane UBER-1: the simulation profile the text names ("profile uber" -> Uber; "profile enhanced" -> Faithful)
	SimulationProfile simulationProfile() const;
	static ProfileIdentity fromText(const std::string &text);
	bool operator==(const ProfileIdentity &o) const { return digest == o.digest && identityText() == o.identityText(); }
	bool operator!=(const ProfileIdentity &o) const { return !(*this == o); }
	// "" when equal, else the first differing lines of the two identity texts ("ours: ... / theirs: ..."); a differing simdata line also names the
	// archive lines that differ (lane PROFILE-2); with a version 3 side, a differing simdata or sdk sim line also names both sides' mods (PROFILE-3)
	static std::string difference(const ProfileIdentity &ours, const ProfileIdentity &theirs);
	// lane PROFILE-3: "" when the lines that never refuse a game are the same, else what differs: "packs only ours: ...; only theirs: ...", the same for
	// "mods" and "archives" (a checker's report: what the players had, not whether they could play)
	static std::string presentationDifference(const ProfileIdentity &ours, const ProfileIdentity &theirs);
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

class ArchiveFileSystem;

// lane PROFILE-2 (owner decision 10-26 "B"): the digest of the game data the simulation reads, as the mounted file system resolves it (archive
// precedence, a mod's loose and derived files): one line "<path> <md5 of its bytes>" per file, the path lower-case with '\\' separators, sorted,
// over every file under data\ini\, data\scripts\, bases\ (the AI's base layouts) and libraries\ (the AI's script libraries), every file under
// maps\ but its pictures and strings (the map, its map.ini, scripts.lua, scriptevents.xml), and every model under art\w3d\ (PROFILE-3: the read
// trace, tests/test_profile3_retail.cpp, showed the AI's bases and libraries and the map's Lua were read but not covered). Wider than
// retail's m_iniCRC / m_exeCRC (ProfileIdentity above): every INI, not only those loaded at engine init, and every map, not only the game's.
namespace SimulationData
{
// the prefixes (lower case) and, under maps\, the extensions the digest covers
bool covers(const std::string &lowerPath);
// false + *error when a listed file cannot be read (a mount that lists a file it cannot read is broken: never skipped)
bool digest(ArchiveFileSystem &fs, std::string &out, std::string *error, size_t *files = nullptr);
// digest through a cache file (4.4 s for a pure 2.01 install on srv1: once per install, not per start): one line "<SHA-256 of the digest's
// definition + key> <digest>" per mount; `key` names the mount's bytes (its archive lines, each archive's md5 verified at mount, and its mod lines,
// with their content hashes). `cachePath` "" = no cache. A cache that cannot be read or written only costs time: *warning says so
bool cachedDigest(ArchiveFileSystem &fs, const std::string &key, const std::string &cachePath, std::string &out, std::string *error,
	std::string *warning = nullptr);
// the headless peer's cache file: OPENBFME_SIMDATA_CACHE, else $XDG_CACHE_HOME/openbfme (~/.cache/openbfme; %LOCALAPPDATA%\OpenBFME on Windows)
// /simdata-cache.tsv; "" when none of those is set
std::string defaultCachePath();
} // namespace SimulationData
