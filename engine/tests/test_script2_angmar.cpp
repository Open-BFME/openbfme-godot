// OpenBFME unit tests: the first Angmar campaign mission (MAP ANG Angmar) played to its end by its own scripts (lane SCRIPT-2). GPL-3.0.
//
// A retail run (about 2,100 logic frames; it SKIPs when ROTWK_INSTALL / BFME2_INSTALL are unset). The
// player's part is driven by test helpers where a player would fight or walk (the heroes are placed into the trigger areas the objectives watch, the
// objective enemies are killed, a builder that does not arrive is placed at its build point); everything else is the map's scripts: the
// cinematics' timer chains, the sequential scripts, the indestructible flags, the fortress construction by the builders, the counterattack timer,
// the finale and the victory (VICTORY_SCREEN, QUICKVICTORY).

#include "doctest.h"
#include "CampaignTestUtil.h"
#include "StartTestUtil.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/LiveGame.h"
#include "GameClient/MapChunks.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameLogic/Damage.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/EmotionModules.h"
#include "Common/CommandPoints.h"
#include "Common/Dict.h"
#include "GameLogic/ScriptEngine/ScriptTemplates.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ScriptEngine/ScriptConditions.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <functional>
#include <memory>

namespace
{

using campaigntest::Mission;

} // namespace

