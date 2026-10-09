// OpenBFME. GPL-3.0.
//
// RFC 1321 MD5, used only to verify retail archives against the pinned policy hashes.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

class MD5
{
public:
	MD5();
	void update(const void *data, size_t size);
	std::string finalHex(); // lower-case hex; the object is spent afterwards

	static std::string ofBytes(const void *data, size_t size);
	// Hashes a whole file. Empty string + *error on I/O failure.
	static std::string ofFile(const std::string &path, std::string *error);

private:
	void transform(const std::uint8_t block[64]);

	std::uint32_t m_state[4];
	std::uint64_t m_bitCount;
	std::uint8_t m_buffer[64];
	size_t m_bufferUsed;
};
