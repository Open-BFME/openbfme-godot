// OpenBFME retail tests (lane BUILD-2): RotWK's construction rate. Each faction's starting fortress unpacks its build plots; a plot builds the first structure its
// command set offers (FoundationAIUpdate::construct, RW 0x858701): the foundation starts at 1.0 health; its GettingBuiltBehavior (RW 0x857E77) either spawns the
// template's WorkerName, whose WorkerAIUpdate adds 100 / calcTimeToBuild to the percent per frame of work (RW 0x88DE43) and fades away once the structure is done
// (RW 0x857238: UNRESISTABLE / DEATH_FADED), or builds itself by healing MaxHealth / calcTimeToBuild per frame (the percent is the health). A Porter repairs a damaged
// structure at RepairHealthPercentPerSecond * MaxHealth / 5 per frame for free (RW 0x88DC46). The expected numbers come from independent float models here.
// SKIP loudly without ROTWK_INSTALL / BFME2_INSTALL.  GPL-3.0.

#include "doctest.h"

#include "BuildTestUtil.h"

#include "Common/BuildAssistant.h"
#include "Common/Player.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/ControlBarCommands.h"
#include "GameLogic/Construction.h"
#include "GameLogic/Damage.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/CastleModules.h"
#include "GameLogic/Module/ConstructionModules.h"
#include "GameLogic/Module/DozerAIUpdate.h"
#include "GameLogic/Object/Object.h"

#include <cstdio>
#include <string>
#include <vector>

namespace
{
const char *const kFactions[] = { "FactionMen", "FactionElves", "FactionDwarves", "FactionIsengard", "FactionMordor", "FactionWild", "FactionAngmar" };

struct PlotBuild
{
	std::string problem, structure, worker;
	bool selfBuild = false;
	int buildFrames = 0;     // calcTimeToBuild of the builder (the worker, or none for a self build)
	int workFrames = 0;      // the worker's frames of work
	int heals = 0;           // the self build's heals
	int expected = 0;        // the independent model's count
	int framesToComplete = 0;
	bool workerFaded = false;
	float startHealth = 0.0f;
	std::vector<std::uint32_t> hashes;
};

// the independent model of a self build: health starts at 1.0, each heal adds (float)(max / T) and clamps at max (RW 0x8C31A5), done when health >= max
int selfHeals(float maxHealth, int frames)
{
	volatile float amount = maxHealth / (float)frames;
	volatile float h = 1.0f;
	int n = 0;
	while (h < maxHealth && n < 1000000)
	{
		h = h + amount;
		if (h > maxHealth)
		{
			h = maxHealth;
		}
		++n;
	}
	return n;
}

void buildOnPlot(starttest::Shared &s, const char *faction, PlotBuild &out)
{
	std::string error;
	buildtest::Game g;
	if (!buildtest::startGame(s, faction, "FactionMen", 5150, g, &error))
	{
		out.problem = error;
		return;
	}
	LiveGame &live = *g.live;
	GameLogic &logic = live.logic();
	Player *player = live.players().findPlayerWithName(live.report().startSlotPlayers[0]);
	for (int i = 0; i < 3; ++i)
	{
		logic.runLogicFrame(); // the fortress unpacks its plots
	}
	Object *plot = nullptr;
	const ThingTemplate *what = nullptr;
	for (Object *o = logic.getFirstObject(); o && !what; o = o->getNextObject())
	{
		if (o->getControllingPlayer() != player || !o->isKindOfName("BASE_FOUNDATION") || !TheCommandStore)
		{
			continue;
		}
		const CommandSet *set = TheCommandStore->findCommandSet(o->getCommandSetName());
		for (int slot = 0; set && slot < CommandSet::MAX_BUTTONS && !what; ++slot)
		{
			const CommandButton *btn = set->getCommandButton(slot);
			if (btn && btn->m_command == GUI_COMMAND_FOUNDATION_CONSTRUCT && btn->getThingTemplate() && btn->getThingTemplate()->findField("BuildCost"))
			{
				what = btn->getThingTemplate()->getFinalOverride();
				plot = o;
			}
		}
	}
	if (!what)
	{
		out.problem = "no plot of the fortress offers a FOUNDATION_CONSTRUCT structure";
		return;
	}
	out.structure = what->getName();
	Object *building = Construction::constructOnPlot(*plot, *what, *plot->getPosition(), 0.0f, *player, false);
	if (!building)
	{
		out.problem = "the plot refused " + out.structure;
		return;
	}
	GettingBuiltBehavior *gb = dynamic_cast<GettingBuiltBehavior *>(building->findModule("GettingBuiltBehavior"));
	if (!gb || !building->getBodyModule())
	{
		out.problem = out.structure + " has no GettingBuiltBehavior or body";
		return;
	}
	out.startHealth = building->getBodyModule()->getHealth();
	out.worker = gb->data()->m_workerName;
	out.selfBuild = out.worker.empty();
	const UnsignedInt placed = logic.getFrame();
	Object *worker = nullptr;
	float last = out.startHealth;
	int guard = 4000;
	while (building->getConstructionPercent() != -1.0f && guard-- > 0)
	{
		logic.runLogicFrame();
		out.hashes.push_back(logic.computeStateHash());
		if (!worker && building->getBuilderID() != INVALID_ID && building->getBuilderID() != building->getID())
		{
			worker = logic.findObjectByID(building->getBuilderID());
		}
		const float h = building->getBodyModule()->getHealth();
		if (out.selfBuild && h != last)
		{
			++out.heals;
		}
		last = h;
	}
	out.framesToComplete = (int)(logic.getFrame() - placed);
	if (building->getConstructionPercent() != -1.0f)
	{
		out.problem = out.structure + " never completed";
		return;
	}
	if (out.selfBuild)
	{
		out.buildFrames = gb->completedOnce() ? BuildAssistant::calcTimeToBuild(*what, player, nullptr, -1, logic.productionSettings(), logic) : 0;
		out.expected = selfHeals(building->getBodyModule()->getMaxHealth(), out.buildFrames);
		return;
	}
	if (!worker)
	{
		out.problem = "no worker built " + out.structure;
		return;
	}
	DozerAIUpdate *dz = dynamic_cast<DozerAIUpdate *>(worker->getAIUpdateInterface());
	out.workFrames = dz ? (int)dz->taskWorkFrames() : -1;
	out.buildFrames = BuildAssistant::calcTimeToBuild(*what, player, worker, -1, logic.productionSettings(), logic);
	out.expected = buildtest::framesToAccumulate(out.buildFrames);
	const ObjectID workerId = worker->getID();
	for (int i = 0; i < 40; ++i)
	{
		logic.runLogicFrame();
		Object *w = logic.findObjectByID(workerId);
		if (!w || w->isEffectivelyDead())
		{
			out.workerFaded = true;
			break;
		}
	}
}
} // namespace