TEST_CASE("script2 angmar: MAP ANG Angmar plays from its intro to the victory by its own scripts")
{
	OPENBFME_REQUIRE_START(s);
	Mission m;
	m.load(*s);
	REQUIRE(m.game->mapScriptsRunning());
	const std::string me = m.logic().players().getLocalPlayer()->getPlayerName();
	// 1. the intro cinematic (frames 1 .. 181); Rogash's attackers stay alive (indestructible) and "Rogash Saved" waits
	m.step(200);
	CHECK(m.requested("CAMERA_LETTERBOX_END"));
	CHECK_FALSE(m.flag(me + "/Rogash Saved"));
	MESSAGE("intro over at frame " << m.frame());

	// 2. the heroes reach Rogash: "Rogash Becomes Vulnerable" lifts the flags; the attackers die; Objective 3 sets "Rogash Saved"
	for (const char *h : { "Witch King", "Morgomir" })
	{
		REQUIRE(m.unit(h) != nullptr);
		m.place(*m.unit(h), m.centreOf("AT - Rogash Becomes Vulnerable"));
	}
	m.step(2);
	CHECK_FALSE(m.unit("Rogash Attacker 1")->getBodyModule()->isIndestructible());
	for (const char *n : { "Rogash Attacker 1", "Rogash Attacker 2", "Surrender Monkeys" })
	{
		m.killNamed(n);
	}
	REQUIRE(m.until([&] { return m.flag(me + "/Rogash Saved"); }, 50));
	MESSAGE("Rogash saved at frame " << m.frame());
	// the cinematic: its sequential part 3 runs on the Witch King when he is idle; part 6 ends it
	REQUIRE(m.until([&] { return m.requested("SHOW_MILITARY_CAPTION", "SCRIPT:ANGAngmarVOText_04"); }, 600));
	REQUIRE(m.until([&] { return m.requested("SHOW_MISSION_OBJECTIVE") && m.unit("Rogash")->getControllingPlayer() == m.logic().players().getLocalPlayer(); }, 300));
	MESSAGE("Rogash joins at frame " << m.frame());

	// 3. Rogash enters the troll plateau: the plateau cinematic; the plateau's Black Numenoreans are killed -> Objective 6 and the fortress cinematic
	m.step(150);
	m.place(*m.unit("Rogash"), m.centreOf("AT - Troll Plateau"));
	REQUIRE(m.until([&] { return m.flag("/Flag - Plateau Cine Over"); }, 600));
	MESSAGE("plateau cinematic over at frame " << m.frame());
	for (const char *n : { "Plateau Swordsman 1", "Plateau Swordsman 2", "Plateau Swordsman 3", "Plateau Rangers 1", "Plateau Rangers 2" })
	{
		m.killNamed(n);
	}
	REQUIRE(m.until([&] { return m.flag("/Flag - Fortress Objective Cine Over"); }, 600));
	MESSAGE("fortress objective at frame " << m.frame());

	// 4. the camps fall: each builder walks its path (a sequential script) and builds a fortress; the fortresses complete -> Objective 8
	m.killTeam("PlyrBNEast/Camp Guards");
	m.killTeam("PlyrBNWest/Camp Guards");
	m.killTeam("PlyrCreeps/Valley End Giants");
	const char *points[3] = { "Fortress Build Pt 1", "Fortress Build Pt 2", "Fortress 3 Build Pt" };
	const unsigned camps = m.frame();
	bool placed[3] = { false, false, false };
	const bool built = m.until(
		[&] {
			for (int i = 0; i < 3; ++i)
			{
				// a builder that has not started 1500 frames after its camp fell is placed at its build point (its path is a stand-in, S-1186)
				const std::string started = me + "/Flag - Fortress " + std::to_string(i + 1) + " started";
				Object *b = m.unit("Builder " + std::to_string(i + 1));
				if (!placed[i] && !m.flag(started) && m.frame() > camps + 1500 && b)
				{
					placed[i] = true;
					m.place(*b, m.waypoint(points[i]));
					MESSAGE("builder " << (i + 1) << " placed at its build point at frame " << m.frame());
				}
			}
			return m.flag("/Fortress 1 Built") && m.flag("/Fortress 2 Built") && m.flag("/Fortress 3 Built");
		},
		4000);
	for (int i = 0; i < 3; ++i)
	{
		MESSAGE("fortress " << (i + 1) << " built: " << m.flag("/Fortress " + std::to_string(i + 1) + " Built") << " (" << points[i] << ")");
	}
	REQUIRE(built);
	MESSAGE("three fortresses at frame " << m.frame());
	// the castles' owner change brought the keeps (AngmarFortressCitadel) along (CastleBehavior::onCapture RW 0x798F2B): "Check for BN Victory" counts them
	m.step(40);
	int citadels = 0;
	for (Object *o = m.logic().getFirstObject(); o; o = o->getNextObject())
	{
		citadels += o->getTemplate()->getName() == "AngmarFortressCitadel" && o->getControllingPlayer() == m.logic().players().getLocalPlayer() ? 1 : 0;
	}
	CHECK(citadels == 3);

	// 5. the counterattack (a 120 s timer after the objective message), its three waves destroyed -> Objective 9 -> the finale -> "Mission Won" -> victory
	REQUIRE(m.until([&] { return m.requested("DISPLAY_COUNTDOWN_TIMER"); }, 200));
	const ScriptEngine::Counter *go = nullptr;
	REQUIRE(m.until([&] { go = m.engine().findCounter(me + "/BN Counterattack Go"); return go && go->value < 1; }, 800));
	m.step(60);
	for (const char *t : { "PlyrBNEast/Counterattack Wave 1", "PlyrBNEast/Counterattack Troll Slings", "PlyrBNWest/Counterattack Wave 1",
			 "PlyrBNWest/Counterattack Troll Slings", "PlyrBNOther/South Counterattack Wave 1", "PlyrBNOther/South Counterattack Troll Slings" })
	{
		m.killTeam(t);
	}
	REQUIRE(m.until([&] { const ScriptEngine::Counter *c = m.engine().findCounter("/NumCounterattacksDefeated"); return c && c->value >= 3; }, 100));
	MESSAGE("counterattacks defeated at frame " << m.frame());
	REQUIRE(m.until([&] { return !m.engine().endRequests().empty(); }, 1000));
	const ScriptEngine::EndRequest &end = m.engine().endRequests().front();
	CHECK(end.victory);
	CHECK(m.requested("VICTORY_SCREEN"));
	CHECK(m.requested("SHOW_MILITARY_CAPTION", "SCRIPT:ANGAngmarEndCineText_03"));
	MESSAGE("victory (" << end.action << ") at frame " << end.frame);
	for (const std::string &l : m.engine().report())
	{
		MESSAGE("  " << l);
	}
}

