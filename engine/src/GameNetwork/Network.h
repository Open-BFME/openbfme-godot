// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Network (ZH GameNetwork/Network.cpp, ConnectionManager.cpp, FrameData.cpp, FrameDataManager.cpp; GameLogic::processCommandList's CRC check), lane MP-1:
// the lockstep frame model. Every peer runs the same logic frames with the same command lists; a command a player issues on logic frame G executes on frame
// G + runAhead on every peer (latency hiding), and a frame runs only when every player's commands for it are known.
//
// DONOR FACTS (ZH, GeneralsMD Source/GameNetwork):
//   * Network::getExecutionFrame (Network.cpp:489): max(TheGameLogic->getFrame() + m_runAhead, the last execution frame handed out); every local command is
//     sent with it (ConnectionManager::sendLocalGameMessage).
//   * Network::processCommand (Network.cpp:504): on each new logic frame the peer sends a FRAMEINFO (the command count) for every frame from the last one it
//     completed up to getExecutionFrame() - 1 (ConnectionManager::processFrameTick): no later command can execute on those frames.
//   * ConnectionManager::allCommandsReady / FrameData::allCommandsReady: a frame is ready when, for every player still in the game, the frame info arrived and
//     the commands received for the frame equal its count (more than the count is a resend request, FRAMEDATA_RESEND).
//   * ConnectionManager::getFrameCommandList: the frame's command list is every slot's commands, slot 0 first; inside a slot NetCommandList keeps them
//     ordered by command id (NetCommandList::addMessage), i.e. the order the player issued them.
//   * the GameMessage's player is the SENDER's player (NetGameCommandMsg::constructGameMessage sets it from the slot), not a field the sender controls.
//   * CRC: GameLogic::update appends MSG_LOGIC_CRC (args: integer CRC, boolean playback) every getCRCInterval() frames (GameLogic.cpp:3653) to the message
//     stream; it travels like any command (so every peer's CRC of frame F executes on the same later frame); processCommandList (GameLogic.cpp:2540) collects
//     the CRCs of the frame per player and compares them; a mismatch sets TheNetwork->setSawCRCMismatch().
//   * a leaving player announces the frame it leaves on (Network::processCommand MSG_CLEAR_GAME_DATA, ConnectionManager::handleLocalPlayerLeaving): after it,
//     its frame data is not waited for (FrameData::getIsQuitting).
// TARGET FACTS (RotWK game.dat, caveat S-001; read in MP-1 review r1): RotWK's NetCommandType list has no RUNAHEAD / RUNAHEADMETRICS (RW 0x989449), so
// ZH's packet-router run-ahead messages (ConnectionManager::updateRunAhead) are not RotWK's (this alone does not exclude an internal policy). The frame
// pacing: Network init (RW 0x65DBFC) takes QueryPerformanceFrequency; the router's frame gate (RW 0x65DD0F) divides it by the value at 0xD9F608 (5): a
// 200 ms frame period, one period subtracted per frame run, accumulated lag beyond two seconds dropped; the preview RW 0x65DE5E uses the same period
// (threshold 1.5 at 0xBDE8C8); non-router peers advance on announced frames and complete command counts (FRAMEINFO RW 0x8D4E89, update RW 0x8D77B5). The
// command paths RW 0x8D4301 / 0x8D3AF1 stamp an unassigned command with the current logic frame + 1 and RW 0x65E2EF asks for the next frame's commands.
// FRAME_DATA_LENGTH = 258 (0xDB8248) and FRAMES_TO_KEEP = 65 (0xDB824C) give ZH's MAX_FRAMES_AHEAD = 128. Lane NETLAG-1 (BFME2 decomp, tier A for
// RW 0x8D4301 ConnectionManager::sendLocalCommand and RW 0x8D3AF1 sendLocalCommandDirect): the packet router stamps every command, its own and the ones
// the other peers send it, with its current frame + 1, so RotWK's command delay is one frame on the router (S-720); the lobby's run-ahead, carried in
// the start message, is therefore 1 (MP-1 .. MP-3 used 2). LiveGame's 5 Hz clock drops a held frame's time as the 2 s lag drop does in spirit (not
// ported exactly).
// The CRC interval (GameInfo::getCRCInterval) is 100 in retail MP games (PLAN, replays); MSG_LOGIC_CRC = 1098 (PLAN rule 4).
// OpenBFME DIFFERENCE: the CRC is OpenBFME's state hash (GameLogic::computeStateHash), not retail's xfer CRC (PLAN rule 5, CRC-1). On a mismatch the peers
// exchange the hash cut into its sections (DESYNCREPORT, OpenBFME's own command) so the report names the diverging subsystem and objects.
//
// Determinism: the Network only decides WHICH commands a frame gets and in which order (a pure function of the received data); it never reads a clock.
// Pacing and transport (time, sockets, resends) are the transport's and the driver's (Transport.h, LockstepDriver.h).

#pragma once

#include "GameClient/LiveGameFrameDriver.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameNetwork/DisconnectManager.h"
#include "GameNetwork/RelayCanonicalClient.h"
#include "GameNetwork/NetPacket.h"
#include "GameNetwork/NetworkSettings.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

