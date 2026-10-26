// OpenBFME. GPL-3.0.
//
// RelayLog (lane ONLINE-2, docs/ONLINE.md phase O1: "the server-side command log -> replay"): reads the online relay's command log
// (services/online/relay/RelayCore.h, "Log format") and assembles the game's frame command lists as every peer's Network did (Network::relayCommands: per
// execution frame, the network slots in slot order, each slot's commands in the order it issued them, the message's player the SENDER's: file() sets it
// from slotPlayerIndex; a DESTROYPLAYER is MSG_SELF_DESTRUCT { TRUE } of the target slot's player; a slot runs no frame from its PLAYERLEAVE frame on).
// The result is written as an OpenBFME replay (Common/Recorder.h) whose hash records are the players' MSG_LOGIC_CRC values: the n-th CRC a slot sent is
// the state hash after frame n x crcInterval (Network::frameCompleted: the completions of frames that are multiples of the interval, in order). Playing it
// back (openbfme_peer --replay) compares them: "CRC parity" of the server's log with the game that was played.
//
// Lane ONLINE-3: the log (version 2) also holds the decider's DISCONNECTPLAYERs (the frame a dropped player's frames end on every peer), the packet
// router's REJOINs (frame commands: the slot is a member again from the frame after) and a REJOINED record where a slot's new incarnation (its restarted
// game) bound: a slot's commands after it are that incarnation's. assemble() follows every incarnation (from its join frame to its drop or leave) and maps
// a rejoined incarnation's CRCs to their frames by execution frame (the n-th CRC rule holds only from frame 0). `live`: the game still runs (a rejoining
// client's catch-up): a member without a FRAMEINFO for a frame ends the complete frames there, instead of counting as gone.
// Limits (stop S-3710): a player whose frames end with neither a PLAYERLEAVE nor a logged drop (the log ends: a crash at the end of the game) is taken as
// gone from the first frame it has no FRAMEINFO for (reported in `problems`); a frame whose commands are not complete in the log (a log gap on every stream
// of that sender) is reported too; nothing is invented.
//
// Not simulation code: it reads a file and produces the input the replay playback feeds through the audited lockstep path.

#pragma once

#include "Common/Recorder.h"
#include "GameNetwork/NetPacket.h"
#include "GameNetwork/Network.h"
#include "GameNetwork/RelayCommit.h"
#include "GameNetwork/RelayProtocol.h"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct RelayLogFile
{
	std::uint64_t gameId = 0;
	std::uint16_t slotMask = 0;
	std::uint32_t startUnix = 0;
	struct Command
	{
		int sender = 0;
		std::uint32_t ms = 0;
		NetCommandMsg msg;
	};
	std::vector<Command> commands;
	std::vector<std::string> events; ///< "ms slot N bound|rebound|bye|rejoined generation G"
	struct Rejoined ///< lane ONLINE-3: a REJOINED record: the slot's commands from `firstCommand` on are its new incarnation's
	{
		int slot = 0;
		int generation = 0;
		size_t firstCommand = 0;
	};
	struct Input ///< lane ANTICHEAT-1: an INPUT record (log version 4, kind 7): the slot's c2r datagrams as AUTH-1 verified them
	{
		int slot = 0;
		bool hadKey = false; ///< authenticated
		unsigned long long signedIn = 0, plainIn = 0, badTag = 0, replayed = 0, unsignedRefused = 0, slotMismatch = 0, malformed = 0;
	};
	std::vector<Input> inputs;
	std::vector<Rejoined> rejoins;
	int version = 0;
	std::vector<std::string> gaps;   ///< "sender S target T at ms: reason"
	bool complete = false;           ///< the END record was read
	int endReason = 0;               ///< 1 every slot said BYE, 2 silence, 3 relay shutdown
	std::array<unsigned long long, RelayProtocol::kMaxSlots> datagramsIn{}, bytesIn{}, dropped{};
	std::vector<RelayCommit::Record> canonical; ///< lane ROUTER-1: kind-6 records — a canonical game's committed OBRC stream

	// online batch r4: a version-2 log of a game a previous-release HELLO opened keeps its INPUT records in a sidecar next to it (<log>.input,
	// RelayCore.h "INPUT sidecar"): load reads it when it exists (a malformed or another game's sidecar is an error)
	bool inputSidecar = false;      ///< `inputs` came from the sidecar
	bool legacyAuthAnomaly = false; ///< the sidecar's flag: a player authenticated (AUTH-1) in a game whose log is version 2 (honest games never mix builds)
	int openerSlot = -1;            ///< the sidecar's: the player slot whose HELLO opened the game (and so chose version 2)
	unsigned long long authenticatedHellos = 0; ///< the sidecar's: the authenticated player HELLOs the relay saw in that game
	static constexpr size_t kMaxInputSidecarBytes = 2048; ///< online batch r6: load refuses a larger one before reading it (valid: at most 527)
	static bool parseInputSidecar(const std::vector<std::uint8_t> &bytes, const std::string &logName, RelayLogFile &io, std::string *error);
	static bool load(const std::string &path, RelayLogFile &out, std::string *error);
	// lane ONLINE-3: `partialTail`: the log is still being written (a rejoining client fetched it from the relay): a record cut at the end is not an
	// error, it and what follows are left out
	static bool parse(const std::vector<std::uint8_t> &bytes, RelayLogFile &out, std::string *error, bool partialTail = false);
};

