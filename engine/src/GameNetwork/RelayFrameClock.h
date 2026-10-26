// OpenBFME. GPL-3.0.
// NET-SYNC: relay-owned 5 Hz boundary arithmetic, not simulation maths.
#pragma once

#include <cstdint>
#include <limits>
#include <string>

class RelayFrameClock
{
public:
	static constexpr std::uint64_t kFrameMs = 200;
	// ROUTER-1: the frame period is a per-game value from the game's START
	// (logic rate 5/10/15/20 Hz); 200 ms stays the default for legacy setups.
	static constexpr std::uint64_t kMinFrameMs = 50;
	static constexpr std::uint64_t kMaxFrameMs = 200;

	// originMs is the committed load/resume clock origin. The first batch closes
	// one period later; receipt exactly at its boundary belongs to the next batch.
	RelayFrameClock(std::uint64_t originMs, std::uint32_t firstFrame, std::uint64_t frameMs = kFrameMs)
		: m_originMs(originMs), m_firstFrame(firstFrame), m_frameMs(frameMs)
	{
	}

	bool frameAfter(std::uint64_t receiptMs, std::uint32_t &frame, std::string &error) const
	{
		if (receiptMs < m_originMs)
		{
			error = "relay order receipt precedes the committed clock origin";
			return false;
		}
		const auto periods = (receiptMs - m_originMs) / m_frameMs;
		if (periods > std::numeric_limits<std::uint32_t>::max() - m_firstFrame)
		{
			error = "relay execution frame exhausted";
			return false;
		}
		const auto candidate = m_firstFrame + static_cast<std::uint32_t>(periods);
		std::uint64_t boundary = 0;
		if (!boundaryOf(candidate, boundary, error))
		{
			return false;
		}
		frame = candidate;
		error.clear();
		return true;
	}

	bool boundaryOf(std::uint32_t frame, std::uint64_t &boundaryMs, std::string &error) const
	{
		if (frame < m_firstFrame)
		{
			error = "relay frame precedes the committed clock origin";
			return false;
		}
		const auto elapsed = (static_cast<std::uint64_t>(frame) - m_firstFrame + 1) * m_frameMs;
		if (elapsed > std::numeric_limits<std::uint64_t>::max() - m_originMs)
		{
			error = "relay frame boundary clock exhausted";
			return false;
		}
		boundaryMs = m_originMs + elapsed;
		error.clear();
		return true;
	}

private:
	std::uint64_t m_originMs;
	std::uint32_t m_firstFrame;
	std::uint64_t m_frameMs;
};
