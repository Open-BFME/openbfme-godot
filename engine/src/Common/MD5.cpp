// OpenBFME. GPL-3.0. Straightforward implementation of RFC 1321.

#include "Common/MD5.h"
#include "Common/MD5Kernel.h"
#include "Common/PathIO.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

MD5::MD5() : m_bitCount(0), m_bufferUsed(0)
{
	m_state[0] = 0x67452301;
	m_state[1] = 0xefcdab89;
	m_state[2] = 0x98badcfe;
	m_state[3] = 0x10325476;
}

void MD5::transform(const std::uint8_t block[64])
{
	// Inlines into update(). The scalar loop remains MD5::ofBytesReference.
#ifdef OPENBFME_MD5_REFERENCE
	md5k::transformReference(m_state, block);
#else
	md5k::transformFast(m_state, block);
#endif
}

void MD5::update(const void *data, size_t size)
{
	if (size == 0)
	{
		return; // an empty update (data may be null) changes nothing; memcpy must not see a null pointer
	}
	const std::uint8_t *p = static_cast<const std::uint8_t *>(data);
	m_bitCount += (std::uint64_t)size * 8;
	if (m_bufferUsed > 0)
	{
		size_t take = 64 - m_bufferUsed;
		if (take > size)
		{
			take = size;
		}
		std::memcpy(m_buffer + m_bufferUsed, p, take);
		m_bufferUsed += take;
		p += take;
		size -= take;
		if (m_bufferUsed == 64)
		{
			transform(m_buffer);
			m_bufferUsed = 0;
		}
	}
	while (size >= 64)
	{
		transform(p);
		p += 64;
		size -= 64;
	}
	if (size > 0)
	{
		std::memcpy(m_buffer, p, size);
		m_bufferUsed = size;
	}
}

std::string MD5::finalHex()
{
	std::uint64_t bits = m_bitCount;
	std::uint8_t pad = 0x80;
	update(&pad, 1);
	std::uint8_t zero = 0;
	while (m_bufferUsed != 56)
	{
		update(&zero, 1);
	}
	std::uint8_t len[8];
	for (int i = 0; i < 8; ++i)
	{
		len[i] = (std::uint8_t)(bits >> (8 * i));
	}
	update(len, 8);
	static const char *hex = "0123456789abcdef";
	std::string out;
	for (int i = 0; i < 4; ++i)
	{
		for (int j = 0; j < 4; ++j)
		{
			std::uint8_t byte = (std::uint8_t)(m_state[i] >> (8 * j));
			out.push_back(hex[byte >> 4]);
			out.push_back(hex[byte & 15]);
		}
	}
	return out;
}

std::string MD5::ofBytes(const void *data, size_t size)
{
	MD5 md5;
	md5.update(data, size);
	return md5.finalHex();
}

std::string MD5::ofBytesReference(const void *data, size_t size)
{
	// Independent state machine so a test can hash with the scalar loop
	// without a second MD5 object layout. Padding matches MD5::update / finalHex.
	std::uint32_t state[4] = {0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u};
	std::uint64_t bitCount = 0;
	std::uint8_t buffer[64];
	size_t bufferUsed = 0;
	auto feed = [&](const std::uint8_t *p, size_t n) {
		bitCount += (std::uint64_t)n * 8;
		while (n > 0)
		{
			size_t take = 64 - bufferUsed;
			if (take > n)
			{
				take = n;
			}
			std::memcpy(buffer + bufferUsed, p, take);
			bufferUsed += take;
			p += take;
			n -= take;
			if (bufferUsed == 64)
			{
				md5k::transformReference(state, buffer);
				bufferUsed = 0;
			}
		}
	};
	feed(static_cast<const std::uint8_t *>(data), size);
	const std::uint64_t bits = bitCount;
	const std::uint8_t pad = 0x80;
	feed(&pad, 1);
	const std::uint8_t zero = 0;
	while (bufferUsed != 56)
	{
		feed(&zero, 1);
	}
	std::uint8_t len[8];
	for (int i = 0; i < 8; ++i)
	{
		len[i] = (std::uint8_t)(bits >> (8 * i));
	}
	feed(len, 8);
	static const char *hex = "0123456789abcdef";
	std::string out;
	out.resize(32);
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

std::string MD5::ofFile(const std::string &path, std::string *error)
{
	std::ifstream in(PathIO::native(path), std::ios::binary);
	if (!in.is_open())
	{
		if (error)
		{
			*error = "could not open " + path + " for hashing";
		}
		return std::string();
	}
	MD5 md5;
	std::vector<char> buffer(1 << 20);
	while (in)
	{
		in.read(buffer.data(), (std::streamsize)buffer.size());
		std::streamsize got = in.gcount();
		if (got > 0)
		{
			md5.update(buffer.data(), (size_t)got);
		}
	}
	if (!in.eof())
	{
		if (error)
		{
			*error = "read error while hashing " + path;
		}
		return std::string();
	}
	return md5.finalHex();
}
