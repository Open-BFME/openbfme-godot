// OpenBFME unit tests: the map script engine (lane SCRIPT-1). GPL-3.0.
//
// Expected values are never the engine's own output: the template registry pins are binary facts (tools/script/extract_script_templates.py,
// RW 0x7D01C0 / 0x7D5270), the census is tools/script/script_census.py's independent reader (engine/tests/data/script-census.json), and the
// runtime cases follow the RotWK functions cited at each check (ScriptEngine.h).

#include "doctest.h"
#include "LogicTestUtil.h"
#include "ScriptTestUtil.h"
#include "MapCorpusUtil.h"
#include "RetailTestMount.h"
#include "MapTestUtil.h"

#include "Common/DataChunk.h"
#include "Common/MiniJson.h"
#include "GameClient/MapChunks.h"
#include "GameClient/EndGame.h"
#include "GameClient/ScriptCameraDirector.h"
#include "GameLogic/Economy.h"
#include "GameClient/MapUtil.h"
#include "GameLogic/ScriptEngine/ScriptActions.h"
#include "GameLogic/ScriptEngine/ScriptConditions.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"
#include "GameLogic/ScriptEngine/ScriptTemplates.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include <functional>
#include <map>
#include <set>

using namespace mapcorpus;

using namespace scripttest;


TEST_CASE("script templates: the RotWK registries (RW 0x7D5270 then 0x7D01C0, RW 0x607D6C: 600 action and 203 condition slots)")
{
	CHECK(ScriptTemplates::conditions().size() == 203);
	CHECK(ScriptTemplates::actions().size() == 600);
	size_t named = 0;
	for (const ScriptTemplate &t : ScriptTemplates::conditions())
	{
		named += t.name.empty() ? 0 : 1;
	}
	CHECK(named == 196); // the holes 59, 60, 67, 68, 72, 73, 122 stay empty
	named = 0;
	for (const ScriptTemplate &t : ScriptTemplates::actions())
	{
		named += t.name.empty() ? 0 : 1;
	}
	CHECK(named == 596);
	CHECK(ScriptTemplates::condition(0)->name == "CONDITION_FALSE");
	CHECK(ScriptTemplates::condition(3)->name == "CONDITION_TRUE");
	CHECK(ScriptTemplates::condition(112)->name == "COUNTER_SECONDS");
	CHECK(ScriptTemplates::action(5)->name == "NO_OP");
	CHECK(ScriptTemplates::action(0)->name == "DEBUG_MESSAGE_BOX");
	// the action init fills condition slots 115 / 116 (stores at ScriptEngine + 0x16420 / 0x164A0)
	CHECK(ScriptTemplates::condition(115)->name == "UNIT_THREAT_LEVEL");
	CHECK(ScriptTemplates::condition(116)->name == "TEAM_THREAT_LEVEL");
	// MAP_REVEAL_IN_TRIGGER is registered twice: the by-name search finds the first
	CHECK(ScriptTemplates::action(552)->name == "MAP_REVEAL_IN_TRIGGER");
	CHECK(ScriptTemplates::action(553)->name == "MAP_REVEAL_IN_TRIGGER");
	CHECK(ScriptTemplates::findAction("MAP_REVEAL_IN_TRIGGER") == 552);
	// mode masks (Template + 0x00; constructor default 1): TIMER_EXPIRED runs only in an ordinary game, NO_OP in both
	CHECK(ScriptTemplates::condition(4)->modeMask == 1);
	CHECK(ScriptTemplates::action(5)->modeMask == 3);
	// the dispatch switch cases: REGION_CAMPS_SHOULD_UNPACK has none (retail: false)
	CHECK_FALSE(ScriptTemplates::condition(ScriptTemplates::findCondition("REGION_CAMPS_SHOULD_UNPACK"))->retailCase);
	CHECK(ScriptTemplates::condition(ScriptTemplates::findCondition("NAMED_DESTROYED"))->retailCase);
	// parameter types of a template (COUNTER: COUNTER, COMPARISON, INT)
	CHECK(ScriptTemplates::condition(1)->parameterTypes == std::vector<int>{ 4, 6, 0 });
}

TEST_CASE("script engine: seconds to frames (RW 0x60911C: ceil(0.005f * s * 1000.0f) under PC24, FISTP)")
{
	CHECK(ScriptEngine::secondsToFrames(0.0f) == 0);
	CHECK(ScriptEngine::secondsToFrames(1.0f) == 5);
	CHECK(ScriptEngine::secondsToFrames(0.5f) == 3);   // 2.5 -> ceil 3
	CHECK(ScriptEngine::secondsToFrames(2.0f) == 10);
	CHECK(ScriptEngine::secondsToFrames(14.5f) == 73); // 72.5 -> 73
	CHECK(ScriptEngine::secondsToFrames(0.1f) == 1);   // 0.5 (in binary32 just above) -> 1
	CHECK(ScriptEngine::secondsToFrames(-1.0f) == -5);
}

