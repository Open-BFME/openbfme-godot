// OpenBFME unit tests: NumericState and retail numerics in the INI scanners. GPL-3.0.
// Expected values come from docs/PLAN.md rules 2 and 3, the disassembly cited in
// Common/NumericState.h, and above all from REAL x87 hardware: the 32-bit helper in
// tools/x87_oracle runs the retail instruction sequences at PC24 / round-to-nearest. Tests that
// need the helper print a loud SKIP when it is not built (tools/x87_oracle/build.bat).

#include "doctest.h"
#include "IniTestUtil.h"

#include "Common/NumericState.h"

#include <cfenv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#ifndef OPENBFME_X87_ORACLE_DEFAULT
#define OPENBFME_X87_ORACLE_DEFAULT ""
#endif
// '|' separated sources the default exe is built from (engine/CMakeLists.txt); empty: no staleness check
#ifndef OPENBFME_X87_ORACLE_SOURCES
#define OPENBFME_X87_ORACLE_SOURCES ""
#endif

using namespace initest;

namespace
{
struct ScopedRounding
{
	int old;
	explicit ScopedRounding(int mode)
		: old(std::fegetround())
	{
		std::fesetround(mode);
	}
	~ScopedRounding() { std::fesetround(old); }
};

std::uint32_t bitsOf(float f)
{
	std::uint32_t b;
	std::memcpy(&b, &f, sizeof(b));
	return b;
}

float floatOf(std::uint32_t b)
{
	float f;
	std::memcpy(&f, &b, sizeof(f));
	return f;
}

// ---- the hardware oracle -------------------------------------------------------------------
// Why `exe` is older than what it is built from ('|' separated paths), or "" when fresh. A helper built from older
// sources answers for code that no longer exists (a stale x87_oracle.exe prints "ERR" for requests added since), so
// the tests refuse it with the rebuild command instead of failing cryptically.
std::string staleReason(const std::string &exe, const std::string &sources)
{
	namespace fs = std::filesystem;
	std::error_code ec;
	const fs::file_time_type exeTime = fs::last_write_time(exe, ec);
	if (ec)
	{
		return "cannot stat the exe: " + ec.message();
	}
	std::size_t start = 0;
	while (!sources.empty() && start <= sources.size())
	{
		std::size_t end = sources.find('|', start);
		if (end == std::string::npos)
		{
			end = sources.size();
		}
		const std::string src = sources.substr(start, end - start);
		start = end + 1;
		const fs::file_time_type srcTime = fs::last_write_time(src, ec);
		if (ec)
		{
			return "input " + src + " is unreadable: " + ec.message();
		}
		if (srcTime > exeTime)
		{
			return fs::path(src).filename().string() + " is newer than the exe";
		}
	}
	return "";
}

std::string oraclePath()
{
	const char *env = std::getenv("OPENBFME_X87_ORACLE");
	if (env && env[0])
	{
		return env;
	}
	return OPENBFME_X87_ORACLE_DEFAULT;
}

bool oracleAvailable()
{
#ifndef _WIN32
	// the helper is a 32-bit Windows executable; runOracle only drives it on Windows (the file can still
	// exist when the worktree is shared with Windows, e.g. WSL, and must not be mistaken for available)
	std::printf("SKIP: x87 hardware oracle is a Windows executable; not run on this platform\n");
	return false;
#else
	const std::string path = oraclePath();
	std::ifstream f(path, std::ios::binary);
	if (path.empty() || !f.good())
	{
		std::printf("SKIP: x87 hardware oracle not built (%s); run tools/x87_oracle/build.bat or set OPENBFME_X87_ORACLE\n", path.c_str());
		return false;
	}
	if (!std::getenv("OPENBFME_X87_ORACLE")) // an explicit override is the caller's own binary: its sources are unknown
	{
		const std::string stale = staleReason(path, OPENBFME_X87_ORACLE_SOURCES);
		REQUIRE_MESSAGE(stale.empty(), "stale helper: x87_oracle (" << stale << "); rebuild with tools\\retail_oracle\\build.bat (or tools\\x87_oracle\\build.bat)");
	}
	return true;
#endif
}

// Sends the request lines to the helper, returns one answer line per request.
std::vector<std::string> runOracle(const std::vector<std::string> &requests)
{
#ifdef _WIN32
	const std::string base = (std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "\\openbfme-x87-" + std::to_string(std::rand()));
	const std::string in = base + ".in";
	const std::string out = base + ".out";
	{
		std::ofstream f(in, std::ios::binary);
		for (const std::string &r : requests)
		{
			f << r << "\n";
		}
	}
	const std::string cmd = "\"\"" + oraclePath() + "\" < \"" + in + "\" > \"" + out + "\"\"";
	const int rc = std::system(cmd.c_str());
	REQUIRE_MESSAGE(rc == 0, "x87 oracle failed: " << cmd);
	std::vector<std::string> answers;
	std::ifstream f(out);
	std::string line;
	while (std::getline(f, line))
	{
		answers.push_back(line);
	}
	f.close();
	std::remove(in.c_str());
	std::remove(out.c_str());
	REQUIRE(answers.size() == requests.size());
	return answers;
#else
	(void)requests;
	return {};
#endif
}

std::string hex8(std::uint32_t v)
{
	char b[16];
	std::snprintf(b, sizeof(b), "%08x", v);
	return b;
}

std::string op32Request(const char *op, float a, float b)
{
	return std::string("f32 ") + op + " " + hex8(bitsOf(a)) + " " + hex8(bitsOf(b));
}

float emulate(const std::string &op, float a, float b)
{
	if (op == "add")
	{
		return NumericState::pc24Add(a, b);
	}
	if (op == "sub")
	{
		return NumericState::pc24Sub(a, b);
	}
	if (op == "mul")
	{
		return NumericState::pc24Mul(a, b);
	}
	return NumericState::pc24Div(a, b);
}

// operands: specials, boundaries, and random patterns biased toward exponent extremes and ties
std::vector<std::uint32_t> edgeOperands()
{
	return { 0x00000000, 0x80000000, 0x00000001, 0x80000001, 0x007FFFFF, 0x00800000, 0x00800001, 0x3F800000, 0xBF800000, 0x3F800001, 0x3F7FFFFF,
		0x7F7FFFFF, 0xFF7FFFFF, 0x7F800000, 0xFF800000, 0x00400000, 0x3F000000, 0x4B000000, 0x4B000001, 0x4B800000, 0x4B7FFFFF, 0x34000000, 0x33800000,
		0x7F000000, 0x7E800000, 0x00000400, 0x00100000, 0x2F800000, 0x7FC00000, 0xFFC00000, 0x3E4CCCCD, 0x3F666666 };
}

std::uint32_t randomOperand(std::mt19937 &rng)
{
	const std::uint32_t sign = (rng() & 1u) << 31;
	std::uint32_t exp;
	switch (rng() % 6)
	{
	case 0: exp = rng() % 6; break;              // subnormal and just above
	case 1: exp = 250 + rng() % 5; break;        // near overflow
	case 2: exp = 100 + rng() % 50; break;       // moderate
	case 3: exp = 1 + rng() % 254; break;        // anywhere
	case 4: exp = 127 + (rng() % 9) - 4; break;  // around 1.0
	default: exp = 60 + rng() % 40; break;       // small (products near underflow)
	}
	std::uint32_t frac;
	switch (rng() % 6)
	{
	case 0: frac = 0; break;
	case 1: frac = 0x7FFFFF; break;
	case 2: frac = 1; break;
	case 3: frac = 0x400000; break;
	case 4: frac = 0x7FFFFF - (rng() & 3u); break;
	default: frac = rng() & 0x7FFFFFu; break;
	}
	return sign | (exp << 23) | frac;
}

// result exponent near the underflow / overflow boundaries: pick b so that a op b lands there
std::uint32_t partnerFor(const std::string &op, std::uint32_t a, std::mt19937 &rng)
{
	const int ea = (int)((a >> 23) & 0xFF);
	int eb;
	const int r = (int)(rng() % 37) - 30; // wanted biased result exponent: -30..6, straddling the subnormal edge
	if (op == "mul")
	{
		eb = r + 127 - ea; // ea + eb - 127 == r
	}
	else if (op == "div")
	{
		eb = ea + 127 - r; // ea - eb + 127 == r
	}
	else
	{
		eb = ea - (int)(rng() % 45) + 3; // addition with every exponent gap from -3 to 41
	}
	eb = eb < 0 ? 0 : (eb > 254 ? 254 : eb);
	const std::uint32_t sign = (rng() & 1u) << 31;
	return sign | ((std::uint32_t)eb << 23) | (rng() & 0x7FFFFFu);
}

bool sameResult(std::uint32_t expected, std::uint32_t actual)
{
	const bool eNaN = (expected & 0x7F800000u) == 0x7F800000u && (expected & 0x7FFFFFu);
	const bool aNaN = (actual & 0x7F800000u) == 0x7F800000u && (actual & 0x7FFFFFu);
	if (eNaN || aNaN)
	{
		return eNaN && aNaN; // NaN payloads are not emulated
	}
	return expected == actual;
}
} // namespace

