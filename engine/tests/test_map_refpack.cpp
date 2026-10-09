// OpenBFME unit tests: the RefPack ("EAR") decoder and envelope on hand-built streams. GPL-3.0.
//
// Every stream below is assembled by hand from the command table (spec maps-and-terrain.md 1.2,
// ZH refdecode.cpp / Open-BFME-1 BfmeRefPackDecode.cpp) and the expected output is written out
// literally; the decoder under test never produces its own expectation.

#include "doctest.h"

#include "Libraries/Compression/CompressionManager.h"
#include "Libraries/Compression/EAC/refdecode.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{

typedef std::vector<std::uint8_t> Bytes;

Bytes bytes(std::initializer_list<int> v)
{
	Bytes b;
	for (int x : v)
	{
		b.push_back((std::uint8_t)x);
	}
	return b;
}

Bytes cat(Bytes a, const Bytes &b)
{
	a.insert(a.end(), b.begin(), b.end());
	return a;
}

Bytes text(const std::string &s) { return Bytes(s.begin(), s.end()); }

// 10fb header with a 3-byte big-endian decoded size.
Bytes header10(size_t n) { return bytes({ 0x10, 0xFB, (int)(n >> 16) & 0xFF, (int)(n >> 8) & 0xFF, (int)n & 0xFF }); }

bool decode(const Bytes &src, Bytes &out, std::string *err = nullptr, size_t *consumed = nullptr)
{
	return REF_decode(src.data(), src.size(), out, consumed, err);
}

} // namespace

TEST_CASE("RefPack: literal block (0xE0 = 4 literal bytes) then EOF")
{
	// E0 -> run = ((0xE0 & 0x1F) << 2) + 4 = 4 literal bytes; FC -> EOF with 0 trailing literals.
	Bytes src = cat(cat(header10(4), bytes({ 0xE0 })), cat(text("ABCD"), bytes({ 0xFC })));
	Bytes out;
	size_t consumed = 0;
	REQUIRE(decode(src, out, nullptr, &consumed));
	CHECK(out == text("ABCD"));
	CHECK(consumed == src.size()); // the whole stream was read, nothing trailing
}

TEST_CASE("RefPack: EOF command carries 0..3 trailing literals")
{
	// 0xFF = EOF + 3 literals.
	Bytes src = cat(header10(3), cat(bytes({ 0xFF }), text("xyz")));
	Bytes out;
	REQUIRE(decode(src, out));
	CHECK(out == text("xyz"));
}

TEST_CASE("RefPack: short form (control < 0x80), literals then back reference")
{
	// 03 02: literal count 3, copy length ((0x03&0x1C)>>2)+3 = 3, distance ((0x03&0x60)<<3)+2+1 = 3.
	// literals ABC, then copy 3 bytes from 3 back -> ABCABC
	Bytes src = cat(cat(header10(6), bytes({ 0x03, 0x02 })), cat(text("ABC"), bytes({ 0xFC })));
	Bytes out;
	REQUIRE(decode(src, out));
	CHECK(out == text("ABCABC"));
}

TEST_CASE("RefPack: short form overlapping copy repeats the last byte (distance 1)")
{
	// 05 00: literal 1 (0x05&3), copy length ((0x05&0x1C)>>2)+3 = 4, distance 0+0+1 = 1 -> 'A' then AAAA
	Bytes src = cat(cat(header10(5), bytes({ 0x05, 0x00 })), cat(text("A"), bytes({ 0xFC })));
	Bytes out;
	REQUIRE(decode(src, out));
	CHECK(out == text("AAAAA"));
}

