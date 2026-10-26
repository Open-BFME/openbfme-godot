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
//
// Lane ONLINE-3 (online games, docs/ONLINE.md "Phase O3"): a START with a rejoin ticket (LobbyStart::rejoinGeneration > 0: the player's game restarted and
// the lobby gave it a fresh ticket) begins with beginRejoin instead of begin: the transport binds to the relay as the slot's new incarnation, the relay's
// command log is fetched while the others play on (GameNetwork/Rejoin.h), the game is re-simulated from frame 0 at full speed (CommandLogCatchUp: the
// players' CRCs in the log are compared on the way), the client asks the others to take it back once it is near the live edge, the packet router's REJOIN
// names the frame, the catch-up runs up to it, and from the frame after the client plays in lockstep (Network::beginLive). The first CRC exchange that
// includes it proves the state. The host loop calls rejoinStep until it reports Live (or Failed), then plays as usual.

#pragma once

#include "Common/Recorder.h"
#include "GameClient/LiveGame.h"
#include "GameNetwork/LANLobby.h"
#include "GameNetwork/LockstepDriver.h"
#include "GameNetwork/Network.h"
#include "GameNetwork/Rejoin.h"
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
		bool earlyOrders = true;      ///< lane NETLAG-2: NetworkConfig::earlyOrders (an order runs a frame sooner where the link allows)
		std::uint32_t rejoinWindowMs = 300000; ///< lane ONLINE-3: an online game's rejoin window (NetworkConfig::rejoinWindowMs)
		bool census = false;          ///< lane MP-3: LockstepDriver::setCensus (the per-frame census of the measurements)
		bool hashEveryFrame = false; ///< ONLINE16-FIX: the driver hashes every frame without a replay (a rejoined incarnation's evidence in tests)
	};
	NetGameSession(UDP &socket, const LobbyStart &start, const Options &options);
	~NetGameSession();

	// after game.load(): the slot -> player map, the network objects, the driver installed on `game`, LOADCOMPLETE sent. False + *error when a network
	// slot has no player in the game or the replay cannot be written.
	bool begin(LiveGame &game, std::string *error);
	// the load barrier: true once every network player loaded (call often before advancing the game)
	bool pollLoaded();

	// ---- lane ONLINE-3: rejoin (see above) ----
	bool beginRejoin(LiveGame &game, std::string *error);
	enum class RejoinState
	{
		Binding,    ///< HELLO with the rejoin ticket until the relay welcomes it
		CatchingUp, ///< fetching the log and re-simulating it
		Waiting,    ///< near the live edge, asking to be taken back (REJOINREQUEST) until the packet router's REJOIN is in the log
		Live,       ///< in lockstep (the session's driver is installed)
		Failed      ///< see rejoinReport().failure
	};
	// one step: services the transport, fetches the log, runs catch-up frames for at most `budgetMs`, switches when the REJOIN's frame is reached
	RejoinState rejoinStep(LiveGame &game, std::uint32_t budgetMs = 50);
	struct RejoinReport
	{
		std::uint64_t startedMs = 0, liveMs = 0;  ///< NetMilliseconds at beginRejoin and at the switch (the catch-up time is the difference)
		std::uint64_t logBytes = 0;
		UnsignedInt liveFrame = 0;               ///< the first lockstep frame (the REJOIN's frame + 1)
		UnsignedInt framesCaughtUp = 0;          ///< the frames the catch-up ran
		unsigned long long crcsCompared = 0, crcMismatches = 0, crcsSentAfter = 0, framesFilled = 0, logRequests = 0;
		std::string firstMismatch, failure;
		std::vector<std::string> tailErrors; ///< the log read after the switch could not be used (reported, the live data still arrives)
	};
	const RejoinReport &rejoinReport() const { return m_rejoin; }

	// ---- lane ONLINE-3: a spectator (OnlineStart::Kind::Spectate; LobbyStart::spectatorIndex >= 0). Read-only: the transport binds the spectator
	// ticket and only reads the relay's log (LOG_REQUEST, held back by the spectator delay at the relay), the game re-simulates it with the catch-up
	// driver and follows it as it grows; no Network, so it sends no command, takes no part in the CRC exchange, the run-ahead or the disconnect path,
	// and nobody waits for it. Its own state hash is compared with the players' CRCs in the log at every CRC frame (a mismatch fails it). The game must
	// have been loaded with no local slot (the observer's ReplayObserver side is the local player: the whole map revealed, RW 0x62F91A) ----
	bool beginSpectate(LiveGame &game, std::string *error);
	enum class SpectateState
	{
		Binding,   ///< HELLO with the spectator ticket until the relay welcomes it
		Following, ///< reading the log and running its frames (caught up or not)
		Ended,     ///< the log's END record arrived and every frame of it ran
		Failed     ///< see spectateReport().failure
	};
	SpectateState spectateStep(LiveGame &game, std::uint32_t budgetMs = 50);
	bool detachSpectator(LiveGame &game, std::string *error = nullptr);
	bool attachSpectator(LiveGame &game, std::string *error = nullptr);
	void disableSpectatorAutoCatchup() { m_spectatorAutoCatchup = false; }
	bool spectatorAutoCatchupInProgress() const
	{
		return m_spectatorAutoCatchup && m_spectateState == SpectateState::Following && m_spectate.caughtUpMs == 0;
	}
	bool spectatorFailed() const { return m_spectateState == SpectateState::Failed; }
	UnsignedInt spectatorFinalFrame() const;
	struct SpectateReport
	{
		std::uint64_t startedMs = 0, caughtUpMs = 0; ///< NetMilliseconds at beginSpectate and when it first reached the end of what the relay served
		UnsignedInt reached = 0;                    ///< the logic frame the spectator's game is at
		UnsignedInt served = 0;                     ///< the last complete frame of the log the relay served so far
		std::uint64_t logBytes = 0;
		unsigned long long crcsCompared = 0, crcMismatches = 0, logRequests = 0;
		bool logComplete = false;
		std::string firstMismatch, failure;
		int64_t firstMismatchFrame = -1;
	};
	const SpectateReport &spectateReport() const { return m_spectate; }
	SpectateState spectateState() const { return m_spectateState; }
	// the spectator stops after this logic frame is reached (a test compares its state with the players' at the same frame); default: the log's end
	void setSpectateStopFrame(UnsignedInt frame) { m_spectateStop = frame; }
	bool spectating() const { return m_start.spectatorIndex >= 0; }
	bool rejoining() const { return m_rejoinState != RejoinState::Live && m_catchUp != nullptr; }
	// services the transport and answers late lobby START datagrams (a lost START_ACK); call every tick
	void service();
	// lane ONLINEUX-1 r2 (Sol 1): a rejoin START whose incarnation never went live (binding, catching up, waiting for its REJOIN, or failed): it is no
	// member of the lockstep game yet, so it has nothing to leave
	bool rejoinUnjoined() const { return m_start.rejoinGeneration > 0 && m_rejoinState != RejoinState::Live; }
	// the local player stops (ZH leave); the replay gets its end record. An unjoined rejoin (above) sends nothing: no frame counts for frames it never
	// played and no PLAYERLEAVE (the player keeps the slot, as after a crash, and can rejoin again)
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
	// RW 0x90313E / 0x927183: human slots marked disconnected, mapped to the score screen's player indices.
	std::vector<int> disconnectedPlayerIndices() const;

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
	bool setUp(LiveGame &game, std::string *error); ///< begin's common part: the config, the transport, the network, the driver (not installed)
	// lane ONLINE-3
	std::unique_ptr<RelayLogFetch> m_fetch;
	std::unique_ptr<CommandLogCatchUp> m_catchUp;
	std::shared_ptr<const RelayLog::Assembled> m_assembled;
	RejoinState m_rejoinState = RejoinState::Binding;
	RejoinReport m_rejoin;
	size_t m_parsedBytes = 0;
	std::uint64_t m_lastTail = 0;
	std::string assembleLog(); ///< "" or why the fetched log cannot be used
	SpectateState m_spectateState = SpectateState::Binding;
	SpectateReport m_spectate;
	bool m_logComplete = false;
	UnsignedInt m_spectateStop = 0xFFFFFFFFu;
	bool m_spectatorAutoCatchup = true;
	void fillFromLog(UnsignedInt fromFrame);
	void failRejoin(const std::string &why);
};