TEST_CASE("NumericState::setFPMode selects round-to-nearest whatever the host state was (RW 0x440809)")
{
	for (int mode : { FE_UPWARD, FE_DOWNWARD, FE_TOWARDZERO })
	{
		ScopedRounding scoped(mode);
		REQUIRE(std::fegetround() == mode);
		NumericState::setFPMode();
		CHECK(std::fegetround() == FE_TONEAREST);
	}
}

#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
TEST_CASE("NumericState::setFPMode also clears FTZ / DAZ and re-masks the exceptions (round 2 review: min-normal * 0.5 must be 00400000)")
{
	const std::uint32_t saved = NumericState::readControlRegister();
	volatile float minNormal = std::numeric_limits<float>::min();
	volatile float half = 0.5f;
	const auto bitsOf = [](float f) {
		std::uint32_t u;
		std::memcpy(&u, &f, 4);
		return u;
	};
	// FTZ (bit 15) and DAZ (bit 6) on: the host (a driver, a plug-in) changed the environment
	NumericState::writeControlRegister(0x1F80u | 0x8000u | 0x0040u);
	{
		volatile float flushed = minNormal * half;
		REQUIRE(bitsOf(flushed) == 0u); // the premise: the product is flushed to zero
	}
	NumericState::setFPMode();
	{
		volatile float gradual = minNormal * half;
		CHECK(bitsOf(gradual) == 0x00400000u);
	}
	CHECK((NumericState::readControlRegister() & 0xFFC0u) == 0x1F80u);
	// every exception unmasked and a rounding mode set: all restored
	NumericState::writeControlRegister(0x0000u | 0x4000u);
	NumericState::setFPMode();
	CHECK((NumericState::readControlRegister() & 0xFFC0u) == 0x1F80u);
	CHECK(std::fegetround() == FE_TONEAREST);
	NumericState::writeControlRegister(saved);
}
#endif