TEST_CASE("script engine: timers count down before the scripts run; one-shot scripts deactivate after their actions (RW 0x60CC67, 0x6099DC)")
{
	ScriptGame g;
	g.lists[1].items.push_back(item(script("start", { { cond("CONDITION_TRUE", {}) } },
		{ act("SET_MILLISECOND_TIMER", { sp(4, "t"), rp(1.0f) }), act("INCREMENT_COUNTER", { ip(0, 1), sp(4, "starts") }) }, {}, true)));
	g.lists[1].items.push_back(item(script("expire", { { cond("TIMER_EXPIRED", { sp(4, "t") }) } }, { act("SET_FLAG", { sp(5, "done"), ip(8, 1) }) }, {}, true)));
	g.start();
	g.frame(); // frame 1: start runs (t = 5 frames)
	CHECK(g.counter("Alice/t") == 5);
	CHECK(g.counter("Alice/starts") == 1);
	for (int i = 0; i < 4; ++i)
	{
		g.frame(); // frames 2..5: 4, 3, 2, 1
	}
	CHECK(g.counter("Alice/t") == 1);
	CHECK_FALSE(g.engine().scriptActive("Alice/expire") == false); // still waiting
	g.frame(); // frame 6: 0 -> expired
	CHECK(g.counter("Alice/t") == 0);
	CHECK_FALSE(g.engine().scriptActive("Alice/expire"));
	CHECK(g.counter("Alice/starts") == 1); // the one-shot ran once
	g.frame();
	g.frame();
	CHECK(g.counter("Alice/t") == -1); // a countdown stops at -1
	g.frame();
	CHECK(g.counter("Alice/t") == -1);
}

TEST_CASE("script engine: OR of AND lists; an empty AND list is false; a disabled condition is skipped (RW 0x60930F)")
{
	ScriptGame g;
	// (empty) OR (FALSE) -> false: the false actions run
	g.lists[1].items.push_back(item(script("emptyOr", { {}, { cond("CONDITION_FALSE", {}) } }, { act("INCREMENT_COUNTER", { ip(0, 1), sp(4, "yes") }) },
		{ act("INCREMENT_COUNTER", { ip(0, 1), sp(4, "no") }) })));
	// (TRUE and disabled FALSE) -> true
	g.lists[1].items.push_back(item(script("disabled", { { cond("CONDITION_TRUE", {}), cond("CONDITION_FALSE", {}, false) } },
		{ act("INCREMENT_COUNTER", { ip(0, 1), sp(4, "disabledTrue") }) })));
	// no OrCondition at all -> false
	g.lists[1].items.push_back(item(script("none", {}, { act("INCREMENT_COUNTER", { ip(0, 1), sp(4, "noneTrue") }) })));
	g.start();
	g.frame();
	g.frame();
	CHECK(g.counter("Alice/yes") == -999);
	CHECK(g.counter("Alice/no") == 2);
	CHECK(g.counter("Alice/disabledTrue") == 2);
	CHECK(g.counter("Alice/noneTrue") == -999);
}

TEST_CASE("script engine: per-side names, scripts before groups, inactive and subroutine groups, CALL_SUBROUTINE, ENABLE / DISABLE (RW 0x72C43C, 0x60BCE5, 0x60BFBD)")
{
	ScriptGame g;
	std::vector<ScriptItem> groupItems;
	groupItems.push_back(item(script("inGroup", { { cond("CONDITION_TRUE", {}) } }, { act("SET_COUNTER", { sp(4, "order"), ip(0, 2) }) })));
	g.lists[1].items.push_back(group("G", true, false, std::move(groupItems)));
	// a script AFTER the group in the file still runs before the group
	g.lists[1].items.push_back(item(script("first", { { cond("CONDITION_TRUE", {}) } }, { act("SET_COUNTER", { sp(4, "order"), ip(0, 1) }) })));
	std::vector<ScriptItem> sub;
	sub.push_back(item(script("subScript", { { cond("CONDITION_TRUE", {}) } }, { act("INCREMENT_COUNTER", { ip(0, 1), sp(4, "Bob/called") }) })));
	g.lists[1].items.push_back(group("SubGroup", true, true, std::move(sub)));
	auto subroutine = script("subS", { { cond("CONDITION_TRUE", {}) } }, { act("INCREMENT_COUNTER", { ip(0, 10), sp(4, "Bob/called") }) });
	subroutine->isSubroutine = true;
	g.lists[1].items.push_back(item(std::move(subroutine)));
	std::vector<ScriptItem> off;
	off.push_back(item(script("offScript", { { cond("CONDITION_TRUE", {}) } }, { act("INCREMENT_COUNTER", { ip(0, 1), sp(4, "off") }) })));
	g.lists[1].items.push_back(group("Off", false, false, std::move(off)));
	// Bob's script calls Alice's subroutines and enables Alice's group, by qualified names
	g.lists[2].items.push_back(item(script("caller", { { cond("CONDITION_TRUE", {}) } },
		{ act("CALL_SUBROUTINE", { sp(13, "Alice/SubGroup") }), act("CALL_SUBROUTINE", { sp(13, "Alice/subS") }), act("CALL_SUBROUTINE", { sp(13, "Alice/first") }),
			act("CALL_SUBROUTINE", { sp(13, "Alice/nothing") }) },
		{}, true)));
	g.start();
	g.frame();
	CHECK(g.counter("Alice/order") == 2); // "first" ran, then the group's script
	CHECK(g.counter("Bob/called") == 11);  // the subroutine group's script + the subroutine script; never on their own
	CHECK(g.counter("Alice/off") == -999);
	const auto &notes = g.engine().stats().notes;
	CHECK(notes.count("***Attempting to call script that is not a subroutine: Alice/first") == 1);
	CHECK(notes.count("***Script not defined: Alice/nothing") == 1);
	g.lists[2].items.clear();
}

