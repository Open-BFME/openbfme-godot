// OpenBFME. GPL-3.0.
// See GameNetwork/Rejoin.h.

#include "GameNetwork/Rejoin.h"

#include "GameNetwork/Transport.h"

#include <algorithm>
#include <cstdio>
#include <iterator>

void RelayLogFetch::step(Transport &transport, std::uint64_t nowMs)
{
	for (auto &kv : transport.takeRelayLogChunks())
	{
		m_asked.erase(kv.first);
		if (kv.first >= m_bytes.size())
		{
			m_early[kv.first] = std::move(kv.second);
		}
	}
	m_relaySize = std::max(m_relaySize, transport.relayLogSize());
	// the contiguous bytes: every chunk was cut at the relay's size then, which only grows, so a chunk asked below a size the relay reported is whole
	for (auto it = m_early.find((std::uint32_t)m_bytes.size()); it != m_early.end(); it = m_early.find((std::uint32_t)m_bytes.size()))
	{
		const bool empty = it->second.empty();
		m_bytes.insert(m_bytes.end(), it->second.begin(), it->second.end());
		m_early.erase(it);
		if (empty)
		{
			break;
		}
	}
	m_early.erase(m_early.begin(), m_early.lower_bound((std::uint32_t)m_bytes.size()));
	// ask: the chunks below the relay's size, then (caught up) the end again every pollMs
	const std::uint32_t have = (std::uint32_t)m_bytes.size();
	for (auto it = m_asked.begin(); it != m_asked.end();)
	{
		it = it->first < have ? m_asked.erase(it) : std::next(it);
	}
	int inFlight = 0;
	for (std::uint32_t off = have; (off < m_relaySize || off == have) && inFlight < window; off += (std::uint32_t)RelayProtocol::kLogChunkBytes)
	{
		++inFlight;
		const auto asked = m_asked.find(off);
		const bool due = asked == m_asked.end() ? (off < m_relaySize || nowMs - m_lastPoll >= pollMs) : nowMs - asked->second >= retryMs;
		if (!due || m_early.count(off))
		{
			continue;
		}
		transport.requestRelayLog(off);
		m_asked[off] = nowMs;
		++m_requests;
		if (off >= m_relaySize)
		{
			m_lastPoll = nowMs;
		}
	}
}

LiveGameFrameDriver::Capture CommandLogCatchUp::capture() const
{
	Capture c;
	c.breakdownInterval = m_crcInterval; // the CRC frames' hash and breakdown (compared with the log, kept for the CRCs sent after the rejoin)
	return c;
}

void CommandLogCatchUp::pump(UnsignedInt protocolFrame, CommandList &pending)
{
	(void)protocolFrame;
	m_ignored += pending.messages().size(); // the player's input during the catch-up does not reach the logic (as in a replay)
	pending.reset();
}

bool CommandLogCatchUp::acquire(UnsignedInt frame, FrameBatch &out)
{
	if (!m_log || !m_log->anyComplete || frame > m_log->lastCompleteFrame || frame > m_target)
	{
		return false;
	}
	out.frame = frame;
	out.commands.clear();
	const auto it = m_log->frames.find(frame);
	if (it != m_log->frames.end())
	{
		out.commands = it->second.messages();
	}
	return true;
}

void CommandLogCatchUp::completed(const FrameCompletion &c)
{
	m_reached = c.frame;
	if (!c.breakdown)
	{
		return;
	}
	Network::LiveStart::Crc crc;
	crc.frame = c.frame;
	crc.hash = c.hash;
	crc.half.frame = c.frame;
	crc.half.hash = c.hash;
	crc.half.sections = c.sections;
	crc.half.objects = c.objects;
	m_crcs[c.frame] = std::move(crc);
	while (m_crcs.size() > 64)
	{
		m_crcs.erase(m_crcs.begin()); // only the last few can still execute after the rejoin (run-ahead <= 25 frames)
	}
	if (m_log)
	{
		const auto it = m_log->hashes.find(c.frame);
		if (it != m_log->hashes.end())
		{
			compare(c.frame, c.hash, it->second);
			return;
		}
	}
	m_uncompared[c.frame] = c.hash;
	while (m_uncompared.size() > 64)
	{
		m_uncompared.erase(m_uncompared.begin());
	}
}

void CommandLogCatchUp::setLog(std::shared_ptr<const RelayLog::Assembled> log)
{
	m_log = std::move(log);
	for (auto u = m_uncompared.begin(); m_log && u != m_uncompared.end();)
	{
		const auto it = m_log->hashes.find(u->first);
		if (it == m_log->hashes.end())
		{
			++u;
			continue;
		}
		compare(u->first, u->second, it->second);
		u = m_uncompared.erase(u);
	}
}

void CommandLogCatchUp::compare(UnsignedInt frame, std::uint32_t own, std::uint32_t players)
{
	++m_compared;
	if (players != own)
	{
		++m_mismatches;
		if (m_firstMismatch.empty())
		{
			char b[96];
			std::snprintf(b, sizeof b, "frame %u: the players' CRC 0x%08X, the catch-up 0x%08X", frame, players, own);
			m_firstMismatch = b;
		}
	}
}
