// OpenBFME unit tests: lane SDK-2, the sandboxed Luau script state (GameLogic/Sdk/LuauScriptState.h). GPL-3.0.
// Every script here is written by the test.

#include "doctest.h"

#include "Common/DeterministicLibm.h"
#include "GameLogic/Sdk/LuauScriptState.h"
#include "GameLogic/Sdk/SdkManifest.h"

#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace
{

// a mod folder in memory: path -> bytes
struct ModFiles
{
	std::map<std::string, std::string> files;
	std::vector<std::string> asked;
	SdkLister lister()
	{
		return [this](const std::string &folder, std::vector<std::string> &paths, std::string &) {
			for (const auto &f : files)
				if (f.first.rfind(folder, 0) == 0)
					paths.push_back(f.first);
			return true;
		};
	}
	LuauScriptState::Reader reader()
	{
		return [this](const std::string &path, std::string &out, std::string &error) {
			asked.push_back(path);
			auto it = files.find(path);
			if (it == files.end())
			{
				error = "no file " + path + " in the mod";
				return false;
			}
			out = it->second;
			return true;
		};
	}
};

std::string run(LuauScriptState &s, const std::string &source)
{
	std::string error;
	if (!s.load("test.luau", source, error))
		return "<" + error + ">";
	return s.printed().empty() ? std::string() : s.printed().back();
}

std::uint64_t bits(double d)
{
	std::uint64_t b;
	std::memcpy(&b, &d, sizeof b);
	return b;
}

} // namespace

TEST_CASE("SDK-2: a script state has no os, io, debug, environment, loader or collector access; the globals are read-only")
{
	for (SdkScriptRole role : { SdkScriptRole::Sim, SdkScriptRole::Presentation })
	{
		CAPTURE(int(role));
		ModFiles mod;
		LuauScriptState s(role, "TestMod", SdkScriptLimits(), mod.reader());
		CHECK(run(s, "print(type(os), type(io), type(debug), type(getfenv), type(setfenv), type(newproxy), type(gcinfo), type(collectgarbage), "
					 "type(loadstring), type(load), type(dofile))") == "nil\tnil\tnil\tnil\tnil\tnil\tnil\tnil\tnil\tnil\tnil");
		// Sol r4: a sim script does not see the manifest's name (it is not in the sim fingerprint)
		CHECK(run(s, "print(sdk.mod, sdk.role)") == (role == SdkScriptRole::Sim ? std::string("nil\tsim") : std::string("TestMod\tpresentation")));
		LuauScriptState t(role, "TestMod", SdkScriptLimits(), mod.reader());
		CHECK(run(t, "math.sin = function() return 0 end").find("attempt to modify a readonly table") != std::string::npos);
		CHECK(t.failed());
		std::string error;
		CHECK_FALSE(t.load("again.luau", "print(1)", error)); // a failed state refuses everything after
		CHECK(error == t.failure());
	}
}

TEST_CASE("SDK-2: require and sdk.read reach only the mod's own files")
{
	ModFiles mod;
	mod.files["openbfme/sdk/sim/util.luau"] = "print('util loaded') return { twice = function(x) return 2 * x end }";
	mod.files["openbfme/sdk/sim/a.luau"] = "return require('b')";
	mod.files["openbfme/sdk/sim/b.luau"] = "return require('a')";
	mod.files["data/names.txt"] = "Gondor\nRohan";
	LuauScriptState s(SdkScriptRole::Sim, "TestMod", SdkScriptLimits(), mod.reader());
	CHECK(run(s, "local u = require('util') local v = require('util') print(u.twice(21), u == v)") == "42\ttrue");
	CHECK(s.printed().size() == 2); // the module ran once
	CHECK(run(s, "print(sdk.read('data/names.txt'))") == "Gondor\nRohan");
	for (const char *bad : { "../secret", "/etc/passwd", "C:/x", "a\\\\b", "x/../../y", "./x", "" })
	{
		CAPTURE(bad);
		LuauScriptState t(SdkScriptRole::Sim, "TestMod", SdkScriptLimits(), mod.reader());
		CHECK(run(t, std::string("sdk.read('") + bad + "')").find("is not a path inside the mod") != std::string::npos);
		LuauScriptState u(SdkScriptRole::Presentation, "TestMod", SdkScriptLimits(), mod.reader());
		CHECK(run(u, std::string("require('") + bad + "')").find("is not a module of the mod") != std::string::npos);
	}
	CHECK(mod.asked == std::vector<std::string>{ "openbfme/sdk/sim/util.luau", "data/names.txt" }); // nothing outside was asked for
	LuauScriptState c(SdkScriptRole::Sim, "TestMod", SdkScriptLimits(), mod.reader());
	CHECK(run(c, "require('a')").find("require cycle: a -> b -> a") != std::string::npos);
	LuauScriptState m(SdkScriptRole::Sim, "TestMod", SdkScriptLimits(), mod.reader());
	CHECK(run(m, "require('missing')").find("require 'missing': no file openbfme/sdk/sim/missing.luau in the mod") != std::string::npos);
}

