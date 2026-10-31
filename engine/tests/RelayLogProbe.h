// OpenBFME. GPL-3.0.
//
// Relay-log assembly probes (test_online_relay.cpp): synthetic relay logs of several network slots (frames, REJOINs, REJOINED records,
// leaves, drops, run-ahead changes, incomplete frames) from a seed, and a text summary of everything RelayLog::assemble returns. The
// classic-room differential compares the summaries' hash with the one lane NETDIAG-1's assembler (0d044941d) gives for the same logs.

#pragma once

#include "GameNetwork/RelayLog.h"

#include <cstdint>
#include <sstream>
#include <string>

namespace relaylogprobe
{
inline std::uint64_t next(std::uint64_t &x)
{
	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 17;
	return x;
}

inline NetCommandMsg frameInfo(int slot, std::uint32_t frame, int count)
{
	NetCommandMsg m;
	m.type = NETCOMMANDTYPE_FRAMEINFO;
	m.slot = (std::uint8_t)slot;
	m.executionFrame = frame;
	m.commandCount = (std::uint16_t)count;
	return m;
}

// a log of 2 .. 4 network slots (slot i plays player i); every slot has 1 .. 3 incarnations
inline RelayLogFile classicLog(std::uint64_t seed, NetworkConfig &config)
{
	std::uint64_t x = seed * 0x9E3779B97F4A7C15ull + 1;
	RelayLogFile log;
	log.version = 2;
	const int slots = 2 + (int)(next(x) % 3);
	log.slotMask = (std::uint16_t)((1u << slots) - 1);
	config.slotPlayerIndex.fill(-1);
	for (int s = 0; s < slots; ++s)
	{
		config.slotPlayerIndex[(size_t)s] = s;
	}
	auto add = [&](int sender, const NetCommandMsg &m) {
		RelayLogFile::Command c;
		c.sender = sender;
		c.msg = m;
		log.commands.push_back(c);
	};
	for (int s = 0; s < slots; ++s)
	{
		const int incarnations = 1 + (int)(next(x) % 3);
		std::uint32_t start = 0;
		for (int g = 0; g < incarnations; ++g)
		{
			if (g > 0)
			{
				RelayLogFile::Rejoined j;
				j.slot = s;
				j.generation = g;
				j.firstCommand = log.commands.size();
				log.rejoins.push_back(j);
				start += (std::uint32_t)(next(x) % 8) + ((next(x) % 10) == 0 ? 1000u : 0u);
			}
			const std::uint32_t length = (std::uint32_t)(next(x) % 14);
			for (std::uint32_t f = start; f < start + length; ++f)
			{
				int entries = 0;
				if (next(x) % 5 == 0)
				{
					NetCommandMsg r;
					r.type = NETCOMMANDTYPE_REJOIN;
					r.slot = (std::uint8_t)s;
					r.executionFrame = f;
					r.targetSlot = (std::uint8_t)(next(x) % (std::uint64_t)slots);
					add(s, r);
					++entries;
				}
				if (next(x) % 8 == 0)
				{
					NetCommandMsg r;
					r.type = NETCOMMANDTYPE_RUNAHEAD;
					r.slot = (std::uint8_t)s;
					r.executionFrame = f;
					r.frame = 1 + (std::uint32_t)(next(x) % 4);
					add(s, r);
					++entries;
				}
				add(s, frameInfo(s, f, entries + (next(x) % 15 == 0 ? 1 : 0)));
			}
			if (next(x) % 6 == 0)
			{
				NetCommandMsg m;
				m.type = NETCOMMANDTYPE_PLAYERLEAVE;
				m.slot = (std::uint8_t)s;
				m.executionFrame = start + (std::uint32_t)(next(x) % (length + 1));
				add(s, m);
			}
			if (next(x) % 6 == 0)
			{
				NetCommandMsg m;
				m.type = NETCOMMANDTYPE_DISCONNECTPLAYER;
				m.slot = (std::uint8_t)s;
				m.targetSlot = (std::uint8_t)(next(x) % (std::uint64_t)slots);
				m.frame = start + (std::uint32_t)(next(x) % (length + 2));
				add(s, m);
			}
			start += length;
		}
	}
	return log;
}

// RELAY-4: a one-human room's log as the relay writes it (one network slot, 0 .. 7, plays player = slot; CRC interval 5): its first incarnation
// from frame 0, then 0 .. 3 restarts, each a REJOINED record and the restarted game's own REJOIN of itself some frames after the last frame
// the previous incarnation ran (with or without a FRAMEINFO counting it), then 0 .. 19 frames (0: the REJOIN is the log's last frame);
// sometimes a run-ahead change, a leave at the end, the END of every BYE
inline RelayLogFile soloLog(std::uint64_t seed, NetworkConfig &config)
{
	std::uint64_t x = seed * 0xD1B54A32D192ED03ull + 7;
	RelayLogFile log;
	log.version = 2;
	const int s = (int)(next(x) % 8);
	log.slotMask = (std::uint16_t)(1u << s);
	config.slotPlayerIndex.fill(-1);
	config.slotPlayerIndex[(size_t)s] = s;
	config.crcInterval = 5;
	auto add = [&](const NetCommandMsg &m) {
		RelayLogFile::Command c;
		c.sender = s;
		c.msg = m;
		log.commands.push_back(c);
	};
	auto frame = [&](std::uint32_t f, int extra) {
		int entries = extra;
		if (f % 5 == 4)
		{
			NetCommandMsg m;
			m.type = NETCOMMANDTYPE_GAMECOMMAND;
			m.slot = (std::uint8_t)s;
			m.executionFrame = f;
			m.message = std::make_shared<GameMessage>(MSG_LOGIC_CRC, s);
			m.message->appendIntegerArgument((int)(f * 7 + 1));
			add(m);
			++entries;
		}
		if (next(x) % 9 == 0)
		{
			NetCommandMsg m;
			m.type = NETCOMMANDTYPE_RUNAHEAD;
			m.slot = (std::uint8_t)s;
			m.executionFrame = f;
			m.frame = 1 + (std::uint32_t)(next(x) % 3);
			add(m);
			++entries;
		}
		add(frameInfo(s, f, entries));
	};
	std::uint32_t end = 1 + (std::uint32_t)(next(x) % 30); // the first incarnation runs frames 0 .. end - 1
	for (std::uint32_t f = 0; f < end; ++f)
	{
		frame(f, 0);
	}
	const int restarts = (int)(next(x) % 4);
	for (int g = 1; g <= restarts; ++g)
	{
		RelayLogFile::Rejoined j;
		j.slot = s;
		j.generation = g;
		j.firstCommand = log.commands.size();
		log.rejoins.push_back(j);
		const std::uint32_t at = end + (std::uint32_t)(next(x) % 10) + ((next(x) % 8) == 0 ? 500u : 0u);
		NetCommandMsg r;
		r.type = NETCOMMANDTYPE_REJOIN;
		r.slot = (std::uint8_t)s;
		r.executionFrame = at;
		r.targetSlot = (std::uint8_t)s;
		add(r);
		if (next(x) % 2 == 0)
		{
			add(frameInfo(s, at, 1));
		}
		const std::uint32_t length = (std::uint32_t)(next(x) % 20);
		for (std::uint32_t f = at + 1; f <= at + length; ++f)
		{
			frame(f, 0);
		}
		end = at + length + 1;
	}
	if (next(x) % 4 == 0)
	{
		NetCommandMsg m;
		m.type = NETCOMMANDTYPE_PLAYERLEAVE;
		m.slot = (std::uint8_t)s;
		m.executionFrame = end;
		add(m);
	}
	log.endReason = next(x) % 2 == 0 ? 1 : 0;
	return log;
}

inline std::string summary(bool ok, const std::string &error, const RelayLog::Assembled &a)
{
	std::ostringstream o;
	o << ok << '|' << error << '|' << a.lastCompleteFrame << '|' << a.anyComplete << '|' << a.crcFrames << '|' << a.crcDisagreements << "|F";
	for (const auto &f : a.frames)
	{
		o << ' ' << f.first << ':' << f.second.messages().size();
	}
	o << "|H";
	for (const auto &h : a.hashes)
	{
		o << ' ' << h.first << ':' << h.second;
	}
	o << "|L";
	for (UnsignedInt l : a.leaveFrame)
	{
		o << ' ' << l;
	}
	o << "|P";
	for (const std::string &p : a.problems)
	{
		o << " [" << p << ']';
	}
	o << "|M";
	for (const auto &m : a.membership)
	{
		o << ' ' << (int)m.kind << ',' << m.slot << ',' << m.frame;
	}
	o << "|R";
	for (const auto &r : a.runAheadChanges)
	{
		o << ' ' << r.first << ':' << r.second;
	}
	o << "|S";
	for (size_t s = 0; s < a.latestFrames.size(); ++s)
	{
		o << ' ' << a.incarnations[s] << '/' << a.joinFrame[s] << '{';
		for (const auto &f : a.latestFrames[s])
		{
			o << f.first << ':' << f.second.commands.size() << ',' << f.second.runAheadChange << ',' << f.second.rejoins.size() << ';';
		}
		o << '}';
	}
	return o.str();
}

inline std::uint64_t fnv(std::uint64_t h, const std::string &text)
{
	for (unsigned char c : text)
	{
		h = (h ^ c) * 0x100000001B3ull;
	}
	return h;
}

// the hash of the summaries of logs 1 .. count, each assembled by `assemble` (RelayLog::assemble)
template <typename Assemble> std::uint64_t classicDigest(int count, Assemble assemble)
{
	std::uint64_t h = 0xCBF29CE484222325ull;
	for (int seed = 1; seed <= count; ++seed)
	{
		NetworkConfig config;
		const RelayLogFile log = classicLog((std::uint64_t)seed, config);
		RelayLog::Assembled a;
		std::string error;
		const bool ok = assemble(log, config, a, &error);
		h = fnv(h, summary(ok, error, a));
	}
	return h;
}
// RELAY-4: the hash of the summaries (and the REJOINs' generations) of one-human room logs 1 .. count (soloLog), each assembled complete and
// as a rejoining client's live copy
template <typename Assemble> std::uint64_t soloDigest(int count, Assemble assemble)
{
	std::uint64_t h = 0xCBF29CE484222325ull;
	for (int seed = 1; seed <= count; ++seed)
	{
		for (bool live : { false, true })
		{
			NetworkConfig config;
			const RelayLogFile log = soloLog((std::uint64_t)seed, config);
			RelayLog::Assembled a;
			std::string error;
			const bool ok = assemble(log, config, a, &error, live);
			std::string generations = "|G";
			for (const auto &m : a.membership)
			{
				generations += ' ' + std::to_string(m.generation);
			}
			h = fnv(h, summary(ok, error, a) + generations);
		}
	}
	return h;
}
} // namespace relaylogprobe