TEST_CASE("script2 angmar: CREATE_REINFORCEMENT_TEAM (RW 0x7C8657) makes a team's units in rows at the waypoint; UNIT_FORCE_EMOTION (RW 0x8B4EB1); "
		  "OVERRIDE_PLAYER_COMMAND_POINTS (RW 0x6A7ACD)")
{
	OPENBFME_REQUIRE_START(s);
	Mission m;
	m.load(*s);
	ScriptEngine &e = m.engine();
	// MAP ANG Angmar's team templates list no units: a template made like a map's Teams entry (the dict keys of RW 0x7A2D1C) on PlyrBNEast
	Player *owner = m.logic().players().findPlayerWithName("PlyrBNEast");
	REQUIRE(owner != nullptr);
	Dict dict;
	dict.setAsciiString("teamName", "SCRIPT2 Reinforcements");
	dict.setAsciiString("teamOwner", "PlyrBNEast");
	dict.setBool("teamIsSingleton", false);
	dict.setAsciiString("teamUnitType1", "AngmarWolfRider");
	dict.setInt("teamUnitMinCount1", 1);
	dict.setInt("teamUnitMaxCount1", 3);
	dict.setAsciiString("teamUnitType2", "AngmarHillTroll");
	dict.setInt("teamUnitMaxCount2", 2);
	dict.setAsciiString("teamUnitType3", "AngmarSnowTroll");
	dict.setInt("teamUnitMaxCount3", 0); // no entry
	TeamPrototype *proto = m.logic().players().teams().initTeam("SCRIPT2 Reinforcements", owner, false, &dict);
	REQUIRE(proto != nullptr);
	const Dict &d = proto->getDict();
	std::int32_t expected = 0;
	for (int i = 1; i <= 7; ++i)
	{
		bool has = false;
		d.getAsciiString("teamUnitType" + std::to_string(i), &has);
		const std::int32_t n = d.getInt("teamUnitMaxCount" + std::to_string(i));
		expected += has && n > 0 ? n : 0;
	}
	REQUIRE(expected > 0);
	const size_t before = proto->teams().size();
	ScriptActionRec a;
	a.internalName = "CREATE_REINFORCEMENT_TEAM";
	a.type = a.resolved = ScriptTemplates::findAction("CREATE_REINFORCEMENT_TEAM");
	ScriptParameter team;
	team.type = 3;
	team.stringValue = "PlyrBNEast/SCRIPT2 Reinforcements";
	ScriptParameter wp;
	wp.type = 7;
	wp.stringValue = "InitialCameraPosition";
	a.params = { team, wp };
	e.executeAction(a);
	REQUIRE(proto->teams().size() == before + 1);
	Team *made = proto->teams().back();
	// MaxCount of each listed type: 3 wolf riders in a row from the waypoint (x + radius * k * 2.25), then 2 hill trolls one row up (y + 2 * radius)
	CHECK(expected == 5);
	REQUIRE(made->getMemberCount() == 5u);
	const Coord3D at = m.waypoint("InitialCameraPosition");
	std::vector<Object *> units = ScriptConditions::members(*made);
	CHECK(units[0]->getTemplate()->getName() == "AngmarWolfRider");
	CHECK(units[0]->getPosition()->x == at.x);
	CHECK(units[0]->getPosition()->y == at.y);
	CHECK(units[1]->getPosition()->x > at.x);
	CHECK(units[1]->getPosition()->y == at.y);
	CHECK(units[3]->getTemplate()->getName() == "AngmarHillTroll");
	CHECK(units[3]->getPosition()->x == at.x);
	CHECK(units[3]->getPosition()->y > at.y);

	// UNIT_FORCE_EMOTION(<an object with an EmotionTrackerUpdate>, 3, 10 s): the tracker's forced type and its 50 frames
	Object *feeler = nullptr;
	for (Object *o = m.logic().getFirstObject(); o && !feeler; o = o->getNextObject())
	{
		feeler = EmotionTrackerUpdate::of(*o) ? o : nullptr;
	}
	REQUIRE(feeler != nullptr);
	feeler->setName("SCRIPT2 Feeler");
	ScriptActionRec emo;
	emo.internalName = "UNIT_FORCE_EMOTION";
	emo.type = emo.resolved = ScriptTemplates::findAction("UNIT_FORCE_EMOTION");
	ScriptParameter unit;
	unit.type = 14;
	unit.stringValue = "SCRIPT2 Feeler";
	ScriptParameter type;
	type.type = 63;
	type.intValue = 3;
	ScriptParameter secs;
	secs.type = 1;
	secs.realValue = 10.0f;
	emo.params = { unit, type, secs };
	e.executeAction(emo);
	EmotionTrackerUpdate *t = EmotionTrackerUpdate::of(*feeler);
	REQUIRE(t != nullptr);
	CHECK(t->forcedType() == 3);
	CHECK(t->forcedFrames() == 50);

	// "Map Setup" ran OVERRIDE_PLAYER_COMMAND_POINTS(<Local Player>, 1000, 1000)
	const CommandPoints &cp = m.logic().players().getLocalPlayer()->commandPoints();
	CHECK(cp.scriptSet());
	CHECK(cp.getBase() == 1000);
	CHECK(cp.getCap() == 1000);
}