// what moves the encoded net commands between the peers: reliable and ordered per sender (Transport.h); a test can use an in-memory one
class NetTransport
{
public:
	virtual ~NetTransport() = default;
	// queues one command for the slot; false + *error when it cannot be sent (too big, an unknown or retired slot): never dropped silently
	virtual bool sendTo(int slot, const std::vector<std::uint8_t> &bytes, std::string *error) = 0;
	// one received command (and its sender's slot), false when none is waiting
	virtual bool receive(int &fromSlot, std::vector<std::uint8_t> &bytes) = 0;
	// moves bytes (sends what is due, reads what arrived); Network::update calls it
	virtual void service() {}
	// the slot left the game (its leave took effect): nothing more goes to it, what was queued for it is dropped
	virtual void retire(int slot) { (void)slot; }
	// lane MPDROP-3: the slot was DROPPED (the disconnect path, its game hung or died): nothing new goes to it, but what was queued for it (its
	// DISCONNECTPLAYER last) keeps going until it acknowledges it, resent while it was dropped or last heard within `holdMs`, and again whenever
	// anything comes from it later (a hung game that runs again learns it was dropped instead of dropping the others: a split game). Its own records are
	// no longer delivered. The default retires it (a transport that cannot lose a command: the in-memory test hubs)
	virtual void retireDropped(int slot, std::uint32_t holdMs)
	{
		(void)holdMs;
		retire(slot);
	}
	// lane MP-2: the time (the Network's clock, ms) anything last arrived from the slot, 0 never or unknown (the disconnect path's silence test)
	virtual std::uint64_t lastHeardFrom(int slot) const
	{
		(void)slot;
		return 0;
	}
	// lane MP-2: the measured round trip to the slot and its variation (ms), -1 unknown (the adaptive run-ahead)
	virtual int roundTripMs(int slot) const
	{
		(void)slot;
		return -1;
	}
	virtual int roundTripVarMs(int slot) const
	{
		(void)slot;
		return -1;
	}
	// lane NETLAG-2: the round trip the adaptive run-ahead plans for (ms), -1 while too little is measured. The transport takes a high percentile of its
	// recent samples, so one late ack (a client's hitch, the load barrier) does not move it; this default (in-memory test transports) is the smoothed
	// round trip plus twice its variation
	virtual int plannedRoundTripMs(int slot) const
	{
		const int rtt = roundTripMs(slot);
		return rtt < 0 ? -1 : rtt + 2 * std::max(0, roundTripVarMs(slot));
	}
	// lane ONLINE-3 (relay mode; LAN keeps these defaults): the relay's view of a slot (RelayProtocol STATUS): the incarnation the relay bound it with (a
	// rejoin's fresh ticket raises it) and whether the relay has not heard it lately
	virtual int peerGeneration(int slot) const
	{
		(void)slot;
		return 0;
	}
	virtual bool silentAtRelay(int slot) const
	{
		(void)slot;
		return false;
	}
	// lane ONLINE-3: the disconnect path's ping test (RW 0x8D7C78): false when the local link failed (relay mode: nothing arrived from the relay for `ms`)
	virtual bool localLinkAlive(std::uint32_t ms) const
	{
		(void)ms;
		return true;
	}
	// lane ONLINE-3: the slot's streams start afresh (sequence numbers from 0 both ways, nothing queued): the Network takes a new incarnation of it back
	virtual void resetPeer(int slot) { (void)slot; }
	// lane ONLINE-3 (relay mode): a frame command no other player receives (the others left or are held) goes to the relay's log only, so the log keeps
	// every frame a rejoining client must re-simulate; false where there is no relay (LAN: nothing logs)
	virtual bool sendToLog(const std::vector<std::uint8_t> &bytes)
	{
		(void)bytes;
		return false;
	}
	// ---- lane NET-SYNC (canonical relay games): the relay keeps the beat ----
	// True once a canonical game's WELCOME bound (orders and beats go up as OBCI messages, committed OBRC records
	// come down). Every method is inert outside a canonical bind.
	virtual bool relayCanonical() const { return false; }
	virtual std::uint64_t canonicalGame() const { return 0; }
	virtual std::uint64_t canonicalEpoch() const { return 0; }
	// one local order (the complete serialized GameMessage), stamped by the relay; `id` is the slot's dense order counter
	virtual bool sendCanonicalOrder(std::uint64_t id, const std::vector<std::uint8_t> &bytes, std::uint64_t now, std::string &error)
	{
		(void)id; (void)bytes; (void)now; (void)error;
		return false;
	}
	// this client's input for the window ending now is complete; `id` is its dense beat counter
	virtual bool sendCanonicalBeat(std::uint64_t id, std::uint64_t now, std::string &error)
	{
		(void)id; (void)now; (void)error;
		return false;
	}
	// the next committed OBRC record in journal order, false when none waits
	virtual bool takeCanonicalRecord(std::vector<std::uint8_t> &bytes)
	{
		(void)bytes;
		return false;
	}
};

