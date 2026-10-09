// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Recorder (ZH Common/Recorder.cpp), lane MP-1: replays of OpenBFME games. A replay is the start of the game plus every logic frame's command list; playing
// it back through the same logic gives the same game, and a hash recorded for every frame proves it (the determinism regression tool).
//
// DONOR FACTS (ZH Recorder.cpp): the replay header carries the game (GameInfo, the seed) and the recording player; the body is the command stream frame by
// frame as the logic executed it (Recorder::writeToFile is called from GameLogic for every message of the frame's command list), MSG_LOGIC_CRC included;
// playback (RECORDERMODETYPE_PLAYBACK) appends the recorded commands to the command list of their frame and ignores the local player's input;
// handleCRCMessage compares the recorded CRCs with the playback's own and reports a mismatch (the "replay desync").
// OpenBFME DIFFERENCES (enhanced profile, PLAN: "own replays"): the file format is OpenBFME's (retail .BfME2Replay playback is REPLAY-1, stop S-722), and
// besides the CRC messages every frame's state hash is recorded, so playback names the FIRST frame that differs.
//
// Format version 3 (little-endian, NetPacket's byte writer; 3: lane HERO-2's slot Create-a-Heroes in the GameInfo): "OBFMEREP" (8 bytes), u32 version 3, str profile identity (GameNetwork/ProfileIdentity.h: the
// text; playback refuses a replay of another identity before loading the game), the NewGameMessage (NetPacket::writeNewGameMessage), i32 recording slot,
// u32 RNG algorithm, 8 x i32 slot -> player index of the network players, i32 run-ahead, i32 CRC interval; then records:
//   u8 1  frame:  u32 frame, u32 n, n x GameMessage (NetPacket::writeGameMessage, format 2: u32 argument counts)   (only frames with commands)
//   u8 2  hash:   u32 frame (the logic frame after the frame ran), u32 state hash
//   u8 3  end:    u32 final frame, u32 final state hash
//   u8 4  lost:   u32 frame, u32 index, str reason   (a command of the frame that could not be encoded: the recording is incomplete and playback says so)
// Version 1 (u8 argument counts, a free profile string) is refused.

#pragma once

#include "Common/RandomValue.h"
#include "GameClient/LiveGameFrameDriver.h"
#include "GameNetwork/ProfileIdentity.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameNetwork/Network.h"

#include <cstdint>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct ReplayHeader
{
	ProfileIdentity profile; ///< the recording build's (GameNetwork/ProfileIdentity.h)
	NewGameMessage game;
	int recordingSlot = 0;
	RandomAlgorithm algorithm = RandomAlgorithm::ZH_CarryChain;
	NetworkConfig network; ///< slotPlayerIndex, runAhead, crcInterval of the recorded game (localSlot = recordingSlot)
};

struct ReplayFile
{
	ReplayHeader header;
	std::map<UnsignedInt, std::vector<GameMessage>> frames; ///< frame -> its command list
	std::map<UnsignedInt, std::uint32_t> hashes;            ///< logic frame after the frame ran -> state hash
	UnsignedInt finalFrame = 0;
	std::uint32_t finalHash = 0;
	bool complete = false; ///< the end record was read
	std::vector<std::string> lost; ///< commands the recording could not encode ("frame F command I: reason"): the replay is incomplete
	// "" when the replay was recorded by this profile, else why not (PLAN: different identifiers refuse to play)
	std::string profileMismatch(const ProfileIdentity &running) const { return ProfileIdentity::difference(running, header.profile); }
	unsigned long long commandCount() const;
	// false + *error on a bad magic, version, record or truncation (a missing end record is not an error: `complete` says it)
	static bool load(const std::string &path, ReplayFile &out, std::string *error);
	static bool parse(const std::vector<std::uint8_t> &bytes, ReplayFile &out, std::string *error);
};

// writes a replay while the game runs
class ReplayWriter
{
public:
	bool open(const std::string &path, const ReplayHeader &header, std::string *error);
	bool isOpen() const { return m_out.is_open(); }
	void recordFrame(UnsignedInt frame, const CommandList &commands);
	void recordHash(UnsignedInt frameAfter, std::uint32_t hash);
	void close(UnsignedInt finalFrame, std::uint32_t finalHash);
	// commands that could not be encoded (also written as `lost` records) and write failures of the file
	const std::vector<std::string> &errors() const { return m_errors; }

private:
	void write(const std::vector<std::uint8_t> &b);
	std::ofstream m_out;
	std::vector<std::string> m_errors;
	bool m_writeFailed = false;
};

// Plays a replay back as a LiveGame frame driver (ZH RECORDERMODETYPE_PLAYBACK): the recorded commands go to their frames, the local input is ignored
// (counted), and every frame's state hash is compared with the recorded one.
class ReplayPlayback : public LiveGameFrameDriver
{
public:
	explicit ReplayPlayback(const ReplayFile &replay) : m_replay(replay) {}
	// SMOOTH-1 (review r4): the protocol owner half takes the recorded batches and compares the completions' hashes (captured on the simulation owner)
	Capture capture() const override;
	void pump(UnsignedInt protocolFrame, CommandList &pending) override;
	bool acquire(UnsignedInt frame, FrameBatch &out) override;
	void completed(const FrameCompletion &c) override;

	// every recorded frame ran and its completion was consumed (the protocol frame reached the recording's final frame)
	bool finished() const { return m_pumped && m_protocolFrame >= m_replay.finalFrame; }
	bool finished(const GameLogic &logic) const { return logic.getFrame() >= m_replay.finalFrame; }
	struct Mismatch
	{
		UnsignedInt frame = 0;
		std::uint32_t recorded = 0, played = 0;
	};
	// the first frame whose hash differs (frame 0 / hasMismatch false when none)
	bool hasMismatch() const { return m_hasMismatch; }
	const Mismatch &firstMismatch() const { return m_first; }
	unsigned long long hashesCompared() const { return m_compared; }
	unsigned long long mismatches() const { return m_mismatches; }
	unsigned long long localInputIgnored() const { return m_ignored; }

private:
	const ReplayFile &m_replay;
	bool m_hasMismatch = false;
	Mismatch m_first;
	unsigned long long m_compared = 0, m_mismatches = 0, m_ignored = 0;
	UnsignedInt m_protocolFrame = 0;
	bool m_pumped = false;
};