TEST_CASE("RefPack: short form distance high bits come from control bits 5-6")
{
	// Build 600 bytes of literals (112-byte blocks), then 0x60|0x00: distance = (0x60<<3) + b1 + 1.
	// (0x60 << 3) = 0x300 = 768 > 600, so use 0x20: (0x20<<3) = 256, b1=44 -> distance 301.
	Bytes lits;
	for (int i = 0; i < 600; ++i)
	{
		lits.push_back((std::uint8_t)(i * 7 + 3));
	}
	Bytes src = header10(600 + 3);
	size_t pos = 0;
	while (pos < lits.size())
	{
		size_t n = std::min<size_t>(112, lits.size() - pos);
		// literal block sizes are multiples of 4 (4..112); 600 = 5*112 + 40
		REQUIRE(n % 4 == 0);
		src.push_back((std::uint8_t)(0xE0 | ((n - 4) / 4)));
		src.insert(src.end(), lits.begin() + pos, lits.begin() + pos + n);
		pos += n;
	}
	// control 0x20: literals 0, length ((0x20&0x1C)>>2)+3 = 3, distance ((0x20&0x60)<<3)+44+1 = 301
	src = cat(src, bytes({ 0x20, 44, 0xFC }));
	Bytes out;
	REQUIRE(decode(src, out));
	REQUIRE(out.size() == 603);
	Bytes expected = lits;
	expected.push_back(lits[600 - 301]);
	expected.push_back(lits[600 - 301 + 1]);
	expected.push_back(lits[600 - 301 + 2]);
	CHECK(out == expected);
}

TEST_CASE("RefPack: int form (control 0x80-0xBF) with and without trailing literals")
{
	// 80 00 03: length (0x80&0x3F)+4 = 4, no literals (0x00>>6), distance ((0x00&0x3F)<<8)+3+1 = 4
	Bytes src = cat(cat(header10(8), bytes({ 0xE0 })), cat(text("ABCD"), bytes({ 0x80, 0x00, 0x03, 0xFC })));
	Bytes out;
	REQUIRE(decode(src, out));
	CHECK(out == text("ABCDABCD"));

	// 81 40 03 'X': length 5, one literal (0x40>>6 = 1) written AFTER the 3 command bytes, distance 4
	// stream: ABCD, literal X, then copy 5 from 4 back (positions 1..5 of "ABCDX" -> "BCDXB")... careful:
	// after the literal the output is ABCDX (5 bytes); distance 4 -> start at index 1: B C D X, then the
	// overlap continues with the bytes just written: B -> "BCDXB".
	Bytes src2 = cat(cat(header10(10), bytes({ 0xE0 })), cat(text("ABCD"), bytes({ 0x81, 0x40, 0x03, 'X', 0xFC })));
	Bytes out2;
	REQUIRE(decode(src2, out2));
	CHECK(out2 == text("ABCDXBCDXB"));
}

TEST_CASE("RefPack: very-int form (control 0xC0-0xDF), including the 17th distance bit")
{
	// C0 00 03 00: length ((0xC0&0x0C)>>2<<8)+0+5 = 5, no literals, distance (0<<16)+(0<<8)+3+1 = 4
	Bytes src = cat(cat(header10(9), bytes({ 0xE0 })), cat(text("ABCD"), bytes({ 0xC0, 0x00, 0x03, 0x00, 0xFC })));
	Bytes out;
	REQUIRE(decode(src, out));
	CHECK(out == text("ABCDABCDA"));

	// 70000 literal bytes (pseudo-random but fixed), then D4 01 CF 00: control 0xD4 sets bit 0x10
	// (distance + 65536) and bit 0x04 (length + 256): distance = 65536 + (1<<8) + 0xCF + 1 = 66000,
	// length = (0x04>>2<<8) + forth + 5 = 256 + 2 + 5 = 263.
	Bytes lits;
	std::uint32_t x = 12345;
	for (int i = 0; i < 70000; ++i)
	{
		x = x * 1103515245u + 12345u;
		lits.push_back((std::uint8_t)(x >> 16));
	}
	Bytes big = header10(70000 + 263);
	for (size_t pos = 0; pos < lits.size();)
	{
		size_t n = std::min<size_t>(112, lits.size() - pos);
		if (n % 4 != 0)
		{
			// last block: 70000 = 625*112 -> exact; guard anyway
			FAIL("literal blocks must be a multiple of 4");
		}
		big.push_back((std::uint8_t)(0xE0 | ((n - 4) / 4)));
		big.insert(big.end(), lits.begin() + pos, lits.begin() + pos + n);
		pos += n;
	}
	big = cat(big, bytes({ 0xD4, 0x01, 0xCF, 0x02, 0xFC }));
	Bytes outBig;
	std::string err;
	REQUIRE_MESSAGE(decode(big, outBig, &err), err);
	REQUIRE(outBig.size() == 70263);
	for (size_t i = 0; i < 263; ++i)
	{
		CHECK(outBig[70000 + i] == lits[70000 - 66000 + i]); // all 263 within the literals (66000 + 263 < 70000)
	}
}

