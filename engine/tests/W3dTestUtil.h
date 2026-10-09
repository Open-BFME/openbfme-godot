// OpenBFME unit tests. GPL-3.0.
// Builds W3D chunk streams byte by byte so tests never need retail files.

#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace w3dtest
{

struct ChunkWriter
{
	std::vector<std::uint8_t> bytes;

	template <typename T>
	void pod(const T &v)
	{
		const std::uint8_t *p = reinterpret_cast<const std::uint8_t *>(&v);
		bytes.insert(bytes.end(), p, p + sizeof(T));
	}

	void u8(std::uint8_t v) { bytes.push_back(v); }
	void u16(std::uint16_t v) { pod(v); }
	void u32(std::uint32_t v) { pod(v); }
	void f32(float v) { pod(v); }

	// Data chunk.
	void chunk(std::uint32_t id, const std::vector<std::uint8_t> &body)
	{
		u32(id);
		u32((std::uint32_t)body.size());
		bytes.insert(bytes.end(), body.begin(), body.end());
	}

	// Wrapper chunk (MSB of size set).
	void wrapper(std::uint32_t id, const ChunkWriter &children)
	{
		u32(id);
		u32((std::uint32_t)children.bytes.size() | 0x80000000u);
		bytes.insert(bytes.end(), children.bytes.begin(), children.bytes.end());
	}

	template <typename T>
	static std::vector<std::uint8_t> of(const T &v)
	{
		const std::uint8_t *p = reinterpret_cast<const std::uint8_t *>(&v);
		return std::vector<std::uint8_t>(p, p + sizeof(T));
	}

	template <typename T>
	static std::vector<std::uint8_t> ofArray(const std::vector<T> &v)
	{
		const std::uint8_t *p = reinterpret_cast<const std::uint8_t *>(v.data());
		return std::vector<std::uint8_t>(p, p + v.size() * sizeof(T));
	}
};

inline void setName(char *dst, size_t len, const char *src)
{
	std::memset(dst, 0, len);
	std::strncpy(dst, src, len - 1);
}

// Byte-vector helpers for hand-assembled chunk bodies.
struct Bytes
{
	std::vector<std::uint8_t> v;
	Bytes &u8(std::uint8_t x) { v.push_back(x); return *this; }
	Bytes &u16(std::uint16_t x) { v.push_back((std::uint8_t)x); v.push_back((std::uint8_t)(x >> 8)); return *this; }
	Bytes &u32(std::uint32_t x) { u16((std::uint16_t)x); u16((std::uint16_t)(x >> 16)); return *this; }
	Bytes &f32(float x) { std::uint32_t b; std::memcpy(&b, &x, 4); return u32(b); }
	Bytes &str(const char *s, size_t len) // fixed-width, NUL padded
	{
		for (size_t i = 0; i < len; ++i) v.push_back(i < std::strlen(s) ? (std::uint8_t)s[i] : 0);
		return *this;
	}
};

} // namespace w3dtest
