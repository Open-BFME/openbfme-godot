// OpenBFME retail tests (lane BUILD-3): builders build everything. Every faction's starting builder builds every structure its command set offers (DOZER_CONSTRUCT
// buttons) through the lockstep command path on two maps, each to completion; QA-1's U17 spot (the Goblins' GoblinCave at (930, 877) on Tournament MP1, start 2) completes;
// a dozer-built site rises from 1 hit point (U4: the placement RW 0x88D44F); a builder at work is not an idle worker (U3: the primary machine RW 0x88E582); the dock
// position is the structure's Repair contact point (RW 0x88C2FE -> 0x667C76). SKIP loudly without the retail install.  GPL-3.0.

#include "doctest.h"

#include "BuildTestUtil.h"

#include "Common/BuildAssistant.h"
#include "Common/Player.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/ControlBarCommands.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/BuildPlacement.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/DozerAIUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/ObjectGeometry.h"
#include "GameLogic/ObjectTemplateInfo.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

namespace
{
const char *const kFactions[] = { "FactionMen", "FactionElves", "FactionDwarves", "FactionIsengard", "FactionMordor", "FactionWild", "FactionAngmar" };
const char *const kTournament = "maps/map mp tournament mp1/map mp tournament mp1.map";

GameMessage selectMsg(int player, ObjectID id)
{
	GameMessage m(MSG_CREATE_SELECTED_GROUP, player);
	m.appendBooleanArgument(true);
	m.appendObjectIDArgument(id);
	return m;
}
GameMessage dozerMsg(int player, const ThingTemplate &t, const Coord3D &loc, float angle)
{
	GameMessage m(MSG_DOZER_CONSTRUCT, player);
	m.appendIntegerArgument((int)t.getTemplateID());
	m.appendLocationArgument(loc);
	m.appendRealArgument(angle);
	return m;
}

Player *humanOf(LiveGame &live)
{
	return live.players().findPlayerWithName(live.report().startSlotPlayers[0]);
}

Object *fortressOf(LiveGame &live, Player *player)
{
	for (const StartingBase::Placed &p : live.report().startingObjects)
	{
		Object *o = p.structure ? live.logic().findObjectByID(p.id) : nullptr;
		if (o && o->getControllingPlayer() == player)
		{
			return o;
		}
	}
	return nullptr;
}

Object *dozerOf(LiveGame &live, Player *player)
{
	for (Object *o = live.logic().getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() == player && o->isKindOfName("DOZER") && dynamic_cast<DozerAIUpdate *>(o->getAIUpdateInterface()))
		{
			return o;
		}
	}
	return nullptr;
}

// what one build showed
struct Build
{
	std::string name;
	bool placed = false, completed = false, legalSite = false;
	int frames = 0;                    // order to completion
	float healthAtArrival = -1.0f;     // the structure's health the frame its placement ran (the dozer's unplaced structure cleared, nothing built yet)
	bool idleWorkerWhileWorking = false; // U3: the dozer was an idle worker in a frame it worked
	bool taskWhileWorking = true;        // the dozer's current task was BUILD in every frame it worked
	bool idleWorkerAfter = false;        // the dozer became an idle worker after the completion
	bool hiddenWhileWorking = true;      // RW 0x88C51B: a porter is out of the world and UNATTACKABLE in every frame it works
	bool shownAfter = false;             // RW 0x88D6AE: back in the world, the statuses cleared, after the completion
	int framesToIdle = -1;
	std::string problem;
	ObjectID id = INVALID_ID;
};