TEST_CASE("SDK-2: the step budget stops a runaway script at the same step every time; the memory cap stops a hungry one")
{
	SdkScriptLimits limits;
	limits.stepsPerCall = 10000;
	std::uint64_t steps[2] = {};
	for (int i = 0; i < 2; ++i)
	{
		LuauScriptState s(SdkScriptRole::Sim, "TestMod", limits, nullptr);
		CHECK(run(s, "local n = 0 while true do n += 1 end").find("ran longer than its step budget (10000 VM safepoints in one call)") !=
			std::string::npos);
		steps[i] = s.stepsLastCall();
	}
	CHECK(steps[0] == steps[1]);

	LuauScriptState ok(SdkScriptRole::Sim, "TestMod", limits, nullptr);
	CHECK(run(ok, "function tick(n) local s = 0 for i = 1, n do s += i end print(s) end") == "");
	std::string error;
	REQUIRE_MESSAGE(ok.callNumbers("tick", { 100 }, error), error);
	CHECK(ok.printed().back() == "5050");
	const std::uint64_t small = ok.stepsLastCall();
	REQUIRE_MESSAGE(ok.callNumbers("tick", { 1000 }, error), error);
	CHECK(ok.stepsLastCall() > small); // tolerance: the count is per call, more work counts more
	CHECK_FALSE(ok.callNumbers("tick", { 100000 }, error));
	CHECK(error.find("step budget") != std::string::npos);
	CHECK_FALSE(ok.call("untick", {}, error)); // a failed state refuses further calls
	CHECK(error == ok.failure());

	SdkScriptLimits mem;
	mem.memoryBytes = 4u << 20;
	LuauScriptState hungry(SdkScriptRole::Sim, "TestMod", mem, nullptr);
	CHECK(run(hungry, "local t = {} for i = 1, 1e7 do t[i] = string.rep('x', 64) .. i end").find("not enough memory") != std::string::npos);
	CHECK(hungry.bytesInUse() <= mem.memoryBytes); // tolerance: the cap is the property under test (the bytes left after the failed allocation vary with what was freed)
}

TEST_CASE("SDK-2: a sim script runs lockstep-safe: no address keys, no weak tables, no addresses shown; a presentation script may")
{
	LuauScriptState sim(SdkScriptRole::Sim, "TestMod", SdkScriptLimits(), nullptr);
	CHECK(run(sim, "local t = {} t[{}] = 1").find("a table as a table key is not lockstep-safe in a sim script") != std::string::npos);
	LuauScriptState sim2(SdkScriptRole::Sim, "TestMod", SdkScriptLimits(), nullptr);
	CHECK(run(sim2, "local t = {} t[print] = 1").find("a function as a table key is not lockstep-safe") != std::string::npos);
	LuauScriptState sim3(SdkScriptRole::Sim, "TestMod", SdkScriptLimits(), nullptr);
	CHECK(run(sim3, "setmetatable({}, { __mode = 'k' })").find("weak tables (__mode) are not lockstep-safe") != std::string::npos);
	LuauScriptState sim3b(SdkScriptRole::Sim, "TestMod", SdkScriptLimits(), nullptr);
	CHECK(run(sim3b, "local mt = {} setmetatable({}, mt) mt.__mode = 'k'").find("attempt to modify a readonly table") != std::string::npos);
	LuauScriptState sim3c(SdkScriptRole::Sim, "TestMod", SdkScriptLimits(), nullptr);
	CHECK(run(sim3c, "local mt = { __index = function(t, k) return k .. '!' end } local t = setmetatable({}, mt) print(t.hello, mt.__mode)") ==
		"hello!\tnil"); // an ordinary metatable works
	LuauScriptState sim4(SdkScriptRole::Sim, "TestMod", SdkScriptLimits(), nullptr);
	CHECK(run(sim4, "local t = { [1] = 'a', x = 'b', [true] = 'c', [2.5] = 'd' } local n = 0 for _ in t do n += 1 end print(n, tostring({}))") ==
		"4\ttable: 0x0000000000000000");

	LuauScriptState pres(SdkScriptRole::Presentation, "TestMod", SdkScriptLimits(), nullptr);
	CHECK(run(pres, "local t = {} t[{}] = 1 local w = setmetatable({}, { __mode = 'k' }) print('ok')") == "ok");
	CHECK(run(pres, "print(tostring({}) ~= 'table: 0x0000000000000000')") == "true");
}

