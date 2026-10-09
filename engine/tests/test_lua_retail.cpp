// OpenBFME retail tests: the real Scripts.lua and ScriptEvents.xml of RotWK 2.01 in the engine's two Lua states (lane LUA-1).
// Run only when ROTWK_INSTALL and BFME2_INSTALL are set (prints SKIP otherwise); uses the shared retail mount.
//
// Expected values: spec lua-scripting.md 1.1-1.7 (counts from independent scripts over the archive text; a second count by regular
// expression here), the retail text of Scripts.lua quoted at each test, and the engine behaviour traced from the disassembly.

#include "doctest.h"

#include "LuaTestUtil.h"
#include "RetailTestMount.h"

#include "Common/MD5.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include <regex>

using luatest::run;
using luatest::RecordingHost;

namespace
{
struct RetailScripts
{
	std::string lua, xml;
	bool loaded = false;
};

RetailScripts &retail()
{
	static RetailScripts s;
	if (!s.loaded)
	{
		retailtest::Mount *mount = retailtest::pureMount();
		if (mount)
		{
			REQUIRE_MESSAGE(mount->fs, mount->error);
			std::vector<std::uint8_t> a, b;
			std::string err;
			REQUIRE_MESSAGE(mount->fs->readFile("data\\scripts\\scripts.lua", a, &err), err);
			REQUIRE_MESSAGE(mount->fs->readFile("data\\scripts\\scriptevents.xml", b, &err), err);
			s.lua.assign(a.begin(), a.end());
			s.xml.assign(b.begin(), b.end());
			s.loaded = true;
		}
	}
	return s;
}

struct RetailRig : luatest::Rig
{
	RetailRig()
	{
		const RetailScripts &r = retail();
		load(r.lua, r.xml);
	}
	std::string eval(const std::string &code) { return run(*engine->logic(), code).values; }
	// an object of the given EventList, registered
	int make(int id, const char *list)
	{
		RecordingHost::Obj &o = host.add(id);
		o.info.luaEvents = engine->findEventList(list);
		engine->registerObject(o.info);
		return id;
	}
};

bool skipped(const char *name)
{
	if (!retailtest::pureMount())
	{
		retailtest::printSkip(name);
		return true;
	}
	return false;
}

std::vector<std::string> matches(const std::string &text, const char *re)
{
	std::vector<std::string> out;
	const std::regex rx(re);
	for (auto it = std::sregex_iterator(text.begin(), text.end(), rx); it != std::sregex_iterator(); ++it)
	{
		out.push_back((*it)[1].str());
	}
	return out;
}
} // namespace

TEST_CASE("retail: Scripts.lua is the file of the spec (29,240 bytes, md5 38849a61...) and compiles and runs in the logic state without an error")
{
	if (skipped("retail: Scripts.lua loads"))
	{
		return;
	}
	const RetailScripts &r = retail();
	CHECK(r.lua.size() == 29240);
	CHECK(MD5::ofBytes(r.lua.data(), r.lua.size()) == "38849a61980303adb37db8519c7204f1");
	RetailRig rig;
	CHECK(rig.engine->logic()->alerts().empty()); // no syntax error, no run time error
	// 84 global functions (spec 1.4), counted independently from the text
	const std::vector<std::string> declared = matches(r.lua, "(?:^|\\n)function +([A-Za-z_][A-Za-z_0-9]*)");
	CHECK(declared.size() == 84);
	for (const std::string &n : declared)
	{
		INFO(n);
		CHECK(rig.eval("return type(" + n + ")") == "string:function");
	}
	// no global of its own besides the functions: the only free global read in the file is `data` (spec 1.7 item 3)
	CHECK(rig.eval("return data") == "nil");
}

TEST_CASE("retail: every EventHandler of ScriptEvents.xml names a function Scripts.lua defines (the handler check of RW 0x739983 finds nothing to drop)")
{
	if (skipped("retail: handler functions exist"))
	{
		return;
	}
	RetailRig rig;
	for (const std::string &n : rig.engine->events().notes())
	{
		// the only retail notes are the Inherit data bug of EvilPorterFunctions
		CHECK(n.find("is not defined") == std::string::npos);
		CHECK(n.find("is not a lua function") == std::string::npos);
	}
	// 119 handlers over 77 lists: every list's every flattened handler resolves to a Lua function
	size_t checked = 0;
	for (size_t i = 0; i < rig.engine->events().eventListCount(); ++i)
	{
		for (const LuaEventHandler &h : rig.engine->events().eventList(i).handlers)
		{
			INFO(rig.engine->events().eventList(i).name << " " << h.function);
			if (rig.keys.keyToName(h.key) == "onCreated")
			{
				CHECK(h.function == "MakeMeAlert"); // Fram's lower case handler is registered under a key nothing dispatches
			}
			CHECK(run(*rig.engine->logic(), "return type(" + h.function + ")").values == "string:function");
			++checked;
		}
	}
	CHECK(checked > 119);
}

