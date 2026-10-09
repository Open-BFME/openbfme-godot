// OpenBFME unit tests: the drawable Lua state and the real Lua behind the draw layer's script host (lane LUA-1; spec lua-scripting.md 3.3, 4.7).
// Expected values: the retail disassembly of the 21 drawable functions (RW 0x734xxx-0x735xxx, cited per test), the retail BeginScript text,
// and for the corpus two independent counts (this scan over the INI layer's lines, a Python scan over the archive text).

#include "doctest.h"

#include "LuaTestUtil.h"
#include "RetailTestMount.h"
#include "W3DDrawRetail.h"
#include "W3DDrawTestUtil.h"

#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DLuaDrawScriptHost.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DScriptedModelDraw.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameLogic/ScriptEngine/LuaDrawableState.h"
#include "Libraries/file/TextFile.h"

#include <deque>
#include <regex>

using luatest::run;

namespace
{
class RecCtx : public LuaDrawableContext
{
public:
	std::vector<std::string> calls;
	std::string prevState, prevAnim;
	float fraction = 0.25f;
	std::set<std::string> conditions, statuses, modules;
	int kindOf = 1;
	bool haveTarget = true;
	std::string previousAnimationState() override { return prevState; }
	std::string previousAnimation() override { return prevAnim; }
	float previousAnimFraction() override { return fraction; }
	void setTransitionAnimState(const std::string &n) override { calls.push_back("transition(" + n + ")"); }
	void allowToContinue() override { calls.push_back("allow"); }
	bool modelCondition(const std::string &n) override { return conditions.count(n) != 0; }
	bool objectStatus(const std::string &n) override { return statuses.count(n) != 0; }
	bool hasModule(const std::string &n) override { return modules.count(n) != 0; }
	bool showModule(const std::string &n, bool v, bool p) override
	{
		calls.push_back(std::string("module(") + n + "," + (v ? "1" : "0") + "," + (p ? "1" : "0") + ")");
		return modules.count(n) != 0;
	}
	void showSubObject(const std::string &n, bool v, bool p) override { calls.push_back(std::string("sub(") + n + "," + (v ? "1" : "0") + "," + (p ? "1" : "0") + ")"); }
	void playSound(const std::string &n) override { calls.push_back("sound(" + n + ")"); }
	bool targetDistance(double *o) override { if (!haveTarget) return false; *o = 12.5; return true; }
	bool targetHeight(double *o) override { if (!haveTarget) return false; *o = -3.0; return true; }
	bool targetBearing(double *o) override { if (!haveTarget) return false; *o = 90.0; return true; }
	int targetKindOf(const std::string &) override { return kindOf; }
	float clientRandomReal(float lo, float hi) override { calls.push_back("random"); return (lo + hi) * 0.5f; }
};

struct Draw
{
	LuaReportSink sink;
	std::uint32_t frame = 41;
	bool audio = true;
	LuaDrawableState state{ sink, [this] { return frame; }, [this] { return audio; } };
	RecCtx ctx;
	std::string result;
	int status = 0;
	std::string run(const std::string &script)
	{
		ctx.calls.clear();
		status = state.run(script, "TestObject", &ctx, &result);
		return result;
	}
};
} // namespace

