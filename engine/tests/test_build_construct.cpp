// OpenBFME retail tests (lane BUILD-1): a Porter of every faction's starting base builds that faction's barracks equivalent through the lockstep command path
// (MSG_CREATE_SELECTED_GROUP + MSG_DOZER_CONSTRUCT): the price is paid when it is placed, the foundation rises while the Porter works (lane BUILD-2: 100 / BuildTime frames per
// frame of work, RW 0x88DE43), the finished building counts
// its command points and produces a unit (MSG_QUEUE_UNIT_CREATE).  Two runs of the same commands agree on every frame hash.  GPL-3.0.

#include "doctest.h"

#include "BuildTestUtil.h"

#include "Common/BuildAssistant.h"
#include "Common/Player.h"
#include "Common/StateHash.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/ControlBarCommands.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/BuildPlacement.h"
#include "GameLogic/Module/CastleModules.h"
#include "GameLogic/Module/DozerAIUpdate.h"
#include "GameLogic/Module/ConstructionModules.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"

#include <cmath>
#include <cstdio>

namespace
{
struct Barracks
{
	const char *faction;
	const char *building;
};
const Barracks kBarracks[] = {
	{ "FactionMen", "GondorBarracks" },
	{ "FactionElves", "ElvenBarracks" },
	{ "FactionDwarves", "DwarfBarracks" },
	{ "FactionIsengard", "IsengardUrukPit" },
	{ "FactionMordor", "MordorOrcPit" },
	{ "FactionWild", "GoblinCave" },
	{ "FactionAngmar", "AngmarBarracks" },
};

GameMessage selectMsg(int player, ObjectID id)
{
	GameMessage m(MSG_CREATE_SELECTED_GROUP, player);
	m.appendBooleanArgument(true);
	m.appendObjectIDArgument(id);
	return m;
}
GameMessage dozerMsg(int player, const ThingTemplate &t, const Coord3D &loc)
{
	GameMessage m(MSG_DOZER_CONSTRUCT, player);
	m.appendIntegerArgument((int)t.getTemplateID());
	m.appendLocationArgument(loc);
	m.appendRealArgument(0.0f);
	return m;
}
GameMessage queueMsg(int player, const ThingTemplate &t)
{
	GameMessage m(MSG_QUEUE_UNIT_CREATE, player);
	m.appendBooleanArgument(false);
	m.appendIntegerArgument((int)t.getTemplateID());
	m.appendIntegerArgument(-1);
	m.appendBooleanArgument(false);
	m.appendBooleanArgument(false);
	return m;
}

struct Outcome
{
	std::vector<std::uint32_t> hashes;
	int placedFrame = -1, completeFrame = -1, unitFrame = -1;
	int moneyBefore = 0, moneyAfterPlace = 0, expectedCost = 0, expectedFrames = 0, framesUsed = 0;
	std::string building, unit;
	int iniCost = 0, iniFrames = 0; // BuildCost / 5 * trunc(BuildTime) of the template's own fields (retail's calcTimeToBuild with every factor at 1)
	int workFrames = 0, accumulateFrames = 0; // the Porter's frames of work; the frames the retail accumulation (percent += 100 / T in float, RW 0x88DE95) needs to reach 100
	int cellDuring = -1, cellAfter = -1, cellDestroyed = -1; // the pathfinder cell under the building: CellType while it rises, when it is complete, after it was destroyed
	ObjectID buildingId = INVALID_ID;
	int cpLimitBefore = 0, cpLimitDuring = 0, cpLimitAfter = 0; // Player::commandPointLimit around the build
	size_t castlePieces = 0, castlePlots = 0; // when the building is a castle: what its unpack made
	std::string castleError;
	int commandPointsBefore = 0, commandPointsDuring = 0, commandPointsAfter = 0;
	std::vector<std::string> problems;
};

// the whole scenario on a fresh game; the checks that need doctest macros are made by the caller from the Outcome
void run(starttest::Shared &s, const Barracks &b, Outcome &out, bool produce = true)
{
	std::string error;
	buildtest::Game g;
	if (!buildtest::startGame(s, b.faction, "FactionMen", 5150, g, &error))
	{
		out.problems.push_back(error);
		return;
	}
	LiveGame &live = *g.live;
	GameLogic &logic = live.logic();
	Player *player = live.players().findPlayerWithName(live.report().startSlotPlayers[0]);
	const int pi = player->getPlayerIndex();
	const ThingTemplate *tt = logic.things().findTemplate(b.building);
	if (!tt)
	{
		out.problems.push_back(std::string("no template ") + b.building);
		return;
	}
	tt = tt->getFinalOverride();
	out.building = b.building;
	// the builder: a Porter of the starting units (DOZER, DozerAIUpdate); the site: the nearest clear ground around the fortress
	Object *centre = nullptr;
	for (const StartingBase::Placed &p : live.report().startingObjects)
	{
		Object *o = p.structure ? logic.findObjectByID(p.id) : nullptr;
		if (o && o->getControllingPlayer() == player)
		{
			centre = o;
		}
	}
	if (!centre)
	{
		out.problems.push_back("no starting structure");
		return;
	}
	Object *plot = nullptr; // (named plot for the shared code below: the builder)
	for (Object *o = logic.getFirstObject(); o && !plot; o = o->getNextObject())
	{
		if (o->getControllingPlayer() == player && o->isKindOfName("DOZER") && dynamic_cast<DozerAIUpdate *>(o->getAIUpdateInterface()) &&
			BuildAssistant::canMakeUnit(*o, tt, -1) == CANMAKE_OK)
		{
			plot = o;
		}
	}
	if (!plot)
	{
		out.problems.push_back("no builder offers " + std::string(b.building));
		return;
	}
	Coord3D site = *centre->getPosition();
	bool found = false;
	for (float r = 180.0f; r <= 520.0f && !found; r += 40.0f)
	{
		for (int k = 0; k < 16 && !found; ++k)
		{
			const float a = (float)k * 0.3927f;
			Coord3D c = *centre->getPosition();
			c.x += r * std::cos(a);
			c.y += r * std::sin(a);
			c.z = logic.getGroundHeight(c.x, c.y);
			if (BuildPlacement::isLocationLegalToBuild(logic, c, *tt, 0.0f, LLF_TERRAIN_RESTRICTIONS | LLF_NO_OBJECT_OVERLAP, plot, player) == LBC_OK)
			{
				site = c;
				found = true;
			}
		}
	}
	if (!found)
	{
		out.problems.push_back("no legal site near the fortress");
		return;
	}
	out.hashes.push_back(logic.computeStateHash());
	out.moneyBefore = (int)player->getMoney()->countMoney();
	out.cpLimitBefore = player->commandPointLimit();
	out.commandPointsBefore = player->commandPoints().getUsage();
	out.iniCost = (int)BuildAssistant::buildCost(*tt);
	out.iniFrames = 5 * (int)BuildAssistant::buildTime(*tt);
	out.expectedCost = BuildAssistant::calcCostToBuild(*tt, player, plot, -1);
	out.expectedFrames = BuildAssistant::calcTimeToBuild(*tt, player, plot, -1, logic.productionSettings(), logic);
	live.commands().append(selectMsg(pi, plot->getID())); // the Porter
	live.commands().append(dozerMsg(pi, *tt, site));
	auto step = [&]() {
		logic.runLogicFrame();
		out.hashes.push_back(logic.computeStateHash());
	};
	step();
	// the foundation exists
	Object *building = nullptr;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getTemplate() == tt && o->getControllingPlayer() == player)
		{
			building = o;
		}
	}
	if (!building)
	{
		out.problems.push_back("the foundation was not made");
		return;
	}
	out.placedFrame = (int)logic.getFrame();
	out.moneyAfterPlace = (int)player->getMoney()->countMoney();
	if (!building->isUnderConstruction() || building->getConstructionPercent() < 0.0f)
	{
		out.problems.push_back("the foundation is not under construction");
		return;
	}
	out.commandPointsDuring = player->commandPoints().getUsage();
	out.buildingId = building->getID();
	auto cellOf = [&](Object &o) {
		PathfindCell *c = live.ai().pathfinder().getCell(LAYER_GROUND, o.getPosition());
		return c ? (int)c->getType() : -2;
	};
	out.cellDuring = cellOf(*building);
	out.cpLimitDuring = player->commandPointLimit();
	// rise
	int guard = out.expectedFrames + 1500; // the builder walks to the site first
	while (building->isUnderConstruction() && guard-- > 0)
	{
		step();
	}
	out.completeFrame = (int)logic.getFrame();
	if (DozerAIUpdate *dz = dynamic_cast<DozerAIUpdate *>(plot->getAIUpdateInterface()))
	{
		out.workFrames = (int)dz->taskWorkFrames();
	}
	out.accumulateFrames = buildtest::framesToAccumulate(out.expectedFrames);
	out.framesUsed = out.completeFrame - out.placedFrame;
	if (building->isUnderConstruction())
	{
		out.problems.push_back("the building never completed");
		return;
	}
	out.commandPointsAfter = player->commandPoints().getUsage();
	out.cpLimitAfter = player->commandPointLimit();
	out.cellAfter = cellOf(*building);
	if (CastleBehavior *cb = dynamic_cast<CastleBehavior *>(building->findModule("CastleBehavior")))
	{
		step(); // the unpack is the castle's first update
		step();
		out.castlePieces = cb->ownedObjects().size();
		out.castleError = cb->lastError();
		for (ObjectID id : cb->ownedObjects())
		{
			if (Object *o = logic.findObjectByID(id))
			{
				out.castlePlots += o->isKindOfName("BASE_FOUNDATION") ? 1 : 0;
			}
		}
	}
	if (!produce)
	{
		logic.destroyObject(building); // death or sale: the footprint leaves the pathfinder's map (AIWorld::objectLeftWorld)
		step();
		step();
		PathfindCell *c = live.ai().pathfinder().getCell(LAYER_GROUND, &site);
		out.cellDestroyed = c ? (int)c->getType() : -2;
		return;
	}
	if (building->getConstructionPercent() != -1.0f)
	{
		out.problems.push_back("the percent of a finished building is not -1");
	}
	// production: the first unit its command set offers
	const CommandSet *set = nullptr;
	const std::string setName = building->getCommandSetName();
	if (TheCommandStore)
	{
		set = TheCommandStore->findCommandSet(setName);
	}
	const ThingTemplate *unit = nullptr;
	for (int slot = 0; set && slot < CommandSet::MAX_BUTTONS && !unit; ++slot)
	{
		const CommandButton *btn = set->getCommandButton(slot);
		if (btn && btn->m_command == GUI_COMMAND_UNIT_BUILD && btn->getThingTemplate() && BuildAssistant::canMakeUnit(*building, btn->getThingTemplate(), -1) == CANMAKE_OK)
		{
			unit = btn->getThingTemplate();
		}
	}
	if (!unit)
	{
		out.problems.push_back("the finished building offers no unit (command set '" + setName + "')");
		return;
	}
	out.unit = unit->getName();
	live.commands().append(selectMsg(pi, building->getID()));
	live.commands().append(queueMsg(pi, *unit->getFinalOverride()));
	const size_t objectsBefore = logic.getObjectCount();
	const int t0 = (int)logic.getFrame();
	for (int i = 0; i < 1500 && out.unitFrame < 0; ++i)
	{
		step();
		for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
		{
			if (o->getControllingPlayer() == player && o->getProducerID() == building->getID() && o != building && o->getTemplate()->getFinalOverride() == unit->getFinalOverride())
			{
				out.unitFrame = (int)logic.getFrame() - t0;
				break;
			}
		}
	}
	(void)objectsBefore;
	if (out.unitFrame < 0)
	{
		out.problems.push_back("the building produced no " + out.unit);
	}
}

