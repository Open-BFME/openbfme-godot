// OpenBFME unit tests: lane SDK-2, a mod's Luau scripts in the game: the mount's identity line, the script host's events and lockstep.
// GPL-3.0. Every mod, script and world here is written by the test.

#include "doctest.h"
#include "BigTestUtil.h"
#include "LogicTestUtil.h"

#include "Common/ModMount.h"
#include "Common/Money.h"
#include "Common/Player.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameLogic/Module/LogicModules.h"
#include "GameLogic/Sdk/SdkScriptHost.h"
#include "GameNetwork/ProfileIdentity.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>

using namespace bigtest;
using namespace logictest;
namespace fs = std::filesystem;

namespace
{

struct TempDir
{
	fs::path path;
	TempDir()
	{
		std::mt19937_64 rng((unsigned long long)std::chrono::steady_clock::now().time_since_epoch().count());
		path = fs::temp_directory_path() / ("openbfme-sdk2-" + std::to_string(rng()));
		fs::create_directories(path);
	}
	~TempDir()
	{
		std::error_code ec;
		fs::remove_all(path, ec);
	}
};

void writeFile(const fs::path &p, const std::string &bytes)
{
	fs::create_directories(p.parent_path());
	std::ofstream out(p, std::ios::binary);
	out.write(bytes.data(), (std::streamsize)bytes.size());
}

std::string joined(const std::vector<std::string> &v)
{
	std::string out;
	for (const std::string &s : v)
		out += s + "\n";
	return out;
}

const char kManifest[] = R"({"sdk": 1, "name": "Income", "sim": ["income"], "presentation": ["banner"]})";

// every 30 frames each player gets 100 + a synchronized random 1 .. 10; it counts the objects it saw created and prints at frame 60
const char kIncome[] = R"(
local created = 0
function onGameStart() print("start at frame " .. game.frame() .. ", players " .. game.playerCount()) end
function onObjectCreated(id, template, player) created += 1 end
function onFrame(frame)
	if frame % 30 == 0 then
		for i = 0, game.playerCount() - 1 do game.addMoney(i, 100 + math.random(1, 10)) end
	end
	if frame == 60 then print("frame 60: created " .. created .. ", money " .. game.money(0) .. " / " .. game.money(1)) end
end
)";

struct MountedMod
{
	TempDir tmp;
	Win32BIGFileSystem fsys;
	ModMountReport report;
	explicit MountedMod(const std::map<std::string, std::string> &files)
	{
		const fs::path mod = tmp.path / "IncomeMod";
		for (const auto &f : files)
			writeFile(mod / f.first, f.second);
		std::string error;
		auto base = fsys.openArchiveFromMemory("rotwk/INI.big",
			std::make_shared<const std::vector<std::uint8_t>>(makeBig("BIGF", { { "data\\ini\\a.ini", "retail-a" } })), &error);
		REQUIRE_MESSAGE(base, error);
		fsys.mountArchive(std::move(base), "rotwk/INI.big", false);
		ModOperand op;
		op.dir = mod.u8string() + "/";
		ModMountOptions options;
		options.rotwkInstallRoot = (tmp.path / "install").u8string();
		fs::create_directories(options.rotwkInstallRoot);
		report = MountRetailMod(fsys, op, options);
	}
	std::string sdkLine() const
	{
		for (const std::string &l : report.identity)
			if (l.rfind("sdk ", 0) == 0)
				return l;
		return "";
	}
	std::string content() const
	{
		for (const std::string &l : report.identity)
			if (l.rfind("modcontent ", 0) == 0)
				return l;
		return "";
	}
};

const char kObjects[] =
	"Object Fighter\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"End\n";

struct World : LogicWorld
{
	World()
	{
		REQUIRE_MESSAGE(loadError.empty(), loadError);
		LogicModules::registerAll(w.modules);
		const std::string err = w.load(kObjects);
		REQUIRE_MESSAGE(err.empty(), err);
		logic->settings().bodyThresholdsLoaded = true;
		logic->settings().unitDamagedThreshold = 0.65f;
		logic->settings().unitReallyDamagedThreshold = 0.4f;
	}
};

} // namespace