TEST_CASE("INI::load leaves nearest rounding in effect and the evaluator ignores a hostile host rounding mode")
{
	// the premise: with upward rounding the plain float32 sum is NOT 16777216
	{
		ScopedRounding upward(FE_UPWARD);
		volatile float a = 16777216.0f;
		volatile float one = 1.0f;
		volatile float sum = a + one;
		sum = sum + one;
		REQUIRE(sum != 16777216.0f);
	}

	Fixture fx;
	float result = 0;
	fx.env.blocks.registerBlock("M", [&result](INI *i) {
		i->readLine();
		i->firstToken();
		result = i->scanReal(i->getNextToken());
	});
	ScopedRounding upward(FE_UPWARD);
	INI ini(fx.env);
	ini.loadMemory("m.ini", toBytes("M x\nKey #ADD( 16777216 1 1 )\n"), INI_LOAD_OVERWRITE);
	CHECK(result == 16777216.0f);
	CHECK(std::fegetround() == FE_TONEAREST); // retail leaves the game in this state
}

// Sol review round 2: SSE float32 differs from x87 PC24 + fstp for subnormal results
TEST_CASE("PC24 emulation: the subnormal double-rounding case from the review, 5.439772e-38 * 0.20226517 is 0x0077CF3C")
{
	const float a = floatOf(bitsOf(5.439772e-38f));
	const float b = 0.20226517f;
	CHECK(bitsOf(NumericState::pc24Mul(a, b)) == 0x0077CF3Cu);
	// the plain float32 product (what SSE and numpy compute) is one ulp lower
	volatile float va = a, vb = b;
	CHECK(bitsOf(va * vb) == 0x0077CF3Bu);

	Fixture fx;
	float result = 0;
	fx.env.blocks.registerBlock("M", [&result](INI *i) {
		i->readLine();
		i->firstToken();
		result = i->scanReal(i->getNextToken());
	});
	INI ini(fx.env);
	ini.loadMemory("m.ini", toBytes("M x\nKey #MULTIPLY( 5.439772e-38 0.20226517 )\n"), INI_LOAD_OVERWRITE);
	CHECK(bitsOf(result) == 0x0077CF3Cu);
}

