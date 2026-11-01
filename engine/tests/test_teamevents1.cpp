// OpenBFME unit tests: lane TEAMEVENTS-1, teamExecutesActionsOnCreate. A new team of a prototype with the flag runs its production-condition script's
// actions (TeamFactory::createInactiveTeam RW 0x7A6E8E: the lookup RW 0x758ECB, the action run RW 0x60D053 with no team), the singleton's existing team
// too. GPL-3.0.
//
// Expected values follow the RotWK functions cited at each check (Team.cpp, ScriptEngine.cpp), never the engine's own output. The generic hooks a team
// fires are the prototype's copies (RW 0x7A1759, made once, the original's active flag copied then) gated per team (Team + 0x70, all set by the
// constructor RW 0x7A6E63, a one-shot hook's cleared when it fires): a new team fires a one-shot hook again, and a hook whose script was inactive when
// the copy was made never fires; the production-condition actions run before the copy exists, so their ENABLE_SCRIPT arms it.

#include "doctest.h"
#include "CampaignTestUtil.h"
#include "LogicTestUtil.h"
#include "ScriptTestUtil.h"

#include "Common/Dict.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"

#include <string>

using namespace scripttest;

namespace
{

std::unique_ptr<Script> subroutine(std::unique_ptr<Script> s)
{
	s->isSubroutine = true;
	return s;
}

Dict teamDict(const std::string &condition, bool executeActions, const std::string &hook)
{
	Dict d;
	d.setAsciiString("teamProductionCondition", condition);
	d.setBool("teamExecutesActionsOnCreate", executeActions);
	d.setAsciiString("teamGenericScriptHook0", hook);
	return d;
}

void frames(ScriptGame &g, int n)
{
	for (int i = 0; i < n; ++i)
	{
		g.frame();
	}
}

} // namespace

TEST_CASE("teamevents1 teamExecutesActionsOnCreate (RW 0x7A6E8E): each new team runs its production condition's actions (inactive script, false "
		  "conditions, the owner's side); its ENABLE_SCRIPT arms the team's one-shot hook, which a second team fires again; without the flag nothing runs")
{
	ScriptGame g;
	// Alice's side: the production conditions (never true, the first inactive: RW 0x60D053 runs the actions whatever the script's state) and the hooks
	// (inactive subroutines, one-shot)
	g.lists[1].items.push_back(item(script("Create Builder", { { cond("CONDITION_FALSE", {}) } },
		{ act("ENABLE_SCRIPT", { sp(2, "Rebuild") }), act("INCREMENT_COUNTER", { ip(0, 1), sp(4, "Created") }) }, {}, false, false)));
	g.lists[1].items.push_back(item(subroutine(script("Rebuild", { { cond("CONDITION_TRUE", {}) } }, { act("INCREMENT_COUNTER", { ip(0, 1), sp(4, "Fired") }) },
		{}, true, false))));
	g.lists[1].items.push_back(item(script("Create Plain", { { cond("CONDITION_FALSE", {}) } },
		{ act("ENABLE_SCRIPT", { sp(2, "Rebuild Plain") }), act("INCREMENT_COUNTER", { ip(0, 1), sp(4, "Plain Created") }) })));
	g.lists[1].items.push_back(item(subroutine(script("Rebuild Plain", { { cond("CONDITION_TRUE", {}) } },
		{ act("INCREMENT_COUNTER", { ip(0, 1), sp(4, "Plain Fired") }) }, {}, true, false))));
	g.start();
	TeamFactory &teams = g.lw.players.teams();
	Player *alice = g.lw.players.findPlayerWithName("Alice");
	REQUIRE(alice != nullptr);
	const Dict builders = teamDict("Create Builder", true, "Rebuild");
	const Dict plain = teamDict("Create Plain", false, "Rebuild Plain");
	TeamPrototype *proto = teams.initTeam("Builders", alice, false, &builders);
	TeamPrototype *plainProto = teams.initTeam("Plain", alice, false, &plain);
	ScriptEngine &e = g.engine();
	CHECK(g.counter("Alice/Created") == -999); // a non-singleton prototype makes no team at its init (RW 0x7A73DB)
	CHECK_FALSE(e.scriptActive("Alice/Rebuild"));

	// the first team: the actions run at its creation (RW 0x7A6F65), before it is activated (RW 0x7A6FCB)
	Team *t1 = teams.createTeam(proto, true);
	CHECK(g.counter("Alice/Created") == 1);
	CHECK(e.scriptActive("Alice/Rebuild"));
	CHECK(e.stats().teamCreateActionsRun == 1);
	frames(g, 10);
	CHECK(t1->scriptState().ready);
	CHECK(g.counter("Alice/Fired") == 1);
	CHECK_FALSE(t1->scriptState().attemptGeneric[0]); // one-shot: Team + 0x70 cleared (RW 0x7A272B)
	frames(g, 10);
	CHECK(g.counter("Alice/Fired") == 1);

	// a second team of the prototype: its own attempt flags, the prototype's (active) copy: the hook fires again
	Team *t2 = teams.createTeam(proto, true);
	CHECK(t2 != t1);
	CHECK(g.counter("Alice/Created") == 2);
	frames(g, 10);
	CHECK(g.counter("Alice/Fired") == 2);

	// the same team without the flag: no action run, the hook's script stays inactive, so the prototype's copy is inactive and never fires (RW 0x7A285D)
	Team *p1 = teams.createTeam(plainProto, true);
	frames(g, 10);
	CHECK(p1->scriptState().ready);
	CHECK(g.counter("Alice/Plain Created") == -999);
	CHECK(g.counter("Alice/Plain Fired") == -999);
	CHECK_FALSE(e.scriptActive("Alice/Rebuild Plain"));
	CHECK(e.stats().teamCreateActionsRun == 2);
}