struct NetworkConfig
{
	int localSlot = 0;
	// per GameInfo slot: the logic player index of the human player in that slot, -1 for a slot that is not a network player (AI, open, closed)
	std::array<int, MAX_SLOTS> slotPlayerIndex = [] {
		std::array<int, MAX_SLOTS> slots;
		slots.fill(-1);
		return slots;
	}();
	// frames between issuing a command and executing it; with adaptiveRunAhead the minimum. Lane NETLAG-1: 1, RotWK's delay (the packet router stamps a
	// command with the current frame + 1, RW 0x8D4301 / 0x8D3AF1: 200 to 400 ms from a click to its frame); MP-1 .. MP-3 used 2
	int runAhead = 1;
	// lane MP-2: the run-ahead follows the packet router's measured round trip (Network::updateRunAhead): the router sends RUNAHEAD as a frame command and
	// every peer takes the new value when that frame runs (equal run-aheads keep every peer's MSG_LOGIC_CRC on one execution frame)
	bool adaptiveRunAhead = false;
	// lane NETLAG-2 (the owner's choice of 2026-10-20: orders faster than RotWK where the link allows): a player's order may execute one frame before the
	// run-ahead's frame when every peer can still learn of it in time (Network::setFrameDue; OpenBFME's own, retail always waits the full frame + 1)
	bool earlyOrders = true;
	int maxRunAhead = 25;  ///< frames (5 s)
	int crcInterval = 100; ///< GameInfo::getCRCInterval; 0 = no CRC exchange
	// lane MP-2: the disconnect path (GameNetwork/DisconnectManager.h)
	bool disconnectPath = true;
	NetworkSettings settings;                       ///< GameData's network timing (NetworkDisconnectTime, NetworkPlayerTimeoutTime, ...)
	std::array<std::u16string, MAX_SLOTS> slotNames; ///< the players' names (the disconnect screen, the left-game notice)
	// ZH / RotWK ConnectionManager + 0x12030: the packet router fallback order (empty: the network slots in slot order, the first one the router; INFERENCE:
	// ZH fills it from the game's slots in order, RotWK's filler was not read, S-1121)
	std::vector<int> packetRouterOrder;
	// lane ONLINE-3 (an online game: every peer behind the relay; LAN keeps RotWK's hand-over at the drop): a player the disconnect path drops after its
	// timeout (not by a vote) is HELD: its frames are not waited for, its army stays its own and idle; the packet router hands the army over (DESTROYPLAYER,
	// retail's MSG_SELF_DESTRUCT) once the rejoin window has passed, or takes the player back (REJOIN) when its restarted game asks (REJOINREQUEST)
	bool holdForRejoin = false;
	std::uint32_t rejoinWindowMs = 300000; ///< OpenBFME's choice (S-3900): five minutes
};

// The breakdown of one peer's state hash for one frame (GameLogic::computeStateHashBreakdown), as exchanged after a mismatch
struct DesyncHalf
{
	int slot = -1;
	std::uint32_t frame = 0; ///< the logic frame the hash describes (the state after frame - 1 ran)
	std::uint32_t hash = 0;
	std::vector<GameLogic::StateHashSection> sections;
	std::vector<GameLogic::ObjectStateHash> objects;
	std::vector<std::uint8_t> encode() const;
	static bool decode(const std::vector<std::uint8_t> &bytes, DesyncHalf &out, std::string *error);
};

struct DesyncReport
{
	std::uint32_t frame = 0;          ///< the frame whose hashes differ
	std::uint32_t checkedOnFrame = 0; ///< the frame whose command list carried the CRCs
	std::map<int, std::uint32_t> crcs; ///< slot -> the CRC that slot sent
	std::map<int, DesyncHalf> halves;  ///< slot -> its breakdown (the local one at once, the others when they arrive)
	// the report: the frame, every slot's hash, the section table with the differing sections marked, and the first differing objects
	std::string text() const;
	// the sections whose values differ between the halves present ("" when fewer than two halves)
	std::vector<std::string> differingSections() const;
};

class Network
{
public:
	Network(const NetworkConfig &config, NetTransport &transport);

	const NetworkConfig &config() const { return m_config; }
	// lane ANTICHEAT-1 (owner 2026-10-21): the local player's view for the relay's log: a VIEW command on the log-only stream (the relay logs it, no other
	// player receives it; not simulation input): the logic frame shown, the camera's look-at x, y, the world point under the cursor x, y. False when the
	// transport keeps no log (LAN)
	bool submitView(UnsignedInt frame, float cameraX, float cameraY, float cursorX, float cursorY, bool cursorOverWorld);

	// ZH Network::update: (1) every message the local player put on `pending` since the last call is sent with the execution frame (its player index is set
	// to the local player's), `pending` is emptied; (2) the frame infos up to getExecutionFrame() - 1 go out; (3) everything received is filed.
	// `logicFrame` is the frame the logic runs next.
	void update(UnsignedInt logicFrame, CommandList &pending);
	// ZH ConnectionManager::allCommandsReady
	bool isFrameReady(UnsignedInt frame) const;
	// the slots `frame` still waits for (frame info or commands missing)
	std::vector<int> waitingFor(UnsignedInt frame) const;
	// ZH Network::RelayCommandsToCommandList: appends the frame's commands to `out` (slot order, then issue order) and checks the MSG_LOGIC_CRCs among them.
	// The frame must be ready.
	void relayCommands(UnsignedInt frame, CommandList &out);

	// GameLogic's CRC step (GameLogic.cpp:3653), called after the logic ran a frame: when `frameNow` (the logic's frame after it) is a CRC frame, appends
	// MSG_LOGIC_CRC to `pending` and keeps the breakdown for a desync report. SMOOTH-1 (review r4): consumes the completion the simulation owner captured
	// (c.frame is frameNow; the breakdown was taken on the worker), on the protocol owner; it never reads the live GameLogic
	void frameCompleted(const FrameCompletion &c, CommandList &pending);

	// the local player leaves after the frames it has announced (ZH handleLocalPlayerLeaving): sends PLAYERLEAVE for the next execution frame
	void leave(UnsignedInt logicFrame);
	bool hasLeft(int slot) const;

	// ZH NETCOMMANDTYPE_LOADCOMPLETE: the start barrier (every network player loaded the map before frame 0 runs)
	void sendLoadComplete();
	bool allLoaded() const;

