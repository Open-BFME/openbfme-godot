// OpenBFME unit tests: the campaign missions after MAP ANG Angmar (lane SCRIPT-3): every RotWK campaign mission (LinearCampaign ANGMAR_CAMPAIGN,
// GOOD_CAMPAIGN, EVIL_CAMPAIGN of data\ini\linearcampaign*.ini) loads and runs its opening; MAP ANG Rhudaur plays to its victory by its own scripts.
// Retail runs (they SKIP when ROTWK_INSTALL / BFME2_INSTALL are unset). GPL-3.0.

#include "doctest.h"
#include "CampaignTestUtil.h"

#include "GameLogic/AI/AIHunt.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/AI/AIWaypointPath.h"
#include "Common/Science.h"

using campaigntest::Mission;

namespace
{

// the hunt state is running on some member of the team (the horde objects of a village)
bool teamHunting(Mission &m, const std::string &team)
{
	Team *t = ScriptConditions::team(m.engine(), team);
	if (!t)
	{
		return false;
	}
	for (Object *o : ScriptConditions::members(*t))
	{
		if (o->getAIUpdateInterface() && o->getAIUpdateInterface()->currentStateId() == (unsigned)AI_HUNT)
		{
			return true;
		}
	}
	return false;
}

Coord3D teamCentre(Mission &m, const std::string &team)
{
	Team *t = ScriptConditions::team(m.engine(), team);
	REQUIRE_MESSAGE(t != nullptr, team);
	REQUIRE_MESSAGE(t->getFirstMember() != nullptr, team);
	return *t->getFirstMember()->getPosition();
}

} // namespace

TEST_CASE("script3 rhudaur: MAP ANG Rhudaur plays from its intro to the victory by its own scripts")
{
	OPENBFME_REQUIRE_START(s);
	Mission m;
	m.load(*s, "map ang rhudaur");
	REQUIRE(m.game->mapScriptsRunning());
	const std::string me = m.logic().players().getLocalPlayer()->getPlayerName();
	Object *wk = m.unit("Witch King");
	REQUIRE(wk != nullptr);
	wk->getBodyModule()->setIndestructible(true); // the player keeps the hero alive while the helpers walk him around

	// 1. the intro cinematic: its timer chain, the camera moves, the captions; "Flag - Intro Cine Over" 2 s after the camera returns
	REQUIRE(m.until([&] { return m.flag("/Flag - Intro Cine Over"); }, 800));
	CHECK(m.requested("CAMERA_LETTERBOX_END"));
	CHECK(m.requested("SHOW_MILITARY_CAPTION", "SCRIPT:ANGRhudaurIntroText_03"));
	MESSAGE("intro over at frame " << m.frame());
	// "Map Setup": PLAYER_SCIENCE_AVAILABILITY(<Local Player>, SCIENCE_SummonShadeOfWolf / SCIENCE_Avalanche, Hidden) (RW 0x6AE412)
	Player *local = m.logic().players().getLocalPlayer();
	CHECK(local->isScienceHidden(TheScienceStore->getScienceFromInternalName("SCIENCE_SummonShadeOfWolf")));
	CHECK(local->isScienceHidden(TheScienceStore->getScienceFromInternalName("SCIENCE_Avalanche")));
	// HAS_FINISHED_AUDIO's length model over the retail audio (S-1187): the intro's first line (a voice file) lasts some seconds; no info: false
	std::int32_t ms = 0;
	bool picked = false;
	REQUIRE(m.engine().host() != nullptr);
	CHECK(m.engine().host()->audioLengthMs("MARhuda_AngmarOff01", ms, picked));
	CHECK(ms > 1000);
	CHECK(ms < 60000);
	CHECK_FALSE(m.engine().host()->audioLengthMs("SCRIPT3 no such event", ms, picked));

	// 2. Hwaldar's prison: a barricade falls -> Hwaldar joins (NAMED_TRANSFER_OWNERSHIP_PLAYER) -> Objective 02 -> "Hwaldar Freed"; his crew walks its
	// exact waypoint path (state 5, AIWaypointPath.cpp) into the trigger area and joins
	m.killNamed("Hwaldar Barricade 1");
	REQUIRE(m.until([&] { return m.unit("Hwaldar") && m.unit("Hwaldar")->getControllingPlayer() == local; }, 50));
	REQUIRE(m.until([&] { return m.flag(me + "/Hwaldar Freed"); }, 50));
	MESSAGE("Hwaldar freed at frame " << m.frame());
	m.step(2);
	Object *crew = m.unit("Hwaldar's Crew 1");
	REQUIRE(crew != nullptr);
	CHECK(crew->getAIUpdateInterface()->currentStateId() == (unsigned)AI_FOLLOW_WAYPOINT_PATH_EXACT_AS_INDIVIDUALS);
	const bool crewJoined = m.until([&] { return crew->getTeam() && crew->getTeam()->getName() == "teamPlyrAngmar"; }, 1500);
	CHECK(crewJoined);
	MESSAGE("Hwaldar's crew " << std::string(crewJoined ? "joined" : "did not join") << " at frame " << m.frame());

	// 3. the villages: the Witch King is seen at each (TEAM_DISCOVERED); the warriors hunt (TEAM_HUNT, the hunt state RW 0x741133) and are beaten
	for (const char *v : { "SW", "NW", "NE" })
	{
		const std::string team = std::string("PlyrWildmen/") + v + " Village Warriors";
		Coord3D at = teamCentre(m, team);
		at.x += 120.0f;
		m.place(*wk, at);
		const bool hunting = m.until([&] { return teamHunting(m, team); }, 100);
		CHECK_MESSAGE(hunting, team);
		MESSAGE(team << std::string(hunting ? " hunts" : " does not hunt") << " at frame " << m.frame());
		m.step(20);
		m.killTeam(team);
		REQUIRE(m.until([&] { return m.flag(std::string("/Flag - ") + v + " Village Cleared"); }, 100));
	}
	// SE: Hwaldar is free: the loyalists run away along their path; the village is cleared 5 s later
	m.place(*wk, teamCentre(m, "PlyrWildmen/SE Village Warriors"));
	REQUIRE(m.until([&] { return m.flag("/Flag - SE Village Cleared"); }, 200));
	REQUIRE(m.until([&] { return m.flag(me + "/Rebellion Raised"); }, 20));
	MESSAGE("rebellion raised at frame " << m.frame());

	// 4. the forts and King Argeleb: the objective appears 15 s after both objectives; the forts and the king fall -> "Mission Won" 6 s later
	REQUIRE(m.until([&] { return m.requested("SHOW_MISSION_OBJECTIVE"); }, 200));
	m.step(10);
	for (const char *n : { "North Fortress", "South Fortress", "King Argeleb" })
	{
		m.killNamed(n);
	}
	REQUIRE(m.until([&] { return m.flag(me + "/Flag - Mission Won"); }, 200));
	MESSAGE("mission won at frame " << m.frame());
	REQUIRE(m.until([&] { return !m.engine().endRequests().empty(); }, 200));
	const ScriptEngine::EndRequest &end = m.engine().endRequests().front();
	CHECK(end.victory);
	CHECK(m.requested("VICTORY_SCREEN"));
	MESSAGE("victory (" << end.action << ") at frame " << end.frame);
	for (const std::string &l : m.engine().report())
	{
		MESSAGE("  " << l);
	}
}