void check(const Barracks &b)
{
	OPENBFME_REQUIRE_START(s);
	INFO(b.faction << " " << b.building);
	Outcome a, c;
	run(*s, b, a);
	for (const std::string &p : a.problems)
	{
		FAIL_CHECK(p);
	}
	if (!a.problems.empty())
	{
		return;
	}
	// the price is paid when the plot places it; the foundation rises for BuildTime; the building then counts and produces
	CHECK(a.moneyBefore - a.moneyAfterPlace == a.expectedCost);
	CHECK(a.expectedCost > 0);
	CHECK(a.expectedCost == a.iniCost);
	CHECK(a.expectedFrames == a.iniFrames);
	CHECK(a.workFrames == a.accumulateFrames); // the Porter's work frames: 100 / T added in float until 100, whatever the walk took
	CHECK(a.accumulateFrames >= a.expectedFrames);
	CHECK(a.accumulateFrames <= a.expectedFrames + 1);
	CHECK(a.framesUsed >= a.expectedFrames);
	CHECK(a.framesUsed < a.expectedFrames + 1200);
	CHECK(a.expectedFrames > 0);
	CHECK(a.unitFrame > 0);
	std::printf("  info: %s: cost %d, %d frames (work %d, used %d), first unit %s after %d frames; command points %d -> %d -> %d\n", b.building, a.expectedCost, a.expectedFrames, a.workFrames, a.framesUsed, a.unit.c_str(),
		a.unitFrame, a.commandPointsBefore, a.commandPointsDuring, a.commandPointsAfter);
	// two runs agree on every frame hash
	run(*s, b, c);
	CHECK(c.problems.empty());
	CHECK(c.hashes == a.hashes);
	CHECK(a.hashes.size() > 100);
}
} // namespace

