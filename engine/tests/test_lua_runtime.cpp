// OpenBFME unit tests: the Lua 4.0.1 runtime of EA's fork (lane LUA-1; spec lua-scripting.md 2.3-2.6, 5.3, 8).
// Expected values, by source:
//   * RECORDED FROM RETAIL: tools/retail_oracle/test_retail_oracle.py (ORACLE-1) drives the real EA Lua inside game.dat on Windows; the
//     boolean results, the library function counts (37/19/11/23/5) and the type(<boolean>) fault are its recorded observations;
//   * the Lua 4.0 manual for plain Lua;
//   * the retail disassembly cited in engine/src/Libraries/Lua/README.md for the cases the oracle has not been run on (the quirks);
//   * x87 hardware through NumericState for the PC24 arithmetic.
// Nothing is read back from the code under test.

#include "doctest.h"
#include "LuaTestUtil.h"

#include "Common/MD5.h"
#include "Common/NumericState.h"

#include <iterator>
#include <cfenv>
#include <clocale>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <random>
#include <sstream>

#ifndef OPENBFME_LUA_PRISTINE_DIR
#define OPENBFME_LUA_PRISTINE_DIR "thirdparty/lua-4.0.1"
#endif
#ifndef OPENBFME_LUA_PATCH_DIR
#define OPENBFME_LUA_PATCH_DIR "src/Libraries/Lua"
#endif

using luatest::run;

namespace
{
struct Fixture
{
	LuaReportSink sink;
	LuaRuntime rt{ sink, "test" };
	std::string eval(const std::string &code) { return run(rt, code).values; }
};

std::string readText(const std::string &path)
{
	std::ifstream f(path, std::ios::binary);
	std::stringstream ss;
	ss << f.rdbuf();
	return ss.str();
}
} // namespace

// ---------------------------------------------------------------------------------------------------------------------
// vendoring
// ---------------------------------------------------------------------------------------------------------------------
TEST_CASE("lua vendoring: every file of the pristine lua-4.0.1 tree matches the manifest of the tarball (md5 a31d963d...)")
{
	const std::string manifest = readText(std::string(OPENBFME_LUA_PRISTINE_DIR) + ".manifest");
	REQUIRE(!manifest.empty());
	std::istringstream in(manifest);
	std::string line;
	int files = 0;
	bool sawTarball = false;
	while (std::getline(in, line))
	{
		if (!line.empty() && line.back() == '\r')
		{
			line.pop_back();
		}
		if (line.rfind("# lua-4.0.1.tar.gz", 0) == 0)
		{
			sawTarball = line.find("a31d963dbdf727f9b34eee1e0d29132c") != std::string::npos &&
				line.find("df746e149cf6939e90009d2e540eee918d585b4d1bc6d68b19316a050d484d2a") != std::string::npos;
		}
		if (line.empty() || line[0] == '#')
		{
			continue;
		}
		const size_t sp = line.find("  ");
		REQUIRE(sp != std::string::npos);
		const std::string digest = line.substr(0, sp), name = line.substr(sp + 2);
		std::string err;
		const std::string actual = MD5::ofFile(std::string(OPENBFME_LUA_PRISTINE_DIR) + "/" + name, &err);
		INFO(name << " " << err);
		CHECK(actual == digest);
		++files;
	}
	CHECK(sawTarball);
	CHECK(files == 48);
}

TEST_CASE("lua vendoring: every altered file of the patch layer says so (the licence: altered source versions must be plainly marked)")
{
	const char *altered[] = { "lapi.c", "lbaselib.c", "lcode.c", "liolib.c", "llex.c", "llex.h", "llimits.h", "lmathlib.c", "lobject.c", "lobject.h",
		"lopcodes.h", "lparser.c", "lstate.c", "lstate.h", "ltable.c", "ltable.h", "lvm.c", "lfunc.c" };
	for (const char *n : altered)
	{
		const std::string text = readText(std::string(OPENBFME_LUA_PATCH_DIR) + "/" + n);
		INFO(n);
		CHECK(text.find("ALTERED SOURCE VERSION") != std::string::npos);
	}
	// the pristine file never says it
	CHECK(readText(std::string(OPENBFME_LUA_PRISTINE_DIR) + "/src/lvm.c").find("ALTERED SOURCE VERSION") == std::string::npos);
}