	// the desyncs seen (ZH sawCRCMismatch); a report is complete when every slot's half arrived
	const std::vector<DesyncReport> &desyncs() const { return m_desyncs; }
	bool sawCRCMismatch() const { return !m_desyncs.empty(); }
	// the CRCs checked so far (frames whose CRCs all agreed)
	unsigned long long crcChecksPassed() const { return m_crcChecksPassed; }
	// malformed or unexpected input (a decode error, a command for a frame already run, more commands than announced): reported, never dropped silently
	const std::vector<std::string> &errors() const { return m_errors; }
	// the commands the frame relayed so far (all slots), for statistics
	unsigned long long commandsRelayed() const { return m_commandsRelayed; }

	UnsignedInt executionFrame(UnsignedInt logicFrame);
	// lane NETLAG-2: the protocol owner's clock: logic frame `frame` (the next one it runs) is due in `msFromNow` ms (0: due now or overdue); later frames
	// are taken a 200 ms period apart until the next call. Without it (a test, a replay) every order executes on the run-ahead's frame. With it and NetworkConfig::earlyOrders, an order issued while the frame before the
	// run-ahead's one is at least earlyLeadMs() away executes on that frame, and that frame's FRAMEINFO goes out only once it is closer than the lead
	void setFrameDue(UnsignedInt frame, std::uint32_t msFromNow);
	// the time a FRAMEINFO needs to reach every peer before its frame runs there: the planned one-way latency to the farthest peer + NetworkRunAheadSlack
	// percent + a margin; -1 while a peer's round trip is unknown (no early orders then)
	long long earlyLeadMs() const;
	unsigned long long earlyOrdersSent() const { return m_earlyOrders; }
	// lane MP-2: the run-ahead this peer uses now, and how often the adaptation changed it
	int currentRunAhead() const { return m_config.runAhead; }
	int runAheadChanges() const { return m_runAheadChanges; }
	int maxRoundTripMs() const { return m_lastMaxRtt; }

	// ---- lane MP-2: the disconnect path ----
	// the frame gate: false while this peer's announced disconnect frame waits for the next packet router (S-1121; ZH's screen gate is not applied, see
	// DisconnectManager.h)
	bool allowedToContinue(UnsignedInt frame);
	DisconnectManager &disconnectManager() { return m_disconnect; }
	const DisconnectManager &disconnectManager() const { return m_disconnect; }
	// the disconnect screen's Kick (a vote of the local player for `slot`) and Quit (RW 0x9195E1: a vote for every other slot, then the local player quits)
	void voteForPlayerDisconnect(int slot);
	void quitFromDisconnectScreen();
	// the local player has to leave (dropped by the others, its own connection failed, alone, or Quit): the session ends its game
	bool quitRequested() const { return m_quitFromScreen || m_disconnect.quitRequested(); }
	std::string quitReason() const { return m_quitFromScreen ? std::string("the local player quit from the disconnect screen") : m_disconnect.quitReason(); }
	// the slot was dropped by the disconnect path (its frames from its disconnect frame on are not waited for)
	bool isDisconnected(int slot) const { return slot >= 0 && slot < MAX_SLOTS && m_slots[(size_t)slot].disconnected; }
	bool isGameSlot(int slot) const { return slot >= 0 && slot < MAX_SLOTS && m_slots[(size_t)slot].active; } ///< lane MP-DROP-1: a network player of the game
	/// lane MP-DROP-1: the slot left or was dropped at some frame here (a rejoined slot keeps its departure: it is never router or decider again)
	bool hasDeparted(int slot) const { return slot >= 0 && slot < MAX_SLOTS && m_departureFrame[(size_t)slot] != 0xFFFFFFFFu; }
	// WOTR2 milestone 8 (read only): the slot's departure frame (its leave or drop, the earliest: passRouterRole), 0xFFFFFFFF none
	UnsignedInt departureFrame(int slot) const { return slot >= 0 && slot < MAX_SLOTS ? m_departureFrame[(size_t)slot] : 0xFFFFFFFFu; }
	// "Network:PlayerLeftGame" notices (RW 0x8D57D8), one per dropped or departed player, for the UI
	const std::vector<std::u16string> &leftGameNotices() const { return m_leftNotices; }
	// the clock of the disconnect path (default NetMilliseconds); a test drives its own
	void setClock(std::function<std::uint64_t()> clock) { m_clock = std::move(clock); }
	std::uint64_t now() const;
	// in a decided game (the local player won or lost: VictoryConditions vslots 0x4C / 0x48 / 0x54, RW 0x8D8BD8) a silent peer makes the local player quit
	// instead of showing the screen; the session answers it from the logic's completed state
	void setGameDecided(std::function<bool()> f) { m_gameDecided = std::move(f); }
	// frames whose commands were sent to a peer that was behind (sendFrameDataToPlayer) and frame data that arrived that way
	unsigned long long framesResent() const { return m_framesResent; }
	unsigned long long framesFilledFromResend() const { return m_framesFilled; }

