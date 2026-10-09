// OpenBFME. GPL-3.0.
//
// BitFlags<NUMBITS>: a fixed-size bit set with the operations the sparse matcher and the INI bit-string parsers need.
// Port of the interface of ZH GameEngine/Include/Common/BitFlags.h (m_bits is a std::bitset there; the storage here is an
// array of 32-bit words, which is also what the retail binary uses: ModelConditionFlags is 19 words / 0x4C bytes and is
// compared with memcmp, RW 0x444D9B).
//
// countIntersection(that)        = |this & that|
// countInverseIntersection(that) = |~this & that|  (ZH BitFlags.h:208-221): the bits of `that` this set lacks.
// Unused high bits of the last word are always zero, so memcmp-style equality and the counts are exact.

#pragma once

#include <cstdint>
#include <cstring>

template <int NUMBITS>
class BitFlags
{
public:
	enum
	{
		NUM_BITS = NUMBITS,
		NUM_WORDS = (NUMBITS + 31) / 32
	};

	BitFlags() { std::memset(m_words, 0, sizeof(m_words)); }

	int size() const { return NUMBITS; }
	bool test(int i) const { return inRange(i) && ((m_words[i >> 5] >> (i & 31)) & 1u) != 0; }
	void set(int i, bool value = true)
	{
		if (!inRange(i))
		{
			return;
		}
		if (value)
		{
			m_words[i >> 5] |= (1u << (i & 31));
		}
		else
		{
			m_words[i >> 5] &= ~(1u << (i & 31));
		}
	}
	void clearBit(int i) { set(i, false); }
	void clear() { std::memset(m_words, 0, sizeof(m_words)); }

	int count() const
	{
		int n = 0;
		for (int w = 0; w < NUM_WORDS; ++w)
		{
			n += popcount(m_words[w]);
		}
		return n;
	}
	bool any() const
	{
		for (int w = 0; w < NUM_WORDS; ++w)
		{
			if (m_words[w])
			{
				return true;
			}
		}
		return false;
	}

	int countIntersection(const BitFlags &that) const
	{
		int n = 0;
		for (int w = 0; w < NUM_WORDS; ++w)
		{
			n += popcount(m_words[w] & that.m_words[w]);
		}
		return n;
	}
	int countInverseIntersection(const BitFlags &that) const
	{
		int n = 0;
		for (int w = 0; w < NUM_WORDS; ++w)
		{
			n += popcount(~m_words[w] & that.m_words[w]);
		}
		return n;
	}
	bool anyIntersectionWith(const BitFlags &that) const { return countIntersection(that) != 0; }
	bool testForAny(const BitFlags &that) const { return anyIntersectionWith(that); }
	bool testForAll(const BitFlags &that) const { return countInverseIntersection(that) == 0; }
	bool testForNone(const BitFlags &that) const { return !anyIntersectionWith(that); }

	// ZH BitFlags::clear(const BitFlags&) / set(const BitFlags&): remove / add every bit of the argument.
	void clear(const BitFlags &clr)
	{
		for (int w = 0; w < NUM_WORDS; ++w)
		{
			m_words[w] &= ~clr.m_words[w];
		}
	}
	void set(const BitFlags &add)
	{
		for (int w = 0; w < NUM_WORDS; ++w)
		{
			m_words[w] |= add.m_words[w];
		}
	}

	bool operator==(const BitFlags &that) const { return std::memcmp(m_words, that.m_words, sizeof(m_words)) == 0; }
	bool operator!=(const BitFlags &that) const { return !(*this == that); }
	// A strict total order, only so a BitFlags can key a std::map (the sparse matcher's cache).
	bool operator<(const BitFlags &that) const { return std::memcmp(m_words, that.m_words, sizeof(m_words)) < 0; }

	const std::uint32_t *words() const { return m_words; }
	std::uint32_t *words() { return m_words; }

private:
	static bool inRange(int i) { return i >= 0 && i < NUMBITS; }
	static int popcount(std::uint32_t v)
	{
		int n = 0;
		while (v)
		{
			v &= v - 1;
			++n;
		}
		return n;
	}

	std::uint32_t m_words[NUM_WORDS];
};
