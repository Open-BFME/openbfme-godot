// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (Common/UserPreferences.cpp, ScoreScreen.cpp's
// updateChallengeMedals, which BFME2 turned into per-map difficulty counters).
//
// RealTimeStats (lane OWNERFIX-3, the owner's playtest of 2026-10-17: "the map list's hammer icon and star column do nothing"): the battle honors of
// the skirmish map list. Each map's first column shows whether the current profile has beaten it and against which AI, and the hammer header sorts by
// that column (MpGameSetup::OnSortIcons, AptSkirmish::setMapSort). Client code: a profile file of the user data folder; nothing reaches the logic.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; the BFME2 decomp is the donor, tier A at every RW address unless said):
//   * the file (RealTimeStatsPreferences RW 0x93E72F; decomp Common/ProfilePreferences.cpp): the user data folder + the current Skirmish profile's name
//     (SkirmishPreferences RW 0x821A14: the decoded, trimmed CurrentUserName) + "RealTimeStats.ini" (the leaf static RW 0xE05E00's text), read as a
//     UserPreferences file (UserPreferences::load RW 0x7B2B7F: "key = value" lines, both trimmed, an empty key or value skipped); an empty profile name
//     reads no file (loadStatsFile RW 0x93E669: the map is cleared, nothing loaded); then the constructor sets "UseMapListTooltips" (setBool: "1");
//   * a map's counter (RW 0x93DAC6 get / RW 0x93DA51 set; decomp UserPreferences.cpp): the key "%s_%d" of the map name and the difficulty
//     (2 Easy, 3 Medium, 4 Hard, 5 Brutal, 6 every opponent Brutal), the value getInt (atoi, a missing key 0) / setInt ("%d");
//   * the map list (MpGameSetup's fill RW 0x8460B5; decomp AptMpGameSetupRefreshMapList.cpp, tier B same-shape): with the setup panel's flag bit 0
//     (+ 0x3A4; the skirmish screen passes 0x10000007, RW 0x929773) the stats of the current profile are read, and every MULTIPLAYER map (+ 0x24) gets the
//     state 6 / 5 / 4 / 3 / 2 for the highest difficulty with a non-zero counter (6 first), else 1; a map that is not multiplayer, or no stats, 0; a map that
//     is not official (+ 0x26) adds 0x8000. The images: 0 / 1 AptDifficultyNotConquered, 2 .. 6 AptDifficulty{Easy,Med,Hard,Brutal,Max}Conquered, 0x8000
//     / 0x8001 AptUserMapNotConquered, 0x8002 AptUserMapEasyConquered, 0x8003 AptDifficultyMedConquered (sic: RW 0x846278 loads the official image for the
//     user-map medium state), 0x8004 AptUserMapHardConquered, 0x8005 AptUserMapBrutalConquered, 0x8006 AptUserMapMaxConquered; the sort's state key
//     compares these numbers;
//   * the record (Rva005BF4BCProcess RW 0x9CE8F3, tier B; called by the score screen's CollectPlayerData RW 0x9270AD for the LOCAL player in a type 2 game,
//     the skirmish, RW 0x927205): nothing in a sandbox game (GameInfo::isSandbox, decomp GameSlotApparent.cpp, BFME2 0x7FF3E3, which the map does not pair with a
//     RotWK function: every other occupied slot on the local slot's team, a team below 0 is never shared); on the local player's allied victory (VictoryConditions vslot isLocalAlliedVictory) the honors (Rva005BF2C7Update RW 0x9CE6FE):
//     over the 8 slots, an AI slot (states 2 .. 5) that is not an ally (RW 0x9CE6C5: the local slot's own, or the same team number >= 0; no local slot:
//     every slot is an ally) counts by its state; an AI ally voids the record; with any counted AI the counter of the HIGHEST difficulty present becomes
//     max(old, count), where "every opponent Brutal" (the map's player count - 1 == the Brutal count, GameInfo RW 0x803C52's map cache lookup) writes
//     difficulty 6 with max(old Brutal counter, count); then the file is written (UserPreferences::write RW 0x7B274C: "%s = %s\n" per key in key order).
// INFERENCE / NOT PORTED (stop S-5041): the rest of RW 0x9CE8F3 (the wins / losses / streaks per side, the favourite side, the game times and the
// ScoreKeeper totals of the stats screens) is not written; the map name of the key is the port's map cache key with '/' turned back into '\' (retail's
// GameInfo map name, the cache key of RW 0x803C52; a user map's key outside "maps\" was not compared with a retail file); the profile name becomes the
// file name in UTF-8 (retail: a UTF-16 _wfopen path).