TEST_CASE("teamevents1 a singleton's existing team runs the actions again (RW 0x7A6ED8); a team made before the scripts runs them when the engine starts; "
		  "a missing production condition runs nothing and is reported")
{
	ScriptGame g;
	g.lists[1].items.push_back(item(script("Create Guards", {}, { act("INCREMENT_COUNTER", { ip(0, 1), sp(4, "Guards") }) })));
	TeamFactory &teams = g.lw.players.teams();
	Player *alice = g.lw.players.findPlayerWithName("Alice");
	REQUIRE(alice != nullptr);
	const Dict guards = teamDict("Create Guards", true, "");
	const Dict missing = teamDict("No Such Script", true, "");
	// a singleton's init makes its team (RW 0x7A73DB -> 0x7A6E8E); the engine has no scripts yet: the run waits for its start
	TeamPrototype *proto = teams.initTeam("Guards", alice, true, &guards);
	TeamPrototype *missingProto = teams.initTeam("Lost", alice, true, &missing);
	CHECK(g.counter("Alice/Guards") == -999);
	g.start();
	CHECK(g.counter("Alice/Guards") == 1);
	ScriptEngine &e = g.engine();
	CHECK(e.stats().teamCreateActionsRun == 1);
	CHECK(e.stats().notes.count("teamProductionCondition script not found: 'No Such Script' (team Lost)") == 1);

	// the singleton's existing team (RW 0x7A6ECB: + 0x334 set): the same team, the actions run again (CREATE_REINFORCEMENT_TEAM RW 0x7C8770 takes it)
	Team *first = proto->getFirstTeam();
	CHECK(teams.createTeam(proto) == first);
	CHECK(g.counter("Alice/Guards") == 2);
	CHECK(teams.createTeam(missingProto) == missingProto->getFirstTeam());
	CHECK(e.stats().teamCreateActionsRun == 2);
}