TEST_CASE("SDK-2: a mod's sim scripts are an identity line naming the mod; its presentation scripts are not in the identity at all")
{
	const std::map<std::string, std::string> files = { { "openbfme/sdk/mod.json", kManifest }, { "openbfme/sdk/sim/income.luau", kIncome },
		{ "openbfme/sdk/presentation/banner.luau", "print('banner')" }, { "data/ini/a.ini", "mod-a" } };
	MountedMod a(files);
	REQUIRE_MESSAGE(a.report.ok, joined(a.report.errors));
	CHECK(a.report.sdkName == "Income");
	CHECK(a.sdkLine() == "sdk sim " + a.report.sdkSimFingerprint);
	CHECK(a.report.sdkSimFingerprint.size() == 64);
	CHECK(a.content().find(" 2 files ") != std::string::npos); // the sim script and a.ini: not the banner, not the manifest (its sim part is the sdk line)

	auto changed = files;
	changed["openbfme/sdk/presentation/banner.luau"] = "print('another banner')";
	changed["openbfme/sdk/presentation/more.luau"] = "x";
	MountedMod b(changed);
	REQUIRE(b.report.ok);
	CHECK(b.report.identity == a.report.identity); // presentation scripts never stop a game or a replay
	auto relisted = files;
	relisted["openbfme/sdk/mod.json"] = R"({"sdk": 1, "name": "Income", "sim": ["income"], "presentation": ["banner", "more"]})";
	relisted["openbfme/sdk/presentation/more.luau"] = "x";
	MountedMod b2(relisted);
	REQUIRE(b2.report.ok);
	CHECK(b2.report.identity == a.report.identity); // nor does the manifest's presentation list (Sol r1)

	changed = files;
	changed["openbfme/sdk/sim/income.luau"] = std::string(kIncome) + "\n-- tuned";
	MountedMod c(changed);
	REQUIRE(c.report.ok);
	CHECK(c.sdkLine() != a.sdkLine());
	const ProfileIdentity pa = ProfileIdentity::compute({}, RandomAlgorithm::ZH_CarryChain, {}, SimulationProfile::Faithful, a.report.identity);
	const ProfileIdentity pc = ProfileIdentity::compute({}, RandomAlgorithm::ZH_CarryChain, {}, SimulationProfile::Faithful, c.report.identity);
	CHECK(pa.digest != pc.digest);
	const std::string why = ProfileIdentity::difference(pa, pc);
	CHECK(why.find("ours: 'sdk sim " + a.report.sdkSimFingerprint + "'") != std::string::npos); // the lobby's / replay's refusal quotes the scripts' line

	MountedMod d({ { "openbfme/sdk/mod.json", R"({"sdk": 1, "name": "Broken", "sim": ["missing"]})" } });
	CHECK_FALSE(d.report.ok);
	CHECK(joined(d.report.errors).find("mod: openbfme/sdk/mod.json: the sim script 'missing' is not in the mod") != std::string::npos);

	MountedMod e({ { "data/ini/a.ini", "no scripts" } });
	REQUIRE(e.report.ok);
	CHECK(e.sdkLine().empty()); // a mod without scripts: the identity of every build before SDK-2
}

TEST_CASE("SDK-2: the sim scripts run in lockstep: two identical games hash alike every frame, and the scripts changed them")
{
	MountedMod mod({ { "openbfme/sdk/mod.json", R"({"sdk": 1, "name": "Income", "sim": ["income"]})" }, { "openbfme/sdk/sim/income.luau", kIncome } });
	REQUIRE_MESSAGE(mod.report.ok, joined(mod.report.errors));
	MountedMod noBanner({ { "openbfme/sdk/mod.json", kManifest }, { "openbfme/sdk/sim/income.luau", kIncome } });
	CHECK_FALSE(noBanner.report.ok); // the manifest lists a presentation script the mod does not have
	CHECK(joined(noBanner.report.errors).find("the presentation script 'banner' is not in the mod") != std::string::npos);
	World a, b, plain;
	SdkScriptHost ha(*a.logic), hb(*b.logic);
	std::string error;
	REQUIRE_MESSAGE(ha.start(mod.fsys.modLooseLayer(), error), error);
	REQUIRE_MESSAGE(hb.start(mod.fsys.modLooseLayer(), error), error);
	CHECK(ha.active());
	for (World *w : { &a, &b, &plain })
	{
		w->make("Fighter", w->teamOf("Alice"));
		w->make("Fighter", w->teamOf("Bob"));
	}
	const std::uint32_t moneyBefore = a.players.findPlayerWithName("Alice")->getMoney()->countMoney();
	for (int frame = 1; frame <= 90; ++frame)
	{
		a.logic->runLogicFrame();
		b.logic->runLogicFrame();
		plain.logic->runLogicFrame();
		REQUIRE(a.logic->computeStateHash() == b.logic->computeStateHash());
		if (frame == 45)
		{
			a.make("Fighter", a.teamOf("Alice")); // the same input on both peers
			b.make("Fighter", b.teamOf("Alice"));
		}
	}
	CHECK(a.logic->computeStateHash() != plain.logic->computeStateHash()); // the scripts did change the game
	CHECK(ha.reports() == hb.reports());
	REQUIRE(ha.reports().size() == 2);
	CHECK(ha.reports()[0] == "SDK Income: start at frame 1, players " + std::to_string(a.players.getPlayerCount()));
	const std::uint32_t alice = a.players.findPlayerWithName("Alice")->getMoney()->countMoney();
	// the three Fighters were all created by frame 60 (two before the first frame, one at frame 45)
	CHECK(ha.reports()[1].rfind("SDK Income: frame 60: created 3, money ", 0) == 0);
	CHECK(alice > moneyBefore + 300); // tolerance: three paydays of 100 + 1 .. 10 (frames 30, 60, 90)
	CHECK(alice <= moneyBefore + 330); // tolerance: as above
	CHECK(ha.calls() == hb.calls());
	CHECK(ha.steps() == hb.steps());
	CHECK_FALSE(ha.stopped());
	// lane SDK-2b: the game saves (its scripts' block: test_sdk2b_saves.cpp)
	std::vector<std::uint8_t> snapshot;
	std::string why;
	CHECK_MESSAGE(a.logic->saveSnapshot(snapshot, &why), why);
}