TEST_CASE("script engine: ENABLE_SCRIPT / DISABLE_SCRIPT act on a group and a script of the name; flags, counters, random counters draw the logic RNG")
{
	ScriptGame g;
	std::vector<ScriptItem> off;
	off.push_back(item(script("inside", { { cond("CONDITION_TRUE", {}) } }, { act("INCREMENT_COUNTER", { ip(0, 1), sp(4, "inside") }) })));
	g.lists[1].items.push_back(group("Later", false, false, std::move(off)));
	g.lists[1].items.push_back(item(script("enabler", { { cond("COUNTER", { sp(4, "tick"), ip(6, 2), ip(0, 2) }) } }, { act("ENABLE_SCRIPT", { sp(2, "Later") }) })));
	g.lists[1].items.push_back(item(script("ticker", { { cond("CONDITION_TRUE", {}) } }, { act("INCREMENT_COUNTER", { ip(0, 1), sp(4, "tick") }) })));
	g.lists[1].items.push_back(item(script("flagger", { { cond("FLAG", { sp(5, "f"), ip(8, 0) }) } },
		{ act("SET_FLAG", { sp(5, "f"), ip(8, 1) }), act("SET_RANDOM_COUNTER", { sp(4, "r"), ip(0, 1), ip(0, 8) }) }, {}, true)));
	g.start();
	g.lw.logic->random().enableCallLog(true);
	g.frame(); // tick 0 at enabler, then 1
	g.frame(); // tick 1, then 2
	CHECK(g.counter("Alice/inside") == -999);
	g.frame(); // enabler sees 2 -> Later on; its script runs in this frame (groups after scripts)
	CHECK(g.counter("Alice/inside") == 1);
	bool *f = g.engine().findFlag("Alice/f");
	REQUIRE(f != nullptr);
	CHECK(*f);
	const auto &log = g.lw.logic->random().callLog();
	REQUIRE(log.size() >= 1);
	CHECK(log[0].lo == 1);
	CHECK(log[0].hi == 8);
	CHECK(g.counter("Alice/r") == log[0].result);
}

TEST_CASE("script engine: delay evaluation, difficulty flags, the state hash follows the script state")
{
	ScriptGame g;
	auto delayed = script("delayed", { { cond("CONDITION_TRUE", {}) } }, { act("INCREMENT_COUNTER", { ip(0, 1), sp(4, "d") }) });
	delayed->delayEvaluationSeconds = 1; // RW 0x60A1A6: next evaluation 5 frames later
	g.lists[1].items.push_back(item(std::move(delayed)));
	auto hardOnly = script("hard", { { cond("CONDITION_TRUE", {}) } }, { act("INCREMENT_COUNTER", { ip(0, 1), sp(4, "h") }) });
	hardOnly->easy = hardOnly->normal = false;
	g.lists[1].items.push_back(item(std::move(hardOnly)));
	g.start();
	const std::uint32_t h0 = g.lw.logic->computeStateHash();
	for (int i = 0; i < 6; ++i)
	{
		g.frame();
	}
	// frames 1 and 6
	CHECK(g.counter("Alice/d") == 2);
	// Alice has no AI: the game's difficulty (RW 0x6AA61B -> ScriptEngine + 0x1A5C4; 1 normal unless set) skips the hard-only script
	CHECK(g.counter("Alice/h") == -999);
	CHECK(g.lw.logic->computeStateHash() != h0);
}

