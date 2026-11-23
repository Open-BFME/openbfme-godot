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
#include <initializer_list>

template <int NUMBITS>
class BitFlags
{
public:
	enum
	{
		NUM_BITS = NUMBITS,
		NUM_WORDS = (NUMBITS + 31) / 32
	};

	// Both editions: RW 0x0064C39A (DONOR BFME2 0x0024B8FA) zero-initializes all words.
	BitFlags() { std::memset(m_words, 0, sizeof(m_words)); }
	// Both editions: RW 0x00444D65 (DONOR BFME2 0x0004493A) copies all words.
	BitFlags(const BitFlags &other) { std::memcpy(m_words, other.m_words, sizeof(m_words)); }
	BitFlags &operator=(const BitFlags &other)
	{
		if (this != &other)
		{
			std::memcpy(m_words, other.m_words, sizeof(m_words));
		}
		return *this;
	}
	// Both editions: RW 0x0068FC98 (DONOR BFME2 0x0028F5A3) single-bit constructor (-1 leaves mask empty).
	explicit BitFlags(int bit)
	{
		std::memset(m_words, 0, sizeof(m_words));
		if (bit != -1)
		{
			set(bit, true);
		}
	}
	// Both editions: RW 0x00851770 (DONOR BFME2 0x0044DEC9) multi-bit constructor (-1 entries are ignored).
	BitFlags(std::initializer_list<int> bits)
	{
		std::memset(m_words, 0, sizeof(m_words));
		for (int b : bits)
		{
			if (b != -1)
			{
				set(b, true);
			}
		}
	}

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

	// Both editions: RW 0x008C2233 (DONOR BFME2 0x0028F528 Rva0028F528::rva0028F528) 32-bit word popcount.
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

	// Both editions: RW 0x0073C77B (DONOR BFME2 0x0033ACBA) popcount of word-wise intersection.
	int countIntersection(const BitFlags &that) const
	{
		int n = 0;
		for (int w = 0; w < NUM_WORDS; ++w)
		{
			n += popcount(m_words[w] & that.m_words[w]);
		}
		return n;
	}
	// Both editions: RW 0x0073C7D2 (DONOR BFME2 0x0033AD11) popcount of (~this & that).
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

	// Both editions: RW 0x007628A9 (DONOR BFME2 0x00360C1F) word-array equality.
	bool operator==(const BitFlags &that) const { return std::memcmp(m_words, that.m_words, sizeof(m_words)) == 0; }
	bool operator!=(const BitFlags &that) const { return !(*this == that); }
	// A strict total order, only so a BitFlags can key a std::map (the sparse matcher's cache).
	bool operator<(const BitFlags &that) const { return std::memcmp(m_words, that.m_words, sizeof(m_words)) < 0; }
	// Both editions: RW 0x0073C692 (DONOR BFME2 0x0033ABD1 SparseMatchFinder::MapHelper::operator()) bit-order strict weak ordering.
	bool lessByBitOrder(const BitFlags &that) const
	{
		for (int i = 0; i < NUMBITS; ++i)
		{
			const bool a = test(i);
			const bool b = that.test(i);
			if (a != b)
			{
				return b;
			}
		}
		return false;
	}

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