TEST_CASE("SDK-2: a sim script error stops that mod's scripts at the same frame on every peer, reported with the mod's name")
{
	const char kBoom[] = "function onFrame(f) if f == 20 then error('boom at ' .. f) end end";
	MountedMod mod({ { "openbfme/sdk/mod.json", R"({"sdk": 1, "name": "Boom", "sim": ["boom"]})" }, { "openbfme/sdk/sim/boom.luau", kBoom } });
	REQUIRE_MESSAGE(mod.report.ok, joined(mod.report.errors));
	World a, b;
	SdkScriptHost ha(*a.logic), hb(*b.logic);
	std::string error;
	REQUIRE(ha.start(mod.fsys.modLooseLayer(), error));
	REQUIRE(hb.start(mod.fsys.modLooseLayer(), error));
	for (int frame = 1; frame <= 30; ++frame)
	{
		a.logic->runLogicFrame();
		b.logic->runLogicFrame();
		REQUIRE(a.logic->computeStateHash() == b.logic->computeStateHash());
	}
	CHECK(ha.stopped());
	CHECK(ha.reports() == std::vector<std::string>{ "SDK Boom: openbfme/sdk/sim/boom.luau:1: boom at 20 (frame 20; the mod's scripts are stopped)" });
	CHECK(ha.reports() == hb.reports());

	MountedMod bad({ { "openbfme/sdk/mod.json", R"({"sdk": 1, "name": "Bad", "sim": ["bad"]})" }, { "openbfme/sdk/sim/bad.luau", "this is not luau" } });
	REQUIRE(bad.report.ok);
	World c;
	SdkScriptHost hc(*c.logic);
	CHECK_FALSE(hc.start(bad.fsys.modLooseLayer(), error)); // a script that does not load stops the start
	CHECK(error.rfind("SDK Bad: openbfme/sdk/sim/bad.luau:1:", 0) == 0);
}

TEST_CASE("SDK-2: presentation scripts show banners, may differ between peers, and never change the game")
{
	const char kSim[] = "function onFrame(f) if f % 10 == 0 then game.addMoney(0, 5) end end";
	const char kQuiet[] = "function onGameStart() ui.banner('hello ' .. game.playerName(game.localPlayer())) end";
	// a busy one: its own random draws, work every frame, a banner every 10 frames, and no addMoney (it has none)
	const char kBusy[] = R"(
local t = {}
function onFrame(f)
	for i = 1, 200 do t[i] = math.random() * i end
	if f % 10 == 0 then ui.banner(string.format('frame %d, you have %d', f, game.money(game.localPlayer()))) end
	if f == 25 then print(type(game.addMoney)) end
end)";
	auto mod = [&](const char *presentation) {
		return std::map<std::string, std::string>{ { "openbfme/sdk/mod.json", R"({"sdk": 1, "name": "Look", "sim": ["s"], "presentation": ["p"]})" },
			{ "openbfme/sdk/sim/s.luau", kSim }, { "openbfme/sdk/presentation/p.luau", presentation } };
	};
	MountedMod quiet(mod(kQuiet)), busy(mod(kBusy));
	REQUIRE_MESSAGE(quiet.report.ok, joined(quiet.report.errors));
	REQUIRE(busy.report.ok);
	CHECK(quiet.report.identity == busy.report.identity); // the same game for the lobby
	World a, b;
	a.players.setLocalPlayer(a.players.findPlayerWithName("Alice"));
	b.players.setLocalPlayer(b.players.findPlayerWithName("Bob"));
	SdkScriptHost ha(*a.logic), hb(*b.logic);
	std::string error;
	REQUIRE_MESSAGE(ha.start(quiet.fsys.modLooseLayer(), error), error);
	REQUIRE_MESSAGE(hb.start(busy.fsys.modLooseLayer(), error), error);
	CHECK(ha.presentationActive());
	std::vector<std::string> bannersA, bannersB;
	for (int frame = 1; frame <= 30; ++frame)
	{
		a.logic->runLogicFrame();
		b.logic->runLogicFrame();
		REQUIRE(a.logic->computeStateHash() == b.logic->computeStateHash());
		for (const std::string &s : ha.takeBanners())
			bannersA.push_back(s);
		for (const std::string &s : hb.takeBanners())
			bannersB.push_back(s);
	}
	CHECK(bannersA == std::vector<std::string>{ "hello Alice" });
	const std::uint32_t bobMoney = b.players.findPlayerWithName("Bob")->getMoney()->countMoney();
	CHECK(bannersB == std::vector<std::string>{ "frame 10, you have " + std::to_string(bobMoney), "frame 20, you have " + std::to_string(bobMoney),
						  "frame 30, you have " + std::to_string(bobMoney) });
	CHECK(hb.takeNewReports() == std::vector<std::string>{ "SDK presentation Look: nil" }); // a presentation script cannot pay anyone
	CHECK(ha.calls() == hb.calls()); // only the sim script's calls count
}