// ---------------------------------------------------------------------------------------------------------------------
// the language: values recorded from retail
// ---------------------------------------------------------------------------------------------------------------------
TEST_CASE("lua: the version, stock results and the status codes recorded from retail")
{
	Fixture f;
	CHECK(f.eval("return _VERSION") == "string:Lua 4.0.1");
	CHECK(f.eval("return 1+2") == "number:3");
	CHECK(f.eval("return 10/4") == "number:2.5");
	CHECK(f.eval("return 'a'..'b'") == "string:ab");
	CHECK(f.eval("a = 3 return a == 3 and 'eq' or 'ne'") == "string:eq");
	CHECK(f.eval("return strlen('abc')") == "number:3");
	CHECK(run(f.rt, "return foo(").status == 3); // LUA_ERRSYNTAX
	CHECK(run(f.rt, "error('boom')").status == 1); // LUA_ERRRUN
}

TEST_CASE("lua: EA's boolean tag: the 16 results recorded from retail (ORACLE-1, test_lua_ea_boolean_semantics_recorded_from_retail)")
{
	Fixture f;
	const std::pair<const char *, const char *> recorded[] = {
		{ "return 3 < 4", "boolean:true" },
		{ "return 1 == 1", "boolean:true" },
		{ "return 1 ~= 2", "boolean:true" },
		{ "return nil == false", "boolean:false" },
		{ "return nil == nil", "boolean:true" },
		{ "return not nil", "boolean:false" },
		{ "return not 1", "boolean:false" },
		{ "return not not nil", "boolean:false" },
		{ "return true", "boolean:true" },
		{ "return false", "boolean:false" },
		{ "return nil and 1", "boolean:false" },
		{ "return 1 and nil", "nil" },
		{ "return 0 and 1", "number:1" },
		{ "return false or 5", "number:5" },
		{ "return true and 7", "number:7" },
		{ "return tostring(1<2)", "string:true" },
	};
	for (const auto &r : recorded)
	{
		INFO(r.first);
		CHECK(f.eval(r.first) == r.second);
	}
}

TEST_CASE("lua: the five library openers register 37 / 19 / 11 / 23 / 5 function globals (recorded from retail; the spec's 33/11/11/23/5 is wrong)")
{
	LuaReportSink sink;
	lua_State *L = lua_open(0x100);
	REQUIRE(L);
	void (*openers[5])(lua_State *) = { lua_baselibopen, lua_iolibopen, lua_strlibopen, lua_mathlibopen, lua_dblibopen };
	const int expected[5] = { 37, 19, 11, 23, 5 };
	int previous = 0;
	for (int i = 0; i < 5; ++i)
	{
		openers[i](L);
		lua_settop(L, 0);
		REQUIRE(lua_dostring(L, "n=0 foreach(globals(), function(k,v) if type(v)=='function' then n=n+1 end end)") == 0);
		lua_getglobal(L, "n");
		const int n = (int)lua_tonumber(L, -1);
		lua_settop(L, 0);
		CHECK(n - previous == expected[i]);
		previous = n;
	}
	lua_close(L);
}

TEST_CASE("lua: type() of a boolean is the compatibility fault S-041 / S-120 in the retail-compatible profile: reported, counted, the running chunk aborted, no later effect")
{
	Fixture f;
	f.eval("after = 'untouched'");
	// the effect before the call stays, the one after it never happens, and no value is returned
	CHECK(run(f.rt, "before = 1 local t = type(true) after = 'ran' return t").status == 1);
	CHECK(f.eval("return before") == "number:1");
	CHECK(f.eval("return after") == "string:untouched");
	REQUIRE(f.rt.faults().size() == 1);
	CHECK(f.rt.faults()[0].find("faults") != std::string::npos);
	CHECK(f.sink.has("S-120", "COMPATIBILITY FAULT"));
	// the argument error path of a function given a boolean names the type: the same fault
	CHECK(run(f.rt, "getn(true)").status == 1);
	CHECK(f.rt.faults().size() == 2);
	// the other tags keep their stock names
	CHECK(f.eval("return type(nil)") == "string:nil");
	CHECK(f.eval("return type(1)") == "string:number");
	CHECK(f.eval("return type({})") == "string:table");
	CHECK(f.eval("return type(print)") == "string:function");
	CHECK(f.rt.faults().size() == 2);
}

TEST_CASE("lua: the Enhanced profile, selected explicitly, answers type(<boolean>) with \"boolean\" and counts no fault")
{
	Fixture f;
	f.rt.setEnhancedProfile(true);
	CHECK(lua_ea_getprofile(f.rt.state()) == LUA_EA_PROFILE_ENHANCED);
	CHECK(f.eval("return type(true)") == "string:boolean");
	CHECK(f.rt.faults().empty());
	CHECK(f.sink.has("S-120", "Enhanced profile"));
	Fixture g; // the default is the retail-compatible profile
	CHECK(lua_ea_getprofile(g.rt.state()) == LUA_EA_PROFILE_RETAIL);
}

