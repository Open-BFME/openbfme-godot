// OpenBFME unit tests: the acceptance stops of lane LUA-1 (S-120..S-129): each is reported at runtime by the mechanism docs/STOPS.md names,
// and each has its row (PLAN "Acceptance stops": report at runtime, pin in a test, register).

#include "doctest.h"

#include "LuaTestUtil.h"

#include <fstream>
#include <sstream>

#ifndef OPENBFME_DOCS_DIR
#define OPENBFME_DOCS_DIR "../docs"
#endif

using luatest::run;

TEST_CASE("stops S-120 and S-123: every state the engine opens reports the reconstructed fork and the assumed PC24")
{
	luatest::Rig r;
	r.load("", "<SageLuaScriptSection/>");
	CHECK(r.engine->reports().has("S-120", "[logic state]"));
	CHECK(r.engine->reports().has("S-120", "[drawable state]"));
	CHECK(r.engine->reports().has("S-123", "[logic state]"));
	CHECK(r.engine->reports().has("S-123", "[drawable state]"));
	CHECK(r.engine->reports().has("S-123", "x87 precision control 24"));
}

TEST_CASE("stops S-125: print output is dropped and reported while the gate is closed; open, it is kept")
{
	luatest::Rig r;
	r.load("", "<SageLuaScriptSection/>");
	CHECK(!r.engine->reports().has("S-125"));
	run(*r.engine->logic(), "print('hello')");
	CHECK(r.engine->reports().has("S-125", "options byte"));
	CHECK(r.engine->logic()->printed().empty());
	r.engine->logic()->setPrintEnabled(true);
	run(*r.engine->logic(), "print('hello')");
	CHECK(r.engine->logic()->printed() == "hello\n");
}

TEST_CASE("stops S-129: a game start reports the event sources of the other lanes; ObjectStatusEvent records are kept and reported")
{
	luatest::Rig r;
	r.load("", "<SageLuaScriptSection/>");
	CHECK(r.engine->reports().has("S-129", "OnCreated (RW 0x628882 GameLogic::sendObjectCreated)"));
	CHECK(r.engine->reports().has("S-129", "OnTeamEntered, OnTeamExited and DamageIncoming have no dispatch site in this binary"));
	luatest::Rig s;
	s.load("", "<SageLuaScriptSection><Events><ObjectStatusEvent Name=\"Burning\"><Conditions>+AFLAME -WET</Conditions></ObjectStatusEvent></Events></SageLuaScriptSection>");
	CHECK(s.engine->events().objectStatusEvents().size() == 1);
	CHECK(s.engine->reports().has("S-129", "ObjectStatusEvent Burning"));
	CHECK(s.engine->events().findEvent("Burning").kind == LuaEventRef::OBJECT_STATUS);
	// a status the name table does not have is a reported parse error
	luatest::Rig t;
	t.load("", "<SageLuaScriptSection><Events><ObjectStatusEvent Name=\"Bad\"><Conditions>+NO_SUCH_STATUS</Conditions></ObjectStatusEvent></Events></SageLuaScriptSection>");
	CHECK(t.engine->events().objectStatusEvents().empty());
	CHECK(t.engine->reports().has("S-127", "ObjectStatusEvent Bad"));
}

TEST_CASE("stops: LuaGameHost without a sink throws instead of dropping the stop (PLAN rule 10); the engine needs its three collaborators")
{
	struct Bare : LuaGameHost
	{
		bool findObject(int, LuaObjectInfo *) override { return false; }
		void poke() { unimplemented("x"); }
	} bare;
	CHECK_THROWS(bare.poke());
	LuaScriptEngine::Config c;
	CHECK_THROWS(LuaScriptEngine(c));
}

TEST_CASE("stops S-120..S-129 have a row in docs/STOPS.md that names the mechanism that reports them")
{
	std::ifstream f(std::string(OPENBFME_DOCS_DIR) + "/STOPS.md");
	REQUIRE(f.good());
	std::stringstream ss;
	ss << f.rdbuf();
	const std::string text = ss.str();
	for (int id = 120; id <= 129; ++id)
	{
		const std::string row = "| S-" + std::to_string(id) + " | code |";
		INFO(row);
		const size_t at = text.find(row);
		REQUIRE(at != std::string::npos);
		const std::string line = text.substr(at, text.find('\n', at) - at);
		CHECK(line.find("test_lua") != std::string::npos); // pinned by a Lua test file
	}
}