TEST_CASE("SDK-2 (Anti-cheat pin 3): a presentation script reaches no logic state: no writes, not the logic's generator, not the state hash")
{
	// every way in it might try: the sim API's write, a write into the read-only game table, a new global named like the sim one, its own
	// math.random (heavily) and a failed script: each is refused or stays in its own state, and the game is the one without it
	const std::vector<std::string> attempts = { "function onFrame(f) game.addMoney(0, 1000) end", "function onFrame(f) game.addMoney = 1 end",
		"function onFrame(f) rawset(game, 'addMoney', print) end", "function onFrame(f) for i = 1, 500 do math.random() end end",
		"function onFrame(f) error('presentation boom') end" };
	for (const std::string &attempt : attempts)
	{
		CAPTURE(attempt);
		MountedMod mod({ { "openbfme/sdk/mod.json", R"({"sdk": 1, "name": "Sneaky", "presentation": ["p"]})" }, { "openbfme/sdk/presentation/p.luau", attempt } });
		REQUIRE_MESSAGE(mod.report.ok, joined(mod.report.errors));
		CHECK(mod.report.sdkSimFingerprint.empty()); // presentation only: the identity of a mod without scripts
		World a, plain;
		SdkScriptHost ha(*a.logic);
		std::string error;
		REQUIRE_MESSAGE(ha.start(mod.fsys.modLooseLayer(), error), error);
		CHECK(ha.presentationActive());
		for (int frame = 1; frame <= 20; ++frame)
		{
			a.logic->runLogicFrame();
			plain.logic->runLogicFrame();
			REQUIRE(a.logic->computeStateHash() == plain.logic->computeStateHash()); // the logic RNG and money included
		}
		CHECK(a.players.findPlayerWithName("Alice")->getMoney()->countMoney() == plain.players.findPlayerWithName("Alice")->getMoney()->countMoney());
		CHECK(ha.calls() == 0); // only sim calls are counted (and hashed)
	}
}

TEST_CASE("SDK-2 (Sol r4): mods that differ only in their manifest name are the same game, and their sim scripts cannot tell them apart")
{
	const char kSim[] = "function onFrame(f) game.addMoney(0, sdk.mod == nil and 100 or 200) end";
	MountedMod a({ { "openbfme/sdk/mod.json", R"({"sdk": 1, "name": "A", "sim": ["s"]})" }, { "openbfme/sdk/sim/s.luau", kSim } });
	MountedMod b({ { "openbfme/sdk/mod.json", R"({"sdk": 1, "name": "B", "sim": ["s"]})" }, { "openbfme/sdk/sim/s.luau", kSim } });
	REQUIRE(a.report.ok);
	REQUIRE(b.report.ok);
	CHECK(a.report.identity == b.report.identity);
	World wa, wb;
	SdkScriptHost ha(*wa.logic), hb(*wb.logic);
	std::string error;
	REQUIRE(ha.start(a.fsys.modLooseLayer(), error));
	REQUIRE(hb.start(b.fsys.modLooseLayer(), error));
	for (int frame = 1; frame <= 10; ++frame)
	{
		wa.logic->runLogicFrame();
		wb.logic->runLogicFrame();
		REQUIRE(wa.logic->computeStateHash() == wb.logic->computeStateHash());
	}
}

