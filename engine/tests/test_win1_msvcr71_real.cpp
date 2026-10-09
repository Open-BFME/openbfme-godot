// OpenBFME unit tests (lane WIN-1 r2). GPL-3.0.
//
// Retail reads every INI real with MSVCR71's sscanf(token, "%f") (INI::scanReal RW 0x42EAAD, the RwGrammar probe RW 0x73B723). Msvcr71Real ports that
// converter instruction by instruction (Common/INI/Msvcr71Real.h). Expected values: the real msvcr71.dll of the RotWK install, executed under Wine with
// the native DLL forced and its code bytes verified (tools/retail_oracle/msvcr71_real_oracle.c); the golden file holds 12,000 synthetic texts with
// their retail results (the full measurement: 281,499 texts, 0 differences). The power-of-ten tables are generated (tools/sim/gen_msvcr71_pow10.py)
// and compared here with the bytes of the installed DLL when ROTWK_INSTALL is set.

#include "doctest.h"

#include "Common/INI.h"
#include "Common/INI/Msvcr71Real.h"
#include "IniTestUtil.h"
#include "RetailTestMount.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace
{
std::uint32_t bitsOf(float f)
{
	std::uint32_t u;
	std::memcpy(&u, &f, sizeof u);
	return u;
}

// sscanf "%f" through the port: the bits, or 0xC640E400 (the oracle's sentinel, -12345.0f) when nothing was assigned
std::uint32_t retailBits(const char *text)
{
	float f = -12345.0f;
	Msvcr71Real::scanfFloat(text, f);
	return bitsOf(f);
}

std::uint32_t le32(const std::vector<unsigned char> &b, size_t at)
{
	return (std::uint32_t)b[at] | ((std::uint32_t)b[at + 1] << 8) | ((std::uint32_t)b[at + 2] << 16) | ((std::uint32_t)b[at + 3] << 24);
}
} // namespace

TEST_CASE("win1 msvcr71 real: the port of MSVCR71's sscanf(\"%f\") reproduces the retail DLL's result on every golden text (12,000 synthetic texts)")
{
	std::ifstream in(retailtest::dataDir() + "/win1/msvcr71_scanf_golden.txt", std::ios::binary);
	REQUIRE(in.good());
	std::string line;
	int compared = 0, bad = 0;
	while (std::getline(in, line))
	{
		if (line.empty() || line[0] == '#')
		{
			continue;
		}
		const size_t tab = line.rfind('\t');
		REQUIRE(tab != std::string::npos);
		const std::string text = line.substr(0, tab);
		const std::string result = line.substr(tab + 1);
		const bool want = result[0] == '1';
		float f = -12345.0f;
		const bool got = Msvcr71Real::scanfFloat(text.c_str(), f);
		bool same = got == want;
		if (same && want)
		{
			same = bitsOf(f) == (std::uint32_t)std::strtoul(result.c_str() + 2, nullptr, 16);
		}
		if (!same && ++bad <= 10)
		{
			MESSAGE("differs: [" << text << "] retail " << result << " port " << got << " " << std::hex << bitsOf(f));
		}
		++compared;
	}
	CHECK(compared == 12000);
	CHECK(bad == 0);
}

TEST_CASE("win1 msvcr71 real: retail's own rounding, not correct rounding (exact ties truncate, the 12-byte intermediate rounds first, subnormals shift twice)")
{
	CHECK(retailBits("16777217") == 0x4B800000u); // tie: truncated
	CHECK(retailBits("16777219") == 0x4B800001u); // tie: truncated (correct rounding: ...02)
	CHECK(retailBits("132752060") == 0x4CFD3457u); // a retail INI token: correct rounding gives ...58
	CHECK(retailBits("29782747") == 0x4BE3396Du);
	CHECK(retailBits("7.038531e-26") == 0x15AE43FDu);
	CHECK(retailBits("0.1") == 0x3DCCCCCDu);
	CHECK(retailBits("1.5") == 0x3FC00000u);
	CHECK(retailBits("-0") == 0x80000000u);
	CHECK(retailBits("1.401298464324817e-45") == 0x00000002u); // the minimum subnormal's text reads TWICE it
	CHECK(retailBits("1e-45") == 0x00000000u);
	CHECK(retailBits("7.0064923216240854e-46") == 0x00000000u);
	CHECK(retailBits("3.4028235e38") == 0x7F7FFFFFu);
	CHECK(retailBits("3.4028236e38") == 0x7F800000u);
	CHECK(retailBits("1e400") == 0x7F800000u);
	CHECK(retailBits("1e-400") == 0x00000000u);
}