	// ---- DisconnectManager's view of the connections (RotWK ConnectionManager) ----
	bool hasConnection(int slot) const;      ///< a remote slot still in the game (RW 0x8D3488 without the local slot)
	bool isPlayerConnected(int slot) const;  ///< RW 0x8D3488
	bool isLeftState(int slot) const;        ///< RW 0x8D34D2: ConnectionManager + 0x12080 in 1 .. 3 (left or disconnected)
	int numConnected() const;                ///< RW 0x8D3D33
	std::uint64_t lastHeard(int slot, std::uint64_t now); ///< the last time anything arrived (the first query starts the clock, RW 0x8D335D)
	bool gameRunning() const;
	bool gameDecidedForLocalPlayer() const { return m_gameDecided && m_gameDecided(); }
	std::u16string playerName(int slot) const { return slot >= 0 && slot < MAX_SLOTS ? m_config.slotNames[(size_t)slot] : std::u16string(); }
	int packetRouterSlot() const { return m_packetRouter; }
	// ---- lane ONLINE-3: hold, rejoin and pause (OpenBFME additions, docs/ONLINE.md "Phase O3", stops S-3900 / S-3901) ----
	// the slot was dropped and is held for a rejoin (its army stays its own); handed over: its DESTROYPLAYER went out (the window ended or a vote)
	bool isHeld(int slot) const { return slot >= 0 && slot < MAX_SLOTS && m_slots[(size_t)slot].held && !m_slots[(size_t)slot].handedOver; }
	bool isHandedOver(int slot) const { return slot >= 0 && slot < MAX_SLOTS && m_slots[(size_t)slot].handedOver; }
	// a newer incarnation of the member slot appeared at the relay (its game restarted): the old one is gone, the disconnect path drops it at once
	bool slotRestarted(int slot) const { return slot >= 0 && slot < MAX_SLOTS && m_slots[(size_t)slot].restarted; }
	// the frame the slot rejoined at (0: never), how many rejoins this peer applied, and their notices ("slot S rejoined at frame F")
	UnsignedInt rejoinFrame(int slot) const { return slot >= 0 && slot < MAX_SLOTS ? m_slots[(size_t)slot].joinFrame : 0; }
	const std::vector<std::string> &rejoinLog() const { return m_rejoinLog; }
	// the disconnect path's ping test (RW 0x8D7C78): false when the local link failed (relay mode: nothing from the relay for NetworkDisconnectTime)
	bool pingTest() const;
	// pause: every peer runs the frames before the pause frame (the requester's next execution frame) and stops; resume ends it (any player may do either)
	void requestPause();
	void requestResume();
	bool isPaused() const { return !m_pauses.empty(); }
	bool pausedBefore(UnsignedInt frame) const { return !m_pauses.empty() && frame >= m_pauses.begin()->first; }
	UnsignedInt pauseFrame() const { return m_pauses.empty() ? 0 : m_pauses.begin()->first; }
	int pausedBy() const { return m_pauses.empty() ? -1 : m_pauses.begin()->second; }
	unsigned long long pausesSeen() const { return m_pausesSeen; }
	// the frame commands this peer sent to the relay's log-only stream again when it found itself alone (logAloneFrames)
	unsigned long long frameCommandsRelogged() const { return m_frameCommandsRelogged; }
	// the rejoining client (its game restarted, NetGameSession's rejoin): until beginLive the Network sends no frame data and runs no disconnect path;
	// once ready it asks the others to take it back (REJOINREQUEST every second, with its ticket's generation)
	void setRejoining(int generation);
	void setRejoinReady() { m_rejoinReady = true; }
	bool rejoining() const { return m_rejoining; }
	struct MembershipEvent
	{
		enum Kind
		{
			DROP,    ///< DISCONNECTPLAYER: the slot runs no frame from `frame` on
			LEAVE,   ///< PLAYERLEAVE at `frame`
			REJOIN,  ///< the packet router's REJOIN ran on `frame`: the slot is a member again from frame + 1
			DESTROY  ///< DESTROYPLAYER of the slot (its army handed over)
		} kind = DROP;
		int slot = -1;
		UnsignedInt frame = 0;
		int generation = 0; ///< REJOIN: the generation of the incarnation taken back (the log's REJOINED record)
	};
	struct LiveStart
	{
		UnsignedInt frame = 0; ///< the first frame this client runs in lockstep (the REJOIN's frame + 1)
		int runAhead = 1;      ///< the run-ahead in effect from it
		std::vector<MembershipEvent> membership; ///< every membership change of the game so far, in log order
		struct Crc
		{
			UnsignedInt frame = 0, executionFrame = 0; ///< a CRC frame the catch-up ran whose MSG_LOGIC_CRC executes on `frame` or later
			std::uint32_t hash = 0;
			DesyncHalf half;
		};
		std::vector<Crc> crcs;
	};
	// the switch from the catch-up to lockstep: the membership, the frame cursors and the run-ahead of the game at `frame`, and this slot's MSG_LOGIC_CRCs
	// that execute from `frame` on (the others sent theirs before it rejoined)
	void beginLive(const LiveStart &start);
	// a frame of another slot taken from the relay's log, complete (its FRAMEINFO and every entry): filled whole unless complete already (as FRAMERESEND)
	void fillFromLog(int slot, UnsignedInt frame, const std::vector<std::shared_ptr<GameMessage>> &commands, int runAheadChange, const std::vector<int> &rejoins);

