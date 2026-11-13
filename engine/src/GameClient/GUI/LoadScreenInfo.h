// OpenBFME. GPL-3.0.
//
// What the loading screen shows (LoadScreen.apt, lane START-1): the cards of the players in the game and the kind of load.  Retail's load screens read the GameInfo
// (RotWK LoadScreen init, RW 0x81CB08); the port gets one struct from the host so the screen (AptLoadScreen) can answer like retail does.
//
// TARGET FACTS (RotWK game.dat, S-001 caveat; static disassembly of the load screen class RW 0x81C5D3 .. 0x81D4xx):
//   * the cards are the occupied slots in slot order (at most 8); card c shows the Apt text records `LoadingScreen::PlayerName<c>` (the slot's display name),
//     `LoadingScreen::TeamNumber<c>` (the game text of the label "Team:<team + 1>"; "Team:0" for no team), `LoadingScreen::ArmyName<c>` (the faction's name) and
//     `LoadingScreen::Rank<c>` (blank " "); an unused card's three texts are emptied.  The movie's text fields are bound to them by name (bfme_bindAptText).
//   * the extern provider `GameLoading:PlayerColor:<c>` answers sprintf("%d", MultiplayerColor.getColor() of the slot's colour): the colour as a signed 32 bit
//     0xAARRGGBB; `GameLoadingType` answers "_lan" (game mode 1), "_skirmish" (2) or "_internetAdv" (5).
//   * the bar: processProgress(slot, percent) calls the movie's SetBarTo(card, percent) with both arguments as decimal strings; every card gets 0 at init and
//     the local slot's card gets updateLoadProgress(percent).
//   * the rank icons: the image records `UIClip/Level/<c>` (rankIconFor below: the AI level 1 / 4 / 6 / 9, a player's from the current profile's stats
//     file in the LOCAL rank table, lane RANKICON-1) and `UIClip/Fellowship/<c>` (cleared outside an online fellowship game); lane MENU-1.
//   * the template's LoadScreenMusic is the host's to play (LoadScreenSlotInfo::loadMusic, the AUDIO-1 hook; S-273).
// OpenBFME extended maps retain up to 16 cards; the retail eight-card path is unchanged.

#pragma once

#include "GameNetwork/GameInfo.h"

#include <cstdint>
#include <functional>
#include <string>

class GameTextSource;
class SkirmishProfileStore;
class SkirmishSetupSource;

constexpr int MAX_LOAD_SLOTS = MAX_SLOTS;

struct LoadScreenSlotInfo
{
	bool occupied = false;
	std::u16string playerName;
	std::u16string armyName;
	int rank = 0;
	int teamNumber = -1;     ///< 0-based team, -1 none
	std::uint32_t color = 0; ///< 0xRRGGBB
	std::string loadMusic;   ///< PlayerTemplate LoadScreenMusic of the card's faction (the AUDIO-1 hook; not played here)
	std::string levelImage;  ///< lane MENU-1: the rank icon of the Level column (UIClip/Level/<card>, rankIconFor); "" none
	bool human = false;      ///< lane MENU-1: the slot is a human player (its level is from the profile's points, S-3506)
	int rankPoints = 0;      ///< lane RANKICON-1: a human card's "<army>Points" from the current profile's stats file (0 without one)
};

struct LoadScreenInfo
{
	LoadScreenSlotInfo cards[MAX_LOAD_SLOTS];
	int mapStartCount = LEGACY_MAX_SLOTS;
	int cardCapacity() const { return mapStartCount > LEGACY_MAX_SLOTS ? MAX_LOAD_SLOTS : LEGACY_MAX_SLOTS; }
	std::string error;
	int localCard = 0;        ///< the card of the local slot (it receives the progress)
	int gameLoadingType = 0;  ///< 1 LAN, 2 skirmish, 5 internet
	std::string mapName;
	// lane RULES-1, OpenBFME (no retail field): the OpenBFME rules in force, one line ("Legendary Heroes (OpenBFME rule): 4x"); empty when none
	std::u16string rulesSummary;
	// lane RANKICON-1: where the human cards' points came from: the stats file read ("" when none was read: no profile store, or no current profile)
	std::string rankStatsPath;
	bool rankStatsRead = false; ///< a profile store was given and its stats were looked up (a missing file is 0 points, like retail)
	std::string rankStatsError; ///< the stats file could not be read (the cards show 0 points; the load screen reports it, never silent)
};

// UTF-16 to UTF-8 and back for the texts (no surrogate checks beyond pairs)
std::string loadScreenU16ToUtf8(const std::u16string &s);