TEST_CASE("PC24 emulation: hand-checked cases (ties, overflow, specials, signed zero)")
{
	// 16777216 + 1 + 1 stays at 2^24 (each step rounds to even)
	CHECK(NumericState::pc24Add(NumericState::pc24Add(16777216.0f, 1.0f), 1.0f) == 16777216.0f);
	CHECK(NumericState::pc24Add(16777216.0f, 3.0f) == 16777220.0f);
	CHECK(NumericState::pc24Mul(3.4028235e38f, 2.0f) == INFINITY);
	CHECK(NumericState::pc24Mul(-3.4028235e38f, 2.0f) == -INFINITY);
	CHECK(NumericState::pc24Div(1.0f, 0.0f) == INFINITY);
	CHECK(NumericState::pc24Div(-1.0f, 0.0f) == -INFINITY);
	CHECK(std::isnan(NumericState::pc24Div(0.0f, 0.0f)));
	CHECK(std::isnan(NumericState::pc24Sub(INFINITY, INFINITY)));
	CHECK(std::isnan(NumericState::pc24Mul(0.0f, INFINITY)));
	CHECK(bitsOf(NumericState::pc24Sub(1.5f, 1.5f)) == 0x00000000u);           // exact cancellation is +0
	CHECK(bitsOf(NumericState::pc24Add(-0.0f, -0.0f)) == 0x80000000u);
	CHECK(bitsOf(NumericState::pc24Mul(-2.0f, 0.0f)) == 0x80000000u);
	CHECK(NumericState::pc24Mul(1.0e-30f, 1.0e-30f) == 0.0f);                  // underflows to zero
	CHECK(NumericState::pc24Div(1.0f, 3.0f) == 1.0f / 3.0f);
	// for results in the normal range the emulation equals plain float32 arithmetic
	std::mt19937 rng(7);
	for (int i = 0; i < 20000; ++i)
	{
		const float a = floatOf(0x20000000u + (rng() & 0x1FFFFFFu));
		const float b = floatOf(0x20000000u + (rng() & 0x1FFFFFFu));
		volatile float va = a, vb = b;
		REQUIRE(NumericState::pc24Add(a, b) == va + vb);
		REQUIRE(NumericState::pc24Mul(a, b) == va * vb);
		REQUIRE(NumericState::pc24Div(a, b) == va / vb);
		REQUIRE(NumericState::pc24Sub(a, b) == va - vb);
	}
}