TEST_CASE("retail: a troll is created: TrollFunctions -> OnTrollCreated hides Trunk01 permanently and grants Upgrade_SwitchToRockThrowing (scripts.lua:31-34 via scriptevents.xml:91)")
{
	if (skipped("retail: troll created"))
	{
		return;
	}
	RetailRig rig;
	rig.host.upgradesObjectType = { "Upgrade_SwitchToRockThrowing" };
	const int troll = rig.make(1, "TrollFunctions");
	rig.engine->dispatchInternal(LUAEVENT_OnCreated, rig.info(troll));
	REQUIRE(rig.host.calls.size() == 3);
	CHECK(rig.host.calls[0] == "showModule(1, Trunk01, 0, 1)");
	CHECK(rig.host.calls[1] == "showSubObject(1, Trunk01, 0, 1)");
	CHECK(rig.host.calls[2] == "Object::giveUpgrade(1, Upgrade_SwitchToRockThrowing)");
	CHECK(rig.engine->logic()->alerts().empty());
	CHECK(lua_gettop(rig.engine->logic()->state()) == 0);
}

TEST_CASE("retail: every OnCreated handler of every EventList runs on a synthetic object, calls only the bindings its text names, in order, and raises no Lua error")
{
	if (skipped("retail: all OnCreated handlers"))
	{
		return;
	}
	const RetailScripts &r = retail();
	RetailRig rig;
	// every upgrade name of the file is an object upgrade for this host
	for (const std::string &u : matches(r.lua, "\"(Upgrade_[A-Za-z0-9_]+)\""))
	{
		rig.host.upgradesObjectType.insert(u);
	}
	// function bodies: from `function NAME(` to the next line that starts with `end`
	std::map<std::string, std::string> bodies;
	{
		// manual scan (a lazy std::regex over the whole file recurses once per character and overflows the stack in sanitizer builds)
		const std::regex head("function +([A-Za-z_0-9]+)\\s*\\([^)]*\\)");
		const std::string text = "\n" + r.lua;
		size_t at = 0;
		while ((at = text.find("\nfunction ", at)) != std::string::npos)
		{
			const size_t start = at + 1;
			const size_t lineEnd = text.find('\n', start);
			const std::string line = text.substr(start, lineEnd - start);
			std::smatch m;
			REQUIRE(std::regex_search(line, m, head));
			const size_t bodyStart = start + (size_t)m.length(0);
			const size_t bodyEnd = text.find("\nend", lineEnd);
			REQUIRE(bodyEnd != std::string::npos);
			bodies[m[1].str()] = text.substr(bodyStart, bodyEnd - bodyStart);
			at = bodyEnd;
		}
	}
	REQUIRE(bodies.size() == 84);
	rig.host.knownPowers.insert("SpecialAbilityGateWatchersFear");
	size_t ran = 0, hides = 0, grants = 0;
	int id = 100;
	for (size_t i = 0; i < rig.engine->events().eventListCount(); ++i)
	{
		const LuaEventList &list = rig.engine->events().eventList(i);
		const LuaEventHandler *h = list.find(rig.engine->events().internalKey(LUAEVENT_OnCreated));
		if (!h)
		{
			continue;
		}
		INFO(list.name << " " << h->function);
		const int obj = rig.make(++id, list.name.c_str());
		rig.host.calls.clear();
		rig.engine->dispatchInternal(LUAEVENT_OnCreated, rig.info(obj));
		CHECK(rig.engine->logic()->alerts().empty());
		CHECK(lua_gettop(rig.engine->logic()->state()) == 0);
		// expected from the text of the handler: the binding calls with `self` as first argument, in order, comments removed; the helper
		// OnCreateAHeroFunctions calls (CreateAHeroHideEverything) is expanded in place
		std::vector<std::string> expected;
		const std::string idText = std::to_string(obj);
		auto expand = [&](const std::string &text) {
			const std::string clean = std::regex_replace(text, std::regex("--[^\\n]*"), "");
			const std::regex call("([A-Za-z]+)\\s*\\(\\s*self\\s*(?:,\\s*(?:\"([A-Za-z0-9_]+)\"\\s*(?:,\\s*(true|false)\\s*)?|(true|false)\\s*))?\\)");
			for (auto it = std::sregex_iterator(clean.begin(), clean.end(), call); it != std::sregex_iterator(); ++it)
			{
				const std::string fnName = (*it)[1].str(), name = (*it)[2].str(), flag = (*it)[3].str();
				if (fnName == "ObjectHideSubObjectPermanently")
				{
					expected.push_back("showModule(" + idText + ", " + name + ", 0, 1)");
					expected.push_back("showSubObject(" + idText + ", " + name + ", " + (flag == "true" ? "0" : "1") + ", 1)");
					++hides;
				}
				else if (fnName == "ObjectHideSubObject")
				{
					expected.push_back("showSubObject(" + idText + ", " + name + ", " + (flag == "true" ? "0" : "1") + ", 0)");
				}
				else if (fnName == "ObjectGrantUpgrade")
				{
					expected.push_back("Object::giveUpgrade(" + idText + ", " + name + ")");
					++grants;
				}
				else if (fnName == "ObjectForbidPlayerCommands")
				{
					expected.push_back("forbidPlayerCommands(" + idText + ", true)");
				}
				else if (fnName == "ObjectSetGeometryActive")
				{
					expected.push_back("setGeometryActive(" + idText + ", " + name + ", " + (flag == "true" ? "true" : "false") + ")");
				}
				else if (fnName == "ObjectDoSpecialPower")
				{
					expected.push_back("doSpecialPower(" + idText + ", " + name + ")");
				}
				else if (fnName == "CreateAHeroHideEverything")
				{
					// expanded below
				}
				else
				{
					FAIL("an OnCreated handler calls a binding this test does not model: " << fnName);
				}
			}
		};
		const std::string body = bodies.at(h->function);
		if (body.find("CreateAHeroHideEverything") != std::string::npos)
		{
			expand(bodies.at("CreateAHeroHideEverything"));
		}
		else
		{
			expand(body);
		}
		CHECK(rig.host.calls == expected);
		++ran;
	}
	// the spec counts 47 OnCreated handler elements; flattened over 77 lists more lists have one (inheritance), every one ran
	CHECK(ran >= 47);
	CHECK(hides > 150);
	CHECK(grants >= 5);
	CHECK(rig.engine->reports().count("S-124") == 0);
}

