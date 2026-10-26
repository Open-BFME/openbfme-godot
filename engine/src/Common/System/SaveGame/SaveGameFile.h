// OpenBFME. GPL-3.0.
//
// SaveGameFile (lane SAVE-2, docs/SAVES.md section 2 "one container, three parts"): OpenBFME's own save file (.obfmesave). A save is the game's command log
// up to frame F (the running replay, Common/Recorder.h) with a header that names the build, the data and the game; loading replays the log to F at full
// speed (GameClient/CommandLogLoad.h) and hands the game to the player there. The snapshot part holds SAVE-4's Xfer snapshot (lane SAVE-4a: GameState's
// stream, Common/GameState.h); no build writes one until every simulation block has its xfer.
//
// TARGET FACTS (RotWK game.dat, caveat S-001), what is kept of retail's save: the files live in the user data folder's Save\ (GameState RW 0x6DE38D /
// UserDataFolder::heroSaveFolder), one file per save named by the player (FileNameTextEntry, at most 40 characters, RW 0x817667), the extension says the kind
// (RW 0x6DCDE9: .BfME2Skirmish for mode 2, .BfME2Campaign otherwise); the save list reads each file's header only (GameState::getSaveGameInfoFromFile,
// BFME2 decomp GameStateAutoSave.cpp, tier A for RW 0x6E04AD's caller). OpenBFME DIFFERENCES (owner decision 10, SAVES.md): our own extension .obfmesave in
// the same folder (retail never lists a file it cannot read; retail's files are listed by us as "import" entries), our own container (the retail
// Xfer format is SAVE-7 / SAVE-8).
//
// Layout (little-endian, NetPacket's byte writer):
//   "OBFMESAV"                          8 bytes
//   u32 container version               1
//   three parts, in this order: tag "HEAD", "LOG ", "SNAP"; each: 4-byte tag, u32 length, 32-byte SHA-256 of the payload, payload
//   HEAD payload: u32 header version 1, str build version (BuildVersion::kVersion), str engine id (ProfileIdentity::buildId), str commit (provenance), str
//                 profile identity text, str execution profile (the identity's "profile" line), u32 game kind (SaveLoad.apt's type: 1 Campaign, 2 Skirmish),
//                 u32 frame F, u32 state hash at F, str description (the name the player typed, UTF-8), str map (path), str map display label, str date
//                 (YYYY-MM-DD), str time (HH:MM:SS), u32 commands in the log, str campaign ("" for a skirmish), i32 mission index, i32 difficulty
//                 (the execution profile switches of a game loaded under the other profile are records of its log, Common/Recorder.h)
//                 header version 2 (lane CAMPSAVE-1) adds: u32 save point (0 a game at frame F; 1 the campaign menu between missions: game kind 1,
//                 frame 0, no commands, empty LOG and SNAP parts), u8 won, u8 ready, u8 advanced (the campaign's + 0xD0 / + 0xD2 / + 0xD3,
//                 GameClient/LinearCampaign.h), u8 auto-save (retail's IsAutoSaveOrNot, RW 0x6DE8F1: the Load page's AutoSaveList); each 0 or 1 and
//                 0 for a game; version 1 headers still load (a game, no flags)
//                 header version 3 adds: str UTC build date (YYYY-MM-DD, BuildVersion::kBuildDate); absent in versions 1 and 2
//   LOG payload:  an OpenBFME replay (format 4) whose end record is (F, hash at F)
//   SNAP payload: empty, or the snapshot at F (Common/GameState.h's stream, SAVE-4a); read and digest-checked like the other parts (at most
//                 kMaxSnapshotBytes); loading a save that carries one is refused by CommandLogLoad until the snapshot loads (SAVE-4b..d)
// Reading never defaults: a bad magic, an unknown version, a part out of order, a truncated part or a digest that does not match is a named error.
//
// Not simulation code: it is read and written around the game, never part of the logic state.

#pragma once

#include "GameNetwork/ProfileIdentity.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