TEST_CASE("SDK-2 (Sol r4 follow-up): a presentation script that defines the sim script's globals changes nothing of the sim script")
{
	const char kSim[] = "local n = 0 function onFrame(f) n += 1 if n % 5 == 0 then game.addMoney(0, 10) end end";
	const char kClash[] = "n = 1000 function onFrame(f) n = -1; onFrame = nil end function addMoney() end";
	MountedMod with({ { "openbfme/sdk/mod.json", R"({"sdk": 1, "name": "Clash", "sim": ["s"], "presentation": ["p"]})" },
		{ "openbfme/sdk/sim/s.luau", kSim }, { "openbfme/sdk/presentation/p.luau", kClash } });
	MountedMod without({ { "openbfme/sdk/mod.json", R"({"sdk": 1, "name": "Clash", "sim": ["s"]})" }, { "openbfme/sdk/sim/s.luau", kSim } });
	REQUIRE(with.report.ok);
	REQUIRE(without.report.ok);
	CHECK(with.report.identity == without.report.identity);
	World wa, wb;
	SdkScriptHost ha(*wa.logic), hb(*wb.logic);
	std::string error;
	REQUIRE(ha.start(with.fsys.modLooseLayer(), error));
	REQUIRE(hb.start(without.fsys.modLooseLayer(), error));
	for (int frame = 1; frame <= 20; ++frame)
	{
		wa.logic->runLogicFrame();
		wb.logic->runLogicFrame();
		REQUIRE(wa.logic->computeStateHash() == wb.logic->computeStateHash());
	}
	CHECK(ha.calls() == hb.calls());
}

// ---- lane SDK-2b: sdk.state, the only thing of a script a save keeps ---------------------------------------------------------------------

namespace
{

std::string stateError(const char *fill)
{
	LuauScriptState s(SdkScriptRole::Sim, "State", SdkScriptLimits(), [](const std::string &, std::string &, std::string &e) {
		e = "no files";
		return false;
	});
	std::string error;
	REQUIRE_MESSAGE(s.load("fill", std::string("function fill() ") + fill + " end", error), error);
	REQUIRE_MESSAGE(s.call("fill", {}, error), error);
	std::string bytes;
	CHECK_FALSE(s.serializeState(bytes, error));
	return error;
}

} // namespace

TEST_CASE("SDK-2b: sdk.state has canonical bytes: plain data in key order; a restore gives the same bytes; what it cannot hold is named")
{
	std::string error, bytes;
	{
		LuauScriptState s(SdkScriptRole::Sim, "State", SdkScriptLimits(), [](const std::string &, std::string &, std::string &e) {
			e = "no files";
			return false;
		});
		REQUIRE_MESSAGE(s.load("fill", "function fill() sdk.state.b = true; sdk.state[2] = 'x'; sdk.state.a = { [1] = -0.5 } end", error), error);
		REQUIRE(s.serializeState(bytes, error));
		CHECK(bytes == std::string("T\0\0\0\0", 5)); // empty after the top level
		REQUIRE_MESSAGE(s.call("fill", {}, error), error);
		REQUIRE_MESSAGE(s.serializeState(bytes, error), error);
	}
	// T, 3 pairs: the number key 2 first, then "a" and "b" bytewise
	std::string expected("T\3\0\0\0", 5);
	auto num = [](double d) {
		std::string o = "N";
		std::uint64_t bits;
		std::memcpy(&bits, &d, 8);
		for (int i = 0; i < 8; ++i)
			o += (char)((bits >> (8 * i)) & 0xFF);
		return o;
	};
	auto str = [](const std::string &v) { return std::string("S") + (char)v.size() + std::string(3, '\0') + v; };
	expected += num(2) + str("x") + str("a") + std::string("T\1\0\0\0", 5) + num(1) + num(-0.5) + str("b") + "R";
	CHECK(bytes == expected);
	{
		LuauScriptState t(SdkScriptRole::Sim, "State", SdkScriptLimits(), [](const std::string &, std::string &, std::string &e) {
			e = "no files";
			return false;
		});
		REQUIRE_MESSAGE(t.load("read", "function read() assert(sdk.state.a[1] == -0.5 and sdk.state[2] == 'x' and sdk.state.b) end", error), error);
		REQUIRE_MESSAGE(t.restoreState(bytes, error), error);
		REQUIRE_MESSAGE(t.call("read", {}, error), error);
		std::string again;
		REQUIRE(t.serializeState(again, error));
		CHECK(again == bytes);
		CHECK_FALSE(t.restoreState(bytes, error)); // only into an empty sdk.state
	}
	// damaged bytes are refused, never half read
	CHECK_FALSE(LuauScriptState::checkStateBytes(bytes + "R", error));
	CHECK(error.find("bytes after the end") != std::string::npos);
	CHECK_FALSE(LuauScriptState::checkStateBytes(bytes.substr(0, bytes.size() - 1), error));
	std::string swapped = std::string("T\2\0\0\0", 5) + str("b") + "R" + str("a") + "F";
	CHECK_FALSE(LuauScriptState::checkStateBytes(swapped, error));
	CHECK(error.find("keys out of order") != std::string::npos);
	CHECK_FALSE(LuauScriptState::checkStateBytes(std::string("T\1\0\0\0", 5) + str("n") + num(std::nan("")), error));

	CHECK(stateError("sdk.state.f = print") == "sdk.state.f is a function (sdk.state holds booleans, numbers, strings and tables of them)");
	CHECK(stateError("sdk.state[1.5] = 1") == "sdk.state[1.5]: a key that is not a whole number (sdk.state's keys are strings or whole numbers)");
	CHECK(stateError("sdk.state[true] = 1") == "sdk.state has a boolean key (sdk.state's keys are strings or whole numbers)");
	CHECK(stateError("local t = {} sdk.state.a = t sdk.state.b = t") == "sdk.state.b is a table sdk.state already holds elsewhere (it holds each table once)");
	CHECK(stateError("sdk.state.m = setmetatable({}, {})") == "sdk.state.m has a metatable (a save keeps plain tables only)");
	CHECK(stateError("sdk.state.n = 0/0") == "sdk.state.n is NaN (sdk.state holds numbers that are not NaN)");
	CHECK(stateError("local t = sdk.state for i = 1, 16 do t.x = {} t = t.x end") == "sdk.state.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x is more than 16 tables deep");
	CHECK(stateError("for i = 1, 3000 do sdk.state[i] = string.rep('x', 20) end") == "sdk.state is over 64 KB serialized");
	{
		LuauScriptState s(SdkScriptRole::Sim, "State", SdkScriptLimits(), [](const std::string &, std::string &, std::string &e) {
			e = "no files";
			return false;
		});
		CHECK_FALSE(s.load("top", "sdk.state.n = 1", error)); // a top level reruns on a load
		CHECK(error == "State: top: its top level changed sdk.state (a top level reruns when a save is loaded: change sdk.state from an event, such as "
					   "onGameStart or onGameLoaded)");
	}
	{
		LuauScriptState s(SdkScriptRole::Sim, "State", SdkScriptLimits(), [](const std::string &, std::string &, std::string &e) {
			e = "no files";
			return false;
		});
		CHECK_FALSE(s.load("swap", "function f() sdk.state = {} end f()", error)); // sdk is read-only: the table itself stays
	}
}