TEST_CASE("build rate: each faction's fortress plot builds its first structure at the retail rate (a spawned worker's accumulation or the self build's healing)")
{
	OPENBFME_REQUIRE_START(s);
	for (const char *faction : kFactions)
	{
		INFO(faction);
		PlotBuild a;
		buildOnPlot(*s, faction, a);
		if (!a.problem.empty())
		{
			FAIL_CHECK(a.problem);
			continue;
		}
		CHECK(a.startHealth == 1.0f); // RW 0x85895D
		if (a.selfBuild)
		{
			CHECK(a.heals == a.expected);
		}
		else
		{
			CHECK(a.workFrames == a.expected);
			CHECK(a.workerFaded); // RW 0x857BDA then 0x857238: idle, then killed with DEATH_FADED
		}
		std::printf("  info: %s: %s %s, calcTimeToBuild %d, %s %d (model %d), complete after %d frames\n", faction, a.structure.c_str(), a.selfBuild ? "builds itself" : ("worker " + a.worker).c_str(),
			a.buildFrames, a.selfBuild ? "heals" : "work frames", a.selfBuild ? a.heals : a.workFrames, a.expected, a.framesToComplete);
		PlotBuild b;
		buildOnPlot(*s, faction, b);
		CHECK(b.hashes == a.hashes); // two runs agree on every frame hash
	}
}

