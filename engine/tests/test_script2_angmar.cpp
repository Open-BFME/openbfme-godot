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

#include "Common/CommandPoints.h"
#include "Common/Dict.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/System/SaveGame/SaveGameFile.h"
#include "Common/Team.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/CommandLogLoad.h"
#include "GameClient/LiveGame.h"
#include "GameClient/MapChunks.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Damage.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/CastleModules.h"
#include "GameLogic/Module/EmotionModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ScriptEngine/ScriptConditions.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"
#include "GameLogic/ScriptEngine/ScriptTemplates.h"
#include "GameNetwork/LockstepDriver.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <map>
#include <sstream>
#include <memory>
#include "GameLogic/Module/CastleModules.h"

namespace
{

using campaigntest::Mission;

} // namespace

static void runFoundation(SimulationProfile profile, bool defend, const SaveGameFile *saved = nullptr)
{
	OPENBFME_REQUIRE_START(s);
	CommandLogLoad load;
	ReplayWriter writer;
	std::unique_ptr<LockstepDriver> driver;
	Mission m;
	m.load(*s, "map ang angmar", saved ? SimulationProfile::Faithful : profile);
	REQUIRE(m.game->mapScriptsRunning());
	const std::string me = m.logic().players().getLocalPlayer()->getPlayerName();
	if (saved)
	{
		std::string error;
		REQUIRE_MESSAGE(load.prepare(*saved, &error), error);
		REQUIRE_MESSAGE(load.begin(*m.game, &error), error);
		while (load.step(*m.game, 20) == CommandLogLoad::State::CatchingUp)
		{
		}
		std::cerr << "FOUNDATION LOAD frame=" << m.frame()
			<< " hash=" << m.logic().computeStateHash()
			<< " compared=" << load.hashesCompared()
			<< " failure=" << load.failure() << '\n';
		REQUIRE_MESSAGE(load.state() == CommandLogLoad::State::Ready, load.failure());
		REQUIRE(m.frame() == saved->header.frame);
		REQUIRE(m.logic().computeStateHash() == saved->header.stateHash);
		REQUIRE_MESSAGE(load.handOver(*m.game, writer, std::string(), profile, &error), error);
		REQUIRE(m.logic().simulationProfile() == profile);
		driver = std::make_unique<LockstepDriver>(nullptr, &writer);
		m.game->setFrameDriver(driver.get());
		m.take();
		std::cerr << "FOUNDATION HANDOVER profile=" << simulationProfileName(profile) << '\n';
	}
	// lane DEVPORT-8 (FALSEDEF-1): every hit a citadel takes, by its source's template (the trace is outside the simulation: SHIP-1's observer)
	struct CitadelHit
	{
		unsigned frame;
		ObjectID victim;
		std::string source;
		float dealt, taken;
	};
	std::vector<CitadelHit> citadelHits;
	m.logic().combat().setDamageTrace([&](const Object &victim, std::uint32_t sourceId, float dealt, float taken) {
		if (victim.getTemplate()->getName() != "AngmarFortressCitadel")
		{
			return;
		}
		const Object *source = m.logic().findObjectByID(sourceId);
		citadelHits.push_back({ m.frame(), victim.getID(), source ? source->getTemplate()->getName() : std::string("absent"), dealt, taken });
		if (saved)
		{
			std::cerr << "FOUNDATION DAMAGE frame=" << m.frame() << " victim=" << victim.getID() << " source=" << sourceId << " source_type="
				<< (source ? source->getTemplate()->getName() : "absent") << " dealt=" << dealt << " taken=" << taken
				<< " health=" << victim.getBodyModule()->getHealth() << '\n';
		}
	});
	// the retail numbers of the counterattack's troll slings (AngmarTrollSling, the IceRock upgrade: test_falsedef_retail.cpp, from the INI) on the citadel:
	// AngmarStoneThrowerIceWarhead's 500 FROST x FortressArmor 10% = 50 and 900 SIEGE x DamageScalar 150% (ENEMIES +STRUCTURE) x FortressArmor 100% = 1350.
	// Before lane WARG-1 the undefended loss of a full 7,500 citadel was 5 volleys of 50 + 1350 (7,000), a 6th 50 (7,050) and a 6th 1350 that took the
	// last 450, all from the slings. Lane WARG-1 (the attack-follow states, RW 0x755D78): the waves' wolf riders now ride their paths to the citadels and
	// attack them too (their mood target on the way), so the citadels also take the riders' and their banner carrier's melee hits (engine pins, measured on
	// the lane's tree; the slings' hits keep the retail numbers above, the last one takes what the melee left). Merge queue q36 re-measured the melee counts once on the
	// merged tree, compared at each merge point: WARG-1 keeps them; PR #182's Uber target rule (an attacker's goal object or victim does not refuse its
	// cells, RW checkForMovement 0x6EBAA0; this case runs Uber) moves them
	auto checkSlingKill = [&]() {
		std::map<std::string, int> kinds;
		for (const CitadelHit &h : citadelHits)
		{
			std::ostringstream k;
			k << h.source << " dealt " << h.dealt << " taken " << h.taken;
			++kinds[k.str()];
		}
		for (const auto &kv : kinds)
		{
			MESSAGE("citadel hits: " << kv.second << " x " << kv.first);
		}
		// merge queue q37 (measured once on the merged tree; the table at #187's merge point differs again, ATKRESET-1's attack orders move it): wolf rider
		// hits 1033 -> 1006, banner carrier hits 101 -> 83, the last sling hit takes 955 (1017)
		// merge queue q38 (measured once on the merged tree and at each merge point; LOCODIST-1's retail locomotor distance to the goal, RW 0x664127, moves it,
		// #172 keeps it): wolf rider hits 1006 -> 1016, banner carrier hits 83 -> 88, the last sling hit takes 1111 (955)
		// merge queue q41 (measured once on the merged tree and at its merge point; DECOMPFIX-1104B's aim and give-way rules, AIMAIM-1 RW 0x75232F and BUMP-1,
		// move it, NECRODIE-1 keeps it): wolf rider hits 1016 -> 982, banner carrier hits 88 -> 97, the last sling hit takes 1285 (1111)
		const std::map<std::string, int> expected = { { "AngmarThrallMasterWolfBanner dealt 2 taken 2", 97 }, { "AngmarTrollSling dealt 1350 taken 1285", 1 },
			{ "AngmarTrollSling dealt 1350 taken 1350", 3 }, { "AngmarTrollSling dealt 50 taken 50", 4 }, { "AngmarWolfRider dealt 5 taken 5", 982 } };
		CHECK(kinds == expected);
	};
	if (!saved)
	{
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
	}

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
	std::vector<ObjectID> citadels;
	for (Object *o = m.logic().getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getTemplate()->getName() == "AngmarFortressCitadel" && o->getControllingPlayer() == m.logic().players().getLocalPlayer())
		{
			citadels.push_back(o->getID());
		}
	}
	REQUIRE(citadels.size() == 3);
	if (!saved)
	{
		// lane WARG-1: the counterattack's waves follow their waypoint paths in the attack-follow state (RW 0x755D78) and attack what they meet on the way
		// (getNextMoodTarget RW 0x66844A); every path starts at the troll plateau's mouth (about 2600, 560), where the fixture left the player's army. These
		// flows need the waves at the citadels (the undefended loss; the defended flow's citadel damage before the fixture defeats them), so the player's
		// mobile units leave the route as a player would: placed at 3300, 2400, at least 700 from every counterattack path and citadel
		std::vector<Object *> army;
		for (Object *o = m.logic().getFirstObject(); o; o = o->getNextObject())
		{
			if (o->getControllingPlayer() == m.logic().players().getLocalPlayer() && o->getAIUpdateInterface() && !o->getContainedBy() &&
				!o->isKindOfName("STRUCTURE") && !o->isEffectivelyDead())
			{
				army.push_back(o);
			}
		}
		MESSAGE(army.size() << " of the player's units leave the counterattack's route");
		int k = 0;
		for (Object *o : army)
		{
			m.place(*o, Coord3D{ 3300.0f + 40.0f * (float)(k % 6), 2400.0f + 40.0f * (float)(k / 6), 0.0f });
			++k;
		}
	}
	const ThingTemplate *citadelType = m.logic().things().findTemplate("AngmarFortressCitadel");
	REQUIRE(citadelType);
	auto assertFortressesSurvive = [&] {
		for (ObjectID id : citadels)
		{
			const Object *o = m.logic().findObjectByID(id);
			REQUIRE(o);
			CHECK_FALSE(o->isEffectivelyDead());
			CHECK_FALSE(o->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION));
			CHECK(o->getControllingPlayer() == m.logic().players().getLocalPlayer());
			REQUIRE(o->getTeam());
			CHECK(o->getTeam()->getPrototype()->getControllingPlayer() == m.logic().players().getLocalPlayer());
		}
		CHECK(ScriptConditions::countPlayerObjects(m.logic(), *m.logic().players().getLocalPlayer(), { citadelType }) == 3);
		CHECK_FALSE(m.flag("/BN Destroyed Fortress"));
	};
	assertFortressesSurvive();

	// 5. the counterattack (a 120 s timer after the objective message), its three waves destroyed -> Objective 9 -> the finale -> "Mission Won" -> victory
	REQUIRE(m.until([&] { return m.requested("DISPLAY_COUNTDOWN_TIMER"); }, 200));
	const ScriptEngine::Counter *go = nullptr;
	REQUIRE(m.until([&] { go = m.engine().findCounter(me + "/BN Counterattack Go"); return go && go->value < 1; }, 800));
	if (!defend)
	{
		// lane WARG-1: the waves follow their waypoint paths (the attack-follow states) from the plateau's mouth round by the map's east edge to the
		// citadels, a longer march than the stand-in's straight attack move to the path's end (600 frames before)
		REQUIRE(m.until([&] { return m.flag("/BN Destroyed Fortress"); }, 1500));
		CHECK(ScriptConditions::countPlayerObjects(m.logic(), *m.logic().players().getLocalPlayer(), { citadelType }) == 2);
		MESSAGE("fortress loss at frame " << m.frame());
		if (saved)
		{
			int deadCitadels = 0;
			for (ObjectID id : citadels)
			{
				const Object *o = m.logic().findObjectByID(id);
				REQUIRE(o);
				CHECK(o->getControllingPlayer() == m.logic().players().getLocalPlayer());
				CHECK_FALSE(o->isDestroyed());
				if (o->isEffectivelyDead())
				{
					++deadCitadels;
					CHECK(o->getBodyModule()->getHealth() == 0.0f);
				}
			}
			CHECK(deadCitadels == 1);
		}
		checkSlingKill();
		REQUIRE(m.until([&] { return !m.engine().endRequests().empty(); }, 300));
		CHECK_FALSE(m.engine().endRequests().front().victory);
		MESSAGE("defeat at frame " << m.engine().endRequests().front().frame);
		return;
	}
	// Let the real attack damage a completed fortress before the fixture defeats the waves. No invulnerability, healing, ownership or counter edits.
	REQUIRE(m.until([&] {
		assertFortressesSurvive();
		for (ObjectID id : citadels)
		{
			const Object *o = m.logic().findObjectByID(id);
			if (o && o->getBodyModule()->getHealth() < o->getBodyModule()->getMaxHealth())
			{
				return true;
			}
		}
		return false;
	}, 1500)); // lane WARG-1: the waves' march along their paths (above; 300 frames before)
	MESSAGE("first citadel damage at frame " << m.frame());
	for (const char *t : { "PlyrBNEast/Counterattack Wave 1", "PlyrBNEast/Counterattack Troll Slings", "PlyrBNWest/Counterattack Wave 1",
			 "PlyrBNWest/Counterattack Troll Slings", "PlyrBNOther/South Counterattack Wave 1", "PlyrBNOther/South Counterattack Troll Slings" })
	{
		m.killTeam(t);
	}
	REQUIRE(m.until([&] { const ScriptEngine::Counter *c = m.engine().findCounter("/NumCounterattacksDefeated"); return c && c->value >= 3; }, 100));
	MESSAGE("counterattacks defeated at frame " << m.frame());
	assertFortressesSurvive();
	// Finale Part 2 deliberately TEAM_DELETEs the player's army, fortresses included; Part 1 first disables Mission Loss.
	REQUIRE(m.until([&] { return !m.engine().endRequests().empty(); }, 1000));
	const ScriptEngine::EndRequest &end = m.engine().endRequests().front();
	CHECK(end.victory);
	CHECK_FALSE(m.flag("/Mission Lost"));
	CHECK(std::any_of(m.requests.begin(), m.requests.end(), [](const ScriptClientRequest &r) {
		return r.action == "MARK_MISSION_OBJECTIVE_COMPLETED" && !r.params.empty() && r.params[0].intValue == 9;
	}));
	CHECK(m.requested("VICTORY_SCREEN"));
	CHECK(m.requested("SHOW_MILITARY_CAPTION", "SCRIPT:ANGAngmarEndCineText_03"));
	MESSAGE("victory (" << end.action << ") at frame " << end.frame);
	for (const std::string &l : m.engine().report())
	{
		MESSAGE("  " << l);
	}
}