TEST_CASE("teamevents1 retail MAP ANG Amon Sul: a 'Barracks 1 Rebuilding Team' (teamExecutesActionsOnCreate, made by Arnor's AI in retail) runs "
		  "'Create Barracks 1 Builder' (ENABLE_SCRIPT Rebuild Barracks 1) and each new team fires its one-shot rebuild hook again")
{
	OPENBFME_REQUIRE_START(s);
	campaigntest::Mission m;
	m.load(*s, "map ang amon sul");
	REQUIRE(m.game->mapScriptsRunning());
	ScriptEngine &e = m.engine();
	TeamFactory &teams = m.logic().players().teams();
	TeamPrototype *proto = teams.findTeamPrototype("PlyrArnor", "Barracks 1 Rebuilding Team");
	REQUIRE(proto != nullptr);
	CHECK(proto->templateScripts().executeActionsOnCreate);
	CHECK(proto->templateScripts().productionCondition == "Create Barracks 1 Builder");
	CHECK(proto->templateScripts().generic[0] == "Rebuild Barracks 1");
	CHECK(proto->teams().empty()); // the AI builds it (teamProductionPriority 50): no team at the start
	ScriptEngine::RScript *hook = e.findScript("PlyrArnor/Rebuild Barracks 1");
	REQUIRE(hook != nullptr);
	CHECK(hook->active); // retail's map starts it active: the ENABLE_SCRIPT only matters once something disabled it
	hook->active = false; // stand-in for a mod that does: E-4 is what arms it again

	// the barracks gone (the hook's conditions: Flag - Barracks 1 Built / UC both false, set by the map's "Barracks 1 Built?" scripts)
	m.killNamed("AS Barracks 1");
	REQUIRE(m.until([&] { return !m.flag("PlyrArnor/Flag - Barracks 1 Built") && !m.flag("PlyrArnor/Flag - Barracks 1 UC"); }, 50));

	// the hook fires once per team: its one-shot clears that team's Team + 0x70 (RW 0x7A272B) when its conditions held (RW 0x609D97)
	Team *t1 = teams.createTeam(proto, true); // the AI's RW 0x8F4B0D -> 0x7A6E8E (the AI's team production is not ported)
	CHECK(e.stats().teamCreateActionsRun == 1);
	CHECK(hook->active);
	REQUIRE(m.until([&] { return !t1->scriptState().attemptGeneric[0]; }, 50));

	// the porter lost: a new team of the prototype (TEAM_DELETE by "Despawn Barracks 1 Builder" only once the barracks stand again)
	Team *t2 = teams.createTeam(proto, true);
	CHECK(t2 != t1);
	CHECK(e.stats().teamCreateActionsRun == 2);
	CHECK(t2->scriptState().attemptGeneric[0]);
	REQUIRE(m.until([&] { return !t2->scriptState().attemptGeneric[0]; }, 50));
}

TEST_CASE("teamevents1 the runs waiting for the scripts keep their creation order, only flagged prototypes queue, and TeamFactory::clear drops them")
{
	TeamFactory f;
	const Dict a = teamDict("Make A", true, "");
	const Dict b = teamDict("Make B", true, "");
	const Dict off = teamDict("Make C", false, "");
	TeamPrototype *pb = f.initTeam("B", nullptr, true, &b);
	f.initTeam("C", nullptr, true, &off);
	TeamPrototype *pa = f.initTeam("A", nullptr, true, &a);
	f.createTeam(pb); // the singleton's existing team: queued again (RW 0x7A6ED8)
	const std::vector<TeamPrototype *> pending = f.takePendingCreateActions();
	REQUIRE(pending.size() == 3);
	CHECK(pending[0] == pb);
	CHECK(pending[1] == pa);
	CHECK(pending[2] == pb);
	CHECK(f.takePendingCreateActions().empty());
	f.initTeam("A2", nullptr, true, &a);
	f.clear();
	CHECK(f.takePendingCreateActions().empty());
}
