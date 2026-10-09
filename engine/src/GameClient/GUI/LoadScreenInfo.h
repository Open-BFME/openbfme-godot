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
//   * the rank icons (clips `UIClip/Level/<c>` and `UIClip/Fellowship/<c>` set to an image, from the AI level 1 / 4 / 6 / 9 or the profile) and the template's
//     LoadScreenMusic playing during the load are not shown here (S-273).

#pragma once

#include "GameNetwork/GameInfo.h"

#include <cstdint>
#include <string>

class GameTextSource;
class SkirmishSetupSource;

constexpr int MAX_LOAD_SLOTS = 8;

struct LoadScreenSlotInfo
{
	bool occupied = false;
	std::u16string playerName;
	std::u16string armyName;
	int rank = 0;
	int teamNumber = -1;     ///< 0-based team, -1 none
	std::uint32_t color = 0; ///< 0xRRGGBB
	std::string loadMusic;   ///< PlayerTemplate LoadScreenMusic of the card's faction (the AUDIO-1 hook; not played here)
};

struct LoadScreenInfo
{
	LoadScreenSlotInfo cards[MAX_LOAD_SLOTS];
	int localCard = 0;        ///< the card of the local slot (it receives the progress)
	int gameLoadingType = 0;  ///< 1 LAN, 2 skirmish, 5 internet
	std::string mapName;
};

// UTF-16 to UTF-8 and back for the texts (no surrogate checks beyond pairs)
std::string loadScreenU16ToUtf8(const std::u16string &s);

// The cards of a RESOLVED skirmish GameInfo (every faction and colour is known: the load screen is made after the random choices are resolved, RW 0x62DC3E): the
// occupied slots in slot order, the faction names and the lobby colours from `setup`, the local card the first human.  `gameLoadingType` 2 (skirmish).
LoadScreenInfo makeLoadScreenInfo(const SkirmishGameInfo &game, const SkirmishSetupSource &setup, const GameTextSource *text);