TEST_CASE("SDK-2: a sim script's maths is the deterministic libm's, also in hot loops and constant expressions; math.random is the game's")
{
	LuauScriptState s(SdkScriptRole::Sim, "TestMod", SdkScriptLimits(), nullptr);
	// %.17g prints a double exactly; the loop runs each call often enough for any fast path the compiler might pick
	REQUIRE(run(s, "function probe(x) local r for i = 1, 50 do r = string.format('%.17g %.17g %.17g %.17g %.17g %.17g', math.sin(x), math.cos(x), math.exp(x), "
				   "math.log(x), math.pow(x, 1.7), x ^ 1.3) end print(r) end") == "");
	for (double x : { 0.5, 1.25, 3.0, 10.0 })
	{
		std::string error;
		REQUIRE_MESSAGE(s.callNumbers("probe", { x }, error), error);
		char want[256];
		std::snprintf(want, sizeof want, "%.17g %.17g %.17g %.17g %.17g %.17g", DetLibm::sin(x), DetLibm::cos(x), DetLibm::exp(x), DetLibm::log(x),
			DetLibm::pow(x, 1.7), DetLibm::pow(x, 1.3));
		CHECK(s.printed().back() == want);
	}
	CHECK(run(s, "print(string.format('%.17g', 2 ^ 0.5))") == [] {
		char b[64];
		std::snprintf(b, sizeof b, "%.17g", DetLibm::pow(2.0, 0.5));
		return std::string(b);
	}()); // the compiler folds 2 ^ 0.5 with the same pow

	std::uint32_t next = 7;
	LuauScriptState r(SdkScriptRole::Sim, "TestMod", SdkScriptLimits(), nullptr, [&next] { return next += 0x9E3779B9u; });
	CHECK(run(r, "print(math.random(1, 6), math.random(10), math.random())") ==
		"4\t3\t0.8541019675321877"); // from the host's generator: 0x9E3779C0, 0x3C6EF379, 0xDAA66D32
	LuauScriptState r2(SdkScriptRole::Sim, "TestMod", SdkScriptLimits(), nullptr);
	CHECK(run(r2, "math.randomseed(1)").find("math.randomseed is not available to a sim script") != std::string::npos);
	(void)bits;
}

