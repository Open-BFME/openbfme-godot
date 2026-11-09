// Standalone MD5 kernel microbenchmark. GPL-3.0.
// Times transformReference against transformFast on one buffer. Prints cycles
// per byte from CLOCK_THREAD_CPUTIME_ID. Exits non-zero if the digests differ.

#include "Common/MD5Kernel.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <time.h>
#include <vector>

namespace
{
std::string hexState(const std::uint32_t state[4])
{
	static const char *hex = "0123456789abcdef";
	std::string out(32, '0');
	for (int i = 0; i < 4; ++i)
	{
		for (int j = 0; j < 4; ++j)
		{
			const std::uint8_t byte = (std::uint8_t)(state[i] >> (8 * j));
			out[(size_t)(i * 8 + j * 2)] = hex[byte >> 4];
			out[(size_t)(i * 8 + j * 2 + 1)] = hex[byte & 15];
		}
	}
	return out;
}

std::string hashWith(void (*transform)(std::uint32_t[4], const std::uint8_t *), const std::uint8_t *data, size_t size)
{
	std::uint32_t state[4] = {0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u};
	std::uint64_t bits = (std::uint64_t)size * 8;
	size_t off = 0;
	while (size - off >= 64)
	{
		transform(state, data + off);
		off += 64;
	}
	std::uint8_t block[64];
	std::memset(block, 0, sizeof block);
	const size_t rem = size - off;
	if (rem)
	{
		std::memcpy(block, data + off, rem);
	}
	block[rem] = 0x80;
	if (rem >= 56)
	{
		transform(state, block);
		std::memset(block, 0, sizeof block);
	}
	for (int i = 0; i < 8; ++i)
	{
		block[56 + i] = (std::uint8_t)(bits >> (8 * i));
	}
	transform(state, block);
	return hexState(state);
}

double seconds(void (*transform)(std::uint32_t[4], const std::uint8_t *), const std::uint8_t *data, size_t size, int reps)
{
	timespec a{}, b{};
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &a);
	std::uint32_t state[4] = {0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u};
	for (int r = 0; r < reps; ++r)
	{
		state[0] = 0x67452301u;
		state[1] = 0xefcdab89u;
		state[2] = 0x98badcfeu;
		state[3] = 0x10325476u;
		for (size_t off = 0; off + 64 <= size; off += 64)
		{
			transform(state, data + off);
		}
	}
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &b);
	volatile std::uint32_t sink = state[0] ^ state[1] ^ state[2] ^ state[3];
	std::fprintf(stderr, "");
	(void)sink;
	return (double)(b.tv_sec - a.tv_sec) + (double)(b.tv_nsec - a.tv_nsec) * 1e-9;
}
}

int main()
{
	constexpr size_t N = 8u << 20;
	std::vector<std::uint8_t> buf(N);
	std::uint32_t x = 1;
	for (size_t i = 0; i < N; ++i)
	{
		x = x * 1664525u + 1013904223u;
		buf[i] = (std::uint8_t)(x >> 24);
	}
	const std::string ref = hashWith(md5k::transformReference, buf.data(), buf.size());
	const std::string fast = hashWith(md5k::transformFast, buf.data(), buf.size());
	if (ref != fast)
	{
		std::fprintf(stderr, "mismatch ref %s fast %s\n", ref.c_str(), fast.c_str());
		return 1;
	}
	// RFC "abc"
	const std::string abc = hashWith(md5k::transformFast, reinterpret_cast<const std::uint8_t *>("abc"), 3);
	if (abc != "900150983cd24fb0d6963f7d28e17f72")
	{
		std::fprintf(stderr, "rfc mismatch %s\n", abc.c_str());
		return 1;
	}
	const int reps = 8;
	const double tRef = seconds(md5k::transformReference, buf.data(), buf.size(), reps);
	const double tFast = seconds(md5k::transformFast, buf.data(), buf.size(), reps);
	const double bytes = (double)N * reps;
	std::printf("digest %s\n", fast.c_str());
	std::printf("reference_s %.6f fast_s %.6f speedup %.3f\n", tRef, tFast, tRef / tFast);
	std::printf("reference_MBps %.1f fast_MBps %.1f\n", bytes / tRef / 1e6, bytes / tFast / 1e6);
	return 0;
}
