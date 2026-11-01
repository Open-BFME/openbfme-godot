// OpenBFME. GPL-3.0.
#include "GameNetwork/RelayMembership.h"

#include <limits>
#include <utility>

namespace RelayMembership
{
namespace
{
bool fail(std::string &error, const char *message)
{
	error = message;
	return false;
}

bool valid(const Snapshot &s, std::string &error)
{
	if (!s.slotMask || (s.active | s.held | s.departed) != s.slotMask
		|| (s.active & s.held) || (s.active & s.departed) || (s.held & s.departed)
		|| (s.loaded & ~s.active))
	{
		return fail(error, "invalid canonical membership partition");
	}
	for (unsigned slot = 0; slot < s.ticketGenerations.size(); ++slot)
	{
		if (!(s.slotMask & (1u << slot)) && s.ticketGenerations[slot])
		{
			return fail(error, "ticket generation outside canonical membership");
		}
	}
	if (s.clockValid && (s.paused || s.loaded != s.active))
	{
		return fail(error, "canonical clock cannot run while paused or loading");
	}
	if (!s.clockValid && (s.originMs || s.firstFrame))
	{
		return fail(error, "inactive canonical clock must have zero origin and first frame");
	}
	return true;
}

void put(std::vector<std::uint8_t> &out, std::uint64_t value, unsigned width)
{
	for (unsigned i = 0; i < width; ++i)
	{
		out.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
	}
}

std::uint64_t get(const std::vector<std::uint8_t> &in, size_t &offset, unsigned width)
{
	std::uint64_t value = 0;
	for (unsigned i = 0; i < width; ++i)
	{
		value |= static_cast<std::uint64_t>(in[offset++]) << (8 * i);
	}
	return value;
}
}

bool encode(const Snapshot &snapshot, std::vector<std::uint8_t> &bytes, std::string &error)
{
	if (!valid(snapshot, error))
	{
		return false;
	}
	std::vector<std::uint8_t> out{ 'O', 'B', 'M', 'S', 1, 0,
		static_cast<std::uint8_t>((snapshot.paused ? 1 : 0) | (snapshot.clockValid ? 2 : 0)), 0 };
	for (auto mask : { snapshot.slotMask, snapshot.active, snapshot.held, snapshot.departed, snapshot.loaded })
	{
		put(out, mask, 2);
	}
	for (auto generation : snapshot.ticketGenerations)
	{
		put(out, generation, 2);
	}
	put(out, snapshot.originMs, 8);
	put(out, snapshot.firstFrame, 4);
	bytes.swap(out);
	error.clear();
	return true;
}

bool decode(const std::vector<std::uint8_t> &bytes, Snapshot &snapshot, std::string &error)
{
	if (bytes.size() != 62 || bytes[0] != 'O' || bytes[1] != 'B' || bytes[2] != 'M' || bytes[3] != 'S'
		|| bytes[4] != 1 || bytes[5] || (bytes[6] & ~3u) || bytes[7])
	{
		return fail(error, "unsupported or malformed canonical membership snapshot");
	}
	Snapshot out;
	out.paused = (bytes[6] & 1u) != 0;
	out.clockValid = (bytes[6] & 2u) != 0;
	size_t offset = 8;
	for (auto field : { &out.slotMask, &out.active, &out.held, &out.departed, &out.loaded })
	{
		*field = static_cast<std::uint16_t>(get(bytes, offset, 2));
	}
	for (auto &generation : out.ticketGenerations)
	{
		generation = static_cast<std::uint16_t>(get(bytes, offset, 2));
	}
	out.originMs = get(bytes, offset, 8);
	out.firstFrame = static_cast<std::uint32_t>(get(bytes, offset, 4));
	if (!valid(out, error))
	{
		return false;
	}
	snapshot = out;
	error.clear();
	return true;
}

Cursor::Cursor(std::uint64_t game, std::uint64_t epoch, std::uint16_t slotMask, std::uint32_t firstFrame)
	: m_slotMask(slotMask), m_nextFrame(firstFrame)
{
	m_stamp.game = game;
	m_stamp.epoch = epoch;
}

bool Cursor::accept(const RelayCommit::Record &record, const FrameValidator &validateFrame, std::string &error)
{
	const auto &stamp = record.stamp;
	if (m_ended || !m_stamp.game || !m_stamp.epoch || !m_slotMask
		|| stamp.game != m_stamp.game || stamp.epoch != m_stamp.epoch
		|| m_stamp.sequence == std::numeric_limits<std::uint64_t>::max() || stamp.sequence != m_stamp.sequence + 1
		|| stamp.frame != m_nextFrame)
	{
		return fail(error, "canonical record is terminal, out of order, or bound to another game");
	}
	if (record.kind == RelayCommit::Kind::Membership)
	{
		if (m_stamp.membershipGeneration == std::numeric_limits<std::uint64_t>::max()
			|| stamp.membershipGeneration != m_stamp.membershipGeneration + 1)
		{
			return fail(error, "canonical membership generation is not consecutive");
		}
		Snapshot next;
		if (!decode(record.payload, next, error))
		{
			return false;
		}
		if (next.slotMask != m_slotMask || (m_staged.departed & ~next.departed))
		{
			return fail(error, "canonical membership changes original slots or restores a departed slot");
		}
		for (size_t slot = 0; slot < next.ticketGenerations.size(); ++slot)
		{
			const auto old = m_staged.ticketGenerations[slot], generation = next.ticketGenerations[slot];
			if (generation < old || static_cast<unsigned>(generation) > static_cast<unsigned>(old) + 1)
			{
				return fail(error, "canonical ticket generation is out of order");
			}
		}
		if (next.clockValid && (!m_hasMembership || !m_staged.clockValid))
		{
			if (next.firstFrame != m_nextFrame)
			{
				return fail(error, "canonical clock activation does not start at the next frame");
			}
		}
		else if (next.clockValid && (next.originMs != m_staged.originMs || next.firstFrame != m_staged.firstFrame))
		{
			return fail(error, "canonical clock origin changed without a committed pause or load");
		}
		m_staged = next;
		m_hasMembership = true;
	}
	else
	{
		if (stamp.membershipGeneration != m_stamp.membershipGeneration)
		{
			return fail(error, "canonical record references another membership generation");
		}
		if (record.kind == RelayCommit::Kind::Frame)
		{
			if (!m_hasMembership || !m_staged.clockValid || !validateFrame || m_nextFrame == std::numeric_limits<std::uint32_t>::max())
			{
				return fail(error, "canonical frame has no active clock or validator, or overflows");
			}
			if (!validateFrame(record, m_staged, error))
			{
				if (error.empty())
				{
					error = "invalid canonical frame payload";
				}
				return false;
			}
			m_departedAtFrame = static_cast<std::uint16_t>(m_staged.departed & ~m_applied.departed);
			m_applied = m_staged;
			m_hasAppliedFrame = true;
			++m_nextFrame;
		}
		else if (record.kind == RelayCommit::Kind::End && record.payload.empty())
		{
			m_ended = true;
		}
		else
		{
			return fail(error, "unsupported canonical record kind or end payload");
		}
	}
	m_stamp = stamp;
	error.clear();
	return true;
}
}