TEST_CASE("lua: every host-independent hash: the string hash is explicit 32 bit arithmetic (goldens computed with 32 bit masks), userdata and table keys never hash an address")
{
	// computed by an independent 32 bit implementation (the review's value for ObjectGrantUpgrade is 62A141AD; LP64 `unsigned long' gives C23D9AAD)
	CHECK(luaEA_stringhash("ObjectGrantUpgrade", 18) == 0x62A141ADu);
	CHECK(luaEA_stringhash("_STDIN", 6) == 0x792B60E9u);
	CHECK(luaEA_stringhash("OnCreated", 9) == 0x51D0289Au);
	CHECK(luaEA_stringhash("ObjID#00000001", 14) == 0x332BEAEFu);
	CHECK(luaEA_stringhash("a", 1) == 0x80u);
	CHECK(luaEA_stringhash("", 0) == 0u);
	CHECK(luaEA_stringhash(std::string(100, 'x').c_str(), 100) == 0xD1E894F5u); // longer than 32: the step skips characters
}

TEST_CASE("lua: iteration order does not depend on the heap: the same program gives the same order of every global, the standard handles included, whatever was allocated before")
{
	const char *program = "s = '' foreach(globals(), function(k, v) s = s .. k .. ' ' end) return s";
	std::vector<std::string> orders;
	std::vector<void *> keep;
	std::mt19937 rng(7);
	for (int round = 0; round < 8; ++round)
	{
		// perturb the allocator: blocks of random sizes, some kept alive, some freed
		for (int i = 0; i < 200 + round * 37; ++i)
		{
			void *p = std::malloc(8 + rng() % 4096);
			if (rng() % 3 == 0)
			{
				keep.push_back(p);
			}
			else
			{
				std::free(p);
			}
		}
		Fixture f;
		// tables and functions as keys as well
		f.eval("t1 = {} t2 = {} function g1() end function g2() end m = {} m[t1] = 1 m[t2] = 2 m[g1] = 3 m[g2] = 4 m[print] = 5 m[_STDIN] = 6 m[_STDOUT] = 7 m[_STDERR] = 8");
		std::string o = f.eval(program);
		o += "|" + f.eval("s = '' foreach(m, function(k, v) s = s .. v end) return s");
		orders.push_back(o);
	}
	for (void *p : keep)
	{
		std::free(p);
	}
	for (const std::string &o : orders)
	{
		CHECK(o == orders[0]);
	}
	CHECK(orders[0].find("_STDIN") != std::string::npos);
	// userdata blocks (lua_newuserdata) take their hash from a creation serial: two states give the same serials
	LuaReportSink sink;
	lua_State *a = lua_open(0x100), *b = lua_open(0x100);
	void *noise = std::malloc(100000);
	for (lua_State *L : { a, b })
	{
		lua_newuserdata(L, 16);
		lua_newuserdata(L, 16);
	}
	CHECK(lua_gettop(a) == lua_gettop(b));
	std::free(noise);
	lua_close(a);
	lua_close(b);
}

// Runs the separate probe process (tests/lua_userdata_probe.cpp) with a heap shift and returns what it printed.
static std::string runUserdataProbe(int shift)
{
#ifdef OPENBFME_LUA_PROBE_EXE
	const std::string out = std::string("lua_probe_out_") + std::to_string(shift) + ".txt";
	std::string cmd = std::string("\"") + OPENBFME_LUA_PROBE_EXE + "\" " + std::to_string(shift) + " > " + out;
#ifdef _WIN32
	cmd = "\"" + cmd + "\"";
#endif
	const int rc = std::system(cmd.c_str());
	REQUIRE_MESSAGE(rc == 0, "probe failed: " << cmd);
	std::ifstream f(out, std::ios::binary);
	std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	f.close();
	std::remove(out.c_str());
	return text;
#else
	(void)shift;
	return std::string();
#endif
}

TEST_CASE("lua: lua_pushusertag userdata are deterministic too: independent processes with different heap histories give the same table order, interning and finalizer order")
{
	// Six logical userdata made from caller pointers in the same order; the six pointers (and so their addresses and relative address order)
	// differ with the shift. Before the fix their hash was the pointer value: the same program printed cfdeba, adfebc and dfebca.
	const int shifts[] = { 0, 1, 7, 32, 96, 127 };
	const std::string first = runUserdataProbe(0);
	REQUIRE(!first.empty());
	for (int shift : shifts)
	{
		const std::string out = runUserdataProbe(shift);
		INFO("shift " << shift << ": " << out);
		CHECK(out == first);
	}
	// repeated pointer / tag pairs are one record, LUA_ANYTAG finds the earlier record, another tag is another record
	CHECK(first.find("same=1 anytag=1 othertag=0") != std::string::npos);
	// the order itself is a function of the creation serials alone, so it is pinned (no address can enter it):
	CHECK(first == "iter=123450\nsame=1 anytag=1 othertag=0\ngc=0420531\n");
}