TEST_CASE("retail: the fear events: BeUncontrollablyAfraid on an infantry unit enters the uncontrollable cower state unless it cannot suffer fear (InfantryFunctions, scripts.lua:133)")
{
	if (skipped("retail: fear events"))
	{
		return;
	}
	RetailRig rig;
	const int a = rig.make(1, "InfantryFunctions");
	const int b = rig.make(2, "InfantryFunctions");
	LuaEventArgs args;
	args.arg[0] = LuaEventArg::makeObject(b);
	const LuaEventRef ev = rig.engine->findEvent("BeUncontrollablyAfraid");
	REQUIRE(ev.kind == LuaEventRef::SCRIPTED);
	rig.engine->dispatch(ev, rig.info(a), args);
	REQUIRE(rig.host.calls.size() == 1);
	CHECK(rig.host.calls[0] == "enterEmotion(1, 5, 2)");
	// with a fear resistance modifier that is always enough: the handler returns early, after drawing the logic RNG once
	rig.host.calls.clear();
	rig.host.objects.at(a).hasModifier = true;
	rig.host.objects.at(a).modifierBonus = 1.0f;
	const auto seed = rig.random.seedArray();
	rig.engine->dispatch(ev, rig.info(a), args);
	CHECK(rig.host.calls.empty());
	CHECK(rig.random.seedArray() != seed);
	// BeAfraidOfRampage / BeAfraidOfBalrog / BeAfraidOfGateDamaged: cower (4); BeTerrified: run away panic (6) with no RNG draw
	rig.host.objects.at(a).hasModifier = false;
	for (const char *name : { "BeAfraidOfRampage", "BeAfraidOfBalrog" })
	{
		rig.host.calls.clear();
		rig.engine->dispatch(rig.engine->findEvent(name), rig.info(a), args);
		INFO(name);
		CHECK(rig.host.calls == std::vector<std::string>{ "enterEmotion(1, 4, 2)" });
	}
	rig.host.calls.clear();
	const auto before = rig.random.seedArray();
	rig.engine->dispatch(rig.engine->findEvent("BeTerrified"), rig.info(a), args);
	CHECK(rig.host.calls == std::vector<std::string>{ "enterEmotion(1, 6, 2)" });
	CHECK(rig.random.seedArray() == before);
	// EvilPorterFunctions does not inherit the infantry fear set (spec 1.7 item 2): BeUncontrollablyAfraid does nothing for it
	const int porter = rig.make(3, "EvilPorterFunctions");
	rig.host.calls.clear();
	rig.engine->dispatch(ev, rig.info(porter), args);
	CHECK(rig.host.calls.empty());
}