namespace
{
// the paydays of kIncome, with what the script must remember kept in sdk.state; onGameLoaded rebuilds its own variable (`label`)
const char kSaved[] = R"(
local label = "fresh"
function onGameStart() sdk.state.paid = 0; sdk.state.days = {} end
function onGameLoaded() label = "loaded"; print("loaded at frame " .. game.frame() .. ", " .. sdk.state.paid .. " paydays") end
function onFrame(frame)
	if frame % 30 == 0 then
		for i = 0, game.playerCount() - 1 do game.addMoney(i, 100 + math.random(1, 10)) end
		sdk.state.paid += 1
		table.insert(sdk.state.days, frame)
	end
	if frame == 100 then print(label .. ": paid " .. sdk.state.paid .. " on " .. table.concat(sdk.state.days, ",")) end
end
)";

std::map<std::string, std::string> savedMod(const char *script)
{
	return { { "openbfme/sdk/mod.json", R"({"sdk": 1, "name": "Saved", "sim": ["s"]})" }, { "openbfme/sdk/sim/s.luau", script } };
}
} // namespace

TEST_CASE("SDK-2b: a game with sim scripts saves; loaded, the scripts restart with sdk.state and the game hashes as the uninterrupted one")
{
	MountedMod mod(savedMod(kSaved));
	REQUIRE_MESSAGE(mod.report.ok, joined(mod.report.errors));
	World a, b, d;
	SdkScriptHost ha(*a.logic), hb(*b.logic), hd(*d.logic);
	std::string error;
	REQUIRE_MESSAGE(ha.start(mod.fsys.modLooseLayer(), error), error);
	REQUIRE_MESSAGE(hb.start(mod.fsys.modLooseLayer(), error), error);
	REQUIRE_MESSAGE(hd.start(mod.fsys.modLooseLayer(), error), error);
	a.make("Fighter", a.teamOf("Alice"));
	for (int frame = 1; frame <= 45; ++frame)
		a.logic->runLogicFrame();
	std::vector<std::uint8_t> saved;
	REQUIRE_MESSAGE(a.logic->saveSnapshot(saved, &error), error);
	const std::uint32_t savedHash = a.logic->computeStateHash();
	std::vector<std::uint32_t> hashes;
	for (int frame = 46; frame <= 120; ++frame)
	{
		a.logic->runLogicFrame();
		hashes.push_back(a.logic->computeStateHash());
	}
	// two other games (the same mod; two peers of a loaded multiplayer game) load the save: the same hash, then frame by frame in lockstep with
	// each other and with the uninterrupted game
	REQUIRE_MESSAGE(b.logic->loadSnapshot(saved, &error), error);
	REQUIRE_MESSAGE(d.logic->loadSnapshot(saved, &error), error);
	CHECK(b.logic->computeStateHash() == savedHash);
	CHECK(d.logic->computeStateHash() == savedHash);
	std::vector<std::uint8_t> again;
	REQUIRE_MESSAGE(b.logic->saveSnapshot(again, &error), error);
	CHECK(again == saved);
	for (size_t i = 0; i < hashes.size(); ++i)
	{
		b.logic->runLogicFrame();
		d.logic->runLogicFrame();
		REQUIRE(b.logic->computeStateHash() == hashes[i]);
		REQUIRE(d.logic->computeStateHash() == hashes[i]);
	}
	CHECK(hd.reports() == hb.reports());
	CHECK(hd.stateBytes() == hb.stateBytes());
	CHECK(ha.reports() == std::vector<std::string>{ "SDK Saved: fresh: paid 3 on 30,60,90" });
	CHECK(hb.reports() == std::vector<std::string>{ "SDK Saved: loaded at frame 46, 1 paydays", "SDK Saved: loaded: paid 3 on 30,60,90" });
	CHECK(ha.calls() == hb.calls()); // onGameLoaded is the load's: not counted
	CHECK(ha.stateBytes() == hb.stateBytes());

	// a game whose mod has other sim scripts refuses the save, naming both
	MountedMod tuned(savedMod((std::string(kSaved) + "\n-- tuned").c_str()));
	World c;
	SdkScriptHost hc(*c.logic);
	REQUIRE_MESSAGE(hc.start(tuned.fsys.modLooseLayer(), error), error);
	CHECK_FALSE(c.logic->loadSnapshot(saved, &error));
	CHECK(error.find("obfme:SdkScripts: the save's mod scripts ('" + ha.simFingerprint() + "') are not the loaded mod's ('" + hc.simFingerprint() +
				"', the mod Saved): load the save with the mod it was made with") != std::string::npos);
}