TEST_CASE("script2 angmar: MAP ANG Angmar plays from its intro to the victory by its own scripts")
{
	runFoundation(SimulationProfile::Faithful, true);
}

TEST_CASE("script2 angmar: MAP ANG Angmar Uber survives the counterattack and reaches victory")
{
	runFoundation(SimulationProfile::Uber, true);
}

TEST_CASE("script2 angmar: MAP ANG Angmar an undefended Uber citadel loss reaches defeat")
{
	runFoundation(SimulationProfile::Uber, false);
}

TEST_CASE("campaign owner save: post-load Foundation fortress construction and counterattack")
{
	const char *path = std::getenv("OPENBFME_FOUNDATION_SAVE");
	if (!path || !*path)
	{
		// lane DEVPORT-8: the owner's private save is on no CI host; the case says so loudly (stdout, as retailtest::printSkip) and does not run
		std::printf("SKIP: campaign owner save (test_script2_angmar.cpp) needs OPENBFME_FOUNDATION_SAVE (the owner's private Saved Game 1, frame 3301); not run\n");
		std::fflush(stdout);
		return;
	}
	SaveGameFile save;
	std::string error;
	REQUIRE_MESSAGE(SaveGameFile::load(path, save, &error), error);
	REQUIRE(save.header.campaign == "ANGMAR_CAMPAIGN");
	REQUIRE(save.header.campaignMission == 0);
	REQUIRE(save.header.campaignDifficulty == 1);
	REQUIRE(save.header.executionProfile == "enhanced");
	const char *mode = std::getenv("OPENBFME_FOUNDATION_MODE");
	const std::string selected = mode ? mode : "defended";
	REQUIRE((selected == "defended" || selected == "undefended" || selected == "uber-defended" || selected == "uber-undefended"));
	runFoundation(selected.find("uber") != std::string::npos ? SimulationProfile::Uber : SimulationProfile::Faithful,
		selected.find("undefended") == std::string::npos, &save);
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