// the dozer builds `tt` at `site`: the frames run until it completes (or the guard ends); the U3 / U4 observations are recorded
void buildOne(LiveGame &live, Player &player, Object &dozerObj, const ThingTemplate &tt, const Coord3D &site, float angle, Build &out, int guard)
{
	GameLogic &logic = live.logic();
	DozerAIUpdate *dozer = dynamic_cast<DozerAIUpdate *>(dozerObj.getAIUpdateInterface());
	const int pi = player.getPlayerIndex();
	std::set<ObjectID> before;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		before.insert(o->getID());
	}
	live.commands().append(selectMsg(pi, dozerObj.getID()));
	live.commands().append(dozerMsg(pi, tt, site, angle));
	const int t0 = (int)logic.getFrame();
	logic.runLogicFrame();
	Object *building = nullptr;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (!before.count(o->getID()) && o->getTemplate()->getFinalOverride() == tt.getFinalOverride() && o->getControllingPlayer() == &player)
		{
			building = o;
		}
	}
	if (!building)
	{
		out.problem = "the foundation was not made";
		return;
	}
	out.placed = true;
	const ObjectID id = building->getID();
	out.id = id;
	while (guard-- > 0)
	{
		building = logic.findObjectByID(id);
		if (!building || building->isDestroyed())
		{
			out.problem = "the structure vanished while rising";
			return;
		}
		if (!building->isUnderConstruction())
		{
			break;
		}
		if (out.healthAtArrival < 0.0f && dozer->unplacedStructure() == INVALID_ID && building->getConstructionPercent() == 0.0f && building->getBodyModule())
		{
			out.healthAtArrival = building->getBodyModule()->getHealth();
		}
		logic.runLogicFrame();
		if (dozer->working())
		{
			static const int unattackable = ObjectTemplateInfoBuilder::objectStatusIndex("UNATTACKABLE");
			out.hiddenWhileWorking = out.hiddenWhileWorking && !dozerObj.isInWorld() && dozerObj.testStatus((unsigned)unattackable);
			out.idleWorkerWhileWorking = out.idleWorkerWhileWorking || dozer->isIdleWorker();
			out.taskWhileWorking = out.taskWhileWorking && dozer->getCurrentTask() == DozerAIUpdate::DOZER_TASK_BUILD;
		}
	}
	out.frames = (int)logic.getFrame() - t0;
	building = logic.findObjectByID(id);
	out.completed = building && !building->isUnderConstruction() && !building->isDestroyed();
	if (!out.completed)
	{
		char buf[200];
		std::snprintf(buf, sizeof buf, "not complete after %d frames (%.1f %%, dozer task %d, action %d, sub %d)", out.frames, building ? building->getConstructionPercent() : -2.0f,
		              dozer->getCurrentTask(), dozer->actionState(), dozer->getBuildSubTask());
		out.problem = buf;
		return;
	}
	{
		static const int unattackable = ObjectTemplateInfoBuilder::objectStatusIndex("UNATTACKABLE");
		out.shownAfter = dozerObj.isInWorld() && !dozerObj.testStatus((unsigned)unattackable);
	}
	// the porter walks BuilderMoveFromNewStructureDistance away from the structure (RW 0x88D6AE) and is an idle worker when it stands
	for (int i = 0; i < 100 && !out.idleWorkerAfter; ++i)
	{
		logic.runLogicFrame();
		out.idleWorkerAfter = dozer->isIdleWorker();
		out.framesToIdle = i + 1;
	}
}

// a site for `tt` around `centre` the dozer's construct gate accepts (rings of 40 from 160 to 760, 24 directions), away from the sites already used
bool findSite(LiveGame &live, DozerAIUpdate &dozer, Player &player, const ThingTemplate &tt, const Coord3D &centre, Coord3D &site)
{
	GameLogic &logic = live.logic();
	for (float r = 160.0f; r <= 760.0f; r += 40.0f)
	{
		for (int k = 0; k < 24; ++k)
		{
			const double a = (double)k * 0.2617993877991494;
			Coord3D c = centre;
			c.x += (float)(r * std::cos(a));
			c.y += (float)(r * std::sin(a));
			c.z = logic.getGroundHeight(c.x, c.y);
			if (dozer.siteIsLegal(tt, c, 0.0f, player))
			{
				site = c;
				return true;
			}
		}
	}
	return false;
}

// every DOZER_CONSTRUCT button of the dozer's command set, in slot order
std::vector<const ThingTemplate *> buildable(const Object &dozer)
{
	std::vector<const ThingTemplate *> out;
	const CommandSet *set = TheCommandStore ? TheCommandStore->findCommandSet(dozer.getCommandSetName()) : nullptr;
	for (int slot = 0; set && slot < CommandSet::MAX_BUTTONS; ++slot)
	{
		const CommandButton *btn = set->getCommandButton(slot);
		if (btn && btn->m_command == GUI_COMMAND_DOZER_CONSTRUCT && btn->getThingTemplate())
		{
			const ThingTemplate *t = btn->getThingTemplate()->getFinalOverride();
			if (std::find(out.begin(), out.end(), t) == out.end())
			{
				out.push_back(t);
			}
		}
	}
	return out;
}

