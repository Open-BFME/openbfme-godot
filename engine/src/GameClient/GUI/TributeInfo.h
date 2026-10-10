// OpenBFME. GPL-3.0.
//
// TributeSource (lane PLAY-1): what PlayerTribute.apt's tribute page (AptPlayerTribute, the Palantir's flag) reads of the live game (its status page reads
// HUD-5's PlayerStatusInfo) and the one thing it does to it.
// Retail's screen reads TheGameInfo's slots, ThePlayerList and TheVictoryConditions directly and appends MSG_GIVE_MONEY to TheMessageStream; the port's
// shell is given this interface by the game's host (GodotDevice/GodotGameWorld) so the screen can be built in a test with exactly the data it is given.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

// one player of the tribute page (RW 0x914B54: the local player first, then the active allies, at most 7)
struct TributePlayerRow
{
	int playerIndex = -1;     ///< Player + 0x54
	std::string name;         ///< field 0: the player's display name (UTF-8, Player::getPlayerDisplayName)
	std::uint32_t cash = 0;   ///< Player + 0x94 (field 1 shows the cash with the pending amounts, RW 0x9155C2)
	bool local = false;
	bool active = true;       ///< RW 0x6AAC52
	std::uint32_t color = 0;  ///< Player + 0x2A0 (row + 0x58, RW 0x915F5D: the extern "<row>_color")
};

class TributeSource
{
public:
	virtual ~TributeSource() = default;
	virtual bool localActive() const = 0;                   ///< the "_TributeEnabled" extern (RW 0x914C08 -> RW 0x6AAC52 on the local player)
	virtual bool transferAllowed() const = 0;               ///< RW 0x626087: NumMinutesBeforePlayersCanTransferMoney have passed (the page's "_enabled")
	virtual std::vector<TributePlayerRow> tributePlayers() = 0;
	// the screen's Send (RW 0x914DD6): MSG_GIVE_MONEY(from, to, amount) through the command path the HUD's orders take (lockstep in a network game)
	virtual void send(int fromIndex, int toIndex, std::uint32_t amount) = 0;
};