TEST_CASE("lua numerics: the number text is exact and independent of the FPU rounding mode and the locale (the review's probe: 1.2345678901234567 is 1.234567890123457 in every mode)")
{
	char b[48];
	auto text = [&](double v) { luaEA_number2str(b, v); return std::string(b); };
	const int mode = std::fegetround();
	// the input is formed under round-to-nearest: the simulation build is compiled with -frounding-math / /fp:strict, so a division written in the
	// loop below would itself be rounded in the loop's mode (the point of the test is the number TEXT, not the division)
	const double twoThirds = 2.0 / 3.0;
	for (int m : { FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO })
	{
		std::fesetround(m);
		CHECK(text(1.2345678901234567) == "1.234567890123457");
		CHECK(text(0.1) == "0.1");
		CHECK(text(twoThirds) == "0.6666666666666666");
		CHECK(text(-1.2345678901234567e100) == "-1.234567890123457e+100");
	}
	std::fesetround(mode);
	// a locale with a comma decimal point, when the host has one, changes nothing
	const char *old = std::setlocale(LC_ALL, nullptr);
	const std::string saved = old ? old : "C";
	for (const char *loc : { "de_DE.UTF-8", "fr_FR.UTF-8", "de_DE", "fr_FR" })
	{
		if (std::setlocale(LC_ALL, loc))
		{
			CHECK(text(1234.5) == "1234.5");
			break;
		}
	}
	std::setlocale(LC_ALL, saved.c_str());
	// the cases that do not depend on MSVCR71's tie handling (S-123)
	CHECK(text(100000.0) == "100000");
	CHECK(text(1e15) == "1000000000000000");
	CHECK(text(1e16) == "1e+016");
	CHECK(text(123456789012345678.0) == "1.234567890123457e+017");
	CHECK(text(0.0001) == "0.0001");
	CHECK(text(0.00001) == "1e-005");
	CHECK(text(-0.0) == "0"); // MSVCR71 clears the sign of a zero (0x7C37208D)
	CHECK(text(0.0) == "0");
	CHECK(text(5e-324) == "4.940656458412465e-324");
	CHECK(text(1.7976931348623157e308) == "1.797693134862316e+308");
	CHECK(text(9007199254740993.0) == "9007199254740992");
	CHECK(text(0.30000000000000004) == "0.3");
	CHECK(text(1.0 / 3.0) == "0.3333333333333333");
	CHECK(text(1e23) == "9.999999999999999e+022"); // the double nearest 1e23 is 99999999999999991611392
	CHECK(text(4294967296.5) == "4294967296.5");
}

TEST_CASE("lua numerics: %.16g is MSVCR71's two stage conversion (17 digits, then 16, each rounded up by its guard digit alone), pinned against values executed from the DLL's own instructions")
{
	// Each golden was produced by running msvcr71.dll's sprintf("%.16g") (md5 86f1895ae8c5e8b17d99ece768a70732) in an x86 emulator
	// (review probe msvcr71_format_r2.py, not a running Windows oracle); ties, non-ties, carries and the extremes.
	char b[48];
	auto text = [&](double v) { luaEA_number2str(b, v); return std::string(b); };
	struct Case { double v; const char *text; };
	const Case cases[] = {
		{ 1000000000000002.5, "1000000000000003" }, { 1000000000000004.5, "1000000000000005" }, { -1000000000000002.5, "-1000000000000003" },
		{ 1000000000000003.5, "1000000000000004" }, { -0.0, "0" }, { 0.0, "0" }, { 1.2345678901234567, "1.234567890123457" },
		{ 0.9999999999999994, "0.9999999999999994" }, { 0.9999999999999996, "0.9999999999999996" }, { 9999999999999998.0, "9999999999999998" },
		{ 99999999999999.95, "99999999999999.95" }, { -1.3385568178638965e-145, "-1.338556817863897e-145" }, { -2000738.4283522505, "-2000738.428352251" },
		{ 1114491761395.9075, "1114491761395.908" }, { 8.321641678031264e-07, "8.321641678031265e-007" }, { 2.0208604945989995e-254, "2.020860494599e-254" },
		{ 6.02539345405255e+201, "6.02539345405255e+201" }, { 1e23, "9.999999999999999e+022" }, { 5e-324, "4.940656458412465e-324" },
		{ 2.2250738585072014e-308, "2.225073858507201e-308" }, { 1.7976931348623157e308, "1.797693134862316e+308" },
		{ 123456789012345678.0, "1.234567890123457e+017" }, { 0.1, "0.1" }, { 1.0 / 3.0, "0.3333333333333333" }, { 2.0 / 3.0, "0.6666666666666666" },
		{ 0.1 + 0.2, "0.3" }, { 3.14159265358979, "3.14159265358979" }, { 9007199254740994.0, "9007199254740994" }, { 9.223372036854776e+18, "9.223372036854776e+018" },
	};
	for (const Case &c : cases)
	{
		INFO("value " << c.v << " expected " << c.text);
		CHECK(text(c.v) == c.text);
	}
	// the difference to a correctly rounded conversion is known and registered (S-123): the exact value of 1000000000000002.5 is a tie that
	// round-half-even would print as ...002
	CHECK(text(1000000000000002.5) != "1000000000000002");
}