TEST_CASE("RefPack: header variants and rejections")
{
	// RotWK REF_decode (RW 0xAA17E0) / REF_is (RW 0xAA1A00): the types are 0x10fb, 0x11fb, 0x90fb, 0x91fb; flag 0x80 =
	// 4-byte size field, flag 0x01 = a size field of that width to skip first (tools/retail_oracle runs the retail
	// function on the same streams).
	// 0x11fb skips a 3-byte compressed-size field.
	Bytes src11 = cat(bytes({ 0x11, 0xFB, 0x00, 0x00, 0x09, 0x00, 0x00, 0x04, 0xE0 }), cat(text("ABCD"), bytes({ 0xFC })));
	Bytes out;
	REQUIRE(decode(src11, out));
	CHECK(out == text("ABCD"));
	CHECK(REF_size(src11.data(), src11.size()) == 4);

	// 0x90fb carries a 4-byte decoded size, 0x91fb skips a 4-byte compressed size first.
	Bytes src90 = cat(bytes({ 0x90, 0xFB, 0x00, 0x00, 0x00, 0x04, 0xE0 }), cat(text("ABCD"), bytes({ 0xFC })));
	REQUIRE(decode(src90, out));
	CHECK(out == text("ABCD"));
	CHECK(REF_size(src90.data(), src90.size()) == 4);
	Bytes src91 = cat(bytes({ 0x91, 0xFB, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x04, 0xE0 }), cat(text("ABCD"), bytes({ 0xFC })));
	REQUIRE(decode(src91, out));
	CHECK(out == text("ABCD"));

	// BFME1's 0x15fb / 0x16fb are NOT RotWK types (REF_is refuses them): refuse rather than guess
	std::string err;
	Bytes src15 = cat(bytes({ 0x15, 0xFB, 0x00, 0x00, 0x00, 0x04, 0xE0 }), cat(text("ABCD"), bytes({ 0xFC })));
	CHECK_FALSE(decode(src15, out, &err));
	CHECK(err.find("unsupported type 0x15fb") != std::string::npos);
	CHECK(REF_size(src15.data(), src15.size()) == -1);
	Bytes bad = cat(bytes({ 0x10, 0xFA, 0x00, 0x00, 0x04, 0xE0 }), cat(text("ABCD"), bytes({ 0xFC })));
	CHECK_FALSE(decode(bad, out, &err)); // the marker byte must be 0xFB
}