TEST_CASE("script conditions: player parameters, START_POSITION_IS, PLAYER_HAS_CREDITS, COUNTER_COUNTER, pointInTrigger (RW 0x758F7C, 0x7E5707, 0x7E4B36, 0x70CD24)")
{
	ScriptGame g;
	g.start();
	ScriptEngine &e = g.engine();
	auto names = [&](const std::string &p) {
		std::string s;
		for (Player *pl : ScriptConditions::players(e, p))
		{
			s += pl->getPlayerName() + ",";
		}
		return s;
	};
	CHECK(names("<All Players>") == ",Alice,Bob,");
	CHECK(names("Bob") == "Bob,");
	CHECK(names("<Local Player>") == "Alice,");
	CHECK(names("<Local Player's Enemies>") == "Bob,");
	CHECK(names("<Local Player's Allies incl Self>") == "Alice,");
	CHECK(names("Nobody").empty());
	CHECK(e.stats().notes.count("***Invalid Player name (Nobody)***") == 1);
	// a trigger square 0..10 with a point on the edge
	TriggerArea t;
	t.points = { { 0.0f, 0.0f }, { 10.0f, 0.0f }, { 10.0f, 10.0f }, { 0.0f, 10.0f } };
	CHECK(ScriptConditions::pointInTrigger(t, 5.0f, 5.0f));
	CHECK_FALSE(ScriptConditions::pointInTrigger(t, 11.0f, 5.0f));
	CHECK_FALSE(ScriptConditions::pointInTrigger(t, 5.0f, 0.0f)); // a.y < p.y is strict
	CHECK(ScriptConditions::pointInTrigger(t, 5.0f, 10.0f));      // p.y <= b.y
	// conditions evaluated through the engine for player Alice (the money: the fixture's skirmish setup)
	g.lists[1].items.push_back(item(script("credits", { { cond("PLAYER_HAS_CREDITS", { ip(0, 100), ip(6, 0), sp(11, "<This Player>") }) } },
		{ act("SET_FLAG", { sp(5, "rich"), ip(8, 1) }) })));
	g.lists[1].items.push_back(item(script("start", { { cond("START_POSITION_IS", { sp(11, "<This Player>"), ip(0, 1) }) } },
		{ act("SET_FLAG", { sp(5, "start1"), ip(8, 1) }) }, { act("SET_FLAG", { sp(5, "notStart1"), ip(8, 1) }) })));
	g.lists[1].items.push_back(item(script("cc", { { cond("COUNTER_COUNTER", { sp(4, "missing"), ip(6, 2), sp(4, "alsoMissing") }) } },
		{ act("SET_FLAG", { sp(5, "zeroEqual"), ip(8, 1) }) })));
	g.start();
	g.frame();
	const std::uint32_t money = g.lw.players.findPlayerWithName("Alice")->getMoney()->countMoney();
	CHECK((*g.engine().findFlag("Alice/rich")) == (100 < (int)money));
	CHECK(g.engine().findFlag("Alice/start1") != nullptr); // Alice's start index is 0
	CHECK(g.engine().findFlag("Alice/zeroEqual") != nullptr);
}

TEST_CASE("script actions: PLAYER_SET_MONEY / GIVE_MONEY, PLAYER_RELATES_PLAYER, client requests and end requests, unported actions are counted")
{
	ScriptGame g;
	g.lists[1].items.push_back(item(script("once", { { cond("CONDITION_TRUE", {}) } },
		{ act("PLAYER_SET_MONEY", { sp(11, "<This Player>"), ip(0, 1000) }), act("PLAYER_GIVE_MONEY", { sp(11, "<This Player>"), ip(0, -300) }),
			act("PLAYER_RELATES_PLAYER", { sp(11, "Alice"), sp(11, "Bob"), ip(19, 2) }), act("SHOW_MILITARY_CAPTION", { sp(25, "SCRIPT:X"), rp(3.0f) }),
			act("VICTORY", {}), act("GATE_OPEN", { sp(14, "nobody") }) },
		{}, true)));
	g.start();
	g.frame();
	Player *alice = g.lw.players.findPlayerWithName("Alice");
	Player *bob = g.lw.players.findPlayerWithName("Bob");
	CHECK(alice->getMoney()->countMoney() == 700);
	CHECK(alice->getRelationship(bob) == ALLIES);
	std::vector<ScriptClientRequest> reqs = g.engine().takeClientRequests();
	REQUIRE(reqs.size() == 1);
	CHECK(reqs[0].action == "SHOW_MILITARY_CAPTION");
	CHECK(reqs[0].playerIndex == alice->getPlayerIndex());
	CHECK(reqs[0].script == "once");
	REQUIRE(g.engine().endRequests().size() == 1);
	CHECK(g.engine().endRequests()[0].victory);
	CHECK(g.engine().stats().unportedActions.count("GATE_OPEN") == 1);
	bool found = false;
	for (const std::string &s : g.lw.logic->report().stops)
	{
		found = found || s.find("[S-1180] script action not ported (did nothing): GATE_OPEN x1") != std::string::npos;
	}
	CHECK(found);
}