// one faction on one map: every structure its builder offers, prerequisites first (passes until nothing new can be built)
void buildEverything(starttest::Shared &s, const char *faction, const char *map, int start0, int start1)
{
	std::string error;
	buildtest::Game g;
	// the other player (an Easy AI) is an ally: on a small map an enemy AI attacks the builds (the structures it destroys are not U5's vanishing)
	REQUIRE_MESSAGE(buildtest::startGame(s, faction, "FactionMen", 7177, g, &error, map, start0, start1, true), error);
	LiveGame &live = *g.live;
	Player *player = humanOf(live);
	REQUIRE(player);
	player->depositMoney(200000, false);
	Object *fortress = fortressOf(live, player);
	Object *dozerObj = dozerOf(live, player);
	REQUIRE(fortress);
	REQUIRE(dozerObj);
	DozerAIUpdate *dozer = dynamic_cast<DozerAIUpdate *>(dozerObj->getAIUpdateInterface());
	const std::vector<const ThingTemplate *> offered = buildable(*dozerObj);
	CHECK_MESSAGE(!offered.empty(), faction, ": the builder offers no structure");
	std::set<std::string> done;
	std::vector<Build> builds;
	for (int pass = 0; pass < 4; ++pass)
	{
		bool progress = false;
		for (const ThingTemplate *tt : offered)
		{
			if (done.count(tt->getName()) || BuildAssistant::canMakeUnit(*dozerObj, tt, -1) != CANMAKE_OK)
			{
				continue;
			}
			Build b;
			b.name = tt->getName();
			Coord3D site;
			b.legalSite = findSite(live, *dozer, *player, *tt, *fortress->getPosition(), site);
			if (!b.legalSite)
			{
				b.problem = "no legal site";
			}
			else
			{
				const int t = BuildAssistant::calcTimeToBuild(*tt, player, dozerObj, -1, live.logic().productionSettings(), live.logic());
				buildOne(live, *player, *dozerObj, *tt, site, 0.0f, b, t + 1500);
			}
			done.insert(b.name);
			progress = true;
			builds.push_back(b);
		}
		if (!progress)
		{
			break;
		}
	}
	// U5: nothing built vanishes afterwards (the Dwarven Mine Shaft, a TunnelContain, was a non-STRUCTURE: its ChildObject's `KindOf = +...` replaced the set)
	for (int i = 0; i < 150; ++i)
	{
		live.logic().runLogicFrame();
	}
	for (const Build &b : builds)
	{
		if (b.completed)
		{
			const Object *o = live.logic().findObjectByID(b.id);
			CHECK_MESSAGE((o && !o->isDestroyed() && o->isKindOfName("STRUCTURE")), faction, " ", b.name, ": gone or not a STRUCTURE 150 frames after the builds");
		}
	}
	int completed = 0;
	for (const Build &b : builds)
	{
		std::printf("  info: %s on %s: %s %s in %d frames (idle worker %d frames later), health at arrival %.2f%s\n", faction, map, b.name.c_str(), b.completed ? "completed" : "FAILED", b.frames, b.framesToIdle, b.healthAtArrival,
		            b.problem.empty() ? "" : (" (" + b.problem + ")").c_str());
		CHECK_MESSAGE(b.completed, faction, " ", b.name, ": ", b.problem);
		if (b.completed)
		{
			++completed;
			CHECK_MESSAGE(b.healthAtArrival == 1.0f, faction, " ", b.name, ": health at the placement ", b.healthAtArrival); // U4
			CHECK_MESSAGE(!b.idleWorkerWhileWorking, faction, " ", b.name, ": an idle worker while it worked");          // U3
			CHECK_MESSAGE(b.taskWhileWorking, faction, " ", b.name);
			CHECK_MESSAGE(b.idleWorkerAfter, faction, " ", b.name, ": not an idle worker after the completion");
			CHECK_MESSAGE(b.hiddenWhileWorking, faction, " ", b.name, ": the porter was not inside the structure while it worked");
			CHECK_MESSAGE(b.shownAfter, faction, " ", b.name, ": the porter did not come out");
		}
	}
	for (const ThingTemplate *tt : offered)
	{
		if (!done.count(tt->getName()))
		{
			std::printf("  info: %s on %s: %s not buildable (canMakeUnit %d)\n", faction, map, tt->getName().c_str(), (int)BuildAssistant::canMakeUnit(*dozerObj, tt, -1));
		}
	}
	CHECK(completed > 0);
}
} // namespace

