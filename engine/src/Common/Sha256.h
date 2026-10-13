// OpenBFME. GPL-3.0.
//
// SHA-256 (FIPS 180-4) and HMAC-SHA256 (RFC 2104), lane ONLINE-2: the online relay's session tickets (GameNetwork/RelayProtocol.h) are HMAC-SHA256
// signed by the lobby service and checked by the relay. Written from the standards; the tests check the FIPS 180-4 examples and the RFC 4231 vectors.
// Not simulation code.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class Sha256
{
public:
	Sha256();
	void update(const void *data, size_t size);
	std::array<std::uint8_t, 32> finish();
	static std::array<std::uint8_t, 32> hash(const void *data, size_t size);

private:
	void block(const std::uint8_t *p);
	std::uint32_t m_h[8];
	std::uint8_t m_buffer[64];
	size_t m_used = 0;
	std::uint64_t m_bytes = 0;
};

std::array<std::uint8_t, 32> HmacSha256(const std::uint8_t *key, size_t keySize, const std::uint8_t *data, size_t size);
// constant-time comparison of n bytes (a MAC check must not leak where the first difference is)
bool ConstantTimeEqual(const std::uint8_t *a, const std::uint8_t *b, size_t n);
// lower-case hex
std::string HexBytes(const std::uint8_t *data, size_t size);
