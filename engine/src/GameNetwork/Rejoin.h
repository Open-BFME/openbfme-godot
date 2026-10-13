// OpenBFME. GPL-3.0.
//
// Rejoin (lane ONLINE-3, docs/ONLINE.md "Phase O3: reconnect"): what a client whose game restarted (a crash, a lost connection, a new process) needs to
// rejoin its running online game. OpenBFME's own (RotWK 2.01 has no reconnect: a dropped player is gone, RW 0x8D57D8; stop S-3900).
//
//   * RelayLogFetch: the game's command log, read from the relay in LOG_CHUNKs (RelayProtocol.h) while the game goes on (the log grows: it is polled).
//   * CommandLogCatchUp: "the START + a command log -> the state at frame F". A LiveGame frame driver that runs the assembled frames of the log
//     (RelayLog::assemble) at full speed, up to a target frame, and compares every CRC frame's state hash with the players' CRCs in the log. The same path
//     serves a later save / resume (a snapshot-free resume is this, from frame 0).
//
// The orchestration (bind with the rejoin ticket, fetch, catch up, ask to be taken back, switch to lockstep at the REJOIN's frame + 1) is NetGameSession's
// (beginRejoin / rejoinStep), shared by the headless peer and the Godot client.
//
// Not simulation code: it decides which commands the audited lockstep path runs (the log's), never what a frame computes.

#pragma once

#include "GameClient/LiveGameFrameDriver.h"
#include "GameNetwork/Network.h"
#include "GameNetwork/RelayLog.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

class Transport;

class RelayLogFetch
{
public:
	// asks for what is missing (up to `window` chunks in flight, each asked again after `retryMs`), takes the chunks that arrived; call often
	void step(Transport &transport, std::uint64_t nowMs);
	// the log's first bytes, contiguous (the relay's size when last told: relaySize)
	const std::vector<std::uint8_t> &bytes() const { return m_bytes; }
	std::uint32_t relaySize() const { return m_relaySize; }
	// everything the relay had at its last report has arrived
	bool caughtUp() const { return m_relaySize > 0 && m_bytes.size() >= m_relaySize; }
	unsigned long long requests() const { return m_requests; }
	int window = 64;
	std::uint64_t retryMs = 300;
	std::uint64_t pollMs = 250; ///< caught up: the end is asked again this often (the log grows)

private:
	std::vector<std::uint8_t> m_bytes;
	std::map<std::uint32_t, std::vector<std::uint8_t>> m_early;
	std::map<std::uint32_t, std::uint64_t> m_asked; ///< offset -> when
	std::uint32_t m_relaySize = 0;
	std::uint64_t m_lastPoll = 0;
	unsigned long long m_requests = 0;
};

class CommandLogCatchUp : public LiveGameFrameDriver
{
public:
	explicit CommandLogCatchUp(int crcInterval) : m_crcInterval(crcInterval) {}
	// the frames to run (a newer assembly of the growing log replaces the older one: its frames up to the older one's last complete frame are the same)
	// lane ONLINE-3: a frame that ran before its CRC was in the log (a spectator at the log's edge: the CRC describing frame F executes a few frames
	// later) is compared when a newer assembly has it
	void setLog(std::shared_ptr<const RelayLog::Assembled> log);
	// frames up to and including `frame` may run (0xFFFFFFFF: every complete frame of the log)
	void setTarget(UnsignedInt frame) { m_target = frame; }

	Capture capture() const override;
	void pump(UnsignedInt protocolFrame, CommandList &pending) override;
	bool acquire(UnsignedInt frame, FrameBatch &out) override;
	void completed(const FrameCompletion &c) override;

	// the logic frame the completions reached (the state after frame - 1 ran)
	UnsignedInt reached() const { return m_reached; }
	unsigned long long crcsCompared() const { return m_compared; }
	unsigned long long crcMismatches() const { return m_mismatches; }
	const std::string &firstMismatch() const { return m_firstMismatch; }
	// every CRC frame's hash and breakdown this catch-up computed (a rejoined client sends those whose MSG_LOGIC_CRC executes after it rejoined)
	const std::map<UnsignedInt, Network::LiveStart::Crc> &crcs() const { return m_crcs; }
	unsigned long long localInputIgnored() const { return m_ignored; }

private:
	int m_crcInterval;
	std::shared_ptr<const RelayLog::Assembled> m_log;
	UnsignedInt m_target = 0xFFFFFFFFu;
	UnsignedInt m_reached = 0;
	unsigned long long m_compared = 0, m_mismatches = 0, m_ignored = 0;
	std::string m_firstMismatch;
	std::map<UnsignedInt, Network::LiveStart::Crc> m_crcs;
	std::map<UnsignedInt, std::uint32_t> m_uncompared; ///< CRC frames run whose players' CRC the log did not have yet (the last 64)
	void compare(UnsignedInt frame, std::uint32_t own, std::uint32_t players);
};
