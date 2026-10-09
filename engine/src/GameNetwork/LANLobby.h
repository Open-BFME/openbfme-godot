// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// LANLobby (the minimal host / join flow of lane MP-1; ZH GameNetwork/LANAPI.cpp is the donor of the shape: a host announces a game, players join it and are
// given a slot, the host starts the game by sending every player the GameInfo and the players' addresses). The real APT LAN lobby (discovery, chat, the
// options UI, the retail LANMessage encoding) is NET-2 / XPLAY-2 (stop S-724); this one joins by address.
//
// Datagrams (on the game's UDP socket, before the Transport takes it over): u16 magic 0x4C42 ("BL"), u8 kind, then
//   JOIN       u8 lobby version 2, wstr name, str profile identity   (player -> host, repeated until ACCEPT / REFUSE)
//   ACCEPT     i32 slot                                     (host -> player)
//   REFUSE     str reason                                   (host -> player: the game is full or already started, another lobby version, another profile)
//   START      u8 lobby version 2, str profile identity, NewGameMessage, i32 run-ahead, i32 CRC interval, 8 x (u8 used, u32 ip, u16 port)
//                                                           (host -> every player, repeated until START_ACK)
//   START_ACK  i32 slot
// The START's address of the host's own slot is 0:0; a player uses the address it joined.
// The profile identity (GameNetwork/ProfileIdentity.h, PLAN: "peers with different identifiers refuse to play") is compared BEFORE anything is loaded: the
// host refuses a JOIN with another identity, a player refuses a START with another one (LANLobbyClient::error names the differing lines).
//
// Not simulation code (sockets and time); what it produces (the NewGameMessage) is the game every peer starts.

#pragma once

#include "GameNetwork/Network.h"
#include "GameNetwork/ProfileIdentity.h"
#include "GameNetwork/Transport.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

struct LobbyStart
{
	NewGameMessage game;
	int localSlot = -1;
	int runAhead = 2;
	int crcInterval = 100;
	std::array<NetAddress, MAX_SLOTS> addresses{}; ///< per slot, the network players' addresses (the local one is its own socket's)
	std::array<bool, MAX_SLOTS> networkSlot{};     ///< the human slots of the game
	ProfileIdentity profile;                       ///< the host's (equal to every player's: checked)
};

namespace LANLobby
{
// the encoded datagrams (exposed for the tests)
std::vector<std::uint8_t> encodeStart(const LobbyStart &s);
bool decodeStart(const std::vector<std::uint8_t> &d, LobbyStart &out, std::string *error);
bool isLobbyDatagram(const std::vector<std::uint8_t> &d);
std::vector<std::uint8_t> encodeStartAck(int slot);
} // namespace LANLobby

class LANLobbyHost
{
public:
	// `game`: the game to host; its human slots (SLOT_PLAYER) other than `hostSlot` are filled by the players who join, in the order they join (the slot's
	// name becomes the joiner's)
	LANLobbyHost(UDP &socket, const NewGameMessage &game, int hostSlot, int runAhead, int crcInterval, const ProfileIdentity &profile);
	// call often: answers JOINs; once every human slot is filled sends START (and resends it) until every player acked. True when done.
	bool poll();
	const LobbyStart &start() const { return m_start; }
	std::vector<std::string> log() const { return m_log; }

private:
	UDP &m_socket;
	LobbyStart m_start;
	std::array<bool, MAX_SLOTS> m_filled{}, m_acked{};
	bool m_started = false;
	std::uint64_t m_lastStartSent = 0;
	std::vector<std::string> m_log;
};

class LANLobbyClient
{
public:
	LANLobbyClient(UDP &socket, const NetAddress &host, const std::u16string &name, const ProfileIdentity &profile);
	// call often: JOINs until accepted, then waits for START (acks it). True when the START arrived; false + error() when refused
	bool poll();
	const std::string &error() const { return m_error; }
	const LobbyStart &start() const { return m_start; }
	int slot() const { return m_slot; }
	const NetAddress &host() const { return m_host; }

private:
	UDP &m_socket;
	NetAddress m_host;
	std::u16string m_name;
	ProfileIdentity m_profile;
	int m_slot = -1;
	bool m_started = false;
	std::uint64_t m_lastJoinSent = 0;
	std::string m_error;
	LobbyStart m_start;
};