TEST_CASE("PC24 emulation matches real x87 hardware: fld; f<op>; fstp dword, over random and edge inputs")
{
	if (!oracleAvailable())
	{
		return;
	}
	std::mt19937 rng(20260930);
	const std::vector<std::uint32_t> edges = edgeOperands();
	const char *ops[] = { "add", "sub", "mul", "div" };

	std::vector<std::string> requests;
	std::vector<std::string> opNames;
	std::vector<std::pair<std::uint32_t, std::uint32_t>> operands;
	auto add = [&](const char *op, std::uint32_t a, std::uint32_t b) {
		requests.push_back(op32Request(op, floatOf(a), floatOf(b)));
		opNames.push_back(op);
		operands.push_back({ a, b });
	};
	for (const char *op : ops)
	{
		for (std::uint32_t a : edges)
		{
			for (std::uint32_t b : edges)
			{
				add(op, a, b);
			}
		}
		for (int i = 0; i < 12000; ++i)
		{
			add(op, randomOperand(rng), randomOperand(rng));
		}
		for (int i = 0; i < 12000; ++i)
		{
			const std::uint32_t a = randomOperand(rng);
			add(op, a, partnerFor(op, a, rng)); // results near the underflow edge, large exponent gaps for add
		}
	}

	const std::vector<std::string> answers = runOracle(requests);
	size_t mismatches = 0;
	for (size_t i = 0; i < requests.size(); ++i)
	{
		const std::uint32_t expected = (std::uint32_t)std::strtoul(answers[i].c_str(), nullptr, 16);
		const float got = emulate(opNames[i], floatOf(operands[i].first), floatOf(operands[i].second));
		if (!sameResult(expected, bitsOf(got)))
		{
			if (++mismatches <= 10)
			{
				MESSAGE("MISMATCH " << requests[i] << ": hardware " << answers[i] << " emulation " << hex8(bitsOf(got)));
			}
		}
	}
	MESSAGE("x87 oracle: " << requests.size() << " operations compared");
	CHECK(mismatches == 0);
}

TEST_CASE("pc24SubInt32: the integer operand is exact (fisub) while the difference is rounded to 24 bits (BFME2 0x00B03664)")
{
	// 16777216 - 16777217 is -1: the integer is NOT rounded to float first
	CHECK(NumericState::pc24SubInt32(16777216.0f, 16777217) == -1.0f);
	CHECK(NumericState::pc24Sub(16777216.0f, (float)16777217) == 0.0f); // rounding the integer first (the other branch) differs
	CHECK(NumericState::pc24SubInt32(1.5f, 1) == 0.5f);
	CHECK(NumericState::pc24SubInt32(1.5f, -2) == 3.5f);
	CHECK(bitsOf(NumericState::pc24SubInt32(3.0f, 3)) == 0x00000000u);
	CHECK(NumericState::pc24SubInt32(5.0f, 0) == 5.0f);
	CHECK(NumericState::pc24SubInt32(0.0f, std::numeric_limits<std::int32_t>::min()) == 2147483648.0f);
	CHECK(std::isnan(NumericState::pc24SubInt32(NAN, 5)));
	CHECK(NumericState::pc24SubInt32(INFINITY, 5) == INFINITY);
}

TEST_CASE("pc24SubInt32 matches real x87 hardware: fld; fisub; fstp dword, over random and edge inputs")
{
	if (!oracleAvailable())
	{
		return;
	}
	std::mt19937 rng(97531);
	const std::vector<std::uint32_t> edges = edgeOperands();
	const std::int32_t ints[] = { 0, 1, -1, 2, 16777215, 16777216, 16777217, 16777219, -16777217, 2147483647, (std::int32_t)0x80000000, 1000, -1000, 33554433 };
	std::vector<std::string> requests;
	std::vector<std::pair<std::uint32_t, std::int32_t>> operands;
	auto add = [&](std::uint32_t a, std::int32_t b) {
		char buf[64];
		std::snprintf(buf, sizeof(buf), "f32i sub %s %x", hex8(a).c_str(), (unsigned)b);
		requests.push_back(buf);
		operands.push_back({ a, b });
	};
	for (std::uint32_t a : edges)
	{
		for (std::int32_t b : ints)
		{
			add(a, b);
		}
	}
	for (int i = 0; i < 20000; ++i)
	{
		std::int32_t b = (std::int32_t)rng();
		switch (rng() % 4)
		{
		case 0: b = (std::int32_t)(rng() % 70000000) - 35000000; break;
		case 1: b = (std::int32_t)(16777216 + (int)(rng() % 9) - 4); break;
		default: break;
		}
		add(randomOperand(rng), b);
	}
	const std::vector<std::string> answers = runOracle(requests);
	size_t mismatches = 0;
	for (size_t i = 0; i < requests.size(); ++i)
	{
		const std::uint32_t expected = (std::uint32_t)std::strtoul(answers[i].c_str(), nullptr, 16);
		const float got = NumericState::pc24SubInt32(floatOf(operands[i].first), operands[i].second);
		if (!sameResult(expected, bitsOf(got)) && ++mismatches <= 10)
		{
			MESSAGE("MISMATCH " << requests[i] << ": hardware " << answers[i] << " emulation " << hex8(bitsOf(got)));
		}
	}
	MESSAGE("x87 oracle: " << requests.size() << " fisub operations compared");
	CHECK(mismatches == 0);
}

