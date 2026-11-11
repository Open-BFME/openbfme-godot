// OpenBFME. GPL-3.0. PORT-4: synthetic INI, no retail installs.
// Expectations: RW 0x6CCC59..0x6CCC9F; independent HitPercentage RW 0x6CCCA7.
#include "doctest.h"
#include "CombatTestUtil.h"
#include "GameLogic/Combat/CombatNames.h"
#include <string>
#include <vector>

using namespace combattest;
namespace
{
struct WallMeshes : PathfindWallMeshSource
{
	static PathfindWallSurface quad(float x0, float x1, float z0, float z1)
	{
		PathfindWallSurface s;
		PathfindWallTriangle a, b;
		a.v[0] = {x0, 250, z0}; a.v[1] = {x1, 250, z1}; a.v[2] = {x1, 350, z1};
		b.v[0] = {x0, 250, z0}; b.v[1] = {x1, 350, z1}; b.v[2] = {x0, 350, z0};
		s.triangles = {a, b}; s.loX = x0; s.hiX = x1; s.loY = 250; s.hiY = 350;
		s.height = z1;
		return s;
	}
	bool wallGeometry(const Object &o, PathfindWallGeometry &out) override
	{
		if (o.getTemplate()->getName() != "Port4Wall") return false;
		out.walls.push_back(quad(350, 450, 60, 60));
		out.ramps.push_back(quad(300, 350, 0, 60));
		return true;
	}
};
const char objects[] = R"ini(
Object Port4Wall
  KindOf = STRUCTURE IMMOBILE WALK_ON_TOP_OF_WALL
  Geometry = BOX
  GeometryMajorRadius = 50
  GeometryMinorRadius = 50
  GeometryHeight = 60
  ArmorSet
    Conditions = None
    Armor = NoArmor
  End
  Body = ActiveBody Body
    MaxHealth = 1000
  End
End
)ini";
struct Shot
{
	std::vector<int> draws;
	float health;
};
// Construct a real AIWorld wall grid using synthetic world-space meshes, not a mocked wall answer.
Shot fire(bool disable, float x, bool hitRoll = false, bool infantry = true, bool radius = true, bool wallEdge = false)
{
	WallMeshes meshes;
	CombatWorld w(objects);
	w.combat().setAutoAcquireEnabled(false);
	w.ai->setWallMeshes(&meshes);
	w.unit("Port4Wall", 'B', 400, 300);
	Object *source = w.unit("Dummy", 'A', 200, 300);
	Object *victim = w.unit(infantry ? "Dummy" : "Port4Wall", 'B', x, 300);
	Coord3D pos{x, 300, x == 400 ? 60.0f : 0.0f};
	victim->setPosition(&pos);
	w.buildMap();
	if (wallEdge)
	{
		// RW 0x6CCC8C passes 0: even a non-CLEAR wall cell qualifies (0x6EA876..0x6EA87F).
		PathfindCell *cell = w.ai->pathfinder().getCell(LAYER_GROUND, 40, 30);
		REQUIRE(cell != nullptr);
		cell->setType(PathfindCell::CELL_WALL_EDGE);
		REQUIRE_FALSE(w.ai->pathfinder().isPointOnWall(pos, true));
	}
	REQUIRE(w.ai->pathfinder().isPointOnWall(pos, false) == (x == 400));
	if (infantry) REQUIRE(victim->isKindOf((unsigned)CombatNames::kinds().infantry));
	std::string ini = "Weapon Port4Shot\n AttackRange = 1000\n ScatterRadius = 40\n ScatterRadiusVsInfantry = ";
	ini += radius ? "40\n" : "0\n";
	ini += " DisableScatterForTargetsOnWall = "; ini += disable ? "Yes\n" : "No\n";
	ini += hitRoll ? " HitPercentage = 0%\n" : " HitPercentage = 100%\n";
	ini += " DamageNugget\n Damage = 10\n Radius = 0\n DamageType = PIERCE\n DeathType = NORMAL\n End\nEnd\n";
	REQUIRE(w.w.load(ini, INI_LOAD_OVERWRITE, "port4-weapon.ini").empty());
	const WeaponTemplate *weapon = TheWeaponStore->findWeaponTemplate("Port4Shot");
	REQUIRE(weapon != nullptr);
	CHECK(weapon->m_disableScatterForTargetsOnWall == disable);
	w.logic->random().enableCallLog(true);
	const float before = w.health(victim);
	ObjectWeapons::createAndFireTempWeaponAt(weapon, source, *victim);
	Shot result;
	for (const GameLogicRandom::Call &c : w.logic->random().callLog())
		if (c.file == "Weapon.cpp") result.draws.push_back(c.line);
	result.health = before - w.health(victim);
	return result;
}
}

TEST_CASE("PORT-4 wall infantry: flag suppresses infantry scatter only on a wall")
{
	const Shot on = fire(true, 400);
	CHECK(on.draws == std::vector<int>{1749}); // aim only: no scatter radius or angle draw
	CHECK(on.health == 8.0f); // Dummy PlainArmor: PIERCE 80%
	const Shot off = fire(false, 400);
	CHECK(off.draws == std::vector<int>{1749, 1625, 1639});
	CHECK(off.health == 0.0f);
}

TEST_CASE("PORT-4 wall edge: the weapon query does not require a CLEAR cell")
{
	const Shot s = fire(true, 400, false, true, true, true);
	CHECK(s.draws == std::vector<int>{1749});
	CHECK(s.health == 8.0f);
}