// The cards of a RESOLVED skirmish GameInfo (every faction and colour is known: the load screen is made after the random choices are resolved, RW 0x62DC3E): the
// occupied slots in slot order, the faction names and the lobby colours from `setup`, the local card the first human.  `gameLoadingType` 2 (skirmish).
// Lane MENU-1: `imageExists` (a mapped image lookup) fills each card's levelImage; without one the cards have none.
// Lane RANKICON-1: `profiles` (the Skirmish profiles; null: none) gives the human cards' points: the current profile's StrategicStats.ini for a War of the
// Ring battle (game.gameMode 1), else its RealTimeStats.ini (RW 0x81CEF8 .. 0x81CFD4), each human card its own army's "<army>Points".
// `online`: an online game (retail: TheGameSpyInfo, RW 0xDE7D68): retail looks a human up in the online player records and the ONLINE table (RW 0x81CDAB ..,
// AptRankIcon0 without a record); the port has no such record on this screen, so no stats file is read and the human cards stay at 0 points (S-3506).
LoadScreenInfo makeLoadScreenInfo(const SkirmishGameInfo &game, const SkirmishSetupSource &setup, const GameTextSource *text,
	const std::function<bool(const std::string &)> &imageExists = {}, const SkirmishProfileStore *profiles = nullptr, bool online = false);

// Lane RANKICON-1: the rank-point tables (RankPointValue, 11 ints: -1, 0, then the thresholds of levels 2 .. 10). TARGET FACTS (RotWK game.dat, S-001
// caveat; BFME2 decomp RankPointValue.cpp / Rva00559A11.cpp, byte-matched):
//   * LOCAL (the profile's stats): RW 0x96054C (BFME2 Rva00559A76, 0x959A76) builds -1, 0, 5, 10, 30, 50, 150, 300, 500, 800, 1500 into the strategic
//     instance RW 0xDEBAA4 (BFME2 0xE06034) and the real-time instance RW 0xDEBAD0 (BFME2 0xE06060), the static initializers RW 0xBC921A / 0xBC9227;
//   * ONLINE (an online player record's points): RW 0x9604E7 (RotWK-only ctor; BFME2's counterpart Rva00559A11, 0x959A11) builds -1, 0, 5, 10, 30, 50,
//     150, 500, 1000, 2000, 5000 into RW 0xDEBA3C (strategic) and RW 0xDEBA70 (real-time); BFME2 0xE05FCC / 0xE06000, the same values in both editions;
//   * the load screen picks the strategic instance when TheGameInfo's game mode (+ 0x5C, RW 0xDE892C) is 1, a War of the Ring battle (RW 0x81CF24 / the
//     online branch's [ebp-0x44] == 1; BFME2 AptLoadScreenInit.cpp `gameMode == 1`), else the real-time one.
// The online path is not wired (the port has no online player records on this screen); its tables are kept so a caller names the one that applies.
enum class RankPointTable
{
	LocalStrategic, // RW 0xDEBAA4
	LocalRealTime,  // RW 0xDEBAD0
	OnlineStrategic, // RW 0xDEBA3C
	OnlineRealTime   // RW 0xDEBA70
};
RankPointTable loadScreenRankTable(bool online, bool warOfTheRing);
// the 11 entries of a table
const int *rankPointThresholds(RankPointTable table);
// RW 0x960597 (BFME2 Rva00559AC1::rva00559AC1): the index of the first threshold above `points`, less one; 10 past the last
int rankLevelForPoints(RankPointTable table, int points);

// Lane MENU-1: the Level column's rank icon of a card (LoadScreen init RW 0x81CB08). TARGET FACTS (RotWK game.dat, caveat S-001):
//   * the side index (RW 0x73BDA3) of the card's army name as ASCII (the text LoadingScreen::ArmyName<c> shows): "Goblins" (case-sensitive) is 5, else
//     the first of RW 0xDA3AC0's names Men, Elves, Dwarves, Isengard, Mordor, Wild, Angmar, Arnor, Neutral that matches without case, else 9;
//   * a computer player's level is 1 / 4 / 6 / 9 for the slot states easy / medium / hard / brutal (RW 0x81CFAA ..); a human's is rankLevelForPoints of
//     the profile's "<army>Points" (RW 0x93C1F9) in `table` (RW 0x9606E2 -> RW 0x960597; lane RANKICON-1: the load screen's skirmish branch passes a
//     LOCAL table, RW 0x81CF71 / 0x81CFCF);
//   * a human slot whose player template is not in the store (an observer: getNthPlayerTemplate RW 0x5FCAD9 is null) gets no icon (RW 0x81CF10);
//   * the icon (RW 0x960621) is the mapped image "AptRankIcon<side><level>", else "AptRankIcon<level>" (the side index 9 has no name: "(null)").
// `imageExists` answers whether a mapped image exists; `humanPoints` the profile's points for the army. `templateCount` the number of player templates
// (-1: not checked). Returns "" for a slot that gets none (an observer, a state without a level).
std::string rankIconFor(const SkirmishGameSlot &slot, const std::u16string &armyName, RankPointTable table, int humanPoints,
	const std::function<bool(const std::string &)> &imageExists, int templateCount = -1);
// the army text as retail's AsciiString of the card's UnicodeString (the low byte of each unit): the side's name and the stats key's prefix
std::string rankArmyAscii(const std::u16string &armyName);