TEST_CASE("construction: FactionMen builds GondorBarracks with its Porter, it rises, completes and produces") { check(kBarracks[0]); }
TEST_CASE("construction: FactionElves builds ElvenBarracks with its Porter, it rises, completes and produces") { check(kBarracks[1]); }
TEST_CASE("construction: FactionDwarves builds DwarfBarracks with its Porter, it rises, completes and produces") { check(kBarracks[2]); }
TEST_CASE("construction: FactionIsengard builds IsengardUrukPit with its Porter, it rises, completes and produces") { check(kBarracks[3]); }
TEST_CASE("construction: FactionMordor builds MordorOrcPit with its Porter, it rises, completes and produces") { check(kBarracks[4]); }
TEST_CASE("construction: FactionWild builds GoblinCave with its Porter, it rises, completes and produces") { check(kBarracks[5]); }
TEST_CASE("construction: FactionAngmar builds AngmarBarracks with its Porter, it rises, completes and produces") { check(kBarracks[6]); }

TEST_CASE("construction: a Porter builds a GondorFarm: the price is paid at placement and the command point limit grows by its bonus when it completes")
{
	OPENBFME_REQUIRE_START(s);
	Outcome o;
	run(*s, { "FactionMen", "GondorFarm" }, o, false);
	for (const std::string &p : o.problems)
	{
		FAIL_CHECK(p);
	}
	if (!o.problems.empty())
	{
		return;
	}
	CHECK(o.moneyBefore - o.moneyAfterPlace == o.expectedCost);
	CHECK(o.workFrames == o.accumulateFrames);
	// GENERIC_ECONOMY_COMMAND_POINT_BONUS (gamedata.ini) joins the limit at completion (RW 0x6AA7A8 -> 0x68E0C2 -> RW 0x6AA56D addBonus), not before
	CHECK(o.cpLimitDuring == o.cpLimitBefore); // a structure under construction counts nothing (RW 0x68E0C2)
	// the pathfinder: a foundation is an obstacle from the moment it is placed (RW 0x936C57 excludes only PHANTOM_STRUCTURE), the finished one blocks its cell and the cell is free again after it was destroyed
	CHECK(o.cellDuring == (int)PathfindCell::CELL_OBSTACLE);
	CHECK(o.cellAfter == (int)PathfindCell::CELL_OBSTACLE);
	CHECK(o.cellDestroyed != (int)PathfindCell::CELL_OBSTACLE);
	CHECK(o.cpLimitAfter > o.cpLimitBefore);
	std::printf("  info: GondorFarm: command point limit %d -> %d\n", o.cpLimitBefore, o.cpLimitAfter);
}