TEST_CASE("BUILD-3 U17: the Goblins' GoblinCave at QA-1's spot on Tournament MP1 (start 2) completes; the dock is its Repair contact point")
{
	OPENBFME_REQUIRE_START(s);
	std::string error;
	buildtest::Game g;
	REQUIRE_MESSAGE(buildtest::startGame(*s, "FactionWild", "FactionMen", 5150, g, &error, kTournament, 1, 0), error);
	LiveGame &live = *g.live;
	GameLogic &logic = live.logic();
	Player *player = humanOf(live);
	REQUIRE(player);
	Object *fortress = fortressOf(live, player);
	Object *dozerObj = dozerOf(live, player);
	REQUIRE(fortress);
	REQUIRE(dozerObj);
	std::printf("  info: Tournament MP1 start 2: fortress at (%.1f, %.1f), porter %s at (%.1f, %.1f)\n", fortress->getPosition()->x, fortress->getPosition()->y,
	            dozerObj->getTemplate()->getName().c_str(), dozerObj->getPosition()->x, dozerObj->getPosition()->y);
	const ThingTemplate *cave = logic.things().findTemplate("GoblinCave");
	REQUIRE(cave);
	cave = cave->getFinalOverride();
	// the contact points the engine now reads (cave.ini: two Repair points, two unlabelled, one Swoop)
	const std::vector<ObjectGeometry::ContactPoint> points = ObjectGeometry::contactPointsOf(*cave);
	REQUIRE(points.size() == 5);
	CHECK(points[0].label == "Repair");
	CHECK(points[0].x == doctest::Approx(-40.188f));
	CHECK(points[0].y == doctest::Approx(41.694f));
	CHECK(points[1].label == "Repair");
	CHECK(points[2].label.empty());
	CHECK(points[4].label == "Swoop");
	CHECK(points[4].z == doctest::Approx(32.902f));
	Coord3D site{ 930.0f, 877.0f, 0.0f };
	site.z = logic.getGroundHeight(site.x, site.y);
	DozerAIUpdate *dozer = dynamic_cast<DozerAIUpdate *>(dozerObj->getAIUpdateInterface());
	REQUIRE_MESSAGE(dozer->siteIsLegal(*cave, site, 0.0f, *player), "QA-1's site is not legal in this game");
	Build b;
	b.name = "GoblinCave";
	buildOne(live, *player, *dozerObj, *cave, site, 0.0f, b, 2000);
	std::printf("  info: GoblinCave at (930, 877): %s in %d frames, health at arrival %.2f %s\n", b.completed ? "completed" : "FAILED", b.frames, b.healthAtArrival, b.problem.c_str());
	CHECK_MESSAGE(b.completed, b.problem);
	CHECK(b.healthAtArrival == 1.0f);
	CHECK_FALSE(b.idleWorkerWhileWorking);
	CHECK(b.idleWorkerAfter);
	CHECK(b.hiddenWhileWorking);
	CHECK(b.shownAfter);
	// the dock position is a Repair contact point of the cave, turned with the structure and placed at its position (angle 0: the offsets themselves)
	Object *built = nullptr;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getTemplate()->getFinalOverride() == cave && o->getControllingPlayer() == player)
		{
			built = o;
		}
	}
	REQUIRE(built);
	Coord3D dock;
	REQUIRE(DozerAIUpdate::findGoodBuildOrRepairPosition(*dozerObj, *built, dock));
	const bool atFirst = std::fabs(dock.x - (built->getPosition()->x - 40.188f)) < 0.01f && std::fabs(dock.y - (built->getPosition()->y + 41.694f)) < 0.01f;
	const bool atSecond = std::fabs(dock.x - (built->getPosition()->x + 38.037f)) < 0.01f && std::fabs(dock.y - (built->getPosition()->y - 38.085f)) < 0.01f;
	CHECK_MESSAGE((atFirst || atSecond), "dock (", dock.x, ", ", dock.y, ")");
	// the lane's stops are in the live report (S-1282's face command is also noted at runtime: the dozer reached its sub task 1 -> 2 above)
	const GameLogic::Report report = logic.report();
	for (const char *tag : { "[S-304]", "[S-1280]", "[S-1281]", "[S-1282]" })
	{
		bool found = false;
		for (const std::string &line : report.stops)
		{
			found = found || line.rfind(tag, 0) == 0;
		}
		CHECK_MESSAGE(found, tag);
	}
	bool faceNoted = false;
	for (const std::string &line : report.stops)
	{
		faceNoted = faceNoted || line.find("face command 0x26") != std::string::npos;
	}
	CHECK(faceNoted);
}