	// ---- lane CHAT-1: the in-game chat (RotWK Network::sendChat RW 0x65DF98 -> ConnectionManager::sendChat RW 0x8D5BA5, processChat RW 0x8D54D1) ----
	// TARGET FACTS (RotWK game.dat, caveat S-001): sendChat makes a NetChatCommandMsg (type 14, the text and the recipients mask), sends it with
	// sendLocalCommandDirect (RW 0x8D3AF1: a direct, acknowledged command to each connection, NOT frame data: CHAT is not a synchronized type, RW 0x989315
	// lists only 3, 4, 10, 11 and 0x1E) and runs processChat on it at once (the sender's own echo). processChat shows the line when the local slot's bit is
	// in the mask and the sender's slot is not muted (GameSlot + 0xA, set from the players screen); nothing of it reaches the logic, the frame data, the
	// command counts or the run-ahead.
	// OpenBFME DIFFERENCES (named, docs/STOPS.md S-5150): (1) the command goes only to the connections whose bit is in the mask (retail sends it to every
	// connection and each receiver filters: an allies line never reaches an enemy's machine here); (2) a receiver drops a sender's lines beyond a flood
	// limit (kChatBurst lines, then one per kChatRefillMs; retail has none); dropped lines are counted, never shown. The byte layout is OpenBFME's (S-721).
	struct ChatLine
	{
		int slot = -1;               ///< the sender (the command's slot, which receiveAll checked against the connection)
		std::uint32_t playerMask = 0; ///< its recipients (bit per slot)
		std::u16string text;         ///< the line as the sender formatted it ("<Team> hello")
	};
	static constexpr int kChatBurst = 8;
	static constexpr std::uint64_t kChatRefillMs = 1000;
	// sends `text` to the slots of `mask` (the local bit: the local echo); false + *error for an empty or over-long text (NetChat::kMaxTextUnits), while
	// leaving / rejoining, or when no slot of the mask is a player of the game
	bool sendChat(const std::u16string &text, std::uint32_t mask, std::string *error = nullptr);
	// the chat lines shown since the last call, in arrival order (per sender: the order it sent them)
	std::vector<ChatLine> takeChat();
	// the players screen's mute (GameSlot + 0xA): a muted slot's lines are not shown; Everyone lines leave muted slots out of the mask (the caller's)
	void setChatMuted(int slot, bool muted);
	// lane CHAT-1 (OpenBFME): the lobby moderators' mute (the START's chatMuted): those slots' lines are dropped on every machine, and a muted local player
	// cannot send (sendChat fails with "muted by the moderators"); the players' own /mute is setChatMuted
	void setChatLobbyMuted(std::uint32_t mask) { m_chatLobbyMuted = mask; }
	std::uint32_t chatLobbyMuted() const { return m_chatLobbyMuted; }
	bool chatMuted(int slot) const { return slot >= 0 && slot < 32 && (m_chatMuted >> slot & 1u); }
	std::uint32_t chatMutedMask() const { return m_chatMuted; }
	unsigned long long chatReceived() const { return m_chatReceived; }      ///< lines filed (shown or not)
	unsigned long long chatDroppedFlood() const { return m_chatFlood; }    ///< lines over a sender's flood limit
	unsigned long long chatDroppedMuted() const { return m_chatMutedDrops; }
	unsigned long long chatNotAddressed() const { return m_chatNotAddressed; } ///< lines whose mask did not hold the local slot
	// the disconnect screen's chat (RotWK sendDisconnectChat RW 0x8D5C5B, processDisconnectChat RW 0x8D5403): NETCOMMANDTYPE_DISCONNECTCHAT 13 with the text
	// only, direct to every connected player, the local echo; a receiver shows it on its disconnect screen when that is up (the caller's). The flood limit
	// and the moderators' mute apply as to CHAT (OpenBFME); the players' own mute does not (retail's processDisconnectChat has none)
	bool sendDisconnectChat(const std::u16string &text, std::string *error = nullptr);
	std::vector<ChatLine> takeDisconnectChat();

	// lane MP-3 (review r1): the packet router of a frame (the role passes on at a drop or leave frame)
	int packetRouterAt(UnsignedInt frame) const { return routerAt(frame); }
	int nextPacketRouterSlot(int slot) const; ///< RW 0x8D3ECD (8 or more: none)
	// lane MP-2 (review): who decides that `target` leaves the game. The decider is the first slot of the agreed fallback order (the lobby's, never edited)
	// whose predecessors are all the target or gone; it names the predecessors it holds gone (a mask of slots) in its DISCONNECTPLAYER. dropClaims: may
	// `sender` decide on this peer's knowledge, and which predecessors it claims gone. verifyDropClaims, on a receiver: -1 refused (a predecessor that is
	// not the target is not claimed: decided on the message and the agreed order alone, the same on every peer), 0 pending (a claimed leave / drop has not
	// been applied here yet: the drop waits for it, and the frames from the drop frame on wait for the drop), 1 accepted
	bool dropClaims(int sender, int target, std::uint16_t &gone) const;
	int verifyDropClaims(int sender, int target, std::uint16_t gone) const;
	// lane MP-2 (review): a membership change at `frame` is still possible here: this peer has not run that frame
	void reportError(const std::string &e) { m_errors.push_back(e); }
	bool frameStillOpen(UnsignedInt frame) const { return !m_relayedAny || frame > m_lastRelayed; }
	void sendDirect(const NetCommandMsg &c);  ///< ZH sendLocalCommandDirect: to every other connected slot, not frame data
	void sendDirectTo(int slot, const NetCommandMsg &c); ///< lane MP-DROP-1: as sendDirect, to one connected slot (the decider's resent drop)
	void sendPlayerDestruct(int slot);        ///< ZH DisconnectManager::sendPlayerDestruct: DESTROYPLAYER at the execution frame + 1
	void disconnectPlayer(int slot, UnsignedInt frame); ///< RW 0x8D57D8: the slot is not waited for from `frame` on, its connection is retired
	void sendFrameDataToPlayer(int slot, UnsignedInt startFrame, UnsignedInt endFrame); ///< RW 0x8D4761
	void notePlayerLatestFrame(int slot, UnsignedInt frame);                            ///< RW 0x8D4884 (+ 0x12060)

