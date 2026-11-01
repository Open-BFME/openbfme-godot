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
//   * the rank icons: the image records `UIClip/Level/<c>` (rankIconFor below: the AI level 1 / 4 / 6 / 9, the player's from the profile's points, which
//     are not ported: level 1, S-3506) and `UIClip/Fellowship/<c>` (cleared outside an online fellowship game); lane MENU-1.
//   * the template's LoadScreenMusic is the host's to play (LoadScreenSlotInfo::loadMusic, the AUDIO-1 hook; S-273).
// OpenBFME extended maps retain up to 16 cards; the retail eight-card path is unchanged.

#pragma once

#include "GameNetwork/GameInfo.h"

#include <cstdint>
#include <functional>
#include <string>

class GameTextSource;
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
};

// UTF-16 to UTF-8 and back for the texts (no surrogate checks beyond pairs)
std::string loadScreenU16ToUtf8(const std::u16string &s);

// The cards of a RESOLVED skirmish GameInfo (every faction and colour is known: the load screen is made after the random choices are resolved, RW 0x62DC3E): the
// occupied slots in slot order, the faction names and the lobby colours from `setup`, the local card the first human.  `gameLoadingType` 2 (skirmish).
// Lane MENU-1: `imageExists` (a mapped image lookup) fills each card's levelImage; without one the cards have none.
LoadScreenInfo makeLoadScreenInfo(const SkirmishGameInfo &game, const SkirmishSetupSource &setup, const GameTextSource *text,
	const std::function<bool(const std::string &)> &imageExists = {});

// Lane MENU-1: the Level column's rank icon of a card (LoadScreen init RW 0x81CB08). TARGET FACTS (RotWK game.dat, caveat S-001):
//   * the side index (RW 0x73BDA3) of the card's army name as ASCII (the text LoadingScreen::ArmyName<c> shows): "Goblins" (case-sensitive) is 5, else
//     the first of RW 0xDA3AC0's names Men, Elves, Dwarves, Isengard, Mordor, Wild, Angmar, Arnor, Neutral that matches without case, else 9;
//   * a computer player's level is 1 / 4 / 6 / 9 for the slot states easy / medium / hard / brutal (RW 0x81CFAA ..); a human's is the rank of the
//     profile's "<army>Points" (RW 0x93C1F9) in the thresholds 0, 5, 10, 30, 50, 150, 500, 1000, 2000, 5000 (RW 0x9604E7, RW 0x960597: the first
//     threshold above the points, less one; 10 past the last);
//   * the icon (RW 0x960621) is the mapped image "AptRankIcon<side><level>", else "AptRankIcon<level>" (the side index 9 has no name: "(null)").
// `imageExists` answers whether a mapped image exists; `humanPoints` the profile's points for the army (stop S-3506: the stats files are not ported,
// the port passes 0: a level 1 icon). Returns "" for a slot that gets none (an observer, a state without a level).
std::string rankIconFor(const SkirmishGameSlot &slot, const std::u16string &armyName, int humanPoints, const std::function<bool(const std::string &)> &imageExists);