TEST_CASE("lua numerics: tonumber with a base is MSVCR71's 32 bit unsigned strtoul widened as unsigned (RW 0xB5FA89-0xB5FAE1): '-1' in base 16 is 4294967295, overflow saturates")
{
	Fixture f;
	CHECK(f.eval("return tonumber('-1', 16)") == "number:4294967295");
	CHECK(f.eval("return tonumber('ffffffff', 16)") == "number:4294967295");
	CHECK(f.eval("return tonumber('100000000', 16)") == "number:4294967295"); // overflow: ULONG_MAX
	CHECK(f.eval("return tonumber('-100000000', 16)") == "number:4294967295");
	CHECK(f.eval("return tonumber(' 0x1f ', 16)") == "number:31");
	CHECK(f.eval("return tonumber('+7f', 16)") == "number:127");
	CHECK(f.eval("return tonumber('z', 36)") == "number:35");
	CHECK(f.eval("return tonumber('-5', 10 + 0 * 1)") == "number:-5"); // base 10 is the decimal path
	CHECK(f.eval("return tonumber('102', 2)") == "nil");
	CHECK(f.eval("return tonumber('', 16)") == "nil");
	CHECK(f.eval("return tonumber('12 x', 16)") == "nil");
	CHECK(f.eval("return tonumber('-2', 16)") == "number:4294967294");
	// numbers become ints through _ftol (low 32 bits), not a host cast: strsub(s, 2^32 + 2) is strsub(s, 2)
	CHECK(f.eval("return strsub('abcdef', 4294967298)") == "string:bcdef");
	CHECK(luaEA_ftol(4294967298.0) == 2);
	CHECK(luaEA_ftol(-1.9) == -1);
	CHECK(luaEA_ftol(3e9) == (int)(std::int32_t)3000000000u);
	CHECK(luaEA_ftol(1e30) == 0);
	CHECK(luaEA_ftol(NAN) == 0);
}

TEST_CASE("lua numerics: what stays unresolved is reported: S-123 names the number text's two stage conversion, strtod rounding and the CRT math library")
{
	Fixture f;
	CHECK(f.sink.has("S-123", "number text"));
	CHECK(f.sink.has("S-123", "two stage conversion"));
	CHECK(f.sink.has("S-123", "1000000000000002.5 prints 1000000000000003"));
	CHECK(f.sink.has("S-123", "strtod"));
	CHECK(f.sink.has("S-123", "CRT math"));
}

TEST_CASE("lua: `not` used as a value is always a false boolean (RW 0xB63759 reads the operand's truth after overwriting its tag); in a condition it is exact")
{
	Fixture f;
	// recorded from retail: not nil / not 1 / not not nil are all false
	CHECK(f.eval("x = 5 return not x") == "boolean:false");
	CHECK(f.eval("x = nil return not x") == "boolean:false");
	CHECK(f.eval("local t = not nil; return t == false") == "boolean:true");
	// OP_NOT + OP_JMPF is rewritten to OP_JMPT by the compiler, so conditions on `not` work: from the code generator, not the VM
	CHECK(f.eval("x = nil if not x then return 'taken' end return 'skipped'") == "string:taken");
	CHECK(f.eval("x = 1 if not x then return 'taken' end return 'skipped'") == "string:skipped");
	CHECK(f.eval("x = nil while not x do x = 1 end return x") == "number:1");
}