TEST_CASE("drawable: Show / Hide SubObject try a draw module of that tag first (RW 0x734EF4); the permanent forms and Show / HideModule do not (RW 0x73509E, 0x73501F, 0x73511D)")
{
	Draw d;
	d.ctx.modules.insert("Mod");
	d.run("CurDrawableHideSubObject('Part') CurDrawableShowSubObject('Part') CurDrawableHideSubObject('Mod') CurDrawableShowSubObject('Mod')");
	CHECK(d.status == 0);
	CHECK(d.ctx.calls == (std::vector<std::string>{ "sub(Part,0,0)", "sub(Part,1,0)", "module(Mod,0,0)", "module(Mod,1,0)" }));
	d.run("CurDrawableHideSubObjectPermanently('Mod') CurDrawableShowSubObjectPermanently('Part')");
	CHECK(d.ctx.calls == (std::vector<std::string>{ "sub(Mod,0,1)", "sub(Part,1,1)" }));
	d.run("CurDrawableHideModule('M') CurDrawableShowModule('M')");
	CHECK(d.ctx.calls == (std::vector<std::string>{ "module(M,0,0)", "module(M,1,0)" }));
	// a call without its argument does nothing (argc > 0 is required) except Hide/ShowModule, which read index 1 without a count check
	d.run("CurDrawableHideSubObject() CurDrawableShowSubObject() CurDrawableHideSubObjectPermanently()");
	CHECK(d.ctx.calls.empty());
	// none returns a value: the script that returns them returns nothing
	CHECK(d.run("return CurDrawableHideSubObject('x')") == "");
}

TEST_CASE("drawable: the context readers: previous state / animation are nil when empty, the fraction is a number, model condition and status are 1.0 or nil (RW 0x7351C4, 0x735230, 0x7346FC, 0x734924, 0x73499B)")
{
	Draw d;
	CHECK(d.run("return CurDrawablePrevAnimationState()") == "");           // nil: no result string
	d.ctx.prevState = "STATE_Idle";
	d.ctx.prevAnim = "IdleA";
	CHECK(d.run("return CurDrawablePrevAnimationState()") == "STATE_Idle");
	CHECK(d.run("return CurDrawablePrevAnimation()") == "IdleA");
	CHECK(d.run("return CurDrawablePrevAnimFraction()") == "0.25");
	CHECK(d.run("if CurDrawablePrevAnimationState() == 'STATE_Idle' then return 'yes' end return 'no'") == "yes");
	d.ctx.conditions.insert("MOVING");
	CHECK(d.run("return CurDrawableModelcondition('MOVING')") == "1");       // the number 1.0
	CHECK(d.run("return CurDrawableModelcondition('DAMAGED')") == "");
	CHECK(d.run("return CurDrawableModelcondition('NOT_A_FLAG')") == "");   // unknown name: nil
	CHECK(d.run("return CurDrawableModelcondition()") == "");
	CHECK(d.run("if CurDrawableModelcondition('MOVING') == 1 then return 'moving' end") == "moving");
	d.ctx.statuses.insert("UNDER_CONSTRUCTION");
	CHECK(d.run("return CurDrawableObjectStatus('UNDER_CONSTRUCTION')") == "1");
	CHECK(d.run("return CurDrawableObjectStatus('DESTROYED')") == "");
}

TEST_CASE("drawable: transition requests, AllowToContinue, sounds (audio needed), client random, GetFrame (RW 0x734ACE, 0x734739, 0x73529F, 0x734695, 0x73461B)")
{
	Draw d;
	d.run("CurDrawableSetTransitionAnimState('TRANS_A') CurDrawableAllowToContinue() CurDrawablePlaySound('Snd')");
	CHECK(d.ctx.calls == (std::vector<std::string>{ "transition(TRANS_A)", "allow", "sound(Snd)" }));
	d.audio = false;
	d.run("CurDrawablePlaySound('Snd')");
	CHECK(d.ctx.calls.empty());
	d.run("CurDrawableSetTransitionAnimState()");
	CHECK(d.ctx.calls.empty());
	CHECK(d.run("return GetClientRandomNumberReal(1, 3)") == "2");
	CHECK(d.run("return GetClientRandomNumberReal(1)") == "0");   // argc < 2: 0.0
	CHECK(d.run("return GetClientRandomNumberReal()") == "0");
	CHECK(d.run("return GetFrame()") == "41");
	d.frame = 0xFFFFFFFFu;
	CHECK(d.run("return GetFrame()") == "4294967295");
}