TEST_CASE("wide-operand PC24 helpers: hand-checked cases (the under operand is not rounded to float first)")
{
	// under = 16777217 exact, top = 1.0f: 16777216 (the exact difference 16777216 is representable)
	CHECK(NumericState::pc24SubD(16777217.0, 1.0) == 16777216.0f);
	// the operand rounded to float first would give 16777216 - 1 = 16777215
	CHECK(NumericState::pc24Sub((float)16777217, 1.0f) == 16777215.0f);
	// 16777217 * 1.00000012f = 16777219.0000002: above the tie between 16777218 and 16777220
	CHECK(NumericState::pc24MulD(16777217.0, (double)floatOf(0x3F800001u)) == 16777220.0f);
	CHECK(NumericState::pc24Mul(16777216.0f, floatOf(0x3F800001u)) == 16777218.0f);
	// 16777217 / 1.00000012f = 16777215.0000...: rounds to 16777215
	CHECK(NumericState::pc24DivD(16777217.0, (double)floatOf(0x3F800001u)) == 16777215.0f);
	// a string's double: 1.0005 - 1 stays accurate before the rounding to float
	CHECK(bitsOf(NumericState::pc24SubD(1.0005, 1.0)) == 0x3A03126Fu);
	CHECK(bitsOf(NumericState::pc24SubD((double)1.0005f, 1.0)) == 0x3A031000u); // rounding 1.0005 to float first loses it
	CHECK(NumericState::pc24AddD(1.5, 2.25) == 3.75f);
	CHECK(std::isnan(NumericState::pc24SubD(INFINITY, INFINITY)));
	CHECK(NumericState::pc24MulD(1e200, 1e200) == INFINITY);
	CHECK(NumericState::pc24DivD(1.0, 0.0) == INFINITY);
	CHECK(bitsOf(NumericState::pc24SubD(1.5, 1.5)) == 0x00000000u);
}