TEST_CASE("build rate: a Porter repairs a damaged GondorBarracks at RepairHealthPercentPerSecond * MaxHealth / 5 per frame, for free (MSG_DO_REPAIR)")
{
	OPENBFME_REQUIRE_START(s);
	std::string error;
	buildtest::Game g;
	REQUIRE_MESSAGE(buildtest::startGame(*s, "FactionMen", "FactionMordor", 5150, g, &error), error);
	LiveGame &live = *g.live;
	GameLogic &logic = live.logic();
	Player *player = live.players().findPlayerWithName(live.report().startSlotPlayers[0]);
	const int pi = player->getPlayerIndex();
	Object *porter = nullptr, *centre = nullptr;
	for (const StartingBase::Placed &p : live.report().startingObjects)
	{
		Object *o = p.structure ? logic.findObjectByID(p.id) : nullptr;
		centre = (o && o->getControllingPlayer() == player) ? o : centre;
	}
	for (Object *o = logic.getFirstObject(); o && !porter; o = o->getNextObject())
	{
		porter = (o->getControllingPlayer() == player && o->isKindOfName("DOZER") && dynamic_cast<DozerAIUpdate *>(o->getAIUpdateInterface())) ? o : nullptr;
	}
	REQUIRE((porter && centre));
	const ThingTemplate *tt = logic.things().findTemplate("GondorBarracks")->getFinalOverride();
	Coord3D at = *porter->getPosition();
	at.x += 120.0f;
	Object *barracks = Construction::buildObjectNow(logic, nullptr, *tt, at, 0.0f, *player); // complete at once (BuildAssistant slot 0x38, no dozer)
	REQUIRE(barracks != nullptr);
	for (int i = 0; i < 6; ++i)
	{
		logic.runLogicFrame();
	}
	BodyModuleInterface *body = barracks->getBodyModule();
	const float maxHealth = body->getMaxHealth();
	DamageInfo info;
	info.m_input.m_damageType = DAMAGE_UNRESISTABLE;
	info.m_input.m_amount = maxHealth * 0.5f;
	barracks->attemptDamage(info);
	const float damaged = body->getHealth();
	REQUIRE(damaged < maxHealth);
	DozerAIUpdate *dz = dynamic_cast<DozerAIUpdate *>(porter->getAIUpdateInterface());
	const std::uint32_t money = player->getMoney()->countMoney();
	GameMessage sel(MSG_CREATE_SELECTED_GROUP, pi);
	sel.appendBooleanArgument(true);
	sel.appendObjectIDArgument(porter->getID());
	live.commands().append(sel);
	GameMessage rep(MSG_DO_REPAIR, pi);
	rep.appendObjectIDArgument(barracks->getID());
	live.commands().append(rep);
	logic.runLogicFrame();
	CHECK(dz->repairing());
	// the independent model: amount = (float)(max * rate / 5); heals until the health is full (clamped)
	volatile float amount = maxHealth * dz->repairHealthPercentPerSecond() / 5.0f;
	int heals = 0, frames = 0;
	float last = body->getHealth();
	while (body->getHealth() < maxHealth && frames < 20000)
	{
		logic.runLogicFrame();
		++frames;
		const float h = body->getHealth();
		if (h != last)
		{
			if (h < maxHealth)
			{
				CHECK(h - last == doctest::Approx((float)amount));
			}
			++heals;
			last = h;
		}
	}
	int expected = 0;
	for (volatile float h = damaged; h < maxHealth; ++expected)
	{
		h = h + amount;
	}
	CHECK(body->getHealth() == maxHealth);
	CHECK(heals == expected);
	CHECK(player->getMoney()->countMoney() >= money); // the dozer's repair costs nothing (RW 0x88DB9A); the fortress's income goes on
	logic.runLogicFrame();
	CHECK_FALSE(dz->repairing());
	std::printf("  info: Porter repair of GondorBarracks: %.1f -> %.1f health, %.4f per frame (%.2f %% of max per second), %d heals, %d frames\n", damaged, maxHealth, (float)amount,
		dz->repairHealthPercentPerSecond() * 100.0f, heals, frames);
}

// ---- lane BUILD-2: walls (WallHubBehavior, MSG_WALL_HUB_CONSTRUCT_SPAN) -------------------------------------------------------------------------------------------------
#include "GameLogic/WallSpan.h"