TEST_CASE("lua: booleans: equality compares the int, ordering is signed int, conversions, concatenation")
{
	Fixture f;
	CHECK(f.eval("return true == true") == "boolean:true");
	CHECK(f.eval("return true == false") == "boolean:false");
	CHECK(f.eval("return false < true") == "boolean:true");
	CHECK(f.eval("return true < false") == "boolean:false");
	CHECK(f.eval("return tostring(false)") == "string:false");
	CHECK(f.eval("return tostring(nil)") == "string:nil");
	CHECK(f.eval("return 'v='..tostring(true)") == "string:v=true");
	// a boolean is not a number for the VM (RW 0xB62570 converts only strings): arithmetic on it is an error; concatenation converts it
	// like a number does (RW 0xB625B0 turns tag 6 into "true" / "false")
	CHECK(run(f.rt, "return true + 1").status == 1);
	CHECK(f.eval("return 'x' .. true") == "string:xtrue");
	CHECK(f.eval("return 'x' .. false .. 'y'") == "string:xfalsey");
	// ordering across kinds is an error (no tag method)
	CHECK(run(f.rt, "return true < 1").status == 1);
}

TEST_CASE("lua: a table as a key is hashed by its creation serial, never its address: iteration order is the same in every run")
{
	Fixture f1;
	Fixture f2;
	const std::string code =
		"t1 = {} t2 = {} t3 = {} m = {} m[t1] = 1 m[t2] = 2 m[t3] = 3 s = '' "
		"foreach(m, function(k, v) s = s .. v end) return s";
	const std::string a = f1.eval(code);
	const std::string b = f2.eval(code);
	CHECK(a == b);
	CHECK(a.size() == std::string("string:123").size()); // all three visited
	// tostring prints the serial in the %p shape of MSVCR71 (8 upper case hex digits), not an address
	const std::string s = f1.eval("return tostring({})");
	CHECK(s.rfind("string:table: ", 0) == 0);
	CHECK(s.size() == std::string("string:table: ").size() + 8);
}

TEST_CASE("lua: the object handle table carries an integer payload (RW 0xB5BB30 / 0xB5B3E0) and a boolean reads 0")
{
	Fixture f;
	lua_State *L = f.rt.state();
	lua_newtablewithid(L, 0x1234);
	lua_pushnumber(L, 7);
	lua_pushboolean(L, 1);
	lua_newtable(L);
	CHECK(lua_toobjid(L, 1) == 0x1234);
	CHECK(lua_toobjid(L, 2) == 0);   // a number
	CHECK(lua_toobjid(L, 3) == 0);   // a boolean
	CHECK(lua_toobjid(L, 4) == 0);   // an ordinary table
	CHECK(lua_toobjid(L, 99) == 0);  // no such index
	CHECK(lua_type(L, 1) == LUA_TTABLE);
	CHECK(lua_toboolean(L, 3) == 1);
	CHECK(lua_toboolean(L, 2) == 1); // everything but nil and false
	CHECK(lua_toboolean(L, 99) == 0);
	lua_settop(L, 0);
}

// ---------------------------------------------------------------------------------------------------------------------
// numerics
// ---------------------------------------------------------------------------------------------------------------------
TEST_CASE("lua numerics: round24 is one rounding of the exact result (a tie breaks to even, a hair above a tie rounds up)")
{
	// 1 + 2^-24 is exactly half way between 1 and 1 + 2^-23: to even gives 1
	CHECK(luaEA_add(LUA_EA_NUM_PC24, 1.0, std::ldexp(1.0, -24)) == 1.0);
	// the same sum with 2^-60 more is above the tie: rounds up, which a round-to-double-then-to-24-bits would miss
	CHECK(luaEA_add(LUA_EA_NUM_PC24, 1.0 + std::ldexp(1.0, -24), std::ldexp(1.0, -60)) == 1.0 + std::ldexp(1.0, -23));
	// plain double mode is untouched
	CHECK(luaEA_add(LUA_EA_NUM_DOUBLE, 1.0, std::ldexp(1.0, -24)) == 1.0 + std::ldexp(1.0, -24));
	CHECK(luaEA_div(LUA_EA_NUM_PC24, 1.0, 3.0) == (double)(1.0f / 3.0f));
	CHECK(luaEA_mul(LUA_EA_NUM_PC24, 0.1, 3.0) == (double)(float)(0.1 * 3.0) );
	// integers below 2^24 are exact: script arithmetic on frames and counts is unaffected
	CHECK(luaEA_add(LUA_EA_NUM_PC24, 16777215.0, 1.0) == 16777216.0);
	// 4097 * 4097 = 16785409 needs 25 bits: an exact tie between 16785408 and 16785410, to even gives 16785408
	CHECK(luaEA_mul(LUA_EA_NUM_PC24, 4097.0, 4097.0) == 16785408.0);
	// 4099 * 4099 = 16801801: a tie between 16801800 and 16801802, to even gives 16801800
	CHECK(luaEA_mul(LUA_EA_NUM_PC24, 4099.0, 4099.0) == 16801800.0);
}