TEST_CASE("wide-operand PC24 helpers match real x87 hardware: fld qword; f<op> dword; fstp dword, over random and edge inputs")
{
	if (!oracleAvailable())
	{
		return;
	}
	std::mt19937_64 rng(1357911);
	std::mt19937 rng32(2468);
	auto bitsOfDouble = [](double d) {
		std::uint64_t b;
		std::memcpy(&b, &d, sizeof(b));
		return b;
	};
	auto doubleOf = [](std::uint64_t b) {
		double d;
		std::memcpy(&d, &b, sizeof(d));
		return d;
	};
	std::vector<double> edgeDoubles = { 0.0, -0.0, 1.0, -1.0, 16777217.0, 16777216.0, 16777219.0, 2147483647.0, -2147483648.0, 1.0005, 1.00099999, 0.001, 1e-10,
		5.439772e-38, 1e-320, 4.9e-324, 1e300, 1.7976931348623157e308, INFINITY, -INFINITY, (double)NAN, 33554433.0, 0.1, 1.0 / 3.0 };
	auto randomDouble = [&]() {
		const std::uint64_t sign = (rng() & 1ull) << 63;
		std::uint64_t exp;
		switch (rng() % 5)
		{
		case 0: exp = 1023 - 30 + rng() % 70; break;       // moderate
		case 1: exp = rng() % 4; break;                      // tiny / subnormal doubles
		case 2: exp = 1023 - 160 + rng() % 40; break;        // near the float subnormal range
		case 3: exp = 2040 + rng() % 8; break;               // near overflow
		default: exp = 1023 + rng() % 40; break;             // up to 2^40
		}
		std::uint64_t frac = rng() & ((1ull << 52) - 1);
		if (rng() % 7 == 0)
		{
			frac &= ~((1ull << 29) - 1); // exactly a float significand
		}
		return doubleOf(sign | (exp << 52) | frac);
	};
	const char *ops[] = { "add", "sub", "subr", "mul", "div" };
	std::vector<std::string> requests;
	struct Case
	{
		std::string op;
		double a;
		float b;
	};
	std::vector<Case> cases;
	auto add = [&](const char *op, double a, float b) {
		char buf[96];
		std::snprintf(buf, sizeof(buf), "f64 %s %llx %s", op, (unsigned long long)bitsOfDouble(a), hex8(bitsOf(b)).c_str());
		requests.push_back(buf);
		cases.push_back({ op, a, b });
	};
	const std::vector<std::uint32_t> edgeFloats = edgeOperands();
	for (const char *op : ops)
	{
		for (double a : edgeDoubles)
		{
			for (std::uint32_t b : edgeFloats)
			{
				add(op, a, floatOf(b));
			}
		}
		for (int i = 0; i < 12000; ++i)
		{
			add(op, randomDouble(), floatOf(randomOperand(rng32)));
		}
		for (int i = 0; i < 8000; ++i)
		{
			// an exact 32-bit integer under, a float near it over: the rounding edge of the review cases
			const std::int32_t n = (std::int32_t)rng();
			const float f = floatOf(0x3F800000u + (std::uint32_t)(rng() % 5));
			add(op, (double)n, f);
		}
	}
	const std::vector<std::string> answers = runOracle(requests);
	size_t mismatches = 0;
	for (size_t i = 0; i < requests.size(); ++i)
	{
		const std::uint32_t expected = (std::uint32_t)std::strtoul(answers[i].c_str(), nullptr, 16);
		const Case &c = cases[i];
		float got;
		if (c.op == "add")
		{
			got = NumericState::pc24AddD(c.a, (double)c.b);
		}
		else if (c.op == "sub")
		{
			got = NumericState::pc24SubD(c.a, (double)c.b);
		}
		else if (c.op == "subr")
		{
			got = NumericState::pc24SubD((double)c.b, c.a);
		}
		else if (c.op == "mul")
		{
			got = NumericState::pc24MulD(c.a, (double)c.b);
		}
		else
		{
			got = NumericState::pc24DivD(c.a, (double)c.b);
		}
		if (!sameResult(expected, bitsOf(got)) && ++mismatches <= 10)
		{
			MESSAGE("MISMATCH " << requests[i] << ": hardware " << answers[i] << " emulation " << hex8(bitsOf(got)));
		}
	}
	MESSAGE("x87 oracle: " << requests.size() << " wide-operand operations compared");
	CHECK(mismatches == 0);
}

TEST_CASE("duration: signed fild plus a ROUNDED 2^32 correction, then PC24 multiply (RW 0x73A440-0x73A458)")
{
	CHECK(NumericState::durationProduct(0, 0.005f) == 0.0);
	CHECK(NumericState::ceilScaled(1000, 0.005f) == 5u);
	CHECK(NumericState::ceilScaled(200, 0.005f) == 1u);
	// PLAN rule 2: 52,428,805 ms is 262145 frames; the early float32 conversion gives 262144
	CHECK(NumericState::durationProduct(52428805u, 0.005f) == 262144.03125);
	CHECK(NumericState::ceilScaled(52428805u, 0.005f) == 262145u);
	volatile float early = (float)52428805u;
	const float scaled = early * 0.005f;
	CHECK((unsigned)std::ceil((double)scaled) == 262144u);
	// Sol review round 2: 2147483749 ms. fild reads it as negative, fadd 2^32 rounds it to
	// 2147483648 (24-bit significand), so retail gets 10,737,418 frames, not 10,737,419
	CHECK(NumericState::ceilScaled(2147483749u, 0.005f) == 10737418u);
	CHECK(NumericState::ceilScaled(4294967295u, 0.005f) == 21474836u); // 2^32 - 1 rounds to 2^32 first; value taken from the x87 helper
}