namespace
{
struct WallRun
{
	std::string problem;
	WallSpan::Plan plan;
	std::vector<std::string> names;
	std::vector<int> startFrames; // the frame each tile first gained health
	int completeFrames = -1;
	int moneyBefore = 0, moneyAfter = 0;
	float halfSegment = 0.0f, halfCap = 0.0f;
	std::vector<std::uint32_t> hashes;
};

void wallSpan(starttest::Shared &s, WallRun &out)
{
	std::string error;
	buildtest::Game g;
	if (!buildtest::startGame(s, "FactionMen", "FactionMordor", 5150, g, &error))
	{
		out.problem = error;
		return;
	}
	LiveGame &live = *g.live;
	GameLogic &logic = live.logic();
	Player *player = live.players().findPlayerWithName(live.report().startSlotPlayers[0]);
	for (int i = 0; i < 3; ++i)
	{
		logic.runLogicFrame();
	}
	Object *centre = nullptr, *plot = nullptr;
	for (const StartingBase::Placed &p : live.report().startingObjects)
	{
		Object *o = p.structure ? logic.findObjectByID(p.id) : nullptr;
		centre = (o && o->getControllingPlayer() == player) ? o : centre;
	}
	float best = -1.0f;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() == player && o->isKindOfName("BASE_FOUNDATION") && centre)
		{
			const float dx = o->getPosition()->x - centre->getPosition()->x, dy = o->getPosition()->y - centre->getPosition()->y;
			if (dx * dx + dy * dy > best)
			{
				best = dx * dx + dy * dy;
				plot = o; // the outermost pad
			}
		}
	}
	const ThingTemplate *hubTmpl = logic.things().findTemplate("MenWallHubSmallExpansion");
	const ThingTemplate *capTmpl = logic.things().findTemplate("MenWallHubSmall");
	const ThingTemplate *segTmpl = logic.things().findTemplate("MenWallSegmentSmall");
	if (!centre || !plot || !hubTmpl || !capTmpl || !segTmpl)
	{
		out.problem = "no fortress pad or wall templates";
		return;
	}
	out.halfSegment = WallSpan::halfExtentY(*segTmpl->getFinalOverride());
	out.halfCap = WallSpan::halfExtentY(*capTmpl->getFinalOverride());
	Object *hub = Construction::constructOnPlot(*plot, *hubTmpl->getFinalOverride(), *plot->getPosition(), 0.0f, *player, true);
	if (!hub)
	{
		out.problem = "the pad refused the wall hub";
		return;
	}
	logic.runLogicFrame();
	// the span: outward from the fortress centre, 400 units
	Coord3D start = *hub->getPosition(), end = start;
	const float dx = start.x - centre->getPosition()->x, dy = start.y - centre->getPosition()->y;
	const float len = std::sqrt(dx * dx + dy * dy);
	end.x += dx / len * 400.0f;
	end.y += dy / len * 400.0f;
	const unsigned optionOne = 1u << 13; // OPTION_ONE (RW 0xDAE268 index 13), the button's and the hub module's Options
	WallSpan::plan(logic, *hub, start, end, optionOne, out.plan);
	for (const WallSpan::Tile &t : out.plan.tiles)
	{
		out.names.push_back(t.tmpl ? t.tmpl->getName() : "?");
	}
	out.moneyBefore = (int)player->getMoney()->countMoney();
	GameMessage m(MSG_WALL_HUB_CONSTRUCT_SPAN, player->getPlayerIndex());
	m.appendIntegerArgument((int)capTmpl->getFinalOverride()->getTemplateID());
	m.appendLocationArgument(start);
	m.appendLocationArgument(end);
	m.appendIntegerArgument((int)optionOne);
	m.appendObjectIDArgument(hub->getID());
	live.commands().append(m);
	logic.runLogicFrame();
	out.moneyAfter = (int)player->getMoney()->countMoney();
	std::vector<Object *> tiles;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getProducerID() == hub->getID() && o->getControllingPlayer() == player && o->isKindOfName("STRUCTURE"))
		{
			tiles.push_back(o);
		}
	}
	if (tiles.size() != out.plan.tiles.size())
	{
		out.problem = "the span made " + std::to_string(tiles.size()) + " objects for a plan of " + std::to_string(out.plan.tiles.size()) + ", codes";
		for (const WallSpan::Tile &t : out.plan.tiles)
		{
			out.problem += " " + std::to_string(t.code);
		}
		out.problem += ", cost " + std::to_string(out.plan.cost) + ", money " + std::to_string(out.moneyBefore);
		for (const std::string &r : live.buildCommands().refusals())
		{
			out.problem += "; refused: " + r;
		}
		return;
	}
	out.startFrames.assign(tiles.size(), -1);
	std::vector<float> last(tiles.size());
	for (size_t i = 0; i < tiles.size(); ++i)
	{
		last[i] = tiles[i]->getBodyModule()->getHealth();
	}
	const UnsignedInt t0 = logic.getFrame();
	for (int f = 0; f < 4000; ++f)
	{
		logic.runLogicFrame();
		out.hashes.push_back(logic.computeStateHash());
		bool all = true;
		for (size_t i = 0; i < tiles.size(); ++i)
		{
			const float h = tiles[i]->getBodyModule()->getHealth();
			if (out.startFrames[i] < 0 && h != last[i])
			{
				out.startFrames[i] = (int)(logic.getFrame() - t0);
			}
			last[i] = h;
			all = all && tiles[i]->getConstructionPercent() == -1.0f;
		}
		if (all)
		{
			out.completeFrames = (int)(logic.getFrame() - t0);
			break;
		}
	}
}
} // namespace

