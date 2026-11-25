// OpenBFME. GPL-3.0.
// GameClient/DdsDecode.h: the DDS files Godot's loader refuses (a cut or partial mip chain, sizes that are not a multiple of 4,
// or non-zero dwPitchOrLinearSize without DDSD_LINEARSIZE / DDSD_PITCH) read from their top level.

#include "doctest.h"

#include "GameClient/DdsDecode.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace
{

using Bytes = std::vector<std::uint8_t>;

void put32(Bytes &b, size_t at, std::uint32_t v)
{
	for (int i = 0; i < 4; ++i)
	{
		b[at + i] = (std::uint8_t)(v >> (8 * i));
	}
}

// a DDS header of `w` x `h` with `mips` levels declared, then `payload`
Bytes makeDds(const char *fourCC, int w, int h, int mips, const Bytes &payload)
{
	Bytes b(128, 0);
	std::memcpy(b.data(), "DDS ", 4);
	put32(b, 4, 124);
	put32(b, 8, 0x1007 | 0x20000);
	put32(b, 12, (std::uint32_t)h);
	put32(b, 16, (std::uint32_t)w);
	put32(b, 28, (std::uint32_t)mips);
	put32(b, 76, 32);
	put32(b, 80, 4);
	std::memcpy(b.data() + 84, fourCC, 4);
	b.insert(b.end(), payload.begin(), payload.end());
	return b;
}

} // namespace

TEST_CASE("DdsDecode: a DXT5 block with a gradient alpha, and a size that is not a multiple of 4, is read from its top level")
{
	// one block: both colour endpoints pure red (0xF800), indices 0; alpha endpoints 255 / 0 (8 values), indices 0 -> all alpha 255
	Bytes block = { 0xFF, 0x00, 0, 0, 0, 0, 0, 0, 0x00, 0xF8, 0x00, 0xF8, 0, 0, 0, 0 };
	Bytes dds = makeDds("DXT5", 3, 3, 1, block);
	bool fallback = false;
	std::string err;
	REQUIRE_MESSAGE(DdsDecode::inspect(dds.data(), dds.size(), fallback, &err), err);
	CHECK(fallback); // 3 x 3 is no multiple of 4
	DdsImage img;
	REQUIRE_MESSAGE(DdsDecode::decodeLevel0(dds.data(), dds.size(), img, &err), err);
	CHECK(img.width == 3);
	CHECK(img.height == 3);
	REQUIRE(img.rgba.size() == 36);
	CHECK(img.rgba[0] == 255); CHECK(img.rgba[1] == 0); CHECK(img.rgba[2] == 0); CHECK(img.rgba[3] == 255);
	CHECK(img.rgba[32] == 255); CHECK(img.rgba[35] == 255); // pixel (2,2)
}

TEST_CASE("DdsDecode: a mip chain shorter than the header says needs the fallback, a whole one does not")
{
	// 8 x 8 DXT1 declared with 4 levels (4 + 1 + 1 + 1 blocks = 7 x 8 bytes); two blocks of data only
	Bytes two(16, 0);
	Bytes cut = makeDds("DXT1", 8, 8, 4, two);
	bool fallback = false;
	std::string err;
	REQUIRE(DdsDecode::inspect(cut.data(), cut.size(), fallback, &err));
	CHECK(fallback);
	Bytes whole = makeDds("DXT1", 8, 8, 4, Bytes(56, 0));
	REQUIRE(DdsDecode::inspect(whole.data(), whole.size(), fallback, &err));
	CHECK_FALSE(fallback);
	// a chain that is whole for its header but stops above 1 x 1 (8 x 8 declared with 2 levels, both present): Godot would expect 4 levels
	Bytes partial = makeDds("DXT1", 8, 8, 2, Bytes(40, 0));
	REQUIRE(DdsDecode::inspect(partial.data(), partial.size(), fallback, &err));
	CHECK(fallback);
	// the whole chain of 8 x 8 is 4 levels; a single level (no mipmaps) is Godot's to read
	Bytes single = makeDds("DXT1", 8, 8, 1, Bytes(32, 0));
	REQUIRE(DdsDecode::inspect(single.data(), single.size(), fallback, &err));
	CHECK_FALSE(fallback);
	// the top level alone (4 blocks) is enough to decode
	Bytes top = makeDds("DXT1", 8, 8, 4, Bytes(32, 0));
	DdsImage img;
	CHECK(DdsDecode::decodeLevel0(top.data(), top.size(), img, &err));
	CHECK(img.rgba.size() == 8 * 8 * 4);
	Bytes under = makeDds("DXT1", 8, 8, 4, Bytes(24, 0));
	CHECK_FALSE(DdsDecode::decodeLevel0(under.data(), under.size(), img, &err));
	CHECK(err.find("cut") != std::string::npos);
}

TEST_CASE("DdsDecode: non-zero pitchOrLinearSize without matching header flag triggers fallback")
{
	// Compressed DXT1 with non-zero dwPitchOrLinearSize (offset 20) and DDSD_LINEARSIZE (0x80000) unset
	Bytes noFlag = makeDds("DXT1", 4, 4, 1, Bytes(8, 0));
	put32(noFlag, 20, 8);
	bool fallback = false;
	std::string err;
	REQUIRE_MESSAGE(DdsDecode::inspect(noFlag.data(), noFlag.size(), fallback, &err), err);
	CHECK(fallback);

	// With DDSD_LINEARSIZE set, Godot's loader handles it natively
	Bytes withFlag = noFlag;
	put32(withFlag, 8, 0x1007 | 0x20000 | 0x80000);
	REQUIRE_MESSAGE(DdsDecode::inspect(withFlag.data(), withFlag.size(), fallback, &err), err);
	CHECK_FALSE(fallback);
}

TEST_CASE("DdsDecode: DXT1 punch-through alpha, DX10 and other formats")
{
	// c0 <= c1 selects the 3-colour mode: index 3 is transparent black
	Bytes block = { 0x00, 0x00, 0x00, 0xF8, 0xFF, 0xFF, 0xFF, 0xFF };
	Bytes dds = makeDds("DXT1", 4, 4, 1, block);
	DdsImage img;
	std::string err;
	REQUIRE_MESSAGE(DdsDecode::decodeLevel0(dds.data(), dds.size(), img, &err), err);
	CHECK(img.rgba[3] == 0);
	Bytes dx10 = makeDds("DX10", 4, 4, 1, Bytes(32, 0));
	bool fallback = false;
	CHECK_FALSE(DdsDecode::inspect(dx10.data(), dx10.size(), fallback, &err));
	CHECK(err.find("DX10") != std::string::npos);
	Bytes junk(200, 0);
	CHECK_FALSE(DdsDecode::inspect(junk.data(), junk.size(), fallback, &err));
}