TEST_CASE("script parse: the RotWK re-match (RW 0x7B776D / 0x7B68F9): by name, unknown names, parameter counts, heals, the area-condition surfaces rewrite")
{
	using namespace maptest;
	Toc t;
	auto param = [](W &w, int type, int i, float r, const std::string &str) { w.i32(type).i32(i).f32(r).astr(str); };
	auto condition = [&](int stored, const std::string &name, int version, const std::function<void(W &)> &params, int count, int flagA) {
		W w;
		w.i32(stored);
		if (version >= 4)
		{
			w.i32(t.key(name, 3));
		}
		w.i32(count);
		params(w);
		if (version >= 5)
		{
			w.i32(flagA).i32(0);
		}
		return chunk(t, "Condition", version, w.b);
	};
	auto action = [&](const char *label, int stored, const std::string &name, const std::function<void(W &)> &params, int count, int tail) {
		W w;
		w.i32(stored).i32(t.key(name, 3)).i32(count);
		params(w);
		w.i32(tail);
		return chunk(t, label, 3, w.b);
	};
	auto none = [](W &) {};
	// conditions: COUNTER stored at a wrong ordinal (9) -> 1; an unknown name -> CONDITION_FALSE (0); a v3 record (no name) -> 0; TEAM_INSIDE_AREA_PARTIALLY
	// v5 with surfaces 3 -> the last parameter becomes (SURFACES_ALLOWED, 0); a disabled (flagA 0) TRUE
	Bytes conds = cat(cat(cat(condition(9, "COUNTER", 5, [&](W &w) { param(w, 4, 0, 0, "c"); param(w, 6, 2, 0, ""); param(w, 0, 1, 0, ""); }, 3, 1),
							  condition(1, "NOT_A_CONDITION", 5, none, 0, 1)),
						  cat(condition(3, "", 3, none, 0, 1),
							  condition(7, "TEAM_INSIDE_AREA_PARTIALLY", 5, [&](W &w) { param(w, 3, 0, 0, "team"); param(w, 9, 0, 0, "area"); param(w, 37, 3, 0, ""); }, 3, 1))),
		condition(3, "CONDITION_TRUE", 5, none, 0, 0));
	// actions: ENABLE_SCRIPT stored at 3 -> 8; an unknown name -> NO_OP; SET_FLAG with one parameter -> NO_OP; MOVE_CAMERA_TO with 3 parameters
	// healed to 5 (two REALs); an OBJECT_TYPE parameter in an OBJECT_TYPE_LIST slot becomes 61; a disabled (tail 0) action
	Bytes acts = cat(cat(cat(action("ScriptAction", 3, "ENABLE_SCRIPT", [&](W &w) { param(w, 2, 0, 0, "x"); }, 1, 1),
							 action("ScriptAction", 3, "GONE_ACTION", none, 0, 1)),
						 cat(action("ScriptAction", 1, "SET_FLAG", [&](W &w) { param(w, 5, 0, 0, "f"); }, 1, 1),
							 action("ScriptAction", 14, "MOVE_CAMERA_TO", [&](W &w) { param(w, 51, 0, 0, "wp"); param(w, 1, 0, 2.0f, ""); param(w, 1, 0, 0.0f, ""); }, 3, 1))),
		action("ScriptActionFalse", 132, "SET_ATTACK_PRIORITY_THING", [&](W &w) { param(w, 28, 0, 0, "set"); param(w, 15, 0, 0, "Thing"); param(w, 0, 5, 0, ""); }, 3, 0));
	W sh;
	sh.astr("S").astr("").astr("").astr("").u8(1).u8(0).u8(1).u8(1).u8(1).u8(0).i32(0);
	Bytes scr = chunk(t, "Script", 2, cat(cat(sh.b, chunk(t, "OrCondition", 1, conds)), acts));
	Bytes psl = chunk(t, "PlayerScriptsList", 6, chunk(t, "ScriptList", 1, scr));
	LoadedMap m;
	std::string err;
	MapReadOptions opt;
	REQUIRE_MESSAGE(MapReader::load(file(t, psl), "synthetic.map", opt, m, &err), err);
	REQUIRE(m.playerScripts.lists.size() == 1);
	REQUIRE(m.playerScripts.lists[0].items.size() == 1);
	const Script &s = *m.playerScripts.lists[0].items[0].script;
	REQUIRE(s.orConditions.size() == 1);
	const std::vector<ScriptCondition> &c = s.orConditions[0].conditions;
	REQUIRE(c.size() == 5);
	CHECK(c[0].type == 9);
	CHECK(c[0].resolved == 1);
	CHECK(c[1].resolved == 0);
	CHECK(c[2].resolved == 0);
	CHECK(c[3].resolved == 7);
	REQUIRE(c[3].params.size() == 3);
	CHECK(c[3].params[2].type == 37);
	CHECK(c[3].params[2].intValue == 0);
	CHECK(c[4].resolved == 3);
	CHECK_FALSE(c[4].enabled);
	CHECK(c[0].enabled);
	REQUIRE(s.actions.size() == 4);
	CHECK(s.actions[0].resolved == 8);
	CHECK(s.actions[1].resolved == 5);
	CHECK(s.actions[2].resolved == 5);
	CHECK(s.actions[3].resolved == 14);
	REQUIRE(s.actions[3].params.size() == 5);
	CHECK(s.actions[3].params[3].type == 1);
	CHECK(s.actions[3].params[4].type == 1);
	REQUIRE(s.falseActions.size() == 1);
	CHECK(s.falseActions[0].resolved == 132);
	CHECK(s.falseActions[0].params[1].type == 61);
	CHECK_FALSE(s.falseActions[0].enabled);
}