namespace RelayLog
{
struct Assembled
{
	std::map<UnsignedInt, CommandList> frames;         ///< execution frame -> its command list (frames with commands)
	std::map<UnsignedInt, std::uint32_t> hashes;       ///< logic frame after the frame ran -> the players' agreed CRC
	UnsignedInt lastCompleteFrame = 0;                 ///< the last frame every needed slot announced (FRAMEINFO)
	bool anyComplete = false;
	unsigned long long crcFrames = 0, crcDisagreements = 0;
	std::array<UnsignedInt, RelayProtocol::kMaxSlots> leaveFrame{};           ///< per slot: the frame its latest incarnation runs no more from (0xFFFFFFFF: to the end)
	std::vector<std::string> problems;                 ///< what makes the replay differ from the game (S-3710), or the log incomplete
	// lane ONLINE-3 (the rejoining client's catch-up)
	std::vector<Network::MembershipEvent> membership;  ///< drops, leaves, rejoins and hand-overs, in log order
	std::map<UnsignedInt, int> runAheadChanges;        ///< frame -> the RUNAHEAD it ran with (in effect from the frame after)
	int runAheadAfter(UnsignedInt frame, int initial) const; ///< the run-ahead in effect after `frame` ran
	std::uint16_t disconnectedSlotsAt(UnsignedInt nextFrame) const; ///< score presentation at this playback boundary (bit = slot 0 .. 15); a graceful leave is not a disconnect
	struct SlotFrame
	{
		std::vector<std::shared_ptr<GameMessage>> commands;
		int runAheadChange = -1;
		std::vector<int> rejoins;
	};
	// per slot, the complete frames (FRAMEINFO and every entry) of its latest incarnation: what a rejoined client fills its frames after the switch with
	std::array<std::map<UnsignedInt, SlotFrame>, RelayProtocol::kMaxSlots> latestFrames;
	std::array<int, RelayProtocol::kMaxSlots> incarnations{};                 ///< per slot, how many incarnations the log holds (1 + its REJOINED records)
	std::array<UnsignedInt, RelayProtocol::kMaxSlots> joinFrame{};            ///< per slot, the join frame of its latest incarnation (0xFFFFFFFF: its REJOIN has not run yet)
	std::vector<ReplayFile::View> views;               ///< lane ANTICHEAT-1: the players' VIEW commands, in log order (writeReplay keeps them)
};
// `config`: slotPlayerIndex (from the loaded game) and crcInterval of the game; the network slots are the log's slot mask. `live`: see above
bool assemble(const RelayLogFile &log, const NetworkConfig &config, Assembled &out, std::string *error, bool live = false);
// writes the replay (no end record: the server never saw the final state; playback runs to the last CRC frame)
bool writeReplay(const std::string &path, const ReplayHeader &header, const Assembled &a, std::string *error);
// lane ANTICHEAT-1: D7, packet integrity: the report (tools/integrity/README.md) of the log's INPUT records (version 4, or a version-2 log's sidecar)
std::string packetIntegrityReport(const RelayLogFile &log, const NetworkConfig &config, const Assembled &a, const std::string &gameId);
// lane ONLINE-2's acceptance stops (S-3710, S-3711): reported by every online peer report, the log conversion and GameWorld.net_status of an online game
std::vector<std::string> stopLines();
} // namespace RelayLog