TEST_CASE("construction: a Porter builds a MenFortress: on completion it unpacks the Bases.big layout around itself (instant unpack)")
{
	OPENBFME_REQUIRE_START(s);
	Outcome o;
	run(*s, { "FactionMen", "MenFortress" }, o, false);
	for (const std::string &p : o.problems)
	{
		FAIL_CHECK(p);
	}
	if (!o.problems.empty())
	{
		return;
	}
	CHECK(o.moneyBefore - o.moneyAfterPlace == o.expectedCost);
	CHECK(o.workFrames == o.accumulateFrames);
	CHECK(o.castleError.empty());
	// the layout of fortress_men: the keep and the six pads of the independent survey (castle-survey.json: 7 entries)
	CHECK(o.castlePieces == 7);
	CHECK(o.castlePlots == 6);
}

namespace
{
// a Porter of FactionMen, its fortress and the first legal site for a GondorBarracks around it
struct SiteFx
{
	buildtest::Game g;
	Player *player = nullptr;
	Object *porter = nullptr, *centre = nullptr;
	const ThingTemplate *barracks = nullptr;
	Coord3D site;
	bool ready(starttest::Shared &s, std::string &error)
	{
		if (!buildtest::startGame(s, "FactionMen", "FactionMordor", 5150, g, &error))
		{
			return false;
		}
		GameLogic &logic = g.live->logic();
		player = g.live->players().findPlayerWithName(g.live->report().startSlotPlayers[0]);
		barracks = logic.things().findTemplate("GondorBarracks")->getFinalOverride();
		for (const StartingBase::Placed &p : g.live->report().startingObjects)
		{
			Object *o = p.structure ? logic.findObjectByID(p.id) : nullptr;
			centre = (o && o->getControllingPlayer() == player) ? o : centre;
		}
		for (Object *o = logic.getFirstObject(); o && !porter; o = o->getNextObject())
		{
			porter = (o->getControllingPlayer() == player && o->isKindOfName("DOZER") && dynamic_cast<DozerAIUpdate *>(o->getAIUpdateInterface())) ? o : nullptr;
		}
		if (!centre || !porter)
		{
			error = "no fortress or Porter";
			return false;
		}
		for (float r = 180.0f; r <= 520.0f; r += 40.0f)
		{
			for (int k = 0; k < 16; ++k)
			{
				Coord3D c = *centre->getPosition();
				c.x += r * std::cos((float)k * 0.3927f);
				c.y += r * std::sin((float)k * 0.3927f);
				c.z = logic.getGroundHeight(c.x, c.y);
				if (BuildPlacement::isLocationLegalToBuild(logic, c, *barracks, 0.0f, DozerAIUpdate::kConstructLegalFlags, porter, player) == LBC_OK)
				{
					site = c;
					return true;
				}
			}
		}
		error = "no legal site";
		return false;
	}
	size_t count(const char *name)
	{
		size_t n = 0;
		for (Object *o = g.live->logic().getFirstObject(); o; o = o->getNextObject())
		{
			n += o->getTemplate()->getName() == name ? 1 : 0;
		}
		return n;
	}
	void send(const Coord3D &where)
	{
		g.live->commands().append(selectMsg(player->getPlayerIndex(), porter->getID()));
		g.live->commands().append(dozerMsg(player->getPlayerIndex(), *barracks, where));
	}
};
} // namespace

