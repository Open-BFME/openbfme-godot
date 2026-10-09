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
// FRAME_DATA_LENGTH = 258 (0xDB8248) and FRAMES_TO_KEEP = 65 (0xDB824C) give ZH's MAX_FRAMES_AHEAD = 128. RotWK's EFFECTIVE command delay is not
// established (stop S-720): the run-ahead here is OpenBFME's fixed choice, carried in the start message; LiveGame's 5 Hz clock drops a held frame's time
// as the 2 s lag drop does in spirit (not ported exactly).
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
#include "GameNetwork/NetPacket.h"
#include "GameNetwork/NetworkSettings.h"

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
};

struct NetworkConfig
{
	int localSlot = 0;
	// per GameInfo slot: the logic player index of the human player in that slot, -1 for a slot that is not a network player (AI, open, closed)
	std::array<int, MAX_SLOTS> slotPlayerIndex{ { -1, -1, -1, -1, -1, -1, -1, -1 } };
	int runAhead = 2;      ///< frames between issuing a command and executing it (stop S-720: RotWK's value not identified); with adaptiveRunAhead the minimum
	// lane MP-2: the run-ahead follows the packet router's measured round trip (Network::updateRunAhead): the router sends RUNAHEAD as a frame command and
	// every peer takes the new value when that frame runs (equal run-aheads keep every peer's MSG_LOGIC_CRC on one execution frame)
	bool adaptiveRunAhead = false;
	int maxRunAhead = 25;  ///< frames (5 s)
	int crcInterval = 100; ///< GameInfo::getCRCInterval; 0 = no CRC exchange
	// lane MP-2: the disconnect path (GameNetwork/DisconnectManager.h)
	bool disconnectPath = true;
	NetworkSettings settings;                       ///< GameData's network timing (NetworkDisconnectTime, NetworkPlayerTimeoutTime, ...)
	std::array<std::u16string, MAX_SLOTS> slotNames; ///< the players' names (the disconnect screen, the left-game notice)
	// ZH / RotWK ConnectionManager + 0x12030: the packet router fallback order (empty: the network slots in slot order, the first one the router; INFERENCE:
	// ZH fills it from the game's slots in order, RotWK's filler was not read, S-1121)
	std::vector<int> packetRouterOrder;
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
	int nextPacketRouterSlot(int slot) const; ///< RW 0x8D3ECD (8 or more: none)
	// lane MP-2 (review): who decides that `target` leaves the game. The decider is the first slot of the agreed fallback order (the lobby's, never edited)
	// whose predecessors are all the target or gone; it names the predecessors it holds gone (a mask of slots) in its DISCONNECTPLAYER. dropClaims: may
	// `sender` decide on this peer's knowledge, and which predecessors it claims gone. verifyDropClaims, on a receiver: -1 refused (a predecessor that is
	// not the target is not claimed: decided on the message and the agreed order alone, the same on every peer), 0 pending (a claimed leave / drop has not
	// been applied here yet: the drop waits for it, and the frames from the drop frame on wait for the drop), 1 accepted
	bool dropClaims(int sender, int target, std::uint8_t &gone) const;
	int verifyDropClaims(int sender, int target, std::uint8_t gone) const;
	// lane MP-2 (review): a membership change at `frame` is still possible here: this peer has not run that frame
	void reportError(const std::string &e) { m_errors.push_back(e); }
	bool frameStillOpen(UnsignedInt frame) const { return !m_relayedAny || frame > m_lastRelayed; }
	void sendDirect(const NetCommandMsg &c);  ///< ZH sendLocalCommandDirect: to every other connected slot, not frame data
	void sendPlayerDestruct(int slot);        ///< ZH DisconnectManager::sendPlayerDestruct: DESTROYPLAYER at the execution frame + 1
	void disconnectPlayer(int slot, UnsignedInt frame); ///< RW 0x8D57D8: the slot is not waited for from `frame` on, its connection is retired
	void sendFrameDataToPlayer(int slot, UnsignedInt startFrame, UnsignedInt endFrame); ///< RW 0x8D4761
	void notePlayerLatestFrame(int slot, UnsignedInt frame);                            ///< RW 0x8D4884 (+ 0x12060)

	// the acceptance stops of the network layer (docs/STOPS.md S-720 .. S-725), one line each; every peer report carries them
	static std::vector<std::string> stopLines();

private:
	struct FrameData // ZH FrameData: one slot's data for one frame
	{
		int commandCount = -1; ///< from the FRAMEINFO; -1 until it arrived
		std::vector<std::shared_ptr<GameMessage>> commands;
		bool fromResend = false; ///< lane MP-2: filled whole by a FRAMERESEND (the sender's own late commands for it are dropped)
		int runAheadChange = -1; ///< lane MP-2: the slot's RUNAHEAD for this frame (counted in the frame info like a command)
		size_t entries() const { return commands.size() + (runAheadChange >= 0 ? 1 : 0); }
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
	};
	void send(const NetCommandMsg &c);
	void receiveAll();
	void file(const NetCommandMsg &c, int fromSlot);
	void checkCRCs(UnsignedInt frame, const std::map<int, std::uint32_t> &crcs);
	bool slotNeeded(int slot, UnsignedInt frame) const;

	NetworkConfig m_config;
	NetTransport &m_transport;
	std::array<SlotData, MAX_SLOTS> m_slots;
	UnsignedInt m_lastExecutionFrame = 0;
	long long m_lastFrameAnnounced = -1; ///< ZH m_lastFrameCompleted
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
		std::array<int, MAX_SLOTS> runAheadChange{ { -1, -1, -1, -1, -1, -1, -1, -1 } };
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
	int m_minRunAhead = 2;
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
	int m_baseRunAhead = 2;
	int runAheadAt(UnsignedInt logicFrame) const;
	// lane MP-2 (review): the packet router of each frame (a drop of the router hands the role on at the drop frame): only its RUNAHEAD is taken
	std::vector<std::pair<UnsignedInt, int>> m_routerHistory;
	int routerAt(UnsignedInt frame) const;
};