TEST_CASE("lua numerics: PC24 arithmetic agrees with the hardware-verified NumericState on random operands")
{
	std::mt19937_64 rng(0x5EED1234ull);
	std::uniform_real_distribution<double> mant(-1.0, 1.0);
	std::uniform_int_distribution<int> expo(-20, 20);
	int checked = 0;
	for (int i = 0; i < 20000; ++i)
	{
		const double a = std::ldexp(mant(rng), expo(rng));
		const double b = std::ldexp(mant(rng), expo(rng));
		if (a == 0.0 || b == 0.0)
		{
			continue;
		}
		// NumericState::pc24*D returns the PC24 result stored as float32; for results in the float range the double is the same value
		CHECK(luaEA_add(LUA_EA_NUM_PC24, a, b) == (double)NumericState::pc24AddD(a, b));
		CHECK(luaEA_sub(LUA_EA_NUM_PC24, a, b) == (double)NumericState::pc24SubD(a, b));
		CHECK(luaEA_mul(LUA_EA_NUM_PC24, a, b) == (double)NumericState::pc24MulD(a, b));
		CHECK(luaEA_div(LUA_EA_NUM_PC24, a, b) == (double)NumericState::pc24DivD(a, b));
		++checked;
	}
	CHECK(checked > 19000);
}

TEST_CASE("lua numerics: the VM uses the numeric mode of its state (S-123: PC24 is assumed, a switch selects plain doubles)")
{
	Fixture f;
	CHECK(lua_ea_getnumericmode(f.rt.state()) == LUA_EA_NUM_PC24);
	CHECK(f.eval("return 1/3") == "number:0.3333333432674408");
	lua_ea_setnumericmode(f.rt.state(), LUA_EA_NUM_DOUBLE);
	CHECK(f.eval("return 1/3") == "number:0.33333333333333331");
}

TEST_CASE("lua numerics: tostring of a number is MSVCR71's printf %.16g: three digit exponents, 1.#INF, 1.#QNAN, -1.#IND")
{
	Fixture f;
	char b[40];
	luaEA_number2str(b, 1e20);
	CHECK(std::string(b) == "1e+020");
	luaEA_number2str(b, 1.5e-7);
	CHECK(std::string(b) == "1.5e-007");
	luaEA_number2str(b, 123456789012345678.0);
	CHECK(std::string(b) == "1.234567890123457e+017");
	luaEA_number2str(b, 100000.0);
	CHECK(std::string(b) == "100000");
	luaEA_number2str(b, -0.5);
	CHECK(std::string(b) == "-0.5");
	luaEA_number2str(b, 1e100);
	CHECK(std::string(b) == "1e+100");
	luaEA_number2str(b, INFINITY);
	CHECK(std::string(b) == "1.#INF");
	luaEA_number2str(b, -INFINITY);
	CHECK(std::string(b) == "-1.#INF");
	CHECK(f.eval("return tostring(1/0)") == "string:1.#INF");
	CHECK(f.eval("return tostring(0/0)") == "string:-1.#IND"); // the x87 default NaN: sign set, empty payload
	CHECK(f.eval("return 7 .. ''") == "string:7");
	CHECK(f.eval("return 0.5 .. ''") == "string:0.5");
}

TEST_CASE("lua numerics: tonumber / string to number is MSVCR71's strtod: no inf, nan or hex; trailing white space ok, trailing junk not")
{
	Fixture f;
	CHECK(f.eval("return tonumber('12')") == "number:12");
	CHECK(f.eval("return tonumber(' 12 ')") == "number:12");
	CHECK(f.eval("return tonumber('1e2')") == "number:100");
	CHECK(f.eval("return tonumber('.5')") == "number:0.5");
	CHECK(f.eval("return tonumber('5.')") == "number:5");
	CHECK(f.eval("return tonumber('-3.25')") == "number:-3.25");
	CHECK(f.eval("return tonumber('inf')") == "nil");
	CHECK(f.eval("return tonumber('nan')") == "nil");
	CHECK(f.eval("return tonumber('0x10')") == "nil");
	CHECK(f.eval("return tonumber('1e')") == "nil");
	CHECK(f.eval("return tonumber('12abc')") == "nil");
	CHECK(f.eval("return tonumber('')") == "nil");
	CHECK(f.eval("return tonumber('1e999')") == "number:inf");
	CHECK(f.eval("return tonumber('1e-999')") == "number:0");
	CHECK(f.eval("return tonumber('ff', 16)") == "number:255");
	CHECK(f.eval("return '10' + 1") == "number:11"); // a string operand is converted by the VM
}