TEST_CASE("script census: every script record of the pure 2.01 mount resolves as the independent census says (tools/script/script_census.py)")
{
	retailtest::Mount *mount = retailtest::pureMount();
	if (!mount)
	{
		retailtest::printSkip("script census");
		return;
	}
	REQUIRE_MESSAGE(mount->fs != nullptr, mount->error);
	std::vector<unsigned char> bytes;
	std::string err;
	REQUIRE_MESSAGE(retailtest::readLocalFile(retailtest::dataDir() + "/script-census.json", bytes, &err), err);
	JsonValue census;
	REQUIRE_MESSAGE(JsonValue::parse(std::string(bytes.begin(), bytes.end()), census, &err), err);

	FilenameList list;
	mount->fs->getFileListInDirectory("", "", "*.map", list, true);
	FilenameList scb;
	mount->fs->getFileListInDirectory("", "", "*.scb", scb, true);
	for (const std::string &s : scb)
	{
		list.insert(s);
	}
	std::map<std::string, std::map<std::string, std::int64_t>> uses; // kind -> name -> uses
	std::int64_t scripts = 0, groups = 0, rematched = 0, filesWithScripts = 0;
	std::vector<std::uint8_t> data;
	for (const std::string &path : list)
	{
		REQUIRE_MESSAGE(mount->fs->readFile(path, data, &err), err);
		if (data.empty())
		{
			continue;
		}
		LoadedMap m;
		MapReadOptions opt;
		REQUIRE_MESSAGE(MapReader::load(data, path, opt, m, &err), path << ": " << err);
		std::int64_t fileScripts = 0;
		std::function<void(const std::vector<ScriptItem> &)> walk = [&](const std::vector<ScriptItem> &items) {
			for (const ScriptItem &it : items)
			{
				if (it.group)
				{
					++groups;
					walk(it.group->items);
					continue;
				}
				++scripts;
				++fileScripts;
				for (const OrCondition &o : it.script->orConditions)
				{
					for (const ScriptCondition &c : o.conditions)
					{
						uses["condition"][ScriptTemplates::condition(c.resolved)->name]++;
						rematched += c.resolved != c.type ? 1 : 0;
					}
				}
				for (const ScriptActionRec &a : it.script->actions)
				{
					uses["action"][ScriptTemplates::action(a.resolved)->name]++;
					rematched += a.resolved != a.type ? 1 : 0;
				}
				for (const ScriptActionRec &a : it.script->falseActions)
				{
					uses["falseAction"][ScriptTemplates::action(a.resolved)->name]++;
					rematched += a.resolved != a.type ? 1 : 0;
				}
			}
		};
		for (const ScriptList &l : m.playerScripts.lists)
		{
			walk(l.items);
		}
		filesWithScripts += fileScripts > 0 ? 1 : 0;
	}
	const JsonValue &totals = J(census, "totals");
	CHECK(scripts == N(J(totals, "scripts")));
	CHECK(groups == N(J(totals, "groups")));
	CHECK(filesWithScripts == N(J(totals, "filesWithScripts")));
	CHECK(rematched == N(J(totals, "rematched")));
	for (const char *kind : { "condition", "action", "falseAction" })
	{
		std::map<std::string, std::int64_t> want;
		for (const auto &kv : J(census, kind).object)
		{
			want[kv.first] = N(J(kv.second, "uses"));
		}
		for (const auto &kv : want)
		{
			CHECK_MESSAGE(uses[kind][kv.first] == kv.second, kind << " " << kv.first);
		}
		for (const auto &kv : uses[kind])
		{
			CHECK_MESSAGE(want.count(kv.first) == 1, kind << " " << kv.first << " (not in the census)");
		}
	}
	// pins of the census itself (independent numbers, so a regenerated census cannot drift silently)
	CHECK(N(J(totals, "scripts")) == 9061);
	CHECK(N(J(totals, "conditions")) == 14493);
	CHECK(N(J(totals, "actions")) == 28593);
	CHECK(N(J(totals, "falseActions")) == 422);
	CHECK(N(J(totals, "distinctConditionTypes")) == 106);
	CHECK(N(J(totals, "distinctActionTypes")) == 340);
	CHECK(N(J(totals, "rematched")) == 135);
	std::printf("  info: script census: %lld scripts, %lld groups; %zu condition types, %zu action types used\n", (long long)scripts, (long long)groups,
		uses["condition"].size(), uses["action"].size());
}

TEST_CASE("script camera director: MOVE_CAMERA_TO / RESET_CAMERA / ROTATE_CAMERA with ParabolicEase, letterbox, captions (ZH W3DView donor, S-1182)")
{
	// ParabolicEase (ZH ParabolicEase.cpp:75): no ease is linear; with in = out = 0.25 the middle runs at 4/3 speed
	ParabolicEase linear;
	linear.setEaseTimes(0.0f, 0.0f);
	CHECK(linear(0.5f) == doctest::Approx(0.5f));
	ParabolicEase e;
	e.setEaseTimes(0.25f, 0.25f);
	CHECK(e(0.0f) == doctest::Approx(0.0f));
	CHECK(e(0.25f) == doctest::Approx(0.25f / 1.5f));
	CHECK(e(0.5f) == doctest::Approx(0.5f));
	CHECK(e(1.0f) == doctest::Approx(1.0f));
	ScriptCameraDirector d;
	ScriptClientRequest move;
	move.action = "MOVE_CAMERA_TO";
	move.hasPosition = true;
	move.position = Coord3D{ 100.0f, 200.0f, 0.0f };
	move.params = { sp(51, "Cam"), rp(2.0f), rp(0.0f), rp(0.0f), rp(0.0f) };
	d.apply(move, Coord3D{ 0.0f, 0.0f, 0.0f }, 0.0f);
	d.update(1000.0);
	CHECK(d.moving());
	CHECK(d.target().x == doctest::Approx(50.0f));
	CHECK(d.target().y == doctest::Approx(100.0f));
	d.update(1000.0);
	CHECK_FALSE(d.moving());
	CHECK(d.target().x == 100.0f);
	ScriptClientRequest rot;
	rot.action = "ROTATE_CAMERA";
	rot.params = { rp(0.5f), rp(1.0f), rp(0.0f), rp(0.0f) };
	d.apply(rot, d.target(), 0.0f);
	d.update(1000.0);
	CHECK(d.angle() == doctest::Approx(3.14159265f));
	ScriptClientRequest lb;
	lb.action = "CAMERA_LETTERBOX_BEGIN";
	d.apply(lb, d.target(), d.angle());
	CHECK(d.letterbox());
	ScriptClientRequest cap;
	cap.action = "SHOW_MILITARY_CAPTION";
	cap.params = { sp(25, "SCRIPT:X"), rp(3.0f) };
	d.apply(cap, d.target(), d.angle());
	CHECK(d.captionLabel() == "SCRIPT:X");
	d.update(2999.0);
	CHECK(d.captionLabel() == "SCRIPT:X");
	d.update(2.0);
	CHECK(d.captionLabel().empty());
}