TEST_CASE("drawable: target queries: distance, height, bearing are numbers or nil; IsCurrentTargetKindof pushes nil THEN false when the context is missing (Q8) (RW 0x734A18, 0x734649, 0x734A75, 0x73667D)")
{
	Draw d;
	CHECK(d.run("return CurDrawableGetCurrentTargetDistance()") == "12.5");
	CHECK(d.run("return CurDrawableGetCurrentTargetHeight()") == "-3");
	CHECK(d.run("return CurDrawableGetCurrentTargetBearing()") == "90");
	d.ctx.haveTarget = false;
	CHECK(d.run("return CurDrawableGetCurrentTargetDistance()") == "");
	d.ctx.kindOf = 2;
	CHECK(d.run("if CurDrawableIsCurrentTargetKindof('INFANTRY') then return 'yes' else return 'no' end") == "yes");
	d.ctx.kindOf = 1; // no target or not of that kind: false
	CHECK(d.run("if CurDrawableIsCurrentTargetKindof('INFANTRY') then return 'yes' else return 'no' end") == "no");
	d.ctx.kindOf = 0; // the chain context -> drawable -> object is missing: nil then false, returns 1: the visible result is the false
	CHECK(d.run("if CurDrawableIsCurrentTargetKindof('INFANTRY') then return 'yes' else return 'no' end") == "no");
	CHECK(d.run("return tostring(CurDrawableIsCurrentTargetKindof('INFANTRY'))") == "false");
	// outside a script there is no context at all
	lua_State *L = d.state.runtime().state();
	CHECK(run(d.state.runtime(), "return CurDrawablePrevAnimFraction()").values == "number:0");
	CHECK(run(d.state.runtime(), "return CurDrawableIsCurrentTargetKindof('X')").values == "boolean:false");
	CHECK(run(d.state.runtime(), "return CurDrawableGetCurrentTargetDistance()").values == "nil");
	CHECK(run(d.state.runtime(), "return CurDrawableModelcondition('MOVING')").values == "nil");
	CHECK(lua_gettop(L) == 0);
}

TEST_CASE("drawable: run(): the returned value is the Animation label, converted to a string; errors are swallowed and give no label; the state's globals persist (RW 0x734D62)")
{
	Draw d;
	CHECK(d.run("return 'FlagUpNoLower'") == "FlagUpNoLower");
	CHECK(d.run("return 5") == "5");
	CHECK(d.run("return 1 < 2") == "true");   // lua_tostring converts a boolean too
	CHECK(d.run("return nil") == "");
	CHECK(d.run("return {}") == "");
	CHECK(d.run("x = 1") == "");
	CHECK(d.status == 0);
	// a script error: status 1, no label, an alert was kept
	d.state.runtime().clearAlerts();
	CHECK(d.run("CurDrawableNoSuchFunction('x')") == "");
	CHECK(d.status == 1);
	REQUIRE(d.state.runtime().alerts().size() == 1);
	CHECK(d.state.runtime().alerts()[0].find("TestObject") != std::string::npos); // the chunk name is the object's template name
	// a syntax error: status 3, nothing runs
	CHECK(d.run("if then") == "");
	CHECK(d.status == 3);
	// the retail typo: CurDrawableTransitionAnimState is not a registered name (spec 1.5)
	CHECK(d.run("CurDrawableTransitionAnimState('TRANS')") == "");
	CHECK(d.status == 1);
	// globals persist across runs and "drawables": the five retail scripts that read `Prev` before assigning it rely on it
	d.run("Prev = 'STATE_A'");
	CHECK(d.run("return Prev") == "STATE_A");
	CHECK(d.run("if Prev == nil then Prev = 'x' end return Prev") == "STATE_A");
	// the stack is empty after every run, the context is cleared
	CHECK(lua_gettop(d.state.runtime().state()) == 0);
	CHECK(d.state.context() == nullptr);
}