TEST_CASE("lua numerics: number key hash is the low 32 bits of RW _ftol; math.random is the CRT rand of MSVCR71 with seed 1")
{
	CHECK(luaEA_numhash(5.0) == 5ul);
	CHECK(luaEA_numhash(-1.0) == 0xFFFFFFFFul);
	CHECK(luaEA_numhash(4294967296.0 + 7.0) == 7ul);
	CHECK(luaEA_numhash(NAN) == 0ul);
	CHECK(luaEA_numhash(1e30) == 0ul);
	// the classic first outputs of MSVC rand() with the default seed 1
	Fixture f;
	std::vector<int> got;
	for (int i = 0; i < 10; ++i)
	{
		got.push_back(luaEA_rand(f.rt.state()));
	}
	const int expected[10] = { 41, 18467, 6334, 26500, 19169, 15724, 11478, 29358, 26962, 24464 };
	for (int i = 0; i < 10; ++i)
	{
		CHECK(got[(size_t)i] == expected[i]);
	}
	luaEA_srand(f.rt.state(), 1);
	CHECK(luaEA_rand(f.rt.state()) == 41);
	// math.random uses that stream per state and nothing else
	Fixture a, b;
	CHECK(a.eval("return random()") == b.eval("return random()"));
	CHECK(a.eval("randomseed(5) return random(100)") == b.eval("randomseed(5) return random(100)"));
}

// ---------------------------------------------------------------------------------------------------------------------
// the host layer: print, _ALERT, the refused library functions
// ---------------------------------------------------------------------------------------------------------------------
TEST_CASE("lua host: print is gated (retail: an options byte, S-125), errors are swallowed into _ALERT and kept as a diagnostic")
{
	Fixture f;
	f.eval("print('a', 1, nil, true)");
	CHECK(f.rt.printed().empty()); // the gate is closed by default
	f.rt.setPrintEnabled(true);
	f.eval("print('a', 1, nil, true)");
	CHECK(f.rt.printed() == "a\t1\tnil\ttrue\n");
	f.rt.clearAlerts();
	CHECK(run(f.rt, "error('boom')").status == 1);
	REQUIRE(f.rt.alerts().size() == 1);
	CHECK(f.rt.alerts()[0].find("boom") != std::string::npos);
	CHECK(f.rt.alerts()[0].rfind("LUA Alert: ", 0) == 0);
	// a call of an undefined function is a run-time error like any other
	f.rt.clearAlerts();
	CHECK(run(f.rt, "NoSuchFunction(1)").status == 1);
	CHECK(f.rt.alerts().size() == 1);
}

TEST_CASE("lua host: the library functions that reach files, processes, the clock, locale or environment are refused and reported (S-122)")
{
	Fixture f;
	const char *refused[] = { "clock()", "date()", "execute('x')", "exit(1)", "getenv('HOME')", "remove('x')", "rename('a','b')", "setlocale('C')",
		"tmpname()", "readfrom('x')", "writeto('x')", "appendto('x')", "openfile('x','r')", "read()", "write('x')", "seek()", "flush()",
		"closefile(_STDIN)", "dofile('x')", "debug()" };
	for (const char *call : refused)
	{
		f.sink.clear();
		INFO(call);
		CHECK(run(f.rt, call).status == 1);
		CHECK(f.sink.has("S-122"));
	}
	// the standard handles still exist (retail opens them), they just cannot be used
	CHECK(f.eval("return type(_STDIN)") == "string:userdata");
	CHECK(f.eval("return type(_OUTPUT)") == "string:userdata");
	// the pure parts of the libraries work: str, math (degrees), base
	CHECK(f.eval("return strlen('héllo')") == "number:6");
	CHECK(f.eval("return format('%d-%s', 5, 'x')") == "string:5-x");
	CHECK(f.eval("return floor(2.7) .. ',' .. ceil(2.1)") == "string:2,3");
	CHECK(f.eval("return abs(-3)") == "number:3");
	CHECK(f.eval("return sin(90)") == "number:1"); // degrees: stock 4.0.1
	CHECK(f.eval("return strupper('abc')") == "string:ABC");
	CHECK(f.eval("return dostring('return 1+1')") == "number:2");
}

TEST_CASE("lua host: character classes and string comparison do not depend on the host locale")
{
	Fixture f;
	// bytes >= 0x80 are not letters in the C locale of MSVCR71, so they cannot start or continue an identifier
	CHECK(run(f.rt, "local \xE9 = 1").status != 0);
	CHECK(f.eval("return strupper('\xE9')") == "string:\xE9");
	CHECK(f.eval("return 'a' < 'b'") == "boolean:true");
	CHECK(f.eval("return 'B' < 'a'") == "boolean:true"); // strcmp order
}