#pragma once

#include "GameClient/OptionPreferences.h"
#include "GameNetwork/GameInfo.h"

#include <array>
#include <string>
#include <vector>

struct MapCacheEntry;

class RealTimeStatsPreferences
{
public:
	// RW 0x93E72F: `folder` is the user data folder (ending in a separator), `profile` the current Skirmish profile ("" reads nothing). A missing file is
	// a profile that has no stats yet (true, no entries); a file that cannot be read is an error.
	bool load(const std::string &folder, const std::u16string &profile, std::string *error);
	const std::string &path() const { return m_path; }

	// UserPreferences::getInt (RW 0x7B2620: atoi; missing `defaultValue`) / setInt ("%d")
	int getInt(const std::string &key, int defaultValue) const;
	void setInt(const std::string &key, int value);
	// RW 0x93DAC6 / 0x93DA51: the "%s_%d" counter of a map and a difficulty
	int mapCounter(const std::string &statsMapName, int difficulty) const { return getInt(statsMapName + "_" + std::to_string(difficulty), 0); }
	void setMapCounter(const std::string &statsMapName, int difficulty, int value) { setInt(statsMapName + "_" + std::to_string(difficulty), value); }
	// UserPreferences::write (RW 0x7B274C; false and `*error` when it cannot be written, or when nothing was loaded: an empty profile has no file name)
	bool write(std::string *error) const;
	const OptionPreferences &values() const { return m_file; }

	// the map name of the counters for a map cache key (retail spelling: '\' separators)
	static std::string statsMapName(const std::string &cacheKey);
	// RW 0x8460B5: a map's state (0 .. 6, + 0x8000 for a map that is not official); `stats` null: no stats (the flag is clear)
	static int conquestState(const RealTimeStatsPreferences *stats, const MapCacheEntry &map);
	// RW 0x846674 ..: the MappedImage of a state ("" for a state the table has not got: retail keeps the not-conquered image's default)
	static const char *conquestImage(int state);
	static constexpr const char *kLeaf = "RealTimeStats.ini";

private:
	std::string m_path;
	OptionPreferences m_file;
};

// What the score screen's record needs of the skirmish game (retail: TheGameInfo): the slots' states and teams, the local slot, the map
struct SkirmishHonorsGame
{
	bool valid = false;
	struct Slot
	{
		int state = 1;   ///< SlotState (1 closed, 2 .. 5 AI, 6 human)
		int team = -1;   ///< the team number, -1 none
	};
	std::array<Slot, MAX_SLOTS> slots{}; ///< ONLINE16-FIX: every game slot (AptSkirmish::startGame fills MAX_SLOTS; eight overran into mapKey)
	int localSlot = -1;
	std::string mapKey;   ///< the map cache key
	int mapPlayers = -1;  ///< RW 0x803C52: the map's player count, -1 not in the cache

	bool occupied(int i) const { return slots[(size_t)i].state >= 2 && slots[(size_t)i].state <= 6; }
	bool isAI(int i) const { return slots[(size_t)i].state >= 2 && slots[(size_t)i].state <= 5; }
	// GameInfo::isSandbox (decomp GameSlotApparent.cpp, BFME2 0x7FF3E3)
	bool isSandbox() const;
	// RW 0x9CE6C5 (decomp GameInfoAllyCheck.cpp)
	bool isAlly(int i) const;
};

// RW 0x9CE8F3's honors part for the local player's game: false when nothing is recorded (sandbox, not an allied victory, an AI ally, no AI opponent);
// `*what` names the record (the difficulty and value) or why there is none
bool recordSkirmishHonors(RealTimeStatsPreferences &stats, const SkirmishHonorsGame &game, bool localAlliedVictory, std::string *what);