TEST_CASE("script engine: stop lines (S-1180 .. S-1189) reach the logic report once a game's scripts are loaded")
{
	const std::vector<std::string> lines = ScriptEngine::stopLines();
	std::set<std::string> ids;
	for (const std::string &l : lines)
	{
		ids.insert(l.substr(0, 8));
	}
	CHECK(ids == std::set<std::string>{ "[S-1180]", "[S-1181]", "[S-1182]", "[S-1183]", "[S-1184]", "[S-1185]", "[S-1186]", "[S-1187]", "[S-1188]", "[S-1189]" });
	ScriptGame g;
	bool before = false;
	for (const std::string &s : g.lw.logic->report().stops)
	{
		before = before || s.rfind("[S-1180]", 0) == 0;
	}
	CHECK_FALSE(before); // a game without map scripts reports none
	g.start();
	size_t after = 0;
	for (const std::string &s : g.lw.logic->report().stops)
	{
		after += s.rfind("[S-118", 0) == 0 ? 1 : 0;
	}
	CHECK(after == lines.size());
}

TEST_CASE("script actions: OBJECTLIST_ADDOBJECTTYPE / REMOVEOBJECTTYPE (RW 0x759D77): lists made on first add, a type once, an empty list removed")
{
	ScriptGame g;
	g.lists[1].items.push_back(item(script("lists", { { cond("CONDITION_TRUE", {}) } },
		{ act("OBJECTLIST_ADDOBJECTTYPE", { sp(48, "L"), sp(15, "A") }), act("OBJECTLIST_ADDOBJECTTYPE", { sp(48, "L"), sp(15, "B") }),
			act("OBJECTLIST_ADDOBJECTTYPE", { sp(48, "L"), sp(15, "A") }), act("OBJECTLIST_ADDOBJECTTYPE", { sp(48, "M"), sp(15, "C") }),
			act("OBJECTLIST_REMOVEOBJECTTYPE", { sp(48, "M"), sp(15, "C") }) },
		{}, true)));
	g.start();
	g.frame();
	const std::vector<std::string> *l = g.engine().findObjectList("L");
	REQUIRE(l != nullptr);
	CHECK(*l == std::vector<std::string>{ "A", "B" });
	CHECK(g.engine().findObjectList("M") == nullptr);
}

TEST_CASE("script r2: in a LAN game the <Local Player...> forms answer for the current script player, so two peers with different local players hash alike (S-1181)")
{
	ScriptGame peers[2];
	for (int k = 0; k < 2; ++k)
	{
		ScriptGame &g = peers[k];
		g.lw.logic->economy().context().gameMode = EconomyContext::MODE_LAN;
		g.lw.players.setLocalPlayer(g.lw.players.findPlayerWithName(k == 0 ? "Alice" : "Bob"));
		g.lists[1].items.push_back(item(script("local", { { cond("CONDITION_TRUE", {}) } },
			{ act("PLAYER_SET_MONEY", { sp(11, "<Local Player>"), ip(0, 777) }), act("PLAYER_SET_MONEY", { sp(11, "<Local Player's Enemies>"), ip(0, 333) }) },
			{}, true)));
		g.start();
		g.frame();
	}
	CHECK(peers[0].lw.logic->computeStateHash() == peers[1].lw.logic->computeStateHash());
	CHECK(peers[1].lw.players.findPlayerWithName("Alice")->getMoney()->countMoney() == 777u);
	CHECK(peers[1].lw.players.findPlayerWithName("Bob")->getMoney()->countMoney() == 333u);
	// a single-machine game (skirmish) keeps retail's local player
	ScriptGame sk;
	sk.lw.players.setLocalPlayer(sk.lw.players.findPlayerWithName("Bob"));
	sk.lists[1].items.push_back(item(script("local", { { cond("CONDITION_TRUE", {}) } }, { act("PLAYER_SET_MONEY", { sp(11, "<Local Player>"), ip(0, 777) }) }, {}, true)));
	sk.start();
	sk.frame();
	CHECK(sk.lw.players.findPlayerWithName("Bob")->getMoney()->countMoney() == 777u);
}