TEST_CASE("construction legality: the Dozer construct body refuses an overlapping, an off-map and a same-frame duplicate site without creating, paying or tasking (RW 0x88C2A5)")
{
	OPENBFME_REQUIRE_START(s);
	std::string error;
	SiteFx f;
	REQUIRE_MESSAGE(f.ready(*s, error), error);
	GameLogic &logic = f.g.live->logic();
	DozerAIUpdate *dozer = dynamic_cast<DozerAIUpdate *>(f.porter->getAIUpdateInterface());
	const std::uint32_t money = f.player->getMoney()->countMoney();
	const size_t objects = logic.getObjectCount();
	BuildCommands &commands = f.g.live->buildCommands();
	REQUIRE(commands.stats().refused == 0);
	// on the fortress itself
	f.send(*f.centre->getPosition());
	logic.runLogicFrame();
	CHECK(f.count("GondorBarracks") == 0);
	CHECK(logic.getObjectCount() == objects);
	CHECK(f.player->getMoney()->countMoney() == money);
	CHECK(dozer->taskTarget() == INVALID_ID);
	CHECK(commands.stats().refused == 1);
	// far outside the map
	f.send(Coord3D{ -9000.0f, -9000.0f, 0.0f });
	logic.runLogicFrame();
	CHECK(f.count("GondorBarracks") == 0);
	CHECK(f.player->getMoney()->countMoney() == money);
	CHECK(dozer->taskTarget() == INVALID_ID);
	CHECK(commands.stats().refused == 2);
	// the same legal site twice in one frame: the first is built, the second finds the first's foundation in the way
	f.g.live->commands().append(selectMsg(f.player->getPlayerIndex(), f.porter->getID()));
	f.g.live->commands().append(dozerMsg(f.player->getPlayerIndex(), *f.barracks, f.site));
	f.g.live->commands().append(dozerMsg(f.player->getPlayerIndex(), *f.barracks, f.site));
	logic.runLogicFrame();
	CHECK(f.count("GondorBarracks") == 1);
	CHECK(commands.stats().dozerBuilds == 1);
	CHECK(commands.stats().refused == 3);
	CHECK(money - f.player->getMoney()->countMoney() == (std::uint32_t)BuildAssistant::calcCostToBuild(*f.barracks, f.player, f.porter, -1));
	CHECK(dozer->taskTarget() != INVALID_ID);
}