TEST_CASE("drawable: Lua arithmetic and string operators the retail scripts use: / - .. comparisons and `or`, `and`, `not` in conditions")
{
	Draw d;
	d.ctx.conditions.insert("MOVING");
	CHECK(d.run("Num = 7 Rand = GetClientRandomNumberReal(0, 4) if Rand / 2 > 0.5 and Num - 3 == 4 then return 'ok' end") == "ok");
	CHECK(d.run("PrevName = 'a' .. 'b' if PrevName == 'ab' or Num == 0 then return PrevName end") == "ab");
	CHECK(d.run("if not CurDrawableModelcondition('B') then return 'notB' end") == "notB");
	CHECK(d.run("if CurDrawableModelcondition('MOVING') ~= nil and not (Num == 0) then return 'M' end") == "M");
}

// ---------------------------------------------------------------------------------------------------------------------
// the draw layer's host
// ---------------------------------------------------------------------------------------------------------------------
namespace
{
struct StubApi : W3DDrawScriptApi
{
	std::string prevState = "STATE_Idle";
	std::vector<std::string> log;
	std::string prevAnimationState() override { return prevState; }
	std::string prevAnimation() override { return ""; }
	float prevAnimFraction() override { return 0.95f; }
	std::string transitionAnimState() override { return ""; }
	bool modelCondition(const std::string &) override { return true; }
	void setTransitionAnimState(const std::string &n) override { log.push_back("transition " + n); }
	void allowToContinue() override { log.push_back("allow"); }
	void hideSubObject(const std::string &n) override { log.push_back("hide " + n); }
	void showSubObject(const std::string &n) override { log.push_back("show " + n); }
	void hideSubObjectPermanently(const std::string &n) override { log.push_back("hidep " + n); }
	void showSubObjectPermanently(const std::string &n) override { log.push_back("showp " + n); }
	void hideModule(const std::string &n) override { log.push_back("hidemod " + n); }
	void showModule(const std::string &n) override { log.push_back("showmod " + n); }
	void playSound(const std::string &n) override { log.push_back("sound " + n); }
	float clientRandomReal(float lo, float) override { return lo; }
	bool isCurrentTargetKindOf(const std::string &) override { return true; }
	float currentTargetBearing() override { return 0.0f; }
};
} // namespace

TEST_CASE("W3DLuaDrawScriptHost: runs a BeginScript through W3DDrawScriptApi, reports what the api cannot answer (S-126), swallows script errors into a failed run")
{
	W3DLuaDrawScriptHost host;
	StubApi api;
	std::string returned, error;
	// the retail shape (neutral\captureflag.ini): a state script that hides parts and returns an Animation label
	CHECK(host.run("if CurDrawablePrevAnimationState() == 'STATE_Idle' then CurDrawableHideSubObject('FLAG') CurDrawableSetTransitionAnimState('TRANS_X') return 'Lbl' end", api, &returned, &error));
	CHECK(returned == "Lbl");
	CHECK(api.log == (std::vector<std::string>{ "hide FLAG", "transition TRANS_X" }));
	// a module-first probe is answered "no module" and reported; an explicit module call is forwarded
	api.log.clear();
	CHECK(host.run("CurDrawableShowModule('Mod')", api, &returned, &error));
	CHECK(api.log == (std::vector<std::string>{ "showmod Mod" }));
	CHECK(host.reports().has("S-126", "module tags"));
	CHECK(host.reports().has("S-124", "CurDrawableShowModule")); // the explicit show is forwarded but its visibility is not applied: reported
	CHECK(host.run("return CurDrawableObjectStatus('X')", api, &returned, &error));
	CHECK(returned == "");
	CHECK(host.reports().has("S-126", "status bits"));
	CHECK(host.run("return CurDrawableGetCurrentTargetDistance()", api, &returned, &error));
	CHECK(host.reports().has("S-126", "target position"));
	CHECK(host.run("return GetFrame()", api, &returned, &error));
	CHECK(host.reports().has("S-124", "frame counter"));
	// a failing script: false and the message; a later script is unaffected
	CHECK(!host.run("NoSuchFunction()", api, &returned, &error));
	CHECK(error.find("NoSuchFunction") != std::string::npos);
	CHECK(returned == "");
	CHECK(host.run("return 'again'", api, &returned, &error));
	CHECK(returned == "again");
	CHECK(host.scriptsRun() == 7);
	CHECK(host.failures().size() == 1);
	// with a frame source GetFrame answers
	W3DLuaDrawScriptHost::Options o;
	o.frame = [] { return 123u; };
	W3DLuaDrawScriptHost framed(o);
	CHECK(framed.run("return GetFrame()", api, &returned, &error));
	CHECK(returned == "123");
	CHECK(!framed.reports().has("S-124"));
}

