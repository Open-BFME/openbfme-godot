// OpenBFME. GPL-3.0. Terrain termination carried from the fork main branch.
#include "doctest.h"
#include "CombatTestUtil.h"
#include "Common/Money.h"
#include "Common/Player.h"
#include "GameLogic/WallSpan.h"
using namespace combattest;

namespace
{
const char kDefinitions[] =
 "Object TerrainWallHub\n KindOf = STRUCTURE IMMOBILE WALL_HUB\n BuildCost = 20\n Geometry = BOX\n GeometryMajorRadius = 4\n GeometryMinorRadius = 10\n GeometryHeight = 20\n Behavior = WallHubBehavior Hub\n SegmentTemplateName = TerrainWallTile\n DefaultSegmentTemplateName = TerrainWallTile\n HubCapTemplateName = TerrainWallHub\n CliffCapTemplateName = TerrainWallEnd\n BuilderRadius = 10\n End\nEnd\n"
 "Object TerrainWallTile\n KindOf = STRUCTURE IMMOBILE WALL_SEGMENT\n BuildCost = 10\n Geometry = BOX\n GeometryMajorRadius = 4\n GeometryMinorRadius = 10\n GeometryHeight = 20\nEnd\n"
 "ChildObject TerrainWallEnd TerrainWallTile\n BuildCost = 7\nEnd\n"
 "Object TerrainWallCC\n KindOf = STRUCTURE IMMOBILE COMMANDCENTER\nEnd\n";

Object *prepare(CombatWorld &w)
{
	w.logic->settings().buildRulesLoaded = true;
	w.logic->settings().allowedHeightVariationForBuilding = 10;
	w.logic->settings().maxLineBuildObjects = 50;
	Object *hub = w.unit("TerrainWallHub", 'A', 300, 500);
	w.unit("TerrainWallCC", 'A', 100, 100); // lane BUILDCLEAR-1: the hub caps (WALL_HUB builds) need a COMMANDCENTER within MaxBuildoutDistance (RW 0x796C66)
	hub->getControllingPlayer()->getMoney()->deposit(1000, false);
	for (int y = 0; y < 100; ++y)
		for (int x = 45; x < 50; ++x) w.terrain.cliff.insert({x, y});
	return hub;
}
} // namespace

TEST_CASE("wall span terminates at terrain with a hub and cliff cap")
{
	CombatWorld w(kDefinitions);
	Object *hub = prepare(w);
	WallSpan::Plan plan;
	REQUIRE(WallSpan::plan(*w.logic, *hub, {300,500,0}, {700,500,0}, 0, plan));
	REQUIRE(plan.tiles.size() == 8);
	CHECK(plan.worstCode == 0);
	CHECK(plan.endCapPlaced);
	CHECK(plan.tiles.back().tmpl->getName() == "TerrainWallEnd");
	CHECK(plan.tiles[6].tmpl->getName() == "TerrainWallHub");
	CHECK(plan.tiles.back().pos.x == 445.0f);
	CHECK(plan.cost == 87); // six segments, one hub, one cliff cap
	const auto money = hub->getControllingPlayer()->getMoney()->countMoney();
	std::vector<ObjectID> made;
	REQUIRE(WallSpan::build(*w.logic, *hub, {300,500,0}, {700,500,0}, *hub->getControllingPlayer(), 0, &made));
	CHECK(made.size() == plan.tiles.size());
	CHECK(hub->getControllingPlayer()->getMoney()->countMoney() == money - 87);
}

TEST_CASE("wall terrain termination cannot bypass collisions or payment")
{
	for (bool blocked : {false, true})
	{
		CombatWorld w(kDefinitions);
		Object *hub = prepare(w);
		Player *owner = hub->getControllingPlayer();
		if (blocked)
		{
			w.unit("TerrainWallTile", 'B', 445, 500);
		}
		else
		{
			owner->getMoney()->withdraw(owner->getMoney()->countMoney() - 86, false);
		}
		const auto money = owner->getMoney()->countMoney();
		std::vector<ObjectID> made;
		CHECK_FALSE(WallSpan::build(*w.logic, *hub, {300,500,0}, {700,500,0}, *owner, 0, &made));
		CHECK(made.empty());
		CHECK(owner->getMoney()->countMoney() == money);
	}
}

TEST_CASE("wall terrain termination refuses a missing cap or terrain at the first tile")
{
	for (bool missingCap : {false, true})
	{
		std::string definitions = kDefinitions;
		if (missingCap)
		{
			const std::string field = " CliffCapTemplateName = TerrainWallEnd\n";
			definitions.erase(definitions.find(field), field.size());
		}
		CombatWorld w(definitions.c_str());
		Object *hub = prepare(w);
		if (!missingCap)
		{
			for (int y = 0; y < 100; ++y)
				for (int x = 30; x < 45; ++x) w.terrain.cliff.insert({x, y});
		}
		CHECK_FALSE(WallSpan::build(*w.logic, *hub, {300,500,0}, {700,500,0}, *hub->getControllingPlayer(), 0, nullptr));
	}
}