struct SaveGameHeader
{
	std::string buildVersion; ///< BuildVersion::kVersion of the build that saved ("v0.3.0-preview.1-4-g0123456789")
	std::string engineId;     ///< ProfileIdentity::buildId(): the engine compatibility id; a log replays only in the build with the same id
	std::string commit;       ///< provenance only
	std::string buildDate;    ///< header version 3: UTC build day, not the local save date; empty in older headers
	ProfileIdentity profile;  ///< the data, rules and execution profile the game ran under
	std::string executionProfile; ///< the profile the game ran at F (the identity's "profile" line value: "enhanced" for Faithful, UBER-1's "uber")
	int gameKind = 2;         ///< 1 Campaign, 2 Skirmish (SaveLoad.apt's game types, RW 0x816164)
	std::uint32_t frame = 0;  ///< F: the logic frame the save describes (every batch below F ran)
	std::uint32_t stateHash = 0; ///< GameLogic::computeStateHash at F
	std::string description;  ///< the save's name as the player typed it (UTF-8)
	std::string mapPath;      ///< the map (the NewGameMessage's map name)
	std::string mapDisplay;   ///< the map cache's display label of the map (retail SaveGameInfo's mapLabel; the Load page resolves it)
	std::string date, time;   ///< when it was saved (local time, "YYYY-MM-DD", "HH:MM:SS")
	std::uint32_t commands = 0; ///< commands in the log (information)
	// a campaign mission (game kind 1): what starts it again (GameWorld::start_campaign_mission); empty for a skirmish, whose log's NewGameMessage starts it
	std::string campaign;
	int campaignMission = 0, campaignDifficulty = 1;
	// lane CAMPSAVE-1 (header version 2): where the save was made and the campaign's flags there
	int savePoint = 0;             ///< 0 a game at frame F, 1 CampaignMenu.apt between missions (the auto-save, the menu's Save)
	bool campaignVictorious = false, campaignReady = false, campaignAdvanced = false;
	bool autoSave = false;         ///< the auto-save after a won mission (RW 0x6DE8F1: SaveGameInfo IsAutoSaveOrNot = 1)
};

struct SaveGameFile
{
	static constexpr std::uint32_t kContainerVersion = 1;
	static constexpr std::uint32_t kHeaderVersion = 3; // 3 adds build date; 1 and 2 still read
	static constexpr const char *kExtension = ".obfmesave";
	// the most a part may declare (checked before it is read: a larger one is a named refusal, never an allocation)
	static constexpr std::uint64_t kMaxHeaderBytes = 1u << 20;  // 1 MiB (an identity text and a few names)
	static constexpr std::uint64_t kMaxLogBytes = 256u << 20;   // 256 MiB (a 60-minute 8-player log is a few MiB)
	static constexpr std::uint64_t kMaxSnapshotBytes = 256u << 20; // 256 MiB (lane SAVE-4a; the measured core is far below, docs/SAVES.md section 8)

	SaveGameHeader header;
	std::vector<std::uint8_t> log;      ///< the replay bytes up to F
	std::vector<std::uint8_t> snapshot; ///< GameState's snapshot stream at F, or empty (lane SAVE-4a)

	std::vector<std::uint8_t> encode() const;
	// headerOnly: the HEAD part is read and checked, the other parts only skipped by their length (the Load page's listing); false + *error otherwise
	static bool parse(const std::vector<std::uint8_t> &bytes, SaveGameFile &out, std::string *error, bool headerOnly = false);
	static bool load(const std::string &path, SaveGameFile &out, std::string *error, bool headerOnly = false);
	class Source;
	static bool parseFrom(Source &src, SaveGameFile &out, std::string *error, bool headerOnly);
	// written to "<path>.tmp" first, then renamed over `path` (an existing save is replaced only once the new one is complete)
	bool write(const std::string &path, std::string *error) const;

	// the "profile" line's value of an identity text ("" without one)
	static std::string executionProfileOf(const std::string &identityText);
};

// Whether a save may be loaded by the running build (owner decisions, 2026-10-12: a save loads only in the build that made it, refused with a message
// naming that build; in single player a save of one execution profile may load under the other).
struct SaveGameCompatibility
{
	bool loadable = false;
	std::string refusal;    ///< why not (shown on the Load page and in the report)
	bool crossProfile = false; ///< saved under another execution profile: the log replays under the saved profile, then the game goes on under this one
	std::string note;       ///< what the player is told about a cross-profile load

	// lane CAMPSAVE-1: the campaign menu save point (no log): the data identity must match (the engine id and execution profile need not), and
	// `campaignRefusal` (CampaignProgress::menuSaveRefusal, "" when the campaign, mission and difficulty exist) is the other reason
	static SaveGameCompatibility checkMenuSave(const SaveGameHeader &save, const ProfileIdentity &running, const std::string &campaignRefusal);
	static SaveGameCompatibility check(const SaveGameHeader &save, const ProfileIdentity &running, const std::string &runningEngineId,
		const std::string &runningBuildVersion, bool multiplayer);
};