TEST_CASE("pathfinder footprints: an ordinary foundation is an obstacle at once; only PHANTOM_STRUCTURE (RW 0x936C57) keeps a structure out of the map")
{
	OPENBFME_REQUIRE_START(s);
	std::string error;
	SiteFx f;
	REQUIRE_MESSAGE(f.ready(*s, error), error);
	GameLogic &logic = f.g.live->logic();
	Team *team = f.player->getDefaultTeam();
	auto cellAt = [&](const Coord3D &p) {
		PathfindCell *c = f.g.live->ai().pathfinder().getCell(LAYER_GROUND, &p);
		return c ? (int)c->getType() : -2;
	};
	auto place = [&](const Coord3D &p, int status) {
		ObjectStatusMaskType mask{};
		if (status >= 0)
		{
			MaskSet(mask, (unsigned)status, true);
		}
		Object *o = logic.newObject(f.barracks, team, mask);
		Coord3D q = p;
		o->setPosition(&q);
		f.g.live->ai().addObjectToPathfindMap(*o);
		return o;
	};
	const int phantom = ObjectTemplateInfoBuilder::objectStatusIndex("PHANTOM_STRUCTURE");
	REQUIRE(phantom >= 0);
	Coord3D a = f.site, b = f.site;
	b.x += 400.0f;
	b.z = logic.getGroundHeight(b.x, b.y);
	CHECK(cellAt(a) != (int)PathfindCell::CELL_OBSTACLE);
	place(a, OBJECT_STATUS_UNDER_CONSTRUCTION);
	CHECK(cellAt(a) == (int)PathfindCell::CELL_OBSTACLE);
	CHECK(cellAt(b) != (int)PathfindCell::CELL_OBSTACLE);
	place(b, phantom);
	CHECK(cellAt(b) != (int)PathfindCell::CELL_OBSTACLE);
}