TEST_CASE("duration product matches real x87 hardware over random and edge inputs, including >= 2^31 (RW 0x73A440-0x73A458)")
{
	if (!oracleAvailable())
	{
		return;
	}
	std::mt19937 rng(424242);
	std::vector<std::string> requests;
	std::vector<std::pair<std::uint32_t, float>> inputs;
	auto add = [&](std::uint32_t ms, float scale) {
		char buf[64];
		std::snprintf(buf, sizeof(buf), "dur %x %s", ms, hex8(bitsOf(scale)).c_str());
		requests.push_back(buf);
		inputs.push_back({ ms, scale });
	};
	const std::uint32_t edges[] = { 0u, 1u, 199u, 200u, 201u, 16777215u, 16777216u, 16777217u, 52428805u, 2147483647u, 2147483648u, 2147483649u, 2147483749u,
		2147483777u, 4294967295u, 4294967294u, 3221225472u, 3221225473u };
	const float scales[] = { 0.005f, 0.2f, 1.0f, 0.04f, 0.01f };
	for (std::uint32_t ms : edges)
	{
		for (float s : scales)
		{
			add(ms, s);
		}
	}
	for (int i = 0; i < 20000; ++i)
	{
		std::uint32_t ms = rng();
		switch (rng() % 4)
		{
		case 0: ms = 0x80000000u + (rng() % 4096); break;
		case 1: ms = 0xFFFFF000u + (rng() % 4096); break;
		case 2: ms = rng() % 200000; break;
		default: break;
		}
		add(ms, (rng() % 3 == 0) ? floatOf(randomOperand(rng) & 0x7FFFFFFFu) : 0.005f);
	}
	const std::vector<std::string> answers = runOracle(requests);
	size_t mismatches = 0;
	for (size_t i = 0; i < requests.size(); ++i)
	{
		const std::uint64_t expectedBits = std::strtoull(answers[i].c_str(), nullptr, 16);
		const double got = NumericState::durationProduct(inputs[i].first, inputs[i].second);
		std::uint64_t gotBits;
		std::memcpy(&gotBits, &got, sizeof(gotBits));
		double expected;
		std::memcpy(&expected, &expectedBits, sizeof(expected));
		const bool same = (std::isnan(expected) && std::isnan(got)) || expectedBits == gotBits;
		if (!same && ++mismatches <= 10)
		{
			MESSAGE("MISMATCH " << requests[i] << ": hardware " << answers[i]);
		}
	}
	MESSAGE("x87 oracle: " << requests.size() << " duration products compared");
	CHECK(mismatches == 0);
}

TEST_CASE("the hardware oracle runs at the retail control word (precision 24-bit, round to nearest)")
{
	if (!oracleAvailable())
	{
		return;
	}
	const std::vector<std::string> answers = runOracle({ "cw" });
	CHECK(answers[0] == "007f");
}

TEST_CASE("staleReason: a helper older than any of its sources is stale, a newer one is fresh, a bad path is an error")
{
	namespace fs = std::filesystem;
	const fs::path dir = fs::temp_directory_path() / ("openbfme-stale-" + std::to_string(std::rand()));
	fs::create_directories(dir);
	const fs::path exe = dir / "tool.exe";
	const fs::path a = dir / "a.cpp";
	const fs::path b = dir / "b.bat";
	for (const fs::path &p : { exe, a, b })
	{
		std::ofstream(p) << "x";
	}
	const fs::file_time_type t0 = fs::file_time_type::clock::now();
	fs::last_write_time(a, t0 - std::chrono::hours(2));
	fs::last_write_time(b, t0 - std::chrono::hours(3));
	fs::last_write_time(exe, t0 - std::chrono::hours(1));
	const std::string both = a.string() + "|" + b.string();

	CHECK(staleReason(exe.string(), both).empty());
	CHECK(staleReason(exe.string(), "").empty()); // no sources: nothing can be older

	fs::last_write_time(b, t0); // the second source is edited after the exe was built
	CHECK(staleReason(exe.string(), both) == "b.bat is newer than the exe");
	fs::last_write_time(b, t0 - std::chrono::hours(3));
	fs::last_write_time(a, t0);
	CHECK(staleReason(exe.string(), both) == "a.cpp is newer than the exe");

	CHECK(staleReason(exe.string(), (dir / "nosuch.cpp").string()).find("unreadable") != std::string::npos);
	CHECK(staleReason((dir / "nosuch.exe").string(), both).find("cannot stat") != std::string::npos);
	fs::remove_all(dir);
}