TEST_CASE("W3DLuaDrawScriptHost: the explicit module visibility and the sound the draw layer cannot apply are reported S-124 at each request, naming the operation and the retail address")
{
	W3DLuaDrawScriptHost host;
	StubApi api;
	std::string returned, error;
	CHECK(host.run("CurDrawableHideModule('ModuleTag_Fire')", api, &returned, &error));
	CHECK(host.reports().has("S-124", "CurDrawableHideModule"));
	CHECK(host.reports().has("S-124", "0x6789B4"));
	CHECK(host.reports().has("S-124", "ModuleTag_Fire"));
	CHECK(host.run("CurDrawablePlaySound('Snd_Roar')", api, &returned, &error));
	CHECK(host.reports().has("S-124", "CurDrawablePlaySound"));
	CHECK(host.reports().has("S-124", "0x73529F"));
	CHECK(host.reports().has("S-124", "Snd_Roar"));
	CHECK(api.log == (std::vector<std::string>{ "hidemod ModuleTag_Fire", "sound Snd_Roar" })); // still forwarded to the draw module's own log
}

TEST_CASE("W3DLuaDrawScriptHost: CurDrawablePlaySound on a draw module with the audio service plays without a report (AUDIO-2, RW 0x73529F)")
{
	struct AudibleApi : StubApi
	{
		bool playsSound() const override { return true; }
	};
	W3DLuaDrawScriptHost host;
	AudibleApi api;
	std::string returned, error;
	CHECK(host.run("CurDrawablePlaySound('Snd_Roar')", api, &returned, &error));
	CHECK(api.log == (std::vector<std::string>{ "sound Snd_Roar" }));
	CHECK_FALSE(host.reports().has("S-124", "CurDrawablePlaySound"));
}