	// the acceptance stops of the network layer (docs/STOPS.md S-720 .. S-725), one line each; every peer report carries them
	static std::vector<std::string> stopLines();
	// lane ONLINE-3's acceptance stops (S-3900 reconnect, S-3901 pause); every online peer report and GameWorld.net_status of an online game carry them
	static std::vector<std::string> onlineStopLines();

private:
	struct FrameData // ZH FrameData: one slot's data for one frame
	{
		int commandCount = -1; ///< from the FRAMEINFO; -1 until it arrived
		std::vector<std::shared_ptr<GameMessage>> commands;
		bool fromResend = false; ///< lane MP-2: filled whole by a FRAMERESEND (the sender's own late commands for it are dropped)
		int runAheadChange = -1; ///< lane MP-2: the slot's RUNAHEAD for this frame (counted in the frame info like a command)
		std::vector<int> rejoins; ///< lane ONLINE-3: the slots the slot's REJOINs of this frame name (counted like commands)
		size_t entries() const { return commands.size() + (runAheadChange >= 0 ? 1 : 0) + rejoins.size(); }
	};
	struct SlotData
	{
		bool active = false;
		std::map<UnsignedInt, FrameData> frames;
		UnsignedInt leaveFrame = 0xFFFFFFFFu; ///< frames >= this one are not waited for
		bool loaded = false;
		bool disconnected = false;            ///< lane MP-2: dropped by the disconnect path
		std::uint64_t lastHeard = 0;          ///< lane MP-2: the last command from it (the Network's clock)
		std::uint64_t firstQuery = 0;         ///< lane MP-2: RW 0x8D335D's start of the clock
		UnsignedInt latestFrame = 0;          ///< lane MP-2: RW + 0x12060
		// lane ONLINE-3
		UnsignedInt joinFrame = 0;            ///< frames before this one are not waited for (a rejoined incarnation)
		bool held = false;                    ///< dropped in an online game: held for a rejoin
		std::uint64_t heldSince = 0;
		bool handedOver = false;              ///< a DESTROYPLAYER of it was filed (its army went to an ally)
		int seenGeneration = 0;               ///< the relay's latest generation of it (STATUS)
		int memberGeneration = 0;             ///< the generation of its incarnation in the game (or last in it)
		int resetGeneration = 0;              ///< the generation its transport streams were last started afresh for
		bool restarted = false;               ///< a newer generation appeared while it was a member: the old incarnation is gone
		bool rejoinRequested = false;         ///< its new incarnation asked to be taken back
		bool rejoinIssued = false;            ///< this peer (the packet router) sent a REJOIN of it
	};
	void send(const NetCommandMsg &c);
	void receiveAll();
	void file(const NetCommandMsg &c, int fromSlot);
	void checkCRCs(UnsignedInt frame, const std::map<int, std::uint32_t> &crcs);
	bool slotNeeded(int slot, UnsignedInt frame) const;

