// OpenBFME. GPL-3.0.
//
// StateHasher: the deterministic world state hash (lane LOGIC-1; the tool lockstep multiplayer will use to detect a desync).
//
// TARGET FACTS (PLAN rule 5; RotWK game.dat 0xA211DF, caveat S-001): retail's logic CRC step is `crc = rotl32(crc, 1) + le_word`
// (mod 2^32) with one rotate-add per trailing byte. This hasher uses the same step so it is cheap (one rotate and one add per 32-bit
// word) and so a later retail-CRC port can share it. WHAT IT HASHES is not retail's CRC input (retail xfers every module's state in a
// fixed order, stop S-001/S-080 for the numerics): it is OpenBFME's own logic state, chosen to cover every field a logic class here
// owns. Two runs of the same sequence of inputs must give the same value on every platform: floats are hashed by their bit patterns,
// strings by length and bytes, nothing by pointer value, and containers are walked in a defined order.

#pragma once

#include <cstdint>
#include <cstring>
#include <string>

class StateHasher
{
public:
	StateHasher() = default;

	void addU32(std::uint32_t v) { m_crc = ((m_crc << 1) | (m_crc >> 31)) + v; }
	void addI32(std::int32_t v) { addU32((std::uint32_t)v); }
	void addBool(bool v) { addU32(v ? 1u : 0u); }
	void addU64(std::uint64_t v)
	{
		addU32((std::uint32_t)(v & 0xFFFFFFFFu));
		addU32((std::uint32_t)(v >> 32));
	}
	void addFloat(float f)
	{
		std::uint32_t bits;
		std::memcpy(&bits, &f, sizeof(bits));
		addU32(bits);
	}
	// length, then the bytes: whole little-endian words first, then one rotate-add per trailing byte (RW 0xA211DF)
	void addString(const std::string &s)
	{
		addU32((std::uint32_t)s.size());
		const unsigned char *p = (const unsigned char *)s.data();
		size_t n = s.size();
		while (n >= 4)
		{
			addU32((std::uint32_t)p[0] | ((std::uint32_t)p[1] << 8) | ((std::uint32_t)p[2] << 16) | ((std::uint32_t)p[3] << 24));
			p += 4;
			n -= 4;
		}
		while (n--)
		{
			addU32(*p++);
		}
	}

	std::uint32_t value() const { return m_crc; }

private:
	std::uint32_t m_crc = 0;
};