TEST_CASE("win1 msvcr71 real: retail's grammar (was S-1542): an exponent marker without digits is ignored, hex reads its 0, nan and inf fail, 'd' marks an exponent")
{
	CHECK(retailBits("3e") == 0x40400000u);
	CHECK(retailBits("3ex") == 0x40400000u);
	CHECK(retailBits("4E+") == 0x40800000u);
	CHECK(retailBits("0x10") == 0x00000000u);
	CHECK(retailBits("nan") == 0xC640E400u);
	CHECK(retailBits("inf") == 0xC640E400u);
	CHECK(retailBits("+-1") == 0xC640E400u);
	CHECK(retailBits(".e5") == 0xC640E400u);
	CHECK(retailBits("  +7") == 0x40E00000u);
	CHECK(retailBits("5.") == 0x40A00000u);
	CHECK(retailBits("-.5") == 0xBF000000u);
	const char *end = nullptr;
	float f = 0.0f;
	REQUIRE(Msvcr71Real::scanfFloat("1.0,", f, &end));
	CHECK(f == 1.0f);
	CHECK(*end == ',');
	initest::Fixture fx;
	INI ini(fx.env);
	CHECK(ini.scanReal("3e") == 3.0f); // INI::scanReal: one conversion, no error (lane WIN-1 r1 refused it as S-1542)
	CHECK(ini.scanReal("0x10") == 0.0f);
	CHECK(bitsOf(ini.scanReal("16777219")) == 0x4B800001u);
	try
	{
		ini.scanReal("nan");
		FAIL("no exception");
	}
	catch (const INIException &e)
	{
		CHECK(e.code() == 3);
		CHECK(e.message() == "Expected floating point value, math op, or predefined macro, but found 'nan'");
	}
}

TEST_CASE("win1 msvcr71 real: the generated power-of-ten tables are the ones __multtenpow12 uses from the installed msvcr71.dll")
{
	const char *install = std::getenv("ROTWK_INSTALL");
	if (!install || !*install)
	{
		retailtest::printSkip("win1 msvcr71 real: power-of-ten tables against msvcr71.dll");
		return;
	}
	std::vector<unsigned char> dll;
	std::string error;
	REQUIRE_MESSAGE(retailtest::readLocalFile(std::string(install) + "/msvcr71.dll", dll, &error), error);
	// PE: the section holding the tables (_pow10pos at VA 0x7C38F690, _pow10neg at 0x7C38F7F0)
	const size_t pe = le32(dll, 0x3C);
	REQUIRE(le32(dll, pe) == 0x00004550u);
	const unsigned sections = dll[pe + 6] | (dll[pe + 7] << 8);
	const unsigned optSize = dll[pe + 20] | (dll[pe + 21] << 8);
	const std::uint32_t imageBase = le32(dll, pe + 24 + 28);
	REQUIRE(imageBase == 0x7C340000u);
	auto fileOffset = [&](std::uint32_t va) -> size_t {
		const std::uint32_t rva = va - imageBase;
		for (unsigned i = 0; i < sections; ++i)
		{
			const size_t sh = pe + 24 + optSize + 40 * i;
			const std::uint32_t vaddr = le32(dll, sh + 12), rawSize = le32(dll, sh + 16), rawPtr = le32(dll, sh + 20);
			if (rva >= vaddr && rva < vaddr + rawSize)
			{
				return rawPtr + (rva - vaddr);
			}
		}
		return 0;
	};
	int compared = 0;
	for (int neg = 0; neg < 2; ++neg)
	{
		const std::uint32_t start = neg ? 0x7C38F7F0u : 0x7C38F690u;
		for (int group = 0; group < 5; ++group)
		{
			for (int digit = 1; digit <= 7; ++digit)
			{
				if (group == 4 && digit > 1)
				{
					continue; // 10^(+-8192) and beyond: outside the 80-bit range, never reached (the parser's exponent is at most 5200)
				}
				const size_t at = fileOffset(start + 0x54 * group + 12 * (digit - 1));
				REQUIRE(at != 0);
				std::uint8_t stored[12];
				std::memcpy(stored, &dll[at], 12);
				// __multtenpow12 (0x7C373372): a low word >= 0x8000 means the entry is stored rounded up; it decrements the dword at offset 2
				if ((stored[0] | (stored[1] << 8)) >= 0x8000)
				{
					std::uint32_t mid = (std::uint32_t)stored[2] | ((std::uint32_t)stored[3] << 8) | ((std::uint32_t)stored[4] << 16) | ((std::uint32_t)stored[5] << 24);
					--mid;
					for (int k = 0; k < 4; ++k)
					{
						stored[2 + k] = (std::uint8_t)(mid >> (8 * k));
					}
				}
				std::uint8_t generated[12];
				Msvcr71Real::powerTableEntry(neg != 0, group, digit, generated);
				INFO("10^" << (neg ? "-" : "") << digit << "*8^" << group);
				CHECK(std::memcmp(stored, generated, 12) == 0);
				++compared;
			}
		}
	}
	CHECK(compared == 58);
}