TEST_CASE("BUILD-3: every faction's builder builds every structure it offers on Evendim (rises from 1 hit point, never an idle worker while it works)")
{
	OPENBFME_REQUIRE_START(s);
	for (const char *faction : kFactions)
	{
		buildEverything(*s, faction, "maps/map mp evendim/map mp evendim.map", 0, 4);
	}
}

TEST_CASE("BUILD-3: every faction's builder builds every structure it offers on Tournament MP1")
{
	OPENBFME_REQUIRE_START(s);
	for (const char *faction : kFactions)
	{
		buildEverything(*s, faction, kTournament, 1, 0);
	}
}

TEST_CASE("BUILD-3: a site cancelled before its Porter arrives is paid back once (MSG_DOZER_CANCEL_CONSTRUCT and the dozer's own removal RW 0x88CFFE)")
{
	OPENBFME_REQUIRE_START(s);
	std::string error;
	buildtest::Game g;
	REQUIRE_MESSAGE(buildtest::startGame(*s, "FactionMen", "FactionMordor", 5150, g, &error), error);
	LiveGame &live = *g.live;
	GameLogic &logic = live.logic();
	Player *player = humanOf(live);
	REQUIRE(player);
	Object *fortress = fortressOf(live, player);
	Object *dozerObj = dozerOf(live, player);
	REQUIRE(fortress);
	REQUIRE(dozerObj);
	DozerAIUpdate *dozer = dynamic_cast<DozerAIUpdate *>(dozerObj->getAIUpdateInterface());
	const ThingTemplate *farm = logic.things().findTemplate("GondorFarm")->getFinalOverride();
	// a site far from the Porter (the farthest legal ring of findSite's search)
	Coord3D site;
	bool found = false;
	for (float r = 600.0f; r >= 300.0f && !found; r -= 40.0f)
	{
		for (int k = 0; k < 24 && !found; ++k)
		{
			Coord3D c = *fortress->getPosition();
			c.x += (float)(r * std::cos(k * 0.2617993877991494));
			c.y += (float)(r * std::sin(k * 0.2617993877991494));
			c.z = logic.getGroundHeight(c.x, c.y);
			if (dozer->siteIsLegal(*farm, c, 0.0f, *player))
			{
				site = c;
				found = true;
			}
		}
	}
	REQUIRE(found);
	const std::uint32_t before = player->getMoney()->countMoney();
	const int pi = player->getPlayerIndex();
	live.commands().append(selectMsg(pi, dozerObj->getID()));
	live.commands().append(dozerMsg(pi, *farm, site, 0.0f));
	logic.runLogicFrame();
	Object *structure = logic.findObjectByID(dozer->unplacedStructure());
	REQUIRE(structure);
	const ObjectID id = structure->getID();
	CHECK(player->getMoney()->countMoney() < before);
	logic.runLogicFrame();
	REQUIRE(dozer->unplacedStructure() == id); // still walking
	live.commands().append(selectMsg(pi, id));
	live.commands().append(GameMessage(MSG_DOZER_CANCEL_CONSTRUCT, pi));
	logic.runLogicFrame();
	logic.runLogicFrame();
	CHECK(logic.findObjectByID(id) == nullptr);
	CHECK(dozer->unplacedStructure() == INVALID_ID);
	CHECK(dozer->taskTarget() == INVALID_ID);
	CHECK(player->getMoney()->countMoney() == before); // one refund (the dispatcher's), not two
}