// ---------------------------------------------------------------------------------------------------------------------
// retail: every BeginScript body
// ---------------------------------------------------------------------------------------------------------------------
TEST_CASE("retail: every BeginScript body of every draw module compiles and runs in the real drawable state; the failures are exactly the spec's undefined-function typo")
{
	retailtest::Mount *mount = retailtest::pureMount();
	if (!mount)
	{
		retailtest::printSkip("retail: BeginScript corpus in real Lua");
		return;
	}
	REQUIRE_MESSAGE(mount->fs, mount->error);
	W3DLuaDrawScriptHost host;
	StubApi api;
	size_t scripts = 0, distinctShapes = 0;
	std::set<std::string> shapes;
	std::map<std::string, size_t> moduleNames;
	std::vector<std::string> failedTexts;
	for (const drawtest::RetailDrawModule &m : drawtest::retailDrawScan().modules)
	{
		if (!m.data)
		{
			continue;
		}
		for (const AnimationStateInfo &a : m.data->m_animationStates)
		{
			if (a.beginScript.empty())
			{
				continue;
			}
			++scripts;
			if (shapes.insert(a.beginScript).second)
			{
				++distinctShapes;
			}
			std::string returned, error;
			api.log.clear();
			host.setChunkName(m.object);
			if (!host.run(a.beginScript, api, &returned, &error))
			{
				failedTexts.push_back(m.object + " " + a.stateName + ": " + error);
			}
			for (const std::string &l : api.log)
			{
				if (l.rfind("hidemod ", 0) == 0 || l.rfind("showmod ", 0) == 0)
				{
					++moduleNames[l];
				}
			}
		}
	}
	std::printf("BeginScript corpus in real Lua: %zu scripts (%zu distinct texts), %zu failures\n", scripts, distinctShapes, failedTexts.size());
	for (size_t i = 0; i < failedTexts.size() && i < 12; ++i)
	{
		std::printf("  %s\n", failedTexts[i].c_str());
	}
	CHECK(scripts > 2000);
	// every failure is the use of the unregistered name CurDrawableTransitionAnimState or an error the report explains
	for (const std::string &f : failedTexts)
	{
		INFO(f);
		CHECK(f.find("CurDrawableTransitionAnimState") != std::string::npos);
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// the draw runtime on real Lua (replaces the interim subset interpreter of the DRAW-1 tests for the scenarios below)
// ---------------------------------------------------------------------------------------------------------------------
namespace
{
class ScriptedRandom : public W3DDrawRandom
{
public:
	std::deque<int> values;
	int value(int, int) override
	{
		if (values.empty())
		{
			throw std::logic_error("ScriptedRandom: unexpected value()");
		}
		const int v = values.front();
		values.pop_front();
		return v;
	}
	float real(float lo, float hi) override { return hi - lo <= 0.0f ? hi : (lo + hi) * 0.5f; }
};
} // namespace

TEST_CASE("retail: Gondor soldier on real Lua: SELECTED from idle plays TRANS_IdleToSelected first (the BeginScript asks for it through CurDrawableSetTransitionAnimState), then the state")
{
	retailtest::Mount *mount = retailtest::pureMount();
	if (!mount)
	{
		retailtest::printSkip("retail: Gondor soldier on real Lua");
		return;
	}
	REQUIRE_MESSAGE(mount->fs, mount->error);
	const drawtest::RetailDrawModule *module = nullptr;
	for (const drawtest::RetailDrawModule &m : drawtest::retailDrawScan().modules)
	{
		if (m.data && m.object == "GondorFighter" && m.className == "W3DHordeModelDraw")
		{
			module = &m;
			break;
		}
	}
	REQUIRE(module != nullptr);
	const W3DHordeModelDrawModuleData *data = dynamic_cast<const W3DHordeModelDrawModuleData *>(module->data.get());
	REQUIRE(data != nullptr);
	ArchiveW3DFileSource source(*mount->fs);
	WW3DAssetManager assets(source);
	WW3DDrawAssets drawAssets(assets);
	ScriptedRandom random;
	random.values = { 0 };
	W3DLuaDrawScriptHost host;
	W3DScriptedModelDraw::Options o;
	o.buildBones = false;
	W3DHordeModelDraw draw(*data, 2, drawAssets, random, &host, o);
	REQUIRE(draw.currentAnimationState()->stateName == "STATE_Idle");
	draw.setModelConditionFlags(drawtest::flagsOf({ "SELECTED" }));
	CHECK(draw.currentAnimationState()->stateName == "TRANS_IdleToSelected");
	CHECK(draw.pendingStatePending());
	bool reached = false;
	for (int i = 0; i < 200 && !reached; ++i)
	{
		draw.advance(100.0);
		reached = draw.currentAnimationState()->stateName == "STATE_Selected";
	}
	REQUIRE(reached);
	CHECK(draw.errors().empty());
	CHECK(host.failures().empty());
	CHECK(host.scriptsRun() >= 3);
	const std::vector<std::string> &log = draw.log();
	REQUIRE(log.size() >= 5);
	CHECK(log[0] == "enter STATE_Idle");
	CHECK(log[1] == "script STATE_Idle");
	CHECK(log[2] == "enter STATE_Selected");
	CHECK(log[3] == "script STATE_Selected");
	CHECK(log[4] == "transition TRANS_IdleToSelected");
}

TEST_CASE("retail: every BeginScript block of every INI under data\\ and every map.ini (2,749 + 31) compiles in the real drawable state; three fail: the retail typo")
{
	retailtest::Mount *mount = retailtest::pureMount();
	if (!mount)
	{
		retailtest::printSkip("retail: all BeginScript blocks");
		return;
	}
	REQUIRE_MESSAGE(mount->fs, mount->error);
	FilenameList iniFiles, mapInis;
	mount->fs->getFileListInDirectory(std::string(), "Data\\", "*.ini", iniFiles, true);
	mount->fs->getFileListInDirectory(std::string(), "Maps\\", "map.ini", mapInis, true);
	Draw d;
	size_t inData = 0, inMaps = 0, files = 0, failed = 0, badStatus = 0;
	std::set<std::string> typoFiles;
	// the lines as the INI layer sees them (comments blanked, includes not expanded: they hold definitions only); a script body is its lines
	// concatenated as they are, without a separator, as RW 0x42D400 reads them
	auto scan = [&](const std::string &path, size_t *counter) {
		std::vector<std::uint8_t> bytes;
		std::string err;
		REQUIRE_MESSAGE(mount->fs->readFile(path, bytes, &err), err);
		TextFile tf;
		// includes name other files of the same tree: expanded only when they exist; a missing one is an error of the INI lane, not this test's
		const bool ok = tf.parseBytes(path, bytes, [&](const std::string &p, std::vector<std::uint8_t> &out, std::string *e) {
			out.clear(); // an included file contributes nothing here (it is scanned as a file of its own)
			(void)p; (void)e;
			return true;
		}, &err);
		REQUIRE_MESSAGE(ok, path << ": " << err);
		bool any = false, inScript = false;
		std::string body;
		for (const TextFile::Line &line : tf.lines())
		{
			const std::string &text = line.text;
			const size_t b = text.find_first_not_of(" \t\r\n=");
			std::string first;
			if (b != std::string::npos)
			{
				const size_t e = text.find_first_of(" \t\r\n=", b);
				first = text.substr(b, e == std::string::npos ? std::string::npos : e - b);
			}
			for (char &c : first)
			{
				c = (char)std::tolower((unsigned char)c);
			}
			if (!inScript)
			{
				if (first == "beginscript")
				{
					inScript = true;
					body.clear();
				}
				continue;
			}
			if (first == "endscript")
			{
				inScript = false;
				++*counter;
				any = true;
				std::string result;
				const int status = d.state.run(body, "x", &d.ctx, &result);
				if (status != 0)
				{
					++failed;
					if (body.find("CurDrawableTransitionAnimState") != std::string::npos)
					{
						typoFiles.insert(path);
					}
					else
					{
						++badStatus;
						INFO(path << ": " << body);
						CHECK(status == 0);
					}
				}
				continue;
			}
			body += text;
		}
		if (any)
		{
			++files;
		}
	};
	for (const std::string &p : iniFiles)
	{
		scan(p, &inData);
	}
	for (const std::string &p : mapInis)
	{
		scan(p, &inMaps);
	}
	std::printf("BeginScript scan: %zu blocks under data\\ in %zu files, %zu in map.ini files, %zu failures (files with the typo: %zu)\n", inData, files, inMaps, failed, typoFiles.size());
	// two independent counts agree (this scan over the INI layer's lines and a Python scan over the archive text): 2,749 blocks in 359 files under
	// data\ and 31 in the cinematic map.ini files. The spec's 2,845 / 2,876 (1.5, 1.6) does not reproduce in the effective RotWK tree.
	CHECK(inMaps == 31);
	CHECK(inData == 2749);
	CHECK(files == 359);
	// the three uses of the undefined CurDrawableTransitionAnimState (civilianunit.ini:1584, two cinematic map.ini files)
	CHECK(failed == 3);
	CHECK(badStatus == 0);
	CHECK(typoFiles.size() == 3);
}