namespace
{

// loads `map` and runs the first 15 s of its scripts
void opening(const char *map)
{
	OPENBFME_REQUIRE_START(s);
	Mission m;
	m.load(*s, map);
	REQUIRE(m.game->mapScriptsRunning());
	m.step(75);
	const ScriptEngine::Stats &st = m.engine().stats();
	CHECK(st.scriptsFired > 0);
	CHECK(!m.requests.empty()); // the opening's presentation (letterbox, camera, captions, music ...)
	unsigned long long unportedConditions = 0, unportedActions = 0;
	for (const auto &kv : st.unportedConditions)
	{
		unportedConditions += kv.second;
	}
	for (const auto &kv : st.unportedActions)
	{
		unportedActions += kv.second;
	}
	MESSAGE(std::string(map) << ": frame " << m.frame() << ", " << st.scriptsFired << " scripts fired, " << m.requests.size() << " client requests, unported: "
						  << st.unportedConditions.size() << " conditions (" << unportedConditions << " evaluations), " << st.unportedActions.size() << " actions ("
						  << unportedActions << " runs)");
}

} // namespace

// every RotWK campaign mission after MAP ANG Angmar, in mission order: data\ini\linearcampaignexpansion1.ini ANGMAR_CAMPAIGN after Foundation, then
// data\ini\linearcampaign.ini GOOD_CAMPAIGN and EVIL_CAMPAIGN; each loads and runs its opening
#define SCRIPT3_OPENING(map) \
	TEST_CASE("script3 campaign opening: " map) { opening(map); }
SCRIPT3_OPENING("map ang rhudaur")
SCRIPT3_OPENING("map ang amon sul")
SCRIPT3_OPENING("map ang dark eye")
SCRIPT3_OPENING("map ang barrow downs")
SCRIPT3_OPENING("map ang carn dum")
SCRIPT3_OPENING("map ang barrow wights")
SCRIPT3_OPENING("map ang fornost")
SCRIPT3_OPENING("map good rivendell")
SCRIPT3_OPENING("map good high pass")
SCRIPT3_OPENING("map good ettenmoors")
SCRIPT3_OPENING("map good blue mountain")
SCRIPT3_OPENING("map good grey havens")
SCRIPT3_OPENING("map good celduin")
SCRIPT3_OPENING("map good erebor")
SCRIPT3_OPENING("map good dol guldur")
SCRIPT3_OPENING("map evil lorien")
SCRIPT3_OPENING("map evil grey havens")
SCRIPT3_OPENING("map evil shire")
SCRIPT3_OPENING("map evil fornost")
SCRIPT3_OPENING("map evil mirkwood")
SCRIPT3_OPENING("map evil withered heath")
SCRIPT3_OPENING("map evil erebor")
SCRIPT3_OPENING("map evil rivendell")
