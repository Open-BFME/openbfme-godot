// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Port of the RotWK REF_decode (RW 0xAA17E0; header per refdecode.h) with bounds checks; the command bodies
// are those of ZH Libraries/Source/Compression/EAC/refdecode.cpp REF_decode and of the retail routine.

#include "Libraries/Compression/EAC/refdecode.h"

#include <cstdio>

namespace
{

struct Header
{
	bool ok = false;
	size_t bodyOffset = 0;
	size_t decodedLength = 0;
};

// RotWK REF_decode header (RW 0xAA17E0, see refdecode.h): first byte flags, second byte 0xFB. Flag 0x80: the
// size field is 4 bytes wide instead of 3; flag 0x01: a size field of the same width precedes it and is skipped
// (the compressed size). REF_is (RW 0xAA1A00) accepts only 0x10fb, 0x11fb, 0x90fb and 0x91fb, and so does this.
Header parseHeader(const std::uint8_t *s, size_t len, std::string *error)
{
	Header h;
	if (len < 2)
	{
		if (error)
		{
			*error = "RefPack: stream shorter than the 2-byte type";
		}
		return h;
	}
	unsigned type = ((unsigned)s[0] << 8) + s[1];
	size_t p = 2;
	if (type != 0x10fb && type != 0x11fb && type != 0x90fb && type != 0x91fb)
	{
		if (error)
		{
			char buf[96];
			std::snprintf(buf, sizeof(buf), "RefPack: unsupported type 0x%04x (RotWK accepts 0x10fb 0x11fb 0x90fb 0x91fb)", type);
			*error = buf;
		}
		return h;
	}
	const size_t sizeBytes = (s[0] & 0x80) ? 4 : 3;
	if (s[0] & 0x01)
	{
		p += sizeBytes;
	}
	if (p + sizeBytes > len)
	{
		if (error)
		{
			*error = "RefPack: truncated header";
		}
		return h;
	}
	size_t ulen = 0;
	for (size_t i = 0; i < sizeBytes; ++i)
	{
		ulen = (ulen << 8) + s[p++];
	}
	h.ok = true;
	h.bodyOffset = p;
	h.decodedLength = ulen;
	return h;
}

} // namespace

long long REF_size(const std::uint8_t *compressed, size_t compressedLen)
{
	Header h = parseHeader(compressed, compressedLen, nullptr);
	return h.ok ? (long long)h.decodedLength : -1;
}

