// OpenBFME. GPL-3.0.
//
// Lane HUD-5: what the players screen's Status page shows (PlayerTribute.apt, page "StatusPage"; the owner's report: every cell showed its own text record
// name). Retail's page reads TheGameInfo and the live players; the port gets the rows from the host (built here from the resolved GameInfo and the live
// state) so AptPlayerTribute answers like retail.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; static disassembly, BFME2 decomp map tier A for every function below):
//   * the rows (RW 0x9151C3 = BFME2 0x90EEDC, decomp AptRowListCallbacks.cpp rva0050EEDC): slots 0..7 in order; a slot counts when it is occupied, its player
//     name (GameSlot + 0x34) is not empty and ThePlayerList has a player with that name; row n = (slot, player). NumOfPlayers (RW 0x9166AC registers it) is the
//     row count; InSkirmish is "1" when TheGameLogic + 0x110 == 2.
//   * a row's texts (the row constructor RW 0x915BF6, BFME2 0x90F909 tier A, no decomp C++): field 0 the slot's name (GameSlot + 0x30); field 1
//     GameSlot::getApparentPlayerTemplateDisplayName (RW 0x80147D, decomp GameSlotApparent.cpp: GUI:Random for an enemy's random faction while
//     MultiplayerSettings ShowRandomPlayerTemplate, GUI:Observer for an observer, else the faction's DisplayName); field 2 the game text of "Team:%d" with
//     team + 1, or "Team:AI" when the slot is a computer and has no team (-1); field 3: a human slot of a network game that TheNetwork (vslot 0xCC) reports
//     disconnected is GUI:PlayerObserverGone / GUI:PlayerGone (observer template or not, Player + 0x35A = PlayerTemplate + 0x150, Player::init RW 0x6B063C);
//     otherwise GUI:PlayerAlive unless TheVictoryConditions vslot 0x40 (hasSinglePlayerBeenDefeated) says defeated, then GUI:PlayerObserver / GUI:PlayerDead.
//   * the row's colour extern "_level<n>.<row>_color" (RW 0x914A69): sprintf("%d") of the colour of GameSlot RW 0x801211 (RotWK only): the observer colour for an
//     observer, the original colour for an enemy while ShowRandomColor, else the slot's colour; MultiplayerSettings::getColor(index) + 0x10.
// INFERENCE / port: TheVictoryConditions vslot 0x40 counts every true answer in retail (a hashed counter, S-1063's family); the screen asks the const query
// (VictoryConditions::wouldBeDefeated) so opening it never changes the state hash. The random / observer colours of MultiplayerSettings (+0x84 / +0x44) are
// constructed in retail; their value is not read (stop S-1952): `randomColor` / `observerColor` are the host's.

#pragma once

#include "GameNetwork/GameInfo.h"

#include <cstdint>
#include <string>
#include <vector>

class GameTextSource;
class SkirmishSetupSource;

struct PlayerStatusRow
{
	int slot = -1;
	std::string fields[4]; ///< UTF-8: the player name, army, team, status (the records APT:_level<n>.<row>_field0 .. 3)
	std::int32_t color = 0; ///< what the row's _color extern prints (the colour as a signed 32 bit 0xAARRGGBB)
};

struct PlayerStatusInfo
{
	std::vector<PlayerStatusRow> rows;
	bool inSkirmish = false; ///< TheGameLogic + 0x110 == 2
};

// The live facts of one slot (the host reads them from the logic and the network)
struct PlayerStatusSlotState
{
	bool hasPlayer = false;     ///< ThePlayerList has the slot's player (the slot's player name is not empty)
	bool defeated = false;      ///< TheVictoryConditions says the player is defeated
	bool observer = false;      ///< the player's template is an observer (Player + 0x35A)
	bool connected = true;      ///< TheNetwork vslot 0xCC (only asked for a human slot of a network game)
};

struct PlayerStatusInput
{
	const SkirmishGameInfo *game = nullptr; ///< the resolved game info (with its orig* choices)
	int localSlot = -1;                     ///< TheGameInfo's local slot
	PlayerStatusSlotState slots[MAX_SLOTS];
	bool network = false;                   ///< TheNetwork exists (a LAN / online game)
	int gameMode = 2;                       ///< TheGameLogic + 0x110
	bool showRandomPlayerTemplate = true, showRandomColor = true; ///< MultiplayerSettings (VisionSettings carries them)
	std::uint32_t randomColor = 0xFFFFFFFFu, observerColor = 0xFFFFFFFFu; ///< MultiplayerSettings +0x84 / +0x44 colours (S-1952)
};

// The rows of the Status page (RW 0x9151C3 + RW 0x915BF6). `setup` gives the factions' DisplayName labels and the colours; `text` the game text.
PlayerStatusInfo makePlayerStatusInfo(const PlayerStatusInput &in, const SkirmishSetupSource &setup, const GameTextSource *text);