TEST_CASE("SDK-2b: a top level may not change the game (it reruns on a load): game.addMoney, math.random and sdk.state there are errors")
{
	struct Case
	{
		const char *script, *error;
	};
	const Case cases[] = {
		{ "game.addMoney(0, 100)", "game.addMoney: called from a script's top level, which runs again when a save is loaded" },
		{ "local r = math.random(1, 6)", "math.random: called from a script's top level, which runs again when a save is loaded" },
		{ "sdk.state.started = true", "its top level changed sdk.state" },
	};
	for (const Case &c : cases)
	{
		CAPTURE(c.script);
		MountedMod mod(savedMod(c.script));
		REQUIRE(mod.report.ok);
		World w;
		SdkScriptHost h(*w.logic);
		std::string error;
		CHECK_FALSE(h.start(mod.fsys.modLooseLayer(), error)); // the game does not start, the same on every peer
		CHECK(error.find(c.error) != std::string::npos);
	}
	// inside an event it is the game's: a module required there keeps the rule for its own top level
	MountedMod lazy({ { "openbfme/sdk/mod.json", R"({"sdk": 1, "name": "Lazy", "sim": ["s"]})" },
		{ "openbfme/sdk/sim/s.luau", "function onFrame(f) if f == 3 then require('pay') end end" },
		{ "openbfme/sdk/sim/pay.luau", "game.addMoney(0, 5)" } });
	REQUIRE(lazy.report.ok);
	World w;
	SdkScriptHost h(*w.logic);
	std::string error;
	REQUIRE_MESSAGE(h.start(lazy.fsys.modLooseLayer(), error), error);
	for (int frame = 1; frame <= 5; ++frame)
		w.logic->runLogicFrame();
	CHECK(h.stopped());
	REQUIRE(h.reports().size() == 1);
	CHECK(h.reports()[0].find("game.addMoney: called from a script's top level") != std::string::npos);
	CHECK(h.reports()[0].find("(frame 3; the mod's scripts are stopped)") != std::string::npos);
}

