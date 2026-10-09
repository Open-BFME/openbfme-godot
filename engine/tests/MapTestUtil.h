// OpenBFME unit tests. GPL-3.0.
// Builds map chunk files byte by byte (spec maps-and-terrain.md 1.3-1.5) so parser tests never need
// retail files. Expected values in the tests are written out literally.

#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace maptest
{

typedef std::vector<std::uint8_t> Bytes;

// Little-endian byte writer.
struct W
{
	Bytes b;
	W &u8(int v)
	{
		b.push_back((std::uint8_t)v);
		return *this;
	}
	W &u16(int v)
	{
		u8(v & 0xFF);
		return u8((v >> 8) & 0xFF);
	}
	W &i32(std::int32_t v) { return u32((std::uint32_t)v); }
	W &u32(std::uint32_t v)
	{
		for (int i = 0; i < 4; ++i)
		{
			b.push_back((std::uint8_t)(v >> (8 * i)));
		}
		return *this;
	}
	W &f32(float f)
	{
		std::uint32_t v;
		std::memcpy(&v, &f, 4);
		return u32(v);
	}
	W &astr(const std::string &s)
	{
		u16((int)s.size());
		b.insert(b.end(), s.begin(), s.end());
		return *this;
	}
	W &ustr(const std::u16string &s)
	{
		u16((int)s.size());
		for (char16_t c : s)
		{
			u16((int)c);
		}
		return *this;
	}
	W &raw(const Bytes &v)
	{
		b.insert(b.end(), v.begin(), v.end());
		return *this;
	}
	W &zeros(size_t n)
	{
		b.insert(b.end(), n, 0);
		return *this;
	}
};

// The chunk table of contents: ids are allocated from 1 in first-use order.
struct Toc
{
	std::vector<std::string> names;
	std::uint32_t id(const std::string &name)
	{
		for (size_t i = 0; i < names.size(); ++i)
		{
			if (names[i] == name)
			{
				return (std::uint32_t)(i + 1);
			}
		}
		names.push_back(name);
		return (std::uint32_t)names.size();
	}
	// NameKey / Dict key word: id<<8 | type (3 = ascii for a NameKey)
	std::int32_t key(const std::string &name, int type) { return (std::int32_t)((id(name) << 8) | (std::uint32_t)type); }
};

// Dict value types (ZH Dict::DataType).
struct D
{
	Toc &toc;
	W w;
	int count = 0;
	explicit D(Toc &t) : toc(t) {}
	D &b(const std::string &k, bool v)
	{
		w.i32(toc.key(k, 0)).u8(v ? 1 : 0);
		++count;
		return *this;
	}
	D &i(const std::string &k, std::int32_t v)
	{
		w.i32(toc.key(k, 1)).i32(v);
		++count;
		return *this;
	}
	D &r(const std::string &k, float v)
	{
		w.i32(toc.key(k, 2)).f32(v);
		++count;
		return *this;
	}
	D &s(const std::string &k, const std::string &v)
	{
		w.i32(toc.key(k, 3)).astr(v);
		++count;
		return *this;
	}
	D &u(const std::string &k, const std::u16string &v)
	{
		w.i32(toc.key(k, 4)).ustr(v);
		++count;
		return *this;
	}
	D &bad(const std::string &k, int type) // an unknown type code, no value
	{
		w.i32(toc.key(k, type));
		++count;
		return *this;
	}
	Bytes bytes() const
	{
		W o;
		o.u16(count);
		o.raw(w.b);
		return o.b;
	}
};

// id u32, version u16, size i32, payload.
inline Bytes chunk(Toc &toc, const std::string &name, int version, const Bytes &payload)
{
	W w;
	w.u32(toc.id(name)).u16(version).i32((std::int32_t)payload.size()).raw(payload);
	return w.b;
}

inline Bytes cat(Bytes a, const Bytes &b)
{
	a.insert(a.end(), b.begin(), b.end());
	return a;
}

// "CkMp" + count + {u8 len, name, u32 id} newest first, then the chunks. The ids are fixed when the
// TOC is written, so call this after every chunk() / D has run.
inline Bytes file(Toc &toc, const Bytes &chunks)
{
	W w;
	w.b = { 'C', 'k', 'M', 'p' };
	w.i32((std::int32_t)toc.names.size());
	for (size_t i = toc.names.size(); i-- > 0;)
	{
		w.u8((int)toc.names[i].size());
		w.b.insert(w.b.end(), toc.names[i].begin(), toc.names[i].end());
		w.u32((std::uint32_t)(i + 1));
	}
	w.raw(chunks);
	return w.b;
}

// Wraps a chunk file in the "EAR" envelope using only literal blocks and the EOF command
// (a valid, if useless, RefPack stream) so the loader's envelope path is exercised end to end.
inline Bytes earWrap(const Bytes &data)
{
	W w;
	w.b = { 'E', 'A', 'R', 0 };
	w.i32((std::int32_t)data.size());
	// 10fb header, 3-byte big-endian size
	w.u8(0x10).u8(0xFB).u8((int)(data.size() >> 16) & 0xFF).u8((int)(data.size() >> 8) & 0xFF).u8((int)data.size() & 0xFF);
	size_t pos = 0;
	while (data.size() - pos >= 4)
	{
		size_t n = data.size() - pos;
		if (n > 112)
		{
			n = 112;
		}
		n -= n % 4;
		w.u8(0xE0 | (int)((n - 4) / 4));
		w.b.insert(w.b.end(), data.begin() + (long)pos, data.begin() + (long)(pos + n));
		pos += n;
	}
	size_t tail = data.size() - pos; // 0..3
	w.u8(0xFC | (int)tail);
	w.b.insert(w.b.end(), data.begin() + (long)pos, data.end());
	return w.b;
}

} // namespace maptest
