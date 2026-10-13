// OpenBFME unit tests. GPL-3.0. Lane DEP-2: the Linux GDExtension's own powf and getentropy (src/GodotDevice/LinuxRuntimeShims.cpp; the extension
// is linked with --wrap so its references reach these instead of glibc's 2.27 / 2.25 functions). Linux only.

#include "doctest.h"

#include "Common/DeterministicLibm.h"

#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

extern "C" {
float __wrap_powf(float x, float y);
int __wrap_getentropy(void *buffer, std::size_t length);
}

TEST_CASE("dep2 shims: the extension's powf is the deterministic pow rounded to float")
{
	CHECK(__wrap_powf(2.0f, 10.0f) == 1024.0f);
	CHECK(__wrap_powf(0.5f, 3.0f) == 0.125f);
	CHECK(__wrap_powf(9.0f, 0.5f) == 3.0f);
	CHECK(__wrap_powf(10.0f, -1.0f) == 0.1f); // 0.1 rounded once to float (0x3DCCCCCD)
	const float x = 1.7f, y = 2.3f;
	CHECK(__wrap_powf(x, y) == (float)DetLibm::pow((double)x, (double)y));
	CHECK(std::isnan(__wrap_powf(-8.0f, 1.0f / 3.0f)));
}

TEST_CASE("dep2 shims: the extension's getentropy fills the buffer from getrandom, refuses more than 256 bytes with EIO, like glibc's")
{
	unsigned char a[256], b[256];
	std::memset(a, 0, sizeof a);
	std::memset(b, 0, sizeof b);
	REQUIRE(__wrap_getentropy(a, sizeof a) == 0);
	REQUIRE(__wrap_getentropy(b, sizeof b) == 0);
	// two 256-byte draws from the kernel's generator: equal only with probability 2^-2048
	CHECK(std::memcmp(a, b, sizeof a) != 0);
	CHECK(__wrap_getentropy(a, 0) == 0);
	unsigned char big[257];
	errno = 0;
	CHECK(__wrap_getentropy(big, sizeof big) == -1);
	CHECK(errno == EIO);
}