	NetworkConfig m_config;
	NetTransport &m_transport;
	// lane NET-SYNC: the canonical driver's state — the commit stream's cursor and the frames it produced and the
	// driver has not run yet; the counters keep the per-slot dense OBCI ids
	std::unique_ptr<RelayCanonicalClient::Client> m_canonicalClient;
	std::map<UnsignedInt, RelayCanonicalClient::FrameInput> m_canonicalFrames;
	std::uint64_t m_canonicalOrderId = 0, m_canonicalBeatId = 0;
	void updateCanonical(UnsignedInt logicFrame, CommandList &pending);
	std::array<SlotData, MAX_SLOTS> m_slots;
	UnsignedInt m_lastExecutionFrame = 0;
	long long m_lastFrameAnnounced = -1; ///< ZH m_lastFrameCompleted
	// lane NETLAG-2: the protocol owner's timing (setFrameDue) and the latest execution frame of a local player's order (orders keep their issue order)
	bool m_dueKnown = false;
	UnsignedInt m_dueFrame = 0;
	std::uint64_t m_dueAtMs = 0;
	UnsignedInt m_lastOrderFrame = 0;
	unsigned long long m_earlyOrders = 0;
	UnsignedInt m_lastRelayed = 0;
	bool m_relayedAny = false;
	bool m_leaving = false;
	std::map<UnsignedInt, DesyncHalf> m_localHalves; ///< the CRC frames' breakdowns kept for reports (the last few)
	std::map<UnsignedInt, UnsignedInt> m_crcFrameOfExecution; ///< execution frame of a local MSG_LOGIC_CRC -> the frame it describes
	std::deque<UnsignedInt> m_pendingCrcFrames;               ///< the frames of the local MSG_LOGIC_CRCs not sent yet
	std::vector<DesyncHalf> m_orphanHalves;                   ///< remote halves that arrived before the local report existed
	std::vector<DesyncReport> m_desyncs;
	std::vector<std::string> m_errors;
	unsigned long long m_crcChecksPassed = 0;
	unsigned long long m_commandsRelayed = 0;
	// lane MP-2
	struct RelayedFrame // the commands a frame ran with, per slot (FRAMES_TO_KEEP = 65 frames, RW 0xDB824C), for sendFrameDataToPlayer
	{
		std::array<bool, MAX_SLOTS> present{};
		std::array<bool, MAX_SLOTS> resent{};
		std::array<int, MAX_SLOTS> runAheadChange = [] {
			std::array<int, MAX_SLOTS> changes;
			changes.fill(-1);
			return changes;
		}();
		std::array<std::uint16_t, MAX_SLOTS> rejoinMask{}; ///< lane ONLINE-3; lane P16: a bit per slot
		std::array<std::vector<std::shared_ptr<GameMessage>>, MAX_SLOTS> commands;
	};
	std::map<UnsignedInt, RelayedFrame> m_history;
	DisconnectManager m_disconnect;
	std::function<std::uint64_t()> m_clock;
	std::function<bool()> m_gameDecided;
	UnsignedInt m_logicFrame = 0; ///< the frame the logic runs next (the last update's)
	int m_packetRouter = -1;
	std::vector<int> m_routerOrder;
	std::vector<int> m_agreedRouterOrder; ///< lane MP-2 (review): the fallback order every peer starts with (drop authority)
	bool m_quitFromScreen = false;
	std::vector<std::u16string> m_leftNotices;
	unsigned long long m_framesResent = 0, m_framesFilled = 0;
	// lane MP-2: the adaptive run-ahead
	int m_minRunAhead = 1;
	std::uint64_t m_lastRunAheadUpdate = 0;
	int m_runAheadChanges = 0;
	int m_lastMaxRtt = -1;
	UnsignedInt m_runAheadChangeFrame = 0; ///< the frame the router's last RUNAHEAD runs on (no new one before it ran)
	bool m_runAheadChangePending = false;
	void updateRunAhead(std::uint64_t now);
	// lane MP-2 (review): the run-ahead every peer agreed on, by the frame whose relay changed it (frame -> the value from the next protocol frame on),
	// folded into m_baseRunAhead once older than the frames still in play; runAheadAt(L) is the run-ahead of protocol frame L: the changes of the frames
	// before L, which every peer relayed before it completes frame L (independent of how far the logic worker has acquired)
	std::map<UnsignedInt, int> m_runAheadHistory;
	int m_baseRunAhead = 1;
	int runAheadAt(UnsignedInt logicFrame) const;
	// lane MP-2 (review): the packet router of each frame (a drop of the router hands the role on at the drop frame): only its RUNAHEAD is taken. Lane MP-3
	// (review r3): derived from every slot's departure frame (leave or drop; none = 0xFFFFFFFF) and the agreed fallback order, never from arrival order
	std::array<UnsignedInt, MAX_SLOTS> m_departureFrame = [] {
		std::array<UnsignedInt, MAX_SLOTS> frames;
		frames.fill(0xFFFFFFFFu);
		return frames;
	}();
	int routerAt(UnsignedInt frame) const;
	// RW 0x8D57D8's membership step for a slot that is gone from `frame` on (dropped, or lane MP-3: left): the next router, the fallback order
	void passRouterRole(int slot, UnsignedInt frame);
	// lane ONLINE-3
	void checkRelayStatus();
	void routerRejoinDuties();
	void applyRejoin(int slot, UnsignedInt joinFrame);
	bool newIncarnationTalking(int slot) const;
	bool reachesLivePeer(int slot) const;
	void logAloneFrames();
	struct RecentFrameCommand ///< lane ONLINE-3: one of this peer's frame commands of the last FRAMES_TO_KEEP frames
	{
		UnsignedInt frame = 0;
		std::vector<std::uint8_t> bytes;
		bool onLogStream = false; ///< sent to the relay's log-only stream (each goes there at most once, in send order)
	};
	std::deque<RecentFrameCommand> m_recentFrameCommands;
	void logRecentFrameCommands(); ///< the recent ones not yet on the log-only stream, in send order
	bool m_wasAlone = false;
	bool m_reconstructing = false; ///< lane ONLINE-3 r2: beginLive applies the log's membership history (no transport effects)
	unsigned long long m_frameCommandsRelogged = 0;
	std::map<UnsignedInt, int> m_pauses;     ///< pause frame -> who paused (pending; a RESUME removes it)
	std::set<UnsignedInt> m_resumedPauses;   ///< pauses resumed (a RESUME can overtake its PAUSE from another sender)
	unsigned long long m_pausesSeen = 0;
	// lane CHAT-1
	std::vector<ChatLine> m_chatIn;
	std::vector<ChatLine> m_disconnectChatIn;
	bool chatFloodAllows(int fromSlot);
	std::uint32_t m_chatMuted = 0;
	std::uint32_t m_chatLobbyMuted = 0;
	struct ChatBucket
	{
		int tokens = kChatBurst;
		std::uint64_t last = 0;
		bool started = false;
	};
	std::array<ChatBucket, MAX_SLOTS> m_chatBuckets{};
	unsigned long long m_chatReceived = 0, m_chatFlood = 0, m_chatMutedDrops = 0, m_chatNotAddressed = 0;
	void fileChat(const NetCommandMsg &c, int fromSlot);
	bool m_rejoining = false, m_rejoinReady = false;
	int m_rejoinGeneration = 0;
	std::uint64_t m_lastRejoinRequest = 0;
	bool m_selfRejoinSent = false; ///< lane SOLO-1: the local slot is the game's only network player: its own REJOIN went out
	int m_memberSlots = 0;         ///< the config's network slots (a solo game's is 1)
	std::vector<std::string> m_rejoinLog;
};
