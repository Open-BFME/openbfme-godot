// OpenBFME unit tests: unrolled MD5 matches the scalar RFC 1321 loop. GPL-3.0.

#include "doctest.h"

#include "Common/MD5.h"

#include <cstdint>
#include <string>
#include <vector>

namespace
{
std::vector<std::uint8_t> lcgBytes(size_t n, std::uint32_t seed)
{
	std::vector<std::uint8_t> out(n);
	std::uint32_t x = seed;
	for (size_t i = 0; i < n; ++i)
	{
		x = x * 1664525u + 1013904223u;
		out[i] = (std::uint8_t)(x >> 24);
	}
	return out;
}

void checkSame(const void *data, size_t size)
{
	const std::string fast = MD5::ofBytes(data, size);
	const std::string ref = MD5::ofBytesReference(data, size);
	CHECK(fast == ref);
	CHECK(fast.size() == 32);
}
}

TEST_CASE("MD5 unrolled kernel matches the scalar reference")
{
	CHECK(MD5::ofBytes("", 0) == "d41d8cd98f00b204e9800998ecf8427e");
	CHECK(MD5::ofBytesReference("", 0) == "d41d8cd98f00b204e9800998ecf8427e");
	CHECK(MD5::ofBytes("abc", 3) == "900150983cd24fb0d6963f7d28e17f72");
	const std::string digits = "12345678901234567890123456789012345678901234567890123456789012345678901234567890";
	CHECK(MD5::ofBytes(digits.data(), digits.size()) == "57edf4a22be3c955ac49da2e2107b67a");
	CHECK(MD5::ofBytesReference(digits.data(), digits.size()) == MD5::ofBytes(digits.data(), digits.size()));

	for (size_t n = 0; n <= 256; ++n)
	{
		const std::vector<std::uint8_t> bytes = lcgBytes(n, 0x4d4435u + (std::uint32_t)n);
		checkSame(bytes.data(), bytes.size());
	}

	const std::vector<std::uint8_t> big = lcgBytes(1u << 20, 0x12345678u);
	checkSame(big.data(), big.size());
	checkSame(big.data() + 1, big.size() - 1);
	checkSame(big.data() + 63, 100000);

	const size_t boundaries[] = {55, 56, 63, 64, 65, 119, 128, 4095, 4096, 4097, 100000};
	for (size_t n : boundaries)
	{
		checkSame(big.data(), n);
	}
}

TEST_CASE("MD5 streaming: empty and null updates between chunks change nothing")
{
	const std::string text(200, 'q');
	MD5 whole;
	whole.update(text.data(), text.size());
	const std::string expected = whole.finalHex();
	// a partial block, then empty updates (one with a null pointer), at and around the 64-byte block boundary
	MD5 streamed;
	streamed.update(text.data(), 1);
	streamed.update(nullptr, 0);
	streamed.update(text.data() + 1, 63);
	streamed.update(nullptr, 0);
	streamed.update(text.data() + 64, 0);
	streamed.update(text.data() + 64, 70);
	streamed.update(nullptr, 0);
	streamed.update(text.data() + 134, 66);
	CHECK(streamed.finalHex() == expected);
}