TEST_CASE("SDK-2b: sdk.state that cannot be saved stops the scripts at that frame on every peer, named")
{
	MountedMod mod(savedMod("function onFrame(f) if f == 5 then sdk.state.cb = print end end"));
	REQUIRE(mod.report.ok);
	World a, b;
	SdkScriptHost ha(*a.logic), hb(*b.logic);
	std::string error;
	REQUIRE(ha.start(mod.fsys.modLooseLayer(), error));
	REQUIRE(hb.start(mod.fsys.modLooseLayer(), error));
	for (int frame = 1; frame <= 8; ++frame)
	{
		a.logic->runLogicFrame();
		b.logic->runLogicFrame();
		REQUIRE(a.logic->computeStateHash() == b.logic->computeStateHash());
	}
	CHECK(ha.stopped());
	CHECK(ha.reports() == std::vector<std::string>{ "SDK Saved: sdk.state.cb is a function (sdk.state holds booleans, numbers, strings and tables of "
													 "them) (frame 5; the mod's scripts are stopped)" });
	CHECK(ha.reports() == hb.reports());
}

TEST_CASE("SDK-2b (Sol r1): a loaded game iterates sdk.state (pairs) in the uninterrupted game's order")
{
	// keys put in out of order, some removed and put back: the running table's slots are not in key order
	const char kOrder[] = R"(
function onGameStart()
	sdk.state.m = {}
	for i = 30, 1, -1 do sdk.state.m["k" .. i] = i end
	for i = 1, 30, 3 do sdk.state.m["k" .. i] = nil end
	sdk.state.m.c = 1; sdk.state.m.a = 2; sdk.state.m[7] = 3; sdk.state.m[2] = 4
	for i = 1, 30, 3 do sdk.state.m["k" .. i] = -i end
end
function onFrame(frame)
	if frame == 50 then sdk.state.m.zz = 5; sdk.state.m.a = nil end
	if frame == 60 or frame == 40 then
		local s = {}
		for k, v in pairs(sdk.state.m) do table.insert(s, tostring(k)) end
		print(frame .. ": " .. table.concat(s, ","))
	end
end
)";
	MountedMod mod(savedMod(kOrder));
	REQUIRE_MESSAGE(mod.report.ok, joined(mod.report.errors));
	World a, b;
	SdkScriptHost ha(*a.logic), hb(*b.logic);
	std::string error;
	REQUIRE_MESSAGE(ha.start(mod.fsys.modLooseLayer(), error), error);
	REQUIRE_MESSAGE(hb.start(mod.fsys.modLooseLayer(), error), error);
	for (int frame = 1; frame <= 45; ++frame)
		a.logic->runLogicFrame();
	std::vector<std::uint8_t> saved;
	REQUIRE_MESSAGE(a.logic->saveSnapshot(saved, &error), error);
	for (int frame = 46; frame <= 70; ++frame)
		a.logic->runLogicFrame();
	REQUIRE_MESSAGE(b.logic->loadSnapshot(saved, &error), error);
	for (int frame = 46; frame <= 70; ++frame)
		b.logic->runLogicFrame();
	REQUIRE(ha.reports().size() == 2);
	REQUIRE(hb.reports().size() == 1);
	CHECK(hb.reports()[0] == ha.reports()[1]); // frame 60, after a change past the save
	CHECK(a.logic->computeStateHash() == b.logic->computeStateHash());
	MESSAGE(ha.reports()[0] << "\n" << ha.reports()[1]);
}

TEST_CASE("SDK-2b (Sol r2): sdk.state's upkeep that meets the memory cap is the script's error, never the game's")
{
	// a state close to its cap: the per-frame refill (canonicalizeState) needs memory; over the cap it fails, reported, and nothing throws
	SdkScriptLimits limits;
	limits.memoryBytes = 2u << 20;
	int refused = 0, done = 0;
	for (int kb = 200; kb <= 1950; kb += 6)
	{
		CAPTURE(kb);
		LuauScriptState s(SdkScriptRole::Sim, "Heavy", limits, [](const std::string &, std::string &, std::string &e) {
			e = "no files";
			return false;
		});
		std::string error;
		REQUIRE_MESSAGE(s.load("fill", "function fill(kb) keep = buffer.create(kb * 1024); sdk.state.m = {}; for i = 1, 2500 do sdk.state.m['k' .. i] = i end end",
							error),
			error);
		if (!s.callNumbers("fill", { (double)kb }, error))
			continue; // the script itself met the cap
		std::string bytes;
		if (!s.serializeState(bytes, error))
			continue;
		if (s.canonicalizeState(error))
			++done;
		else
		{
			++refused;
			CHECK(error == "sdk.state: not enough memory (the script's memory cap)");
			CHECK(s.failed());
		}
	}
	MESSAGE(done << " refilled, " << refused << " met the cap");
	CHECK(done > 0); // tolerance: the sweep only has to meet both outcomes; where the cap falls depends on the allocator's block sizes
	CHECK(refused > 0); // tolerance: as above
}