TEST_CASE("RefPack: malformed streams are errors, not out-of-range access")
{
	Bytes out;
	std::string err;

	// header claims 5 bytes but the stream ends after 4
	CHECK_FALSE(decode(cat(cat(header10(5), bytes({ 0xE0 })), cat(text("ABCD"), bytes({ 0xFC }))), out, &err));
	CHECK(err.find("stream ended after 4 bytes, header says 5") != std::string::npos);

	// header claims 3 bytes but 4 are produced
	CHECK_FALSE(decode(cat(cat(header10(3), bytes({ 0xE0 })), cat(text("ABCD"), bytes({ 0xFC }))), out, &err));
	CHECK(err.find("output overrun") != std::string::npos);

	// back reference before the start of the output
	CHECK_FALSE(decode(cat(header10(5), bytes({ 0x00, 0x05, 0xFC })), out, &err));
	CHECK(err.find("back reference before start") != std::string::npos);

	// source truncated mid-command
	CHECK_FALSE(decode(cat(header10(4), bytes({ 0xE0, 'A', 'B' })), out, &err));
	CHECK(err.find("source truncated") != std::string::npos);

	// no EOF command at all
	CHECK_FALSE(decode(cat(header10(4), bytes({ 0xE0, 'A', 'B', 'C', 'D' })), out, &err));
	CHECK(err.find("source truncated") != std::string::npos);

	// decompression-bomb guard
	Bytes bomb = bytes({ 0x10, 0xFB, 0xFF, 0xFF, 0xFF, 0xFC });
	CHECK_FALSE(REF_decode(bomb.data(), bomb.size(), out, nullptr, &err, 1000));
	CHECK(err.find("over the limit") != std::string::npos);

	// the header claim is untrusted: 16 MiB from a 6-byte stream is more than the densest command
	// (4 bytes -> 1028) can produce, so it is refused before the output is allocated
	CHECK_FALSE(decode(bomb, out, &err));
	CHECK(err.find("more than the format can expand to") != std::string::npos);
	// the same claim from a stream that is dense enough is only caught by the content check
	Bytes dense(70000, 0xC0);
	dense = cat(header10(0xFFFFFF), dense);
	CHECK_FALSE(decode(dense, out, &err));
	CHECK(err.find("RefPack:") != std::string::npos);

	// too short for a header
	CHECK_FALSE(decode(bytes({ 0x10 }), out, &err));
	CHECK(err.find("shorter than the 2-byte type") != std::string::npos);
}

TEST_CASE("EAR envelope: tag, declared size, and mismatch handling")
{
	Bytes stream = cat(cat(header10(4), bytes({ 0xE0 })), cat(text("ABCD"), bytes({ 0xFC })));
	Bytes ear = cat(text("EAR"), bytes({ 0 }));
	ear = cat(ear, bytes({ 4, 0, 0, 0 })); // i32 LE uncompressed size
	ear = cat(ear, stream);

	CHECK(CompressionManager::getCompressionType(ear.data(), ear.size()) == COMPRESSION_REFPACK);
	CHECK(CompressionManager::isDataCompressed(ear.data(), ear.size()));
	CHECK(CompressionManager::getUncompressedSize(ear.data(), ear.size()) == 4);
	Bytes out;
	std::string err;
	REQUIRE(CompressionManager::decompressData(ear.data(), ear.size(), out, &err));
	CHECK(out == text("ABCD"));

	// the envelope's size disagrees with the stream: ZH keeps the raw bytes, we report it
	Bytes wrong = ear;
	wrong[4] = 9;
	CHECK_FALSE(CompressionManager::decompressData(wrong.data(), wrong.size(), out, &err));
	CHECK(err.find("declares 9 bytes") != std::string::npos);

	// bytes after the terminator are not a stream any retail tool wrote: an error, not a silent truncation
	Bytes trailing = cat(ear, text("JUNK"));
	CHECK_FALSE(CompressionManager::decompressData(trailing.data(), trailing.size(), out, &err));
	CHECK(err.find("4 trailing byte(s) after the terminator") != std::string::npos);
	CHECK(out.empty());

	// other tags are recognised but the codecs are not ported: an error, never a passthrough
	Bytes zl = cat(cat(text("ZL3"), bytes({ 0 })), bytes({ 4, 0, 0, 0, 1, 2, 3, 4 }));
	CHECK(CompressionManager::getCompressionType(zl.data(), zl.size()) == COMPRESSION_ZLIB3);
	CHECK_FALSE(CompressionManager::decompressData(zl.data(), zl.size(), out, &err));
	CHECK(err.find("not ported") != std::string::npos);

	// "CkMp" data is not an envelope; sizes shorter than 8 bytes never are
	Bytes raw = bytes({ 'C', 'k', 'M', 'p', 1, 0, 0, 0 });
	CHECK_FALSE(CompressionManager::isDataCompressed(raw.data(), raw.size()));
	CHECK(CompressionManager::getUncompressedSize(raw.data(), raw.size()) == (std::int64_t)raw.size());
	Bytes tiny = text("EAR");
	CHECK_FALSE(CompressionManager::isDataCompressed(tiny.data(), tiny.size()));
}
