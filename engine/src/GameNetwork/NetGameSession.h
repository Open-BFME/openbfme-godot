// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// NetGameSession (lane MP-1): one peer's network game around a loaded LiveGame: the Transport on the lobby's socket, the Network, the LockstepDriver
// (with the replay recording), and the load barrier (ZH NETCOMMANDTYPE_LOADCOMPLETE: no peer runs frame 0 before every peer loaded the map).
// Used by the headless peer (Main/openbfme_peer.cpp) and the Godot game world (GodotDevice/GodotGameWorld.cpp).
//
// Every peer starts the game from the same lobby START: the same NewGameMessage (GameInfo, seed), resolved by NewGame::prepareNewGame on every peer (the
// resolution is a pure function of the message), with the peer's own slot as the local one (GameInfo::getLocalSlotNum; it decides only which player is
// the local one, never hashed: PlayerList::crc). The slot -> player index map of the Network is read from the loaded game (the side of each slot).

#pragma once

#include "Common/Recorder.h"
#include "GameClient/LiveGame.h"
#include "GameNetwork/LANLobby.h"
#include "GameNetwork/LockstepDriver.h"
#include "GameNetwork/Network.h"
#include "GameNetwork/Transport.h"

#include <memory>
#include <string>

class NetGameSession
{
public:
	struct Options
	{
		std::string replayPath; ///< "" = no recording
		Transport::Options transport;
		RandomAlgorithm algorithm = RandomAlgorithm::ZH_CarryChain;
		ProfileIdentity profile; ///< recorded in the replay (the lobby already checked it is every peer's)
		NetworkSettings network; ///< lane MP-2: GameData's network timing (NetworkSettings::load), the disconnect path's clocks
		// lane MP-2: where a desync dump goes (GameNetwork/DesyncDump.h; "" = the current directory) and the names in its file name
		std::string desyncDirectory;
		std::string exeName = "openbfme";
		std::string playerName;
		bool adaptiveRunAhead = true; ///< lane MP-2: the local command delay follows the measured round trip (Network::updateRunAhead)
	};
	NetGameSession(UDP &socket, const LobbyStart &start, const Options &options);
	~NetGameSession();

	// after game.load(): the slot -> player map, the network objects, the driver installed on `game`, LOADCOMPLETE sent. False + *error when a network
	// slot has no player in the game or the replay cannot be written.
	bool begin(LiveGame &game, std::string *error);
	// the load barrier: true once every network player loaded (call often before advancing the game)
	bool pollLoaded();
	// services the transport and answers late lobby START datagrams (a lost START_ACK); call every tick
	void service();
	// the local player stops (ZH leave); the replay gets its end record
	void finish(LiveGame &game);
	// true when every command sent so far was acked by every peer (a finished peer stays until then)
	bool allAcked() const;
	// lane MP-2: the disconnect path asks the local player to leave (dropped by the others, its own connection failed, the last one left, or Quit on the
	// disconnect screen); the caller ends the game (finish) and reports the reason
	bool quitRequested() const { return m_network && m_network->quitRequested(); }
	// lane MP-2: the desync dumps written so far (one file per desync, rewritten as the other halves arrive) and the failures to write one
	const std::vector<std::string> &desyncDumps() const { return m_desyncDumps; }
	const std::vector<std::string> &desyncDumpErrors() const { return m_desyncDumpErrors; }
	std::string quitReason() const { return m_network ? m_network->quitReason() : std::string(); }
	// the recording's errors (a command that could not be encoded, a failed write): never silent
	std::vector<std::string> recordingErrors() const { return m_writer ? m_writer->errors() : std::vector<std::string>(); }

	Network &network() { return *m_network; }
	LockstepDriver &driver() { return *m_driver; }
	Transport &transport() { return *m_transport; }
	const NetworkConfig &config() const { return m_config; }
	const LobbyStart &start() const { return m_start; }

private:
	UDP &m_socket;
	LobbyStart m_start;
	Options m_options;
	NetworkConfig m_config;
	LiveGame *m_game = nullptr;
	std::unique_ptr<Transport> m_transport;
	std::unique_ptr<Network> m_network;
	std::unique_ptr<ReplayWriter> m_writer;
	std::unique_ptr<LockstepDriver> m_driver;
	CommandList m_empty;
	std::vector<std::string> m_desyncDumps, m_desyncDumpErrors; ///< lane MP-2
	std::vector<size_t> m_dumpedHalves;                         ///< per desync report, the halves its dump holds + 1 (0 = none written)
	void writeDesyncDumps();
};