TEST_CASE("retail: the data bugs: Fram's onCreated never fires, MountainGiant's generic event reads an undefined `data`, the troll's works (spec 1.7)")
{
	if (skipped("retail: data bugs"))
	{
		return;
	}
	RetailRig rig;
	const int fram = rig.make(1, "FramFunctions");
	rig.engine->dispatchInternal(LUAEVENT_OnCreated, rig.info(fram));
	CHECK(rig.host.calls.empty()); // MakeMeAlert would have called enterEmotion(.., 9, 0)
	// the handler exists under the lower case key: dispatching that key by hand does call it
	LuaEventRef lower;
	lower.kind = LuaEventRef::SCRIPTED;
	lower.key = rig.keys.nameToKey("onCreated");
	rig.engine->dispatch(lower, rig.info(fram), LuaEventArgs());
	CHECK(rig.host.calls == std::vector<std::string>{ "enterEmotion(1, 9, 0)" });
	rig.host.calls.clear();
	// the generic event: the troll compares its parameter, the mountain giant a global that is nil: tostring(nil) is "nil"
	const int troll = rig.make(2, "TrollFunctions");
	const int giant = rig.make(3, "MountainGiantFunctions");
	LuaEventArgs show;
	show.arg[0] = LuaEventArg::makeString("show_rock");
	rig.engine->dispatchInternal(LUAEVENT_OnGenericEvent, rig.info(troll), show);
	CHECK(rig.host.calls == (std::vector<std::string>{ "showModule(2, ROCK, 0, 1)", "showSubObject(2, ROCK, 1, 1)" }));
	rig.host.calls.clear();
	rig.engine->dispatchInternal(LUAEVENT_OnGenericEvent, rig.info(giant), show);
	CHECK(rig.host.calls.empty());
}

TEST_CASE("retail: OnCaptureFlagGenericEvent shows the flag of the capturing side, else of the owner's side, else FLAG_NEUTRAL (scripts.lua:41)")
{
	if (skipped("retail: capture flag"))
	{
		return;
	}
	RetailRig rig;
	const int flag = rig.make(1, "CaptureFlagFunctions");
	rig.host.objects.at(flag).side = "Men";
	LuaEventArgs data;
	data.arg[0] = LuaEventArg::makeString("x");
	rig.engine->dispatchInternal(LUAEVENT_OnGenericEvent, rig.info(flag), data);
	// 7 hides, then the shown one
	REQUIRE(rig.host.calls.size() == 16);
	CHECK(rig.host.calls[0] == "showModule(1, FLAG_ISENGARD, 0, 1)");
	CHECK(rig.host.calls[13] == "showSubObject(1, FLAG_ANGMAR, 0, 1)");
	CHECK(rig.host.calls[14] == "showModule(1, FLAG_MEN, 0, 1)");
	CHECK(rig.host.calls[15] == "showSubObject(1, FLAG_MEN, 1, 1)");
	rig.host.calls.clear();
	rig.host.objects.at(flag).hasCapturer = true;
	rig.host.objects.at(flag).capturingSide = "Angmar";
	rig.engine->dispatchInternal(LUAEVENT_OnGenericEvent, rig.info(flag), data);
	CHECK(rig.host.calls[15] == "showSubObject(1, FLAG_ANGMAR, 1, 1)");
	rig.host.calls.clear();
	rig.host.objects.at(flag).capturingSide = "Nobody";
	rig.engine->dispatchInternal(LUAEVENT_OnGenericEvent, rig.info(flag), data);
	CHECK(rig.host.calls[15] == "showSubObject(1, FLAG_NEUTRAL, 1, 1)");
}