TEST_CASE("SDK-2: the manifest is strict and names every problem")
{
	SdkManifest m;
	std::string error;
	REQUIRE_MESSAGE(SdkManifest::parse(R"({"sdk": 1, "name": "Income", "sim": ["income", "util/tax"], "presentation": ["banner"]})", m, error), error);
	CHECK(m.name == "Income");
	CHECK(m.sim == std::vector<std::string>{ "income", "util/tax" });
	CHECK(m.presentation == std::vector<std::string>{ "banner" });
	CHECK(SdkManifest::scriptPath(SdkScriptRole::Sim, "util/tax") == "openbfme/sdk/sim/util/tax.luau");
	REQUIRE(SdkManifest::parse(R"({"sdk": 1, "name": "Empty"})", m, error));
	CHECK(m.sim.empty());
	const std::vector<std::pair<std::string, std::string>> bad = {
		{ "{", "openbfme/sdk/mod.json: not valid JSON: " },
		{ R"({"sdk": 2, "name": "X"})", "openbfme/sdk/mod.json: \"sdk\" must be 1 (the SDK version this OpenBFME runs)" },
		{ R"({"sdk": 1})", "openbfme/sdk/mod.json: \"name\" must be the mod's name" },
		{ R"({"sdk": 1, "name": "X", "scripts": []})", "openbfme/sdk/mod.json: unknown key \"scripts\" (known: sdk, name, sim, presentation)" },
		{ R"({"sdk": 1, "name": "X", "sim": ["../evil"]})", "openbfme/sdk/mod.json: \"sim\" holds '../evil', not a script name" },
		{ R"({"sdk": 1, "name": "X", "sim": ["a.luau"]})", "openbfme/sdk/mod.json: \"sim\" holds 'a.luau', not a script name" },
		{ R"({"sdk": 1, "name": "X", "presentation": ["a", "a"]})", "openbfme/sdk/mod.json: \"presentation\" lists 'a' twice" },
		{ R"({"sdk": 1, "name": "X", "sim": "a"})", "openbfme/sdk/mod.json: \"sim\" must be a list of script names" },
	};
	for (const auto &b : bad)
	{
		CAPTURE(b.first);
		CHECK_FALSE(SdkManifest::parse(b.first, m, error));
		CHECK(error.rfind(b.second, 0) == 0);
	}
}

TEST_CASE("SDK-2: the sim fingerprint covers every sim file and entry, and no presentation file")
{
	ModFiles mod;
	mod.files["openbfme/sdk/sim/income.luau"] = "-- income";
	mod.files["openbfme/sdk/sim/util/tax.luau"] = "return 1";
	mod.files["openbfme/sdk/presentation/banner.luau"] = "print('hi')";
	SdkManifest m;
	std::string error, a, b;
	REQUIRE(SdkManifest::parse(R"({"sdk": 1, "name": "Income", "sim": ["income"], "presentation": ["banner"]})", m, error));
	REQUIRE_MESSAGE(SdkSimFingerprint(m, mod.reader(), mod.lister(), a, error), error);
	CHECK(a.size() == 64);
	mod.files["openbfme/sdk/presentation/banner.luau"] = "print('changed')"; // presentation: not part of it
	mod.files["openbfme/sdk/presentation/extra.luau"] = "x";
	REQUIRE(SdkSimFingerprint(m, mod.reader(), mod.lister(), b, error));
	CHECK(a == b);
	mod.files["openbfme/sdk/sim/util/tax.luau"] = "return 2"; // a module only required, not listed: part of it
	REQUIRE(SdkSimFingerprint(m, mod.reader(), mod.lister(), b, error));
	CHECK(a != b);
	mod.files["openbfme/sdk/sim/util/tax.luau"] = "return 1";
	SdkManifest m2;
	REQUIRE(SdkManifest::parse(R"({"sdk": 1, "name": "Income", "sim": ["income", "util/tax"], "presentation": ["banner"]})", m2, error));
	REQUIRE(SdkSimFingerprint(m2, mod.reader(), mod.lister(), b, error));
	CHECK(a != b); // the entries (what runs) are part of it
	SdkManifest none;
	REQUIRE(SdkManifest::parse(R"({"sdk": 1, "name": "Look", "presentation": ["banner"]})", none, error));
	ModFiles look;
	look.files["openbfme/sdk/presentation/banner.luau"] = "print('hi')";
	REQUIRE(SdkSimFingerprint(none, look.reader(), look.lister(), b, error));
	CHECK(b.empty()); // a presentation-only mod changes nothing peers must share
	SdkManifest missing;
	REQUIRE(SdkManifest::parse(R"({"sdk": 1, "name": "X", "sim": ["nope"]})", missing, error));
	CHECK_FALSE(SdkSimFingerprint(missing, mod.reader(), mod.lister(), b, error));
	CHECK(error == "openbfme/sdk/mod.json: the sim script 'nope' is not in the mod (openbfme/sdk/sim/nope.luau)");
}