TEST_CASE("script r2: a qualified CALL_SUBROUTINE runs with the called side's counters and flags (RW 0x60BFBD -> RW 0x604243)")
{
	ScriptGame g;
	auto sub = script("subS", { { cond("CONDITION_TRUE", {}) } }, { act("INCREMENT_COUNTER", { ip(0, 1), sp(4, "x") }), act("SET_FLAG", { sp(5, "f"), ip(8, 1) }) });
	sub->isSubroutine = true;
	g.lists[1].items.push_back(item(std::move(sub)));
	std::vector<ScriptItem> grp;
	grp.push_back(item(script("inGroup", { { cond("CONDITION_TRUE", {}) } }, { act("INCREMENT_COUNTER", { ip(0, 10), sp(4, "x") }) })));
	g.lists[1].items.push_back(group("SubG", true, true, std::move(grp)));
	g.lists[2].items.push_back(item(script("caller", { { cond("CONDITION_TRUE", {}) } },
		{ act("CALL_SUBROUTINE", { sp(13, "Alice/subS") }), act("CALL_SUBROUTINE", { sp(13, "Alice/SubG") }), act("INCREMENT_COUNTER", { ip(0, 100), sp(4, "x") }) },
		{}, true)));
	g.start();
	g.frame();
	CHECK(g.counter("Alice/x") == 11);
	CHECK(g.counter("Bob/x") == 100); // the caller's side is restored after the calls
	CHECK(g.engine().findFlag("Alice/f") != nullptr);
	CHECK(g.engine().findFlag("Bob/f") == nullptr);
}

TEST_CASE("script r2: the first update rebuilds the name cache from the live objects (RW 0x60A3CB): a name given before the scripts were loaded is found")
{
	ScriptGame g;
	REQUIRE(g.lw.w.load("Object Named\n  Draw = W3DDefaultDraw ModuleTag_Draw\n  End\nEnd\n", INI_LOAD_OVERWRITE, "named.ini").empty());
	Object *o = g.lw.make("Named", g.lw.teamOf("Alice"));
	REQUIRE(o != nullptr);
	o->setName("Early");
	g.lists[1].items.push_back(item(script("check", { { cond("NAMED_CREATED", { sp(14, "Early") }) } }, { act("SET_FLAG", { sp(5, "seen"), ip(8, 1) }) }, {}, true)));
	g.start(); // newGame clears the cache
	CHECK(g.engine().getUnitNamed("Early") == nullptr);
	const std::uint32_t before = g.lw.logic->computeStateHash();
	g.frame();
	CHECK(g.engine().getUnitNamed("Early") == o);
	bool *f = g.engine().findFlag("Alice/seen");
	REQUIRE(f != nullptr);
	CHECK(*f);
	CHECK(before != g.lw.logic->computeStateHash());
}

TEST_CASE("script r2: ordinal 385 stays whatever name it carries (its template's name is the marker spelling); SPEECH_PLAY's upgrade appends TRUE, MOVIE_PLAY_FULLSCREEN's FALSE")
{
	using namespace maptest;
	Toc t;
	auto param = [](W &w, int type, int i, const std::string &str) { w.i32(type).i32(i).f32(0.0f).astr(str); };
	auto action = [&](int stored, const std::string &name, int count) {
		W w;
		w.i32(stored).i32(t.key(name, 3)).i32(count);
		for (int i = 0; i < count; ++i)
		{
			param(w, 0, i, "p");
		}
		w.i32(1);
		return chunk(t, "ScriptAction", 3, w.b);
	};
	Bytes acts = cat(cat(action(385, "SOME_OTHER_NAME", 5), action(385, "SOME_OTHER_NAME", 4)), cat(action(85, "SPEECH_PLAY", 1), action(82, "MOVIE_PLAY_FULLSCREEN", 1)));
	W sh;
	sh.astr("S").astr("").astr("").astr("").u8(1).u8(0).u8(1).u8(1).u8(1).u8(0).i32(0);
	Bytes psl = chunk(t, "PlayerScriptsList", 6, chunk(t, "ScriptList", 1, chunk(t, "Script", 2, cat(sh.b, acts))));
	LoadedMap m;
	std::string err;
	MapReadOptions opt;
	REQUIRE_MESSAGE(MapReader::load(file(t, psl), "synthetic.map", opt, m, &err), err);
	const Script &s = *m.playerScripts.lists[0].items[0].script;
	REQUIRE(s.actions.size() == 4);
	CHECK(s.actions[0].resolved == 385);
	CHECK(s.actions[1].resolved == 5); // the count still decides
	CHECK(s.actions[2].resolved == 85);
	REQUIRE(s.actions[2].params.size() == 2);
	CHECK(s.actions[2].params[1].type == 8);
	CHECK(s.actions[2].params[1].intValue == 1);
	CHECK(s.actions[3].resolved == 82);
	REQUIRE(s.actions[3].params.size() == 2);
	CHECK(s.actions[3].params[1].intValue == 0);
}

TEST_CASE("script r2: a reused EndGameController shows the next game's script end screen (m_scriptEndsSeen resets in start())")
{
	EndGameController c;
	for (int game = 0; game < 2; ++game)
	{
		c.start(2, 3);
		EndGameView v;
		v.frame = 10;
		v.localPlayerIndex = 1;
		v.logicScripts = true;
		v.scriptEnds = { true };
		c.update(&v, 10, 1000.0 * game * 100);
		const std::vector<EndGameRequest> r = c.takeRequests();
		bool shown = false;
		for (const EndGameRequest &q : r)
		{
			shown = shown || (q.kind == EndGameRequest::SHOW_END_GAME && q.text == "APT:EndVictorious");
		}
		CHECK_MESSAGE(shown, "game " << game);
	}
}