TEST_CASE("walls: a Men wall hub on a fortress pad builds a span (MSG_WALL_HUB_CONSTRUCT_SPAN): the pattern, the tile length, the cap, the price, the staggered rise")
{
	OPENBFME_REQUIRE_START(s);
	WallRun a;
	wallSpan(*s, a);
	REQUIRE_MESSAGE(a.problem.empty(), a.problem);
	REQUIRE(a.plan.tiles.size() >= 3);
	INFO("worst code " << a.plan.worstCode);
	CHECK(a.plan.worstCode == 0);
	// the pattern: segments, the hub cap last (no end hub near the end point)
	for (size_t i = 0; i + 1 < a.names.size(); ++i)
	{
		CHECK((a.names[i] == "MenWallSegmentSmall" || a.names[i] == "MenWallHubSmall"));
	}
	CHECK(a.names.back() == "MenWallHubSmall");
	// tile centres are 2 * (half extent - 1) apart along the span; all tiles share the span's angle
	for (size_t i = 1; i + 1 < a.plan.tiles.size(); ++i)
	{
		const float ddx = a.plan.tiles[i].pos.x - a.plan.tiles[i - 1].pos.x, ddy = a.plan.tiles[i].pos.y - a.plan.tiles[i - 1].pos.y;
		CHECK(std::sqrt(ddx * ddx + ddy * ddy) == doctest::Approx(2.0f * (a.halfSegment - 1.0f)).epsilon(1e-4));
		CHECK(a.plan.tiles[i].angle == a.plan.tiles[0].angle);
	}
	CHECK(a.moneyBefore - a.moneyAfter == a.plan.cost);
	CHECK(a.plan.cost > 0);
	// the segments rise in a wave: tile i starts StaggeredBuildFactor (20) * i frames after the first (the cap, a WALL_HUB, is not staggered)
	CHECK(a.startFrames[0] == 1); // tile 0 runs at once (RW 0x8574F0: the update now, the wake now + 0) and heals in the message's frame already: the change seen is its second heal
	for (size_t i = 2; i + 1 < a.startFrames.size(); ++i)
	{
		if (a.names[i] == "MenWallSegmentSmall" && a.names[i - 1] == "MenWallSegmentSmall")
		{
			CHECK(a.startFrames[i] - a.startFrames[i - 1] == 20);
		}
	}
	CHECK(a.completeFrames > 0);
	std::printf("  info: Men wall span: %zu tiles (half extent segment %.2f, cap %.2f), cost %d, complete after %d frames; tile starts:", a.plan.tiles.size(), a.halfSegment, a.halfCap,
		a.plan.cost, a.completeFrames);
	for (int f : a.startFrames)
	{
		std::printf(" %d", f);
	}
	std::printf("\n");
	WallRun b;
	wallSpan(*s, b);
	CHECK(b.hashes == a.hashes);
}