bool REF_decode(const std::uint8_t *compressed, size_t compressedLen, std::vector<std::uint8_t> &out,
	size_t *consumed, std::string *error, size_t maxOutput)
{
	Header h = parseHeader(compressed, compressedLen, error);
	if (!h.ok)
	{
		return false;
	}
	if (h.decodedLength > maxOutput)
	{
		if (error)
		{
			*error = "RefPack: header claims " + std::to_string(h.decodedLength) + " bytes, over the limit";
		}
		return false;
	}

	// The header length is untrusted and sizes the output allocation. The densest command is the 4-byte
	// "very int" form copying 1028 bytes (+3 literals), so no stream of N bytes can decode to more than
	// 258*N + 8 bytes; a larger claim is refused before anything is allocated.
	if (h.decodedLength / 258 > compressedLen)
	{
		if (error)
		{
			*error = "RefPack: header claims " + std::to_string(h.decodedLength) + " bytes from a " + std::to_string(compressedLen)
				+ "-byte stream (more than the format can expand to)";
		}
		return false;
	}

	const std::uint8_t *s = compressed + h.bodyOffset;
	const std::uint8_t *const sEnd = compressed + compressedLen;
	out.assign(h.decodedLength, 0);
	std::uint8_t *const dBegin = out.data();
	std::uint8_t *d = dBegin;
	std::uint8_t *const dEnd = dBegin + out.size();

#define REF_FAIL(msg)                                                                  \
	do                                                                                 \
	{                                                                                  \
		if (error)                                                                     \
		{                                                                              \
			*error = std::string("RefPack: ") + (msg) + " at source offset "           \
				+ std::to_string((size_t)(s - compressed));                            \
		}                                                                              \
		out.clear();                                                                   \
		return false;                                                                  \
	} while (0)
#define REF_NEED(n)                                                                    \
	do                                                                                 \
	{                                                                                  \
		if ((size_t)(sEnd - s) < (size_t)(n))                                          \
		{                                                                              \
			REF_FAIL("source truncated");                                              \
		}                                                                              \
	} while (0)

	for (;;)
	{
		REF_NEED(1);
		unsigned first = *s++;
		unsigned run;
		const std::uint8_t *ref;
		if (!(first & 0x80)) // short form
		{
			REF_NEED(1);
			unsigned second = *s++;
			run = first & 3;
			REF_NEED(run);
			if ((size_t)(dEnd - d) < run)
			{
				REF_FAIL("output overrun (literal)");
			}
			while (run--)
			{
				*d++ = *s++;
			}
			size_t dist = (((first & 0x60) << 3) + second) + 1;
			run = ((first & 0x1c) >> 2) + 3;
			if ((size_t)(d - dBegin) < dist)
			{
				REF_FAIL("back reference before start of output");
			}
			if ((size_t)(dEnd - d) < run)
			{
				REF_FAIL("output overrun (copy)");
			}
			ref = d - dist;
			while (run--)
			{
				*d++ = *ref++;
			}
			continue;
		}
		if (!(first & 0x40)) // int form
		{
			REF_NEED(2);
			unsigned second = *s++;
			unsigned third = *s++;
			run = second >> 6;
			REF_NEED(run);
			if ((size_t)(dEnd - d) < run)
			{
				REF_FAIL("output overrun (literal)");
			}
			while (run--)
			{
				*d++ = *s++;
			}
			size_t dist = (((second & 0x3f) << 8) + third) + 1;
			run = (first & 0x3f) + 4;
			if ((size_t)(d - dBegin) < dist)
			{
				REF_FAIL("back reference before start of output");
			}
			if ((size_t)(dEnd - d) < run)
			{
				REF_FAIL("output overrun (copy)");
			}
			ref = d - dist;
			while (run--)
			{
				*d++ = *ref++;
			}
			continue;
		}
		if (!(first & 0x20)) // very int form
		{
			REF_NEED(3);
			unsigned second = *s++;
			unsigned third = *s++;
			unsigned forth = *s++;
			run = first & 3;
			REF_NEED(run);
			if ((size_t)(dEnd - d) < run)
			{
				REF_FAIL("output overrun (literal)");
			}
			while (run--)
			{
				*d++ = *s++;
			}
			size_t dist = ((((first & 0x10) >> 4) << 16) + (second << 8) + third) + 1;
			run = (((first & 0x0c) >> 2) << 8) + forth + 5;
			if ((size_t)(d - dBegin) < dist)
			{
				REF_FAIL("back reference before start of output");
			}
			if ((size_t)(dEnd - d) < run)
			{
				REF_FAIL("output overrun (copy)");
			}
			ref = d - dist;
			while (run--)
			{
				*d++ = *ref++;
			}
			continue;
		}
		run = ((first & 0x1f) << 2) + 4; // literal
		if (run <= 112)
		{
			REF_NEED(run);
			if ((size_t)(dEnd - d) < run)
			{
				REF_FAIL("output overrun (literal block)");
			}
			while (run--)
			{
				*d++ = *s++;
			}
			continue;
		}
		run = first & 3; // eof (+0..3 literal)
		REF_NEED(run);
		if ((size_t)(dEnd - d) < run)
		{
			REF_FAIL("output overrun (final literal)");
		}
		while (run--)
		{
			*d++ = *s++;
		}
		break;
	}
#undef REF_NEED
#undef REF_FAIL

	if (d != dEnd)
	{
		if (error)
		{
			*error = "RefPack: stream ended after " + std::to_string((size_t)(d - dBegin)) + " bytes, header says "
				+ std::to_string(out.size());
		}
		out.clear();
		return false;
	}
	if (consumed)
	{
		*consumed = (size_t)(s - compressed);
	}
	return true;
}