TEST_CASE("SDK-2 (Sol r1): the step budget cannot be caught; output has its own budget; a sim script cannot read presentation files")
{
	SdkScriptLimits limits;
	limits.stepsPerCall = 5000;
	for (const char *escape : { "while true do pcall(function() while true do end end) end",
			 "while true do local co = coroutine.create(function() while true do end end) coroutine.resume(co) end",
			 "local ok = xpcall(function() while true do end end, function(e) while true do end end) while true do end" })
	{
		CAPTURE(escape);
		LuauScriptState s(SdkScriptRole::Sim, "TestMod", limits, nullptr);
		CHECK(run(s, escape).find("step budget") != std::string::npos);
		CHECK(s.failed());
	}
	LuauScriptState caughtThenReturns(SdkScriptRole::Sim, "TestMod", limits, nullptr);
	CHECK(run(caughtThenReturns, "pcall(function() while true do end end)").find("step budget") != std::string::npos);

	LuauScriptState chatty(SdkScriptRole::Presentation, "TestMod", SdkScriptLimits(), nullptr);
	CHECK(run(chatty, "for i = 1, 100000 do print(string.rep('x', 1000)) end").find("printed more than its output budget") != std::string::npos);
	size_t bytes = 0;
	for (const std::string &l : chatty.printed())
		bytes += l.size() + 1;
	CHECK(bytes <= LuauScriptState::kPrintPerCall); // tolerance: the budget is the property under test

	ModFiles mod;
	mod.files["openbfme/sdk/presentation/banner.luau"] = "secret";
	mod.files["data/x.txt"] = "fine";
	for (const char *path : { "openbfme/sdk/presentation/banner.luau", "OpenBFME/SDK/Presentation/Banner.luau" })
	{
		CAPTURE(path);
		LuauScriptState sim(SdkScriptRole::Sim, "TestMod", SdkScriptLimits(), mod.reader());
		CHECK(run(sim, std::string("print(sdk.read('") + path + "'))").find("a sim script cannot read") != std::string::npos);
	}
	mod.files["openbfme/sdk/mod.json"] = "{}";
	LuauScriptState manifestReader(SdkScriptRole::Sim, "TestMod", SdkScriptLimits(), mod.reader());
	CHECK(run(manifestReader, "print(sdk.read('OpenBFME/sdk/MOD.json'))").find("a sim script cannot read") != std::string::npos); // Sol r2
	LuauScriptState sim(SdkScriptRole::Sim, "TestMod", SdkScriptLimits(), mod.reader());
	CHECK(run(sim, "print(sdk.read('data/x.txt'))") == "fine");
	LuauScriptState empty(SdkScriptRole::Presentation, "TestMod", SdkScriptLimits(), nullptr);
	CHECK(run(empty, "for i = 1, 1000000 do print() end").find("printed more than its output budget") != std::string::npos); // Sol r2
	CHECK(empty.printed().size() <= LuauScriptState::kPrintPerCall / LuauScriptState::kPrintLineCost); // tolerance: the budget is the property
	LuauScriptState pres(SdkScriptRole::Presentation, "TestMod", SdkScriptLimits(), mod.reader());
	CHECK(run(pres, "print(sdk.read('openbfme/sdk/presentation/banner.luau'))") == "secret");
}

TEST_CASE("SDK-2 (Sol r1): a sim script's vector.angle is DetLibm's atan2 of Luau's float cross product")
{
	LuauScriptState s(SdkScriptRole::Sim, "TestMod", SdkScriptLimits(), nullptr);
	REQUIRE(run(s, "function probe() local r for i = 1, 20 do r = string.format('%.17g %.17g', vector.angle(vector.create(1, 2, 3), vector.create(-2, 0.5, 4)), "
				   "vector.angle(vector.create(1, 0, 0), vector.create(0, 1, 0), vector.create(0, 0, -1))) end print(r) end") == "");
	std::string error;
	REQUIRE_MESSAGE(s.call("probe", {}, error), error);
	const float a[] = { 1, 2, 3 }, b[] = { -2, 0.5f, 4 }, x[] = { 1, 0, 0 }, y[] = { 0, 1, 0 }, z[] = { 0, 0, -1 };
	const float c[] = { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
	const double first = DetLibm::atan2(DetLibm::sqrt(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]), a[0] * b[0] + a[1] * b[1] + a[2] * b[2]);
	char want[128];
	std::snprintf(want, sizeof want, "%.17g %.17g", first, -DetLibm::atan2(1.0, 0.0)); // x cross y = +z, against the axis -z: negative
	(void)x; (void)y; (void)z;
	CHECK(s.printed().back() == want);
}
